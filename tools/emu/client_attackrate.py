# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the attackrate list the client's own skill starts put on a unit, in stock game.exe 1.14d.

    uv run python client_attackrate.py [--mutation-check]

Runs game.exe's own code for the client side of what skill_start_attackrate.py
checks on the server, against hand-written models on random arenas (_skill_rig.py):

- FUN_004c7be0 (Skills.cpp line 0x9e5, ECX unit, EDX value): the twin of
  FUN_0056e520. A flags-4 stat list (FUN_006251f0(0, 4, 0, the unit's type and
  id; 6 / -1 for no unit)); when made: hung on the unit (FUN_00626e10(unit,
  list, 1)), attackrate (stat 0x44) = value (FUN_00627150), the unit re-rated
  (FUN_00623f50(unit, ".\\SKILLS\\Skills.cpp", 0x9e5)).
- FUN_004f26d0, cltstfunc 9 (table 0x727a90; Dragon Tail's only; ECX unit, EDX
  skill id, one stack word): the skill's Param4 (+0x154) as it is to
  FUN_004c7be0, or 0 for an id outside the table. Returns 1.
- FUN_004c7cf0, cltstfunc 27 (Double Swing's): cltcalc1 (+0x114) at the skill
  and level (FUN_00646ca0(unit, calc, skill, level)) to FUN_004c7be0; 0 and
  nothing else for an id outside the table. Returns 1 or 0.

Hooked, logged, not claimed: FUN_006251f0, FUN_00626e10, FUN_00627150,
FUN_00623f50 (for the two cltstfuncs, FUN_004c7be0 itself too), FUN_00646ca0.
`--mutation-check` breaks the model five ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32
from _skill_rig import FILE_SKILLS, SKILL_ROW, SKILLS, fill_tables, point_table, put, skill_id

MUT = set()
STATE = {}
UNIT = 0x400


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig, twin):
        """Hook the callees; `twin`: FUN_004c7be0 is hooked (the two cltstfuncs' rig), else it runs."""
        self.rig = rig
        self.base = rig.arena
        point_table(rig)
        STATE.clear()
        for name, addr, nstack in (("new_list", 0x6251F0, 5), ("hang_list", 0x626E10, 3), ("set_stat", 0x627150, 4),
                                   ("rerate", 0x623F50, 3), ("calc", 0x646CA0, 4)):
            rig.callee(name, addr, nstack)
        rig.custom["new_list"] = lambda e: STATE["list"]
        if twin:
            rig.callee("rate_list", 0x4C7BE0, 0, ("ecx", "edx"))

    def unit(self, rng):
        """The user: null now and then, else the arena unit."""
        return 0 if rng.random() < 0.15 else self.base + UNIT

    def run_twin(self):
        """FUN_004c7be0 itself."""
        rig = self.rig
        rig_mod.ROUNDS = 300

        def build(img, rng):
            """ECX a unit or null, EDX any value; the list maker gives a list or null."""
            STATE["list"] = rng.choice((0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1))
            return {"ecx": self.unit(rng), "edx": rng.choice((0, -40, 1, -1, 50, rng.getrandbits(32)))}

        def model(m, a):
            """A flags-4 list for the unit; when made: hung, attackrate = the value, the unit re-rated."""
            unit, value = a["ecx"], a["edx"]
            kind, uid = (m.r(unit), m.r(unit + 0xC)) if unit else (6, M32)
            stat_list = m.call("new_list", 0, 2 if mut("flags") else 4, 0, kind, uid)
            if stat_list == 0:
                return None
            m.call("hang_list", unit, stat_list, 1)
            m.call("set_stat", stat_list, 0x45 if mut("stat") else 0x44, value, 0)
            m.call("rerate", unit, FILE_SKILLS, 0x12AB if mut("line") else 0x9E5)
            return None
        rig.run("FUN_004c7be0", 0x4C7BE0, build, model, 0)

    def run_clients(self):
        """The two cltstfuncs, FUN_004c7be0 hooked."""
        rig = self.rig
        rig_mod.ROUNDS = 300

        def build(img, rng):
            """ECX unit, EDX skill id, one stack word (the level); random rows."""
            count, _ = fill_tables(img, rng, rig)
            for k in range(max(count, 0) if count < 8 else 0):
                put(img, SKILLS + SKILL_ROW * k + 0x154, rng.choice((-40, 0, 10, rng.getrandbits(32))))
                put(img, SKILLS + SKILL_ROW * k + 0x114, rng.choice((0, 0, rng.getrandbits(32))))
            return {"ecx": self.unit(rng), "edx": skill_id(rng, count),
                    "stack": [rng.choice((1, 5, 20, rng.getrandbits(32)))], "count": count}

        def row_of(a):
            """The skill's row address, or None outside the table (signed compare, as the code)."""
            skill, count = rig_mod.s32(a["edx"]), a["count"]
            if skill < 0 or skill >= rig_mod.s32(count & M32):
                return None
            return self.base + SKILLS + SKILL_ROW * skill

        def model_nine(m, a):
            """FUN_004f26d0: Param4 (+0x154) for a skill in the table, else 0, to FUN_004c7be0; 1."""
            row = row_of(a)
            value = 0 if row is None else m.r(row + (0x150 if mut("param") else 0x154))
            m.call("rate_list", a["ecx"], value)
            return 1
        rig.run("FUN_004f26d0", 0x4F26D0, build, model_nine, 1)

        def model_twenty_seven(m, a):
            """FUN_004c7cf0: nothing and 0 outside the table, else cltcalc1 (+0x114) at the skill and level."""
            row = row_of(a)
            if row is None:
                return 0
            level = a["stack"][0]
            value = m.call("calc", a["ecx"], m.r(row + (0x118 if mut("calc") else 0x114)), a["edx"], level)
            m.call("rate_list", a["ecx"], value)
            return 1
        rig.run("FUN_004c7cf0", 0x4C7CF0, build, model_twenty_seven, 1)


def suite(rig):
    """Run the cases (the cltstfuncs on a second rig, where FUN_004c7be0 is hooked); returns the number of cases."""
    Suite(rig, False).run_twin()
    clients = rig_mod.Rig()
    Suite(clients, True).run_clients()
    return rig.cases + clients.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("flags", "stat", "line", "param", "calc")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "client attackrate", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
