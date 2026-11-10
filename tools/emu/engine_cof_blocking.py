# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the blocking COF preload FUN_0046fb60 in stock 1.14d.

Run: uv run python engine_cof_blocking.py [--mutation-check]
FUN_0046fb60 (EAX unit, EBX mode, stack: the blocking flag) makes sure the
unit's COF node for the mode exists (FUN_0046e5a0, else FUN_0046fa80; if both
fail it prints the `sAsyncLoadBlock failed ...` line, 0x6d6cc8, and returns 0),
walks the unit's COF list to the node, takes the COF data's header (fatal
0x6c3) and layer order (fatal 0x6d6), and for each layer (16 or more is
fatal 0x6dd) fills a request (FUN_004dbb50) and starts the cel load
(FUN_006001f0 with the flag twice). A load that is not done is polled with
FUN_006004e0: a failed poll returns -1, a finished one with bit 2 of the
request's +0x44 clear makes the result 0 (a pending load). It returns 1 when
every layer is in. The state, owner, token, node, loader, header, order,
request, cel and sprintf calls are hooked. The claim: the lookup chain, the
fatal lines, the argument lists, the layer loop and the result.
"""
import argparse

import _dungeon_rig as rig
from unicorn.x86_const import UC_X86_REG_ECX

MUT = set()
FILE = 0x6D6C1C


def suite(test):
    """Run FUN_0046fb60 over every branch."""
    sel = {}
    unit, owner, gfx, _node, graphic, data, header, order = (test.slot(i) for i in range(8))
    nodes = [test.slot(i) for i in (8, 9)]

    def token(e):
        """FUN_0064f380: note the output pointer, write and give the token."""
        sel["p20"] = e.arg(2)
        e.w32(e.arg(2), sel["l20"])
        return sel["tok"]

    def transform(e):
        """Note the output pointers and write this case's values."""
        sel["pt"], sel["pc"], sel["pm"] = e.arg(1), e.arg(2), e.arg(3)
        sel["seen"] = (e.r32(e.arg(1)), e.r32(e.arg(2)))
        sel["site"] = (e.arg(4), e.arg(5))
        e.w32(e.arg(1), sel["type"])
        e.w32(e.arg(2), sel["cls"])
        e.w32(e.arg(3), 0x66)
        return 0

    def param(e):
        """FUN_004db2e0: note the output pointers, write the order selector."""
        sel["p24"], sel["p8"], sel["p2c"] = e.arg(2), e.arg(3), e.arg(4)
        e.w32(e.arg(3), sel["v8"])
        return 0

    def request(e):
        """FUN_004dbb50: note the request buffer, give the next outcome."""
        sel["p78"] = e.mu.reg_read(UC_X86_REG_ECX)
        sel["p30"] = e.arg(3)
        return sel["req"].pop(0)

    def poll(e):
        """FUN_006004e0: give the next outcome and set the flags byte."""
        flag = sel["flags"].pop(0)
        sel["flaglog"].append(flag)
        e.mu.mem_write(e.arg(0) + 0x44, bytes([flag]))
        return sel["poll"].pop(0)

    test._callee("hastate", 0x63A7B0, 2, (), lambda _e: sel["hastate"])
    test._callee("owner", 0x4639D0, 0, ("ecx",), lambda _e: sel["owner"])
    test._callee("token", 0x64F380, 5, (), token, nlog=5)
    test._callee("node", 0x46E5A0, 1, ("ecx", "edx"), lambda _e: sel["node"])
    test._callee("load", 0x46FA80, 1, ("ecx", "edx"), lambda _e: sel["load"])
    test._callee("print", 0x6823FD, 0, (), lambda e: sel.__setitem__("pbuf", e.arg(0)) or 0, nlog=8)
    test._callee("gfxinfo", 0x463E10, 0, ("ecx",), lambda _e: sel["gfx"])
    test._callee("header", 0x4DB040, 0, ("ecx",), lambda _e: sel["header"])
    test._callee("al", 0x620100, 1, (), lambda _e: sel["al"])
    test._callee("mode", 0x621190, 1, (), lambda _e: 0x55)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("param", 0x4DB2E0, 7, ("ecx", "edx"), param, nlog=7)
    test._callee("order", 0x4DB110, 1, ("ecx", "edx"), lambda _e: sel["order"])
    test._callee("request", 0x4DBB50, 6, ("ecx", "edx"), request, nlog=6)
    test._callee("cel", 0x6001F0, 3, (), lambda _e: sel["cel"].pop(0))
    test._callee("poll", 0x6004E0, 1, (), poll)

    def build(img, rng):
        """Random chain outcomes."""
        def put(addr, value, size=4):
            """Store an arena value."""
            img[addr - test.arena:addr - test.arena + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        mode = rng.choice((0, 3, 5))
        sel.update(hastate=rng.randrange(2), owner=owner if rng.randrange(2) else 0, tok=rng.randrange(1 << 32),
                   l20=rng.randrange(1 << 32), node=rng.choice((0, 1, 1)), load=rng.choice((0, 1, 1)),
                   gfx=gfx if rng.randrange(8) else 0, header=header if rng.randrange(8) else 0, al=rng.randrange(256),
                   type=rng.randrange(4), cls=rng.randrange(1 << 32), v8=rng.randrange(1 << 32),
                   order=order if rng.randrange(8) else 0)
        sel["req"] = [rng.choice((0, 1, 1)) for _ in range(4)]
        sel["cel"] = [rng.choice((0, 1)) for _ in range(4)]
        sel["poll"] = [rng.choice((0, 1, 1)) for _ in range(4)]
        sel["flags"] = [rng.choice((0, 4, 0xFB, 0xFF)) for _ in range(4)]
        put(unit + 0xC8, rng.choice((0, 8)))
        # COF list: two nodes, the wanted mode at a random place or missing
        modes = [mode if rng.randrange(3) else 1, mode if rng.randrange(2) else 2]
        put(gfx + 0x28, nodes[0])
        for i, n in enumerate(nodes):
            put(n, modes[i])
            put(n + 0x10, nodes[i + 1] if i + 1 < len(nodes) else 0)
            put(n + 8, graphic)
        put(graphic + 0x14, rng.choice((0, data)))
        img[header - test.arena + 2] = rng.choice((0, 1, 2))
        img[header - test.arena] = rng.choice((0, 1, 2, 3))
        for i in range(4):
            img[order - test.arena + i] = rng.choice((0, 5, 0xF, 0x10, 0xF0))
        sel["mode"] = mode
        sel["flaglog"] = []
        return {"regs": {"eax": unit, "ebx": mode}, "stack": [rng.choice((0, 1, 0x77))]}

    def model(m, args):
        """The blocking preload."""
        mode, flag = args["regs"]["ebx"], args["stack"][0]
        src = unit
        if (m.r(unit + 0xC8) >> 3) & 1 and m.call("hastate", unit, 0x25):
            found = m.call("owner", unit)
            if found:
                src = found
        tok = m.call("token", unit, m.r(src + 0x60), sel["p20"], mode, 0)
        if not m.call("node", unit, tok, mode) and not m.call("load", unit, tok, mode):
            m.call("print", sel["pbuf"], 0x6D6CC8, flag, m.r(unit), m.r(unit + 4), mode, sel["l20"], tok)
            return 0
        info = m.call("gfxinfo", unit)
        if not info:
            return 0
        cur = m.r(info + 0x28)
        while cur and m.r(cur) != mode:
            cur = m.r(cur + 0x10)
        if not cur:
            return 0
        entry = m.r(m.r(cur + 8) + 0x14)
        if not entry:
            return 0
        hdr = m.call("header", entry)
        if not hdr:
            raise rig.Halt(0x6C3)
        layers = m.r(hdr + 2, 1)
        m.call("al", unit)
        m.call("mode", unit)
        m.call("transform", unit)
        rig.require(sel["site"] == (FILE, 0x6CD), "transform site")
        rig.require(sel["seen"] == (m.r(unit), m.r(unit + 4)), "transform locals")
        m.call("param", unit, sel["type"], sel["cls"], mode, sel["p24"], sel["p8"], sel["p2c"], layers, 0)
        ords = m.call("order", entry, sel["v8"], 0)
        if not ords:
            raise rig.Halt(0x6D6)
        result, seen = 1, 0
        for k in range(m.r(hdr, 1)):
            layer = m.r(ords + k, 1)
            if layer >= (0x11 if "order" in MUT else 0x10):
                raise rig.Halt(0x6DD)
            al = m.call("al", unit)
            if not m.call("request", sel["p78"], unit, 0, al, 0, sel["p30"], layer, mode):
                continue
            if m.call("cel", sel["p78"], flag, flag if "flag" not in MUT else 0):
                continue
            polled = m.call("poll", sel["p78"])
            if not polled:
                return 0xFFFFFFFF
            seen += 1
            if not sel["flaglog"][seen - 1] & (8 if "bit" in MUT else 4):
                result = 0
        return result

    test.run("FUN_0046fb60", 0x46FB60, build, model, 1)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1000
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("bit", "flag", "order")}
    rig.run_suite(suite, mutations, "ENGINE COF blocking", args.mutation_check)


if __name__ == "__main__":
    main()
