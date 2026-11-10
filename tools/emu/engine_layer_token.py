# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the GfxUtil.cpp layer token and tint FUN_004dad80 and FUN_004db570 in stock 1.14d.

Run: uv run python engine_layer_token.py [--mutation-check]
FUN_004dad80 (EBX layer, ESI unit) gives the layer's weapon token: `lit `
from layer 16 up, an equipped crossbow's alternate token for layers 5 and 6,
`lit ` for a bow on layer 5, `shh ` for layer 7 with state 0x65 and a shield.
FUN_004db570 (ECX unit, EDX layer, one stack argument) gives the layer's tint:
the colour of the item worn on that component (the torso for the armour
layers), else the stack argument. The state, owner, token, equipment, item
table and item-flag lookups (FUN_0063a7b0 ... FUN_006285f0, FUN_0062c100,
FUN_0064f420) are hooked and return this case's synthetic values. The claim:
the layer limits, the owner / own token source, the branch order, the
arguments of each lookup, the returned values and the bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import s32

MUT = set()
LIT, XBW, BOW, SHH = 0x2074696C, 0x20776278, 0x20776F62, 0x20687368
FILE = 0x6DB974


def suite(test):
    """Run both functions over every branch."""
    sel = {}
    unit, owner, item, row = test.slot(0), test.slot(1), test.slot(2), test.slot(3)

    def token(emu):
        """FUN_0064f380: note the output pointer, give this case's token."""
        sel["buf"] = emu.arg(2)
        return sel["tok"]

    def tint(emu):
        """FUN_0062c100: note the output pointer, give this case's colour."""
        sel["buf2"] = emu.arg(2)
        return sel["tint"]

    test._callee("hastate", 0x63A7B0, 2, (), lambda _emu: sel["hastate"])
    test._callee("owner", 0x4639D0, 0, ("ecx",), lambda _emu: sel["owner"])
    test._callee("token", 0x64F380, 5, (), token)
    test._callee("equipped", 0x63C050, 2, (), lambda _emu: sel["items"].pop(0) if sel["items"] else 0)
    test._callee("itemrow", 0x6335F0, 1, (), lambda _emu: sel["row"])
    test._callee("state", 0x639DF0, 2, (), lambda _emu: sel["state"])
    test._callee("shield", 0x63C8F0, 2, (), lambda _emu: sel["shield"])
    test._callee("player", 0x46D9F0, 0, ("ecx",), lambda _emu: sel["player"])
    test._callee("armor", 0x64F420, 1, (), lambda _emu: sel["armor"])
    test._callee("flags", 0x6280A0, 4, (), lambda _emu: sel["flags"].pop(0))
    test._callee("code", 0x6285F0, 1, (), lambda _emu: sel["code"])
    test._callee("colour", 0x62C100, 4, (), tint, nlog=4)

    def source(m, line_unit):
        """The token source: the owner's +0x60 when the unit carries state 0x25 and has an owner, else its own."""
        if (m.r(line_unit + 0xC8) >> 3) & 1:
            if m.call("hastate", line_unit, 0x25):
                found = m.call("owner", line_unit)
                if found:
                    return m.r(found + 0x60)
        return m.r(line_unit + 0x60)

    def build_token(img, rng):
        """Random layer, token and lookups for FUN_004dad80."""
        sel.update(hastate=rng.randrange(2), owner=owner if rng.randrange(3) else 0,
                   tok=rng.choice((XBW, XBW, BOW, rng.randrange(1 << 32))), item=item if rng.randrange(4) else 0,
                   row=row if rng.randrange(4) else 0, state=rng.randrange(2), shield=rng.randrange(2))
        sel["items"] = [sel["item"]]
        for off in (0x80, 0x90):
            img[row - test.arena + off:row - test.arena + off + 4] = rng.choice((0, rng.randrange(1, 1 << 32))).to_bytes(4, "little")
        img[unit - test.arena + 0xC8:unit - test.arena + 0xCC] = rng.choice((0, 8, 0xFFFFFFFF)).to_bytes(4, "little")
        return {"regs": {"ebx": rng.randrange(-2, 20) & rig.M32, "esi": unit}}

    def weapon_token(m, args):
        """The token a layer's weapon gives."""
        layer = s32(args["regs"]["ebx"])
        if layer >= (0x11 if "limit" in MUT else 0x10):
            return LIT
        edi = source(m, unit)
        m.call("token", unit, edi, sel["buf"], 0xFFFFFFFF, 1)
        tok = sel["tok"]
        if tok == XBW:
            if layer in (5, 6):
                weapon = m.call("equipped", edi, 5)
                if not weapon:
                    return LIT
                found = m.call("itemrow", m.r(weapon + 4))
                if not found:
                    return LIT
                return m.r(found + 0x90) or m.r(found + 0x80)
        elif tok == BOW and layer == 5 and "bow" not in MUT:
            return LIT
        if layer == 7:
            if m.call("state", unit, 0x65) and m.call("shield", edi, 0):
                return SHH
        return 0
    test.run("FUN_004dad80", 0x4DAD80, build_token, weapon_token, 0)

    def build_tint(img, rng):
        """Random unit, layer and lookups for FUN_004db570."""
        sel.update(hastate=rng.randrange(2), owner=owner if rng.randrange(3) else 0, tok=rng.randrange(1 << 32),
                   item=item if rng.randrange(4) else 0, player=rng.choice((0, 0, 0, 1)), armor=rng.randrange(2),
                   flags=[rng.choice((0, 0, 0, 5)), rng.choice((0, 0, 0, 5))], code=rng.choice((LIT, 1, 2)),
                   tint=rng.choice((0, 0, 7, 0xFFFFFFFF)))
        # the first equipped lookup may fail (armour layers retry on slot 1)
        sel["items"] = [sel["item"], item if rng.randrange(3) else 0]
        img[unit - test.arena + 0x60:unit - test.arena + 0x64] = rng.choice((0, 1, 0x1234)).to_bytes(4, "little")
        img[unit - test.arena + 0xC8:unit - test.arena + 0xCC] = rng.choice((0, 8, 0xFFFFFFFF)).to_bytes(4, "little")
        return {"ecx": unit if rng.randrange(8) else 0, "edx": rng.randrange(-2, 20) & rig.M32, "stack": [rng.randrange(1 << 32)]}

    def layer_tint(m, args):
        """The colour of the item on the layer, else the default."""
        layer, default, who = s32(args["edx"]), args["stack"][0], args["ecx"]
        if layer >= 0x10:
            return default
        if not who:
            return default
        if m.call("player", who):
            return default
        edi = source(m, who)
        if not edi:
            return default
        found = m.call("equipped", edi, layer & rig.M32)
        if not found:
            if layer == 1:
                return default
            if not m.call("armor", layer & rig.M32):
                return default
            found = m.call("equipped", edi, 1)
            if not found:
                return default
        if m.call("flags", found, 0x100, 0x656, FILE):
            return default
        if m.call("flags", found, 0x4000, 0x657, FILE):
            return default
        if m.call("code", found) == LIT:
            return 0
        colour = m.call("colour", who, found, sel["buf2"], 0)
        return colour if colour and "colour" not in MUT else default
    test.run("FUN_004db570", 0x4DB570, build_tint, layer_tint, 1)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("bow", "colour", "limit")}
    rig.run_suite(suite, mutations, "ENGINE layer token", args.mutation_check)


if __name__ == "__main__":
    main()
