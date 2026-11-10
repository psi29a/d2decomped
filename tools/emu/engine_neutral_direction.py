# SPDX-License-Identifier: GPL-3.0-or-later
"""Check GfxUtil neutral/death direction update FUN_004db1e0 in stock 1.14d.

Run: uv run python engine_neutral_direction.py [--mutation-check]
Mode, transform and path setters are hooked. The transform hook supplies
synthetic type and mode; those callees' own behavior is outside this claim.
"""
import argparse
import itertools

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Exercise transformed unit types, modes, directions and enable flag."""
    selected = {}
    test._callee("mode", 0x621190, 1, (), lambda _emu: 0)

    def transform(emu):
        """Replace the type and mode locals with this case's synthetic values."""
        emu.w32(emu.arg(1), selected["type"])
        emu.w32(emu.arg(3), selected["mode"])
        return 0

    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test.callee("path", 0x6488a0, 2, ())
    unit, path = test.slot(0), test.slot(1)
    cases = itertools.cycle(itertools.product((0, 1, 2), (0, 0xc, 0x11, 2), (0, 1), (0, 1)))

    def build(img, _rng):
        """Set the unit path and custom-register inputs for one case."""
        kind, mode, differs, enabled = next(cases)
        selected.update(type=kind, mode=mode)
        img[0x2c:0x30] = path.to_bytes(4, "little")
        return {"regs": {"esi": unit, "ebx": 1, "edi": 1 + differs}, "stack": [3, enabled]}

    def model(m, args):
        """Change the path only for an enabled neutral/death unit with a new direction."""
        if not args["stack"][1] and "enable" not in MUT:
            return
        owner = args["regs"]["esi"]
        m.call("mode", owner)
        m.call("transform", owner)
        kind, mode = selected["type"], selected["mode"]
        allowed = (kind == 0 and mode in (0, 0x11)) or (kind == 1 and mode in (0, 0xc))
        if "mode" in MUT and kind == 1 and mode == 0xc:
            allowed = False
        if allowed and (args["regs"]["ebx"] != args["regs"]["edi"] or "same" in MUT):
            m.call("path", m.r(owner + 0x2c), args["stack"][0])

    test.run("FUN_004db1e0", 0x4db1e0, build, model, 2, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and optional deliberate defects."""
    rig.ROUNDS = 96
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("enable", "mode", "same")}
    rig.run_suite(suite, mutations, "ENGINE neutral direction", args.mutation_check)


if __name__ == "__main__":
    main()
