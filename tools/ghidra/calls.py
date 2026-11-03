#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Walk game.exe's calls in the order they're written, from tools/ghidra/project/game_all.c.

Usage: tools/ghidra/calls.py 00499450 004aa7e0 --to 00502ef0   # each call to SetFont they reach, with a shortest path
       tools/ghidra/calls.py 00456ee0 --depth 2                # the call tree, two levels down

Order is the decompiled C's text order, statement by statement: that's the run
order, except in a nested call, `f(g())`, which lists f before g though g runs
first. A call in a branch counts as reached. Fastcall arguments are registers
the C often drops: read them with tools/emu/disasm.py at the address. Ghidra's
unknown calls (through a pointer) aren't followed.
"""
import argparse, pathlib, re

HEADER = re.compile(r"// (\S+) @ ([0-9a-f]{8})\n")
CALL = re.compile(r"\b(FUN_[0-9a-f]{8}|d2d_\w+)\s*\(")


def load(path=pathlib.Path(__file__).parent / "project" / "game_all.c"):
    """{name: address} and {address: body} from game_all.c."""
    text = path.read_text(encoding="utf-8")
    heads = list(HEADER.finditer(text))
    names = {m.group(1): m.group(2) for m in heads}
    bodies = {m.group(2): text[m.end():heads[i + 1].start() if i + 1 < len(heads) else None] for i, m in enumerate(heads)}
    return names, bodies


def address_of(arg, names):
    return names.get(arg) or re.sub(r"(?i)^(fun_|0x)", "", arg).lower().rjust(8, "0")


def callees(address, names, bodies):
    body = bodies.get(address, "")
    body = body[body.find("{") + 1:]                 # past the signature
    for match in CALL.finditer(body):
        name = match.group(1)
        yield name, names.get(name) or (name[4:] if name.startswith("FUN_") else None)


def walk(start, limit, target, calls):
    """Lines: the calls `limit` levels down from `start` (a tree), or each call to `target`
    reached, with a shortest path to it. `calls(address)` yields (name, address or None). A
    function is walked once per depth it's first reached at, again only by a shorter path: a
    cycle ends and a short path isn't lost, but two routes of one length to a function give
    one path (listing every route grows exponentially in game.exe's shared helpers)."""
    out, best = [], {}

    def visit(address, level, path):
        if best.get(address, limit + 1) <= level:
            return
        best[address] = level
        for name, callee in calls(address):
            if target:
                if callee == target:
                    out.append(" -> ".join(path + [name]))
                elif callee and level + 1 < limit:
                    visit(callee, level + 1, path + [name])
            else:
                out.append("  " * level + name)
                if callee and level + 1 < limit:
                    visit(callee, level + 1, path)

    visit(start, 0, [start])
    return list(dict.fromkeys(out)) if target else out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("start", nargs="+")
    parser.add_argument("--to", help="list only the calls to this function, each with a shortest path")
    parser.add_argument("--depth", type=int, default=6, help="levels of calls below a start (1: its own calls)")
    options = parser.parse_args()
    names, bodies = load()
    target = address_of(options.to, names) if options.to else None
    for start in options.start:
        out = walk(address_of(start, names), max(options.depth, 1), target, lambda a: callees(a, names, bodies))
        print(f"== {start}" + (f": {len(out)} call(s) to {options.to}" if target else ""))
        print("\n".join(out))


if __name__ == "__main__":
    main()
