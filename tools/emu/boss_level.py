# SPDX-License-Identifier: GPL-3.0-or-later
"""Which MonLvl level a champion's, unique's, superunique's and minion's stats come from, by game.exe 1.14d's own code.

    uv run python boss_level.py gen [N] [SEED] > cases.tsv           # every row x difficulty x kind, then N random (default 100000)
    uv run python boss_level.py game cases.tsv > game.tsv            # game.exe, needs D2_MPQ_DIR
    build/tests/test_boss_level --boss-level cases.tsv > d2d.tsv     # d2d's real make_boss on the same cases
    uv run python boss_level.py compare cases.tsv game.tsv d2d.tsv   # mismatches by field, kind and mods

A case: `id monstats_id row difficulty kind mods seed [level_id]`; kind c champion (mods
holds its kind: 16, 36..39), u unique, s superunique, m minion, p plain; seed is the
monster's unit seed (masked to 32 bits). With a level_id (the area case,
`gen-area N [SEED]`) FUN_0061dca0 runs for real and FUN_0061a1b0 hands it that
Levels.txt id: an expansion Nightmare / Hell unit starts at the area's
MonLvl2Ex / MonLvl3Ex unless its row is noRatio or boss (FUN_00573cb0 0x573e18).
Without one the area rule is hooked off (the case's MonStats level, as before).

Game side, every table loaded by game.exe (drlg.boot), on a synthetic unit:
1. FUN_00573cb0, the stat init: level (stat 12) from the MonStats row, life
   rolled on the unit seed (+0x20 = seed, +0x24 = 666; FUN_0045c3e0 run),
   defence and experience from FUN_006538a0 at THAT level.
2. FUN_005a2120 (the fixed mods 1..4 rndname, hpmultiply, light, leveladd,
   then the unit's own mod bytes, boss flag 1) for c / u / s; for m what its
   party callback does to a minion: the fixed mods' inits with flag 0 (the
   unique's own mods on its minions are not d2d's and not compared).
3. FUN_005a4f50 mode 4 (A1), what a hit reads: to-hit and A1 damage from
   FUN_006538a0 at the level stat 12 holds then.
Hooked (none is the claim; stdcall pops from each function's `ret`):
FUN_00573cb0's helpers FUN_005739d0, FUN_00573930 (player count 1, no
bonus), FUN_0063ee90, FUN_0061a1b0 / FUN_0061dca0 (the area level of an
expansion Nightmare / Hell game: the case's MonStats level instead; in an area
case FUN_0061a1b0 returns the case's level id and FUN_0061dca0 is not hooked),
FUN_00626d40, FUN_0063eef0, FUN_006251f0, FUN_00626e10, FUN_00536b20,
FUN_0063abd0, FUN_006438b0, the skills (FUN_0056deb0, FUN_006439f0,
FUN_00644340, FUN_0063ebc0), MonProp (FUN_00573470: none), FUN_005d6b60;
the stat getters / setters (FUN_00625480, FUN_006253b0, FUN_00627260,
FUN_006272b0, FUN_00627150: one dict a unit, the hit's list apart), max life
(FUN_00625d10 = stat 7), the party walk FUN_0058f380, teleport's and aura's
inits (FUN_005a1600, FUN_005a1650: no stat), mod 41's event FUN_005417d0;
in FUN_005a4f50 FUN_0056e050 (no bonus), FUN_00625760 / FUN_00627340 (the
list), FUN_006259b0 (1: no player scaling), FUN_00451f60 (elements: not
compared). Rows 0x137 / 0x138 (their own extra roll) are left out.

Output `id | lvl life256 def exp ar dmg_lo dmg_hi vel`: life is stat 7 in
256ths, def includes item_armor_percent, ar and damage include the to-hit
and damage percentages, and vel is what the mods add to stat
0x43 (FUN_00573cb0 sets it first).
"""
import csv
import os
import random
import sys

sys.path.insert(0, os.getcwd())

