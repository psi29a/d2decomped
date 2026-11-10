# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 14 Skills.cpp helpers that read the Skills.txt / MonStats rows or a unit's skills, in stock game.exe 1.14d.

    uv run python skill_table_rows.py [--mutation-check]

Runs game.exe's own code for FUN_00645a70 (damage reduction (n + 1) * 100 / (n + 8)),
FUN_00645160 (animation mode map), FUN_00645fb0 (Skills.txt flag byte & bit 1),
FUN_00645080 (charclass of a skill id, 7 = none), FUN_00645b70 / FUN_00645bc0
(Param 0/1 and 2/3 with diminishing returns, level and id checks), FUN_006447b0
(a unit's level of a skill), FUN_00644300 (its bonus levels), FUN_00644750
(required level + levels spent), FUN_00644aa0 (total level capped at the
experience table's last level), FUN_00646170 (a shapeshifted player's attack
frames), FUN_00644c20 (base damage from Strength + Dexterity or 3 x level),
FUN_00643d00 (a skill's animation sequence) and FUN_00643a60 (free a unit's
skill list) against hand-written models on random records (_dungeon_rig.py,
_skill_rig.py): return value, callee log, arena, allocator frees, fatal-error
line and the bytes `ret n` pops. sgptDataTable (0x744304) points at a random
table in the arena (counts, null base pointers, out-of-range ids).
FUN_00645a70 with n = -8 divides by zero: checked directly (the emulator faults).
Hooked, logged, not claimed: FUN_006439b0 (skill by id and owner), FUN_00643810,
FUN_00644180, FUN_00611830, FUN_00645b20, FUN_00625480 (a stat), FUN_0063a400,
FUN_00623990, FUN_0062a710, FUN_00451f80, FUN_004dc0b0 and the allocator's free.
`--mutation-check` breaks the model twelve ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32
from _skill_rig import BIT_MASKS, FILE_SKILLS, MONSTATS, SKILL_ROW, fill_tables, point_table, put, skill_id

MUT = set()
SK = 0x2000          # arena offset of the unit's skill (level +0x28, owner +0x34)
UNIT = 0x400
STATE = {}


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def tdiv(a, b):
    """C division: truncates toward zero (0 for a zero divisor, which only a defective model gets to)."""
    if b == 0:
        return 0
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed; their results come from STATE."""
        self.rig = rig
        self.base = rig.arena
        point_table(rig)
        STATE.clear()
        for name, addr, nstack, regs in (
                ("skill_by_id", 0x6439B0, 1, ("ecx", "edx")), ("get_skill", 0x643810, 0, ("eax", "esi")),
                ("level_bonus", 0x644180, 1, ("esi",)), ("max_level", 0x611830, 1, ()),
                ("diminish", 0x645B20, 3, ()), ("stat", 0x625480, 3, ()), ("shapeshifted", 0x63A400, 1, ()),
                ("weapon", 0x623990, 2, ()), ("weapon_speed", 0x62A710, 2, ()), ("monstats_row", 0x451F80, 0, ("ecx",)),
                ("mem_pool", 0x4DC0B0, 0, ("ecx",))):
            rig.callee(name, addr, nstack, regs)
        for name in ("skill_by_id", "get_skill"):
            rig.custom[name] = lambda e: STATE["skill"]
        rig.custom["level_bonus"] = lambda e: STATE["bonus"]
        rig.custom["max_level"] = lambda e: STATE["max"]
        rig.custom["stat"] = lambda e: STATE["stats"][e.arg(1)]
        rig.custom["shapeshifted"] = lambda e: STATE["shape"]
        rig.custom["weapon"] = lambda e: STATE["weapon"]
        rig.custom["weapon_speed"] = lambda e: STATE["speed"]
        rig.custom["monstats_row"] = lambda e: self.base + MONSTATS

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def row(self, m, n, ident):
        """The Skills.txt row address the code computes for `ident`, or None when it is out of range / null."""
        if s32(ident) < 0 or s32(ident) >= n:
            return None
        base = m.r(self.base + 0xB98)
        row = (base + (ident & M32) * SKILL_ROW) & M32
        return row or None

    def prepare(self, img, rng, **kw):
        """Fill the table and return (n, id): one skill id for the case."""
        n, _ = fill_tables(img, rng, self.rig, null_skills=kw.pop("null_skills", rng.random() < 0.12), **kw)
        return n, skill_id(rng, n)

    def pure(self):
        """FUN_00645a70, FUN_00645160, FUN_00645fb0, FUN_00645080: no callee."""
        rig, at = self.rig, self.at
        edge = (0, 1, -1, 7, 8, 92, -7, -9, 0x7FFFFFFF, -0x80000000, 1000)

        def build(img, rng):
            """n = an edge value or any 32-bit value, never -8 (a divide fault)."""
            n = rng.choice(edge) if rng.random() < 0.5 else rng.getrandbits(32)
            return {"stack": [0 if n & M32 == 0xFFFFFFF8 else n & M32]}

        def model(m, a):
            """(n + 1) * 100 / (n + 8): 32-bit product, divide truncating toward zero."""
            n = s32(a["stack"][0])
            div = s32(n + (7 if mut("rdr-div") else 8))
            return tdiv(s32((n + 1) * 100), div)
        rig.run("FUN_00645a70", 0x645A70, build, model, 1)
        try:
            rig.m.call(0x645A70, 0xFFFFFFF8, limit=1000)
            raise rig_mod.Mismatch("FUN_00645a70(-8) did not fault")
        except RuntimeError:
            rig.cases += 1

        def build_m(img, rng):
            """mode 0..23, class 6 half the time (the (6, 4) special)."""
            return {"regs": {"eax": rng.choice((4, 4, rng.randrange(0, 24)))}, "stack": [rng.choice((6, 6, rng.randrange(0, 8)))]}

        def model_m(m, a):
            """Class 6 and mode 4 answer 12; else the table at 0x6eb308 by mode."""
            cls, mode = a["stack"][0], a["regs"]["eax"]
            if (cls == (5 if mut("mode-class") else 6)) and mode == 4:
                return 0xC
            return rig.m.r32(0x6EB308 + 4 * mode)
        rig.run("FUN_00645160", 0x645160, build_m, model_m, 1)

        def build_f(img, rng):
            """A random table, a random mask in gadwBitMasks[1], one skill id."""
            n, ident = self.prepare(img, rng)
            STATE["mask"] = rng.choice((2, 0xFF, rng.getrandbits(32)))
            rig.m.w32(BIT_MASKS, STATE["mask"])
            return {"stack": [ident], "n": n}

        def model_f(m, a):
            """0 outside the table or on a null row; else byte +6 & the mask."""
            row = self.row(m, m.r(self.base + 0xBA0), a["stack"][0])
            if row is None:
                return 0
            byte = m.r(row + 6, 1)
            return byte if mut("mask-ignored") else byte & STATE["mask"]
        rig.run("FUN_00645fb0", 0x645FB0, build_f, model_f, 1)

        def build_c(img, rng):
            """A random table, a skill id, an out pointer (null a tenth of the time)."""
            n, ident = self.prepare(img, rng)
            return {"stack": [ident, 0 if rng.random() < 0.1 else at(0x3F00)]}

        def model_c(m, a):
            """Null out pointer asserts (0xd79); charclass byte (signed) or 7; stores it, answers class != 7."""
            ident, out = a["stack"]
            if out == 0:
                raise Halt(0xD79)
            row = self.row(m, m.r(self.base + 0xBA0), ident)
            cls = 7
            if row is not None:
                b = m.r(row + 0xC, 1)
                cls = b if (b < 0x80 or mut("cls-sign")) else 7
            m.w(out, cls)
            return int(cls != 7)
        rig.run("FUN_00645080", 0x645080, build_c, model_c, 2)

    def params(self):
        """FUN_00645b70 and FUN_00645bc0: level > 0 and a valid id pass Param pairs to FUN_00645b20."""
        rig = self.rig
        for name, addr, off in (("FUN_00645b70", 0x645B70, 0x148), ("FUN_00645bc0", 0x645BC0, 0x150)):
            def build(img, rng):
                """A table, a level (<= 0 sometimes) and a skill id."""
                n, ident = self.prepare(img, rng)
                return {"stack": [rng.choice((0, -1, 1, 20, 0x7FFFFFFF, -0x80000000, rng.getrandbits(32))) & M32, ident]}

            def model(m, a, off=off):
                """0 for level <= 0 or a bad id; else FUN_00645b20(level, Param[0 or 2], Param[1 or 3])."""
                level, ident = a["stack"]
                row = self.row(m, m.r(self.base + 0xBA0), ident)
                if row is None or s32(level) <= 0:
                    return 0
                return m.call("diminish", level, m.r(row + off), m.r(row + off + (0 if mut("param-pair") else 4)))
            rig.run(name, addr, build, model, 2)

    def unit_levels(self):
        """FUN_006447b0, FUN_00644300, FUN_00644750, FUN_00644aa0: a unit's skill levels."""
        rig, at = self.rig, self.at

        def unit_skill(img, rng):
            """A unit, a skill (null a quarter of the time) with a random level and owner (-1 half the time)."""
            STATE["skill"] = 0 if rng.random() < 0.25 else at(SK)
            put(img, SK + 0x34, rng.choice((0xFFFFFFFF, 0xFFFFFFFF, rng.getrandbits(32))))
            put(img, SK + 0x28, rng.choice((0, 1, 20, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32))))
            STATE["bonus"] = rng.choice((0, 3, 0xFFFFFFFE, rng.getrandbits(32)))
            STATE["max"] = rng.choice((1, 99, 0x7FFFFFFF, 0, rng.getrandbits(32)))

        def build(img, rng):
            """FUN_006447b0: ECX the unit (null a fifth of the time), EDX the skill id."""
            unit_skill(img, rng)
            return {"ecx": 0 if rng.random() < 0.2 else at(UNIT), "edx": rng.getrandbits(32)}

        def model(m, a):
            """Null unit answers 0 with no call; else the skill's level (+0x28) or 0 for no skill."""
            if a["ecx"] == 0:
                return 0
            sk = m.call("skill_by_id", a["ecx"], a["edx"], M32)
            return m.r(sk + 0x28) if sk else 0
        rig.run("FUN_006447b0", 0x6447B0, build, model, 0)

        def build_b(img, rng):
            """FUN_00644300: stdcall (unit, skill id)."""
            unit_skill(img, rng)
            return {"stack": [0 if rng.random() < 0.2 else at(UNIT), rng.getrandbits(32)]}

        def model_b(m, a):
            """Bonus levels (FUN_00644180) only for a skill whose owner is -1."""
            unit, ident = a["stack"]
            if unit == 0:
                return 0
            sk = m.call("skill_by_id", unit, ident, M32)
            if sk and (m.r(sk + 0x34) == M32 or mut("owner-check")):
                return m.call("level_bonus", unit, sk)
            return 0
        rig.run("FUN_00644300", 0x644300, build_b, model_b, 2)

        def build_r(img, rng):
            """FUN_00644750: a table and a skill id; the unit is passed as is."""
            n, ident = self.prepare(img, rng)
            unit_skill(img, rng)
            return {"stack": [at(UNIT), ident]}

        def model_r(m, a):
            """0x7fffffff for a bad id; else the signed short +0x174 plus the unit's skill level."""
            unit, ident = a["stack"]
            row = self.row(m, m.r(self.base + 0xBA0), ident)
            if row is None:
                return 0 if mut("req-oob") else 0x7FFFFFFF
            req = m.r(row + 0x174, 2)
            req = req - 0x10000 if req & 0x8000 else req
            sk = m.call("skill_by_id", unit, ident, M32)
            return (req + m.r(sk + 0x28)) & M32 if sk else req & M32
        rig.run("FUN_00644750", 0x644750, build_r, model_r, 2)

        def build_t(img, rng):
            """FUN_00644aa0: stdcall (unit, skill id) with FUN_00643810 and FUN_00644180 hooked."""
            unit_skill(img, rng)
            return {"stack": [0 if rng.random() < 0.15 else at(UNIT), rng.getrandbits(32)]}

        def model_t(m, a):
            """Level (+ bonus if the owner is -1), floored at 0, capped at FUN_00611830(0)."""
            unit, ident = a["stack"]
            if unit == 0:
                return 0
            sk = m.call("get_skill", unit, ident)
            if not sk:
                return 0
            level = m.r(sk + 0x28)
            if m.r(sk + 0x34) == M32:
                level = (level + m.call("level_bonus", unit, sk)) & M32
            level = level if s32(level) > 0 else 0
            cap = m.call("max_level", 0)
            if s32(level) < s32(cap):
                return level
            return m.call("max_level", 0) if not mut("cap-first") else cap
        rig.run("FUN_00644aa0", 0x644AA0, build_t, model_t, 2)

    def damage(self):
        """FUN_00644c20 (ESI the unit, stack the type) and FUN_00646170 (stdcall unit)."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A unit (null, player, or another type) and random stat results."""
            put(img, UNIT, rng.choice((0, 0, 1, 2)))
            STATE["stats"] = {s: rng.choice((0, 1, 10, 25, 0xFFFFFFF0, 0x7FFFFFFF, 0x80000000, rng.getrandbits(32))) for s in (0, 2, 0xC)}
            return {"regs": {"esi": 0 if rng.random() < 0.1 else at(UNIT)}, "stack": [rng.choice((0, 1, 2, 0xFFFFFFFF))]}

        def model(m, a):
            """Player: stat 2 + stat 0 - 20; else 3 x stat 0xc; at least 1; type 0: /3, else /4 (rounded toward 0); x 256."""
            unit, kind = a["regs"]["esi"], a["stack"][0]
            if unit and m.r(unit) == 0:
                dex = m.call("stat", unit, 2, 0)
                dmg = s32(m.call("stat", unit, 0, 0) + dex - 0x14)
            else:
                dmg = s32(m.call("stat", unit, 0xC, 0) * 3)
            dmg = max(dmg, 1)
            if kind == 0 or (mut("type-div") and kind != 1):
                return (tdiv(dmg, 3) << 8) & M32
            return (tdiv(dmg, 4) << 8) & M32
        rig.run("FUN_00644c20", 0x644C20, build, model, 1)

        def build_f(img, rng):
            """A player (or not), shapeshifted or not, with or without a weapon; frame count at +0x48."""
            put(img, UNIT, rng.choice((0, 0, 0, 1)))
            put(img, UNIT + 0x48, rng.choice((0, 0x100, 0xFFFF, 0xFFFFFF00, 0x80000000, rng.getrandbits(32))))
            STATE["shape"] = rng.choice((0, 1, 1, 1, 5))
            STATE["weapon"] = rng.choice((0, 0, at(0x800)))
            STATE["speed"] = rng.choice((0, -1, 1, 7, 0x13, 0x7FFFFFFF, 0x80000000, rng.randrange(1, 100)))
            return {"stack": [0 if rng.random() < 0.1 else at(UNIT)]}

        def model_f(m, a):
            """Frames * 256 / the weapon's speed (19 with no weapon), 0 unless a shapeshifted player."""
            unit = a["stack"][0]
            if unit == 0 or m.r(unit) != 0 or m.call("shapeshifted", unit) == 0:
                return 0
            speed = 0x14 if mut("speed-default") else 0x13
            weapon = m.call("weapon", unit, 0)
            if weapon:
                speed = s32(m.call("weapon_speed", unit, weapon))
                if speed < 1:
                    return 0
            return tdiv(s32(m.r(unit + 0x48) & 0xFFFFFF00), speed) & M32
        rig.run("FUN_00646170", 0x646170, build_f, model_f, 1)

    def sequence(self):
        """FUN_00643d00: a skill's animation sequence (fastcall unit, skill)."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A unit (player or monster), a skill whose record at 0x2400 has a skill id and sequence number."""
            put(img, UNIT, rng.choice((0, 1, 1, 2)))
            put(img, UNIT + 4, rng.randrange(0, 600))
            put(img, SK, at(0x2400))
            ident = rng.choice((0xC7, 0xC7, rng.randrange(0, 357), -1 & 0xFFFF, 0x8000))
            put(img, 0x2400, ident, 2)
            for i in range(8):
                put(img, MONSTATS + 0x170 + 2 * i, ident if rng.random() < 0.2 else rng.getrandbits(16), 2)
            return {"ecx": rng.choice((0, at(UNIT), at(UNIT), at(UNIT))), "edx": rng.choice((0, at(SK), at(SK), at(SK)))}

        def model(m, a):
            """No unit or skill: 0. Player: byte +0x13 of the record. Else skill 0xc7 gives 0; the first of the
            monster's eight skills equal to it (signed shorts) gives the short at +0x188 + 2 * slot, else 0."""
            unit, skill = a["ecx"], a["edx"]
            if not unit or not skill:
                return 0
            record = m.r(skill)
            if m.r(unit) == 0:
                return m.r(record + 0x13, 1)
            want = m.r(record, 2)
            want = want - 0x10000 if want & 0x8000 else want
            if want == (0xC8 if mut("diab-prison") else 0xC7):
                return 0
            m.call("monstats_row", m.r(unit + 4))
            for i in range(8):
                got = m.r(self.base + MONSTATS + 0x170 + 2 * i, 2)
                if (got - 0x10000 if got & 0x8000 else got) == want:
                    seq = m.r(self.base + MONSTATS + 0x188 + 2 * i, 2)
                    return (seq - 0x10000 if seq & 0x8000 else seq) & M32
            return 0
        rig.run("FUN_00643d00", 0x643D00, build, model, 0)

    def free_list(self):
        """FUN_00643a60: free every skill of a unit's list, then the list, and clear the unit's pointer."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """A unit at 0x400 with a list at 0x800 holding 0..4 skills (nodes at 0x1000 + 0x100 i), +8 its pool."""
            k = rng.randrange(0, 5)
            put(img, UNIT + 0xA8, rng.choice((0, at(0x800), at(0x800))))
            put(img, 0x800 + 4, at(0x1000) if k else 0)
            for i in range(k):
                put(img, 0x1000 + 0x100 * i + 4, at(0x1000 + 0x100 * (i + 1)) if i + 1 < k else 0)
            return {"stack": [0 if rng.random() < 0.15 else at(UNIT)]}

        def model(m, a):
            """Null unit or null list: nothing. Else free each skill (pool from FUN_004dc0b0), the list with the unit's pool."""
            unit = a["stack"][0]
            lst = m.r(unit + 0xA8) if unit else 0
            if not lst:
                return None
            sk = m.r(lst + 4)
            while sk:
                nxt = m.r(sk + 4)
                pool = m.call("mem_pool", unit)
                m.free(pool, sk, 0x505, FILE_SKILLS)
                sk = nxt
            m.free(m.r(unit + 8), lst, 0x509 if mut("free-line") else 0x508, FILE_SKILLS)
            m.w(unit + 0xA8, 0)
        rig.run("FUN_00643a60", 0x643A60, build, model, 1, ret=False)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 80
    s = Suite(rig)
    s.pure()
    s.params()
    s.unit_levels()
    s.damage()
    s.sequence()
    s.free_list()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("rdr-div", "mode-class", "mask-ignored", "cls-sign", "param-pair", "owner-check", "req-oob",
                 "cap-first", "type-div", "speed-default", "diab-prison", "free-line")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Skills.cpp table-row and unit-skill helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
