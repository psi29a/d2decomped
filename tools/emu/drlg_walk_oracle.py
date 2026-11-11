# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00678f70's stepped collision walk against a small model.

    uv run python drlg_walk_oracle.py --exe /path/to/game.exe

The movement update is scripted and the collision helper is hooked; the
velocity scaling helper runs for real. No game data files are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
WALK = 0x678F70


def model(start, end, velocity, speed, stop_at, cleared):
    """Return the result and ordered collision coordinates for bounded cases."""
    if not speed or cleared or velocity == (0, 0):
        return 0, []
    dx, dy = velocity
    while abs(dx) > 0x10000 or abs(dy) > 0x10000:
        dx, dy = dx >> 1, dy >> 1
    threshold = max(abs(dx), abs(dy))
    x, y = ((coord << 16) + 0x8000 for coord in start)
    tx, ty = ((coord << 16) + 0x8000 for coord in end)
    calls = []
    for _ in range(1000):
        if abs(tx - x) < threshold and abs(ty - y) < threshold:
            return 1, calls
        new_x, new_y = x + dx, y + dy
        if (x >> 16, y >> 16) != (new_x >> 16, new_y >> 16):
            calls.append(((new_x >> 16) & 0xFFFF, (new_y >> 16) & 0xFFFF))
            if stop_at and len(calls) == stop_at:
                return 0, calls
        x, y = new_x, new_y
    raise AssertionError("test case did not terminate")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    context_address = machine.alloc(0x40)
    movement_address = machine.alloc(0x40)
    context = machine.view("drlg_walk_context", context_address)
    move = machine.view("drlg_walk_move", movement_address)
    context.room, context.shape, context.mask = 0x1234, 2, 0xCAFE
    calls, updates = [], []
    state = {"stop": 0, "clear": False}

    def update(current):
        updates.append(current.mu.reg_read(UC_X86_REG_ECX))
        if state["clear"]:
            move.speed = 0
        return 0

    def collision(current):
        calls.append(tuple(current.arg(index) for index in range(5)))
        return int(bool(state["stop"]) and len(calls) == state["stop"])

    machine.hook(0x678E30, update, 0)
    machine.hook(0x64D9B0, collision, 5)
    count = 0
    vectors = ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1))
    for unit_x, unit_y in vectors:
        for distance in (0, 1, 3, 7):
            for step in (0x4000, 0x8000, 0x10000, 0x20000):
                for stop_at in (0, 1, 3):
                    start = (100, 100)
                    end = (100 + unit_x * distance, 100 + unit_y * distance)
                    velocity = (unit_x * step, unit_y * step)
                    for speed, cleared in ((1, False), (0, False), (1, True)):
                        move.start_x, move.start_y = start
                        move.end_x, move.end_y = end
                        move.velocity_x, move.velocity_y = velocity
                        move.speed = speed
                        state.update(stop=stop_at, clear=cleared)
                        calls.clear()
                        updates.clear()
                        want_result, want_points = model(start, end, velocity, speed, stop_at, cleared)
                        got = machine.call(WALK, ecx=context_address, edx=movement_address, limit=100000)
                        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
                            raise AssertionError("walk did not return")
                        want_calls = [(context.room, x, y, context.shape, context.mask) for x, y in want_points]
                        if got != want_result or calls != want_calls or updates != ([movement_address] if speed else []):
                            raise AssertionError(f"walk mismatch: {start=} {end=} {velocity=} {speed=} {cleared=} {stop_at=} {got=} {calls=} {want_calls=}")
                        count += 1
    print(f"ok: {count} stepped walk cases with ordered collision calls")


if __name__ == "__main__":
    main()
