# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the composite COF loader FUN_0046e740 in stock 1.14d.

Run: uv run python engine_cof_node.py [--mutation-check]
FUN_0046e740 (EAX unit, stack: a pointer cell, the mode) makes sure the
unit's COF list has a node for the mode: it needs a player / monster /
object unit (FUN_004db180), builds the COF path (FUN_0064fa00, with the
owner's token when the unit carries state 0x25), asks FUN_0046e690 whether the
mode's node is current, and when it is not allocates a 0x14-byte node (line
0x434, fatal 0x435), picks the cache by FUN_0046d9f0 (the unit's transformed type: 0, a player, gives
0x7a8100, any other type 0x7a78f0), finds
or loads the entry (FUN_0046dc40 / FUN_0046dcc0; 0 when the load fails), fills
the node (+0 mode, +4 token, +8 entry, +0x10 the old head) and links it at
the list head (+0x28). It returns the mode's COF data (FUN_0046e570). Every
callee is hooked. The claim: the early exits, the argument lists, the node
layout and link, the cache choice, the fatal line, the return value and the
bytes popped.
"""
import argparse

import _dungeon_rig as rig

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run FUN_0046e740 over every exit."""
    sel = {}
    unit, gfx, owner, entry, out = (test.slot(i) for i in range(5))

    def build_path(e):
        """FUN_0064fa00: note the output pointers, write this case's path byte and token."""
        sel["pbuf"], sel["l4"] = e.arg(1), e.arg(2)
        e.mu.mem_write(e.arg(1), b"x" if sel["path"] else b"\0")
        e.w32(e.arg(2), sel["token"])
        return 0

    def current(e):
        """FUN_0046e690: note the stack cell pointer, give this case's answer."""
        sel["cell"] = e.arg(1)
        return sel["current"]

    def pool(e):
        """FUN_0040b380: a fresh node or a failure."""
        if sel["poolfail"]:
            return 0
        sel["node"] = e.alloc(0x14)
        return sel["node"]

    test._callee("kind", 0x4DB180, 0, ("ecx",), lambda _e: sel["kind"])
    test._callee("hastate", 0x63A7B0, 2, (), lambda _e: sel["hastate"])
    test._callee("owner", 0x4639D0, 0, ("ecx",), lambda _e: sel["owner"])
    test._callee("path", 0x64FA00, 7, (), build_path, nlog=7)
    test._callee("gfxinfo", 0x463E10, 0, ("ecx",), lambda _e: sel["gfx"])
    test._callee("current", 0x46E690, 3, ("eax", "edi"), current, nlog=3)
    test._callee("pool", 0x40B380, 2, ("ecx", "edx"), pool)
    test._callee("utype", 0x46D9F0, 0, ("ecx",), lambda _e: sel["utype"])
    test._callee("find", 0x46DC40, 2, (), lambda _e: sel["found"])
    test._callee("loadfile", 0x46DCC0, 1, ("eax",), lambda _e: sel["loaded"])
    test._callee("data", 0x46E570, 0, ("ecx", "edx"), lambda _e: sel["data"])

    def build(img, rng):
        """Random outcomes for every callee."""
        sel.update(kind=rng.choice((0, 1)), hastate=rng.randrange(2), owner=owner if rng.randrange(2) else 0,
                   path=rng.choice((0, 1, 1, 1)), token=rng.randrange(1 << 32), gfx=gfx if rng.randrange(6) else 0,
                   current=rng.choice((0, 1, 1)), poolfail=rng.choice((0, 0, 0, 1)), utype=rng.randrange(2),
                   found=rng.choice((0, entry)), loaded=rng.choice((0, entry)), data=rng.randrange(1 << 32))
        img[unit - test.arena + 0xC8:unit - test.arena + 0xCC] = rng.choice((0, 8)).to_bytes(4, "little")
        return {"regs": {"eax": unit}, "stack": [out, rng.randrange(1 << 32)]}

    def model(m, args):
        """The node maker."""
        cell, mode = args["stack"]
        if not m.call("kind", unit):
            return 0
        src = unit
        if (m.r(unit + 0xC8) >> 3) & 1 and m.call("hastate", unit, 0x25):
            found = m.call("owner", unit)
            if found:
                src = found
        tok = m.r(src + 0x60)
        m.call("path", unit, sel["pbuf"], sel["l4"], 1, 1, tok, mode)
        if not sel["path"]:
            return 0
        info = m.call("gfxinfo", unit)
        if not info:
            return 0
        token = sel["token"]
        if m.call("current", mode, unit, token, sel["cell"], cell):
            pointer = m.call("pool", 0x14, FILE, 0x434, 0)
            if not pointer:
                raise rig.Halt(0x435)
            buf = bytearray(0x14)
            m.blocks.append((pointer, buf))
            m.regions.append((pointer, buf))
            cache = 0x7A78F0 if m.call("utype", unit) else 0x7A8100
            entry_ptr = m.call("find", cache if "cache" not in MUT else 0x7A8100, sel["pbuf"])
            if not entry_ptr:
                entry_ptr = m.call("loadfile", sel["pbuf"], cache)
                if not entry_ptr:
                    return 0
            m.w(pointer, mode)
            m.w(pointer + 4, token if "token" not in MUT else mode)
            m.w(pointer + 8, entry_ptr)
            m.w(pointer + 0x10, 0 if "link" in MUT else m.r(info + 0x28))
            m.w(info + 0x28, pointer)
        return m.call("data", unit, mode)
    test.run("FUN_0046e740", 0x46E740, build, model, 2)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("cache", "token", "link")}
    rig.run_suite(suite, mutations, "ENGINE COF node", args.mutation_check)


if __name__ == "__main__":
    main()
