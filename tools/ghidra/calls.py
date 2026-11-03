#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Walk game.exe's calls in the order the code makes them, from tools/ghidra/project/game_all.c.

Usage: tools/ghidra/calls.py 00499450 004aa7e0 --to 00502ef0   # each call to SetFont they reach, with its path
       tools/ghidra/calls.py 00456ee0 --depth 2                # the call tree, two levels down

Order is the decompiled C's: a call in a branch counts as reached, each function
once (its first path). Fastcall arguments are registers the C often drops: read
them with tools/emu/disasm.py at the address. Ghidra's unknown calls (through a
pointer) aren't followed.
"""
import argparse, pathlib, re

text = (pathlib.Path(__file__).parent / "project" / "game_all.c").read_text(encoding="utf-8")
HEADER = re.compile(r"// (\S+) @ ([0-9a-f]{8})\n")
CALL = re.compile(r"\b(FUN_[0-9a-f]{8}|d2d_\w+)\s*\(")


heads = list(HEADER.finditer(text))
names = {m.group(1): m.group(2) for m in heads}
bodies = {m.group(2): text[m.end():heads[i + 1].start() if i + 1 < len(heads) else None] for i, m in enumerate(heads)}


def address_of(arg):
    return names.get(arg) or re.sub(r"(?i)^(fun_|0x)", "", arg).lower().rjust(8, "0")


def callees(address):
    body = bodies.get(address, "")
    body = body[body.find("{") + 1:]                 # past the signature
    for match in CALL.finditer(body):
        name = match.group(1)
        yield name, names.get(name) or (name[4:] if name.startswith("FUN_") else None)


def walk(address, depth, limit, target, path, seen, out):
    if address in seen or depth > limit:
        return
    seen.add(address)
    for name, callee in callees(address):
        if target:
            if callee == target:
                out.append(" -> ".join(path + [name]))
            elif callee:
                walk(callee, depth + 1, limit, target, path + [name], seen, out)
        else:
            out.append("  " * depth + name)
            if callee:
                walk(callee, depth + 1, limit, target, path, seen, out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("start", nargs="+")
    parser.add_argument("--to", help="list only the calls to this function, each with its path")
    parser.add_argument("--depth", type=int, default=6)
    options = parser.parse_args()
    target = address_of(options.to) if options.to else None
    for start in options.start:
        out = []
        walk(address_of(start), 0, options.depth, target, [f"FUN_{address_of(start)}"], set(), out)
        print(f"== {start}" + (f": {len(out)} call(s) to {options.to}" if target else ""))
        print("\n".join(out))


if __name__ == "__main__":
    main()
