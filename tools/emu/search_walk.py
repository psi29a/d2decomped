# SPDX-License-Identifier: GPL-3.0-or-later
"""The order FUN_00641cb0 walks units in across the near rooms, as the oracle, seen by a
shift-click's pick (SearchEnemyXY, FUN_00467660):

    export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
    uv run python search_walk.py gen 20000 1 > cases.tsv
    uv run python search_walk.py game cases.tsv > game.tsv
    ../../build/tests/test_search_walk cases.tsv > d2d.tsv          # tests/test_search_walk.cpp
    diff game.tsv d2d.tsv
    uv run python search_walk.py inc 2000 7 > ../../tests/search_walk_cases.inc
    uv run python search_walk.py --mutation-check

search_enemy_xy.py puts every unit in the player's room; here the level is 5 x 5
rooms of 40 x 40 subtiles (sk_tele_spot.py's world), the player stands near the
room edges, and a unit is listed in the room its subtile is in. A case is
`id dir px py tonly n {x y size type mode flag4 shiftsel interact stamp} m {x y bits}`:
as search_enemy_xy.py's, with each unit's `stamp`: a room's list runs by stamp,
highest first (room +0x74, next +0xe8; equal stamps, the later unit first), as
d2d keeps its units (UnitState::room_stamp, rules::room_order). The near list of
the player's room is the rooms touching it, in sk_tele_spot.py's order (x first).
`game` prints `id k`, the index of the unit FUN_00467660 returned, -1 none.

Runs natively: FUN_00467660, FUN_00641cb0 (the near list's rooms in order, each
room's list from its head, FUN_00641930's near test, the footprint by size and mode),
FUN_0064d9b0. Hooked as search_enemy_xy.py lists (the facing, the room, the
near list, the grid, size, shiftSel, interact, the target set). Not the claim:
the order inside a room (the case's stamps), and the near list's own order
on a real level (d2d's near_rooms).
"""
import argparse
import random
import sys

import search_enemy_xy as xy

ROOM, GRID = 40, 5
MUT = set()