UPICK = (5, 6, 7, 8, 9, 17, 18, 25, 26, 27, 28, 29, 30)
CHAMP = (16, 36, 37, 38, 39)
FIELDS = "lvl life256 def exp ar dmg_lo dmg_hi vel".split()
SKIP_ROWS = (0x137, 0x138)


def s32(v): return v - (1 << 32) if v >> 31 else v


def monstats_ids():
    import emu
    text = emu.mpq_cache().read("data\\global\\excel\\monstats.txt")
    rows = list(csv.reader(text.decode("latin1").splitlines(), delimiter="\t"))
    return [r[0] for r in rows[1:] if r and r[0] and r[0] != "Expansion"]


class Game:
    def __init__(self, area=False):
        import drlg
        from unicorn.x86_const import UC_X86_REG_EDX
        e = self.e = drlg.boot()
        self.edx = UC_X86_REG_EDX
        self.stats, self.hit = {}, {}
        self.level_now = 0
        self.area_now = 0
        self.game = e.alloc(0x200)
        e.w32(self.game + 0x70, 1); e.w32(self.game + 0x74, 1)     # expansion, L- columns
        self.unit, self.data, self.list = e.alloc(0x100), e.alloc(0x40), e.alloc(0x40)
        tables = e.r32(0x744304)
        self.monstats, self.rows = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
        get = lambda e: self.stats.get((e.arg(0), e.arg(1)), 0) & 0xFFFFFFFF
        e.hook(0x625480, get, 3)
        e.hook(0x6253b0, get, 3)
        e.hook(0x627260, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), s32(e.arg(2))) or 0, 4)
        e.hook(0x6272b0, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), self.stats.get((e.arg(0), e.arg(1)), 0) + s32(e.arg(2))) or 0, 4)
        e.hook(0x627150, lambda e: self.hit.__setitem__(e.arg(1), self.hit.get(e.arg(1), 0) + s32(e.arg(2))) or 0, 4)
        e.hook(0x625d10, lambda e: self.stats.get((e.arg(0), 7), 0) & 0xFFFFFFFF, 1)
        e.hook(0x573930, self.players, 2)
        if area: e.hook(0x61a1b0, lambda e: self.area_now, 1)
        else: e.hook(0x61dca0, lambda e: self.level_now, 3)
        if not area: e.hook(0x61a1b0, lambda e: 0, 1)
        e.hook(0x625760, lambda e: self.list, 2)
        e.hook(0x6259b0, lambda e: 1, 1)
        for addr, n in ((0x5739d0, 0), (0x63ee90, 1), (0x626d40, 4), (0x63eef0, 3), (0x6251f0, 5), (0x626e10, 3),
                        (0x536b20, 0), (0x63abd0, 2), (0x6438b0, 1), (0x56deb0, 2), (0x6439f0, 0), (0x644340, 2), (0x63ebc0, 2),
                        (0x573470, 0), (0x5d6b60, 5), (0x58f380, 2), (0x5a1600, 1), (0x5a1650, 1), (0x5417d0, 3),
                        (0x56e050, 1), (0x627340, 1), (0x451f60, 0)):
            e.hook(addr, lambda e: 0, n)

    def players(self, e):
        out = e.mu.reg_read(self.edx)
        for off, value in ((0, 0), (4, 0), (8, 0), (0xc, 0), (0x10, 1)): e.w32(out + off, value)
        return 0

    def rec(self, row): return self.monstats + row * 0x1a8

    def level(self, row, diff): return self.e.r16(self.rec(row) + 0xaa + 2 * diff)

    def run(self, row, diff, kind, mods, seed, area=0):
        """Run the stock stat initializer and boss modifiers for one seeded unit."""
        e, u = self.e, self.unit
        e.mu.mem_write(u, b"\0" * 0x100); e.mu.mem_write(self.data, b"\0" * 0x40)
        e.mu.mem_write(self.game + 0x6d, bytes([diff]))
        e.w32(u, 1); e.w32(u + 4, row); e.w32(u + 0x14, self.data); e.w32(u + 0x80, self.game)
        e.w32(u + 0x20, seed); e.w32(u + 0x24, 666)
        e.w32(self.data, self.rec(row))
        self.stats.clear(); self.hit.clear()
        self.level_now = self.level(row, diff)
        self.area_now = area
        e.call(0x573cb0, self.game, 0, 0, regs={"esi": u})
        velocity = self.stats.get((u, 0x43), 0)
        flags = {"c": 0xd, "u": 0x9, "s": 0xb, "m": 0x11, "p": 0}[kind]
        e.mu.mem_write(self.data + 0x16, flags.to_bytes(2, "little"))
        if kind == "p": pass
        elif kind == "m":
            for mod in (1, 2, 3, 4):
                fn = e.r32(0x73c008 + 4 * mod)
                if fn: e.call(fn, 0, ecx=u, edx=mod)
        else:
            e.mu.mem_write(self.data + 0x1c, bytes(mods) + b"\0" * (9 - len(mods)))
            e.call(0x5a2120, self.game, u, 0)
        e.call(0x5a4f50, ecx=u, edx=4)
        g = lambda s: self.stats.get((u, s), 0)
        assert g(6) == g(7), (row, diff, kind, mods, seed)
        pct = lambda v, p: v + int(v * p / 100)
        return [g(0xc), g(7), pct(g(0x1f), g(0x10)), g(0xd), pct(self.hit.get(0x13, 0), g(0x77)),
                pct(self.hit.get(0x15, 0), g(0x19)), pct(self.hit.get(0x16, 0), g(0x19)), g(0x43) - velocity]


