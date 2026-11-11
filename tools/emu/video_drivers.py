# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's video drivers: the mode table, the 54 slots of each driver, and the
Gamma / Contrast maths, for docs/research/re/video-drivers.md.

    uv run python video_drivers.py           # the mode table and every slot
    uv run python video_drivers.py gamma     # FUN_006be190's ramp, FUN_0047cc90 / FUN_0047cd00's slider
"""
import struct
import sys

import emu

DRIVERS = (("gdi", 0x74C4A8), ("ddraw", 0x72F6D0), ("glide", 0x72F178), ("d3d", 0x74BF28))
SLOTS = 54


def tables(oracle):
    print("mode table 0x72da80:", " ".join(f"{index}:{oracle.r32(0x72DA80 + 4 * index):x}" for index in range(8)))
    print("slot\t" + "\t".join(name for name, _ in DRIVERS))
    for slot in range(SLOTS):
        print(f"+0x{4 * slot:02x}\t" + "\t".join(f"{oracle.r32(table + 4 * slot):08x}" for _, table in DRIVERS))


def gamma(oracle):
    oracle.crt_init()
    for gamma_value, contrast in ((100, 100), (155, 100), (55, 100), (255, 100), (155, 50), (155, 0)):
        ramp = oracle.alloc(256)
        low, high = struct.unpack("<II", struct.pack("<d", float(contrast)))
        oracle.call(0x6BE190, low, high, 1, 0x100, 0xFF, ecx=gamma_value, edx=ramp)   # ECX gamma, EDX out
        levels = oracle.read(ramp, 256)
        print(f"ramp gamma {gamma_value:3} contrast {contrast:3}:", " ".join(f"{index}->{levels[index]}" for index in (1, 16, 64, 128, 192, 255)))
    row = oracle.alloc(0x130)
    for name, steps, lowest, highest, values in (("gamma", 21, 0x37, 0xFF, (0x9B, 0x37, 0xFF)), ("contrast", 100, 0, 100, (100, 0, 50))):
        oracle.w32(row + 0x120, steps)
        for value in values:
            position = oracle.call(0x47CC90, row, ecx=highest, edx=lowest, regs={"eax": value})
            oracle.w32(row + 0x124, position)
            saved = oracle.call(0x47CD00, ecx=lowest, edx=row, regs={"eax": highest})
            print(f"slider {name} value {value:3} -> position {position:2} -> saved {saved}")


if __name__ == "__main__":
    (gamma if sys.argv[1:] == ["gamma"] else tables)(emu.Emu())
