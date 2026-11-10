# SPDX-License-Identifier: GPL-3.0-or-later
"""Run retail 1.14d's skill-tree close draw; capture coordinates/frame, not pixels.

Use the existing emulator environment: python skill_tree_close.py /path/to/Game.exe
FUN_004ab530 reads the class/tab close table and calls the cel renderer.
Only memset, the cel getter and the renderer are stubbed. No MPQs or downloads.
"""
import argparse
import json

from unicorn.x86_const import UC_X86_REG_EIP

import emu


def signed(value):
    return value - 0x100000000 if value & 0x80000000 else value


def capture(executable):
    machine = emu.Emu(executable)
    calls = []

    def clear(current):
        current.mu.mem_write(current.arg(0), bytes([current.arg(1) & 255]) * current.arg(2))
        return current.arg(0)

    def draw(current):
        calls.append((signed(current.arg(1)), signed(current.arg(2)), current.r32(current.arg(0))))
        return 0

    machine.hook(0x681ef0, clear, 0)
    machine.hook(0x454600, lambda current: 0x12340000, 0)
    machine.hook(0x4f6480, draw, 6)
    machine.w32(0x71146c, 800)
    machine.w32(0x711470, 600)
    machine.w32(0x7a2858, 80)
    machine.w32(0x7a285c, (-60) & 0xffffffff)
    mouse = [0, 0]
    machine.hook(0x468730, lambda current: mouse[0], 0)
    machine.hook(0x468740, lambda current: mouse[1], 0)
    results = []
    for character_class in range(7):
        for tab in range(1, 4):
            for held in (0, 1):
                machine.mu.mem_write(0x7a0522, bytes([character_class]))
                machine.w32(0x724bec, tab)
                machine.w32(0x7c0c38, held)
                calls.clear()
                machine.call(0x4ab530, limit=10000)
                assert machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC
                assert len(calls) == 1
                left, bottom, frame = calls[0]
                assert frame == 10 + held
                for point_x, point_y, expected in ((left, bottom, 1), (left + 34, bottom - 34, 1),
                    (left - 1, bottom, 0), (left + 35, bottom, 0), (left, bottom + 1, 0), (left, bottom - 35, 0)):
                    mouse[:] = [point_x, point_y]
                    result = machine.call(0x4ab5f0, 34, 34, regs={"eax": left, "ebx": bottom}, limit=1000)
                    assert result == expected
                    assert machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC
                results.append((character_class, tab, held, left, bottom, frame))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--tsv", action="store_true")
    args = parser.parse_args()
    results = capture(args.executable)
    if args.tsv:
        for character_class, tab, held, left, bottom, _frame in results:
            if not held:
                print(character_class, tab, left, bottom, sep="\t")
    else:
        print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
