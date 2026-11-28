# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's rate for a player's attack sequence (mode SQ) as the oracle: FUN_00623f50 on the real Skills.txt record.

    set -o pipefail; ../../build/tests/test_seq_rate --seq-rate | grep '^D ' > d2d.tsv
    uv run python seq_rate.py compare d2d.tsv         # last line: N cells same, or N cells M differ (exit 1)
    uv run python seq_rate.py table                   # which sequence skills game.exe times as an attack, a cast or neither

A player's sequence skill (Skills.txt anim SQ) puts the unit in mode 18 (the skill record's byte +0x10). FUN_00623f50 then picks a
rate by the skill's record: FUN_006216e0 sends it to the cast branch when the record's byte +0x12 (seqtrans) is SC (0xa); else
FUN_00621580 sends it to the attack branch (0x6241d7..0x62430a) when the record's byte +6 holds the bit mask at 0x6ce274 (UseAttackRate);
a record with neither falls through to the other-animation branch. In the attack branch the percent is
`effective IAS + stat 68`, and at 0x624290..0x62429b a PLAYER (unit type 0) in mode 18 loses 30 points of it (cmp [unit], 0; cmp [unit+0x10],
0x12; sub esi, 0x1e) before the clamp to 15..175; the rate is `AnimData speed x percent / 100`.

`d2d.tsv` is test_seq_rate's grid: `D class skill_id skill weapon ias wsm sias steps speed ticks`: Fight::start_sequence's length in ticks for the
`steps` steps the weapon's sequence has, counted at the AnimData speed `speed` (the class's GH record: mode 0x12's COF name takes "gh", FUN_0064f5b0's override table). The game's
ticks are `max(1, ceil(256 steps / rate))` with the rate FUN_00623f50 prints for stat 93 = ias and stat 68 = 100 - wsm + sias (the
convention of the merged rate checks: tools/emu/player_attack_rate.py). Only skills game.exe times in the attack branch are compared; the
others are listed by name (CHECK lines), never dropped silently.