def room_of(x, y):
    """The room a subtile is in, as the room id the d2d test uses: x room * 5 + y room."""
    return (x // ROOM) * GRID + y // ROOM


def near_ids(px, py):
    """The player's room's near list in sk_tele_spot's order: the rooms touching it, x first."""
    i, j = px // ROOM, py // ROOM
    return [a * GRID + b for a in range(i - 1, i + 2) for b in range(j - 1, j + 2) if 0 <= a < GRID and 0 <= b < GRID]


def model(case):
    """search_enemy_xy over the walk: the units in the near rooms' order, each room's by stamp; the first index pick."""
    _, direction, px, py, tonly, units, words = case
    order = []
    rooms = near_ids(px, py)
    if "view_order" in MUT:
        order = list(range(len(units)))
    else:
        for room in rooms:
            members = [k for k, unit in enumerate(units) if room_of(unit[0], unit[1]) == room]
            members.sort(key=lambda k: (-units[k][8], -k) if "tie" not in MUT else (-units[k][8], k))
            order += members
    return xy_pick(direction, px, py, tonly, [units[k][:8] for k in order], words, order)


def xy_pick(direction, px, py, tonly, units, words, order):
    """rules::search_enemy_xy re-stated over `units` in walk order: the pick's original index, or -1."""
    eighth = (direction & 0xFF) >> 3
    for turn in (0, -1, 1, -2, 2):
        way = (turn + eighth) & 7
        sx, sy = px + xy.DIRS_X[way], py + xy.DIRS_Y[way]
        if not words.get((sx, sy), 0) & 0x180:
            continue
        for k, (x, y, size, kind, mode, flag4, shift_sel, interact) in enumerate(units):
            if kind != 1 or not covers(x, y, size, kind, mode, sx, sy):
                continue
            if flag4 and (not tonly or shift_sel or interact):
                return order[k]
            break
    return -1


def covers(x, y, size, kind, mode, sx, sy):
    """rules::search_covers re-stated: a monster not in mode 0 / 0xc, its footprint by size."""
    if kind != 1 or mode in (0, 0xC):
        return False
    size = min(size, 3)
    across, down = abs(sx - x), abs(sy - y)
    if size < 1:
        return False
    if size == 1:
        return across == 0 and down == 0
    if size == 2:
        return across + down <= 1
    return across <= 1 and down <= 1


class Oracle(xy.Oracle):
    """search_enemy_xy.py's oracle with the player's room by its subtile and the units in the rooms they stand in."""

    def __init__(self):
        super().__init__()
        self.e.hook(0x620BB0, lambda e: self.here, 1)

    def run(self, case):
        """Run FUN_00467660 on one case; the index of the unit it returned, -1 none."""
        e = self.e
        cid, direction, px, py, tonly, units, words = case
        self.load(words)
        self.dir = direction
        self.here = self.rooms[(px // ROOM, py // ROOM)]
        path = e.view("path", self.player_path)
        path.x, path.y = px, py
        e.mu.mem_write(self.skill + 6, bytes([0x10 if tonly else 0]))
        self.size, self.shift_sel, self.interact, self.set = {}, {}, {}, None
        for index, (x, y, size, kind, mode, flag4, shift_sel, interact, _) in enumerate(units):
            unit, upath = self.units[index]
            view = e.view("unit", unit)
            view.type, view.mode, view.path = kind, mode, upath
            view.flags = 4 if flag4 else 0
            e.w32(unit + 4, index)
            pv = e.view("path", upath)
            pv.x, pv.y = x, y
            e.w32(upath + 0xC, x)
            e.w32(upath + 0x10, y)
            self.size[unit], self.shift_sel[index], self.interact[unit] = size, int(shift_sel), int(interact)
        for (i, j), room in self.rooms.items():
            members = [k for k, unit in enumerate(units) if unit[0] // ROOM == i and unit[1] // ROOM == j]
            members.sort(key=lambda k: (-units[k][8], -k))
            head = 0
            for k in reversed(members):
                e.w32(self.units[k][0] + 0xE8, head)
                head = self.units[k][0]
            e.w32(room + 0x74, head)
        found = e.call(xy.SEARCH, self.skill, regs={"ebx": self.player}) & 0xFFFFFFFF
        index = next((k for k, (unit, _) in enumerate(self.units[:len(units)]) if unit == found), -1)
        if found and index < 0:
            raise AssertionError(f"case {cid}: FUN_00467660 returned {found:#x}, not a case unit")
        return index


def make_case(cid, rng):
    """A player near a room edge, units on and about the five spots, in either side of the edge."""
    edge = rng.choice((80, 120, 40, 160))
    px, py = edge + rng.randint(-7, 7), edge + rng.randint(-7, 7)
    if rng.random() < 0.5:
        px = rng.randint(70, 130)
    direction = rng.randrange(64)
    eighth = direction >> 3
    spots = [(px + xy.DIRS_X[(t + eighth) & 7], py + xy.DIRS_Y[(t + eighth) & 7]) for t in (0, -1, 1, -2, 2)]
    units = []
    for _ in range(rng.randint(0, xy.MAX_UNITS)):
        sx, sy = rng.choice(spots)
        x, y = sx + rng.choice((0, 0, 0, -1, 1)), sy + rng.choice((0, 0, 0, -1, 1))
        if rng.random() < 0.2:
            x, y = px + rng.randint(-6, 6), py + rng.randint(-6, 6)
        units.append((x, y, rng.choice((1, 2, 2, 3, 3, 3)), rng.choice((1, 1, 1, 1, 1, 0, 3)), rng.choice((1, 1, 1, 2, 0, 0xC)),
                      rng.random() < 0.85, rng.random() < 0.3, rng.random() < 0.7, rng.randint(0, 4)))
    words = {}
    for x, y, size, kind, *_ in units:
        if rng.random() < 0.85:
            for wx, wy, bit in xy.stamp(x, y, min(size, 3), 0x80 if kind == 0 else 0x100):
                words[(wx, wy)] = words.get((wx, wy), 0) | bit
    for spot in spots:
        if rng.random() < 0.3:
            words[spot] = words.get(spot, 0) | 0x100
    words = {key: value for key, value in words.items() if 0 <= key[0] < ROOM * GRID and 0 <= key[1] < ROOM * GRID}
    return cid, direction, px, py, int(rng.random() < 0.5), units, words


def show(case):
    """A case as one line."""
    cid, direction, px, py, tonly, units, words = case
    parts = [cid, direction, px, py, tonly, len(units)]
    for unit in units:
        parts += [int(v) for v in unit]
    parts.append(len(words))
    for (x, y), bits in words.items():
        parts += [x, y, bits]
    return " ".join(str(p) for p in parts)


def parse(line):
    """One line back to a case."""
    it = iter(int(v) for v in line.split())
    cid, direction, px, py, tonly = (next(it) for _ in range(5))
    units = [tuple(next(it) for _ in range(9)) for _ in range(next(it))]
    words = {}
    for _ in range(next(it)):
        x, y, bits = next(it), next(it), next(it)
        words[(x, y)] = bits
    return cid, direction, px, py, tonly, units, words


def gen(count, seed):
    """`count` cases from `seed`."""
    rng = random.Random(seed)
    return [make_case(cid, rng) for cid in range(count)]


def check(count=6000):
    """Run game.exe and the model over `count` cases; the number that differ."""
    oracle = Oracle()
    bad = 0
    for case in gen(count, 11):
        got, want = oracle.run(case), model(case)
        if got != want:
            bad += 1
            if bad <= 10:
                print(f"case {case[0]}: game {got} model {want}", file=sys.stderr)
    return bad


def main(argv):
    """gen / game / inc, or the check with --mutation-check."""
    if len(argv) > 1 and argv[1] == "gen":
        for case in gen(int(argv[2]), int(argv[3])):
            print(show(case))
        return 0
    if len(argv) > 1 and argv[1] == "game":
        oracle = Oracle()
        with open(argv[2], encoding="utf-8") as cases:
            for line in cases:
                if line.strip():
                    case = parse(line)
                    print(f"{case[0]} {oracle.run(case)}")
        return 0
    if len(argv) > 1 and argv[1] == "inc":
        oracle = Oracle()
        print("// Stock game.exe 1.14d, FUN_00467660 over FUN_00641cb0's walk; generated by tools/emu/search_walk.py "
              f"inc {argv[2]} {argv[3]}.")
        print("// dir, player x, y, TargetableOnly, units {x, y, size, type, mode, flag4, shiftSel, interact, stamp}, "
              "collision words {x, y, bits}, the pick (-1 none)")
        for case in gen(int(argv[2]), int(argv[3])):
            _, direction, px, py, tonly, units, words = case
            pick = oracle.run(case)
            unit_text = ", ".join("{ " + ", ".join(str(int(v)) for v in u) + " }" for u in units)
            word_text = ", ".join(f"{{ {x}, {y}, 0x{bits:x} }}" for (x, y), bits in words.items())
            unit_text = f"{{ {{ {unit_text} }} }}" if units else "{}"
            word_text = f"{{ {{ {word_text} }} }}" if words else "{}"
            print(f"    {{ {direction}, {px}, {py}, {tonly}, {len(units)}, {unit_text}, {len(words)}, {word_text}, {pick} }},")
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args(argv[1:])
    bad = check()
    if bad:
        print(f"FAIL: {bad} differ", file=sys.stderr)
        return 1
    print("6000 cases, 0 differ")
    if args.mutation_check:
        for name in ("view_order", "tie"):
            MUT.clear()
            MUT.add(name)
            missed = check()
            MUT.clear()
            if not missed:
                print(f"mutation {name}: NOT caught", file=sys.stderr)
                return 1
            print(f"mutation {name}: caught ({missed} differ)")
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
