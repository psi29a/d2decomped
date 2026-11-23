# SPDX-License-Identifier: GPL-3.0-or-later
"""The Act 1 merc's shot in game.exe as the oracle: RogueMissile (srvdofunc 110, FUN_005ce1b0).

    uv run python merc_shot.py gen 100000 1 > cases.txt     # `magic exploding level`
    uv run python merc_shot.py game < cases.txt > game.txt   # each case and `| row level min max` (or `-`)
    ../../build/tests/test_merc_shot --merc-shot < cases.txt | grep ' | ' > d2d.txt
    uv run python merc_shot.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the merc's stats 157 (item_magicarrow) and 158 (item_explosivearrow) and the skill's level. FUN_005ce1b0(ECX
game, EDX unit; skill, level) runs for real on a type 1 unit (a hireling) with RogueMissile's row of game.exe's own
table (drlg.boot): FUN_00645f00 picks magicarrow (0x1b) at stat 157's value, else explodingarrow (0x29) at stat 158's,
else nothing, and then the skill's srvmissilea (+0x48: rogue1) at the skill's level. Each line: the case, then the
Missiles row the shot is made of, its level, and that row's own physical min and max at that level (256ths), as
FUN_0064b860 adds them to the record: FUN_0064af20 / FUN_0064aff0, run for real. That pair only means something for a
row with no Skill (FUN_0064b860 0x64b8ce..0x64b8e7): magicarrow's damage is its Skill's, and both sides print 0 for it.
Hooked (inputs and side effects, not the claim): the stat getter FUN_00625500 (the case's 157 / 158), the caster
check FUN_00553540 (passes), the missile makers FUN_0056ecb0 / FUN_0056ee90 (recorded: the row in EDX, the level).
"""
import random
import sys

USAGE = "usage: merc_shot.py gen N SEED | game < CASES | compare GAME D2D"
ROGUE_MISSILE = 336
M32 = 0xffffffff


def gen(count, seed):
    rng = random.Random(seed)
    for magic in (-3, -1, 0, 1, 2, 20):
        for exploding in (-3, -1, 0, 1, 5, 20):
            for level in (0, 1, 7, 30):
                print(magic, exploding, level)
    for _ in range(count):
        stat = lambda: rng.choice((0, 0, 0, rng.randint(1, 30), rng.randint(-5, 5), rng.randint(-1000, 1000)))
        print(stat(), stat(), rng.choice((1, rng.randint(0, 40), rng.randint(1, 99))))


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


class Rig:
    def __init__(self):
        import drlg
        e = self.e = drlg.boot()
        self.game, self.unit = e.alloc(0x200), e.alloc(0x200)
        self.stats, self.shots = {}, []
        hooks = ((0x625500, lambda e: self.stats.get(e.arg(1), 0) & M32, 3), (0x553540, lambda e: 1, 2),
                 (0x56ecb0, self.missile, 8), (0x56ee90, self.missile, 8))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def missile(self, e):
        from unicorn.x86_const import UC_X86_REG_EDX
        self.shots.append((signed(e.mu.reg_read(UC_X86_REG_EDX)), signed(e.arg(2))))
        return 1

    def run(self, magic, exploding, level):
        e = self.e
        self.stats, self.shots = {0x9d: magic, 0x9e: exploding}, []
        e.mu.mem_write(self.unit, bytes(0x200))
        e.w32(self.unit, 1)
        e.call(0x5ce1b0, ROGUE_MISSILE, level & M32, ecx=self.game, edx=self.unit)
        if len(self.shots) != 1:
            return "-"
        row, shot_level = self.shots[0]
        low = signed(e.call(0x64af20, 0, self.unit, row & M32, shot_level & M32))
        high = signed(e.call(0x64aff0, 0, self.unit, row & M32, shot_level & M32))
        return f"{row} {shot_level} {low} {high}"


def game():
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:3])))


def integer(field):
    try:
        int(field)
    except ValueError:
        return False
    return True


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            if "|" not in line:
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            case, result = line.split("|", 1)
            case, result = case.split(), result.split()
            result_ok = result == ["-"] or (len(result) == 4 and all(integer(field) for field in result))
            if len(case) != 3 or not result_ok or not all(integer(field) for field in case):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (3 case fields, then `-` or row level min max)")
            out.append((case, result))
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
                print(f"DIFF {' '.join(g_case)}: game {' '.join(g)}, d2d {' '.join(d)}")
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
