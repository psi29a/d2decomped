# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's Talk-submenu quest name (FUN_0049f910) against d2d's rules::quest_name.

    uv run python quest_name.py

FUN_0049f910 (fastcall: CX the NPC message) looks the message up in the
{u16 message, u16 name} table at 0x722678 (0x20f rows, count at 0x722eb4) and
tail-calls the string lookup (0x524a30) with the name, or 0xe8c when no row
matches. A message below row 100's (word at 0x722808) searches rows 0..99,
any other rows 100 up to the count. This runs it on every u16 message with
the string lookup hooked to return its argument, and compares each answer
with the runs in components/rules/quests.hpp's quest_name (read from the
source, first match wins). Row 100 is message 164 (Tools of the Trade): the
second search starts at it, so 164 gives 3716, not 0xe8c. No MPQs are read.
"""
import argparse
import hashlib
import re
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
QUEST_NAME = 0x49F910
STRING_LOOKUP = 0x524A30
TABLE = 0x722678
ROW_COUNT = 0x722EB4
SPLIT = 0x722808
NOT_FOUND = 0xE8C
LIMIT = 10000
SOURCE = Path(__file__).resolve().parents[2] / "components" / "rules" / "quests.hpp"


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def d2d_runs():
    """The (first, last, name) runs of quest_name in quests.hpp."""
    text = SOURCE.read_text()
    body = text.split("std::array<Run,", 1)[1].split("kRuns{ {", 1)[1].split("} };", 1)[0]
    runs = [tuple(int(value) for value in match) for match in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", body)]
    require(runs, "no runs in quests.hpp")
    return runs


def d2d_name(runs, message):
    """rules::quest_name: the first run holding the message, else 0xe8c."""
    return next((name for first, last, name in runs if first <= message <= last), NOT_FOUND)


def main():
    """Compare game.exe and d2d on every u16 message."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    machine.hook(STRING_LOOKUP, lambda current: current.mu.reg_read(UC_X86_REG_ECX), 0)
    rows = machine.r32(ROW_COUNT)
    require(rows == 0x20F, f"{rows:#x} rows")
    require(machine.r16(SPLIT) == machine.r16(TABLE + 4 * 100) == 164, "row 100 is not message 164")
    runs = d2d_runs()
    found = 0
    for message in range(0x10000):
        got = machine.call(QUEST_NAME, ecx=message, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{message}: stopped at the cap")
        want = d2d_name(runs, message)
        require(got == want, f"message {message}: game.exe {got:#x}, d2d {want:#x}")
        found += got != NOT_FOUND
    require(d2d_name(runs, 164) == 3716, "d2d gives 164 no name")
    print(f"65536 messages match d2d's {len(runs)} runs; {found} have a name; 164 (row 100) is 3716")
    print("ok")


if __name__ == "__main__":
    main()
