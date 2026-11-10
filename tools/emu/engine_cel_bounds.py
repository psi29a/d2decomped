# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the GfxUtil cel visibility test FUN_004dab40 against game.exe 1.14d.

Run: uv run python engine_cel_bounds.py [--mutation-check]
The four cel metric getters are hooked and their return values varied; their
internals are not claimed. The rig checks their arguments and order, arena
bytes, the return value and stack cleanup. Arithmetic wraps to 32 bits.
"""
import argparse
import random

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Compare the visibility calculation over boundary and random values."""
    values = [0, 0, 0, 0]
    for index, (name, address) in enumerate((("radius", 0x6018f0), ("y", 0x601950),
                                             ("x", 0x601920), ("width", 0x6018c0))):
        test._callee(name, address, 1, (), lambda _emu, index=index: values[index])
    test.m.w32(0x71146c, 800)
    test.m.w32(0x711470, 600)
    rng = random.Random(11440)
    boundaries = iter(((800, 1), (0, 1), (1, 0)))

    def value():
        """Choose a signed-coordinate boundary or an arbitrary dword."""
        return rng.choice((0, 1, 599, 600, 601, 799, 800, 801, 0xffffffff,
                           0x7fffffff, 0x80000000, rng.getrandbits(32)))

    def build(_img, _rng):
        """Set four metrics, a cel pointer, position and horizontal flip."""
        boundary = next(boundaries, None)
        if boundary is not None:
            values[:] = [0, 0, 0, 0]
            return {"ecx": test.slot(0), "edx": boundary[0], "stack": [boundary[1], 0]}
        values[:] = [value() for _ in values]
        return {"ecx": test.slot(0), "edx": value(), "stack": [value(), rng.randrange(2)]}

    def model(state, args):
        """Apply the four signed screen comparisons with x86 wraparound."""
        cel = args["ecx"]
        radius, cel_y, cel_x, width = (state.call(name, cel) for name in ("radius", "y", "x", "width"))
        x = (cel_x + args["edx"]) & rig.M32
        y = (cel_y + args["stack"][0]) & rig.M32
        if args["stack"][1]:
            x = (x - radius) & rig.M32
            width = (width + radius) & rig.M32
        right = rig.s32((width + x) & rig.M32)
        top = rig.s32((y - radius) & rig.M32)
        bottom = rig.s32((y + radius) & rig.M32)
        return int(rig.s32(x) <= (799 if "x_limit" in MUT else 800)
                   and right >= (1 if "right" in MUT else 0)
                   and top <= 600 and bottom >= (1 if "bottom" in MUT else 0))

    test.run("FUN_004dab40", 0x4dab40, build, model, 2, mask=0xff)
    return test.cases


def main():
    """Run the oracle or check that boundary mutations are detected."""
    rig.ROUNDS = 512
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("x_limit", "right", "bottom")}
    rig.run_suite(suite, mutations, "ENGINE cel bounds", args.mutation_check)


if __name__ == "__main__":
    main()
