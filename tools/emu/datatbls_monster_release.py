# SPDX-License-Identifier: GPL-3.0-or-later
"""Check MonsterTbls.cpp FUN_0065a190's release order and state changes.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_monster_release.py

The release callees are hooked, so only the original FUN_0065a190 runs. Its
pointer guards, call order, null arguments, and global writes are checked on
synthetic state. This does not read MPQs or release host memory.
"""

import random

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x65A190
NESTED = (0x6562B0, 0x659AF0, 0x659DE0, 0x654D50, 0x653E20)
POINTERS = (0x96C810, 0x96C5D8, 0x96C708, 0x96C704, 0x96C87C, 0x96C880,
            0x96C6A8, 0x96C6AC, 0x96C6C0, 0x96C6C4, 0x96C860, 0x96C85C,
            0x96C6B4, 0x96C6B8, 0x96C7A0)
COUNTS = (0x96C814, 0x96C70C, 0x96C884, 0x96C864, 0x96C6BC, 0x96C7A4)


def check(mask, seed):
    """Compare one independently prepared global state with the release trace."""
    rig = Rig()
    rng = random.Random(seed)
    for address in NESTED:
        rig.logger(address, 0, f"nested-{address:08x}", regs=())
    values = {slot: rng.randrange(1, 0x100000) if mask & (1 << index) else 0
              for index, slot in enumerate(POINTERS)}
    counts = {slot: rng.randrange(1, 0x100000) for slot in COUNTS}
    for slot, value in {**values, **counts}.items():
        rig.m.w32(slot, value)
    expected = []

    def table(slot):
        expected.append(("unload", values[slot]))

    def link(slot):
        expected.append(("link-free", values[slot]))

    if values[0x96C810]:
        table(0x96C810)
        counts[0x96C814] = 0
    table(0x96C5D8)
    expected += [(f"nested-{address:08x}",) for address in NESTED[:2]]
    if values[0x96C708]:
        link(0x96C708)
    if values[0x96C704]:
        table(0x96C704)
        values[0x96C704] = 0
    counts[0x96C70C] = 0
    if values[0x96C87C]:
        link(0x96C87C)
        values[0x96C87C] = 0
    if values[0x96C880]:
        table(0x96C880)
        values[0x96C880] = 0
    counts[0x96C884] = 0
    if values[0x96C6A8]:
        table(0x96C6A8)
        link(0x96C6AC)
    values[0x96C6A8] = 0
    if values[0x96C6C0]:
        table(0x96C6C0)
        link(0x96C6C4)
    if values[0x96C860]:
        table(0x96C860)
        link(0x96C85C)
    counts[0x96C864] = 0
    if values[0x96C6B4]:
        table(0x96C6B4)
        link(0x96C6B8)
        counts[0x96C6BC] = 0
    expected += [(f"nested-{address:08x}",) for address in NESTED[2:4]]
    if values[0x96C7A0]:
        table(0x96C7A0)
        counts[0x96C7A4] = 0
    expected.append((f"nested-{NESTED[4]:08x}",))

    result = rig.go(FUNCTION)
    require(result[0] == "ret" and rig.log == expected,
            f"monster release mask {mask:#x}: {result}, {rig.log} != {expected}")
    for slot, value in {**values, **counts}.items():
        require(rig.m.r32(slot) == value,
                f"monster release mask {mask:#x}: {slot:#x} is {rig.m.r32(slot):#x}, expected {value:#x}")


def main():
    """Sweep single guards, their combination, and deterministic mixed states."""
    masks = [0, (1 << len(POINTERS)) - 1]
    masks.extend(1 << index for index in range(len(POINTERS)))
    rng = random.Random(0x65A190)
    masks.extend(rng.randrange(1 << len(POINTERS)) for _ in range(32))
    for index, mask in enumerate(masks):
        check(mask, index)
    print(f"ok: FUN_0065a190 {len(masks)} release states and five nested calls")


if __name__ == "__main__":
    main()
