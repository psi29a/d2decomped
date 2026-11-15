#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print game.exe functions from tools/ghidra/project/game_all.c (tools/ghidra/decompile.sh).

Usage: tools/ghidra/fn.py [--tail=N] 0057c6c0 FUN_0057af80 0x625480 ...

D2_GHIDRA_DECOMP_C may point to a decompilation in another worktree.
--tail=N prints only each function's last N lines, useful for table loaders
whose field schemas take hundreds of lines before the data processing code.
"""
import os
import pathlib
import re
import sys

source = pathlib.Path(os.environ.get(
    "D2_GHIDRA_DECOMP_C", pathlib.Path(__file__).parent / "project" / "game_all.c"))
try:
    text = source.read_text(encoding="utf-8")
except OSError as error:
    raise SystemExit(f"cannot read {source}: {error}") from error
RULE = "// =============================================\n"
sections = {m.group(1): m for m in re.finditer(r"// \S+ @ ([0-9a-f]{8})\n", text)}
missing = False
tail = None
arguments = []
for arg in sys.argv[1:]:
    if arg.startswith("--tail="):
        try:
            tail = int(arg.removeprefix("--tail="))
        except ValueError as error:
            raise SystemExit("--tail requires a positive line count") from error
        if tail < 1:
            raise SystemExit("--tail requires a positive line count")
    else:
        arguments.append(arg)
if not arguments:
    raise SystemExit("usage: fn.py [--tail=N] ADDRESS [ADDRESS ...]")
for arg in arguments:
    address = re.sub(r"(?i)^(fun_|0x)", "", arg)
    if not re.fullmatch(r"(?i)[0-9a-f]{1,8}", address):
        print(f"// invalid address {arg}", file=sys.stderr)
        missing = True
        continue
    address = address.lower().rjust(8, "0")
    found = sections.get(address)
    if not found:
        print(f"// missing {arg}")
        missing = True
        continue
    end = text.find(RULE, found.end() + len(RULE))
    lines = text[found.start():end if end >= 0 else None].rstrip().splitlines()
    if tail is not None:
        lines = lines[-tail:]
    print("\n".join(lines), end="\n\n")

if missing:
    sys.exit(1)
