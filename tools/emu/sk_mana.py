# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's player mana check and payment as the oracle: FUN_0056c160 (the
check the skill start runs, flag 0 of FUN_0056f640) and FUN_0056bfe0 (the
payment, in the start after a srvstfunc or in the do FUN_0056f7f0 at the
action frame).

    uv run python sk_mana.py rows                 # the mana columns of every skill row, as read
    uv run python sk_mana.py gen 40000 1 > cases.tsv
    uv run python sk_mana.py game cases.tsv > game.tsv
    ../../build/tests/test_sk_mana --sk-mana cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv     # tests/game/test_sk_mana.cpp
    uv run python sk_mana.py diff cases.tsv game.tsv d2d.tsv

A case is `id skill lvl mana` (mana in 256ths). `game` prints `id check paid`:
check = FUN_0056c160's answer for a player whose current skill is the case's
skill at the case's level with the case's mana, paid = what FUN_0056bfe0 then
takes off stat 8 (0 when the check failed: the start refuses the skill and
nothing is cast). `game --raw` adds FUN_0056bfe0's own return value. d2d's
side prints `id -1 -1` for a skill no cast path of the build starts (a
passive, a corpse skill); `diff` leaves those out and counts the rest.

Hooked (not the claim): FUN_00620250 (the unit's current skill: a fake
record for the check, none for the payment so the item-charge branch is
skipped), FUN_00643ad0 (the record's item owner: -1, not a charge),
FUN_00644140 (the record's Skills.txt row), FUN_006442a0 (its level),
FUN_00625480 (stat 8: the case's mana), FUN_0063a400 (shapeshifted: no),
FUN_00639df0 (state 0x72 blood_mana: no) and FUN_006272b0 (the stat add:
recorded). The arithmetic (Skills +0x186 minmana, +0x188 manashift, +0x18a
mana, +0x18c lvlmana) runs natively.

`gen N SEED`: every skill row x levels 1..60 x the mana values around the
thresholds (0, formula - 1, formula, formula + 1, minmana x 256 - 1, x 256,
x 256 + 1), then N random cases (levels 1..99, mana 0..0x20000).

Tags: the offsets are V (the run reads the rows through them and the check
and payment come out as combat.md describes); the hook list is V (the run
completes with no other function called); everything else is the code.
"""
import random
import sys

RECORD = 0x23c
FAKE = 0x30000000


def signed(value, bits):
    return value - (1 << bits) if value >= 1 << (bits - 1) else value


def formula(row, lvl):
    """The check's price in 256ths: (mana + lvlmana x (lvl - 1)) << manashift."""
    return (row["mana"] + row["lvlmana"] * max(lvl - 1, 0)) << (row["manashift"] & 31)


def thresholds(row, lvl):
    """The mana values `gen` tries for one row and level: around the formula and the minmana floor."""
    price, floor = formula(row, lvl), row["minmana"] << 8
    return sorted(value for value in {0, price - 1, price, price + 1, floor - 1, floor, floor + 1} if value >= 0)


class Oracle:
    def __init__(self):
        import skill_tips
        self.e = e = skill_tips.boot()
        tables = e.r32(0x744304)
        self.skills, self.count = e.r32(tables + 0xb98), e.r32(tables + 0xba0)
        self.unit = e.alloc(0x200)
        e.view("unit", self.unit).type = 0                       # a player
        self.skill = self.lvl = self.mana = 0
        self.current = FAKE
        self.added = None
        e.hook(0x620250, lambda e: self.current, 1)
        e.stub(0xFFFFFFFF, {0x643ad0: 1})
        e.hook(0x644140, lambda e: self.skills + self.skill * RECORD, 0)
        e.hook(0x6442a0, lambda e: self.lvl, 3)
        e.hook(0x625480, lambda e: self.mana if e.arg(1) == 8 else 0, 3)
        e.stub(0, {0x63a400: 1, 0x639df0: 2})
        e.hook(0x6272b0, self.add, 4)

    def add(self, e):
        if e.arg(1) == 8:
            self.added = signed(e.arg(2), 32)
        return 0

    def row(self, skill):
        row = self.e.view("skill", self.skills + skill * RECORD)
        return {"startmana": signed(row.startmana, 16), "minmana": signed(row.minmana, 16), "manashift": row.manashift,
                "mana": signed(row.mana, 16), "lvlmana": signed(row.lvlmana, 16), "srvst": signed(row.srvst, 16),
                "srvdo": signed(row.srvdo, 16), "usemanaondo": (row.flags & 0x20) != 0}

    def run(self, skill, lvl, mana):
        self.skill, self.lvl, self.mana = skill, lvl, mana
        self.current = FAKE
        check = self.e.call(0x56c160, ecx=self.unit) & 0xFFFFFFFF
        self.current, self.added = 0, None
        ret = self.e.call(0x56bfe0, skill, lvl, ecx=0, edx=self.unit) & 0xFFFFFFFF
        paid = -self.added if self.added is not None else 0
        return check, paid, ret


def gen(count, seed, oracle):
    """Case lines: the threshold grid of every row, then `count` random ones."""
    rng, cid, lines = random.Random(seed), 0, []
    for skill in range(oracle.count):
        row = oracle.row(skill)
        for lvl in range(1, 61):
            for mana in thresholds(row, lvl):
                lines.append(f"{cid} {skill} {lvl} {mana}")
                cid += 1
    for _ in range(count):
        lines.append(f"{cid} {rng.randrange(oracle.count)} {rng.randint(1, 99)} {rng.randrange(0x20000)}")
        cid += 1
    return lines


def diff(cases, game, d2d):
    """Counts of the cases both sides answered, split by what differs: the three file contents as line lists."""
    cases, game, d2d = ([line for line in lines if line.split()[:1] and line.split()[0].isdigit()] for lines in (cases, game, d2d))   # d2d's log lines are not answers
    names = {line.split()[0]: line.split()[1] for line in cases}
    answers = {line.split()[0]: line.split()[1:] for line in game}
    ids = {line.split()[0] for line in d2d}
    if not names or not set(names) <= ids:
        raise ValueError("d2d's answers do not cover the cases")   # a short file must not read as no difference
    total = same = 0
    kinds, by_skill = {}, {}
    for line in d2d:
        cid, *theirs = line.split()
        if theirs[0] == "-1":
            continue
        total += 1
        ours = answers[cid]
        if ours == theirs:
            same += 1
            continue
        kind = ("game casts, d2d refuses" if ours[0] == "1" and theirs[0] == "0" else
                "game refuses, d2d casts" if ours[0] == "0" and theirs[0] == "1" else "paid differs")
        kinds[kind] = kinds.get(kind, 0) + 1
        by_skill[names[cid]] = by_skill.get(names[cid], 0) + 1
    if not total:
        raise ValueError("no case has a cast path on the d2d side")
    return total, same, kinds, by_skill


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "rows"
    if cmd == "gen":
        print("\n".join(gen(int(sys.argv[2]), int(sys.argv[3]), Oracle())))
    elif cmd == "rows":
        oracle = Oracle()
        for skill in range(oracle.count):
            row = oracle.row(skill)
            if row["mana"] or row["lvlmana"] or row["minmana"] or row["startmana"]:
                print(skill, " ".join(f"{key}={value}" for key, value in row.items()))
    elif cmd == "game":
        oracle, raw = Oracle(), "--raw" in sys.argv
        with open(sys.argv[2], encoding="utf-8") as cases:
            for line in cases:
                cid, skill, lvl, mana = map(int, line.split())
                check, paid, ret = oracle.run(skill, lvl, mana)
                print(cid, int(check != 0), paid if check else 0, *([ret] if raw else []))
    elif cmd == "diff":
        files = []
        for path in sys.argv[2:5]:
            with open(path, encoding="utf-8") as handle:
                files.append(handle.read().splitlines())
        total, same, kinds, by_skill = diff(*files)
        print(f"compared {total} of {sum(1 for line in files[2] if line.split()[:1] and line.split()[0].isdigit())}, equal {same}, different {total - same}")
        print("by kind", dict(sorted(kinds.items())))
        print("by skill", dict(sorted(by_skill.items(), key=lambda item: -item[1])[:12]))
        sys.exit(1 if total != same else 0)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
