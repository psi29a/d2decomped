# SPDX-License-Identifier: GPL-3.0-or-later
"""What the light grid is filled with (FUN_00474550, flag 1, FUN_00474610's call) as the oracle.

    uv run python level_ambient.py
    uv run python level_ambient.py borders   # levels that touch with a different fill

Levels.txt's own colours answer FUN_00619d70 and a fixed day answers the
environment's accessors. The client's event state is set as its entries 0x46b290
(event 12) and 0x46b3a0 (event 13) and FUN_0046beb0 leave it, and the dip is
started as FUN_004d87e0 starts it, then FUN_0046ad10 runs a frame at a time.
Each prints its lines, then `ok`, or what differs from lighting.md and a
nonzero exit. Needs D2_MPQ_DIR and D2_PATCH_INSTALLER.
"""
import csv
import io
import sys

import drlg
import emu

DAY_INTENSITY, DAY_RGB = 200, (255, 255, 255)
RED_ON, RED_COUNT = 0x7a7460, 0x7129d0  # event 12's flag, event 13's count (-1: not counting)
DIP_ON = 0x7a742c
GET_LEVEL_ID, LEVEL_LIGHT, ENV_RGB, ENV_INTENSITY, CLIENT_LEVEL = 0x61a1b0, 0x619d70, 0x61c0b0, 0x61c0e0, 0x4646a0
FILL, DIP_START, DIP_FRAME = 0x474550, 0x46ae50, 0x46ad10
CHAOS, RIVER, BLOOD_MOOR, FORTRESS = 108, 107, 2, 103
CLOAK_LENGTH = 200  # Skills.txt Cloak of Shadows, level 1: Param3
FADE_IN, HOLD, FADE_BACK = 5, CLOAK_LENGTH * 3 // 5, CLOAK_LENGTH * 2 // 5  # FUN_004d87e0
WHITE, RED = (255, 255, 255), (255, 64, 48)
# lighting.md "What fills it": the fill per level with no event, with event 12, and event 13 counting
NO_EVENT = {RIVER: (120, WHITE), CHAOS: (80, WHITE), BLOOD_MOOR: (200, WHITE), FORTRESS: (140, WHITE)}
EVENT_12 = {RIVER: (160, RED), CHAOS: (160, RED), BLOOD_MOOR: (200, WHITE), FORTRESS: (140, WHITE)}
# the dip: 0 at the hold and back to the fill after, but not in River of Flame or the Chaos Sanctuary
DIP_HOLD = {BLOOD_MOOR: 0, FORTRESS: 0, RIVER: 120, CHAOS: 80}
# lighting.md: Act 4's listed levels that touch with a different fill
ACT4_BORDERS = {(106, 105), (105, 104), (104, 103)}


def boot():
    e = emu.Emu()
    rows = list(csv.DictReader(io.StringIO(e.mpq_file("data\\global\\excel\\Levels.txt").decode("latin-1")), delimiter="\t"))
    lights = {int(row["Id"]): tuple(int(row[key] or 0) for key in ("Intensity", "Red", "Green", "Blue")) for row in rows if row["Id"]}
    if lights[CHAOS] != (80, 255, 255, 255):     # the control: the patched table, not a failed read
        raise SystemExit(f"level_ambient: Levels.txt row {CHAOS} reads {lights[CHAOS]}")

    def put_bytes(e, addresses, values):
        for address, value in zip(addresses, values):
            e.mu.mem_write(address, bytes([value]))

    e.hook(GET_LEVEL_ID, lambda e: e.arg(0), 1)  # a level "pointer" here is its id
    e.hook(LEVEL_LIGHT, lambda e: put_bytes(e, [e.arg(i) for i in range(1, 5)], lights[e.arg(0)]) or 0, 5)
    e.hook(ENV_RGB, lambda e: put_bytes(e, [e.arg(i) for i in range(1, 4)], DAY_RGB) or 0, 4)
    e.hook(ENV_INTENSITY, lambda e: DAY_INTENSITY, 1)
    return e


