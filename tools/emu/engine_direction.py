# SPDX-License-Identifier: GPL-3.0-or-later
"""Check GfxUtil direction conversion FUN_004db290 against game.exe 1.14d.

Run: uv run python engine_direction.py [--mutation-check]
The direction-table lookup FUN_00600cb0 is hooked: only the wrapper's
selection, arguments, and writeback are claimed. The rig compares callee
order and arguments, all arena bytes, and stack cleanup.
"""
import argparse
import itertools
import random

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Check both supported conversions and unsupported pairs."""
    result = [0]
    test._callee("lookup", 0x600cb0, 4, (), lambda _emu: result[0])
    rng = random.Random(11441)
    pairs = itertools.cycle(((8, 4), (16, 8), (8, 8), (16, 4), (0, 4)))

    def build(img, _rng):
        """Provide a packed input word and synthetic lookup output."""
        source, target = next(pairs)
        value = rng.getrandbits(32)
        result[0] = rng.getrandbits(32)
        img[:4] = value.to_bytes(4, "little")
        return {"ecx": target, "regs": {"eax": source, "esi": test.slot(0)}}

    def model(state, args):
        """Apply the corresponding extraction and packed result mask."""
        source, target = args["regs"]["eax"], args["ecx"]
        pointer = args["regs"]["esi"]
        if (source, target) not in ((8, 4), (16, 8)):
            return
        shift = 3 if source == 8 else 2
        mask = 0x1fffffff if source == 8 else 0x0fffffff
        index = (state.r(pointer) & mask) >> shift
        output = state.call("lookup", index, source, target + ("target" in MUT), 0)
        output_mask = 0x1ffffff if "mask" in MUT else 0x3ffffff
        state.w(pointer, (output & output_mask) << (shift + ("shift" in MUT)))

    test.run("FUN_004db290", 0x4db290, build, model, 0, ret=False)
    return test.cases


def main():
    """Run the comparison and optional mutation checks."""
    rig.ROUNDS = 512
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("mask", "shift", "target")}
    rig.run_suite(suite, mutations, "ENGINE direction conversion", args.mutation_check)


if __name__ == "__main__":
    main()
