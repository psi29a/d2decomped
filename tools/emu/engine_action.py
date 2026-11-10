# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the Gfx.cpp action dispatch FUN_0046f950 against stock 1.14d.

Run: uv run python engine_action.py [--mutation-check]
The death-state predicate FUN_00464820 and action dispatch FUN_00461250 are
hooked; their internals are outside this claim. The null-unit case tests only
the caller with the predicate hooked, since the real predicate dereferences it.
The rig compares callee order and arguments, arena bytes and stack cleanup.
"""
import argparse
import itertools

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Exercise every predicate, pointer and unit-type combination twice."""
    predicate = [0]
    test._callee("dead", 0x464820, 0, ("ecx",), lambda _emu: predicate[0])
    test.callee("action", 0x461250, 2, ("ecx", "edx"))
    cases = itertools.cycle(itertools.product((0, 1), (0, 1), (0, 1, 0xffffffff)))

    def build(img, _rng):
        """Set a unit type and a synthetic death-state result."""
        dead, present, unit_type = next(cases)
        predicate[0] = dead
        img[0:4] = unit_type.to_bytes(4, "little")
        return {"ecx": test.slot(0) if present else 0, "dead": dead}

    def model(state, args):
        """Dispatch action 7 only for a living player, forwarding its unit."""
        unit = args["ecx"]
        dead = state.call("dead", unit)
        if not dead and unit and (state.r(unit) == (1 if "type" in MUT else 0)):
            state.call("action", 8 if "action" in MUT else 7, unit,
                       1 if "arg" in MUT else 0, 1)

    test.run("FUN_0046f950", 0x46f950, build, model, 0, ret=False)
    return test.cases


def main():
    """Run the oracle or confirm that representative model defects are caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("type", "action", "arg")}
    rig.run_suite(suite, mutations, "ENGINE action dispatch", args.mutation_check)


if __name__ == "__main__":
    main()
