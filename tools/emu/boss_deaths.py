# SPDX-License-Identifier: GPL-3.0-or-later
"""A boss's death kills the monsters round it (MonUMod 22, then mod 21) as the oracle.

    uv run python boss_deaths.py

The handler table of superuniques.md "MonUMod 22", with Blood Raven's row
(class 0x10b, 0x5dfd90: undead only), run in game.exe's own code:
- dispatch: mod 22's event-1 hook (0x5a3250) on a death (mode 0) by MonStats
  class, with the handlers hooked to see which one runs;
- parameters: each handler with FUN_005dfbf0 hooked: radius, the delay's
  min and max, undead only; Radament's max from Missiles.txt row 347's Range
  (a fake data table with the real Range, and a control Range); Mephisto's
  missile (FUN_0059fa30 hooked);
- the area: FUN_005dfbf0 builds its search (FUN_0065a950 hooked), then the
  real FUN_0065aa40 judges each candidate (positions, rooms and town hooked);
- the pick and the delay: FUN_005dfbf0 over a given list (alignment, undead
  hooked) for real, its rand (FUN_0045c3e0) on the boss's seed, event 7
  (FUN_005417d0) and mod 21 (FUN_005a4850) hooked to see who gets them;
- mod 21's event-2 hook FUN_005a3aa0 per case (state, owner hooked).

Each part prints its lines, then `ok`, or what differs from WANT_* and a
nonzero exit. Needs D2_MPQ_DIR and D2_PATCH_INSTALLER (Missiles.txt).
"""
import csv
import io
import sys

import emu

DISPATCH = 0x5A3250
HANDLERS = {0x5DFE00: "andariel", 0x5DFE20: "radament", 0x5DFDB0: "mephisto", 0x5E0020: "izual",
            0x5DFD90: "bloodraven", 0x5E0040: "overseer", 0x5E0060: "ancients", 0x5E0070: "ubers"}
COMMON, MAKE_MISSILE, MISSILE_AT = 0x5DFBF0, 0x59FA30, 0x56EDE0
FIND_INIT, FIND_FILL, FIND_FREE, FIND_TEST = 0x65A950, 0x65AC70, 0x65AA00, 0x65AA40
ROOM_OF, IN_TOWN, PATH_X, PATH_Y = 0x620BB0, 0x61AB00, 0x6488C0, 0x648900
ALIGNMENT, UNDEAD, ADD_EVENT, ADD_MOD = 0x6259B0, 0x63E990, 0x5417D0, 0x5A4850
KILL_HOOK, HAS_STATE, OWNER, OWNER_KILL, DEATH_INIT, DEATH = 0x5A3AA0, 0x639DF0, 0x58F0D0, 0x5750E0, 0x5A7E60, 0x5A7C20
DATA_TABLES, MISSILE_ROW, MISSILE_RANGE = 0x744304, 0x1A4, 0x96   # sgptDataTables; Missiles.bin row size, +0x96 Range
RADAMENT_DEATH, MEPHISTO_DEATH = 347, 299

# MonStats class -> handler (#249's superuniques.md table, with 0x10b Blood Raven)
WANT_DISPATCH = {0x9C: "andariel", 0xE5: "radament", 0xF2: "mephisto", 0x100: "izual", 0x10B: "bloodraven",
                 0x1DF: "overseer", 0x21C: "ancients", 0x21D: "ancients", 0x21E: "ancients",
                 0x2C0: "ubers", 0x2C1: "ubers", 0x2C5: "ubers", 0xF3: None, 0x11C: None, 0x192: None}
# handler -> (radius, first frame, last frame, undead only); Radament's last frame is Range - 100 - 1 (at least 99)
WANT_AREA = {"andariel": (35, 1, 50, 0), "radament": (35, 40, 299, 0), "izual": (105, 1, 1, 0),
             "bloodraven": (35, 25, 124, 1), "overseer": (35, 25, 124, 0)}
FRAME = 1000
LIMIT = 100000                                   # instructions a call may run
BOSS_AT = (5000, 5000)


def missiles_range(e, row):
    """Missiles.txt row `row`'s Range, after checking the row is radamentdeath."""
    rows = [r for r in csv.DictReader(io.StringIO(e.mpq_file("data\\global\\excel\\Missiles.txt").decode("latin-1")), delimiter="\t")
            if r["Missile"] and r["Missile"] != "Expansion"]
    if rows[row]["Missile"].lower() != "radamentdeath":     # the control: the right table and row
        raise SystemExit(f"boss_deaths: Missiles.txt row {row} is {rows[row]['Missile']}")
    return int(rows[row]["Range"])