Runs natively: FUN_00623f50, FUN_00621740 (the effective-stat rows), FUN_00621580, FUN_006216e0, FUN_00620250 and the record
lookup around FUN_0045c4b0 with the REAL skill rows game.exe's own loader read (skill_tips.boot). Hooked (not the claim): the stat
getters FUN_00625480 / FUN_00625500, FUN_00645270 (the shapeshift remap, a no-op), FUN_00620f00 (the AnimData record lookup: the record is a stand-in whose
+0xc is `speed`), FUN_00623c80, FUN_006235a0 (dual wield: no), FUN_0063a7b0 (state 0x26: off), FUN_00643ce0 (the unit's active skill block).

Tags: V run: the branch each record takes and the rate. I (not run): that the length of a sequence is `ceil(256 steps / rate)`
(FUN_005539b0 and FUN_00621260 read, the steps and the AnimData record of mode 18 are not what this checks).
"""
import collections
import sys

import skill_tips

RECORD = 0x23c
SQ = 0x12
SC = 0x0a
M32 = 0xFFFFFFFF
CLASS = ["Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"]


def ceil_div(a, b):
    return -(-a // b)


class Game:
    def __init__(self):
        e = self.e = skill_tips.boot()
        tables = e.r32(0x744304)
        self.skills, self.count = e.r32(tables + 0xb98), e.r32(tables + 0xba0)
        self.stats = {}
        self.unit, self.rec, self.sk, self.buf = (e.alloc(0x300) for _ in range(4))
        self.current = 0
        e.w32(self.sk + 0x10, self.buf)
        e.hook(0x625500, lambda e_: self.stats.get(e_.arg(1), 0) & M32, 3)
        e.hook(0x625480, lambda e_: self.stats.get(e_.arg(1), 0) & M32, 3)
        e.hook(0x645270, lambda e_: 0, 6)
        e.hook(0x620f00, lambda e_: 0, 4)
        e.hook(0x623c80, lambda e_: 0, 2)
        e.hook(0x6235a0, lambda e_: 0, 1)
        e.hook(0x63a7b0, lambda e_: 0, 2)
        e.hook(0x646170, lambda e_: 0, 1)
        e.hook(0x643ce0, lambda e_: self.sk, 3)
        e.hook(0x45c4b0, lambda e_: self.skills + self.current * RECORD, 0)

    def row(self, skill):
        return self.skills + skill * RECORD

    def rate(self, skill, speed, ias, stat68, cls=3, mode=SQ, other=0):
        """FUN_00623f50 for a player of class `cls` in `mode` holding `skill` as the active skill."""
        e = self.e
        self.current = skill
        self.stats.clear()
        self.stats[93] = ias
        self.stats[0x44] = stat68
        self.stats[69] = other
        e.mu.mem_write(self.unit, bytes(0x300))
        e.w32(self.unit + 4, cls)
        e.w32(self.unit + 0x10, mode)
        e.w32(self.unit + 0x50, self.rec)
        e.w32(self.unit + 0xa8, self.sk)
        e.w32(self.rec + 0xc, speed)
        e.call(0x623f50, self.unit)
        return e.r16(self.unit + 0x4c)


def sq_skills(g):
    out = []
    for skill in range(g.count):
        row = g.row(skill)
        if g.e.read(row + 0x10, 1)[0] == SQ:
            out.append(skill)
    return out


def branch(g, skill):
    """Which branch FUN_00623f50 takes for a player in mode 18: 'attack' (the rate is speed x (eff IAS + stat 68 - 30)), 'cast',
    or 'other', told from the rate game.exe prints for three probe inputs (no input is a model of the answer)."""
    a = g.rate(skill, 256, 0, 100, other=130)
    expect_attack, expect_cast, expect_other = 256 * 70 // 100, 256, 256 * 130 // 100
    if a == expect_attack:
        return "attack", a
    if a == expect_cast:
        return "cast", a
    if a == expect_other:
        return "other", a
    return "unknown", a


def table():
    g = Game()
    e = g.e
    print("mask 0x6ce274 =", hex(e.r32(0x6ce274)))
    print("skill id  anim seqtrans byte+6  branch  rate(ias 0, stat68 100, speed 256, stat69 130)")
    for skill in sq_skills(g):
        row = g.row(skill)
        kind, a = branch(g, skill)
        print("%3d  anim %02x seqtrans %02x byte6 %02x  %-7s %d" % (skill, e.read(row + 0x10, 1)[0], e.read(row + 0x12, 1)[0], e.read(row + 6, 1)[0], kind, a))


def compare(path):
    g = Game()
    kinds = {}
    for skill in sq_skills(g):
        kinds[skill] = branch(g, skill)[0]
    cells = bad = 0
    stat = collections.Counter()
    shown = 0
    by = collections.defaultdict(collections.Counter)
    for lineno, line in enumerate(open(path, encoding="latin1"), 1):
        p = line.split()
        if not p or p[0] != "D":
            continue
        if len(p) != 11:
            raise SystemExit("%s:%d: malformed D line (%d fields)" % (path, lineno, len(p)))
        try:
            cls, skill, ias, wsm, sias, steps, speed, ticks = (int(p[i]) for i in (1, 2, 5, 6, 7, 8, 9, 10))
        except ValueError:
            raise SystemExit("%s:%d: malformed D line (a number is not one)" % (path, lineno)) from None
        name = p[3].replace("_", " ")
        if not 0 <= cls < len(CLASS):
            raise SystemExit("%s:%d: class %d out of range" % (path, lineno, cls))
        if not 0 <= skill < g.count:
            raise SystemExit("%s:%d: skill id %d out of range" % (path, lineno, skill))
        kind = kinds.get(skill, "not SQ")
        if kind != "attack":
            stat["excluded: %s (%s)" % (name, kind)] += 1
            continue
        if ticks < 0:
            stat["excluded: %s (open-ended hold)" % name] += 1
            continue
        rate = g.rate(skill, speed, ias, 100 - wsm + sias, cls)
        gticks = max(1, ceil_div(256 * steps, rate)) if rate > 0 else -1
        cells += 1
        key = (name, CLASS[cls])
        by[key]["cells"] += 1
        if gticks != ticks:
            bad += 1
            by[key]["differ"] += 1
            if shown < 12:
                shown += 1
                print("C %s %s %s ias %d wsm %d sias %d steps %d speed %d | d2d ticks %d | game rate %d ticks %d | %+d" % (
                    CLASS[cls], name, p[4], ias, wsm, sias, steps, speed, ticks, rate, gticks, ticks - gticks))
    for (name, cls), c in sorted(by.items()):
        print("SUM %-17s %-12s cells %6d  differ %6d" % (name, cls, c["cells"], c["differ"]))
    for what, n in sorted(stat.items()):
        print("CHECK %s: %d" % (what, n))
    print("%d cells  %s" % (cells, "same" if not bad else "%d differ" % bad))
    if cells == 0:
        raise SystemExit("no comparable cells in %s: an empty or fully skipped file is not a pass" % path)
    return 1 if bad else 0


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "compare":
        raise SystemExit(compare(sys.argv[2]))
    if len(sys.argv) > 1 and sys.argv[1] == "table":
        return table()
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
