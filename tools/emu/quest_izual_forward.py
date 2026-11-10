# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005b30e0's quest callback wrapper against stock 1.14d.

Run: python quest_izual_forward.py [--mutation-check]
The receiving callback is hooked and its stack-local argument record is read
field by field. The callback's implementation is not exercised.
"""
import argparse

from unicorn.x86_const import UC_X86_REG_ECX

import _dungeon_rig as rig_mod

MUTATIONS = set()


def suite(rig):
    """Compare the wrapper's record layout and stack cleanup."""
    def hook(emu):
        """Log the record's nine dwords and final word at callback entry."""
        pointer = emu.mu.reg_read(UC_X86_REG_ECX)
        words = [emu.r32(pointer + 4 * index) for index in range(9)]
        final = int.from_bytes(emu.read(pointer + 0x24, 2), "little")
        rig.log.append(("callback", *words, final))
        rig.outs.append(0)
        return 0

    rig.m.hook(0x5b2a00, hook, 0)

    def build(img, rng):
        """Vary both register and stack arguments, including high word bits."""
        return {"ecx": rng.randrange(1 << 32), "edx": rng.randrange(1 << 32),
                "stack": [rng.randrange(1 << 32) for _ in range(7)]}

    def model(m, args):
        """Construct the wrapper's packed argument record for the callback."""
        values = args["stack"]
        field3 = 1 if "zero" in MUTATIONS else 0
        field4 = values[3] if "order" in MUTATIONS else values[2]
        final = values[6] if "word" in MUTATIONS else values[6] & 0xffff
        m.call("callback", args["ecx"], args["edx"], field3, field4, values[3], values[4],
               values[0], values[1], values[5], final)

    rig.run("FUN_005b30e0", 0x5b30e0, build, model, 7, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or demonstrate detection of deliberate defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("zero", "order", "word")},
                      "quest callback wrapper", args.mutation_check)


if __name__ == "__main__":
    main()
