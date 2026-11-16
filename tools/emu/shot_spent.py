# SPDX-License-Identifier: GPL-3.0-or-later
"""A shot spends one of the hand's ammunition, as the oracle: FUN_0056c310.

    uv run python shot_spent.py gen 20000 5 > cases.tsv
    uv run python shot_spent.py game cases.tsv > g.txt      # each case line + game.exe's answer
    ../../build/tests/test_shot_spent g.txt | diff g.txt -
    uv run python shot_spent.py gen 1500 1 > f.tsv           # the fixture test_shot_spent checks
    uv run python shot_spent.py game f.tsv > fg.txt
    uv run python shot_spent.py inc fg.txt > ../../tests/shot_spent_cases.inc
    uv run python shot_spent.py coverage cases.tsv           # instructions of the body never reached

What the run shows (gen 20000 5): rules::shot_spent matches game.exe on
all 20000 cases; `coverage` lists no instruction.

FUN_0056c310 takes the game in ECX, the player in EDX and the item on the
stack (`ret 4`); its one caller, FUN_0056c3f0, picks the hand's item. A
case line is `id none quantity durability most`:
- none: the item is null;
- quantity is stat 0x46, durability stat 0x48 (FUN_00625480), most
  FUN_00625e00's answer (the max durability).
`game` appends `quantity durability answer`: each stat's value after the
call (FUN_00627260's write, else the one read) and FUN_0056c310's EAX.
Every callee is hooked, its arguments checked: the replenish timer
(FUN_00558580, game and item), the client told of each write
(FUN_005531c0 then FUN_0053d130: the line, then 1, the stat, the value,
0), and FUN_00580310 (game and player) last. d2d's side is
rules::shot_spent (tests/test_shot_spent.cpp).
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

START, SIZE = 0x56C310, 220
GAME, CLIENT, MESSAGES = 0x6A3E0000, 0x6A3F0000, 0x6DAB88
INT_MIN = -(1 << 31)


def s32(value):
    return value - (1 << 32) if value & 0x80000000 else value


def gen(count, seed):
    rng = random.Random(seed)
    edge = (0, 1, 2, -1, INT_MIN, INT_MIN + 1, (1 << 31) - 1)

    def value():
        return rng.choice((rng.choice(edge), rng.randrange(0, 512), rng.randrange(INT_MIN, 1 << 31)))
    for case in range(count):
        durability = value()
        most = durability if rng.random() < 0.3 else value()
        print(case, int(rng.random() < 0.05), value(), durability, most, sep="\t")


class Game:
    def __init__(self, exe=None):
        e = self.e = emu.Emu(exe)
        self.item, self.player = e.alloc(0x100), e.alloc(0x100)
        self.now, self.out, self.calls = {}, {}, []

        def check(what, got, want):
            if got != want:
                raise AssertionError(f"{what}: got {got!r}, want {want!r}")

        def read(h):
            check("stat read", (h.arg(0), h.arg(2)), (self.item, 0))
            stat = h.arg(1)
            check("stat", stat in (0x46, 0x48), True)
            self.calls.append(f"read {stat:x}")
            return self.out[stat] & 0xFFFFFFFF

        def write(h):
            check("stat write", (h.arg(0), h.arg(3)), (self.item, 0))
            stat, value = h.arg(1), s32(h.arg(2))
            check("stat", stat in (0x46, 0x48), True)
            self.out[stat] = value
            self.calls.append(f"write {stat:x}")
            return 0

        def armed(h):
            check("replenish", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX)), (GAME, self.item))
            self.calls.append("armed")
            return 0

        def client(h):
            check("client of", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX)), (self.player, MESSAGES))
            self.calls.append(f"line {h.arg(0):x}")
            return CLIENT

        def told(h):
            stat = h.arg(1)
            check("told", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0), s32(h.arg(2)), h.arg(3)),
                  (CLIENT, self.item, 1, self.out[stat], 0))
            self.calls.append(f"told {stat:x}")
            return 0

        def most(h):
            check("max durability of", h.arg(0), self.item)
            self.calls.append("most")
            return self.now["most"] & 0xFFFFFFFF

        def ran_out(h):
            check("out of ammo", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX)), (GAME, self.player))
            self.calls.append("ran out")
            return 0

        e.hook(0x625480, read, 3)
        e.hook(0x627260, write, 4)
        e.hook(0x558580, armed, 0)
        e.hook(0x5531C0, client, 1)
        e.hook(0x53D130, told, 4)
        e.hook(0x625E00, most, 1)
        e.hook(0x580310, ran_out, 0)

    def run(self, none, quantity, durability, most):
        e = self.e
        self.now = {"most": most}
        self.out = {0x46: quantity, 0x48: durability}
        self.calls = []
        e.call(START, 0 if none else self.item, ecx=GAME, edx=self.player, limit=10000)
        if e.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("did not return")
        answer = s32(e.mu.reg_read(UC_X86_REG_EAX))
        want = [] if none else ["read 46", "write 46", "armed", "line 546", "told 46", "most", "read 48"]
        if not none and self.out[0x48] != durability:
            want += ["write 48", "line 54c", "told 48"]
        if not none:
            want.append("ran out")
        if self.calls != want:
            raise AssertionError(f"calls {self.calls}, want {want}")
        return self.out[0x46], self.out[0x48], answer


def cases(path):
    for line in open(path):
        if line.strip():
            yield [int(field) for field in line.split()[:5]]


def game(path, exe=None):
    g = Game(exe)
    for case in cases(path):
        print(*case, *g.run(*case[1:]), sep="\t")


def coverage(path, exe=None):
    import capstone
    g = Game(exe)
    seen = set()
    g.e.mu.hook_add(UC_HOOK_CODE, lambda mu, address, size, user: seen.add(address))
    for case in cases(path):
        g.run(*case[1:])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    missed = 0
    for ins in md.disasm(g.e.read(START, SIZE), START):
        if ins.address not in seen and ins.mnemonic not in ("int3", "nop"):
            print(f"not reached: {ins.address:08x}  {ins.mnemonic} {ins.op_str}")
            missed += 1
    print(f"{missed} instructions of FUN_0056c310 not reached")
    return 1 if missed else 0


def inc(path):
    lines = [line.split() for line in open(path) if line.strip()]
    print(f"// Stock game.exe 1.14d, FUN_0056c310; generated by tools/emu/shot_spent.py (gen {len(lines)} 1, then game, then inc).")
    print("// none, quantity, durability, its most; then quantity, durability, the answer")

    def spell(row):
        return ", ".join(str(int(field)) if int(field) != INT_MIN else "(-2147483647 - 1)" for field in row)
    # Row { Case { 4 ints }, Answer { 3 ints } }, each in its own braces (clang's -Wmissing-braces).
    for fields in lines:
        values = fields[1:]
        print(f"    {{ {{ {spell(values[:4])} }}, {{ {spell(values[4:])} }} }},")


if __name__ == "__main__":
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if len(args) == 3 and args[0] == "gen":
        gen(int(args[1]), int(args[2]))
    elif len(args) == 2 and args[0] == "game":
        game(args[1], exe)
    elif len(args) == 2 and args[0] == "coverage":
        sys.exit(coverage(args[1], exe))
    elif len(args) == 2 and args[0] == "inc":
        inc(args[1])
    else:
        sys.exit(__doc__)
