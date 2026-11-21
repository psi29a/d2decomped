# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's animation rate in each of its modes (unit +0x4c, FUN_00623f50) as the oracle, from the function's entry.

    uv run python mode_rates.py gen 100000 1 > cases.txt     # `class mode skill speed vel frw fhr fbr held ias rate69`
    uv run python mode_rates.py game < cases.txt > game.txt   # each case and `| rate`
    ../../build/tests/test_mode_rates --mode-rates < cases.txt | grep ' | ' > d2d.txt
    uv run python mode_rates.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

For a monster (unit type 1) of MonStats row `class` in monster mode `mode` (0 DT .. 15 RN) FUN_00623f50 picks the rate
by FUN_006216e0 (a cast), the mode row at 0x6e8b90 / 0x6e8cd0 (FUN_00621580 an attack, FUN_006214a0 a moving mode,
FUN_00621630 DT / DD's own speed) and the get-hit, block and knockback branches, all game.exe's own code on its own
loaded tables (drlg.boot): the moving base is MonStats +0x36 (WL) / +0x38 (RN), which the loader derives
(FUN_00650f00). A case: the class and mode, the Skills.txt row of the unit's current skill (UseAttackRate decides
S1..S4 / SQ), the AnimData speed (the mode record's +0xc), stat 0x43 velocitypercent, the raw stats 96 (FRW), 99 (FHR),
102 (FBR) that FUN_00621740 diminishes, whether state 0x65 is on (a block at 100 more), stat 93 (IAS) and stats
68 / 69 (attackrate, other_animrate: one value). Each line: the case, then the rate as stored (a signed word).
Hooked (inputs and side effects, not the claim): the stat getters FUN_00625480 / FUN_00625500 (the case's stats), the
mode record lookup FUN_00645270 / FUN_00620f00 (unit +0x50 is the case's record), the current skill FUN_00620250 /
FUN_00643ce0 (a zeroed skill instance: its +0xc flags, which FUN_006214a0 reads through FUN_006446a0 for
SQ, are 0, so an SQ never counts as moving here), the state test FUN_00639df0 (0x65), the path's velocity FUN_00648690 / FUN_006485f0, and FUN_006235a0,
FUN_00623c80, FUN_0063a7b0, FUN_00646170 (player-only or off).
"""
import random
import sys

USAGE = "usage: mode_rates.py gen N SEED | game < CASES | compare GAME D2D"
M32 = 0xffffffff


def classes_and_skills():
    """The MonStats row count and the Skills.txt rows with no charclass (the monster skills)."""
    from emu import mpq_cache
    cache = mpq_cache()
    monstats = cache.read("data\\global\\excel\\monstats.txt").decode("latin1").split("\r\n")
    rows = sum(1 for line in monstats[1:] if line.split("\t")[0] not in ("", "Expansion"))
    lines = cache.read("data\\global\\excel\\skills.txt").decode("latin1").split("\r\n")
    head = lines[0].split("\t")
    at_id, at_class = head.index("Id"), head.index("charclass")
    skills = [int(f[at_id]) for f in (line.split("\t") for line in lines[1:]) if len(f) > at_class and f[at_id].isdigit() and not f[at_class]]
    return rows, skills


def gen(count, seed):
    rows, skills = classes_and_skills()
    rng = random.Random(seed)
    for _ in range(count):
        mode = rng.randrange(16)
        print(rng.randrange(rows), mode, rng.choice(skills), rng.choice((256, 256, 128, 200, rng.randint(1, 512))),
              rng.choice((75, 75, 25, 125, rng.randint(-150, 300))), rng.choice((0, 0, rng.randint(-100, 300))),
              rng.choice((0, 0, rng.randint(-100, 300))), rng.choice((0, 0, rng.randint(-100, 300))), int(rng.random() < 0.2),
              rng.choice((0, 0, rng.randint(-100, 200))), rng.choice((100, 100, 150, 50, rng.randint(-100, 300))))


class Rig:
    def __init__(self):
        import drlg
        e = self.e = drlg.boot()
        self.unit, self.record, self.path, self.instance = e.alloc(0x200), e.alloc(0x40), e.alloc(0x200), e.alloc(0x40)
        self.stats, self.skill, self.held = {}, 0, 0
        hooks = ((0x625480, self.stat, 3), (0x625500, self.stat, 3), (0x645270, lambda e: 0, 6), (0x620f00, lambda e: 0, 4),
                 (0x620250, lambda e: self.instance, 1), (0x643ce0, lambda e: self.skill, 3), (0x639df0, self.state, 2),
                 (0x648690, lambda e: 0, 4), (0x6485f0, lambda e: 0, 0), (0x6235a0, lambda e: 0, 1), (0x623c80, lambda e: 0, 2),
                 (0x63a7b0, lambda e: 0, 2), (0x646170, lambda e: 0, 1))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def stat(self, e):
        return self.stats.get(e.arg(1), 0) & M32 if e.arg(0) == self.unit else 0

    def state(self, e):
        return self.held if e.arg(0) == self.unit and e.arg(1) == 0x65 else 0

    def run(self, cls, mode, skill, speed, vel, frw, fhr, fbr, held, ias, rate69):
        e = self.e
        self.skill, self.held = skill, held
        self.stats = {0x43: vel, 96: frw, 99: fhr, 102: fbr, 93: ias, 0x44: rate69, 0x45: rate69}
        e.mu.mem_write(self.unit, bytes(0x200))
        e.mu.mem_write(self.record, bytes(0x40))
        e.w32(self.unit, 1)
        e.w32(self.unit + 4, cls)
        e.w32(self.unit + 0x10, mode)
        e.w32(self.unit + 0x2c, self.path)
        e.w32(self.unit + 0x50, self.record)
        e.w32(self.record + 0xc, speed)
        e.mu.mem_write(self.unit + 0x4c, b"\xad\xde")      # a marker: a branch that stores nothing prints it
        e.call(0x623f50, self.unit, 0, 0)
        word = int.from_bytes(e.mu.mem_read(self.unit + 0x4c, 2), "little")
        return str(word - 0x10000 if word & 0x8000 else word)


def game():
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:11])))


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
            if len(case) != 11 or len(result) != 1 or not all(field.lstrip("-").isdigit() for field in case + result):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (11 case fields and 1 result, integers)")
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
