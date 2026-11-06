# SPDX-License-Identifier: GPL-3.0-or-later
"""What the light grid is filled with (FUN_00474550, flag 1, FUN_00474610's call) as the oracle.

    uv run python level_ambient.py

Levels.txt's own colours answer FUN_00619d70 and a fixed day answers the
environment's accessors. The client's event state is set as its entries 0x46b290
(event 12) and 0x46b3a0 (event 13) and FUN_0046beb0 leave it.
Needs D2_MPQ_DIR and D2_PATCH_INSTALLER.
"""
import csv
import io

import emu

DAY_INTENSITY, DAY_RGB = 200, (255, 255, 255)
RED_ON, RED_COUNT = 0x7a7460, 0x7129d0  # event 12's flag, event 13's count (-1: not counting)
GET_LEVEL_ID, LEVEL_LIGHT, ENV_RGB, ENV_INTENSITY = 0x61a1b0, 0x619d70, 0x61c0b0, 0x61c0e0
FILL = 0x474550
CHAOS, RIVER, BLOOD_MOOR, FORTRESS = 108, 107, 2, 103


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


def main():
    e = boot()
    e.w32(RED_COUNT, 0xffffffff)
    for level in (RIVER, CHAOS, BLOOD_MOOR, FORTRESS):
        print(f"no event: level {level}:", *fill(e, level))
    e.w32(RED_ON, 1)                             # event 12, every client in the game
    for level in (RIVER, CHAOS, BLOOD_MOOR, FORTRESS):
        print(f"event 12: level {level}:", *fill(e, level))
    e.w32(RED_COUNT, 0)                          # event 13 (classic only: FUN_005b52e0 tests game +0x70)
    print(f"event 13 counting: level {CHAOS}:", *fill(e, CHAOS))


if __name__ == "__main__":
    main()