class Harness:
    """One emulator with the inputs hooked: positions, rooms, town, alignment, undead; events and mods recorded."""
    def __init__(self):
        """A fresh Emu, a game at FRAME, and the hooks that read this harness's tables."""
        e = self.e = emu.Emu()
        self.game = e.alloc(0x2000)
        e.view("game", self.game).frame = FRAME
        self.pos, self.town, self.align, self.undead = {}, set(), {}, set()
        self.state54, self.owner = set(), {}
        self.calls = []
        e.hook(PATH_X, lambda e: self.pos[e.arg(0)][0], 1)
        e.hook(PATH_Y, lambda e: self.pos[e.arg(0)][1], 1)
        e.hook(ROOM_OF, lambda e: (2 if e.arg(0) in self.town else 1), 1)   # a room "pointer": 2 a town room
        e.hook(IN_TOWN, lambda e: int(e.arg(0) == 2), 1)
        e.hook(ALIGNMENT, lambda e: self.align.get(e.arg(0), 2), 1)
        e.hook(UNDEAD, lambda e: int(e.arg(0) in self.undead), 1)
        e.hook(ADD_EVENT, lambda e: self.calls.append(("event", self.reg("edx"), e.arg(0), e.arg(1))), 4)
        e.hook(ADD_MOD, lambda e: self.calls.append(("mod", self.reg("edx"), e.arg(0), e.arg(1))), 2)

    def call(self, address, *args, **registers):
        """e.call with an instruction cap; a run the cap stops short of its return is an error."""
        result = self.e.call(address, *args, limit=LIMIT, **registers)
        if self.reg("eip") != emu.RET_MAGIC:
            raise SystemExit(f"boss_deaths: FAIL: {address:#x} stopped at {self.reg('eip'):#x}, {LIMIT} instructions in")
        return result

    def reg(self, name):
        """The named register's value (eax, edx, eip, ...)."""
        return self.e.mu.reg_read(getattr(emu.unicorn.x86_const, "UC_X86_REG_" + name.upper()))

    def unit(self, kind, cls, x=0, y=0, mode=1, flags=0x4 | 0x8, seed=(0x1234567, 666)):
        """A new unit of that type, class, subtile, mode, +0xc4 flags and seed; returns its address."""
        e = self.e
        address, path = e.alloc(0x100), e.alloc(0x40)
        unit = e.view("unit", address)
        unit.type, unit["class"], unit.mode, unit.path, unit.flags = kind, cls, mode, path, flags
        unit.seed_low, unit.seed_high = seed
        self.pos[path] = (x, y)
        if kind not in (0, 1, 3):                           # a static path: its subtile at +0xc / +0x10
            e.w32(path + 0xC, x)
            e.w32(path + 0x10, y)
        return address


def rand(seed, bound):
    """FUN_0045c3e0 on a unit's {low, high}: one step, then low % bound (low & bound - 1 for a power of 2)."""
    if bound <= 0:
        return seed, 0
    step = seed[0] * 0x6AC690C5 + seed[1]
    seed = (step & 0xFFFFFFFF, step >> 32 & 0xFFFFFFFF)
    return seed, seed[0] & (bound - 1) if bound & (bound - 1) == 0 else seed[0] % bound


def check(label, got, want):
    """Print a case and whether it matches WANT_*; 1 when it doesn't."""
    print(label, got, "" if got == want else f"  FAIL: expected {want}")
    return int(got != want)


def dispatch():
    """Mod 22's hook 0x5a3250 per class, dying and alive: which handler runs; the failures."""
    h, bad = Harness(), 0
    seen = []
    for address, name in HANDLERS.items():
        h.e.hook(address, lambda e, name=name: seen.append(name), 0)
    for cls, want in WANT_DISPATCH.items():
        for mode, expect in ((0, want), (1, None)):        # mode 1: alive, the control
            seen.clear()
            h.call(DISPATCH, 0, 0, ecx=h.game, edx=h.unit(1, cls, mode=mode))
            bad += check(f"dispatch: class {cls:#x} mode {mode}:", seen[0] if seen else None, expect)
    return bad


