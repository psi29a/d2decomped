# SPDX-License-Identifier: GPL-3.0-or-later
"""Two of cursor-units.md "The click's own path"'s calls, as the oracle, run
natively on sk_tele_spot's synthetic rooms:

    uv run python click_free_spot.py

and, for d2d's SearchOpenXY (gamedata.hpp search_open_xy), case by case:

    uv run python click_free_spot.py gen 20000 1 > cases.tsv
    uv run python click_free_spot.py game cases.tsv > game.tsv
    ../../build/tests/test_search_open_xy cases.tsv > d2d.tsv     # tests/test_search_open_xy.cpp
    diff game.tsv d2d.tsv
    uv run python click_free_spot.py inc 2000 7 > ../../tests/search_open_xy_cases.inc

A case is `id kind ux uy cx cy n {x0 y0 x1 y1 bits}`: the target (kind 0
none, 1 a monster: its dynamic path's subtile, 2 an object: its static
path's +0xc / +0x10) at (ux, uy), the click at (cx, cy), then rectangles
of collision words, each written over the ones before (bits 0 makes a
hole). `game` prints `id kept x y`: kept 1 when FUN_004677b0 returned the
target unchanged, 0 for null (no target, or dropped for the ground), and
the click's x, y as it leaves them. The local player's room (FUN_004646a0,
hooked) is the one holding the searched subtile, so the rooms FUN_0064dea0
sees are it and its touching ones: a spot by the world's edge has subtiles
with no room past it (0x27). U: a spot more than a room from the player is
not covered (a click is on screen).

## FUN_0064e780 (GetFreeCoordinates_WithMaxDistance)

A thin wrapper, like FUN_0064e7b0 / FUN_0064e7e0 (already oracled by
sk_tele_spot.py / corpse_spot.py): it just forwards to FUN_0064dea0
(GetFreeCoordinates, the 50-ring spot search those two already run for
real and match). `test_wrapper` calls FUN_0064e780 and FUN_0064dea0 side by
side with the same room/point/mask and checks they land on the same spot;
FUN_0064dea0's own search is not re-claimed here.

## FUN_004677b0 (SELECT_GetTargetCoords)

Runs natively start to finish on the same rooms: GetXPos/GetYPos
(0x6488c0/0x648900, a real D2DynamicPathStrc), GetCurrentRoom (0x4646a0,
hooked to the caster's room, since there is no real "current unit"
global in this harness) and CheckCollision_BlockPlayerMissile (0x64cb30)
and GetFreeCoordinates_WithMaxDistance (0x64e780) run on the real
collision grid. `test_select` checks: a null target reads x/y straight
from the in/out point; a unit target reads its path's subtile; an open
spot returns the unit pointer unchanged; a blocked spot that
FUN_0064e780 can fix returns null and overwrites the in/out point; a
spot nothing can fix returns the unit pointer with the point untouched
(the walk-there fallback cursor-units.md describes).
"""
import random
import sys

import sk_tele_spot as tele

MASK_WALL = 1    # eCollisionFlags.COLBIT_WALL: what both calls test


def test_wrapper(oracle):
    """FUN_0064e780(room, &xy, 1, 1, 0, 7) against FUN_0064dea0(room, &xy, NULL, 1, 1, 1, 0, 7, 1)."""
    e = oracle.e
    cases = 0
    for radius in (0, 1, 3, 6):
        words = {(110 + dx, 100 + dy): MASK_WALL for dx in range(-radius, radius + 1) for dy in range(-radius, radius + 1)}
        oracle.load(words)
        room = oracle.rooms[(2, 2)]
        p1, p2 = e.alloc(8), e.alloc(8)
        for p in (p1, p2):
            e.w32(p, 110)
            e.w32(p + 4, 100)
        found1 = e.call(0x64E780, room, p1, 1, MASK_WALL, 0, 7) & 0xFFFFFFFF
        found2 = e.call(0x64DEA0, room, p2, 0, 1, MASK_WALL, MASK_WALL, 0, 7, 1) & 0xFFFFFFFF
        cases += 1
        if bool(found1) != bool(found2) or e.s32(p1) != e.s32(p2) or e.s32(p1 + 4) != e.s32(p2 + 4):
            raise AssertionError(f"radius {radius}: wrapper ({found1}, {e.s32(p1)}, {e.s32(p1 + 4)}) "
                                  f"!= GetFreeCoordinates ({found2}, {e.s32(p2)}, {e.s32(p2 + 4)})")
    print(f"ok: {cases} FUN_0064e780 wrapper-matches-FUN_0064dea0 cases")


