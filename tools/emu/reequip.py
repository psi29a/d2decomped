# SPDX-License-Identifier: GPL-3.0-or-later
"""The weapon wielded before put back on once a hand's ammunition is spent,
as the oracle: FUN_0057ff70, ReEquip (.\\PLAYER\\PlrModes.cpp).

    uv run python reequip.py gen 20000 5 > cases.tsv
    uv run python reequip.py game cases.tsv > g.txt      # each case line + game.exe's answer
    ../../build/tests/test_reequip g.txt | diff g.txt -
    uv run python reequip.py gen 1500 1 > f.tsv           # the fixture test_reequip checks
    uv run python reequip.py game f.tsv > fg.txt
    uv run python reequip.py inc fg.txt > ../../tests/reequip_cases.inc
    uv run python reequip.py coverage cases.tsv           # instructions of the body never reached

What the run shows (gen 20000 5): rules::reequip_slot matches game.exe on
all 20000 cases; `coverage` lists only the fatal of a found slot that
isn't empty (0x57ffec..0x580006), which FUN_0055d710 never answers.

FUN_0057ff70 takes the player in EDI, the game and the spent slot on the
stack (`ret 8`); FUN_00580030 calls it at 0x5801cb. Run for real here, on a
built player and item: FUN_00623410 (the player's kept id, player data
+0x90), FUN_006233a0 (its clear), FUN_00628250 (the item's page, item data
+0x45) and FUN_0063ad50 (the item's inventory, item data +0x5c, is the
player's, +0x60). Hooked, their arguments checked: FUN_00552f60 (the
game, unit type 4, the id), FUN_0055d710 (the player, the item, the slot
out pointer, mode 0), FUN_0063bde0 (the inventory, the slot found: always
empty) and FUN_00562a30 (the game, the player, the id, the slot).

A case line is `id kept of_game page ours found slot spent`:
- kept: the player's +0x90, 0 none;
- of_game: FUN_00552f60 finds the item;
- page: the item's page byte; ours: its inventory is the player's;
- found: FUN_0055d710's answer, slot the slot it writes;
- spent: FUN_0057ff70's slot argument (the slot pointer's start value).
`game` appends `slot kept_after`: the slot FUN_00562a30 put the item in
(-1 not called) and the player's +0x90 after. d2d's side is
rules::reequip_slot (tests/test_reequip.cpp).
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

START, SIZE = 0x57FF70, 0xB8
GAME = 0x6A3E0000


def gen(count, seed):
    rng = random.Random(seed)
    for case in range(count):
        kept = 0 if rng.random() < 0.1 else rng.choice((1, 2, 0x7FFFFFFF, 0xFFFFFFFF, rng.randrange(1, 1 << 32)))
        page = 0 if rng.random() < 0.6 else rng.randrange(1, 256)
        found = int(rng.random() < 0.7)
        print(case, kept, int(rng.random() < 0.85), page, int(rng.random() < 0.85), found, rng.randrange(0, 256) if found else 0,
              rng.randrange(0, 256), sep="\t")


class Game:
    def __init__(self, exe=None):
        e = self.e = emu.Emu(exe)
        self.player, self.pdata, self.inv = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
        self.item, self.idata, self.other = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
        e.w32(self.player, 0)                  # unit type 0, a player
        e.w32(self.player + 0x14, self.pdata)
        e.w32(self.player + 0x60, self.inv)
        e.w32(self.item, 4)                    # unit type 4, an item
        e.w32(self.item + 0x14, self.idata)
        self.case, self.calls = {}, []

        def check(what, got, want):
            if got != want:
                raise AssertionError(f"{what}: got {got!r}, want {want!r}")

        def ecx_edx(h):
            return h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX)

        def of_game(h):
            check("unit of", (*ecx_edx(h), h.arg(0)), (GAME, 4, self.case["kept"]))
            self.calls.append("of game")
            return self.item if self.case["of_game"] else 0

        def slot_for(h):
            check("auto-equip", (*ecx_edx(h), h.arg(1)), (self.player, self.item, 0))
            check("slot start", h.r32(h.arg(0)) & 0xFF, self.case["spent"])
            self.calls.append("slot for")
            if self.case["found"]:
                h.w32(h.arg(0), self.case["slot"])
            return self.case["found"]

        def in_slot(h):
            check("slot asked", (h.arg(0), h.arg(1)), (self.inv, self.case["slot"]))
            self.calls.append("in slot")
            return 0

        def put_on(h):
            check("put on", (*ecx_edx(h), h.arg(0)), (GAME, self.player, self.case["kept"]))
            self.calls.append(f"put on {h.arg(1)}")
            self.put = h.arg(1)
            return 1

        e.hook(0x552F60, of_game, 1)
        e.hook(0x55D710, slot_for, 2)
        e.hook(0x63BDE0, in_slot, 2)
        e.hook(0x562A30, put_on, 2)

    def run(self, kept, of_game, page, ours, found, slot, spent):
        e = self.e
        self.case = {"kept": kept, "of_game": of_game, "found": found, "slot": slot, "spent": spent}
        self.calls, self.put = [], -1
        e.w32(self.pdata + 0x90, kept)
        e.mu.mem_write(self.idata + 0x45, bytes([page]))
        e.w32(self.idata + 0x5C, self.inv if ours else self.other)
        e.call(START, GAME, spent, regs={"edi": self.player}, limit=20000)
        if e.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("did not return")
        want = []
        if kept:
            want.append("of game")
            if of_game and page == 0 and ours:
                want.append("slot for")
                if found:
                    want += ["in slot", f"put on {slot}"]
        if self.calls != want:
            raise AssertionError(f"calls {self.calls}, want {want}")
        return self.put, e.r32(self.pdata + 0x90)


def cases(path):
    for line in open(path):
        if line.strip():
            yield [int(field) for field in line.split()[:8]]


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
    print(f"{missed} instructions of FUN_0057ff70 not reached")
    return 0


def inc(path):
    lines = [line.split() for line in open(path) if line.strip()]
    print(f"// Stock game.exe 1.14d, FUN_0057ff70; generated by tools/emu/reequip.py (gen {len(lines)} 1, then game, then inc).")
    print("// kept, of_game, page, ours, found, slot, spent; then the slot put on (-1 none), the kept id after")

    def spell(row):
        return ", ".join(f"{int(field)}u" if int(field) > 0x7FFFFFFF else str(int(field)) for field in row)
    # Row { Case { 7 }, Answer { 2 } }, each in its own braces (clang's -Wmissing-braces).
    for fields in lines:
        values = fields[1:]
        print(f"    {{ {{ {spell(values[:7])} }}, {{ {spell(values[7:])} }} }},")


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