def parameters():
    """Each handler's FUN_005dfbf0 arguments and missiles, Radament with and without the real Range; the failures."""
    h, bad = Harness(), 0
    got = []
    h.e.hook(COMMON, lambda e: got.append((e.arg(0), e.arg(1), e.arg(2), e.arg(3))), 4)
    made = []
    h.e.hook(MISSILE_AT, lambda e: made.append(e.arg(2)), 5)
    h.e.hook(MAKE_MISSILE, lambda e: made.append((e.r32(h.reg("edx") + 0x10), e.r32(h.reg("edx") + 0x4C))), 0)
    tables = h.e.alloc(0xC00)
    rows = h.e.alloc(MISSILE_ROW * (RADAMENT_DEATH + 1))
    h.e.w32(tables + 0xB64, rows)
    h.e.w32(tables + 0xB6C, RADAMENT_DEATH + 1)
    h.e.w32(DATA_TABLES, tables)
    real_range = missiles_range(h.e, RADAMENT_DEATH)
    for name, address in ((name, address) for address, name in HANDLERS.items()):
        if name not in WANT_AREA:
            continue
        for missile_range in ((real_range, 150) if name == "radament" else (None,)):
            if missile_range is not None:
                h.e.mu.mem_write(rows + RADAMENT_DEATH * MISSILE_ROW + MISSILE_RANGE, missile_range.to_bytes(2, "little"))
            got.clear()
            made.clear()
            h.call(address, ecx=h.game, edx=h.unit(1, 0))
            radius, low, high, undead = got[0] if got else (None,) * 4
            area = (radius, low, high - 1, undead) if got else None
            want = WANT_AREA[name] if missile_range in (None, real_range) else (35, 40, 99, 0)   # control: Range 150 -> max 100
            bad += check(f"area: {name}{f' (Range {missile_range})' if missile_range else ''}:", area, want)
            if name == "radament":
                bad += check("  then the missile:", made, [RADAMENT_DEATH])
    made.clear()
    h.call(0x5DFDB0, ecx=h.game, edx=h.unit(1, 0xF2))
    bad += check("mephisto: missile, params [0x13]:", made, [(MEPHISTO_DEATH, 100)])
    return bad


def area():
    """FUN_005dfbf0's search as built, then the real FUN_0065aa40 on each candidate."""
    h, bad = Harness(), 0
    params = []

    def find_init(e):
        """FUN_0065a950's stand-in: keeps the search FUN_005dfbf0 built and empties the list."""
        params.append(e.read(e.arg(7), 0x38))
        e.w32(e.arg(1) + 0x18, 0)
        return 0
    h.e.hook(FIND_INIT, find_init, 8)
    h.e.hook(FIND_FILL, lambda e: 0, 1)
    h.e.hook(FIND_FREE, lambda e: 0, 1)
    boss = h.unit(1, 0x9C, *BOSS_AT, mode=0)              # mod 22's hook runs in mode 0
    h.call(COMMON, 35, 1, 51, 0, regs={"edi": h.game, "ebx": boss})
    if not params:
        return check("area: the search", None, "built")
    built = params[0]
    flags = int.from_bytes(built[0:4], "little")
    bad += check("area: flags, self, x, y, radius:", (hex(flags), *(int.from_bytes(built[at:at + 4], "little") for at in (8, 0xC, 0x10, 0x14))),
                 (hex(0x583), boss, *BOSS_AT, 35))
    search = h.e.put(built)
    x, y = BOSS_AT
    cases = [  # label, unit, kept
        ("35 east", h.unit(1, 0, x + 35, y), 1),
        ("36 east", h.unit(1, 0, x + 36, y), 0),
        ("21, 28 (35 by the root)", h.unit(1, 0, x + 21, y - 28), 1),
        ("25, 25 (35.4)", h.unit(1, 0, x + 25, y + 25), 0),
        ("dying (mode 0)", h.unit(1, 0, x + 1, y, mode=0), 0),
        ("dead (mode 12)", h.unit(1, 0, x + 1, y, mode=12), 0),
        ("no +0xc4 4", h.unit(1, 0, x + 1, y, flags=0x8), 0),
        ("no +0xc4 8", h.unit(1, 0, x + 1, y, flags=0x4), 0),
        ("in a town room", h.unit(1, 0, x + 1, y), 0),
        ("a player", h.unit(0, 0, x + 1, y), 1),
        ("an object", h.unit(2, 0, x + 1, y), 0),
        ("the boss (dying)", boss, 0),
    ]
    h.town.add(cases[8][1])
    for label, unit, want in cases:
        bad += check(f"area: {label}:", h.call(FIND_TEST, unit, search), want)
    h.e.view("unit", boss).mode = 1                        # self is tested only for a player: the mode keeps the boss out
    bad += check("area: the boss, were it alive:", h.call(FIND_TEST, boss, search), 1)
    return bad


