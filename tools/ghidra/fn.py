#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print game.exe functions from tools/ghidra/project/game_all.c (tools/ghidra/decompile.sh).

Usage: tools/ghidra/fn.py 0057c6c0 FUN_0057af80 0x625480 ...
"""
import pathlib
import re
import sys

text = (pathlib.Path(__file__).parent / "project" / "game_all.c").read_text(encoding="utf-8")
RULE = "// =============================================\n"
sections = {m.group(1): m for m in re.finditer(r"// \S+ @ ([0-9a-f]{8})\n", text)}
missing = False
for arg in sys.argv[1:]:
    address = re.sub(r"(?i)^(fun_|0x)", "", arg).lower().rjust(8, "0")
    found = sections.get(address)
    if not found:
        print(f"// missing {arg}")
        missing = True
        continue
    end = text.find(RULE, found.end() + len(RULE))
    print(text[found.start():end if end >= 0 else None].rstrip(), end="\n\n")

if missing:
    sys.exit(1)
