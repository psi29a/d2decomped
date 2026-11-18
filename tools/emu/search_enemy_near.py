# SPDX-License-Identifier: GPL-3.0-or-later
"""A click's own pick of the unit nearest it (Skills.txt SearchEnemyNear,
FUN_00467490, `.\\GAME\\Select.cpp` 0x301) as the oracle, run natively on
sk_tele_spot's synthetic rooms:

    uv run python search_enemy_near.py gen 20000 1 > cases.tsv
    uv run python search_enemy_near.py game cases.tsv > game.tsv
    ../../build/tests/test_search_enemy_near cases.tsv > d2d.tsv     # tests/test_search_enemy_near.cpp
    diff game.tsv d2d.tsv
    uv run python search_enemy_near.py inc 2000 7 > ../../tests/search_enemy_near_cases.inc

A case is `id kind town target cx cy n {type x y mode dead sel metal}`:
the skill (0 plain, 1 TargetCorpse: the row's +7 bit 1, 2 Iron Golem:
skill id 0x5a), whether the rooms are a town's (level 1), the target (an
index into the units, -1 none), the click's subtile, and the units of the
player's room in its list order: unit type, subtile, mode, unit +0xc4 bit
0x10000, whether the hit test FUN_00466870(unit, 0, 0, 0) passes and, for
an item, Items bitfield1 bit 1 (FUN_00629cc0). Unit 0 is the local player
(0x7a6a70). `game` prints `id k x y`: the index FUN_00467490 returned (-1
null) and the click's x, y as it leaves them (a target's subtile). d2d's
test prints the same lines from rules::search_enemy_near.

Runs natively: FUN_00467490, FUN_00464820 (a dead player or monster),
FUN_00643ce0 (the skill id), FUN_0065a950 / FUN_0065ac70 / FUN_0065aa00
(the unit finder), FUN_0065a6b0 and FUN_0065a710 (its rooms), FUN_0061ab00
-> FUN_0066bab0 -> FUN_006426a0 (a town room, mask 0x2000), FUN_0065aa40
(radius 9, the type and mode by mask 0x2003 / 0x3002, not the player),
LAB_00467470 (Iron Golem's: an item FUN_00629cc0 passes), FUN_006417f0
(the distance), FUN_006488c0 / FUN_00648900 and the static path's +0xc /
+0x10.
Hooked (not the claim): FUN_00620bb0 (the room: (2, 2)), FUN_00619790 (the
near list: 3 x 3 rooms), FUN_00466870 (the hit test: the case's),
FUN_00629cc0 (Items bitfield1 & 2: the case's), FUN_0040b430 / FUN_0040b480
(the finder list's memory: checked one alloc, one free a call).

Tags: V (the oracle's own run). U: every unit is in the player's room; a
unit in another near room (walked in the near list's order) is not
covered, nor a click more than a room from the player.
"""
import random
import sys

import sk_tele_spot as tele

SEARCH = 0x467490
PLAYER_GLOBAL = 0x7A6A70
MAX_UNITS = 8


