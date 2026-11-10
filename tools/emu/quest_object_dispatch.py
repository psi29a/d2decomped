# SPDX-License-Identifier: GPL-3.0-or-later
"""Check quest object-class dispatch FUN_005447a0 in stock 1.14d.

Run: python quest_object_dispatch.py [--mutation-check]
Both handlers are hooked; their calls and arguments are compared, not their
implementations. The Objects table global points to a synthetic table.
"""
import argparse

import _dungeon_rig as rig_mod
from _dungeon_rig import s32

MUTATIONS = set()


def suite(rig):
    """Sweep class IDs and signed table bounds around both dispatch values."""
    base = rig.arena
    table = base + 0x1000
    rig.m.w32(0x744304, table)
    rig.callee("handler_c9", 0x59b6c0, 2, ())
    rig.callee("handler_14b", 0x59b6d0, 2, ())
    cases = [(cls, count) for cls in (0, 0xc8, 0xc9, 0xca, 0x14a, 0x14b, 0x14c, 0xffffffff)
             for count in (0, 0xc9, 0xca, 0x14b, 0x14c, 0x80000000)]

    def build(img, rng):
        """Write a table size and choose both arbitrary handler arguments."""
        cls, count = cases[rig.cases % len(cases)]
        img[0x1a80:0x1a84] = count.to_bytes(4, "little")
        return {"stack": [rng.randrange(1 << 32), rng.randrange(1 << 32), cls], "count": count}

    def model(m, args):
        """Apply signed bounds, then dispatch matching object classes."""
        first, second, cls = args["stack"]
        bound = s32(args["count"])
        if s32(cls) < 0 or s32(cls) >= bound + (1 if "bound" in MUTATIONS else 0):
            return
        if cls == (0xc8 if "class" in MUTATIONS else 0xc9):
            m.call("handler_c9", first, second)
        elif cls == 0x14b:
            m.call("handler_14b", second if "argument" in MUTATIONS else first, second)

    # Rig runs 24 cases per call; repeat to cover the full Cartesian product.
    for _ in range(2):
        rig.run("FUN_005447a0", 0x5447a0, build, model, 3, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run comparisons or demonstrate that deliberate defects are detected."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("bound", "class", "argument")},
                      "quest object dispatch", args.mutation_check)


if __name__ == "__main__":
    main()