def filler(listed, count):
    """FUN_0065ac70's stand-in: the list FUN_005dfbf0 walks (+4 the units, +0x18 their count)."""
    def fill(e):
        """Hand FUN_005dfbf0 the given units and their count."""
        e.w32(e.arg(0) + 4, listed)
        e.w32(e.arg(0) + 0x18, count)
        return 0
    return fill


def pick():
    """FUN_005dfbf0 over a given list: who gets event 7 and mod 21, and when."""
    bad = 0
    for radius, low, high, undead_only in ((35, 1, 51, 0), (35, 25, 125, 1), (105, 1, 2, 0)):
        h = Harness()
        boss = h.unit(1, 0x9C, *BOSS_AT, seed=(0x9E3779B9, 666))
        units = []
        for label, kind, cls, align, undead in (("evil", 1, 1, 0, False), ("evil undead", 1, 1, 0, True), ("neutral", 1, 1, 1, False),
                                                 ("good (a pet)", 1, 1, 2, True), ("a player", 0, 0, 0, False), ("diabloclone", 1, 0x14D, 0, True),
                                                 ("evil 2", 1, 2, 0, False)):
            unit = h.unit(kind, cls, *BOSS_AT)
            h.align[unit] = align
            if undead:
                h.undead.add(unit)
            units.append((label, unit, kind == 1 and align == 0 and cls != 0x14D and (undead or not undead_only)))
        listed = h.e.alloc(4 * len(units))
        for index, (_, unit, _) in enumerate(units):
            h.e.w32(listed + 4 * index, unit)

        h.e.hook(FIND_INIT, lambda e: 0, 8)
        h.e.hook(FIND_FILL, filler(listed, len(units)), 1)
        h.e.hook(FIND_FREE, lambda e: 0, 1)
        h.call(COMMON, radius, low, high, undead_only, regs={"edi": h.game, "ebx": boss})
        seed, want = (0x9E3779B9, 666), []
        for _, unit, chosen in units:
            if chosen:
                seed, roll = rand(seed, high - low)
                want += [("event", unit, 7, FRAME + low + roll), ("mod", unit, 21, 0)]
        names = {unit: label for label, unit, _ in units}
        print(f"pick: radius {radius}, {low}..{high}, undead {undead_only}:",
              ", ".join(f"{names[unit]} {kind} {what}{f' at +{when - FRAME}' if kind == 'event' else ''}" for kind, unit, what, when in h.calls))
        bad += check("  as expected:", h.calls == want, True)
    return bad


def kill_case(label, mode, state, owner_kind, want):
    """FUN_005a3aa0 on one monster: its mode, state 54 or not, an owner of that type or none."""
    h = Harness()
    unit = h.unit(1, 1, mode=mode)
    owner = h.unit(owner_kind, 0) if owner_kind is not None else 0
    seen = []
    h.e.hook(HAS_STATE, lambda e: int(state and e.arg(1) == 54), 2)
    h.e.hook(OWNER, lambda e: owner, 0)
    h.e.hook(OWNER_KILL, lambda e: seen.append(f"owner kill {e.arg(1)}" if h.reg("edx") == owner else "?"), 2)
    h.e.hook(DEATH_INIT, lambda e: seen.append(f"death {e.arg(1)}" if e.arg(0) == unit else "?"), 3)
    h.e.hook(DEATH, lambda e: seen.append("death"), 3)
    h.call(KILL_HOOK, 0, 0, ecx=h.game, edx=unit)
    seen += [f"event {what} at +{when - FRAME}" for kind, _, what, when in h.calls if kind == "event"]
    return check(f"mod 21: {label}:", seen, want)


def kill_hook():
    """Mod 21's event-2 hook, FUN_005a3aa0, case by case."""
    return sum(kill_case(*case) for case in (("alive", 1, False, None, ["death 0", "death"]),
                                             ("dead (mode 12)", 12, False, None, []),
                                             ("state 54", 1, True, None, ["event 7 at +3"]),
                                             ("owner a player", 1, False, 0, ["owner kill 1"]),
                                             ("owner a monster", 1, False, 1, ["death 0", "death"])))


def main():
    """Run every part; `ok` and 0, or the failure count and 1."""
    bad = dispatch() + parameters() + area() + pick() + kill_hook()
    print("ok" if not bad else f"FAIL: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:]:
        sys.exit("usage: boss_deaths.py")
    sys.exit(main())
