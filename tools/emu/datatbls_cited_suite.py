# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the local game.exe oracles for the 31 live cited DATATBLS functions.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_cited_suite.py

This imports the persistent oracle modules and runs them in one Python
process. It starts no child process and reads no MPQ. Each module lists its
hooked callees and exact fixture scope; passing the suite does not assert
unrestricted game-data parity.
"""

import importlib
import sys

from _monster_world import require


ORACLES = (
    "datatbls_affixes",
    "datatbls_charstats",
    "datatbls_code_tables",
    "datatbls_excel_open",
    "datatbls_gamble",
    "datatbls_gems",
    "datatbls_itemstatcost",
    "datatbls_leveldefs",
    "datatbls_lvlprest",
    "datatbls_lvlsub",
    "datatbls_monseq",
    "datatbls_monster_release",
    "datatbls_superuniques",
    "datatbls_type_matrices",
    "datatbls_uniqueitems",
)
PROBE_MODES = (
    (),
    ("--matrix",),
    ("--monseq",),
    ("--populated-check",),
    ("--readback-null",),
    ("--readback-present",),
    ("--strings",),
    ("--strings-missing",),
    ("--superuniques",),
    ("--writeback",),
    ("--writeback-fail",),
)


def main():
    """Run each oracle and all probe modes, restoring the caller's argv."""
    require(len(sys.argv) == 1, f"suite takes no arguments: {sys.argv[1:]}")
    saved_argv = sys.argv[:]
    try:
        for module_name in ORACLES:
            sys.argv = [f"{module_name}.py"]
            importlib.import_module(module_name).main()
        probe = importlib.import_module("datatbls_probe")
        for mode in PROBE_MODES:
            sys.argv = ["datatbls_probe.py", *mode]
            probe.main()
    finally:
        sys.argv = saved_argv
    helper_cases = importlib.import_module("item_tbls").run_all()
    require(helper_cases == 2764, f"ItemTbls helper case plan changed: {helper_cases}")
    print(f"ok: DATATBLS cited suite, {len(ORACLES)} focused oracles, "
          f"{len(PROBE_MODES)} probe modes, {helper_cases} ItemTbls helper cases")


if __name__ == "__main__":
    main()
