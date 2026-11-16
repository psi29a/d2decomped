# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify two client UI frontier functions in stock game.exe 1.14d.

Run: .venv/bin/python client_ui_frontier.py [--exe PATH] [--mutation-check]

FUN_004520c0 prefixes its ECX string with ``DATA\\GLOBAL\\UI\\`` and calls
FUN_004788b0 with that path in ECX and zero in EDX.  The DC6 loader is hooked
and logged; wsprintfA and the stock strlen helper run for real.

FUN_004680a0 returns the dword at 0x7a6abc unchanged and has no callee.
"""
import argparse
import hashlib
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

if not __debug__:
    sys.exit("client_ui_frontier.py requires assertions; do not use python -O")

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
UI_DC6_LOAD = 0x4520C0
CURSOR_ITEM_GET = 0x4680A0
LOAD_DC6 = 0x4788B0
CURSOR_ITEM = 0x7A6ABC
CALL_LIMIT = 100000
RETURN_VALUE = 0x13579BDF
mutation = None


class Mismatch(Exception):
    """The stock result disagreed with the independent model."""


def require(condition, message):
    """Raise a comparison failure with context."""
    if not condition:
        raise Mismatch(message)


def model_path(name):
    """The path and loader flag produced by FUN_004520c0."""
    prefix = "DATA\\GLOBAL\\UI\\"
    if mutation == "path_local":
        prefix = "DATA\\LOCAL\\UI\\"
    flag = 1 if mutation == "path_flag" else 0
    return prefix + name, flag


def model_cursor_item(value):
    """The exact dword returned by FUN_004680a0."""
    return int(bool(value)) if mutation == "cursor_boolean" else value


def check(exe):
    """Run both functions over boundary, representative and random-like values."""
    machine = emu.Emu(exe=exe)
    calls = []

    def capture_load(current):
        calls.append((current.cstr(current.mu.reg_read(UC_X86_REG_ECX)),
                      current.mu.reg_read(UC_X86_REG_EDX)))
        return RETURN_VALUE

    machine.hook(LOAD_DC6, capture_load, 0)
    names = ["", "PANEL\\invchar6", "menu\\waygateicons", "x" * 244]
    for name in names:
        calls.clear()
        source = machine.put(name.encode("ascii") + b"\0")
        got = machine.call(UI_DC6_LOAD, ecx=source, limit=CALL_LIMIT)
        require(calls == [model_path(name)], f"UI path {name[:24]!r}: game {calls!r}, model {model_path(name)!r}")
        require(got == RETURN_VALUE, f"UI path {name[:24]!r}: returned {got:#x}")

    values = [0, 1, 4, 0x10000, 0x12345678, 0x80000000, 0xFFFFFFFF]
    for value in values:
        machine.w32(CURSOR_ITEM, value)
        got = machine.call(CURSOR_ITEM_GET, limit=CALL_LIMIT)
        want = model_cursor_item(value)
        require(got == want, f"cursor item {value:#x}: game {got:#x}, model {want:#x}")
    return len(names), len(values)


def main():
    """Parse the executable, run the positive check and optional negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    require(hashlib.sha256(Path(exe).read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    path_cases, cursor_cases = check(exe)
    print(f"ok: {path_cases} UI DC6 path cases, {cursor_cases} cursor-item getter cases")
    if options.mutation_check:
        global mutation
        for defect in ("path_local", "path_flag", "cursor_boolean"):
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
