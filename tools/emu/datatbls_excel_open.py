# SPDX-License-Identifier: GPL-3.0-or-later
"""Check both FUN_00639420 Excel open outcomes in stock game.exe 1.14d.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_excel_open.py

The file open and close callees are hooked. Only game.exe's path formatting
and success gate run; no host file is opened.
"""

from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x639420


def cstring(rig, value):
    """Place a terminated input string in emulated heap memory."""
    pointer = rig.m.alloc(len(value) + 1)
    rig.write(pointer, value + b"\0")
    return pointer


def check(name, extension, opened):
    """Verify the constructed path, open mode and conditional close."""
    rig = Rig()
    rig.logger(0x516E46, 2, "open", ret=opened)
    rig.logger(0x516E84, 0, "close")
    result = rig.go(FUNCTION, cstring(rig, name), ecx=cstring(rig, extension))
    require(result[0] == "ret", f"Excel file opener: {result}; {rig.log}")
    opens = [entry for entry in rig.log if entry[0] == "open"]
    closes = [entry for entry in rig.log if entry[0] == "close"]
    require(len(opens) == 1, f"Excel open calls {opens}")
    require(rig.m.cstr(opens[0][2]) ==
            "DATA\\GLOBAL\\EXCEL\\" + name.decode("ascii") + extension.decode("ascii"),
            f"Excel open path {rig.m.cstr(opens[0][2])!r}")
    require(opens[0][4] == 1 and len(closes) == int(bool(opened)),
            f"Excel mode or close gate: {opens}, {closes}")


def main():
    """Cover success and failure with distinct names and extensions."""
    for name, extension in ((b"demo", b".bin"), (b"x", b".txt")):
        for opened in (0, 1):
            check(name, extension, opened)
    print("ok: FUN_00639420 four Excel open/close cases")


if __name__ == "__main__":
    main()
