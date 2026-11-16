#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List game.exe's Win32 wrappers: the plumbing d2d leaves to SDL3 and the C++ library.

Usage: tools/ghidra/win32.py > docs/research/re/win32.tsv   (after tools/ghidra/decompile.sh)

A function is a wrapper when its C (tools/ghidra/project/game_all.c) calls a
platform API (process, thread, memory, module, file, registry, window, socket,
crypto, IME, shell) and everything it calls is library code (Ghidra's CRT names)
or another wrapper. Helpers game code calls too (rectangles, strings, clocks,
critical sections) don't make a wrapper. DirectDraw, DirectSound, Bink, Smacker
and IJL aren't platform here: the drivers over them are still worth reading.
tools/gaps.py keeps the list out of its totals.
"""
import bisect, collections, os, pathlib, re, sys

RE_DIR = pathlib.Path(__file__).parents[2] / "docs" / "research" / "re"
PLATFORM = {"ADVAPI32.DLL", "CRYPT32.DLL", "GDI32.DLL", "IMM32.DLL", "KERNEL32.DLL", "OLE32.DLL", "PSAPI.DLL",
            "SHELL32.DLL", "USER32.DLL", "VERSION.DLL", "WINMM.DLL", "WINTRUST.DLL", "WS2_32.DLL"}
HELPER = re.compile(r"(IntersectRect|SetRect|SetRectEmpty|PtInRect|OffsetRect|InflateRect|UnionRect|EqualRect|IsRectEmpty"
                    r"|CopyRect|SubtractRect|lstr\w+|w?v?sprintf\w*|MultiByteToWideChar|WideCharToMultiByte|GetTickCount"
                    r"|timeGetTime|QueryPerformance\w+|\w*CriticalSection\w*|Interlocked\w+|Sleep|Char(Upper|Lower|Next|Prev)\w*"
                    r"|IsDBCSLeadByte\w*|CompareString\w*|[GS]etLastError|Ordinal_\d+|MulDiv|GetSystemTime\w*|GetLocalTime"
                    r"|FileTimeTo\w+|SystemTimeTo\w+)")
HEADER = re.compile(r"// =+\n// \S+ @ ([0-9a-f]{8})\n// =+\n")
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def rows(name):
    with open(RE_DIR / name, encoding="utf-8") as f:
        return [line.rstrip("\n").split("\t") for line in f][1:]


def wrappers(game_c):
    apis = {sym for lib, sym, _ in rows("game-imports.tsv") if lib in PLATFORM and not HELPER.fullmatch(sym)}
    parts = HEADER.split(game_c)
    called = {int(parts[i], 16): set(CALL.findall(parts[i + 1])) & apis for i in range(1, len(parts), 2)}
    funcs = {int(a, 16): (name, int(size)) for a, name, size, *_ in rows("game-functions.tsv")}
    library = {a for a, (name, _) in funcs.items() if not name.startswith(("FUN_", "thunk_FUN_", "d2d_"))}
    starts = sorted(funcs)

    def holding(address):
        i = bisect.bisect_right(starts, address) - 1
        return starts[i] if i >= 0 and address < starts[i] + max(funcs[starts[i]][1], 1) else None

    callees = collections.defaultdict(set)
    for caller, callee, kind in rows("game-calls.tsv"):
        if kind == "data" or not re.fullmatch(r"[0-9a-f]{8}", caller):
            continue
        source, target = holding(int(caller, 16)), holding(int(callee, 16))
        if source is not None and target is not None and source != target:
            callees[source].add(target)
    candidates = {a for a in funcs if a not in library and called.get(a)}
    found, grew = set(), True
    while grew:   # a wrapper may call another wrapper
        grew = False
        for a in candidates - found:
            if callees[a] <= library | found:
                found.add(a)
                grew = True
    return [(a, funcs[a][1], sorted(called[a])) for a in sorted(found)]


if __name__ == "__main__":
    default = pathlib.Path(__file__).parent / "project" / "game_all.c"
    game_c = pathlib.Path(os.environ.get("D2_GHIDRA_DECOMP_C", default)).read_text(encoding="utf-8")
    sys.stdout.write("address\tsize\tapis\n")
    for address, size, apis in wrappers(game_c):
        sys.stdout.write(f"{address:08x}\t{size}\t{' '.join(apis)}\n")
