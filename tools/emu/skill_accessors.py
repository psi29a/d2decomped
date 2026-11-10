# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 23 Skills.cpp skill-list and skill-record accessors in stock game.exe 1.14d.

    uv run python skill_accessors.py [--mutation-check]

Runs game.exe's own code for FUN_00643930, FUN_00643960, FUN_00643990,
FUN_006439a0 (the skill list's left, right and current skill, fastcall),
FUN_00643a00, FUN_00643a30 (the list's light, swapped and read),
FUN_00643b70 (the item-charge flag), and the skill record's getters and
setters FUN_00644380 / 006443b0 (+0x10), 006443f0 / 00644420 (+0x14),
00644460 (clear both), 006444a0 / 00644560 (+0x18, param 1), 006444d0 /
006445a0 (+0x1c, param 2), 00644500 / 006445e0 (+0x20, param 3),
00644530 / 00644620 (+0x24, param 4), 006446a0 / 00644660 (flags, +0xc) and
00645120 (+0x30) against hand-written models on random records
(_dungeon_rig.py): return value, arena, fatal-error line (the null-record
assert with the code's own line number) and the bytes `ret n` pops. No callee
is hooked or needed. `--mutation-check` breaks the model six ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt
from _skill_rig import put

MUT = set()
REC = 0x400          # arena offset of the skill (or skill list) record every case uses


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


# name, address, field offset, assert line: records read through a null-checked pointer
GETTERS = [
    ("FUN_00644380", 0x644380, 0x10, 0xA23), ("FUN_006443f0", 0x6443F0, 0x14, 0xA35),
    ("FUN_006444a0", 0x6444A0, 0x18, 0xA52), ("FUN_006444d0", 0x6444D0, 0x1C, 0xA5B),
    ("FUN_00644500", 0x644500, 0x20, 0xA64), ("FUN_00644530", 0x644530, 0x24, 0xA6D),
]
SETTERS = [
    ("FUN_006443b0", 0x6443B0, 0x10, 0xA2C), ("FUN_00644420", 0x644420, 0x14, 0xA3E),
    ("FUN_00644560", 0x644560, 0x18, 0xA76), ("FUN_006445a0", 0x6445A0, 0x1C, 0xA7F),
    ("FUN_006445e0", 0x6445E0, 0x20, 0xA88), ("FUN_00644620", 0x644620, 0x24, 0xA91),
    ("FUN_00644660", 0x644660, 0x0C, 0xA9A), ("FUN_00645120", 0x645120, 0x30, 0xDAD),
]
# fastcall (ECX the list): name, address, field offset, assert line (None: a null list answers 0)
LIST_GETTERS = [
    ("FUN_00643930", 0x643930, 0x08, 0x3E9), ("FUN_00643960", 0x643960, 0x0C, 0x40E),
    ("FUN_006439a0", 0x6439A0, 0x10, None),
]


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Keep the rig; nothing is hooked."""
        self.rig = rig
        self.base = rig.arena

    def pick(self, rng):
        """A record pointer (the arena's REC slot), null a quarter of the time."""
        return 0 if rng.random() < 0.25 else self.base + REC

    def getters(self):
        """The stdcall record getters: field at `off`, or the code's null assert."""
        for name, addr, off, line in GETTERS:
            def build(img, rng):
                """A record of random bytes (or null)."""
                return {"stack": [self.pick(rng)]}

            def model(m, a, off=off, line=line):
                """Null asserts with the line; else the dword at `off`."""
                if a["stack"][0] == 0:
                    raise Halt(line + (1 if mut("halt-line") and off == 0x18 else 0))
                return m.r(a["stack"][0] + (0x24 if mut("par3-off") and off == 0x20 else off))
            self.rig.run(name, addr, build, model, 1)

    def setters(self):
        """The stdcall record setters: store a value at `off`, or the null assert."""
        for name, addr, off, line in SETTERS:
            def build(img, rng):
                """A record and a value of any width/sign."""
                return {"stack": [self.pick(rng), rng.choice((0, 1, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32)))]}

            def model(m, a, off=off, line=line):
                """Null asserts; else the dword is stored."""
                rec, value = a["stack"]
                if rec == 0:
                    raise Halt(line)
                m.w(rec + (off + 4 if mut("set-off") and off == 0x30 else off), value)
            self.rig.run(name, addr, build, model, 2, ret=False)

    def clear(self):
        """FUN_00644460: zero +0x10 and +0x14 (assert line 0xa47 on null)."""
        def build(img, rng):
            """A record of random bytes (or null)."""
            return {"stack": [self.pick(rng)]}

        def model(m, a):
            """Both target fields are cleared."""
            rec = a["stack"][0]
            if rec == 0:
                raise Halt(0xA47)
            m.w(rec + 0x10, 0)
            if not mut("clear-one"):
                m.w(rec + 0x14, 0)
        self.rig.run("FUN_00644460", 0x644460, build, model, 1, ret=False)

    def flags(self):
        """FUN_006446a0 (flags, 0 on null) and FUN_00643b70 (item-charge flag)."""
        def build(img, rng):
            """A record of random bytes (or null)."""
            return {"stack": [self.pick(rng)]}

        def model(m, a):
            """Flags are the dword at +0xc, 0 for no record."""
            rec = a["stack"][0]
            return (1 if mut("flags-null") else 0) if rec == 0 else m.r(rec + 0xC)
        self.rig.run("FUN_006446a0", 0x6446A0, build, model, 1)

        def build_t(img, rng):
            """A record whose owner (+0x34) is -1 about half the time."""
            rec = self.pick(rng)
            if rec:
                put(img, REC + 0x34, rng.choice((0xFFFFFFFF, 0xFFFFFFFF, rng.getrandbits(32))))
            return {"stack": [rec, rng.choice((0, 1, 7, 0xFFFFFFFF, rng.getrandbits(32)))]}

        def model_t(m, a):
            """Null asserts (0x657); an owner of -1 answers 0; else +0x3c = 1, +0x38 = value, answer 1."""
            rec, value = a["stack"]
            if rec == 0:
                raise Halt(0x657)
            if m.r(rec + 0x34) == M32:
                return 0
            m.w(rec + 0x3C, 1)
            m.w(rec + 0x38, value)
            return 1
        self.rig.run("FUN_00643b70", 0x643B70, build_t, model_t, 2)

    def list_accessors(self):
        """The fastcall list getters (+8, +0xc assert; +0x10 answers 0) and the setter FUN_00643990."""
        for name, addr, off, line in LIST_GETTERS:
            def build(img, rng):
                """A skill list (or null) in ECX."""
                return {"ecx": self.pick(rng)}

            def model(m, a, off=off, line=line):
                """Null asserts or answers 0; else the pointer at `off`."""
                if a["ecx"] == 0:
                    if line is None:
                        return 0
                    raise Halt(line)
                return m.r(a["ecx"] + off)
            self.rig.run(name, addr, build, model, 0)

        def build_s(img, rng):
            """A skill list (or null) in ECX and a skill pointer in EDX."""
            return {"ecx": self.pick(rng), "edx": rng.choice((0, rng.getrandbits(32)))}

        def model_s(m, a):
            """FUN_00643990: store EDX at +0x10 unless the list is null."""
            if a["ecx"]:
                m.w(a["ecx"] + 0x10, a["edx"])
        self.rig.run("FUN_00643990", 0x643990, build_s, model_s, 0, ret=False)

    def light(self):
        """FUN_00643a00 (swap the list's light, answer the old one) and FUN_00643a30 (read it)."""
        base = self.base

        def build(img, rng):
            """A unit at 0x400 whose +0xa8 is a list at 0x800, a null list, or the unit itself is null."""
            unit = self.pick(rng)
            put(img, REC + 0xA8, rng.choice((0, base + 0x800, base + 0x800)))
            return {"stack": [unit, rng.choice((0, rng.getrandbits(32)))]}

        def swap(m, a):
            """Null unit or null list answers 0; else the old +0x14 is returned and the new stored."""
            unit, light = a["stack"]
            lst = m.r(unit + 0xA8) if unit else 0
            if not lst:
                return 0
            old = m.r(lst + 0x14)
            m.w(lst + 0x14, light)
            return light if mut("swap-new") else old
        self.rig.run("FUN_00643a00", 0x643A00, build, swap, 2)

        def build_g(img, rng):
            """Same unit and list shapes, one argument."""
            a = build(img, rng)
            return {"stack": a["stack"][:1]}

        def get(m, a):
            """The list's +0x14 or 0."""
            unit = a["stack"][0]
            lst = m.r(unit + 0xA8) if unit else 0
            return m.r(lst + 0x14) if lst else 0
        self.rig.run("FUN_00643a30", 0x643A30, build_g, get, 1)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 40
    s = Suite(rig)
    s.getters()
    s.setters()
    s.clear()
    s.flags()
    s.list_accessors()
    s.light()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("par3-off", "clear-one", "swap-new", "flags-null", "halt-line", "set-off")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Skills.cpp skill-list and skill-record accessor", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
