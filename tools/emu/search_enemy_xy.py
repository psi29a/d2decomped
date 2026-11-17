# SPDX-License-Identifier: GPL-3.0-or-later
"""A shift-click's own pick of a monster beside the player (Skills.txt
SearchEnemyXY, FUN_00467660) as the oracle, run natively on sk_tele_spot's
synthetic rooms:

    uv run python search_enemy_xy.py gen 20000 1 > cases.tsv
    uv run python search_enemy_xy.py game cases.tsv > game.tsv
    ../../build/tests/test_search_enemy_xy cases.tsv > d2d.tsv     # tests/test_search_enemy_xy.cpp
    diff game.tsv d2d.tsv
    uv run python search_enemy_xy.py inc 2000 7 > ../../tests/search_enemy_xy_cases.inc

A case is `id dir px py tonly n {x y size type mode flag4 shiftsel interact}
m {x y bits}`: the player's facing (0..63) and subtile, TargetableOnly (the
skill row's +6 bit 0x10), the units of the player's room in its list order,
and the non-zero collision words. `game` prints `id k`: the index of the
unit FUN_00467660 returned, -1 for none. d2d's test prints the same lines
from rules::search_enemy_xy.

Runs natively: FUN_00467660 (the five spots, 0x6d6b34 / 0x6d6b14 /
0x6d6af4), FUN_0064d9b0 -> FUN_0064d450 (the collision word & 0x180),
FUN_00641cb0 (the room walk, the mode filter, the footprint by size),
FUN_00641930 and LAB_00467640 (a monster only).
Hooked (not the claim): FUN_006238c0 (the facing: the case's), FUN_00620bb0
(the room: (2, 2)), FUN_00619790 (the near list: 3 x 3 rooms), FUN_0061a010
(the grid), FUN_00620510 (a unit's size: the case's), FUN_004638a0 (MonStats2
shiftSel: the case's), FUN_00465c60 (may the player interact with it: the
case's), FUN_00620c10 (the target set: recorded, checked against the
answer).

Tags: V (the oracle's own run). U: every unit is in the player's room; a
unit in another near room (walked in the near list's order) is not
covered.
"""
import random
import sys

import sk_tele_spot as tele

SEARCH = 0x467660
DIRS_X = (0, -2, -3, -2, 0, 2, 3, 2)
DIRS_Y = (3, 2, 0, -2, -3, -2, 0, 2)
MAX_UNITS, MAX_WORDS = 6, 12


class Oracle(tele.Oracle):
    def __init__(self):
        super().__init__()
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX
        e = self.e
        self.room = self.rooms[(2, 2)]
        self.here = self.room
        self.player, self.player_path = e.alloc(0x200), e.alloc(0x200)
        e.view("unit", self.player).type = 0
        e.view("unit", self.player).path = self.player_path
        self.skill = e.alloc(0x40)
        self.units = [(e.alloc(0x200), e.alloc(0x200)) for _ in range(MAX_UNITS)]
        self.dir, self.size, self.shift_sel, self.interact, self.set = 0, {}, {}, {}, None
        e.hook(0x6238C0, lambda e: self.dir, 1)
        e.hook(0x620510, lambda e: self.size.get(e.arg(0), 0), 1)
        e.hook(0x4638A0, lambda e: self.shift_sel.get(e.mu.reg_read(UC_X86_REG_ECX), 0)
               if e.mu.reg_read(UC_X86_REG_EDX) == 6 else 0xDEAD, 0)
        e.hook(0x465C60, lambda e: self.interact.get(e.mu.reg_read(UC_X86_REG_EDX), 0), 0)
        e.hook(0x620C10, self.target_set, 2)

    def target_set(self, e):
        self.set = (e.arg(0), e.arg(1))
        return 0

    def run(self, case):
        e = self.e
        cid, direction, px, py, tonly, units, words = case
        self.load(words)
        self.dir = direction
        path = e.view("path", self.player_path)
        path.x, path.y = px, py
        e.mu.mem_write(self.skill + 6, bytes([0x10 if tonly else 0]))
        self.size, self.shift_sel, self.interact, self.set = {}, {}, {}, None
        head = 0
        for index in reversed(range(len(units))):
            x, y, size, kind, mode, flag4, shift_sel, interact = units[index]
            unit, upath = self.units[index]
            view = e.view("unit", unit)
            view.type, view.mode, view.path = kind, mode, upath
            view.flags = 4 if flag4 else 0
            e.w32(unit + 4, index)                                  # its class: shiftSel's key
            e.w32(unit + 0xE8, head)                                # the room list's next
            head = unit
            pv = e.view("path", upath)
            pv.x, pv.y = x, y
            e.w32(upath + 0xC, x)                                   # a static path's (types 2, 4, 5)
            e.w32(upath + 0x10, y)
            self.size[unit], self.shift_sel[index], self.interact[unit] = size, int(shift_sel), int(interact)
        for room in self.rooms.values():
            e.w32(room + 0x74, head if room == self.room else 0)
        found = e.call(SEARCH, self.skill, regs={"ebx": self.player}) & 0xFFFFFFFF
        index = next((k for k, (unit, _) in enumerate(self.units[:len(units)]) if unit == found), -1)
        if found and index < 0:
            raise AssertionError(f"case {cid}: FUN_00467660 returned {found:#x}, not a case unit")
        if found and self.set != (self.player, found):
            raise AssertionError(f"case {cid}: FUN_00620c10 got {self.set}, not the pick")
        if not found and self.set is not None:
            raise AssertionError(f"case {cid}: FUN_00620c10 called with no pick")
        return index


