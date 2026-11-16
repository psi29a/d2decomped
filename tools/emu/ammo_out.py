# SPDX-License-Identifier: GPL-3.0-or-later
"""A hand's item out of ammunition, as the oracle: FUN_00580030 (.\\PLAYER\\PlrModes.cpp).

    uv run python ammo_out.py gen 20000 5 > cases.tsv
    uv run python ammo_out.py game cases.tsv > g.txt      # each case line + game.exe's answer
    ../../build/tests/test_ammo_out g.txt | diff g.txt -
    uv run python ammo_out.py gen 1500 1 > f.tsv           # the fixture test_ammo_out checks
    uv run python ammo_out.py game f.tsv > fg.txt
    uv run python ammo_out.py inc fg.txt > ../../tests/ammo_out_cases.inc
    uv run python ammo_out.py coverage cases.tsv           # instructions of the body never reached

What the run shows (gen 20000 5): rules::spent_fate and rules::reload_pick
match game.exe on all 20000 cases; `coverage` lists no instruction.

FUN_00580030 takes the item in EAX, the player in EBX and the game on the
stack (`ret 4`). A case line is `id stackable throwable stat7d quantity
weapon magic broken reload reequip slot_full wears n c0 c1 c2`:
- stackable is FUN_006289f0's answer, throwable FUN_0062ba80's, stat7d
  stat 0x7d (FUN_00625500), quantity stat 0x46 (FUN_00625480);
- weapon is FUN_00629bb0(item, 0x2d), magic FUN_0062a0f0, broken item flag
  0x100 (FUN_006280a0, line 0x513);
- reload / reequip are FUN_0062e7e0's / FUN_0062e830's (ItemTypes +0x11 /
  +0x12);
- n pack items of the item's type follow each other (FUN_0063cd80); c0..c2
  say what each is: bit 0 its class is the item's, bit 1 it is broken
  (line 0x531);
- slot_full: the item's body slot still holds something when the reload
  asks (FUN_0063bde0); wears: FUN_00562a30 puts the pack item on.
`game` appends `fate zeroed left asked took reequip`:
- fate 0 nothing, 1 the quantity written 0 and the item kept (stat 0x7d),
  2 it breaks (FUN_0055f850), 3 it leaves its slot (FUN_00560cd0 with flag
  1, after FUN_00624690(player, 1); then FUN_00628170(item, 1, 1));
- zeroed: FUN_00627260(item, 0x46, 0, 0) was called; left: FUN_00560cd0 was;
- asked: how many pack items FUN_0063cd80 was asked for (0 when Reload is 0);
- took: which of them FUN_00562a30 put on (its id and the slot checked), -1 none;
- reequip: FUN_0057ff70(game, slot) was called with the player in EDI.
Every callee but FUN_00451f60 (the class of a unit) is hooked, its
arguments checked. d2d's side is rules::spent_fate and rules::reload_pick
(tests/test_ammo_out.cpp).
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

START, SIZE = 0x580030, 424
INV, TYPE, SLOT, GAME = 0x1A2B3C40, 0x2F, 4, 0x6A3E0000
HOOKED = (0x6289F0, 0x62BA80, 0x625500, 0x625480, 0x627260, 0x629BB0, 0x62A0F0, 0x6280A0, 0x55F850, 0x62E7E0, 0x62B400, 0x627D40, 0x62E830,
          0x624690, 0x560CD0, 0x628170, 0x63CD80, 0x63BDE0, 0x562A30, 0x57FF70)


def gen(count, seed):
    rng = random.Random(seed)
    for case in range(count):
        ammo = rng.random() < 0.85
        quantity = rng.choice((0, 0, 0, -1, 1, rng.randrange(-(1 << 31), 1 << 31)))
        count_pack = rng.choice((0, 1, 2, 3, 3))
        print(case, int(ammo and rng.random() < 0.6), int(ammo and rng.random() < 0.5), rng.choice((0, 0, 0, 0, 1, 0x80000000 - (1 << 32))) if ammo or rng.random() < 0.2 else 0,
              quantity, rng.randrange(2), rng.randrange(2), int(rng.random() < 0.3), rng.choice((0, 1, 1, 0x100, 0xFF)), rng.choice((0, 1, 0x100, 0xFF)),
              int(rng.random() < 0.2), int(rng.random() < 0.7), count_pack, *(rng.choice((1, 1, 0, 2, 3)) for _ in range(3)), sep="\t")


class Game:
    def __init__(self, exe=None):
        e = self.e = emu.Emu(exe)
        self.item, self.player = e.alloc(0x100), e.alloc(0x100)
        self.pack = [e.alloc(0x100) for _ in range(3)]
        e.w32(self.item + 4, 0x51)                           # its class
        e.w32(self.player + 0x60, INV)
        self.now, self.out = {}, {}

        def check(what, got, want):
            if got != want:
                raise AssertionError(f"{what}: got {got!r}, want {want!r}")

        def of_item(key):
            def answer(h):
                check(key, h.arg(0), self.item)
                return self.now[key] & 0xFFFFFFFF
            return answer

        def stat7d(h):
            check("stat 0x7d", (h.arg(0), h.arg(1), h.arg(2)), (self.item, 0x7D, 0))
            return self.now["stat7d"] & 0xFFFFFFFF

        def quantity(h):
            check("stat 0x46", (h.arg(0), h.arg(1), h.arg(2)), (self.item, 0x46, 0))
            return self.now["quantity"] & 0xFFFFFFFF

        def zero(h):
            check("quantity write", tuple(h.arg(i) for i in range(4)), (self.item, 0x46, 0, 0))
            self.out["zeroed"] = 1
            return 0

        def weapon(h):
            check("is a weapon", (h.arg(0), h.arg(1)), (self.item, 0x2D))
            return self.now["weapon"]

        def flag(h):
            unit, line = h.arg(0), h.arg(2)
            check("flag", h.arg(1), 0x100)
            if line == 0x513:
                check("flag of", unit, self.item)
                return self.now["broken"]
            check("flag line", line, 0x531)
            return self.now["pack"][self.pack.index(unit)] >> 1 & 1

        def breaks(h):
            check("break", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0)), (GAME, self.player, self.item))
            self.out["fate"] = 2
            return 0

        def mode(h):
            check("player mode", (h.arg(0), h.arg(1)), (self.player, 1))
            return 0

        def leave(h):
            check("off its slot", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0) & 0xFF, h.arg(1)), (GAME, self.player, SLOT, 1))
            self.out.update(fate=3, left=1)
            return 1

        def transient(h):
            check("item flag", (h.arg(0), h.arg(1), h.arg(2)), (self.item, 1, 1))
            return 0

        def next_of_type(h):
            check("pack search", (h.arg(0), h.arg(1)), (INV, TYPE))
            after = h.arg(2)
            at = 0 if after == 0 else self.pack.index(after) + 1
            self.out["asked"] += 1
            return self.pack[at] if at < self.now["n"] else 0

        def in_slot(h):
            check("slot asked", (h.arg(0), h.arg(1)), (INV, SLOT))
            return 0xBEEF if self.now["slot_full"] else 0

        def put_on(h):
            took = h.arg(0) - 0x700
            check("put on", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(1) & 0xFF), (GAME, self.player, SLOT))
            if self.now["wears"]:
                self.out["took"] = took
            self.out["tried"] = took
            return self.now["wears"]

        def reequip(h):
            check("reequip", (h.arg(0), h.arg(1) & 0xFF, h.mu.reg_read(UC_X86_REG_EDI)), (GAME, SLOT, self.player))
            self.out["reequip"] = 1
            return 0

        e.hook(0x6289F0, of_item("stackable"), 1)
        e.hook(0x62BA80, of_item("throwable"), 1)
        e.hook(0x625500, stat7d, 3)
        e.hook(0x625480, quantity, 3)
        e.hook(0x627260, zero, 4)
        e.hook(0x629BB0, weapon, 2)
        e.hook(0x62A0F0, of_item("magic"), 1)
        e.hook(0x6280A0, flag, 4)
        e.hook(0x55F850, breaks, 1)
        e.hook(0x62E7E0, of_item("reload"), 1)
        e.hook(0x62B400, lambda h: TYPE, 1)
        e.hook(0x627D40, lambda h: 0x5500 | SLOT, 1)         # the slot is AL: the upper bits are not it
        e.hook(0x62E830, of_item("reequip"), 1)
        e.hook(0x624690, mode, 2)
        e.hook(0x560CD0, leave, 3)
        e.hook(0x628170, transient, 3)
        e.hook(0x63CD80, next_of_type, 3)
        e.hook(0x63BDE0, in_slot, 2)
        e.hook(0x562A30, put_on, 2)
        e.hook(0x57FF70, reequip, 2)

    def run(self, stackable, throwable, stat7d, quantity, weapon, magic, broken, reload, reequip, slot_full, wears, n, *pack):
        e = self.e
        self.now = {"stackable": stackable, "throwable": throwable, "stat7d": stat7d, "quantity": quantity, "weapon": weapon, "magic": magic, "broken": broken,
                    "reload": reload, "reequip": reequip, "slot_full": slot_full, "wears": wears, "n": n, "pack": pack}
        for at, unit in enumerate(self.pack):
            e.w32(unit + 4, 0x51 if pack[at] & 1 else 0x52)
            e.w32(unit + 0xC, 0x700 + at)
        self.out = {"fate": 0, "zeroed": 0, "left": 0, "asked": 0, "took": -1, "reequip": 0}
        e.call(START, GAME, regs={"eax": self.item, "ebx": self.player}, limit=10000)
        if e.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("did not return")
        if self.out["zeroed"]:
            if self.out["fate"]:
                raise AssertionError("zeroed and more")
            self.out["fate"] = 1
        return tuple(self.out[key] for key in ("fate", "zeroed", "left", "asked", "took", "reequip"))


def cases(path):
    for line in open(path):
        if line.strip():
            yield [int(field) for field in line.split()[:16]]


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
    print(f"{missed} instructions of FUN_00580030 not reached")
    return 1 if missed else 0


def inc(path):
    lines = [line.split() for line in open(path) if line.strip()]
    print(f"// Stock game.exe 1.14d, FUN_00580030; generated by tools/emu/ammo_out.py (gen {len(lines)} 1, then game, then inc).")
    print("// stackable, throwable, stat 0x7d, quantity, weapon, magic, broken, Reload, ReEquip, slot full, wears, pack items and what each is; then fate, zeroed, left, asked, took, reequip")
    def spell(row):
        return ", ".join(str(int(field)) if int(field) != -(1 << 31) else "(-2147483647 - 1)" for field in row)
    # Row { Case { 12 ints, pack[3] }, Answer { 6 ints } }, each in its own braces (clang's -Wmissing-braces).
    for fields in lines:
        values = fields[1:]
        print(f"    {{ {{ {spell(values[:12])}, {{ {spell(values[12:15])} }} }}, {{ {spell(values[15:])} }} }},")


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