def test_select(oracle):
    """FUN_004677b0(stack pnOutX, ESI pUnit, EBX pnOutY)."""
    e = oracle.e
    room = oracle.rooms[(2, 2)]
    oracle.here = room
    cases = 0

    def run(unit, x, y, words):
        """Load the collision words, call FUN_004677b0(stack &x, ESI unit, EBX &y), return (ret, x, y)."""
        oracle.load(words)
        outx, outy = e.alloc(4), e.alloc(4)
        e.w32(outx, x)
        e.w32(outy, y)
        ret = e.call(0x4677B0, outx, regs={"esi": unit, "ebx": outy}) & 0xFFFFFFFF
        return ret, e.s32(outx), e.s32(outy)

    e.hook(0x4646A0, lambda cur: room, 0)

    # a null target: reads x/y straight from the in/out point, open ground, point untouched
    ret, x, y = run(0, 110, 100, {})
    cases += 1
    if ret != 0 or (x, y) != (110, 100):
        raise AssertionError(f"null target, open ground: {ret:#x} {x} {y}")

    # a null target on a single blocked subtile: FUN_0064e780 (radius 7, step 1) finds the next one over
    ret, x, y = run(0, 110, 100, {(110, 100): MASK_WALL})
    cases += 1
    if ret != 0 or (x, y) == (110, 100):
        raise AssertionError(f"null target, one blocked tile: {ret:#x} {x} {y} (expected moved off it)")

    # a unit target with a dynamic path: reads the path's subtile, not pnOutX/pnOutY
    path = e.alloc(0x40)
    unit = e.alloc(0x40)
    e.w32(unit, 0)              # UNIT_PLAYER: dynamic path
    e.w32(unit + 0x2C, path)
    e.view("path", path).x, e.view("path", path).y = 130, 90
    ret, x, y = run(unit, 1, 1, {})
    cases += 1
    if ret != unit or (x, y) != (1, 1):
        raise AssertionError(f"unit target, open ground: pnOutX/Y must stay untouched: ret {ret:#x} {x} {y}")

    # the same unit target, now on a square no radius-7 search clears: returns the unit, point untouched
    radius = 10
    words = {(130 + dx, 90 + dy): MASK_WALL for dx in range(-radius, radius + 1) for dy in range(-radius, radius + 1)}
    ret, x, y = run(unit, 1, 1, words)
    cases += 1
    if ret != unit or (x, y) != (1, 1):
        raise AssertionError(f"unit target, nothing free within 7: expected the unit back untouched: ret {ret:#x} {x} {y}")
    print(f"ok: {cases} FUN_004677b0 target/spot cases")


