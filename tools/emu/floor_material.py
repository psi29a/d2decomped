# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's footstep floor material FUN_004cadb0, run by its own code.

    uv run --project tools/emu python tools/emu/floor_material.py

FUN_004cadb0(ECX unit) takes the level's SoundEnviron Material 1
(FUN_004817a0 → FUN_00481920, +0x1c; 1 unless 1..6), the unit's subtile
(FUN_006488c0 / FUN_00648900 from its path) and room (FUN_00620bb0), then
the first floor record at that cell in the room's list (FUN_004cace0: the
room's origin from FUN_00619730, its floors from FUN_00619660, 0x30 bytes a
record, cell x / y at +8 / +0xc), else in each room of its near list
(FUN_00619790), in order. A record found gives its tile's material
(FUN_004cad60: the DT1 flags at tile +6), the level's when that is 0.
FUN_004cace0, FUN_004cad60 and FUN_004cadb0 run for real; the helpers that
hand them the unit, the rooms and the lists are hooked. d2d's
rules::sound_ids::first_floor_material over Level::room_floors (the unit's
room listed before the room next to it, every record with a DT1 tile: d2d
lists only those, gamedata.cpp floors_of) is the Python below. Prints the
cases that differ.
"""
import random
import sys

from emu import Emu

FUNCTION = 0x4CADB0
SOUND_ENV, ENV_RECORD = 0x4817A0, 0x481920
PATH_X, PATH_Y, ROOM_OF = 0x6488C0, 0x648900, 0x620BB0
ROOM_ORIGIN, ROOM_FLOORS, NEAR_ROOMS = 0x619730, 0x619660, 0x619790
RECORD = 0x30
FLAGS = (0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x100, 0x400, 0x800,
         0x28, 0x18, 0x50, 0x480, 0xC0, 0x30, 0x8000)
ROOMS = ((10, 10), (18, 10))      # the unit's room, the one next to it: origins in cells


def floor_material(flags):
    """d2d's rules::sound_ids::floor_material."""
    for bit, material in ((0x20, 1), (0x08, 2), (0x10, 3), (0x40, 4), (0x80, 6), (0x400, 5)):
        if flags & bit:
            return material
    return 0


def first_floor_material(rooms, cell_x, cell_y, level_material):
    """d2d's rules::sound_ids::first_floor_material."""
    level_material = level_material if 0 < level_material < 7 else 1
    for floors in rooms:
        for x, y, flags in floors:
            if (x, y) == (cell_x, cell_y):
                material = floor_material(flags)
                return material if material > 0 else level_material
    return level_material


def main():
    emulator = Emu()
    env = emulator.alloc(0x40)
    unit, path = emulator.alloc(0x100), emulator.alloc(0x40)
    rooms = [emulator.alloc(0x40) for _ in ROOMS]
    floors = [emulator.alloc(RECORD * 16) for _ in ROOMS]
    tiles = emulator.alloc(0x10 * 16 * len(ROOMS))
    near = emulator.alloc(4 * len(ROOMS))
    for index, room in enumerate(rooms):
        emulator.w32(near + 4 * index, room)
    cur = {"x": 0, "y": 0, "counts": [0] * len(ROOMS)}

    def origin(emu):
        room, out = emu.arg(0), emu.arg(1)
        cell_x, cell_y = ROOMS[rooms.index(room)]
        emu.w32(out, cell_x * 5)
        emu.w32(out + 4, cell_y * 5)
        return 0

    def floor_list(emu):
        index = rooms.index(emu.arg(0))
        emu.w32(emu.arg(1), cur["counts"][index])
        return floors[index]

    def near_list(emu):
        emu.w32(emu.arg(1), near)
        emu.w32(emu.arg(2), len(ROOMS))
        return 0

    emulator.hook(SOUND_ENV, lambda _emu: 1, 0)
    emulator.hook(ENV_RECORD, lambda _emu: env, 0)
    emulator.hook(PATH_X, lambda _emu: cur["x"], 1)
    emulator.hook(PATH_Y, lambda _emu: cur["y"], 1)
    emulator.hook(ROOM_OF, lambda _emu: rooms[0], 1)
    emulator.hook(ROOM_ORIGIN, origin, 2)
    emulator.hook(ROOM_FLOORS, floor_list, 2)
    emulator.hook(NEAR_ROOMS, near_list, 3)
    emulator.w32(unit, 0)                        # a player: its subtile from its path
    emulator.w32(unit + 0x2C, path)

    rng = random.Random(0x4CADB0)
    differ = total = 0
    seen = set()
    for case in range(20000):
        level_material = rng.randrange(0, 8)
        emulator.w32(env + 0x1C, level_material)
        lists = []
        for index, (origin_x, origin_y) in enumerate(ROOMS):
            listed = []
            for slot in range(rng.randrange(0, 6)):
                x, y = rng.randrange(0, 3), rng.randrange(0, 3)
                flags = rng.choice(FLAGS)
                record = floors[index] + slot * RECORD
                tile = tiles + (index * 16 + slot) * 0x10
                emulator.w32(record + 8, x)
                emulator.w32(record + 0xC, y)
                emulator.w32(record + 0x18, tile)
                emulator.w32(tile + 4, flags << 16)        # the word at +6
                listed.append((origin_x + x, origin_y + y, flags))
            cur["counts"][index] = len(listed)
            lists.append(listed)
        cell_x, cell_y = ROOMS[0][0] + rng.randrange(0, 3), ROOMS[0][1] + rng.randrange(0, 3)
        cur["x"], cur["y"] = cell_x * 5 + rng.randrange(0, 5), cell_y * 5 + rng.randrange(0, 5)
        got = emulator.call(FUNCTION, ecx=unit)
        want = first_floor_material(lists, cell_x, cell_y, level_material)
        seen.add(want)
        total += 1
        if got != want:
            differ += 1
            if differ <= 20:
                print(f"case {case}: level {level_material}, cell ({cell_x}, {cell_y}), rooms {lists}: game.exe {got}, d2d {want}")
    print(f"{differ} of {total} differ; materials seen {sorted(seen)}")
    sys.exit(1 if differ or len(seen) < 6 else 0)


if __name__ == "__main__":
    main()
