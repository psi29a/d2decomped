# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00545cd0's quest-level warning dispatch against stock 1.14d.

Run: python quest_level_warning.py [--mutation-check]
Level lookup, record lookup, quest predicate and warning are hooked and
logged. The global warning flag and returned records are synthetic.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
GAME, UNIT, ROOM, LEVEL, ROW = 0x100, 0x200, 0x300, 0x400, 0x500


def suite(rig):
    """Sweep the level, quest, flag and predicate combinations."""
    base = rig.arena
    pick = {}
    for name, addr, nstack, regs in (("level", 0x61a1b0, 1, ()), ("row", 0x61db70, 1, ()),
                                     ("predicate", 0x5436b0, 1, ("ecx", "edx")),
                                     ("warning", 0x54a930, 1, ("ecx", "edx"))):
        rig.callee(name, addr, nstack, regs)
    rig.custom["level"] = lambda emu: pick["level"]
    rig.custom["row"] = lambda emu: base + ROW
    rig.custom["predicate"] = lambda emu: pick["answer"]
    cases = [(level, quest, flag, answer, enabled) for level in (0, 1) for quest in (0, 1, 0xff)
             for flag in (0, 1) for answer in (0, 1, 2) for enabled in (0, 1)]

    def build(img, rng):
        """Set the exact combination and warning-enable global."""
        level, quest, flag, answer, enabled = cases[rig.cases % len(cases)]
        pick["level"] = base + LEVEL if level else 0
        pick["answer"] = answer
        img[ROW + 0x30] = quest
        rig.m.w32(0x8846dc, enabled)
        return {"ecx": base + GAME, "edx": base + UNIT, "stack": [base + ROOM, flag], "enabled": enabled}

    def model(m, args):
        """Send warning only for the exact predicate and enable conditions."""
        level = m.call("level", args["stack"][0])
        if not level:
            return
        row = m.call("row", level)
        quest = m.r(row + 0x30, 1)
        if not quest:
            return
        answer = m.call("predicate", args["ecx"], args["edx"], quest)
        match = answer == (2 if "equals" in MUTATIONS else 1) if not args["stack"][1] else answer == 0
        if match and (args["enabled"] or "enabled" in MUTATIONS):
            m.call("warning", args["ecx"], 0x6e0c10, 5 if "warning" in MUTATIONS else 4)

    for _ in range(3):
        rig.run("FUN_00545cd0", 0x545cd0, build, model, 2, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run comparisons or demonstrate detection of deliberate defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("equals", "enabled", "warning")},
                      "quest level warning", args.mutation_check)


if __name__ == "__main__":
    main()