class Select:
    """FUN_004677b0 on generated cases: one target unit of each kind, the hooks of test_select."""

    def __init__(self):
        self.oracle = oracle = tele.Oracle()
        e = oracle.e
        self.room = oracle.rooms[(2, 2)]
        e.hook(0x4646A0, lambda cur: self.room, 0)
        self.units = {}
        for kind, unit_type in ((1, 1), (2, 2)):                # a monster (dynamic path), an object (static)
            unit, path = e.alloc(0x40), e.alloc(0x40)
            e.w32(unit, unit_type)
            e.w32(unit + 0x2C, path)
            self.units[kind] = (unit, path)
        self.outx, self.outy = e.alloc(4), e.alloc(4)

    def run(self, case):
        e = self.oracle.e
        _, kind, ux, uy, cx, cy, rects = case
        self.oracle.load(words_of(rects))
        sx, sy = (ux, uy) if kind else (cx, cy)                  # the player stands by the spot: its room's near list
        self.room = self.oracle.rooms[(sx // tele.ROOM, sy // tele.ROOM)]
        unit = 0
        if kind:
            unit, path = self.units[kind]
            if kind == 1:
                view = e.view("path", path)
                view.x, view.y = ux, uy
            else:
                e.w32(path + 0xC, ux)
                e.w32(path + 0x10, uy)
        e.w32(self.outx, cx & 0xFFFFFFFF)
        e.w32(self.outy, cy & 0xFFFFFFFF)
        ret = e.call(0x4677B0, self.outx, regs={"esi": unit, "ebx": self.outy}) & 0xFFFFFFFF
        if ret not in (0, unit):
            raise AssertionError(f"case {case[0]}: FUN_004677b0 returned {ret:#x}, neither null nor the target")
        return int(ret != 0), e.s32(self.outx), e.s32(self.outy)


def words_of(rects):
    words = {}
    for x0, y0, x1, y1, bits in rects:
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                words[(x, y)] = bits
    return {key: value for key, value in words.items() if value and 0 <= key[0] < tele.SIDE and 0 <= key[1] < tele.SIDE}


def make_case(cid, rng):
    edge = rng.random() < 0.1                                    # near the world's edge: subtiles with no room
    sx = rng.choice((rng.randint(0, 5), rng.randint(tele.SIDE - 6, tele.SIDE - 1))) if edge else rng.randint(85, 114)
    sy = rng.randint(85, 114)
    kind = rng.choice((0, 0, 1, 1, 2))
    ux, uy = (sx, sy) if kind else (rng.randint(85, 114), rng.randint(85, 114))
    cx, cy = (sx, sy) if not kind else (rng.randint(85, 114), rng.randint(85, 114))
    rects = []
    if rng.random() < 0.85:                                      # a wall block over the spot, 0 to 9 out
        reach = rng.choice((0, 0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9))
        x0, y0 = sx - rng.randint(0, reach), sy - rng.randint(0, reach)
        rects.append((x0, y0, x0 + rng.randint(0, 2 * reach), y0 + rng.randint(0, 2 * reach),
                      rng.choice((1, 1, 1, 0x5, 0x9, 0xC01, 0x4, 0x1C09))))
    for _ in range(rng.randint(0, 4)):                           # more walls, holes, other bits
        x0, y0 = sx + rng.randint(-8, 8), sy + rng.randint(-8, 8)
        bits = rng.choice((0, 0, 0, 1, 1, 0x4, 0x80, 0x100, 0x1000, 0x400))
        size = 0 if bits == 0 and rng.random() < 0.7 else rng.randint(0, 3)
        rects.append((x0, y0, x0 + size, y0 + rng.randint(0, size), bits))
    return cid, kind, ux, uy, cx, cy, rects


def show(case):
    cid, kind, ux, uy, cx, cy, rects = case
    return " ".join(str(v) for v in (cid, kind, ux, uy, cx, cy, len(rects), *(v for rect in rects for v in rect)))


def parse(line):
    values = [int(v) for v in line.split()]
    count = values[6]
    rects = [tuple(values[7 + 5 * k:12 + 5 * k]) for k in range(count)]
    return (*values[:6], rects)


def gen(count, seed):
    rng = random.Random(seed)
    return [make_case(cid, rng) for cid in range(count)]


def inc(count, seed):
    select = Select()
    print("// Stock game.exe 1.14d, FUN_004677b0; generated by tools/emu/click_free_spot.py "
          f"inc {count} {seed}.")
    print("// kind (0 none, 1 monster, 2 object), target x, y, click x, y, rects {x0, y0, x1, y1, bits}, kept, x, y")
    for case in gen(count, seed):
        _, kind, ux, uy, cx, cy, rects = case
        kept, x, y = select.run(case)
        rect_text = ", ".join("{ " + ", ".join(str(v) if k < 4 else f"0x{v:x}" for k, v in enumerate(rect)) + " }" for rect in rects)
        rect_text = f"{{ {{ {rect_text} }} }}" if rects else "{}"
        print(f"    {{ {kind}, {ux}, {uy}, {cx}, {cy}, {len(rects)}, {rect_text}, {kept}, {x}, {y} }},")


def main(argv):
    """No mode: both FUN_0064e780 and FUN_004677b0 hand cases against stock game.exe."""
    mode = argv[1] if len(argv) > 1 else ""
    if not mode:
        oracle = tele.Oracle()
        test_wrapper(oracle)
        test_select(oracle)
    elif mode == "gen":
        for case in gen(int(argv[2]), int(argv[3])):
            print(show(case))
    elif mode == "game":
        select = Select()
        with open(argv[2], encoding="utf-8") as cases:
            for line in cases:
                if line.strip():
                    case = parse(line)
                    print(case[0], *select.run(case))
    elif mode == "inc":
        inc(int(argv[2]), int(argv[3]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
