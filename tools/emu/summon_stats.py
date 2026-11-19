# SPDX-License-Identifier: GPL-3.0-or-later
"""A summon's stats in game.exe as the oracle: FUN_005c4470, which every summoning skill runs on its pet.

    uv run python summon_stats.py gen 100000 1 > cases.txt     # `skill lvl clvl life n (skill level)*n`
    uv run python summon_stats.py game < cases.txt > game.txt   # each case and `life b: .. a: .. s: ..`
    ../../build/tests/test_summon_stats --summon-stats < cases.txt | grep ' | ' > d2d.txt
    uv run python summon_stats.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: a summoning `skill` of any class (summon_skills) at `lvl`, its owner at character level `clvl` with n other skills at their levels
(base and with items alike; any skill not listed is 0, the summoning skill is `lvl`), and the pet's max life
before it (`life`, whole points: its MonStats roll). FUN_005c4470(game, owner; pet, skill, lvl, 1) runs for real
with the calc machine (FUN_00646ca0 -> FUN_006c0bc0 and its operands FUN_00646460, skill() FUN_00646c00):
- each passivestat k (Skills +0x98) gets calc +0xa4[k] at the skill's level, added to the pet (FUN_006272b0);
- each aurastat k (+0x54) gets calc +0x68[k]; a non-zero one is set on the pet's state node (FUN_00627150);
- its life: the max (FUN_00625d10, the passives in) + the max x calc1 (+0x138) / 100, in 256ths, set as stats 7
  and 6 (FUN_00627260), 0x5c4752..0x5c4821;
- each sumskill k (+0xc4) at its calc (+0xd0) above 0 is given to the pet (FUN_0056deb0).
Each line: the case, then `life` (256ths), `b:` the passive adds, `a:` the aurastat adds (`stat=value`, by stat
id), `s:` the pet's skills (`skill=level`, in sumskill order).
Hooked (inputs and side effects, not the claim): the owner's FUN_00625480 (stat 0xc: clvl, else 0), FUN_00643810
(its skill record by id: a fake one per id) and FUN_006442a0 (that record's level: the case's), as skill_damage.py
does; the pet's stat list as a model (FUN_006272b0 adds to its base, FUN_00627150 to its state node made by
FUN_006251f0 / FUN_00626e10, FUN_00625d10 = base stat 7 + the node's stat 7 (FUN_00624f60's flag-0x80000000 full list
and its maxhp floor of 256 left out: no summon has a maxhp aurastat, and the life is at least a point), FUN_00627260
sets); the node's state
FUN_006252d0 / FUN_00639db0, the client sends FUN_005c0be0 / FUN_0056e740 / FUN_005c0b50 / FUN_005a4850 /
FUN_00621e40, the pet's skills FUN_0056deb0 / FUN_005701b0 (recorded) and its MonEquip gear FUN_005d6b60
(test_valkyrie_gear's: not run). FUN_005c0be0 takes 3 stack arguments (ret 0xc).
Not here: the Decoy's and a Revive's own life before FUN_005c4470 (their dos set stat 7 first); FUN_005aeda0's pets.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_ESI

USAGE = "usage: summon_stats.py gen N SEED | game < CASES | compare GAME D2D"
RECORD = 0x23c
FAKE = 0x30000000          # fake skill records: FAKE + id * 0x40


def boot():
    import drlg
    return drlg.boot()


def summon_skills(e):
    """{summoning skill id: its class's skill ids}: the class skills with a summon or Revive's do 58, each of whose do
    reaches FUN_005c4470 (directly, the golems' through FUN_005c50c0, the traps' through FUN_005d5e10, Bone Wall's and
    Bone Prison's through FUN_005c58b0 / FUN_005c5bc0); not the Shadows (do 49, FUN_005d6e70: their own stats)."""
    def rows(name):
        text = e.mpq_file(f"data\\global\\excel\\{name}.txt").decode("latin-1").splitlines()
        head = text[0].split("\t")
        return [dict(zip(head, line.split("\t"))) for line in text[1:]]
    skills = [row for row in rows("Skills") if row.get("Id", "").isdigit() and row.get("charclass")]
    by_class = {}
    for row in skills:
        by_class.setdefault(row["charclass"], []).append(int(row["Id"]))
    return {int(row["Id"]): by_class[row["charclass"]] for row in skills
            if (row.get("summon") or row.get("srvdofunc") == "58") and row.get("srvdofunc") != "49"}


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.owner, self.pet = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200)
        e.w32(self.pet, 1)                                             # a monster (FUN_0044be50)
        e.w32(self.pet + 0xc, 0x77)                                    # its id (FUN_00451f50)
        self.levels, self.clvl = {}, 1
        self.reset(0)
        hooks = (
            (0x625480, self.stat, 3), (0x643810, lambda e: FAKE + 0x40 * (e.mu.reg_read(UC_X86_REG_ESI) & 0xffff), 0),
            (0x6442a0, lambda e: self.levels.get((e.arg(1) - FAKE) // 0x40, 0), 3),
            (0x6272b0, self.add_base, 4), (0x6251f0, self.new_node, 5), (0x626e10, lambda _e: 0, 3),
            (0x627150, self.set_node, 4), (0x625d10, self.max_life, 1), (0x627260, self.set_stat, 4),
            (0x6252d0, lambda _e: 0, 2), (0x639db0, lambda _e: 0, 3), (0x5c0be0, lambda _e: 1, 3),
            (0x56e740, lambda _e: 0, 6), (0x5c0b50, lambda _e: 0, 2), (0x5a4850, lambda _e: 0, 2),
            (0x621e40, lambda _e: 0, 3), (0x56deb0, self.give_skill, 2), (0x5701b0, lambda _e: 0, 2),
            (0x5d6b60, lambda _e: 0, 5),
        )
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def reset(self, life):
        self.base, self.node, self.sets, self.skills = {7: life << 8}, {}, {}, []
        self.base_adds, self.node_made = {}, 0

    def stat(self, e):
        return self.clvl if e.arg(0) == self.owner and e.arg(1) == 0xc else 0

    def add_base(self, e):
        stat, value = e.arg(1), e.arg(2)
        self.base[stat] = (self.base.get(stat, 0) + value) & 0xffffffff
        self.base_adds[stat] = (self.base_adds.get(stat, 0) + value) & 0xffffffff
        return 0

    def new_node(self, e):
        self.node_made = e.alloc(0x40)
        return self.node_made

    def set_node(self, e):
        self.node[e.arg(1)] = e.arg(2) & 0xffffffff                  # FUN_00627150 sets (FUN_006270b0)
        return 0

    def max_life(self, _e):
        return (self.base.get(7, 0) + self.node.get(7, 0)) & 0xffffffff

    def set_stat(self, e):
        self.sets[e.arg(1)] = e.arg(2)
        return 0

    def give_skill(self, e):
        self.skills.append((e.mu.reg_read(UC_X86_REG_EDX), e.arg(0)))   # EDX the skill, the level, 0
        return 0

    def run(self, skill, lvl, clvl, life, levels):
        self.levels = dict(levels)
        self.levels[skill] = lvl
        self.clvl = clvl
        self.reset(life)
        self.e.call(0x5c4470, self.pet, skill, lvl, 1, ecx=self.game, edx=self.owner)
        signed = lambda v: v - (1 << 32) if v & 0x80000000 else v
        base = " ".join(f"{k}={signed(v)}" for k, v in sorted(self.base_adds.items()) if v)
        aura = " ".join(f"{k}={signed(v)}" for k, v in sorted(self.node.items()) if v)
        skills = " ".join(f"{k}={signed(v)}" for k, v in self.skills)
        return f"{signed(self.sets.get(7, 0))} b: {base} a: {aura} s: {skills}".replace("  ", " ")


def gen(count, seed):
    e = boot()
    skills = summon_skills(e)
    rng = random.Random(seed)
    for skill in sorted(skills):                                       # every level 1..60 alone, then with its class's skills at 20
        for lvl in range(1, 61):
            for others in ((), tuple((s, 20) for s in skills[skill] if s != skill)):
                print(skill, lvl, 30, 21, len(others), *(v for pair in others for v in pair))
    for _ in range(count):
        skill = rng.choice(sorted(skills))
        lvl = rng.choice((1, 2, 3, 4, rng.randint(1, 20), rng.randint(1, 60)))
        clvl = rng.randint(1, 99)
        life = rng.choice((1, rng.randint(1, 60), rng.randint(1, 3000), rng.randint(3000, 30000)))   # a Revive's monster can be big
        others = [(s, rng.choice((0, 1, rng.randint(1, 20), rng.randint(1, 60)))) for s in skills[skill] if s != skill and rng.random() < 0.4]
        print(skill, lvl, clvl, life, len(others), *(v for pair in others for v in pair))


def game():
    e = boot()
    rig = Rig(e)
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        skill, lvl, clvl, life, count = (int(f) for f in field[:5])
        values = [int(f) for f in field[5:5 + 2 * count]]
        levels = dict(zip(values[0::2], values[1::2]))
        print(line.strip(), "|", rig.run(skill, lvl, clvl, life, levels))


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            if "|" not in line:
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            case, result = line.split("|", 1)
            out.append((case.split(), result.split()))
    return out


def compare(game_path, d2d_path):
    game_rows, d2d_rows = rows(game_path), rows(d2d_path)
    if not game_rows or len(game_rows) != len(d2d_rows):
        sys.exit(f"empty or unequal: {len(game_rows)} game, {len(d2d_rows)} d2d (an empty file is not a pass)")
    differ = 0
    for (g_case, g), (d_case, d) in zip(game_rows, d2d_rows, strict=True):
        if g_case != d_case:
            sys.exit(f"cases out of step: game {' '.join(g_case)}, d2d {' '.join(d_case)}")
        if g != d:
            differ += 1
            if differ <= 20:
                print(f"DIFF {' '.join(g_case)}:\n  game {' '.join(g)}\n  d2d  {' '.join(d)}")
    print(f"{len(game_rows)} cases, {differ} differ")
    sys.exit(1 if differ else 0)


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("gen", 3), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "gen":
        gen(int(a[1]), int(a[2]))
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
