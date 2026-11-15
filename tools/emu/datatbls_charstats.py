# SPDX-License-Identifier: GPL-3.0-or-later
"""Check CharStats loader FUN_00613260's call into its row string walker.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_charstats.py

CompileTxt, key resolution and the final wide copier are hooked. The loader
and FUN_00612600's stride, field clear and missing-key fallback run in the
original executable on synthetic CharStats rows.
"""

import _monster_world as mw
from _monster_world import require
from monster_loaders import Rig


FUNCTION = 0x613260
STRIDE = 0xC4
TABLE = 0x96C7F4
COUNT = 0x96C7F8


def check(rows, missing):
    """Compare per-row field destinations and fallback source strings."""
    rig = Rig()
    rig.rows = rows
    rig.table = rig.m.alloc(max(STRIDE, rows * STRIDE))
    rig.write(rig.table, bytes([0xA5]) * max(STRIDE, rows * STRIDE))
    source = rig.m.alloc(32)
    rig.write(source, b"N\0" + bytes(30))

    def resolve(current):
        """Return a scripted key result for each CharStats row."""
        rig.log.append(("resolve", current.mu.reg_read(mw.REGISTERS["ecx"])))
        return 0 if missing else source

    def copy(current):
        """Record the field destination, wide source and requested length."""
        destination = current.mu.reg_read(mw.REGISTERS["ecx"])
        text = current.mu.reg_read(mw.REGISTERS["edx"])
        rig.log.append(("copy", destination, text, current.arg(0), rig.m.read(text, 8)))
        return destination

    rig.m.hook(0x524E20, resolve, 0)
    rig.m.hook(0x526790, copy, 1)
    result = rig.go(FUNCTION, 0)
    require(result[0] == "ret", f"CharStats loader: {result}; {rig.log}")
    require((rig.m.r32(TABLE), rig.m.r32(COUNT)) == (rig.table, rows),
            "CharStats table/count slots")
    resolved = [entry for entry in rig.log if entry[0] == "resolve"]
    copied = [entry for entry in rig.log if entry[0] == "copy"]
    expected_destinations = [rig.table + STRIDE * index for index in range(rows)]
    expected_keys = [pointer + 0x20 for pointer in expected_destinations]
    require([entry[1] for entry in resolved] == expected_keys and
            [entry[1] for entry in copied] == expected_destinations,
            f"CharStats row string calls {resolved}, {copied}")
    require(all(entry[3] == 0x10 and entry[2] != 0 for entry in copied),
            f"CharStats copy length/source {copied}")
    if missing:
        require(all(entry[4].startswith(b"<\0>\0\0\0") for entry in copied),
                f"CharStats missing-key fallback {copied}")
    else:
        require(all(entry[2] == source for entry in copied),
                f"CharStats resolved-key source {copied}")
    for destination in expected_destinations:
        require(rig.m.read(destination, 0x10) == bytes(0x10),
                f"CharStats uncleared field at {destination:#x}")


def main():
    """Check zero, one and three rows with both lookup outcomes."""
    for rows in (0, 1, 3):
        for missing in (False, True):
            check(rows, missing)
    print("ok: FUN_00613260 six CharStats row-string cases")


if __name__ == "__main__":
    main()
