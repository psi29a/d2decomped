# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the item cel name FUN_004dabc0 and the transparency test FUN_004db360 in stock 1.14d.

Run: uv run python engine_item_gfx.py [--mutation-check]
FUN_004dabc0 (ESI item, EDI cel request) sets the request's name pointer
(+0x2c) and flags (+4): for a request of kind 4 (inventory) a set item's
invfile (+0x62, flag 0x10) or the base's +0x60, a unique's +0x5a (flag 1) or
the base's +0x40, else the base's +0x20 or the variant name (FUN_0062e920);
for kind 3 (ground) the unique's +0x3a or set's +0x42 when non-empty, else the
base row. FUN_004db360 (ECX unit, EDX component, stack: output, item class)
gives the transparency table in *output: a fade stat on a player / a monster
(a monster over 15 only with a carried ethereal item), the item row's
transparency (+0x12c / +0x12d), then the player's worn weapon / shield /
helm (components 5, 6, 7) that is ethereal. The class, item row, flag,
quality, id, unique / set table, fade stat, inventory and worn-item lookups
are hooked. The claim: the order of the tests, the offsets, the flags, the
fatal-free exits, the values written and the bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import s32

MUT = set()
FILE = 0x6DB974


def suite(test):
    """Run both functions."""
    sel = {}
    item, row, request, uniq, setr, out, inv, owner, wi = (test.slot(i) for i in range(9))
    test._callee("class", 0x46DAC0, 0, ("ecx",), lambda _e: sel["class"])
    test._callee("itemrow", 0x6335F0, 1, (), lambda _e: sel["row"])
    test._callee("tag", 0x6283F0, 1, (), lambda _e: sel["tag"])
    test._callee("flag", 0x6280A0, 4, (), lambda _e: sel["flags"].pop(0))
    test._callee("quality", 0x627E70, 1, (), lambda _e: sel["quality"])
    test._callee("ident", 0x629DA0, 1, (), lambda _e: sel["ident"])
    test._callee("setrow", 0x483440, 0, ("ecx",), lambda _e: sel["setrow"])
    test._callee("uniqrow", 0x483470, 0, ("ecx",), lambda _e: sel["uniqrow"])
    test._callee("variant", 0x62E8D0, 1, (), lambda _e: sel["variant"])
    test._callee("variantname", 0x62E920, 2, (), lambda _e: sel["vname"])

    def build_name(img, rng):
        """Random item rows, qualities and name tables for a kind 3 / 4 request."""
        def put(addr, value, size=4):
            """Store an arena value."""
            img[addr - test.arena:addr - test.arena + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        sel.update(row=row if rng.randrange(8) else 0, tag=rng.randrange(256), quality=rng.choice((3, 5, 7, 7, 1)),
                   ident=rng.choice((0, 1, 5, 0xFFFFFFFF)), setrow=setr if rng.randrange(3) else 0,
                   uniqrow=uniq if rng.randrange(3) else 0, variant=rng.choice((0, 1, 0x100)), vname=rng.randrange(1 << 32))
        sel["class"] = rng.randrange(1 << 32)
        sel["flags"] = [rng.choice((0, 1)) for _ in range(4)]
        for base in (row, setr, uniq):
            for off in (0x20, 0x3A, 0x40, 0x42, 0x5A, 0x60, 0x62):
                put(base + off, rng.choice((0, 0x41)), 1)
        put(request + 0x10, rng.choice((4, 4, 3, 3, 2)))
        put(request + 4, rng.randrange(1 << 32))
        put(request + 0x2C, rng.randrange(1 << 32))
        return {"regs": {"esi": item, "edi": request}}

    def name(m, args):
        """The cel name of an item."""
        cls = m.call("class", item)
        r = m.call("itemrow", cls)
        if not r:
            return
        kind = m.r(request + 0x10)

        def flags(add):
            """OR a flag into the request."""
            m.w(request + 4, m.r(request + 4) | add)
        if kind == 4:
            tag = m.call("tag", item)
            if m.call("flag", item, 0x10, 0x196, FILE) and m.call("quality", item) == 5:
                q = m.call("ident", item)
                sr = m.call("setrow", q)
                ptr = sr + (0x63 if "setname" in MUT else 0x62) if sr else 0
                if not (sr and m.r(ptr, 1)):
                    ptr = r + 0x60
                    if not m.r(ptr, 1):
                        ptr = 0
                if ptr:
                    flags(0x20 if "flag" in MUT else 0x10)
                    m.w(request + 0x2C, ptr)
                    return
            if m.call("flag", item, 0x10, 0x1AE, FILE) and m.call("quality", item) == 7:
                q = m.call("ident", item)
                ptr = 0
                if s32(q) > 0:
                    ur = m.call("uniqrow", q)
                    if ur and m.r(ur + 0x5A, 1):
                        ptr = ur + 0x5A
                if not ptr and m.r(r + 0x40, 1):
                    ptr = r + 0x40
                if ptr:
                    flags(1)
                    m.w(request + 0x2C, ptr)
                    return
            if m.call("variant", item) & 0xFF:
                m.w(request + 0x2C, m.call("variantname", item, tag))
            else:
                m.w(request + 0x2C, r + 0x20)
        elif kind == 3:
            m.w(request + 0x2C, r)
            quality = m.call("quality", item)
            ptr = 0
            if quality == 7:
                q = m.call("ident", item)
                if s32(q) < 0:
                    return
                ur = m.call("uniqrow", m.call("ident", item))
                if not ur:
                    return
                ptr = ur + 0x3A
            elif m.call("quality", item) == 5:
                sr = m.call("setrow", m.call("ident", item))
                if not sr:
                    return
                ptr = sr + (0x43 if "ground" in MUT else 0x42)
            else:
                return
            if m.r(ptr, 1):
                m.w(request + 0x2C, ptr)
    test.run("FUN_004dabc0", 0x4DABC0, build_name, name, 0, ret=False)

    # FUN_004db360 ---------------------------------------------------------
    test._callee("kind", 0x46D9F0, 0, ("ecx",), lambda _e: sel["kind"])
    test._callee("stat", 0x625480, 3, (), lambda _e: sel["fade"])
    test._callee("first", 0x63B2C0, 1, (), lambda _e: sel["first"].pop(0) if sel["first"] else 0)
    test._callee("carried", 0x63DFD0, 1, (), lambda _e: sel["carried"].pop(0) if sel["carried"] else 0)
    test._callee("nextitem", 0x63DFA0, 1, (), lambda _e: sel["next"].pop(0) if sel["next"] else 0)
    test._callee("hastate", 0x63A7B0, 2, (), lambda _e: sel["hastate"])
    test._callee("owner", 0x4639D0, 0, ("ecx",), lambda _e: sel["owner"])
    test._callee("worn", 0x63C050, 2, (), lambda _e: sel["worn"])

    def build_trans(img, rng):
        """Random fade, rows and inventories."""
        def put(addr, value, size=4):
            """Store an arena value."""
            img[addr - test.arena:addr - test.arena + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        sel.update(kind=rng.choice((0, 0, 0, 1, 2)), fade=rng.choice((0, 0, 0, 1, 15, 16, 0xFFFFFFFF, 40)),
                   row=row if rng.randrange(4) else 0, hastate=rng.randrange(2), owner=owner if rng.randrange(2) else 0,
                   worn=wi if rng.randrange(4) else 0)
        sel["first"] = [rng.choice((0, 5, 6))]
        sel["carried"] = [rng.choice((0, 3, 3)) for _ in range(4)]
        sel["next"] = [rng.choice((0, 7, 7)) for _ in range(4)]
        sel["flags"] = [rng.choice((0, 1)) for _ in range(6)]
        put(row + 0x12C, rng.choice((0, 0, 1)), 1)
        put(row + 0x12D, rng.randrange(256), 1)
        put(item + 0x60, rng.choice((0, inv)))
        put(owner + 0x60, rng.choice((0, inv)))
        put(item + 0xC8, rng.choice((0, 8)))
        return {"ecx": item, "edx": rng.choice((5, 6, 7, 7, 8, 0)), "stack": [out, rng.choice((0xFFFFFFFF, 1, 2))]}

    def sel_row(e):
        """FUN_006335f0 for FUN_004db360."""
        return sel["row"]
    test.custom["itemrow"] = sel_row

    def trans(m, args):
        """The transparency table the unit and item give."""
        component, cls = args["edx"], args["stack"][1]
        kind = m.call("kind", item)

        def done(value):
            """Store the table and answer 1."""
            m.w(out, value)
            return 1
        if kind == 0:
            if m.call("stat", item, 0xB5, 0):
                return done(1)
        elif kind == 1:
            fade = m.call("stat", item, 0xB5, 0)
            if s32(fade) > 0:
                if s32(fade) <= 0xF:
                    return done(1)
                inventory = m.r(item + 0x60)
                if not inventory:
                    return 0
                it = m.call("first", inventory)
                while it:
                    carried = m.call("carried", it)
                    if carried and m.call("flag", carried, 0x400000, 0x5E6, FILE):
                        return done(1)
                    it = m.call("nextitem", it)
                return 0
        if cls != 0xFFFFFFFF:
            r = m.call("itemrow", cls)
            if r and m.r(r + 0x12C, 1):
                return done(m.r(r + 0x12D, 1))
        if kind:
            return 0
        src = item
        if (m.r(item + 0xC8) >> 3) & 1 and m.call("hastate", item, 0x25):
            found = m.call("owner", item)
            if found:
                src = found
        inventory = m.r(src + 0x60)
        if not inventory:
            return 0
        lines = {5: 0x611, 6: 0x61C, 7: 0x627}
        if component not in lines:
            return 0
        worn = m.call("worn", inventory, component)
        if not worn:
            return 0
        if m.call("flag", worn, 0x400000, lines[component], FILE):
            return done(2 if component == 7 and "shield" not in MUT else 1)
        return 0
    test.run("FUN_004db360", 0x4DB360, build_trans, trans, 2)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("setname", "flag", "ground", "shield")}
    rig.run_suite(suite, mutations, "ENGINE item gfx", args.mutation_check)


if __name__ == "__main__":
    main()
