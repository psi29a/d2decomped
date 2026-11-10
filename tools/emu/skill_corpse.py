# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the six Skills.cpp corpse-selection predicates in stock game.exe 1.14d.

    uv run python skill_corpse.py [--mutation-check]

Runs game.exe's own code for FUN_00645510 (corpse a skill can consume: stdcall
unit and a "count the dying too" flag, then the class's Velocity must be
nonzero), FUN_006454c0 (the same test with the unit in ESI, answering only
whether the corpse flag is set), FUN_00645590 and FUN_00645630 (two identical
copies: a corpse of a soft monster, revivable), FUN_006455e0 and FUN_006456d0
(two identical copies: any selectable corpse) against hand-written models on
random units (_dungeon_rig.py): return value, callee log and the bytes `ret n`
pops. A unit qualifies when it is a monster (type 1) in mode 12 (corpse; mode 0,
death, also counts for the first two when the flag is set), has no passive
state (FUN_0063a770) and its class has the corpse-selectable bit (MonStats2
flag 7, FUN_004638a0). Hooked, logged, not claimed: FUN_0063a770, FUN_004638a0,
FUN_00621f60 (soft monster). `--mutation-check` breaks the model four ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32
from _skill_rig import MON_ROW, MONSTATS, fill_tables, point_table, put

MUT = set()
STATE = {}
UNIT = 0x400


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the three callees; their answers come from STATE."""
        self.rig = rig
        self.base = rig.arena
        point_table(rig)
        STATE.clear()
        for name, addr, nstack, regs in (("passive", 0x63A770, 1, ()), ("corpse_flag", 0x4638A0, 0, ("ecx", "edx")),
                                         ("soft", 0x621F60, 1, ())):
            rig.callee(name, addr, nstack, regs)
        rig.custom["passive"] = lambda e: STATE["passive"]
        rig.custom["corpse_flag"] = lambda e: STATE["flag"]
        rig.custom["soft"] = lambda e: STATE["soft"]

    def build(self, img, rng):
        """A unit (null, monster or other type, mode 0 / 12 / other) and random callee answers; returns (unit, count)."""
        _, count = fill_tables(img, rng, self.rig)
        put(img, UNIT, rng.choice((1, 1, 1, 0, 2)))
        mode = rng.choice((12, 12, 0, 0, 1, 7))
        put(img, UNIT + 0x10, mode)
        cls = rng.choice((0, 0, rng.randrange(0, count), -1 & M32, count, 0x7FFFFFFF))
        put(img, UNIT + 4, cls)
        in_range = cls < count
        STATE["passive"] = rng.choice((0, 0, 0, 1, 0xFFFFFFFF))
        STATE["flag"] = rng.choice((0, 1, 1, 7, rng.getrandbits(32))) if in_range else 0   # the flag lookup is 0 for a bad class
        STATE["soft"] = rng.choice((0, 1, 0x100, rng.getrandbits(32)))
        # Velocity (word +0x32 of the MonStats row), zero a third of the time
        for k in range(count):
            put(img, MONSTATS + MON_ROW * k + 0x32, rng.choice((0, 0, rng.getrandbits(16))), 2)
        return 0 if rng.random() < 0.12 else self.base + UNIT

    def run(self):
        """All six functions."""
        rig = self.rig

        def prefix(m, unit, dying):
            """The shared gate: a monster in mode 12 (or 0 when `dying`), no passive state; True when it passes."""
            if unit == 0 or m.r(unit) != (2 if mut("type") else 1):
                return False
            mode = m.r(unit + 0x10)
            if mode != 0xC and not (dying and mode == 0):
                return False
            return m.call("passive", unit) == 0

        def corpse(m, unit):
            """FUN_004638a0(class, 7): is the class corpse-selectable."""
            return m.call("corpse_flag", m.r(unit + 4), 7) != 0

        def b_consumable(img, rng):
            """FUN_00645510: stdcall (unit, bCheckDead)."""
            return {"stack": [self.build(img, rng), rng.choice((0, 1, 2, 0xFFFFFFFF))]}

        def m_consumable(m, a):
            """Gate, class flag, then the class's Velocity != 0 (a bad class cannot get here: its flag is 0)."""
            unit, dying = a["stack"]
            if not prefix(m, unit, dying != 0) or not corpse(m, unit):
                return 0
            vel = m.r(self.base + MONSTATS + MON_ROW * m.r(unit + 4) + 0x32, 2)
            return int((vel == 0) if mut("velocity") else (vel != 0))
        rig.run("FUN_00645510", 0x645510, b_consumable, m_consumable, 2)

        def b_esi(img, rng):
            """FUN_006454c0: ESI the unit, stack the flag."""
            return {"regs": {"esi": self.build(img, rng)}, "stack": [rng.choice((0, 1, 2, 0xFFFFFFFF))]}

        def m_esi(m, a):
            """Gate and class flag; answers 1 or 0 (the flag's truth, not its value)."""
            unit = a["regs"]["esi"]
            if not prefix(m, unit, a["stack"][0] != 0) or not corpse(m, unit):
                return 0
            return 1
        rig.run("FUN_006454c0", 0x6454C0, b_esi, m_esi, 1)

        def b_one(img, rng):
            """One stdcall argument: the unit."""
            return {"stack": [self.build(img, rng)]}

        def m_soft(m, a):
            """Gate (mode 12 only), class flag, then FUN_00621f60 != 0."""
            unit = a["stack"][0]
            if not prefix(m, unit, False) or not corpse(m, unit):
                return 0
            return int(m.call("soft", unit) != 0) if not mut("soft-value") else m.call("soft", unit) & 1
        for name, addr in (("FUN_00645590", 0x645590), ("FUN_00645630", 0x645630)):
            rig.run(name, addr, b_one, m_soft, 1)

        def m_any(m, a):
            """Gate (mode 12 only) and class flag: 1, else 0."""
            unit = a["stack"][0]
            if not prefix(m, unit, mut("dying-any")) or not corpse(m, unit):
                return 0
            return 1
        for name, addr in (("FUN_006455e0", 0x6455E0), ("FUN_006456d0", 0x6456D0)):
            rig.run(name, addr, b_one, m_any, 1)


def suite(rig):
    """Run the cases; returns the number of cases."""
    rig_mod.ROUNDS = 150
    Suite(rig).run()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("type", "velocity", "soft-value", "dying-any")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Skills.cpp corpse predicate", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
