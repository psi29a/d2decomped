# SPDX-License-Identifier: GPL-3.0-or-later
"""Taunt's do (srvdofunc 71, FUN_005d8570) on the taunted monster's leader, in stock game.exe 1.14d.

    export D2_MPQ_DIR=/path/to/data                                  # its bin/game.exe
    uv run python taunt_leader.py gen 2000 1 > cases.txt             # `id type put`
    uv run python taunt_leader.py game < cases.txt > game.txt        # each case and `| id type`
    ../../build/tests/test_taunt_leader --taunt-leader < cases.txt > d2d.txt
    uv run python taunt_leader.py compare game.txt d2d.txt           # `N cases, M differ`; exit 1 on a difference
    uv run python taunt_leader.py --mutation-check                   # the model against game.exe, then 3 mutants caught

A case: the monster's AI control (monster data +0x28) +0xc (its leader's id, -1 none) and +0x10 (the leader's type)
before, and whether the state put (FUN_005c35c0, its return) takes. Each line: the case, then +0xc and +0x10 after.
FUN_005d8570 (ECX game, EDX caster, stack skill id and level; `ret 8`) runs for real with FUN_005b0d70 and
FUN_005dd330; the claim: FUN_005dd330(ECX the taunted monster, EDX 0) at 0x5d8718 sets +0xc -1 and +0x10 1, put or
not, and FUN_005b0d70(-666, -666, -666) leaves +0x14..+0x1c. Each case runs twice: the monster as the unit cast at
(FUN_00553540) and as the nearest one FUN_0056bbc0 finds; both must agree.
Hooked (inputs and side effects, not the claim): FUN_00553540, FUN_005d8510, FUN_00554200, FUN_0056bbc0,
FUN_00646ca0, FUN_005c35c0, FUN_00540e60, FUN_005417d0, FUN_00620c10. The Skills row is a fake table behind
game.exe's pointer at 0x744304 (+0x82 auratargetstate 5, +0x54 no aurastats).
"""
import random
import sys

import emu

USAGE = "usage: taunt_leader.py gen N SEED | game < CASES | compare GAME D2D | --mutation-check"
GLOBAL, SKILL, ROW = 0x744304, 137, 0x23C
SCRATCH = (0x1111, 0x2222, 0x3333)        # +0x14 .. +0x1c before; FUN_005b0d70's -666 leaves them


class Rig:
    """game.exe with Taunt's callees hooked once."""

    def __init__(self):
        """Load the exe, the fake Skills table and the units, and hook the callees."""
        self.e = e = emu.Emu()
        self.game, self.caster, self.monster, self.data, self.ctrl = (e.alloc(0x200) for _ in range(5))
        table = e.alloc(0x1000)
        rows = e.alloc(ROW * (SKILL + 1))
        e.w32(GLOBAL, table)
        e.w32(table + 0xB98, rows)
        e.w32(table + 0xBA0, SKILL + 1)
        e.w32(table + 0xC4, 100)                 # states
        e.w32(table + 0xBD4, 100)                # ItemStatCost rows
        row = rows + SKILL * ROW
        e.mu.mem_write(row + 0x82, (5).to_bytes(2, "little"))
        e.mu.mem_write(row + 0x54, (0xFFFF).to_bytes(2, "little"))
        e.w32(self.game + 0xA8, 1000)
        e.w32(self.caster, 0)                    # a player
        e.w32(self.monster, 1)
        e.w32(self.monster + 0x14, self.data)
        e.w32(self.data + 0x28, self.ctrl)
        self.direct, self.put, self.puts = True, 0, 0
        hooks = ((0x553540, lambda e: self.monster if self.direct else 0, 2), (0x5D8510, lambda e: 1, 0),
                 (0x554200, lambda e: 1, 1), (0x56BBC0, lambda e: self.monster, 2), (0x646CA0, lambda e: 0, 4),
                 (0x5C35C0, self.state, 0), (0x540E60, lambda e: 0, 2), (0x5417D0, lambda e: 0, 4),
                 (0x620C10, lambda e: 0, 2))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def state(self, e):
        """Hook FUN_005c35c0: the state put, taken or not by the case."""
        self.puts += 1
        return self.put

    def once(self, ident, kind, put, direct):
        """Run FUN_005d8570 on one case; return (+0xc, +0x10) after."""
        e = self.e
        self.direct, self.put, self.puts = direct, put, 0
        e.w32(self.ctrl + 0xC, ident)
        e.w32(self.ctrl + 0x10, kind)
        for offset, value in zip((0x14, 0x18, 0x1C), SCRATCH, strict=True):
            e.w32(self.ctrl + offset, value)
        done = e.call(0x5D8570, SKILL, 1, ecx=self.game, edx=self.caster, limit=20000)
        assert done == 1 and self.puts == 1, (done, self.puts)
        assert tuple(e.r32(self.ctrl + offset) for offset in (0x14, 0x18, 0x1C)) == SCRATCH
        return e.s32(self.ctrl + 0xC), e.s32(self.ctrl + 0x10)

    def run(self, ident, kind, put):
        """Both ways to the monster; they must agree."""
        first = self.once(ident, kind, put, True)
        assert first == self.once(ident, kind, put, False), (ident, kind, put)
        return first


