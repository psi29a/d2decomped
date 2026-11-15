# SPDX-License-Identifier: GPL-3.0-or-later
"""Dump game.exe's installed PL2 lookup tables for tools/pl2-dump diffs.

    uv run python pl2.py <additive|blend50|maxblend> [act]
"""
import sys

from palette import boot

TABLES = {
    "additive": 0x7DE468,
    "blend50": 0x7FE468,
    "maxblend": 0x831B68,     # PL2 +0x5b500, FUN_006c8250 mode 6
}


def dump(act, name):
    table = boot(act).read(TABLES[name], 256 * 256)
    return "\n".join(f"{index:04x} {value:02x}" for index, value in enumerate(table))


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3) or sys.argv[1] not in TABLES:
        raise SystemExit("usage: uv run python pl2.py <additive|blend50|maxblend> [act]")
    print(dump(int(sys.argv[2]) if len(sys.argv) == 3 else 1, sys.argv[1]))
