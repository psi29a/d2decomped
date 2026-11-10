# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the sized rectangle draw FUN_0046efd0 in stock 1.14d.

Run: uv run python engine_rect_size.py [--mutation-check]
FUN_0046efd0 (ECX x, EDX y, stack: width, height, colour, mode) turns a
position and a size into the corner pair and calls the rectangle draw
FUN_004f6300 with (x, y, x + width, y + height, colour, mode), returning its
result. The draw is hooked. The claim: the corner arithmetic (32-bit wrap),
the argument order, the returned value and the 0x10 bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32

MUT = set()


def suite(test):
    """Run FUN_0046efd0."""
    test.callee("rect", 0x4F6300, 6, ())

    def build(img, rng):
        """Random position, size and draw arguments."""
        edge = (0, 1, M32, 0x7FFFFFFF, 0x80000000)

        def pick():
            """An edge or random 32-bit value."""
            return rng.choice(edge + (rng.randrange(1 << 32),))
        return {"ecx": pick(), "edx": pick(), "stack": [pick(), pick(), pick(), pick()]}

    def model(m, args):
        """The corner pair and the draw call."""
        x, y = args["ecx"], args["edx"]
        width, height, colour, mode = args["stack"]
        right = (x + (height if "right" in MUT else width)) & M32
        if "args" in MUT:
            colour, mode = mode, colour
        return m.call("rect", x, y, right, (y + (width if "swap" in MUT else height)) & M32, colour, mode)
    test.run("FUN_0046efd0", 0x46EFD0, build, model, 4)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("swap", "right", "args")}
    rig.run_suite(suite, mutations, "ENGINE rect size", args.mutation_check)


if __name__ == "__main__":
    main()
