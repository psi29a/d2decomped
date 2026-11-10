# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the Gfx.cpp COF load wrappers FUN_0046e8d0, FUN_0046e980, FUN_0046ea50 in stock 1.14d.

Run: uv run python engine_cof_load.py [--mutation-check]
FUN_0046e8d0 loads a monster's COF for a mode, FUN_0046e980 an object's,
FUN_0046ea50 any unit's with no checks. The state transform, mode getter,
MonStats2 / Objects.txt lookups, layer flag get / set and the COF loader
(FUN_0046e740) are hooked and return this case's synthetic values; the claim
is the wrapper: the locals it hands the transform (type 6 / class -1 for a
null unit), the lookup chain, the fatal lines (0x486, 0x4c2, 0x4d4, 0x4dc),
the flag set only after a successful load, and the return value.
"""
import argparse
import itertools

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Run the three wrappers over null / real units and every lookup outcome."""
    sel = {}
    unit, objrow = test.slot(0), test.slot(1)

    def transform(emu):
        """Remember the locals the wrapper passed, then replace them with this case's values."""
        sel["seen"] = (emu.r32(emu.arg(1)), emu.r32(emu.arg(2)), emu.r32(emu.arg(3)))
        sel["site"] = (emu.arg(4), emu.arg(5))
        emu.w32(emu.arg(1), 1)
        emu.w32(emu.arg(2), sel["cls"])
        emu.w32(emu.arg(3), 5)
        return 0

    def load(emu):
        """FUN_0046e740: note the output slot pointer, give this case's result."""
        sel["out"] = emu.arg(0)
        return sel["load"]

    test._callee("mode", 0x621190, 1, (), lambda _emu: 0x77)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("monflag", 0x46c140, 0, ("ecx", "edx"), lambda _emu: sel["monflag"])
    test._callee("objrow", 0x640e90, 1, (), lambda _emu: sel["row"])
    test._callee("getflag", 0x4db1a0, 0, ("ecx", "edx"), lambda _emu: sel["flag"])
    test._callee("load", 0x46e740, 2, ("eax",), load, nlog=2)
    test._callee("setflag", 0x4db1c0, 1, ("ecx", "edx"))
    cases = itertools.cycle(itertools.product((0, 1), (0, 1), (0, 0x100, 1), (0, 5), (0, 1, 2), (0, 1)))

    def common(img, rng, kind):
        """Pick the case values and the register / arena set-up for wrapper `kind`."""
        null, monflag, flag, load_ok, row_kind, comp_ok = next(cases)
        sel.update(cls=rng.randrange(1 << 32), monflag=monflag, flag=flag, load=load_ok)
        comp = rng.randrange(8) if comp_ok else rng.randrange(8, 16)
        sel["row"] = 0 if row_kind == 0 else objrow
        img[objrow - test.arena + 0x13f + comp] = (1 if row_kind != 2 else rng.choice((0, 2, 0xff)))
        who = 0 if null else unit
        sel["comp"] = comp
        regs = {"esi": who, "edi": comp} if kind != "plain" else {"edi": who, "esi": comp}
        return {"regs": regs}

    def model_checked(kind):
        """The model of FUN_0046e8d0 ('mon') or FUN_0046e980 ('obj')."""
        def model(m, args):
            """Mirror the transform -> lookup -> load chain."""
            owner, comp = args["regs"]["esi"], args["regs"]["edi"]
            m.call("mode", owner)
            m.call("transform", owner)
            line = 0x464 if kind == "mon" else 0x4be
            rig.require(sel["site"] == (0x6d6c1c, line), "transform site")
            want = (6 if not owner else m.r(owner), (0xffffffff if not owner else m.r(owner + 4)), 0x77)
            rig.require(sel["seen"] == want, f"transform locals {sel['seen']} vs {want}")
            if kind == "mon":
                if not m.call("monflag", sel["cls"], comp):
                    raise rig.Halt(0x486)
            else:
                row = m.call("objrow", sel["cls"])
                if not row:
                    raise rig.Halt(0x4c2)
                if m.r(row + comp + 0x13f, 1) != (2 if "obj-mode" in MUT else 1):
                    raise rig.Halt(0x4dc)
            if m.call("getflag", owner, comp) & 0xff:
                return 0
            if not m.call("load", owner, sel["out"], comp):
                if kind == "mon":
                    return 0
                raise rig.Halt(0x4d4)
            m.call("setflag", owner, comp, 1)
            return 1
        return model

    def plain(m, args):
        """FUN_0046ea50: load, then flag only on success."""
        owner, comp = args["regs"]["edi"], args["regs"]["esi"]
        if not m.call("load", owner, sel["out"], comp):
            return 1 if "plain-ret" in MUT else 0
        if "plain-flag" not in MUT:
            m.call("setflag", owner, comp, 1)
        return 1

    for name, addr, kind in (("FUN_0046e8d0", 0x46e8d0, "mon"), ("FUN_0046e980", 0x46e980, "obj"),
                             ("FUN_0046ea50", 0x46ea50, "plain")):
        test.run(name, addr, lambda img, rng, kind=kind: common(img, rng, kind),
                 plain if kind == "plain" else model_checked(kind), 0)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 144
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("obj-mode", "plain-ret", "plain-flag")}
    rig.run_suite(suite, mutations, "ENGINE COF load wrapper", args.mutation_check)


if __name__ == "__main__":
    main()
