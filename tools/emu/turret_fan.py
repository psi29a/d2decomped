# SPDX-License-Identifier: GPL-3.0-or-later
"""The desert turret's fan (srvdofunc 105, FUN_005cd6a0) as the oracle.

    uv run python turret_fan.py [cases] [seed]

The skill's point (FUN_0056d2c0), the turret's spot (FUN_0045adf0 /
FUN_0045ae20) and calc1 (FUN_00646ca0) come from the case; each missile it
makes (FUN_0059fa30) is caught: its start and its offset (flags 3). One line
per case, `x y to_x to_y count | from_x from_y off_x off_y ; ...`, the form
tests/test_monsters.cpp checks rules::turret_fan with.
"""
import random
import sys

import emu


def run(e, x, y, to_x, to_y, count):
    data, rows = e.alloc(0x1000), e.alloc(0x23c * 4)
    e.w32(0x744304, data); e.w32(data + 0xba0, 4); e.w32(data + 0xb98, rows); e.w32(data + 0xb6c, 100)
    e.mu.mem_write(rows + 0x23c * 3 + 0x48, (7).to_bytes(2, "little"))   # skill 3: srvmissilea 7
    unit = e.alloc(0x200)
    out = []
    def point(_e):
        e.w32(e.arg(0), to_x); e.w32(e.arg(1), to_y); return 1
    def made(_e):
        block = e.mu.reg_read(emu.UC_X86_REG_EDX)
        out.append((e.s32(block + 0x14), e.s32(block + 0x18), e.s32(block + 0x1c), e.s32(block + 0x20), e.r32(block)))
        return 1
    e.hook(0x56d2c0, point, 2)
    e.hook(0x45adf0, lambda _e: x, 0)
    e.hook(0x45ae20, lambda _e: y, 0)
    e.hook(0x646ca0, lambda _e: count, 4)
    e.hook(0x59fa30, made, 0)
    e.call(0x5cd6a0, 3, 1, ecx=e.alloc(0x100), edx=unit)                # skill 3 at level 1
    assert all(flags == 3 for *_, flags in out)
    return [m[:4] for m in out]


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    e = emu.Emu()
    mark = e.brk
    for _ in range(cases):
        e.brk = mark
        x, y = rng.randint(4000, 6000), rng.randint(4000, 6000)
        to_x, to_y = x + rng.choice((0, rng.randint(-30, 30), rng.randint(-30, 30))), y + rng.choice((0, rng.randint(-30, 30), rng.randint(-30, 30)))
        count = rng.randint(1, 5)
        fan = run(e, x, y, to_x, to_y, count)
        print(f"{x} {y} {to_x} {to_y} {count} | " + " ; ".join(" ".join(map(str, m)) for m in fan))
    return 0


if __name__ == "__main__":
    sys.exit(main())
