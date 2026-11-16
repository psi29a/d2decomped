# SPDX-License-Identifier: GPL-3.0-or-later
"""Check six UI/text.cpp frontier functions in stock game.exe 1.14d.

Run: .venv/bin/python ui_text_frontier.py [--mutation-check]
The large queue/controller entries use synthetic records and hooked UI/platform
dependencies; game.exe's own branch and data-manipulation instructions run.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
mutation = None


class Mismatch(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Mismatch(message)


def w16(machine, address, value):
    """Write one little-endian word to synthetic stock state."""
    machine.mu.mem_write(address, struct.pack("<H", value & 0xFFFF))


def replace_import(machine, name, handler):
    """Replace one already-bound Win32 import slot."""
    for address, slot in machine.slots.items():
        if slot[0] == name:
            machine.slots[address] = (name, handler)
            return
    raise RuntimeError(f"import {name} was not found")


def check(exe):
    machine = emu.Emu(exe=exe)
    machine.call(0x49E7E0, ecx=0x12345678, edx=0x89ABCDEF)
    want_second = 0 if mutation == "setter_drops_edx" else 0x89ABCDEF
    require((machine.r32(0x7BF258), machine.r32(0x7BF25C)) == (0x12345678, want_second), "coordinate setter")

    widths = {0x11110000: 7, 0x22220000: 31, 0x33330000: 12}
    machine.hook(0x501820, lambda current: widths[current.mu.reg_read(uc.UC_X86_REG_ECX)], 0)
    nodes = [machine.put(b"\0" * 8) for _ in widths]
    for index, (node, text) in enumerate(zip(nodes, widths)):
        machine.w32(node, text)
        machine.w32(node + 4, nodes[index + 1] if index + 1 < len(nodes) else 0)
    record = machine.put(b"\0" * 0x18)
    machine.w32(record, nodes[0])
    w16(machine, record + 4, 4)
    w16(machine, record + 0x14, 9)
    machine.call(0x49D3C0, regs={"edi": record})
    want_width = 0x14 + (30 if mutation == "layout_not_max" else 31)
    require(machine.r32(record + 8) == 0 and machine.r32(record + 0xC) == 9, "layout reset/copy")
    require(machine.r16(record + 0x10) == want_width and machine.r16(record + 0x12) == 66, "layout dimensions")

    dispatch = []
    machine.hook(0x49FBA0, lambda current: dispatch.append(("box", current.arg(0))) or 0, 1)
    machine.hook(0x49FA10, lambda _current: dispatch.append(("restore",)) or 0, 0)
    item = machine.put(b"\0" * 0x84)
    for code, ready, expected in ((0x20736B62, 0, [("box", 0)]), (0x20646B62, 0, [("box", 1)]),
                                  (0x20646B62, 1, [("restore",)]), (0x20317274, 0, [("restore",)]),
                                  (0x12345678, 0, [])):
        dispatch.clear()
        machine.w32(item + 0x80, code)
        machine.w32(0x7BF228, item)
        machine.w32(0x7BF254, ready)
        machine.call(0x49FF10)
        want = [] if mutation == "dispatch_none" else expected
        require(dispatch == want, f"dispatch {code:#x}/{ready}: {dispatch!r}")

    calls = []
    machine.hook(0x455F20, lambda current: calls.append((0x455F20, current.mu.reg_read(uc.UC_X86_REG_ECX), current.arg(0))) or 0, 1)
    machine.hook(0x44DA40, lambda _current: calls.append((0x44DA40,)) or 0, 0)
    machine.hook(0x478680, lambda current: calls.append((0x478680, current.mu.reg_read(uc.UC_X86_REG_ECX) & 0xFF,
                                                         current.mu.reg_read(uc.UC_X86_REG_EDX))) or 0, 0)
    owner = machine.put(b"\0" * 0x10)
    machine.w32(owner + 0xC, 0x55667788)
    for code, pointer, expected_flag in ((0x20646B62, 0, 1), (0x20646B62, owner, 1), (0x20736B62, owner, 0)):
        calls.clear()
        machine.w32(item + 0x80, code)
        machine.call(0x49FF90, ecx=pointer, edx=item)
        require(machine.r32(0x7BF228) == item, "selected item")
        require(machine.r32(0x7BF254) == expected_flag, "selected kind flag")
        require(machine.r32(0x7BF1E0) == (0 if expected_flag else 1), "selected display flag")
        if expected_flag:
            want_edx = 0xFFFFFFFF if pointer == 0 else 0x55667788
            require(calls[-1] == (0x478680, 0x3E, want_edx), "selected-item packet")
    if mutation == "selected_flag_flip":
        require(machine.r32(0x7BF254) == 1, "mutated selected flag")

    tick = [1000]
    replace_import(machine, "GetTickCount", lambda _current: (tick[0], 0))
    allocations = []

    def allocate(_current):
        address = machine.put(b"\0" * 0x80)
        allocations.append(address)
        return address
    machine.hook(0x40B380, allocate, 2)
    machine.hook(0x502EF0, lambda _current: 0, 0)
    machine.hook(0x526800, lambda _current: 0, 0)
    machine.hook(0x49DBC0, lambda _current: 0, 0)
    machine.hook(0x4538D0, lambda _current: 0, 0)
    machine.hook(0x4B9A00, lambda _current: 0, 3)
    machine.w32(0x7BF1EA, 0)
    w16(machine, 0x7BF1EE, 0)
    machine.w32(0x7BF1B4, 0)
    empty = machine.put(b"\0\0")
    machine.call(0x49E5C0, ecx=empty, edx=4)
    queued = machine.r32(0x7BF1EA)
    require(queued == allocations[0] and machine.r16(0x7BF1EE) == 1, "message queued")
    expiry = 1000 + (9999 if mutation == "expiry_short" else 10000)
    require(machine.r32(queued + 0x1E) == expiry and machine.r32(queued + 0x1A) == 4, "message metadata")
    require(machine.r16(queued + 0x18) == 0 and machine.r32(queued + 0x22) == 0, "empty message node")

    controller_calls = []

    def bounds(current):
        machine.w32(current.mu.reg_read(uc.UC_X86_REG_EDX), 10)
        machine.w32(current.arg(0), 110)
        machine.w32(current.arg(1), 210)
        machine.w32(current.arg(2), 310)
        return 0

    def origin(current):
        machine.w32(current.mu.reg_read(uc.UC_X86_REG_EDX), 20)
        machine.w32(current.arg(0), 30)
        return 0

    machine.hook(0x476070, bounds, 3)
    machine.hook(0x4760C0, origin, 1)
    replace_import(machine, "SetRect", lambda current: (controller_calls.append(("rect",) + tuple(current.arg(i) for i in range(5))) or 0, 5))
    for address in (0x464950, 0x464990, 0x4649D0, 0x49DC40, 0x49DEE0):
        machine.hook(address, lambda current, address=address: controller_calls.append((address, current.mu.reg_read(uc.UC_X86_REG_ECX))) or 0, 0)
    machine.hook(0x4538D0, lambda _current: 0, 0)
    for address in (0x7BF1C0, 0x7BF1C4, 0x7BF1B0):
        machine.w32(address, 0)
    machine.w32(0x7BF20E, 40)
    machine.call(0x4A0E70, ecx=0xCAFEBABE)
    require(machine.r32(0x7BF20E) == (40 if mutation == "controller_no_count" else 41), "controller count")
    require(controller_calls[0] == ("rect", 0x7BF214, 20, 30, 220, 230), f"controller rectangle {controller_calls[0]!r}")
    require([entry[0] for entry in controller_calls[1:]] == [0x464950, 0x464990, 0x4649D0, 0x49DC40, 0x49DEE0], "controller call order")
    require(machine.r32(0x7BF1BC) == 0, "controller clears phase")
    return 1 + 1 + 5 + 3 + 1 + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    require(hashlib.sha256(Path(exe).read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    print(f"ok: {check(exe)} UI/text.cpp frontier scenario groups over 6 functions")
    if options.mutation_check:
        global mutation
        for defect in ("setter_drops_edx", "layout_not_max", "dispatch_none", "selected_flag_flip", "expiry_short", "controller_no_count"):
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
