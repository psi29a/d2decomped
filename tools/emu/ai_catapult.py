# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005dfee0's catapult launcher decisions in stock 1.14d.

    uv run python ai_catapult.py --exe /path/to/game.exe

Room, marker lookup, coordinates, missile creation and side effects are
scripted; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game_address, unit_address, marker_address, path_address, row_address = (machine.alloc(0x200) for _ in range(5))
    game, marker = machine.view("game", game_address), machine.view("unit", marker_address)
    path, row = machine.view("static_path", path_address), machine.view("missile_row", row_address)
    game.frame, marker.id, path.x, path.y, row.delay = 100, 0x4321, 42, 53, 0x5600
    scripted = {"missile": 0}
    calls = []

    def room(current):
        calls.append(("room", current.arg(0)))
        return 0x1234 if current.arg(0) == unit_address else 0x2468

    def marker_lookup(current):
        calls.append(("marker", tuple(current.arg(i) for i in range(3))))
        return marker_address

    def coordinate(name, value):
        def callback(current):
            calls.append((name, current.arg(0)))
            return value
        return callback

    def launch(current):
        request = machine.view("missile_request", current.mu.reg_read(UC_X86_REG_EDX))
        calls.append(("launch", current.mu.reg_read(UC_X86_REG_ECX), request.flags, request.owner,
                      request.origin, request.missile, request.x, request.y, request.level))
        return scripted["missile"]

    def action(name, count, registers=False):
        def callback(current):
            prefix = (current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)) if registers else ()
            calls.append((name, *prefix, tuple(current.arg(i) for i in range(count))))
            return 0
        return callback

    def missile_row(current):
        calls.append(("row", current.arg(0)))
        return row_address

    machine.hook(0x620BB0, room, 1)
    machine.hook(0x65A620, marker_lookup, 3)
    machine.hook(0x6488C0, coordinate("x", 42), 1)
    machine.hook(0x648900, coordinate("y", 53), 1)
    machine.hook(0x59FA30, launch, 0)
    machine.hook(0x64A710, action("set-target", 2), 2)
    machine.hook(0x61AED0, action("room-update", 2), 2)
    machine.hook(0x624690, action("mode", 2), 2)
    machine.hook(0x640E90, missile_row, 1)
    machine.hook(0x5417D0, action("sound", 4, True), 4)
    cases = (("success-static", 2, path_address, 0, 0x5678),
             ("success-dynamic", 1, path_address, 0, 0x5678),
             ("success-no-path", 1, 0, 0, 0x5678),
             ("failure-mode-zero", 2, path_address, 0, 0),
             ("failure-mode-one", 2, path_address, 1, 0))
    for label, kind, path_ptr, mode, missile in cases:
        marker.type, marker.path, marker.mode = kind, path_ptr, mode
        scripted["missile"] = missile
        calls.clear()
        machine.call(0x5DFEE0, ecx=game_address, edx=unit_address, limit=10000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"{label}: did not return")
        expected = [("room", unit_address), ("marker", (0x1234, 2, 0x182))]
        if kind == 1 and path_ptr:
            expected.extend((("x", path_ptr), ("y", path_ptr)))
        x, y = (42, 53) if path_ptr else (0, 0)
        expected.append(("launch", game_address, 0x420, unit_address, unit_address, 0x170, x, y, 1))
        if missile:
            expected.extend((("set-target", (missile, marker.id)), ("room", marker_address),
                             ("room-update", (0x2468, 0))))
        elif mode == 0:
            expected.extend((("mode", (marker_address, 1)), ("row", 0x182),
                             ("sound", game_address, marker_address, (1, 186, 0, 0))))
        if calls != expected:
            raise AssertionError(f"{label}: {calls!r} != {expected!r}")
    print(f"ok: {len(cases)} catapult launcher cases")


if __name__ == "__main__":
    main()
