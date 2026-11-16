# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the five UI/ui.cpp frontier helpers in stock game.exe 1.14d.

Run: .venv/bin/python ui_frontier_helpers.py [--mutation-check]
"""
import argparse
import hashlib
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FUNCTIONS = (0x453AA0, 0x453AB0, 0x453AC0, 0x453AD0, 0x453B00)
mutation = None


class Mismatch(Exception):
    """Stock execution disagreed with the independently stated model."""


def require(condition, message):
    if not condition:
        raise Mismatch(message)


def replace_import(machine, name, handler):
    """Replace one already-bound Win32 import slot."""
    for address, slot in machine.slots.items():
        if slot[0] == name:
            machine.slots[address] = (name, handler)
            return
    raise RuntimeError(f"import {name} was not found")


def check(exe):
    machine = emu.Emu(exe=exe)
    for address, global_address in ((0x453AA0, 0x7A2878), (0x453AB0, 0x7A2880)):
        for value in (0, 1, 0x12345678, 0xFFFFFFFF):
            machine.w32(global_address, value)
            want = int(bool(value)) if mutation == "boolean_getter" else value
            require(machine.call(address) == want, f"{address:#x} value {value:#x}")

    machine.w32(0x70FA9C, 0xAAAAAAAA)
    machine.w32(0x7A2790, 0xBBBBBBBB)
    machine.call(0x453AC0)
    require(machine.r32(0x70FA9C) == 0, "reset primary flag")
    want_secondary = 1 if mutation == "reset_one" else 0
    require(machine.r32(0x7A2790) == want_secondary, "reset secondary flag")
    machine.call(0x453AD0)
    want_enabled = 2 if mutation == "enable_two" else 1
    require(machine.r32(0x70FA9C) == want_enabled, "enable primary flag")
    require(machine.r32(0x7A2790) == want_secondary, "enable changed secondary flag")

    calls = []
    coords = [(-1, -1), (0, 0), (0x1234, 0x5678), (0xFFFF, 0x8000)]

    def x_coord(_current):
        return coords[case][0]

    def y_coord(_current):
        return coords[case][1]

    def window(_current):
        return 0x11223344

    def send(current):
        calls.append(tuple(current.arg(index) for index in range(4)))
        return 0xA5A5A5A5, 4

    machine.hook(0x468730, x_coord, 0)
    machine.hook(0x468740, y_coord, 0)
    machine.hook(0x4F59A0, window, 0)
    replace_import(machine, "SendMessageA", lambda current: send(current))
    for case in range(len(coords)):
        calls.clear()
        machine.call(0x453B00)
        x_value, y_value = coords[case]
        packed = (x_value & 0xFFFF) | ((y_value & 0xFFFF) << 16)
        if mutation == "swap_coordinates":
            packed = (y_value & 0xFFFF) | ((x_value & 0xFFFF) << 16)
        require(calls == [(0x11223344, 0x200, 0, packed)], f"mouse refresh {case}: {calls!r}")
    return 4 * 2 + 2 + len(coords)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    require(hashlib.sha256(Path(exe).read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    print(f"ok: {check(exe)} UI/ui.cpp frontier cases over {len(FUNCTIONS)} functions")
    if options.mutation_check:
        global mutation
        for defect in ("boolean_getter", "reset_one", "enable_two", "swap_coordinates"):
            mutation = defect
            try:
                check(exe)
            except Mismatch:
                print(f"mutation {defect}: failed as required")
            else:
                raise RuntimeError(f"mutation {defect} was not detected")
        mutation = None


if __name__ == "__main__":
    main()
