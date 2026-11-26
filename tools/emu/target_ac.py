# SPDX-License-Identifier: GPL-3.0-or-later
"""A player's blow against the target's defense (FUN_0057d8a0, SUnitDmg.cpp) as the oracle.

    uv run python target_ac.py gen 20000 1 > cases.tsv
    uv run python target_ac.py game cases.tsv > g.txt      # each case line + game.exe's answer
    ../../build/tests/test_target_ac g.txt | diff g.txt -
    uv run python target_ac.py coverage cases.tsv          # instructions of FUN_0057d8a0 never reached
    uv run python target_ac.py gen 2000 2 > f.tsv           # the fixture test_target_ac checks
    uv run python target_ac.py game f.tsv > fg.txt
    uv run python target_ac.py inc fg.txt > ../../tests/target_ac_cases.inc

# gaps-runs: FUN_0057d8a0

FUN_0057d9b0 calls it for a player attacker, before the attack rating's %:
EBX the attacker, EDI the defender, the stack the attack rating's address
then the defense's, RET 8. A case line is tab-separated:
`case ignore fraction demon undead type class flags boss_byte race_byte
attack defense`:
- ignore, fraction, demon, undead: the attacker's stats 115
  (item_ignoretargetac), 116 (item_fractionaltargetac), 123
  (item_demon_tohit), 124 (item_undead_tohit), FUN_00625500's answers (RET
  0xc, hooked; any other stat stops the run).
- type: the defender's unit type, -1 a null defender. class: its +4, a
  MonStats row (0..0x23f, the hirelings' 0x10f, 0x152, 0x167, 0x230, 0x231
  among them). flags: the u16 at its monster data +0x16 (data at +0x14).
- boss_byte, race_byte: that MonStats row's bytes +0xc and +0xd, in a
  0x240-row table the data table pointer (0x744304) names (+0xa78 rows,
  +0xa80 count).
`game` appends `|attack defense` after the call. Real: FUN_0057d8a0, its
readers FUN_005a0180 (the data flags), FUN_0063e9f0 (MonStats +0xc & 0x40),
FUN_0063ee90 (the hireling classes), FUN_0063e940 (+0xd & 0x20, demon),
FUN_0063e990 (+0xd & 0x18, undead) and the MulDiv FUN_00483360.
d2d's side: d2d::rules::target_defense (components/rules/combat/combat.hpp),
tests/test_target_ac.cpp, with the defender as Monster::target gives it.
"""
import random
import sys

from unicorn import UC_HOOK_CODE

import emu

TARGET_AC = 0x57D8A0
BODY = 0x103
STAT = 0x625500
DATA_TABLE = 0x744304
ROWS, ROW_SIZE = 0x240, 0x1A8
HIRELINGS = (0x10F, 0x152, 0x167, 0x230, 0x231)
FLAGS = (0, 1, 1 | 8, 1 | 4 | 8, 2, 2 | 8, 0x10, 1 | 0x10, 4, 8, 0xFFFF)
STATS = {0x73: 0, 0x74: 1, 0x7B: 2, 0x7C: 3}


def value(rng, small):
    roll = rng.random()
    if roll < 0.25:
        return 0
    if roll < 0.7:
        return rng.randint(-small, small)
    if roll < 0.85:
        return rng.choice((1, 2, 99, 100, 101, 199, 200, 201, -1, 0x7FFFFFFF, -0x80000000))
    return rng.randint(-0x80000000, 0x7FFFFFFF)