def model(ident, kind, put, mutant=None):
    """d2d's rule (put_state on a Taunt): the leader none, put or not."""
    if mutant == "kept":
        return ident, kind
    if mutant == "put_only" and not put:
        return ident, kind
    if mutant == "type0":
        return -1, 0
    return -1, 1


def cases(count, seed):
    """Every corner, then random ones."""
    rng = random.Random(seed)
    out = [(ident, kind, put) for ident in (-1, 0, 7) for kind in (0, 1, 6) for put in (0, 1)]
    out += [(rng.choice((-1, rng.randrange(1 << 16))), rng.choice((0, 1, 1, 6)), rng.choice((0, 1))) for _ in range(count)]
    return out


def gen(count, seed):
    """Print the cases."""
    for case in cases(count, seed):
        print(*case)


def game():
    """Each case from stdin and game.exe's answer."""
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", *rig.run(*(int(f) for f in field[:3])))


def rows(path):
    """A result file's (case, result) rows; exit on a malformed one."""
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            if "|" not in line:
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            case, result = (part.split() for part in line.split("|", 1))
            if len(case) != 3 or len(result) != 2:
                sys.exit(f"malformed row in {path}: {line.strip()!r}")
            out.append((case, result))
    return out


def compare(game_path, d2d_path):
    """Diff game.exe's answers with d2d's; exit 1 on a difference or an empty or unequal pair."""
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


def mutation_check():
    """The model against game.exe on every case, then each mutant must miss at least one."""
    rig = Rig()
    answers = [(case, rig.run(*case)) for case in cases(500, 7)]
    bad = [case for case, answer in answers if model(*case) != answer]
    if bad:
        sys.exit(f"model differs from FUN_005d8570 on {len(bad)} cases, first {bad[0]}")
    print(f"ok: {len(answers)} FUN_005d8570 cases")
    for mutant in ("kept", "put_only", "type0"):
        if all(model(*case, mutant=mutant) == answer for case, answer in answers):
            sys.exit(f"mutant {mutant} not caught")
        print(f"mutant {mutant} caught")
    print("mutation check ok")


def main():
    """Dispatch the subcommand."""
    args = sys.argv[1:]
    if args == ["--mutation-check"]:
        mutation_check()
    elif args and (args[0], len(args)) in {("gen", 3), ("game", 1), ("compare", 3)}:
        if args[0] == "gen":
            gen(int(args[1]), int(args[2]))
        elif args[0] == "game":
            game()
        else:
            compare(args[1], args[2])
    else:
        sys.exit(USAGE)


if __name__ == "__main__":
    main()