def gen(n, seed):
    import drlg
    e = drlg.boot()
    ids = monstats_ids()
    tables = e.r32(0x744304)
    monstats, count = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
    level = lambda row, diff: e.r16(monstats + row * 0x1a8 + 0xaa + 2 * diff)
    rows = [r for r in range(min(count, len(ids))) if r not in SKIP_ROWS]
    rng = random.Random(seed & 0xFFFFFFFF)
    unit_seed = lambda: rng.getrandbits(32) & 0xFFFFFFFF
    out = []
    for row in rows:
        for diff in range(3):
            if level(row, diff) <= 0: continue
            for m in CHAMP: out.append((row, diff, "c", (m,)))
            for kind in "usm": out.append((row, diff, kind, ()))
            for m in UPICK: out.append((row, diff, "u", (m,)))
    live = [(r, d) for r in rows for d in range(3) if level(r, d) > 0]
    for _ in range(n):
        row, diff = rng.choice(live)
        kind = rng.choice("cusm")
        if kind == "c": mods = (rng.choice(CHAMP),)
        elif kind == "m": mods = ()
        else: mods = tuple(rng.sample(UPICK, rng.randint(1, 1 + diff + (kind == "s") * 3)))
        out.append((row, diff, kind, mods))
    for i, (row, diff, kind, mods) in enumerate(out):
        print(i, ids[row], row, diff, kind, ".".join(map(str, mods)) or "-", unit_seed())


def gen_area(n, seed):
    """Plain monsters and every boss kind made in a level: every row x difficulty x kind
    with two random Levels.txt ids that have a MonLvl (or are off the table: 0, count + 1), then n random cases."""
    import drlg
    e = drlg.boot()
    ids = monstats_ids()
    tables = e.r32(0x744304)
    count = e.r32(tables + 0xa80)
    levels = e.r32(tables + 0xc5c)
    rows = [r for r in range(min(count, len(ids))) if r not in SKIP_ROWS]
    rng = random.Random(seed & 0xFFFFFFFF)
    unit_seed = lambda: rng.getrandbits(32) & 0xFFFFFFFF
    out = []

    base = e.r32(tables + 0xc58)
    # a level with no MonLvl in that difficulty makes no monsters (the unit would be level 0); ids 0 and
    # count + 1 are off the table (FUN_0061dca0 answers 1)
    areas = [[lid for lid in range(1, levels) if e.r16(base + lid * 0x220 + 0x16 + 2 * d) > 0] + [0, levels + 1] for d in range(3)]

    def make(row, diff, kind):
        if kind == "c": mods = (rng.choice(CHAMP),)
        elif kind in "pm": mods = ()
        else: mods = tuple(rng.sample(UPICK, rng.randint(1, 1 + diff + (kind == "s") * 3)))
        return (row, diff, kind, mods, rng.choice(areas[diff]))
    level = lambda row, diff: e.r16(e.r32(tables + 0xa78) + row * 0x1a8 + 0xaa + 2 * diff)
    live = [(r, d) for r in rows for d in range(3) if level(r, d) > 0]   # a row with no level in that difficulty is not made there
    for row, diff in live:
        for kind in "pcusm":
            for _ in range(2): out.append(make(row, diff, kind))
    for _ in range(n):
        row, diff = rng.choice(live)
        out.append(make(row, diff, rng.choice("pcusm")))
    for i, (row, diff, kind, mods, area) in enumerate(out):
        print(i, ids[row], row, diff, kind, ".".join(map(str, mods)) or "-", unit_seed(), area)


