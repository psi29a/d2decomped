# SPDX-License-Identifier: GPL-3.0-or-later
"""A player's experience award added (FUN_0057e510, SUnitDmg.cpp) as the oracle.

    uv run python exp_add.py gen 20000 1 > cases.tsv
    uv run python exp_add.py game cases.tsv > g.txt      # each case line + game.exe's answer
    ../../build/tests/test_exp_add g.txt | diff g.txt -
    uv run python exp_add.py coverage cases.tsv          # instructions of FUN_0057e510 never reached
    uv run python exp_add.py gen 2000 2 > f.tsv           # the fixture test_exp_add checks
    uv run python exp_add.py game f.tsv > fg.txt
    uv run python exp_add.py inc fg.txt > ../../tests/exp_add_cases.inc

# gaps-runs: FUN_0057e510

ESI the player (unit type 0, its class at +4), the stack the game, the level
before and the award, RET 0xc. A case line is tab-separated:
`case class exp award max_level cap level`:
- exp: the player's stat 13, FUN_006253b0's answer (RET 0xc, hooked; any
  other stat stops the run). award: the third stack argument.
- class: the unit's +4 (out of 0..6 the Experience readers take class 0's
  column). max_level and cap: every class's MaxLvl in the Experience table
  (the dword pointer at 0x96c8a8: row 0 MaxLvl, row L + 1 level L's
  experience, 8 dwords a row) and its row max_level - 1; the other rows
  climb from 0 below the cap. level: the level before (the second argument).
`game` appends `|lastexp exp levelup`: the values FUN_00627260 (RET 0x10,
hooked) set to stats 29 and 13, as signed and unsigned 32-bit numbers, and 1
when the level-up ran (FUN_00570880 with ECX the game and EDX the player,
then FUN_005c0c30 with EDX 0xc; both hooked), else 0. Real: FUN_0057e510 and
the Experience readers FUN_00611830, FUN_00611800 and FUN_00611860.
d2d's side: d2d::rules::exp_after_award (components/rules/combat/combat.hpp),
which rules::gain_exp adds with, tests/test_exp_add.cpp; the level-up and
stat 29 are not d2d's here (gain_exp levels up by its own loop), so the test
checks the stat 13 and 29 values only, the level-up flag passing through.
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

EXP_ADD = 0x57E510
BODY = 0x85
STAT_GET, STAT_SET, LEVEL_UP, EVENT = 0x6253B0, 0x627260, 0x570880, 0x5C0C30
EXPERIENCE = 0x96C8A8
GAME = 0x5EED0000                       # the game: only handed on to the level-up
CAPS = (3520485254, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0, 1, 500)


def u32(rng):
    return rng.choice((rng.randrange(1 << 32), rng.randrange(1 << 20), rng.choice((0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF))))


def gen(count, seed):
    rng = random.Random(seed)
    for case in range(count):
        cls = rng.choice((0, 1, 2, 3, 4, 5, 6, -1, 7, 100))
        max_level = rng.choice((99, 99, 2, 1, 50, 98, 100))
        cap = rng.choice(CAPS) if rng.random() < 0.6 else u32(rng)
        roll = rng.random()
        if roll < 0.3:
            exp = max(cap - rng.randrange(0, 2000), 0)
        elif roll < 0.4:
            exp = min(cap + rng.randrange(0, 2000), 0xFFFFFFFF)
        else:
            exp = u32(rng)
        award = rng.choice((rng.randint(0, 5000), rng.randint(0, 0x300000), rng.randint(-0x80000000, 0x7FFFFFFF), rng.randint(-50, 50), 0x7FFFFFFF))
        level = rng.randint(0, 100)
        print(case, cls, exp, award, max_level, cap, level, sep="\t")


class Game:
    def __init__(self, exe=None):
        e = self.e = emu.Emu(exe)
        self.table = e.alloc(8 * 4 * 102)
        e.w32(EXPERIENCE, self.table)
        self.unit = e.alloc(0x100)
        self.exp, self.sets, self.level_up = 0, {}, []
        e.hook(STAT_GET, self._get, 3)
        e.hook(STAT_SET, self._set, 4)
        e.hook(LEVEL_UP, self._level_up, 0)
        e.hook(EVENT, self._event, 3)

    def _get(self, e):
        if e.arg(0) != self.unit or e.arg(1) != 0xD or e.arg(2) != 0:
            sys.exit(f"FUN_006253b0({e.arg(0):#x}, {e.arg(1):#x}, {e.arg(2)}) is not the player's stat 13")
        return self.exp

    def _set(self, e):
        if e.arg(0) != self.unit or e.arg(1) not in (0x1D, 0xD) or e.arg(3) != 0:
            sys.exit(f"FUN_00627260({e.arg(0):#x}, {e.arg(1):#x}, _, {e.arg(3)}) is not stat 29 or 13 of the player")
        self.sets[e.arg(1)] = e.arg(2)
        return 0

    def _level_up(self, e):
        if e.mu.reg_read(UC_X86_REG_ECX) != GAME or e.mu.reg_read(UC_X86_REG_EDX) != self.unit:
            sys.exit("FUN_00570880 not given the game and the player")
        self.level_up.append("u")
        return 0

    def _event(self, e):
        if e.mu.reg_read(UC_X86_REG_ECX) != GAME or e.mu.reg_read(UC_X86_REG_EDX) != 0xC or e.arg(0) != self.unit:
            sys.exit("FUN_005c0c30 not given the game, event 0xc and the player")
        self.level_up.append("e")
        return 0

    def run(self, fields):
        e = self.e
        cls, exp, award, max_level, cap, level = (int(x) for x in fields)
        rows = [0] * (8 * 102)
        for column in range(7):
            rows[column] = max_level
            for lvl in range(101):
                rows[8 * (lvl + 1) + column] = min(lvl * lvl * 37, cap) if lvl < max_level - 1 else cap
        e.mu.mem_write(self.table, b"".join(v.to_bytes(4, "little") for v in rows))
        e.w32(self.unit, 0); e.w32(self.unit + 4, cls)
        self.exp, self.sets, self.level_up = exp, {}, []
        e.call(EXP_ADD, GAME, level, award & 0xFFFFFFFF, regs={"esi": self.unit})
        if sorted(self.sets) != [0xD, 0x1D] or self.level_up not in ([], ["u", "e"]):
            sys.exit(f"stats set {self.sets}, level-up calls {self.level_up}")
        last = self.sets[0x1D] - (1 << 32) if self.sets[0x1D] >= 1 << 31 else self.sets[0x1D]
        return last, self.sets[0xD], 1 if self.level_up else 0


def cases(path):
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            fields = line.split("\t")
            if len(fields) != 7 or not fields[0].isdigit():
                sys.exit(f"malformed case in {path}: {line!r}")
            yield line, fields[1:]


def game(path, exe=None):
    g = Game(exe)
    for line, fields in cases(path):
        last, exp, level_up = g.run(fields)
        print(f"{line}|{last} {exp} {level_up}")


def coverage(path, exe=None):
    g = Game(exe)
    seen = set()
    g.e.mu.hook_add(UC_HOOK_CODE, lambda mu, address, size, user: seen.add(address), begin=EXP_ADD, end=EXP_ADD + BODY - 1)
    for _line, fields in cases(path):
        g.run(fields)
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    missed = 0
    for ins in md.disasm(g.e.read(EXP_ADD, BODY), EXP_ADD):
        if ins.address not in seen:
            missed += 1
            print(f"not reached: {ins.address:08x} {ins.mnemonic} {ins.op_str}")
    print(f"{missed} instructions of FUN_0057e510 not reached")


def inc(path):
    print("// Generated by tools/emu/exp_add.py inc: each case line and game.exe's answer.")
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line:
                print('"' + line.replace("\t", "\\t") + '",')


def main():
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if args[:1] == ["gen"] and len(args) == 3:
        gen(int(args[1]), int(args[2]))
    elif args[:1] == ["game"] and len(args) == 2:
        game(args[1], exe)
    elif args[:1] == ["coverage"] and len(args) == 2:
        coverage(args[1], exe)
    elif args[:1] == ["inc"] and len(args) == 2:
        inc(args[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
