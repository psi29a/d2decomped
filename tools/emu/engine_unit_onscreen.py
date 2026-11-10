# SPDX-License-Identifier: GPL-3.0-or-later
"""Check GfxUtil.cpp FUN_004db670 (is the unit on screen, then its layer value) in stock 1.14d.

Run: uv run python engine_unit_onscreen.py [--mutation-check]
ECX is the unit, EDX the COF layer. The unit's screen offset comes from the
automap-mode path (FUN_004f51d0 set: three conversions and a quarter-screen
shift in the two side-panel modes) or from the plain position getters; then
FUN_004709a0 (on-screen test, hooked) must pass, the unit's COF node for its
mode must exist (loaded by FUN_0046fa80 when missing), and the layer's byte 2
(FUN_004db0d0) is returned. Every callee is hooked and returns this case's
synthetic values. The claim: which path is taken, the offset arithmetic, the
lookup order and arguments, and the returned value (0 when any step fails).
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32

MUT = set()


def suite(test):
    """Run FUN_004db670 over both offset paths and every failure point."""
    sel = {}
    unit, owner = test.slot(0), test.slot(1)

    def convert(emu):
        """FUN_00620410: write this case's two offsets through its output pointers."""
        emu.w32(emu.arg(2), sel["a"])
        emu.w32(emu.arg(3), sel["b"])
        return sel["r1"]

    def token(emu):
        """FUN_0064f380: note the output pointer, give this case's token."""
        sel["buf"] = emu.arg(2)
        return sel["tok"]

    test._callee("automap", 0x4F51D0, 0, (), lambda _emu: sel["automap"])
    test._callee("convert", 0x620410, 4, (), convert, nlog=2)
    test._callee("convert2", 0x6203B0, 2, (), lambda _emu: sel["r2"])
    test._callee("apply", 0x4F6760, 1, ())
    test._callee("pmode", 0x45AE90, 0, (), lambda _emu: sel["pmode"])
    test._callee("posx", 0x620650, 1, (), lambda _emu: sel["px"])
    test._callee("subx", 0x45AFC0, 0, (), lambda _emu: sel["sx"])
    test._callee("posy", 0x6206B0, 1, (), lambda _emu: sel["py"])
    test._callee("suby", 0x45AFD0, 0, (), lambda _emu: sel["sy"])
    test._callee("onscreen", 0x4709A0, 3, ("ecx", "edx"), lambda _emu: sel["onscreen"])
    test._callee("hastate", 0x63A7B0, 2, (), lambda _emu: sel["hastate"])
    test._callee("owner", 0x4639D0, 0, ("ecx",), lambda _emu: sel["owner"])
    test._callee("token", 0x64F380, 5, (), token)
    test._callee("node", 0x46E5A0, 1, ("ecx", "edx"), lambda _emu: sel["node"])
    test._callee("load", 0x46FA80, 1, ("ecx", "edx"), lambda _emu: sel["load"])
    test._callee("unitmode", 0x46DA60, 0, ("ecx",), lambda _emu: sel["mode"])
    test._callee("data", 0x46E570, 0, ("ecx", "edx"), lambda _emu: sel["data"])
    test._callee("layer", 0x4DB0D0, 0, ("ecx", "edx"))
    nonzero = lambda rng: rng.choice((0, 1, 0x1234))

    def build(img, rng):
        """Random lookups, offsets and screen width."""
        edge = (0, 1, -1, 3, -3, 4, 0x7FFFFFFF, -0x80000000, 100)
        sel.update(automap=rng.randrange(2), a=rng.choice(edge) & M32, b=rng.choice(edge) & M32, r1=rng.randrange(1 << 32),
                   r2=rng.randrange(1 << 32), pmode=rng.choice((0, 1, 2, 3)), px=rng.choice(edge) & M32, sx=rng.choice(edge) & M32,
                   py=rng.choice(edge) & M32, sy=rng.choice(edge) & M32, onscreen=nonzero(rng), hastate=rng.randrange(2),
                   owner=owner if rng.randrange(2) else 0, tok=rng.randrange(1 << 32), node=nonzero(rng), load=nonzero(rng),
                   mode=rng.randrange(1 << 32), data=nonzero(rng))
        img[unit - test.arena + 0xC8:unit - test.arena + 0xCC] = rng.choice((0, 8)).to_bytes(4, "little")
        test.m.w32(0x71146C, rng.choice((0, 1, 7, 800, -5, -4, 0x7FFFFFFF)) & M32)
        return {"ecx": unit, "edx": rng.randrange(1 << 32)}

    def model(m, args):
        """The unit-on-screen chain."""
        a = b = 0
        if m.call("automap"):
            a, b = sel["a"], sel["b"]
            r1 = m.call("convert", unit, 0)
            r2 = m.call("convert2", unit, r1)
            m.call("apply", r2)
            kind = m.call("pmode")
            width = test.m.r32(0x71146C)
            quarter = (s32(width) + ((s32(width) >> 31) & 3)) >> 2
            if kind == 1:
                a = (a - quarter) & M32
            elif kind == 2 and "mode2" not in MUT:
                a = (a + quarter) & M32
        else:
            a = (m.call("posx", unit) - m.call("subx")) & M32
            b = (m.call("posy", unit) - m.call("suby")) & M32
        if not m.call("onscreen", unit, a, b, 0, 0):
            return 0
        owner_unit = unit
        if (m.r(unit + 0xC8) >> 3) & 1 and m.call("hastate", unit, 0x25):
            found = m.call("owner", unit)
            if found:
                owner_unit = found
        tok = m.r(owner_unit + 0x60)
        m.call("token", unit, tok, sel["buf"], 0xFFFFFFFF, 1)
        if not m.call("node", unit, sel["tok"], 0xFFFFFFFF):
            if not m.call("load", unit, sel["tok"], 0xFFFFFFFF) and "load" not in MUT:
                return 0
        mode = m.call("unitmode", unit)
        data = m.call("data", unit, mode)
        if not data:
            return 0
        return m.call("layer", data, args["edx"] ^ (1 if "layer" in MUT else 0))
    test.run("FUN_004db670", 0x4DB670, build, model, 0)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1200
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("mode2", "load", "layer")}
    rig.run_suite(suite, mutations, "ENGINE unit on screen", args.mutation_check)


if __name__ == "__main__":
    main()