def stamp(x, y, size, bit):
    """A unit's own collision bit, as FUN_0064ea90 lays it: SizeX 3 the 3 x 3, else the plus, 0 nothing."""
    if size == 0:
        return []
    if size == 3:
        return [(x + a, y + b, bit) for a in (-1, 0, 1) for b in (-1, 0, 1)]
    return [(x + a, y + b, bit) for a, b in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1))]


def make_case(cid, rng):
    px, py = rng.randint(92, 107), rng.randint(92, 107)
    direction = rng.randrange(64)
    eighth = direction >> 3
    spots = [(px + DIRS_X[(t + eighth) & 7], py + DIRS_Y[(t + eighth) & 7]) for t in (0, -1, 1, -2, 2)]
    others = [(px + DIRS_X[k], py + DIRS_Y[k]) for k in range(8)]
    units = []
    for _ in range(rng.randint(0, MAX_UNITS)):
        if rng.random() < 0.75:
            sx, sy = rng.choice(spots if rng.random() < 0.8 else others)
            x, y = sx + rng.choice((0, 0, 0, -1, 1)), sy + rng.choice((0, 0, 0, -1, 1))
        else:
            x, y = px + rng.randint(-5, 5), py + rng.randint(-5, 5)
        kind = rng.choice((1, 1, 1, 1, 1, 1, 1, 0, 3, 2, 4))
        mode = rng.choice((1, 1, 1, 2, 3, 0, 0xC, 0x11))
        units.append((x, y, rng.choice((0, 1, 2, 2, 2, 3, 3, 4)), kind, mode,
                      rng.random() < 0.85, rng.random() < 0.3, rng.random() < 0.7))
    words = {}
    for x, y, size, kind, *_ in units:
        if rng.random() < 0.8:
            for wx, wy, bit in stamp(x, y, min(size, 3), 0x80 if kind == 0 else 0x100):
                words[(wx, wy)] = words.get((wx, wy), 0) | bit
    for _ in range(rng.randint(0, 3)):
        words[rng.choice(spots + others)] = rng.choice((0x80, 0x100, 0x180, 0x1, 0x1000, 0x101, 0x2000))
    if rng.random() < 0.2:                                       # clear a spot a unit stands on
        spot = rng.choice(spots)
        words.pop(spot, None)
    words = {key: value for key, value in words.items() if value}
    while len(words) > MAX_WORDS:
        words.pop(next(iter(words)))
    return cid, direction, px, py, int(rng.random() < 0.6), units, words


def show(case):
    cid, direction, px, py, tonly, units, words = case
    parts = [cid, direction, px, py, tonly, len(units)]
    for unit in units:
        parts += [int(v) for v in unit]
    parts.append(len(words))
    for (x, y), bits in words.items():
        parts += [x, y, bits]
    return " ".join(str(p) for p in parts)


def parse(line):
    it = iter(int(v) for v in line.split())
    cid, direction, px, py, tonly = (next(it) for _ in range(5))
    units = [tuple(next(it) for _ in range(8)) for _ in range(next(it))]
    words = {}
    for _ in range(next(it)):
        x, y, bits = next(it), next(it), next(it)
        words[(x, y)] = bits
    return cid, direction, px, py, tonly, units, words


def gen(count, seed):
    rng = random.Random(seed)
    return [make_case(cid, rng) for cid in range(count)]


def inc(count, seed):
    oracle = Oracle()
    print("// Stock game.exe 1.14d, FUN_00467660; generated by tools/emu/search_enemy_xy.py "
          f"inc {count} {seed}.")
    print("// dir, player x, y, TargetableOnly, units {x, y, size, type, mode, flag4, shiftSel, interact},"
          " collision words {x, y, bits}, the pick (-1 none)")
    for case in gen(count, seed):
        cid, direction, px, py, tonly, units, words = case
        pick = oracle.run(case)
        unit_text = ", ".join("{ " + ", ".join(str(int(v)) for v in u) + " }" for u in units)
        word_text = ", ".join(f"{{ {x}, {y}, 0x{bits:x} }}" for (x, y), bits in words.items())
        unit_text = f"{{ {{ {unit_text} }} }}" if units else "{}"      # std::array's own brace, then the C array's
        word_text = f"{{ {{ {word_text} }} }}" if words else "{}"
        print(f"    {{ {direction}, {px}, {py}, {tonly}, {len(units)}, {unit_text}, {len(words)}, {word_text}, {pick} }},")


def main(argv):
    mode = argv[1] if len(argv) > 1 else ""
    if mode == "gen":
        for case in gen(int(argv[2]), int(argv[3])):
            print(show(case))
    elif mode == "game":
        oracle = Oracle()
        with open(argv[2], encoding="utf-8") as cases:
            for line in cases:
                if line.strip():
                    case = parse(line)
                    print(f"{case[0]} {oracle.run(case)}")
    elif mode == "inc":
        inc(int(argv[2]), int(argv[3]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