def game(path):
    """One emulator a process (drlg.boot is one shared machine): the hooks differ between
    the plain cases and the area cases, so a file holds one kind."""
    lines = [line.split() for line in open(path)]
    area = len(lines[0]) > 7 if lines else False
    if any((len(f) > 7) != area for f in lines):
        sys.exit("game: a cases file is all plain cases or all area cases")
    g = Game(area=area)
    for f in lines:
        i, _, row, diff, kind, mods, seed = f[:7]
        mods = tuple(int(m) for m in mods.split(".")) if mods != "-" else ()
        print(i, "|", *g.run(int(row), int(diff), kind, mods, int(seed) & 0xFFFFFFFF, int(f[7]) if area else 0))


def compare(cases_path, game_path, d2d_path):
    """Compare each requested case and fail on any mismatch or missing result."""
    with open(cases_path) as stream:
        cases = {fields[0]: fields[1:] for line in stream if (fields := line.split())}

    if not cases:
        sys.exit(f"{cases_path}: no cases")

    def read_rows(path, allow_no_type=False):
        """Read result rows by case ID while ignoring unrelated build output."""
        rows = {}
        with open(path) as stream:
            for line in stream:
                fields = line.split()
                if not fields or fields[1:2] != ["|"] or fields[0] not in cases:
                    continue
                case_id = fields[0]
                if case_id in rows:
                    sys.exit(f"{path}: duplicate answer for case {case_id}")
                values = fields[2:]
                if len(values) != len(FIELDS) and not (allow_no_type and values == ["no-type"]):
                    sys.exit(f"{path}: invalid answer for case {case_id}")
                rows[case_id] = values
        return rows

    want, got = read_rows(game_path), read_rows(d2d_path, allow_no_type=True)
    missing = cases.keys() - (want.keys() & got.keys())
    skipped = sum(row == ["no-type"] for row in got.values())
    mismatches, bad = 0, {}
    for case_id, original in want.items():
        port = got.get(case_id)
        if port is None or port == ["no-type"]:
            continue
        if original == port:
            continue
        mismatches += 1
        name, _, diff, kind, mods, *_ = cases[case_id]
        for field, a, b in zip(FIELDS, original, port, strict=True):
            if a != b:
                count, example = bad.get(field, (0, f"{name} diff {diff} kind {kind} mods {mods}: game {a} d2d {b}"))
                bad[field] = count + 1, example
    print(f"cases {len(cases)}, mismatching cases {mismatches}, no d2d row {skipped}, missing answers {len(missing)}")
    for field, (count, example) in sorted(bad.items()):
        print(f"  {field}: {count} mismatches, e.g. {example}")
    return int(bool(mismatches or missing or skipped))


def main():
    args = sys.argv[1:]
    if args[:1] == ["gen"]:
        gen(int(args[1]) if len(args) > 1 else 100000, int(args[2]) if len(args) > 2 else 20261101)
    elif args[:1] == ["gen-area"]:
        gen_area(int(args[1]) if len(args) > 1 else 100000, int(args[2]) if len(args) > 2 else 20261102)
    elif args[:1] == ["game"]:
        game(args[1])
    elif args[:1] == ["compare"]:
        return compare(args[1], args[2], args[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
