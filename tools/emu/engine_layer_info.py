# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the GfxUtil.cpp layer predicates FUN_004dae60 and FUN_004daf70 in stock 1.14d.

Run: uv run python engine_layer_info.py [--mutation-check]
FUN_004dae60 (ECX unit, EDX layer): does the unit have that COF layer;
FUN_004daf70 (EAX kind, EBX layer, EDI unit, one stack argument): the layer's
draw type. The state transform and mode getter, the MonStats2 / Objects.txt /
class-slot / missile / Overlay.txt row lookups (FUN_00451fe0, FUN_00640e90,
FUN_0046f9d0, FUN_0046ace0, FUN_004dad50) and the local-player getter
(FUN_00463dd0) are hooked and return this case's synthetic rows. The claim:
the layer ranges, the per-type tests, the returned draw types and the stack
bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import s32

MUT = set()


def suite(test):
    """Run both predicates over every unit type, layer and lookup outcome."""
    sel = {}
    unit, mon, obj, slot, missile = (test.slot(i) for i in range(5))

    def transform(emu):
        """Replace the type / class locals with this case's values (the site is checked)."""
        sel["site"] = (emu.arg(4), emu.arg(5))
        emu.w32(emu.arg(1), sel["type"])
        emu.w32(emu.arg(2), sel["cls"])
        return 0

    test._callee("mode", 0x621190, 1, (), lambda _emu: 0)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("monstats2", 0x451FE0, 0, ("ecx",), lambda _emu: sel["mon"])
    test._callee("objects", 0x640E90, 1, (), lambda _emu: sel["obj"])
    test._callee("slot", 0x46F9D0, 0, ("ecx",), lambda _emu: sel["slot"])
    test._callee("missile", 0x46ACE0, 0, ("ecx",), lambda _emu: sel["missile"])
    test._callee("overlay", 0x4DAD50, 0, ("eax",), lambda _emu: sel["overlay"])
    test._callee("local", 0x463DD0, 0, (), lambda _emu: sel["local"])

    def build_has(img, rng):
        """Layer -3..19 for a type 0..5 unit."""
        sel.update(type=rng.randrange(6), cls=rng.randrange(1 << 32), mon=mon if rng.randrange(5) else 0,
                   obj=obj if rng.randrange(5) else 0)
        img[obj - test.arena + 0x151:obj - test.arena + 0x151 + 40] = bytes(rng.choice((0, 1, 1, 2)) for _ in range(40))
        return {"ecx": unit, "edx": rng.randrange(-4, 20) & rig.M32}

    def has(m, args):
        """Layers >= 16 (signed) are absent; player 0..4, 8, 9; monster the +0xe8 bit; object +0x151; missile layer 1."""
        n = s32(args["edx"])
        if n >= (15 if "limit" in MUT else 16):
            return 0
        m.call("mode", unit)
        m.call("transform", unit)
        rig.require(sel["site"] == (0x6DB974, 0x25B), "transform site")
        kind = sel["type"]
        if kind == 0:
            return int(0 <= n <= 9 and not 5 <= n <= 7)
        if kind == 1:
            row = m.call("monstats2", sel["cls"])
            if not row:
                return 0
            return int(bool(m.r(row + 0xE8 + (n >> 3), 1) & (1 << (n & 7))))
        if kind == 2:
            row = m.call("objects", sel["cls"])
            return int(bool(row) and m.r(row + n + 0x151, 1) == (2 if "obj" in MUT else 1))
        return int(kind == 4 and n == 1)
    test.run("FUN_004dae60", 0x4DAE60, build_has, has, 0)

    def build_draw(img, rng):
        """Kind 0..8, layer -2..20, with synthetic rows."""
        sel.update(mon=mon if rng.randrange(5) else 0, slot=slot if rng.randrange(3) else 0, missile=missile if rng.randrange(4) else 0,
                   overlay=rng.randrange(1 << 32), local=rng.randrange(1 << 32))
        sel["local"] = rng.choice((sel["local"], 0))
        layer = rng.randrange(-2, 21)
        for i in range(21):
            img[mon - test.arena + 0xF4 + i] = rng.choice((0, 3, 8, 8, 0x20))
            if rng.randrange(2):
                img[slot - test.arena + 4 * i:slot - test.arena + 4 * i + 4] = bytes(4)
        img[missile - test.arena + 0x1A0] = rng.randrange(256)
        test.m.w32(0x7A8928, rng.choice((0, 0, 1)))
        stack = sel["local"] if rng.randrange(2) else rng.randrange(1 << 32)
        return {"regs": {"eax": rng.randrange(9), "ebx": layer & rig.M32, "edi": unit}, "stack": [stack]}

    def draw(m, args):
        """Draw type per kind: 0 local player (0x10) or 8; 1 MonStats2 byte, 4 when 8 and the class slot is clear; 3 the missile's; 6 the overlay's."""
        kind, layer, owner = args["regs"]["eax"], s32(args["regs"]["ebx"]), args["regs"]["edi"]
        if layer < 0:
            return 1
        if kind > 6:
            return 1
        if kind == 0:
            if test.m.r32(0x7A8928) == 0 and m.call("local") == args["stack"][0]:
                return 0x10
            return 8
        if kind == 1:
            row = m.call("monstats2", owner)
            if not row:
                return 1
            value = m.r(row + layer + 0xF4, 1)
            if value != 8:
                return value
            table = m.call("slot", owner)
            if not table:
                return 8
            return 4 if m.r(table + layer * 4) == 0 else 8
        if kind == 3:
            row = m.call("missile", owner)
            return m.r(row + 0x1A0, 1) if row else 1
        if kind == 6:
            return m.call("overlay", owner) if "overlay" not in MUT else 1
        return 1
    test.run("FUN_004daf70", 0x4DAF70, build_draw, draw, 1)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 600
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("obj", "overlay", "limit")}
    rig.run_suite(suite, mutations, "ENGINE layer info", args.mutation_check)


if __name__ == "__main__":
    main()
