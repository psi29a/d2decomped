# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the COF parameter builder FUN_004db7b0 (GfxUtil.cpp) in stock 1.14d.

Run: uv run python engine_cof_params.py [--mutation-check]
FUN_004db7b0 (ECX graphics record, EDX unit, EAX mode override or -1, one
stack argument: the item class out pointer) fills the record the cel loader
works from: type (+8), class (+0xc), mode (+0x10, the override, else the
transformed mode), by unit type. Type 3 (missile) and 4 (item) call their
file setters, type 5 (warp) and a type above 6 give 0, type 6 or a null unit
flags the record 2 and takes the Overlay.txt row + 2. Types 0..2 write -1 to
the out pointer, take the composit token (0x7c8920 + component), the
weapon class (FUN_0064f380), make sure the COF node and data are loaded,
write 'lit ' and the unit token, then scan the data's layers (0x1c, nine
bytes each, stop at a layer value of 0x10 or more) for the record's
component. Player: local-player flag 8, mode token (0x7c88d0), the worn item
and armour token rules; monster: mode token (0x7c8980) and the
FUN_00664860 overlay token (modes 0 and 0xc only with a Monstats2 flag);
object: mode token (0x7c8960). The padded layer token, then the zero checks,
the 'xxx ' / 'lit ' defaults and the item class lookup (FUN_00633640) end
it. Every callee is hooked. The claim: the dispatch, every field written,
the layer scan, the branch order, the arguments and the result.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX

MUT = set()
LIT, XXX = 0x2074696C, 0x20787878
FILE = 0x6DB974


