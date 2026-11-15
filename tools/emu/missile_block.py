# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d: may a monster's missile be blocked?  The oracle for "a monster missile with no physical damage cannot be
blocked" (d2d rules::monster_blow, fork branch fid/missile-block).

    uv run python mm_diff.py game TABLES_DIR N SEED OUT         # writes OUT/cases.txt and OUT/game.tsv (the missile's own record)
    uv run python missile_block.py game OUT/game.tsv > block.tsv    # FUN_005a89a0 and FUN_005ad730 under unicorn (seconds)
    ../../build/tests/test_monster_missile --missile-block OUT/cases.txt | grep '^K ' > d2d.tsv
    uv run python missile_block.py compare block.tsv d2d.tsv

`game` takes the physical min / max (256ths, game.tsv columns 10 and 11) game.exe's own launch (FUN_0059fa30) put on each case's
missile, writes them to a missile unit's stats 0x15 / 0x16, rolls the missile's record with FUN_005a89a0 (the record's physical field
+8, FUN_005a8910: min and max both over 0, else 0) and runs the missile hit's FUN_005ad730 on it with a defender. Its call to
FUN_0057dfb0(defender, missile 1, can-block) is hooked to read the third argument, `record +8 != 0` (0x5ad7f9): with 0 the block
roll is skipped. Hooked (not the claim): the missile's owner FUN_00552fd0, the unit-state test, the stat getter / setter, and the
record's later users (FUN_0057c6c0, FUN_0057cee0, FUN_0064a5a0, FUN_0064a190, FUN_005a5560); a missile row with no flags and no
ToHit stands in for Missiles.txt (the other rows' reads are after the call). Output: `B id lo hi can_block rolled` (rolled: the
record's +8 in 256ths; 3 seeds are rolled, the line is printed when all three agree on can_block, else the case is `X`). A Blood Raven
case is printed `E case lo hi` and not judged: her record carries her weapon's random damage (mm_diff.py compares no physical part for her either).

The skill path (a monster skill's srvmissilea: Fallen Shaman fire, Radament's bolt, Andariel's spray, the Countess's firewall) has its
own launch records, from mm_launch.py (the `Skill...` lines of its output, game.exe's FUN_0059fa30 on a real monster, `game256 LO HI`):
`skill-cases LAUNCH` prints `id<TAB>level<TAB>row<TAB>skill` for each such line (the file `test_monster_missile --missile-block-skill` reads) and `game-skill
LAUNCH` the same `B` / `E` lines as `game` for them (Blood Raven's Quick Strike carries her weapon: `E`).

`compare`: d2d's `K id blocked trials` (every case's shot through the real monster_shot_stats and monster_blow against a defender
with Block 75, nothing else, to-hit made sure; `blocked` of `trials`). A case with can_block 0 must show 0 blocked, one with 1 more than
0 (75 percent over 400 trials: chance of 0 is 2^-166). Both files must carry every case exactly once, the E cases counted as excluded and a case d2d has no row for (`N case`) listed as NOROW and counted, not judged (the row is a gap of its own); a missing, duplicated or
malformed line is an error (exit 1). `selftest` checks that (no game)."""
import os
import sys

MASK = 0xFFFFFFFF


def skill_lines(path):
    """The `Skill...` lines of mm_launch.py's output: [(id, monster, row, lo, hi, skill, level)], id the 1-based line number among them
    (skill: the monster skill's name after `SkillN:`, level: mm_launch's `pred(lvl N)`, the level d2d launches it at)."""
    out = []
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if len(f) > 14 and f[3].startswith("Skill") and f[11] == "game256":
            out.append((len(out) + 1, f[1], f[4], int(f[12]), int(f[13]), f[3].split(":", 1)[1], int(f[8].split()[-1].rstrip(")"))))
    if not out:
        sys.exit("no Skill lines in " + path)
    return out


def game(skill=False):
    sys.path.insert(0, os.environ.get("EMU", os.path.dirname(os.path.abspath(__file__))))      # emu.py / life_apply.py: $EMU, else this directory
    import life_apply
    path = sys.argv[2]
    cases = []
    if skill:
        cases = [(case, lo, hi, mon == "bloodraven") for case, mon, _row, lo, hi, _skill, _lvl in skill_lines(path)]
    else:
        for line in open(path):
            f = line.rstrip("\n").split("\t")
            if len(f) < 11:
                continue
            cases.append((int(f[0]), int(f[9]), int(f[10]), f[2] == "bloodraven"))
    if not cases:
        sys.exit("no cases in " + path)

    g = life_apply.Game()
    e = g.e
    captured = {}
    missile = e.alloc(0x100)
    table = e.alloc(0x2000)
    rows = e.alloc(0x1a4 * 4)
    e.w32(0x744304, table)
    e.w32(table + 0xb64, rows)                 # the missile rows: class 1 is zeroed (flags 0, ToHit 0)
    e.w32(table + 0xb6c, 4)
    e.w32(missile, 3)                          # unit type 3: a missile
    e.w32(missile + 4, 1)                      # its class
    e.hook(0x552fd0, lambda e_: g.attacker, 0)
    e.hook(0x639df0, lambda e_: 0, 2)
    e.hook(0x57dfb0, lambda e_: captured.__setitem__("can_block", e_.arg(2)) or 0, 3)
    e.stub(0, {0x57c6c0: 3, 0x57cee0: 2, 0x64a5a0: 1, 0x64a190: 1, 0x5a5560: 1})
    seen = {}
    for case, lo, hi, weapon in cases:
        if weapon:                                  # her record carries her weapon's random damage (mm_diff.py compares no physical part for her either)
            print("E %d %d %d" % (case, lo, hi))
            continue
        key = (lo, hi)
        if key not in seen:
            outs = []
            for seed in ((0x1234567, 0x89abcdef), (1, 2), (0xdeadbeef, 0x1)):
                g.reset(100, 0, 0)
                g.stats[(missile, 0x15)] = lo & MASK
                g.stats[(missile, 0x16)] = hi & MASK
                e.w32(missile + 0x20, seed[0])
                e.w32(missile + 0x24, seed[1])
                e.mu.mem_write(g.rec, b"\0" * 0x80)
                e.call(0x5a89a0, missile, 0, g.rec)
                rolled = e.s32(g.rec + 8)
                captured.clear()
                e.call(0x5ad730, g.target, g.rec, ecx=g.game, edx=missile)
                if "can_block" not in captured:
                    sys.exit("FUN_0057dfb0 was not reached for %d %d" % (lo, hi))
                outs.append((captured["can_block"] & 0xff, rolled))
            seen[key] = outs
        outs = seen[key]
        if len({o[0] for o in outs}) != 1:
            print("X %d %d %d %s" % (case, lo, hi, outs))
        else:
            print("B %d %d %d %d %d" % (case, lo, hi, outs[0][0], outs[0][1]))


def die(msg):
    print("error: " + msg)
    sys.exit(1)


def parse(lines, who):
    """game: `B case lo hi can_block rolled` or `E case lo hi` (excluded); d2d: `K case blocked trials`. -> {case: tuple}."""
    cells = {}
    for k, line in enumerate(lines, 1):
        f = line.split()
        if not f:
            continue
        try:
            shape = {"B": 6, "E": 4, "K": 4, "N": 2}[f[0]]
            if len(f) != shape or f[0] not in (("B", "E") if who == "game" else ("K", "N")):
                raise ValueError
            vals = tuple(int(x) for x in f[1:])
            if f[0] == "B" and vals[3] not in (0, 1):
                raise ValueError
        except (ValueError, KeyError):
            die("%s line %d malformed: %r" % (who, k, line.strip()))
        if vals[0] in cells:
            die("%s line %d repeats case %d" % (who, k, vals[0]))
        cells[vals[0]] = (f[0],) + vals[1:]
    if not cells:
        die("%s file has no cases" % who)
    return cells


def judge(g, d, out):
    if set(g) != set(d):
        die("the files differ in cases: game %d, d2d %d, first missing %s" % (len(g), len(d), sorted(set(g) ^ set(d))[:3]))
    n = bad = zero = excluded = norow = 0
    for case in sorted(g):
        if g[case][0] == "E":
            excluded += 1
            continue
        if d[case][0] == "N":                           # d2d does not load the missile row at all: a gap of its own, counted and listed
            norow += 1
            out("NOROW case %d (physical %d..%d, game.exe can-block %d): d2d has no such Missiles.txt row" % (case, g[case][1], g[case][2], g[case][3]))
            continue
        _, lo, hi, can_block, rolled = g[case]
        _, blocked, trials = d[case]
        n += 1
        zero += can_block == 0
        if (can_block == 0 and blocked != 0) or (can_block == 1 and blocked == 0):
            bad += 1
            out("DIFF case %d (physical %d..%d, rolled %d): game.exe can-block %d, d2d blocked %d of %d" % (case, lo, hi, rolled, can_block, blocked, trials))
    out("%d cases, %d differ (%d unblockable in game.exe); %d excluded (Blood Raven: her record carries her weapon's random damage); %d with no d2d row (a gap of its own: listed above, not judged)" % (n, bad, zero, excluded, norow))
    return n, bad


def compare(gpath, dpath):
    g = parse(open(gpath).read().splitlines(), "game")
    d = parse(open(dpath).read().splitlines(), "d2d")
    n, bad = judge(g, d, print)
    sys.exit(1 if bad or n == 0 else 0)


def selftest():
    gl = ["B 1 0 0 0 0", "B 2 256 512 1 300", "E 3 0 0"]
    dl = ["K 1 0 400", "K 2 290 400", "K 3 7 400"]

    def fails(a, b):
        sys.stdout = open(os.devnull, "w")
        try:
            judge(parse(a, "game"), parse(b, "d2d"), lambda s: None)
        except SystemExit as ex:
            return ex.code == 1
        finally:
            sys.stdout = sys.__stdout__
        return False

    ok = [not fails(gl, dl), fails(gl, dl[:2]), fails(gl[:2], dl), fails(gl, dl + [dl[0]]), fails(gl, ["K 1 x", dl[1], dl[2]]), fails([], dl), fails(gl, []),
          fails(gl, ["B 1 0 0 0 0"] + dl[1:]), fails(["K 1 0 400"] + gl[1:], dl)]
    ok.append(judge(parse(gl, "game"), parse(["K 1 5 400", dl[1], dl[2]], "d2d"), lambda s: None)[1] == 1)
    ok.append(judge(parse(gl, "game"), parse([dl[0], "K 2 0 400", dl[2]], "d2d"), lambda s: None)[1] == 1)
    ok.append(judge(parse(gl, "game"), parse(["N 1", dl[1], dl[2]], "d2d"), lambda s: None) == (1, 0))     # a row d2d does not load is listed, not judged
    print("selftest: %d of %d checks" % (sum(ok), len(ok)))
    sys.exit(0 if all(ok) else 1)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "game":
        game()
    elif len(sys.argv) == 3 and sys.argv[1] == "game-skill":
        game(skill=True)
    elif len(sys.argv) == 3 and sys.argv[1] == "skill-cases":
        for case, _mon, row, _lo, _hi, skill, lvl in skill_lines(sys.argv[2]):
            print("%d\t%d\t%s\t%s" % (case, lvl, row, skill))      # tab separated: a row name may hold a space (death mauler)
    elif len(sys.argv) == 4 and sys.argv[1] == "compare":
        compare(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 2 and sys.argv[1] == "selftest":
        selftest()
    else:
        sys.exit(__doc__)
