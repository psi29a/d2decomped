# SPDX-License-Identifier: GPL-3.0-or-later
"""Two of cursor-units.md "The click's own path"'s calls, as the oracle, run
natively on sk_tele_spot's synthetic rooms:

    uv run python click_free_spot.py

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


def main():
    """Run both FUN_0064e780 and FUN_004677b0 cases against stock game.exe."""
    oracle = tele.Oracle()
    test_wrapper(oracle)
    test_select(oracle)


if __name__ == "__main__":
    main()