def suite(test):
    """Run FUN_004db7b0."""
    sel = {}
    gfx, unit, data, hdrp, out = test.slot(0), test.slot(1), test.slot(2), test.arena + 0x1000, test.slot(3)
    rows = test.arena + 0x2000

    def transform(e):
        """Note the site and the locals, write this case's type / class / mode."""
        sel["site"] = (e.arg(4), e.arg(5))
        sel["seen"] = (e.r32(e.arg(1)), e.r32(e.arg(2)), e.r32(e.arg(3)))
        e.w32(e.arg(1), sel["type"])
        e.w32(e.arg(2), sel["cls"])
        e.w32(e.arg(3), sel["mode"])
        return 0

    def token(e):
        """FUN_0064f380: note the output pointer, give the weapon class."""
        sel["buf"] = e.arg(2)
        return sel["wc"]

    def palette(e):
        """FUN_004da770: note DL (the component byte), give the palette."""
        sel["dl"] = e.mu.reg_read(UC_X86_REG_EDX) & 0xFF
        return sel["pal"]

    def armour(e):
        """FUN_00664860 is hooked plainly; FUN_004daab0 notes its output pointer."""
        sel["p20"] = e.arg(1)
        return 0

    def pad(e):
        """FUN_004da720: write the padded token through EAX."""
        e.w32(e.mu.reg_read(UC_X86_REG_EAX), sel["pad"])
        return 0

    def lookup(e):
        """FUN_00633640: write the class through the out pointer, give the outcome."""
        e.w32(e.arg(1), sel["item"])
        return sel["found"]

    test._callee("mode", 0x621190, 1, (), lambda _e: 0x33)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("missile", 0x4DA8E0, 1, ("eax",))
    test._callee("fold", 0x4DAA70, 0, ("ecx", "edx"))
    test._callee("itemname", 0x4DABC0, 0, ("esi", "edi"))
    test._callee("inv", 0x46DB30, 0, ("ecx",), lambda _e: sel["inv"])
    test._callee("token", 0x64F380, 5, (), token, nlog=5)
    test._callee("node", 0x46E5A0, 1, ("ecx", "edx"), lambda _e: sel["node"])
    test._callee("load", 0x46FA80, 1, ("ecx", "edx"), lambda _e: sel["load"])
    test._callee("data", 0x46E570, 0, ("ecx", "edx"), lambda _e: sel["data"])
    test._callee("gfxinfo", 0x463E10, 0, ("ecx",), lambda _e: sel["gfxinfo"])
    test._callee("unittoken", 0x4DAA00, 1, ("ecx", "edx"))
    test._callee("local", 0x463DD0, 0, (), lambda _e: sel["local"])
    test._callee("armour", 0x4DAAB0, 2, ("ecx", "edx"), armour, nlog=1)
    test._callee("worn", 0x63C050, 2, (), lambda _e: sel["worn"])
    test._callee("colours", 0x64F420, 1, (), lambda _e: sel["colours"])
    test._callee("tier", 0x64F500, 2, (), lambda _e: sel["tier"], nlog=1)
    test._callee("wtoken", 0x4DAD80, 0, ("ebx", "esi"), lambda _e: sel["wtok"])
    test._callee("usable", 0x62A4E0, 1, (), lambda _e: sel["usable"])
    test._callee("code", 0x6285F0, 1, (), lambda _e: sel["code"])
    test._callee("pal", 0x4DA770, 0, ("ecx",), palette)
    test._callee("overlay", 0x664860, 3, (), lambda _e: sel["ovl"])
    test._callee("flagbit", 0x4638A0, 0, ("ecx", "edx"), lambda _e: sel["flagbit"])
    test._callee("pad", 0x4DA720, 0, ("edx",), pad)
    test._callee("lookup", 0x633640, 2, (), lookup, nlog=1)

    def build(img, rng):
        """A random record, unit, layer list and lookups."""
        def put(addr, value, size=4):
            """Store an arena value."""
            off = addr - test.arena
            img[off:off + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        comp = rng.choice((0, 1, 2, 3, 5, 7, 0xF, 0x10, 0x11, 0x14))
        put(gfx + 5, comp, 1)
        put(gfx + 0x14, rng.choice((-1, 0, 1, 2, 3, 0x7FFFFFFF, 0x80000000)))
        count = rng.choice((0, 1, 2, 3, 4, 0x80))
        put(data, count, 1)
        for i in range(5):
            put(data + 0x1C + 9 * i, rng.choice((comp, comp, comp, rng.randrange(0x20))), 1)
        put(hdrp + 0xBBC, rows if rng.randrange(8) else 0)
        put(hdrp + 0xBC0, rng.choice((1, 2, 3)))
        test.m.w32(0x744304, hdrp)
        for a in range(0x7C88D0, 0x7C89D0, 4):
            test.m.w32(a, rng.choice((0, rng.randrange(1 << 32))))
        sel.update(type=rng.choice((0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF)), cls=rng.randrange(1 << 32),
                   mode=rng.randrange(0x14), inv=rng.choice((0, 0x1234, 0x1234)), wc=rng.randrange(1 << 32),
                   node=rng.choice((0, 1, 1)), load=rng.choice((0, 1, 1)), data=data if rng.randrange(8) else 0,
                   gfxinfo=rng.choice((0, 1, 1, 1)), local=unit if rng.randrange(2) else rng.randrange(1 << 32),
                   worn=rng.choice((0, 0x55, 0x55)), colours=rng.randrange(2), tier=rng.randrange(4),
                   wtok=rng.choice((0, 0, rng.randrange(1 << 32))), usable=rng.randrange(2), code=rng.choice((0, rng.randrange(1 << 32))),
                   pal=rng.randrange(1 << 32), ovl=rng.choice((0, rng.randrange(1 << 32))), flagbit=rng.randrange(2),
                   pad=rng.choice((0, rng.randrange(1, 1 << 32))), item=rng.randrange(1 << 32), found=rng.randrange(2))
        return {"ecx": gfx, "edx": unit if rng.randrange(8) else 0,
                "regs": {"eax": rng.choice((M32, M32, rng.randrange(0x14)))}, "stack": [out]}

    def model(m, args):
        """The record fill."""
        who, equip = args["edx"], args["regs"]["eax"]
        if not who:
            kind = 6
            m.w(gfx + 8, 6)
        else:
            old = (m.r(who), m.r(who + 4))
            m.call("mode", who)
            m.call("transform", who)
            rig.require(sel["site"] == (FILE, 0x2F0), "transform site")
            rig.require(sel["seen"] == old + (0x33,), "transform locals")
            kind = sel["type"]
            m.w(gfx + 8, kind)
            m.w(gfx + 0xC, sel["cls"])
            m.w(gfx + 0x10, sel["mode"] if equip == M32 else equip)
            if kind > 6:
                return 0
        if kind == 3:
            m.call("missile", who, gfx)
            return 1
        if kind == 4:
            m.call("fold", who, gfx + 0x10)
            m.call("itemname", who, gfx)
            return 1
        if kind == 5:
            return 0
        if kind == 6:
            m.w(gfx + 4, m.r(gfx + 4, 1) | 2, 1)
            ident = s32(m.r(gfx + 0x14))
            if ident < 0 or ident >= s32(m.r(hdrp + 0xBC0)):
                return 1
            row = (m.r(hdrp + 0xBBC) + ident * 0x84) & M32
            if row:
                m.w(gfx + 0x2C, (row + 2) & M32)
            return 1
        if "out" not in MUT:
            m.w(out, M32)
        inv = m.call("inv", who)
        comp = m.r(gfx + 5, 1)
        mode = m.r(gfx + 0x10)
        m.w(gfx + 0x1C, test.m.r32(0x7C8920 + 4 * comp))
        wc = m.call("token", who, inv, sel["buf"], mode, 0)
        m.w(gfx + 0x28, wc)
        if not m.call("node", who, wc, mode):
            if not m.call("load", who, m.r(gfx + 0x28), mode):
                return 0
        if not m.call("data", who, mode):
            return 0
        if not m.call("gfxinfo", who):
            return 0
        m.w(gfx + 0x20, LIT)
        m.w(gfx + 0x18, m.call("unittoken", who, m.r(gfx + 8), m.r(gfx + 0xC)))
        count = m.r(data, 1)
        if count <= 0:
            return 0
        i = 0
        while True:
            ent = data + 0x1C + 9 * i
            layer = m.r(ent, 1)
            if layer >= (0x11 if "limit" in MUT else 0x10):
                return 0
            if layer == comp:
                break
            i += 1
            if i >= count:
                return 0
        if kind == 0:
            if m.call("local") == who:
                m.w(gfx + 4, m.r(gfx + 4, 1) | 8, 1)
            if not inv:
                m.w(gfx + 0x20, LIT)
                return 0
            m.w(gfx + 0x24, test.m.r32(0x7C88D0 + 4 * mode))
            m.call("armour", who, inv, kind)
            item = m.call("worn", inv, comp)
            if m.call("colours", comp):
                if mode in (0x11, 0):
                    m.w(gfx + 0x20, LIT)
                else:
                    m.w(gfx + 0x20, test.m.r32(0x7C89C0 + 4 * m.call("tier", comp)))
                if not m.r(gfx + 0x20):
                    return 0
            else:
                tok = m.call("wtoken", comp, who)
                m.w(gfx + 0x20, tok)
                if item and not tok and m.call("usable", item):
                    m.w(gfx + 0x20, m.call("code", item))
                elif not tok:
                    m.w(gfx + 0x20, LIT)
                if not m.r(gfx + 0x20):
                    return 0
        elif kind == 1:
            m.w(gfx + 0x24, test.m.r32(0x7C8980 + 4 * mode))
            pal = m.call("pal", who) & 0xFF
            rig.require(sel["dl"] == comp, "palette DL")
            if mode == 0 or (mode == 0xC and "mode12" not in MUT):
                if m.call("flagbit", m.r(gfx + 0xC), 0x10):
                    m.w(gfx + 0x20, m.call("overlay", who, comp, pal))
            else:
                m.w(gfx + 0x20, m.call("overlay", who, comp, pal))
        else:
            m.w(gfx + 0x24, test.m.r32(0x7C8960 + 4 * mode))
        m.call("pad", ent + 5)
        m.w(gfx + 0x28, sel["pad"])
        if not m.r(gfx + 0x1C) or not m.r(gfx + 0x18) or not sel["pad"]:
            return 0
        if not m.r(gfx + 0x24):
            m.w(gfx + 0x24, LIT if "xxx" in MUT else XXX)
        if not m.r(gfx + 0x20):
            m.w(gfx + 0x20, LIT)
        if m.call("lookup", m.r(gfx + 0x20)):
            m.w(out, sel["item"])
        return 1
    test.run("FUN_004db7b0", 0x4DB7B0, build, model, 1)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 3000
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("limit", "mode12", "xxx", "out")}
    rig.run_suite(suite, mutations, "ENGINE COF params", args.mutation_check)


if __name__ == "__main__":
    main()
