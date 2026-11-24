# SPDX-License-Identifier: GPL-3.0-or-later
"""The order the unit finder FUN_0065ac70 walks units in across the near rooms, as the oracle,
seen by a click's SearchEnemyNear pick (FUN_00467490):

    export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
    uv run python search_walk_near.py gen 20000 1 > cases.tsv
    uv run python search_walk_near.py game cases.tsv > game.tsv
    ../../build/tests/test_search_walk_near cases.tsv > d2d.tsv      # tests/test_search_walk_near.cpp
    diff game.tsv d2d.tsv
    uv run python search_walk_near.py inc 2000 7 > ../../tests/search_walk_near_cases.inc
    uv run python search_walk_near.py --mutation-check

search_enemy_near.py puts every unit in the player's room; here the level is 5 x 5 rooms of
40 x 40 subtiles (sk_tele_spot.py's world), the player (unit 0, the local player 0x7a6a70) stands
near the room edges, and a unit is listed in the room its subtile is in, each room's list by
stamp, highest first (equal: the later unit first), as d2d keeps its units (rules::room_order).
A case is `id kind town target cx cy n {type x y mode dead sel metal stamp}`: as
search_enemy_near.py's, with each unit's stamp. `game` prints `id k x y`: the index
FUN_00467490 returned (-1 null) and the click's x, y as it leaves them.

Runs natively and hooks as search_enemy_near.py (its docstring); here the finder walks the near
list of the player's room (the player's room by its subtile) and FUN_0065a6b0 / FUN_0065a710
choose the room alone or the list. Not the claim: the order inside a room (the case's stamps),
and the near list's order on a real level (d2d's near_rooms).
"""
import argparse
import random
import sys

import search_enemy_near as near

ROOM, GRID = 40, 5
MUT = set()