def gen(count, seed):
    rng = random.Random(seed)
    for case in range(count):
        ignore = rng.choice((0, 0, 1, 1, 2, -1, 0x7FFFFFFF))
        fraction = rng.choice((0, 0, value(rng, 150), rng.randint(1, 120)))
        demon, undead = (rng.choice((0, value(rng, 500))) for _ in range(2))
        kind = rng.choices((-1, 0, 1, 2, 3, 4, 5), weights=(1, 4, 12, 1, 1, 1, 1))[0]
        cls = rng.choice(HIRELINGS) if rng.random() < 0.2 else rng.randrange(ROWS)
        flags = rng.choice(FLAGS) if rng.random() < 0.9 else rng.randrange(0x10000)
        boss = rng.choice((0, 0x40, 0xBF, 0xFF, rng.randrange(256)))
        race = rng.choice((0, 0x20, 0x08, 0x10, 0x18, 0x38, 0xC7, rng.randrange(256)))
        attack = value(rng, 3000)
        defense = rng.choice((value(rng, 3000), rng.randint(0, 0x100000), rng.randint(0x100000, 0x7FFFFFFF)))
        print(case, ignore, fraction, demon, undead, kind, cls, flags, boss, race, attack, defense, sep="\t")


class Game:
    def __init__(self, exe=None):
        e = self.e = emu.Emu(exe)
        table = e.alloc(0x1000)
        self.rows = e.alloc(ROWS * ROW_SIZE)
        e.w32(table + 0xA78, self.rows)
        e.w32(table + 0xA80, ROWS)
        e.w32(DATA_TABLE, table)
        self.attacker, self.defender, self.data = e.alloc(0x100), e.alloc(0x100), e.alloc(0x40)
        self.attack, self.defense = e.alloc(4), e.alloc(4)
        self.stats = [0, 0, 0, 0]
        e.hook(STAT, self._stat, 3)

    def _stat(self, e):
        unit, stat, param = e.arg(0), e.arg(1), e.arg(2)
        if unit != self.attacker or stat not in STATS or param != 0:
            sys.exit(f"FUN_00625500({unit:#x}, {stat:#x}, {param}) is not one of the attacker's four")
        return self.stats[STATS[stat]] & 0xFFFFFFFF

    def run(self, fields):
        e = self.e
        ignore, fraction, demon, undead, kind, cls, flags, boss, race, attack, defense = (int(x) for x in fields)
        self.stats = [ignore, fraction, demon, undead]
        e.w32(self.attacker, 0)
        e.w32(self.defender, max(kind, 0)); e.w32(self.defender + 4, cls); e.w32(self.defender + 0x14, self.data)
        e.mu.mem_write(self.data + 0x16, (flags & 0xFFFF).to_bytes(2, "little"))
        row = self.rows + cls * ROW_SIZE
        e.mu.mem_write(row + 0xC, bytes([boss, race]))
        e.w32(self.attack, attack); e.w32(self.defense, defense)
        e.call(TARGET_AC, self.attack, self.defense, regs={"ebx": self.attacker, "edi": 0 if kind < 0 else self.defender})
        e.mu.mem_write(row + 0xC, bytes(2))
        return e.s32(self.attack), e.s32(self.defense)


def cases(path):
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            fields = line.split("\t")
            if len(fields) != 12 or not fields[0].isdigit():
                sys.exit(f"malformed case in {path}: {line!r}")
            yield line, fields[1:]


def game(path, exe=None):
    g = Game(exe)
    for line, fields in cases(path):
        attack, defense = g.run(fields)
        print(f"{line}|{attack} {defense}")


def coverage(path, exe=None):
    g = Game(exe)
    seen = set()
    g.e.mu.hook_add(UC_HOOK_CODE, lambda mu, address, size, user: seen.add(address), begin=TARGET_AC, end=TARGET_AC + BODY - 1)
    for _line, fields in cases(path):
        g.run(fields)
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    missed = 0
    for ins in md.disasm(g.e.read(TARGET_AC, BODY), TARGET_AC):
        if ins.address not in seen:
            missed += 1
            print(f"not reached: {ins.address:08x} {ins.mnemonic} {ins.op_str}")
    print(f"{missed} instructions of FUN_0057d8a0 not reached")


def inc(path):
    print("// Generated by tools/emu/target_ac.py inc: each case line and game.exe's answer.")
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