class Oracle(tele.Oracle):
    def __init__(self):
        super().__init__()
        from unicorn.x86_const import UC_X86_REG_ECX
        e = self.e
        self.room = self.rooms[(2, 2)]
        self.here = self.room
        self.levels = {}
        for room in self.rooms.values():                 # FUN_0061ab00: room +0x10 -> +0x58 -> +0x1d0, the level id
            drlg_room, level = e.alloc(0x100), e.alloc(0x200)
            e.w32(room + 0x10, drlg_room)
            e.w32(drlg_room + 0x58, level)
            self.levels[room] = level
        self.row = e.alloc(0x40)                         # the Skills.txt row: word +0 the id, byte +7 TargetCorpse
        self.skill = e.alloc(0x10)                       # the skill: +0 its row
        e.w32(self.skill, self.row)
        self.xy = e.alloc(8)
        self.units = [(e.alloc(0x200), e.alloc(0x100)) for _ in range(MAX_UNITS)]
        self.sel, self.metal, self.hits = {}, {}, []
        e.hook(0x466870, lambda e: self.sel.get(e.mu.reg_read(UC_X86_REG_ECX), 0), 2)
        e.hook(0x629CC0, lambda e: self.metal.get(e.arg(0), 0), 1)
        self.live = 0                                    # the finder's list: one alloc, one free a call
        e.hook(0x40B430, self.allocate, 3)
        e.hook(0x40B480, self.free, 3)

    def allocate(self, e):
        from unicorn.x86_const import UC_X86_REG_EDX
        self.live += 1
        return e.alloc(e.mu.reg_read(UC_X86_REG_EDX))

    def free(self, e):
        self.live -= 1
        return 0

    def run(self, case):
        e = self.e
        cid, kind, town, target, cx, cy, units = case
        for level in self.levels.values():
            e.w32(level + 0x1D0, 1 if town else 2)
        e.mu.mem_write(self.row, (0x5A if kind == 2 else 0x46).to_bytes(2, "little"))
        e.mu.mem_write(self.row + 6, bytes([0x40, 1 if kind == 1 else 0]))
        e.w32(self.xy, cx)
        e.w32(self.xy + 4, cy)
        self.sel, self.metal = {}, {}
        head = 0
        for index in reversed(range(len(units))):
            kind_u, x, y, mode, dead, sel, metal = units[index]
            unit, upath = self.units[index]
            e.mu.mem_write(unit, bytes(0x200))
            view = e.view("unit", unit)
            view.type, view.mode, view.path = kind_u, mode, upath
            view.flags = 0x10000 if dead else 0
            e.w32(unit + 4, 100 + index)                         # its class: FUN_00629cc0's key
            e.w32(unit + 0xE8, head)                             # the room list's next
            head = unit
            pv = e.view("path", upath)
            pv.x, pv.y = x, y
            e.w32(upath + 0xC, x)                                # a static path's (types 2, 4, 5)
            e.w32(upath + 0x10, y)
            self.sel[unit], self.metal[100 + index] = int(sel), int(metal)
        for room in self.rooms.values():
            e.w32(room + 0x74, head if room == self.room else 0)
        e.w32(PLAYER_GLOBAL, self.units[0][0])
        target_ptr = self.units[target][0] if target >= 0 else 0
        found = e.call(SEARCH, self.skill, target_ptr, self.xy, self.xy + 4, regs={"eax": self.row}) & 0xFFFFFFFF
        index = next((k for k, (unit, _) in enumerate(self.units[:len(units)]) if unit == found), -1)
        if found and index < 0:
            raise AssertionError(f"case {cid}: FUN_00467490 returned {found:#x}, not a case unit")
        if self.live:
            raise AssertionError(f"case {cid}: the finder's list left {self.live} allocations")
        return index, e.s32(self.xy), e.s32(self.xy + 4)


def make_case(cid, rng):
    kind = rng.choice((0, 0, 1, 1, 2))
    town = int(rng.random() < 0.1)
    px, py = rng.randint(85, 114), rng.randint(85, 114)
    cx, cy = px + rng.randint(-12, 12), py + rng.randint(-12, 12)
    units = [(0, px, py, rng.choice((1, 1, 1, 2, 0, 0x11)), 0, int(rng.random() < 0.3), 0)]   # the local player
    for _ in range(rng.randint(0, MAX_UNITS - 1)):
        reach = rng.choice((3, 6, 10, 12))
        x, y = cx + rng.randint(-reach, reach), cy + rng.randint(-reach, reach)
        unit_type = rng.choice((1, 1, 1, 1, 0, 4, 4, 2, 3, 5))
        mode = rng.choice((1, 1, 2, 3, 0, 0xC, 0xC, 0x11))
        units.append((unit_type, x, y, mode, int(rng.random() < 0.15), int(rng.random() < 0.75), int(rng.random() < 0.5)))
    target = -1
    if rng.random() < 0.35 and len(units) > 1:
        target = rng.randrange(len(units))
    return cid, kind, town, target, cx, cy, units


def show(case):
    cid, kind, town, target, cx, cy, units = case
    parts = [cid, kind, town, target, cx, cy, len(units)]
    for unit in units:
        parts += list(unit)
    return " ".join(str(p) for p in parts)


def parse(line):
    it = iter(int(v) for v in line.split())
    cid, kind, town, target, cx, cy = (next(it) for _ in range(6))
    units = [tuple(next(it) for _ in range(7)) for _ in range(next(it))]
    return cid, kind, town, target, cx, cy, units


def gen(count, seed):
    rng = random.Random(seed)
    return [make_case(cid, rng) for cid in range(count)]


def inc(count, seed):
    oracle = Oracle()
    print("// Stock game.exe 1.14d, FUN_00467490; generated by tools/emu/search_enemy_near.py "
          f"inc {count} {seed}.")
    print("// kind, town, target, click x, y, units {type, x, y, mode, dead, sel, metal} (unit 0 the player),"
          " the pick (-1 none), the click x, y after")
    for case in gen(count, seed):
        cid, kind, town, target, cx, cy, units = case
        pick, x, y = oracle.run(case)
        unit_text = ", ".join("{ " + ", ".join(str(v) for v in u) + " }" for u in units)
        print(f"    {{ {kind}, {town}, {target}, {cx}, {cy}, {len(units)}, {{ {{ {unit_text} }} }}, {pick}, {x}, {y} }},")


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
                    pick, x, y = oracle.run(case)
                    print(case[0], pick, x, y)
    elif mode == "inc":
        inc(int(argv[2]), int(argv[3]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
