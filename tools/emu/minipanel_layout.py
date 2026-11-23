# SPDX-License-Identifier: GPL-3.0-or-later
"""The mini-panel's layout in both forms, as the oracle: FUN_0047f710 and FUN_0047e8b0.

    uv run python minipanel_layout.py > ../../tests/minipanel_layout_cases.inc

FUN_0047f0c0 sets 0x7bc978 = 1 for game type 0 (single player) and 0 for
the others (ui-module-frontier.md "The UI start"). FUN_0047f710 draws the
bar (FUN_004f6480, its x and y the second and third arguments) by which
side has a panel open (FUN_0047ea60 left, FUN_0047eb50 right; both:
hidden), then FUN_0047e8b0 (EAX 1 right, 2 none, 3 left) lays out the
buttons: x at 0x7bc898, y at 0x7bc8b8, action at 0x7bc8d8 and cel frame at
0x7bc918, 7 of them with the flag set and 8 without. This runs
FUN_0047f710 for real (FUN_0047e8b0 with it) at 640 x 480 and 800 x 600,
each flag and each side, with the drawing and tooltip calls hooked, and
prints one row each for tests/test_mini_panel.cpp, which checks d2d's
apps/d2d/panels/mini_panel.hpp against them:

    { width, height, party, left, right, bar_x, bar_y, count, { { x... } }, y, { { action... } }, { { frame... } } },

`party` is the flag clear (the wide bar). Both sides open draws nothing;
those runs are checked here (no bar, 0x7bc970 = 1) and print no row.
"""
import argparse
import sys

from unicorn.x86_const import UC_X86_REG_EIP

import emu

if not __debug__:
    sys.exit("minipanel_layout.py requires assertions; do not use python -O")

# gaps-runs: 0047f710 0047e8b0

MINIPANEL_DRAW = 0x47F710
LEFT_OPEN, RIGHT_OPEN = 0x47EA60, 0x47EB50
CEL_DRAW = 0x4F6480
SLIDERS_DRAW, TOOLTIP = 0x47E9A0, 0x47F650
CURSOR_A, CURSOR_B = 0x468730, 0x468740
WIDTH, HEIGHT = 0x71146C, 0x711470
SINGLE_FLAG, HIDDEN = 0x7BC978, 0x7BC970
BUTTON_X, BUTTON_Y, BUTTON_ACTION, BUTTON_FRAME = 0x7BC898, 0x7BC8B8, 0x7BC8D8, 0x7BC918


def signed(value):
    """A dword as a signed int."""
    return value - (1 << 32) if value & 0x80000000 else value


def run(machine, sides, draws):
    """One FUN_0047f710 call with the sides open; returns the drawn bar or None."""
    draws.clear()
    machine.call(MINIPANEL_DRAW, limit=200000)
    eip = machine.mu.reg_read(UC_X86_REG_EIP)
    assert eip == emu.RET_MAGIC, f"FUN_0047f710 stopped at {eip:#x}"
    return list(draws)


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    machine = emu.Emu()
    sides = {"left": 0, "right": 0}
    draws = []
    machine.hook(LEFT_OPEN, lambda current: sides["left"], 0)
    machine.hook(RIGHT_OPEN, lambda current: sides["right"], 0)
    machine.hook(CEL_DRAW, lambda current: draws.append((signed(current.arg(1)), signed(current.arg(2)))) or 1, 6)
    machine.hook(SLIDERS_DRAW, lambda current: 0, 1)
    machine.hook(TOOLTIP, lambda current: 0, 0)
    machine.hook(CURSOR_A, lambda current: 0, 0)
    machine.hook(CURSOR_B, lambda current: 0, 0)
    print("// tools/emu/minipanel_layout.py: FUN_0047f710 / FUN_0047e8b0 run in game.exe")
    print("// {width, height, party, left, right, bar_x, bar_y, count, {x...}, y, {action...}, {frame...}}")
    for width, height in ((640, 480), (800, 600)):
        machine.w32(WIDTH, width)
        machine.w32(HEIGHT, height)
        for party in (False, True):
            machine.w32(SINGLE_FLAG, 0 if party else 1)
            for left, right in ((0, 0), (1, 0), (0, 1), (1, 1)):
                sides.update(left=left, right=right)
                for table in (BUTTON_X, BUTTON_Y, BUTTON_ACTION, BUTTON_FRAME):
                    machine.mu.mem_write(table, b"\xa5" * 32)
                machine.w32(HIDDEN, 0x5A)
                drawn = run(machine, sides, draws)
                if left and right:
                    assert drawn == [] and machine.r32(HIDDEN) == 1, f"both open: {drawn}"
                    continue
                assert len(drawn) == 1 and machine.r32(HIDDEN) == 0, f"{width} {party} {left} {right}: {drawn}"
                count = 8 if party else 7
                xs = [machine.s32(BUTTON_X + 4 * index) for index in range(count)]
                ys = {machine.s32(BUTTON_Y + 4 * index) for index in range(count)}
                actions = [machine.s32(BUTTON_ACTION + 4 * index) for index in range(count)]
                frames = [machine.s32(BUTTON_FRAME + 4 * index) for index in range(count)]
                assert len(ys) == 1, f"button y {ys}"
                if count == 7:                           # the eighth slot untouched (with 8 the tables abut)
                    assert machine.r32(BUTTON_X + 4 * count) == 0xA5A5A5A5, "a button past the count"
                bar_x, bar_y = drawn[0]
                def braced(values):              # a std::array field: { { ... } }
                    return "{ { " + ", ".join(map(str, values)) + " } }"
                print(f"{{ {width}, {height}, {str(party).lower()}, {str(bool(left)).lower()}, {str(bool(right)).lower()}, "
                      f"{bar_x}, {bar_y}, {count}, {braced(xs)}, {ys.pop()}, {braced(actions)}, {braced(frames)} }},")


if __name__ == "__main__":
    main()