def fill(e, level):
    """FUN_00474550(level, &intensity, &red, &green, &blue, 1)"""
    out = e.alloc(16)
    e.call(FILL, out + 4, out + 8, out + 12, 1, ecx=level, edx=out)
    return e.s32(out), tuple(e.read(out + 4 * i, 1)[0] for i in (1, 2, 3))


def check(label, got, want):
    """Print a case and whether it matches lighting.md; 1 when it doesn't."""
    print(label, *got if isinstance(got, tuple) else (got,), "" if got == want else f"  FAIL: lighting.md says {want}")
    return int(got != want)


def main():
    e, bad = boot(), 0
    e.w32(RED_COUNT, 0xffffffff)
    for level in (RIVER, CHAOS, BLOOD_MOOR, FORTRESS):
        bad += check(f"no event: level {level}:", fill(e, level), NO_EVENT[level])
    e.w32(RED_ON, 1)                             # event 12, every client in the game
    for level in (RIVER, CHAOS, BLOOD_MOOR, FORTRESS):
        bad += check(f"event 12: level {level}:", fill(e, level), EVENT_12[level])
    e.w32(RED_COUNT, 0)                          # event 13 (classic only: FUN_005b52e0 tests game +0x70)
    bad += check(f"event 13 counting: level {CHAOS}:", fill(e, CHAOS), NO_EVENT[CHAOS])
    e.w32(RED_ON, 0)
    e.w32(RED_COUNT, 0xffffffff)

    for level in (BLOOD_MOOR, FORTRESS, RIVER, CHAOS):
        e.hook(CLIENT_LEVEL, lambda e, level=level: level, 0)
        e.call(DIP_START, FADE_BACK, level, 0, ecx=FADE_IN, edx=HOLD)
        seen = []
        for _ in range(10 * (FADE_IN + HOLD + FADE_BACK)):
            if not e.r32(DIP_ON):
                break
            e.call(DIP_FRAME)
            seen.append(fill(e, level)[0])
        else:
            sys.exit(f"level_ambient: the dip in level {level} never ended")
        after = fill(e, level)[0]
        print(f"dip: level {level}: {len(seen)} frames, first ten", *seen[:10], "last ten", *seen[-10:], "after", after)
        bad += check(f"dip: level {level}: at the hold", seen[FADE_IN + HOLD // 2], DIP_HOLD[level])
        bad += check(f"dip: level {level}: after", after, NO_EVENT[level][0])
    print("ok" if not bad else f"FAIL: {bad}")
    return 1 if bad else 0


def borders():
    e = drlg.boot()
    rows = csv.DictReader(io.StringIO(e.mpq_file("data\\global\\excel\\Levels.txt").decode("latin-1")), delimiter="\t")
    # a level with a colour has its Intensity, the rest the day's (None)
    own = {int(row["Id"]): int(row["Intensity"] or 0) if any(int(row[key] or 0) for key in ("Red", "Green", "Blue")) else None for row in rows if row["Id"]}
    act4 = set()
    for act, first in enumerate((1, 40, 75, 103, 109)):
        placed = [(e.r32(level + 0x1d0), *(e.s32(level + 0x1c + 4 * i) for i in range(4))) for level in drlg.levels(e, drlg.alloc_act(e, act, 1, first))]
        print(f"act {act + 1}: {len(placed)} levels listed")
        for index, (one, x, y, width, height) in enumerate(placed):
            for other, other_x, other_y, other_width, other_height in placed[index + 1:]:
                if one != other and width and other_width and own[one] != own[other] and other_x <= x + width and x <= other_x + other_width and other_y <= y + height and y <= other_y + other_height:
                    print(f"  {one} ({own[one]}) touches {other} ({own[other]})")
                    if act == 3:
                        act4.add((one, other))
    bad = act4 != ACT4_BORDERS
    print("ok" if not bad else f"FAIL: lighting.md says Act 4's are {sorted(ACT4_BORDERS)}")
    return int(bad)


if __name__ == "__main__":
    if sys.argv[1:] not in ([], ["borders"]):
        sys.exit("usage: level_ambient.py [borders]")
    sys.exit(borders() if sys.argv[1:] else main())
