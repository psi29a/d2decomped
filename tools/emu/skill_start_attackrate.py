# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the attackrate a Bash-family or Dragon Tail start puts on its user, in stock game.exe 1.14d.

    uv run python skill_start_attackrate.py [--mutation-check]

Runs game.exe's own code for FUN_0056e520 (Skills.cpp: ECX unit, EDX value, no
stack arguments) and for the two server starts that call it, up to that call,
against hand-written models on random arenas (_skill_rig.py).

- FUN_0056e520: a stat list (FUN_006251f0(0, flags 4, 0, the unit's type and id;
  6 / -1 for no unit)); when one is made, it is hung on the unit
  (FUN_00626e10(unit, list, 1)), its attackrate (stat 0x44) set to the value
  (FUN_00627150(list, 0x44, value, 0)) and the unit re-rated (FUN_00623f50(unit,
  ".\\SKILLS\\Skills.cpp", 0x12ab)). Flags 4 is the list SetMode
  (FUN_00624690 -> FUN_006272e0) frees at the unit's next mode change.
- FUN_005d7090, srvstfunc 27 (Dragon Tail): for a skill inside the Skills table,
  the value is the row's Param4 (+0x154) as it is, before anything else, with or
  without a target.
- FUN_005d7ea0, srvstfunc 32 (Bash, Stun, Concentrate, Conversion, BearSmite):
  only with a target (FUN_00553540(game, unit)) outside a town
  (FUN_0061ab00(FUN_00620bb0(target)) == 0), the value is calc3 (+0x140) at the
  skill and level (FUN_00646ca0(unit, calc, skill, level)).

Each start is stopped at its call of FUN_0056e520 (logged with ECX and EDX),
so the rest of it (the to-hit roll, the damage) is not claimed here.
Hooked, logged, not claimed: FUN_006251f0, FUN_00626e10, FUN_00627150,
FUN_00623f50, FUN_00553540, FUN_00620bb0, FUN_0061ab00, FUN_00646ca0.
`--mutation-check` breaks the model five ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt
from _skill_rig import FILE_SKILLS, SKILL_ROW, SKILLS, fill_tables, point_table, put, skill_id

MUT = set()
STATE = {}
UNIT = 0x400
GAME = 0x500
STOP = 0x56E520      # the halt "line" the stop at FUN_0056e520 reports


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig, starts):
        """Hook the callees; the list maker's, target's and town test's answers come from STATE.

        `starts`: this rig runs the two starts, so FUN_0056e520 is hooked (a stop) instead of run.
        """
        self.rig = rig
        self.base = rig.arena
        point_table(rig)
        STATE.clear()
        for name, addr, nstack in (("new_list", 0x6251F0, 5), ("hang_list", 0x626E10, 3), ("set_stat", 0x627150, 4),
                                   ("rerate", 0x623F50, 3), ("target", 0x553540, 2), ("room", 0x620BB0, 1),
                                   ("town", 0x61AB00, 1), ("calc", 0x646CA0, 4)):
            rig.callee(name, addr, nstack)
        rig.custom["new_list"] = lambda e: STATE["list"]
        rig.custom["target"] = lambda e: STATE["target"]
        rig.custom["town"] = lambda e: STATE["town"]
        if starts:
            def at_rate_list(e):
                """FUN_0056e520 reached from a start: note it and stop the run there."""
                rig.halts.append(STOP)
                e.mu.emu_stop()
                return 0
            rig.callee("rate_list", STOP, 0, ("ecx", "edx"))
            rig.custom["rate_list"] = at_rate_list

    def unit(self, img, rng):
        """The user: null now and then, else the arena unit with a random type and id."""
        return 0 if rng.random() < 0.15 else self.base + UNIT

    def run_list(self):
        """FUN_0056e520 itself."""
        rig = self.rig
        rig_mod.ROUNDS = 300

        def b_list(img, rng):
            """ECX a unit or null, EDX any value; the list maker gives a list or null."""
            STATE["list"] = rng.choice((0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1))
            value = rng.choice((0, -40, 1, -1, 50, rng.getrandbits(32)))
            return {"ecx": self.unit(img, rng), "edx": value}

        def m_list(m, a):
            """A flags-4 list for the unit; when made: hung, attackrate = the value, the unit re-rated."""
            unit, value = a["ecx"], a["edx"]
            kind, uid = (m.r(unit), m.r(unit + 0xC)) if unit else (6, M32)
            stat_list = m.call("new_list", 0, 2 if mut("flags") else 4, 0, kind, uid)
            if stat_list == 0:
                return None
            m.call("hang_list", unit, stat_list, 1)
            m.call("set_stat", stat_list, 0x45 if mut("stat") else 0x44, value, 0)
            m.call("rerate", unit, FILE_SKILLS, 0x12AB)
            return None
        rig.run("FUN_0056e520", 0x56E520, b_list, m_list, 0)

    def run_starts(self):
        """The two starts, stopped at their call of FUN_0056e520."""
        rig = self.rig
        rig_mod.ROUNDS = 300

        def b_start(img, rng):
            """ECX game, EDX unit, stack skill and level; random rows, target and town answers."""
            count, _ = fill_tables(img, rng, rig)
            for k in range(max(count, 0) if count < 8 else 0):
                put(img, SKILLS + SKILL_ROW * k + 0x154, rng.choice((-40, 0, 10, rng.getrandbits(32))))
                put(img, SKILLS + SKILL_ROW * k + 0x140, rng.choice((0, 0, rng.getrandbits(32))))
            STATE["target"] = rng.choice((0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1))
            STATE["town"] = rng.choice((0, 0, 1, rng.getrandbits(32)))
            return {"ecx": self.base + GAME, "edx": self.unit(img, rng),
                    "stack": [skill_id(rng, count), rng.choice((1, 5, 20, rng.getrandbits(32)))], "count": count}

        def row_of(m, a):
            """The skill's row address, or None outside the table (signed compare, as the code)."""
            skill, count = rig_mod.s32(a["stack"][0]), a["count"]
            if skill < 0 or skill >= rig_mod.s32(count & M32):
                return None
            return self.base + SKILLS + SKILL_ROW * skill

        def m_tail(m, a):
            """FUN_005d7090: Param4 (+0x154) to FUN_0056e520 first, for any skill in the table."""
            row = row_of(m, a)
            if row is None:
                return 0
            value = m.r(row + (0x150 if mut("param") else 0x154))
            m.call("rate_list", a["edx"], value)
            raise Halt(STOP)
        rig.run("FUN_005d7090", 0x5D7090, b_start, m_tail, 2)

        def m_bash(m, a):
            """FUN_005d7ea0: a target outside a town, then calc3 (+0x140) at skill and level to FUN_0056e520."""
            row = row_of(m, a)
            if row is None:
                return 0
            game, unit = a["ecx"], a["edx"]
            target = m.call("target", game, unit)
            if target == 0:
                return 0
            room = m.call("room", target)
            if m.call("town", room) != 0 and not mut("town"):
                return 0
            skill, level = a["stack"]
            value = m.call("calc", unit, m.r(row + (0x13C if mut("calc") else 0x140)), skill, level)
            m.call("rate_list", unit, value)
            raise Halt(STOP)
        rig.run("FUN_005d7ea0", 0x5D7EA0, b_start, m_bash, 2)


def suite(rig):
    """Run the cases (the starts on a second rig, where FUN_0056e520 is hooked); returns the number of cases."""
    Suite(rig, False).run_list()
    starts = rig_mod.Rig()
    Suite(starts, True).run_starts()
    return rig.cases + starts.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("flags", "stat", "param", "town", "calc")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "skill start attackrate", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