def room_of(x, y):
    """The room a subtile is in, as the room id the d2d test uses: x room * 5 + y room."""
    return (x // ROOM) * GRID + y // ROOM


def near_ids(px, py):
    """The player's room's near list in sk_tele_spot's order: the rooms touching it, x first."""
    i, j = px // ROOM, py // ROOM
    return [a * GRID + b for a in range(i - 1, i + 2) for b in range(j - 1, j + 2) if 0 <= a < GRID and 0 <= b < GRID]


def walk(units):
    """The units' indices in the finder's walk order: the player's near rooms, each room's list by stamp."""
    if "view_order" in MUT:
        return list(range(len(units)))
    order = []
    for room in near_ids(units[0][1], units[0][2]):
        members = [k for k, unit in enumerate(units) if room_of(unit[1], unit[2]) == room]
        members.sort(key=lambda k: (-units[k][7], -k) if "tie" not in MUT else (-units[k][7], k))
        order += members
    return order


def model(case):
    """rules::search_enemy_near over the walk re-stated for these cases: (pick, x, y)."""
    _, kind, town, target, cx, cy, units = case
    order = walk(units)
    spot_x, spot_y = cx, cy
    if target >= 0:
        aimed = units[target]
        dead = aimed[4] or (aimed[0] == 0 and aimed[3] in (0, 0x11)) or (aimed[0] == 1 and aimed[3] in (0, 0xC))
        if kind == 1 and dead:
            return target, spot_x, spot_y
        spot_x, spot_y = aimed[1], aimed[2]
        if kind == 2 and aimed[0] == 4 and aimed[6]:
            return target, spot_x, spot_y
    best, best_apart = target, 10
    for index in order:
        unit_type, x, y, mode, _, sel, metal, _ = units[index]
        if not found(unit_type, x, y, mode, metal, index == 0, kind, town, spot_x, spot_y):
            continue
        dx, dy = abs(spot_x - x), abs(spot_y - y)
        apart = (dx + dy * 2 if dx <= dy else dy + dx * 2) // 2
        if sel and apart < best_apart:
            best, best_apart = index, apart
    return best, spot_x, spot_y


def found(unit_type, x, y, mode, metal, local, kind, town, spot_x, spot_y):
    """rules::near_found re-stated: whether the finder keeps the unit."""
    if kind == 2:
        return unit_type == 4 and bool(metal)
    if town or (x - spot_x) ** 2 + (y - spot_y) ** 2 > 81:
        return False
    if unit_type == 0:
        return kind != 1 and mode not in (0, 0x11) and not local
    if unit_type != 1:
        return False
    return mode == 0xC if kind == 1 else mode not in (0, 0xC)


class Oracle(near.Oracle):
    """search_enemy_near.py's oracle with the player's room by its subtile and the units in the rooms they stand in."""

    def run(self, case):
        """Run FUN_00467490 on one case; (index it returned, click x, click y)."""
        e = self.e
        cid, kind, town, target, cx, cy, units = case
        self.here = self.rooms[(units[0][1] // ROOM, units[0][2] // ROOM)]
        for level in self.levels.values():
            e.w32(level + 0x1D0, 1 if town else 2)
        e.mu.mem_write(self.row, (0x5A if kind == 2 else 0x46).to_bytes(2, "little"))
        e.mu.mem_write(self.row + 6, bytes([0x40, 1 if kind == 1 else 0]))
        e.w32(self.xy, cx)
        e.w32(self.xy + 4, cy)
        self.sel, self.metal = {}, {}
        for index, (kind_u, x, y, mode, dead, sel, metal, _) in enumerate(units):
            unit, upath = self.units[index]
            e.mu.mem_write(unit, bytes(0x200))
            view = e.view("unit", unit)
            view.type, view.mode, view.path = kind_u, mode, upath
            view.flags = 0x10000 if dead else 0
            e.w32(unit + 4, 100 + index)
            pv = e.view("path", upath)
            pv.x, pv.y = x, y
            e.w32(upath + 0xC, x)
            e.w32(upath + 0x10, y)
            self.sel[unit], self.metal[100 + index] = int(sel), int(metal)
        for (i, j), room in self.rooms.items():
            members = [k for k, unit in enumerate(units) if unit[1] // ROOM == i and unit[2] // ROOM == j]
            members.sort(key=lambda k: (-units[k][7], -k))
            head = 0
            for k in reversed(members):
                e.w32(self.units[k][0] + 0xE8, head)
                head = self.units[k][0]
            e.w32(room + 0x74, head)
        e.w32(near.PLAYER_GLOBAL, self.units[0][0])
        target_ptr = self.units[target][0] if target >= 0 else 0
        got = e.call(near.SEARCH, self.skill, target_ptr, self.xy, self.xy + 4, regs={"eax": self.row}) & 0xFFFFFFFF
        index = next((k for k, (unit, _) in enumerate(self.units[:len(units)]) if unit == got), -1)
        if got and index < 0:
            raise AssertionError(f"case {cid}: FUN_00467490 returned {got:#x}, not a case unit")
        if self.live:
            raise AssertionError(f"case {cid}: the finder's list left {self.live} allocations")
        return index, e.s32(self.xy), e.s32(self.xy + 4)


def make_case(cid, rng):
    """A player near a room edge, a click about it, units about the click on either side of the edge."""
    kind = rng.choice((0, 0, 1, 1, 2))
    town = int(rng.random() < 0.1)
    edge = rng.choice((80, 120, 40, 160))
    px, py = edge + rng.randint(-7, 7), edge + rng.randint(-7, 7)
    if rng.random() < 0.5:
        px = rng.randint(70, 130)
    cx, cy = px + rng.randint(-12, 12), py + rng.randint(-12, 12)
    units = [(0, px, py, rng.choice((1, 1, 1, 2, 0, 0x11)), 0, int(rng.random() < 0.3), 0, rng.randint(0, 4))]   # the local player
    for _ in range(rng.randint(0, near.MAX_UNITS - 1)):
        reach = rng.choice((3, 6, 10, 12))
        x, y = cx + rng.randint(-reach, reach), cy + rng.randint(-reach, reach)
        unit_type = rng.choice((1, 1, 1, 1, 0, 4, 4, 2, 3, 5))
        mode = rng.choice((1, 1, 2, 3, 0, 0xC, 0xC, 0x11))
        units.append((unit_type, x, y, mode, int(rng.random() < 0.15), int(rng.random() < 0.75), int(rng.random() < 0.5), rng.randint(0, 4)))
    target = -1
    if rng.random() < 0.35 and len(units) > 1:
        target = rng.randrange(len(units))
    return cid, kind, town, target, cx, cy, units


def show(case):
    """A case as one line."""
    cid, kind, town, target, cx, cy, units = case
    parts = [cid, kind, town, target, cx, cy, len(units)]
    for unit in units:
        parts += list(unit)
    return " ".join(str(p) for p in parts)


def parse(line):
    """One line back to a case."""
    it = iter(int(v) for v in line.split())
    cid, kind, town, target, cx, cy = (next(it) for _ in range(6))
    units = [tuple(next(it) for _ in range(8)) for _ in range(next(it))]
    return cid, kind, town, target, cx, cy, units


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
                    pick, x, y = oracle.run(case)
                    print(case[0], pick, x, y)
        return 0
    if len(argv) > 1 and argv[1] == "inc":
        oracle = Oracle()
        print("// Stock game.exe 1.14d, FUN_00467490 over FUN_0065ac70's walk; generated by tools/emu/search_walk_near.py "
              f"inc {argv[2]} {argv[3]}.")
        print("// kind, town, target, click x, y, units {type, x, y, mode, dead, sel, metal, stamp} (unit 0 the player),"
              " the pick (-1 none), the click x, y after")
        for case in gen(int(argv[2]), int(argv[3])):
            _, kind, town, target, cx, cy, units = case
            pick, x, y = oracle.run(case)
            unit_text = ", ".join("{ " + ", ".join(str(v) for v in u) + " }" for u in units)
            print(f"    {{ {kind}, {town}, {target}, {cx}, {cy}, {len(units)}, {{ {{ {unit_text} }} }}, {pick}, {x}, {y} }},")
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
    sys.exit(main(argv=sys.argv))
