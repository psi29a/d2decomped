# SPDX-License-Identifier: GPL-3.0-or-later
"""The frame game.exe's client draws at each tick of a monster's get-hit: the oracle for `test_combat --get-hit-frames`.

    uv run python hit_len.py gen 100000 7 > cases.tsv
    uv run python hit_len.py game cases.tsv > game.tsv                # the rate and the ticks, game.exe's
    uv run python hit_render.py [--exe PATH] game cases.tsv game.tsv > g.txt
    ../../build/tests/test_combat --get-hit-frames < cases.tsv | diff g.txt -

The cases are hit_len.py's (`id kind speed frames fhr ...`); only kind 1 (a
monster, mode 3) is run, as test_combat's mode does. game.tsv is hit_len.py's
answer for them (`id rate ticks`): the rate at unit +0x4c from FUN_00623f50's
get-hit branch and the ticks the server holds the mode (FUN_005539b0), both
game.exe's own.

`game` runs the client's side natively and prints `id rate ticks f0 f1 ...`,
one frame a tick of the hold, f0 the frame at the mode set:

- FUN_004b1280(unit, row), the client's per-tick step of a monster, on the
  mode table's get-hit row as it sits in game.exe (0x7252e0 + 3 x 12): its
  kind (+4) 1 tests "over" (FUN_006217c0: +0x44 + rate >= +0x48) and, not
  over, steps (FUN_00623e00: +0x44 += rate). Both run natively.
- FUN_00621810(unit, &frame, &end), the draw's read: +0x44 >> 8.

The unit is a monster (type 1) in mode 3 with the start frame 0, the end at
frames << 8 (as FUN_005533d0 leaves a plain mode) and no path animation
(+0x30 0). Set up by hand, not the claim: the MonStats2 table FUN_004b1280
looks the class up in (DataTables +0xa78, count +0xa80) is one zero row, so
its +0x1a4 test is off, and the AnimData record (+0x50) is zero, so
FUN_00623e00 finds no event frames.

Not covered: the player's own frames (FUN_00463390), a looping mode, the
path-driven animation (+0x30 set).
"""
import sys

import emu
import hit_len

CLIENT_STEP, DRAW_FRAME = 0x4B1280, 0x621810
MODE_TABLE, MODE_ROW, MONSTER_GH = 0x7252E0, 12, 3
DATA_TABLES, MONSTATS2, MONSTATS2_COUNT, MONSTATS2_ROW = 0x744304, 0xA78, 0xA80, 0x1A8


def answers(path):
    """{id: (rate, ticks)} from hit_len.py's `game` output."""
    out = {}
    with open(path, encoding="utf-8") as lines:
        for line in lines:
            if line.startswith("#") or not line.strip():
                continue
            case_id, rate, ticks = map(int, line.split())
            out[case_id] = (rate, ticks)
    return out


def game(exe, cases_path, answers_path):
    """Run FUN_004b1280 a tick and FUN_00621810 after it for each kind-1 case; print `id rate ticks f0 f1 ...`."""
    e = emu.Emu(exe)
    known = answers(answers_path)
    data = e.alloc(0x1000)
    e.w32(DATA_TABLES, data)
    e.w32(data + MONSTATS2, e.alloc(MONSTATS2_ROW))
    e.w32(data + MONSTATS2_COUNT, 1)
    row = MODE_TABLE + MONSTER_GH * MODE_ROW
    if e.r32(row + 4) != 1:
        raise SystemExit(f"the get-hit row's kind at {row + 4:#x} is {e.r32(row + 4)}, not 1")
    unit, record, shown = e.alloc(0x200), e.alloc(0x100), e.alloc(8)
    for case in hit_len.cases(cases_path):
        if case["kind"] != 1:
            continue
        rate, ticks = known[case["id"]]
        e.mu.mem_write(unit, b"\0" * 0x200)
        e.w32(unit, 1)                                   # a monster
        e.w32(unit + 4, 0)                               # class 0: the zero MonStats2 row
        e.w32(unit + 0x10, MONSTER_GH)
        e.w32(unit + 0x44, 0)
        e.w32(unit + 0x48, case["frames"] << 8)
        e.mu.mem_write(unit + 0x4C, (rate & 0xFFFF).to_bytes(2, "little"))
        e.w32(unit + 0x50, record)
        frames = []
        for tick in range(ticks):
            if tick:
                e.call(CLIENT_STEP, ecx=unit, edx=row)
            e.call(DRAW_FRAME, unit, shown, shown + 4)
            frames.append(e.s32(shown))
        print(case["id"], rate, ticks, *frames)


def main():
    """`[--exe PATH] game CASES ANSWERS`."""
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if len(args) == 3 and args[0] == "game":
        game(exe, args[1], args[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
