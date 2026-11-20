# SPDX-License-Identifier: GPL-3.0-or-later
"""Which corpses the Necromancer's corpse skills take, in game.exe as the oracle: the server start functions of
Raise Skeleton / Skeletal Mage (srvstfunc 15, FUN_005c3070), Corpse / Poison Explosion (17, FUN_005c31c0) and Revive
(21, FUN_005c3350), each on the caster's target (FUN_00553540).

    uv run python corpse_tests.py gen 100000 1 > cases.txt     # `pick mode flags c4 c6 hidden s36 s8f town`
    uv run python corpse_tests.py game < cases.txt > game.txt   # each case and `| raise blast revive`
    ../../build/tests/test_corpse_tests --corpse-tests < cases.txt | grep ' | ' > d2d.txt
    uv run python corpse_tests.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: a monster unit (+0 1) of MonStats row `pick` modulo the rows (+4), its mode (+0x10), its monster data's flag
word (+0x16: champions, uniques and superuniques carry 8), its unit flags (+0xc4, only bit 2 given: `c4`), byte +0xc6
bit 0 (`c6`), whether it is hidden (FUN_0063a770), whether it carries state 0x36 (uninterruptable) and state 0x8f
(attached), and whether its level is a town (FUN_0061ab00). The three start functions run for real, with
FUN_00645510 (mode 0xc, MonStats2 corpseSel, MonStats Velocity), FUN_00645680 (mode 0xc, corpseSel), FUN_005c3300
(MonStats2 revive, FUN_005541b0, FUN_00645510, FUN_0056e2f0 -> FUN_005dd480: switchai, mWL, MonStats boss, data +0x16
& 0xa, an imp riding), FUN_004638a0 and FUN_00623470 reading the real tables.
Each line: the case, then `raise blast revive` (1 or 0 each).
Hooked (inputs, not the claim): FUN_00553540 (the case's unit), FUN_0063a770 (hidden), FUN_00639df0 (the two states)
and FUN_0061ab00 (town).
"""
import random
import sys

USAGE = "usage: corpse_tests.py gen N SEED | game < CASES | compare GAME D2D"
STARTS = (0x5c3070, 0x5c31c0, 0x5c3350)
UNINTERRUPTABLE, ATTACHED = 0x36, 0x8f


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.caster, self.unit, self.data = (e.alloc(0x200) for _ in range(4))
        tables = e.r32(0x744304)
        self.rows = e.r32(tables + 0xa80)
        self.hidden, self.states, self.town = 0, set(), 0
        hooks = ((0x553540, lambda e: self.unit, 2), (0x63a770, lambda e: self.hidden, 1),
                 (0x639df0, lambda e: 1 if e.arg(1) in self.states else 0, 2), (0x61ab00, lambda e: self.town, 1))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def run(self, pick, mode, flags, c4, c6, hidden, s36, s8f, town):
        e = self.e
        self.hidden, self.town = hidden, town
        self.states = {state for state, on in ((UNINTERRUPTABLE, s36), (ATTACHED, s8f)) if on}
        e.mu.mem_write(self.unit, bytes(0x200))
        e.mu.mem_write(self.data, bytes(0x200))
        e.w32(self.unit, 1)
        e.w32(self.unit + 4, pick % self.rows)
        e.w32(self.unit + 0x10, mode)
        e.w32(self.unit + 0x14, self.data)
        e.mu.mem_write(self.data + 0x16, (flags & 0xffff).to_bytes(2, "little"))
        e.w32(self.unit + 0xc4, 4 if c4 else 0)
        e.mu.mem_write(self.unit + 0xc6, bytes([1 if c6 else 0]))
        return " ".join("1" if e.call(start, 0, 1, ecx=self.game, edx=self.caster) else "0" for start in STARTS)


def gen(count, seed):
    rng = random.Random(seed)
    for pick in range(800):
        for mode in (0xc, 0, 1):
            for flags in (0, 0x10, 9, 0xd, 0xb):
                print(pick, mode, flags, 1, 0, 0, 0, 0, 0)
    for pick in range(0, 800, 3):
        for c4, c6, hidden, s36, s8f, town in ((0, 0, 0, 0, 0, 0), (0, 1, 0, 0, 0, 0), (1, 0, 1, 0, 0, 0), (1, 0, 0, 1, 0, 0),
                                               (1, 0, 0, 0, 1, 0), (1, 0, 0, 0, 0, 1)):
            print(pick, 0xc, 0, c4, c6, hidden, s36, s8f, town)
    for _ in range(count):
        print(rng.randrange(1 << 16), rng.choice((0xc, 0xc, 0xc, 0, 1, 2, 3, 4, 0x11, rng.randrange(20))),
              rng.choice((0, 0, 0x10, 9, 0xd, 0xb, 1, 2, 8, rng.getrandbits(16))), rng.choice((0, 1, 1)), rng.choice((0, 0, 0, 1)),
              rng.choice((0, 0, 0, 1)), rng.choice((0, 0, 0, 1)), rng.choice((0, 0, 0, 1)), rng.choice((0, 0, 0, 1)))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:9])))


def is_int(token):
    try:
        int(token)
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
            if len(case) != 9 or len(result) != 3 or not all(is_int(f) for f in case + result):
                sys.exit(f"malformed row in {path}: {line.strip()!r}")
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
