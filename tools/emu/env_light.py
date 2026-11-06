# SPDX-License-Identifier: GPL-3.0-or-later
"""The day's intensity (FUN_0061bb80, environment +0xc) as the oracle.

    uv run python env_light.py

ESI the environment, ECX the level, EDI the act (0-based), as FUN_0061bfc0
calls it each client frame. One line per case: what it starts from, the frames
run and the intensity after each, then `ok`, or each line that differs from
lighting.md's numbers and a nonzero exit. Needs D2_MPQ_DIR for game.exe only.
"""
import sys

import emu

ENV_SIZE, INTENSITY, TIME, SCALE, ECLIPSE = 0x38, 0xc, 0x8, 0x28, 0x30
DAY_SCALE = 128
# lighting.md "Act 4's intensity": each level's target (any other: act + 13 = 16)
ACT4_TARGET = {102: 16, 103: 0x80, 104: 0x40, 105: 0x38, 106: 0x30, 107: 16, 108: 16, 109: 16}
# lighting.md "Intensity": the frames each case gives
WANT = {"act4-eclipse": [199, 198], "act4-noon": [199, 198], "sun-0": [128], "sun-90": [255], "sun-270": [64],
        "act5-noon": [170], "level-120": [200], "eclipse": [52, 44, 36, 32, 32]}


def frames(e, env, level, act, count):
    seen = []
    for _ in range(count):
        e.call(0x61bb80, ecx=level, regs={"esi": env, "edi": act})
        seen.append(e.s32(env + INTENSITY))
    return seen


def case(e, label, level, act, start, count, degrees=0, eclipse=0):
    env = e.alloc(ENV_SIZE)
    e.w32(env + INTENSITY, start)
    e.w32(env + TIME, degrees * DAY_SCALE)
    e.w32(env + SCALE, DAY_SCALE)
    e.w32(env + ECLIPSE, eclipse)
    seen = frames(e, env, level, act, count)
    bad = seen != WANT[label]
    print(label, f"level={level} act={act} start={start} degrees={degrees} eclipse={eclipse}:", *seen,
          f"  FAIL: lighting.md says {WANT[label]}" if bad else "")
    return bad


def main():
    if sys.argv[1:]:
        sys.exit("usage: env_light.py")
    e, bad = emu.Emu(), 0
    for level in range(102, 110):                     # Act 4's table and its default, either side of it
        for start in (255, 1):                        # down from above and up from below, a step a frame
            env = e.alloc(ENV_SIZE)
            e.w32(env + INTENSITY, start)
            e.w32(env + SCALE, DAY_SCALE)
            seen = frames(e, env, level, 3, 300)
            target = ACT4_TARGET[level]
            settled = seen.index(seen[-1]) + 1
            wrong = seen[-1] != target or settled != abs(start - target)   # a step a frame
            bad += wrong
            print(f"act4 level={level} start={start}: first {seen[0]} settles at {seen[-1]} after {settled} frames"
                  + (f"  FAIL: lighting.md says {target} after {abs(start - target)}" if wrong else ""))
    bad += case(e, "act4-eclipse", 103, 3, 200, 2, eclipse=1)
    bad += case(e, "act4-noon", 103, 3, 200, 2, degrees=90)  # the time isn't read
    for degrees in (0, 90, 270):                             # the sine, for the other acts
        bad += case(e, f"sun-{degrees}", 1, 0, 0, 1, degrees)
    bad += case(e, "act5-noon", 109, 4, 0, 1, 90)
    bad += case(e, "level-120", 120, 4, 0, 1, 90)
    bad += case(e, "eclipse", 40, 1, 60, 5, eclipse=1)
    print("ok" if not bad else f"FAIL: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
