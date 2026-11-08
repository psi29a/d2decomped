# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the monster-data field accessors in stock game.exe 1.14d.

    uv run python monster_data_fields.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random unit records
and compares the return value, the bytes of the unit and its monster data
after the call, and the bytes the `ret n` popped. Every function takes the
unit in ECX; it acts only when the unit is non-null, its type (+0x0) is 1 and
its data pointer (+0x14) is non-null:

  FUN_005730e0 get data +0x38 (else 0)      FUN_00573100 get data +0x34 (else 0)
  FUN_005734c0 set data +0x54 = EDX         FUN_005734e0 get data +0x54 (else 1)
  FUN_00573500 set data +0x58 = EDX         FUN_00573520 get data +0x58 (else 0)
  FUN_00573540 (word at data +0x5c & DX) != 0 -> 1 (else 0)
  FUN_00573570 stack flag: nonzero ORs DX into the word at data +0x5c, zero
               clears the bits of DX (16-bit read-modify-write; `ret 4`)

No callee is called. `--mutation-check` breaks the model on purpose, one
defect at a time, and requires each to fail. No MPQs are read.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require

GET_38, GET_34, SET_54, GET_54, SET_58, GET_58, TEST_FLAG, SET_FLAG = (
    0x5730E0, 0x573100, 0x5734C0, 0x5734E0, 0x573500, 0x573520, 0x573540, 0x573570)
UNIT_SIZE, DATA_SIZE = 0x40, 0x80
MUTATIONS = ("type-any", "default-54-zero", "flag-dword", "clear-dword", "swap-54-58", "flag-sense-inverted")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def valid(utype, data):
    """True when the accessor reaches the monster data."""
    return data != 0 and (utype == 1 or (mut("type-any") and utype == 0))


def model(address, image, edx, flag):
    """Return (eax, new data bytes) for one call on a valid unit; eax None when the function leaves it undefined."""
    data = bytearray(image)
    word = struct.unpack_from("<H", data, 0x5C)[0]
    if address == GET_38:
        return struct.unpack_from("<I", data, 0x38)[0], data
    if address == GET_34:
        return struct.unpack_from("<I", data, 0x34)[0], data
    if address == SET_54:
        struct.pack_into("<I", data, 0x58 if mut("swap-54-58") else 0x54, edx)
        return None, data
    if address == GET_54:
        return struct.unpack_from("<I", data, 0x54)[0], data
    if address == SET_58:
        struct.pack_into("<I", data, 0x54 if mut("swap-54-58") else 0x58, edx)
        return None, data
    if address == GET_58:
        return struct.unpack_from("<I", data, 0x58)[0], data
    if address == TEST_FLAG:
        if mut("flag-dword"):
            return int(struct.unpack_from("<I", data, 0x5C)[0] & edx != 0), data
        return int(word & edx & 0xFFFF != 0), data
    if (flag != 0) != mut("flag-sense-inverted"):
        struct.pack_into("<H", data, 0x5C, word | (edx & 0xFFFF))
    elif mut("clear-dword"):
        struct.pack_into("<I", data, 0x5C, struct.unpack_from("<I", data, 0x5C)[0] & ~edx & 0xFFFFFFFF)
    else:
        struct.pack_into("<H", data, 0x5C, word & ~edx & 0xFFFF)
    return None, data


def default_for(address):
    """The result when the unit is missing, not type 1 or has no data."""
    return 1 if address == GET_54 and not mut("default-54-zero") else 0


def check_case(world, rng, address):
    """Build one random unit, run `address`, compare with the model."""
    machine = world.m
    utype = rng.choice((0, 1, 1, 1, 2, 3, 4, rng.randrange(1 << 32)))
    unit_image = bytearray(rng.randbytes(UNIT_SIZE))
    struct.pack_into("<I", unit_image, 0, utype)
    data_image = rng.randbytes(DATA_SIZE)
    data = machine.alloc(DATA_SIZE) if rng.random() > 0.15 else 0
    struct.pack_into("<I", unit_image, 0x14, data)
    unit = machine.alloc(UNIT_SIZE)
    world.write(unit, unit_image)
    if data:
        world.write(data, data_image)
    null_unit = rng.random() < 0.1
    edx = rng.choice((0, 1, 0xFFFF, 0x10000, 0xFFFFFFFF, rng.randrange(1 << 32), 1 << rng.randrange(16)))
    flag = rng.choice((0, 1, 2, 0x100, 0xFFFFFFFF))
    args = (flag,) if address == SET_FLAG else ()
    got = world.run(address, *args, ecx=0 if null_unit else unit, edx=edx)
    require(world.log == [], f"{address:#x}: unexpected callee {world.log}")
    ok = not null_unit and valid(utype, data)
    want, new_data = model(address, data_image, edx, flag) if ok else (default_for(address), data_image)
    if address in (SET_54, SET_58, SET_FLAG):
        want = None        # the setters leave EAX undefined
    if want is not None:
        require(got == want, f"{address:#x} utype {utype} edx {edx:#x}: eax {got:#x} != {want:#x}")
    require(machine.read(unit, UNIT_SIZE) == bytes(unit_image), f"{address:#x}: the unit record changed")
    if data:
        require(machine.read(data, DATA_SIZE) == bytes(new_data), f"{address:#x}: monster data differs after the call")


def run_all():
    """Run every accessor on 400 random units; returns the case count."""
    rng = random.Random(0x5730E0)
    world = mw.World()
    cases = 0
    for address in (GET_38, GET_34, SET_54, GET_54, SET_58, GET_58, TEST_FLAG, SET_FLAG):
        for _ in range(400):
            check_case(world, rng, address)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "monster-data accessor cases")


if __name__ == "__main__":
    main()
