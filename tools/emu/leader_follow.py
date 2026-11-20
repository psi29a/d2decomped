# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's walk back to its leader in game.exe as the oracle: the AI driver's first step FUN_005b0ff0, on the unit
its AI control names at +0xc / +0x10 (set by FUN_005dd330: a unique's minions their leader, FUN_005a0c00 0x5a0cb9; a
revived monster its owner, FUN_005c55c0 0x5c561b).

    uv run python leader_follow.py gen 100000 1 > cases.txt     # `id type pick found dying gap backed`
    uv run python leader_follow.py game < cases.txt > game.txt   # each case and `| acted id back path walk`
    ../../build/tests/test_leader_follow --leader-follow < cases.txt | grep ' | ' > d2d.txt
    uv run python leader_follow.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the control's +0xc (the leader's id, -1 none) and +0x10 (its type: 0 a player, 1 a monster), the monster's
MonStats row (`pick` modulo the rows; FUN_0046c140(row, 2): a WL mode), whether the lookup finds the leader
(FUN_00552f60), whether the leader is dead or dying (FUN_005541b0), the distance to it (FUN_005dc380) and whether a
back-off takes (FUN_005defe0's return). FUN_005b0ff0(ECX game, EDX monster; the control block) runs for real.
Each line: the case, then `acted id back path walk`: its return, +0xc after it, and whether it backed off (FUN_005defe0,
0x13 from the leader), set the path (FUN_005de190(7, 0, 0x28)) and walked about the leader (FUN_005df530, 0x13).
Hooked (inputs and side effects, not the claim): FUN_00552f60, FUN_005541b0, FUN_005dc380, FUN_005defe0, FUN_005de190,
FUN_005df530 (recorded).
"""
import random
import sys

USAGE = "usage: leader_follow.py gen N SEED | game < CASES | compare GAME D2D"


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.monster, self.leader, self.ctrl, self.block = (e.alloc(0x200) for _ in range(5))
        self.rows = e.r32(e.r32(0x744304) + 0xa80)
        self.found, self.dying, self.gap, self.backed = 0, 0, 0, 0
        self.calls = set()
        hooks = ((0x552f60, lambda e: self.leader if self.found else 0, 1), (0x5541b0, lambda e: self.dying, 0),
                 (0x5dc380, lambda e: self.gap & 0xffffffff, 0), (0x5defe0, self.back, 3), (0x5de190, self.path, 2),
                 (0x5df530, self.walk, 2))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def back(self, e):
        self.calls.add("back")
        return self.backed

    def path(self, e):
        import emu
        if e.mu.reg_read(emu.UC_X86_REG_EDX) == 7 and e.arg(0) == 0 and e.arg(1) == 0x28:
            self.calls.add("path")
        else:
            self.calls.add("path?")
        return 0

    def walk(self, e):
        self.calls.add("walk" if e.arg(1) == 0x13 and e.arg(0) == self.leader else "walk?")
        return 1

    def run(self, ident, kind, pick, found, dying, gap, backed):
        e = self.e
        self.found, self.dying, self.gap, self.backed, self.calls = found, dying, gap, backed, set()
        e.mu.mem_write(self.monster, bytes(0x200))
        e.w32(self.monster, 1)
        e.w32(self.monster + 4, pick % self.rows)
        e.w32(self.ctrl + 0xc, ident & 0xffffffff)
        e.w32(self.ctrl + 0x10, kind)
        e.w32(self.block, self.ctrl)
        acted = e.call(0x5b0ff0, self.block, ecx=self.game, edx=self.monster)
        return " ".join(str(v) for v in (1 if acted else 0, e.s32(self.ctrl + 0xc), int("back" in self.calls), int("path" in self.calls),
                                         int("walk" in self.calls)))


def gen(count, seed):
    rng = random.Random(seed)
    for pick in range(0, 800, 5):
        for ident, kind in ((-1, 1), (7, 1), (7, 0)):
            for found, dying in ((1, 0), (0, 0), (1, 1)):
                for gap in (-1, 0, 1, 2, 19, 20, 21, 0x7fffffff):
                    for backed in (0, 1):
                        print(ident, kind, pick, found, dying, gap, backed)
    for _ in range(count):
        print(rng.choice((-1, 3, 7, rng.randrange(1 << 16))), rng.choice((0, 1, 1, 2)), rng.randrange(1 << 16), rng.choice((1, 1, 1, 0)),
              rng.choice((0, 0, 0, 1)), rng.choice((rng.randint(-2, 3), rng.randint(0, 40), rng.randint(18, 22))), rng.choice((0, 1)))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:7])))


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
            if len(case) != 7 or len(result) != 5 or not all(is_int(f) for f in case + result):
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
