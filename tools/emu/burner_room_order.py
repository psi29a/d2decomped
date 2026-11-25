# SPDX-License-Identifier: GPL-3.0-or-later
"""A standing burner's first unit across its near rooms (FUN_00641cb0).

Run from tools/emu with the repository virtual environment:

    .venv/bin/python burner_room_order.py --mutation-check

The stock function runs for real over synthetic near-room and unit lists. The
room-rectangle rejection is hooked to admit every synthetic room; the near-list
getter, unit-size getter and callback are the boundaries supplied by the case.
The model is d2d's `room_order` plus `missile_touches`: near rooms in their
listed order, each room's units newest first, stopping at the first touched
active unit. `--mutation-check` breaks each part of that model in turn.
"""
import random
import struct
import sys

import _monster_world as mw

TOUCHES = 0x64_1CB0
NEAR_LIST = 0x61_9790
ROOM_REACH = 0x64_1930
UNIT_SIZE = 0x62_0510
MUTATIONS = ("rooms-reversed", "oldest-first", "global-newest", "dead-units")
mutation = None


def touched(missile_x, missile_y, missile_size, unit):
    """The `rules::missile_touches` shapes read in FUN_00641cb0."""
    unit_size = min(unit[3], 3)
    if missile_size not in (1, 2, 3) or unit_size < 1:
        return False
    dx, dy = abs(missile_x - unit[1]), abs(missile_y - unit[2])
    shape = unit_size - 1 + (missile_size - 1) * 3
    if shape == 0:
        return dx == 0 and dy == 0
    if shape in (1, 3):
        return dx + dy <= 1
    if shape in (2, 6):
        return dx <= 1 and dy <= 1
    if shape == 4:
        return dx + dy <= 2
    if shape in (5, 7):
        return (dx < 3 and dy < 2) or (dy < 3 and dx <= 1)
    return dx <= 2 and dy <= 2


def model(case):
    """Return the unit index d2d's room-order burner lookup selects."""
    missile_x, missile_y, missile_size, rooms, units = case
    room_walk = list(reversed(rooms)) if mutation == "rooms-reversed" else rooms
    if mutation == "global-newest":
        candidates = reversed(range(len(units)))
    else:
        candidates = []
        for room in room_walk:
            indices = [index for index, unit in enumerate(units) if unit[0] == room]
            candidates.extend(indices if mutation == "oldest-first" else reversed(indices))
    for index in candidates:
        unit = units[index]
        active = unit[4] or mutation == "dead-units"
        if active and touched(missile_x, missile_y, missile_size, unit):
            return index
    return -1


class Oracle:
    """One stock game.exe machine with the lookup's environment hooked."""

    def __init__(self):
        self.world = mw.World()
        self.machine = self.world.m
        self.current = None
        self.room_blocks = [self.machine.alloc(0x100) for _ in range(4)]
        self.room_list = self.machine.alloc(4 * len(self.room_blocks))
        self.units = [(self.machine.alloc(0x100), self.machine.alloc(0x100)) for _ in range(10)]
        self.sizes = {}
        self.machine.hook(NEAR_LIST, self.near_list, 3)
        self.machine.hook(ROOM_REACH, lambda _current: 1, 0)
        self.machine.hook(UNIT_SIZE, lambda current: self.sizes.get(current.arg(0), 0), 1)
        self.callback = self.world.slot(lambda _current: 1, 0)

    def near_list(self, current):
        """Give FUN_00641cb0 the case's ordered array of room pointers."""
        current.w32(current.arg(1), self.room_list)
        current.w32(current.arg(2), len(self.current[3]))
        return 0

    def run(self, case):
        """Run one synthetic room/unit graph and return the selected index."""
        self.current = case
        missile_x, missile_y, missile_size, rooms, units = case
        room_by_id = {room: self.room_blocks[position] for position, room in enumerate(rooms)}
        for position, room in enumerate(rooms):
            self.machine.w32(self.room_list + 4 * position, room_by_id[room])
        self.sizes.clear()
        for index, unit_data in enumerate(units):
            room, unit_x, unit_y, size, active = unit_data
            unit, path = self.units[index]
            self.machine.mu.mem_write(unit, bytes(0x100))
            self.machine.mu.mem_write(path, bytes(0x100))
            self.machine.w32(unit, 1)
            self.machine.w32(unit + 0x10, 1 if active else 0)
            self.machine.w32(unit + 0x2C, path)
            self.machine.mu.mem_write(path + 2, struct.pack("<h", unit_x))
            self.machine.mu.mem_write(path + 6, struct.pack("<h", unit_y))
            self.sizes[unit] = size
            self.machine.w32(unit + 0xE8, 0)
        for room in rooms:
            newest = 0
            for index, unit_data in enumerate(units):
                if unit_data[0] != room:
                    continue
                unit = self.units[index][0]
                self.machine.w32(unit + 0xE8, newest)
                newest = unit
            self.machine.w32(room_by_id[room] + 0x74, newest)
        found = self.world.run(TOUCHES, self.room_blocks[0], missile_x, missile_y,
                               self.callback, 0xC0FFEE, missile_size, nstack=6)
        return next((index for index, (unit, _path) in enumerate(self.units[:len(units)]) if unit == found), -1)


def cases():
    """Crafted mutation witnesses followed by deterministic random cases."""
    result = [
        (50, 50, 1, [4, 9], [(4, 50, 50, 1, True), (9, 50, 50, 1, True)]),
        (50, 50, 1, [4], [(4, 50, 50, 1, True), (4, 50, 50, 1, True)]),
        (50, 50, 1, [4, 9], [(4, 50, 50, 1, True), (9, 50, 50, 1, True), (4, 70, 70, 1, True)]),
        (50, 50, 1, [4], [(4, 50, 50, 1, True), (4, 50, 50, 1, False)]),
    ]
    rng = random.Random(20261125)
    for _case in range(400):
        room_count = rng.randint(1, 4)
        rooms = list(range(room_count))
        rng.shuffle(rooms)
        units = []
        for _unit in range(rng.randint(0, 10)):
            units.append((rng.choice(rooms), 50 + rng.randint(-4, 4), 50 + rng.randint(-4, 4),
                          rng.randint(0, 4), rng.random() < 0.8))
        result.append((50, 50, rng.randint(1, 3), rooms, units))
    return result


def run_all():
    """Compare every stock result with the model and return the case count."""
    oracle = Oracle()
    suite = cases()
    for number, case in enumerate(suite):
        got, want = oracle.run(case), model(case)
        mw.require(got == want, f"case {number}: game.exe picked {got}, d2d model picked {want}")
    return len(suite)


def main():
    """Run the comparison or require each deliberate model defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "FUN_00641cb0 burner room-order cases")


if __name__ == "__main__":
    main()
