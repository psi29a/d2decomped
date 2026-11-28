# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare d2d's Rogue Encampment room flags and near lists with game.exe.

    NEAR_VIS=1 $D2_BUILD/tests/test_room_near --near SEED 1 \
        | .venv/bin/python camp_room_flags.py SEED --mutation-check

FUN_00667ed0 splits the preset into rooms, FUN_00667970 supplies each
room's +0x28 slot flags, and FUN_0066c370 builds its near list.  The optional
negative control removes d2d's flag table and must be detected.
"""
import argparse
import re
import sys

import drlg


ROOM_RE = re.compile(r"room (-?\d+),(-?\d+) (\d+)x(\d+) vis ([0-9a-f]+):(.*)")
NEAR_RE = re.compile(r"(\d+)@(-?\d+),(-?\d+),(\d+)x(\d+)")


def parse_dump(text):
    """Return {(x, y, w, h): (flags, sorted near records)} from d2d's dump."""
    rooms = {}
    for line in text.splitlines():
        match = ROOM_RE.fullmatch(line)
        if not match:
            continue
        rect = tuple(int(match[index]) for index in range(1, 5))
        near = sorted(tuple(int(value) for value in found) for found in NEAR_RE.findall(match[6]))
        rooms[rect] = (int(match[5], 16), near)
    if not rooms:
        raise ValueError("no camp room lines on stdin (set NEAR_VIS=1 and request level 1)")
    return rooms


def game_dump(seed):
    """Read game.exe's room flags and completed near lists for level 1."""
    emulator = drlg.boot()
    act = drlg.alloc_act(emulator, 0, seed, 1)
    level = drlg.find_level(emulator, act, 1)
    if not level:
        raise RuntimeError("game.exe did not build level 1")
    origin_x, origin_y = emulator.s32(level + 0x1C), emulator.s32(level + 0x20)
    room_list = []
    room = emulator.r32(level + 0x10)
    while room:
        room_list.append(room)
        room = emulator.r32(room + 0x24)
    for room in room_list:
        emulator.call(0x61B730, ecx=room)

    result = {}
    for room in room_list:
        x_pos, y_pos, width, height = (emulator.s32(room + 0x34 + 4 * index) for index in range(4))
        near = []
        array, count = emulator.r32(room + 8), emulator.r32(room + 0x2C)
        for index in range(count):
            other = emulator.r32(array + 4 * index)
            other_x, other_y, other_width, other_height = (
                emulator.s32(other + 0x34 + 4 * field) for field in range(4)
            )
            other_level = emulator.r32(emulator.r32(other + 0x58) + 0x1D0)
            near.append((other_level, other_x - origin_x, other_y - origin_y, other_width, other_height))
        rect = (x_pos - origin_x, y_pos - origin_y, width, height)
        result[rect] = (emulator.r32(room + 0x28) & 0xFF0, sorted(near))
    return result


def differences(stock, port):
    """Describe every room missing from or different in the port."""
    found = []
    for rect in sorted(set(stock) | set(port)):
        if stock.get(rect) != port.get(rect):
            found.append(f"room {rect}: game.exe {stock.get(rect, '-')} d2d {port.get(rect, '-')}")
    return found


def main():
    """Compare the stdin dump and optionally run the missing-table negative control."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("seed", type=lambda value: int(value, 0))
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    try:
        port = parse_dump(sys.stdin.read())
        stock = game_dump(args.seed)
    except (RuntimeError, ValueError) as error:
        raise SystemExit(f"FAIL: {error}") from error
    mismatches = differences(stock, port)
    if mismatches:
        print("\n".join(mismatches))
        raise SystemExit(f"FAIL: {len(mismatches)} camp rooms differ")
    if args.mutation_check:
        mutant = {rect: (0, near) for rect, (_, near) in port.items()}
        if not differences(stock, mutant):
            raise SystemExit("FAIL: --mutation-check did not detect a missing camp flag table")
    suffix = "; missing-table mutation caught" if args.mutation_check else ""
    print(f"ok: {len(stock)} camp room flags and near lists match{suffix}")


if __name__ == "__main__":
    main()
