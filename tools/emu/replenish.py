# SPDX-License-Identifier: GPL-3.0-or-later
"""A replenishing item's timer 3, as the oracle: FUN_00562d30, FUN_00562c40, FUN_00558530, FUN_00558580.

    uv run python replenish.py gen 50000 3 > cases.tsv
    uv run python replenish.py game cases.tsv > g.txt     # each case line + game.exe's answer
    ../../build/tests/test_replenish g.txt | diff g.txt -
    uv run python replenish.py gen 1500 1 > f.tsv          # the fixture test_replenish checks
    uv run python replenish.py game f.tsv > fg.txt
    uv run python replenish.py inc fg.txt > ../../tests/replenish_cases.inc
    uv run python replenish.py coverage cases.tsv          # instructions of the four bodies never reached

What the run shows (gen 50000 3): rules::replenish_step and
rules::replenish_first match game.exe on all 50000 cases, the 32-bit
range of every number included; `coverage` lists no instruction.

A case line is `id fn broken wears stacks owner drate dur dmost qrate qty
qmost timer`:
- fn 3 runs timer 3, FUN_00562d30 (ECX game, EDX item), with FUN_00562c40
  (EAX the rate's stat, EBX the value's, EDI the item; the game and the
  most on the stack) run for real inside it; 252 runs FUN_00558530 and
  253 FUN_00558580 (ECX game, EDX item).
- broken is FUN_006280a0's answer for flag 0x100, wears FUN_00629930's,
  stacks FUN_006289f0's; dmost / qmost FUN_00625e00's / FUN_006295b0's.
- drate, dur, qrate, qty are stats 0xfc, 0x48, 0xfd, 0x46 (FUN_00625480
  reads, FUN_00627260 writes).
- owner: 0 item data +0xc is -1 (FUN_00629f20), 1 no such unit
  (FUN_00552f60), 2 a unit without a client (FUN_005531c0), 3 a client.
- timer is FUN_005415a0's answer for kind 3 (the queue calls only).
`game` appends `dur qty queued wait mend told`: the two stats after the
call, whether FUN_005417d0 was called with kind 3 for the item, its frame
less the game's (game +0xa8), whether FUN_0055f900 was called (game,
owner, item), and whether the owner's client was sent the stat
(FUN_0053d130). d2d has no packet there, so `told` is game.exe's only;
the test checks it against owner and the point. FUN_005417d0's own lift of
a frame not after the game's (FUN_005416b0, 0x5416e9) is not run here.
d2d's side is rules::replenish_step / replenish_first
(tests/test_replenish.cpp).
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

BODIES = {0x562D30: 106, 0x562C40: 226, 0x558530: 77, 0x558580: 77}
INT_MIN, INT_MAX = -(1 << 31), (1 << 31) - 1
CLIENT, OWNER_ID = 0x7E57C0DE, 0x1234


def s32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >> 31 else value


def gen(count, seed):
    rng = random.Random(seed)
    edges = (INT_MIN, INT_MIN + 1, -2501, -2500, -2499, -126, -125, -21, -20, -2, -1, 0, 0, 1, 2, 19, 20, 21, 124, 125, 126,
             2499, 2500, 2501, INT_MAX - 1, INT_MAX)

    def rate():
        kind = rng.random()
        return 0 if kind < 0.2 else rng.randrange(1, 60) if kind < 0.7 else rng.choice(edges) if kind < 0.9 else rng.randrange(INT_MIN, INT_MAX + 1)

    def pair():
        """A value and its most: play, the edges around each other, the whole range."""
        kind = rng.random()
        if kind < 0.5:
            most = rng.randrange(0, 512)
            return rng.choice((most, most - 1, most - 2, 0, rng.randrange(0, most + 2))), most
        if kind < 0.8:
            most = rng.choice(edges)
            return s32(most + rng.choice((-2, -1, 0, 1))), most
        return rng.randrange(INT_MIN, INT_MAX + 1), rng.randrange(INT_MIN, INT_MAX + 1)

    for case in range(count):
        fn = rng.choice((3, 3, 3, 3, 252, 253))
        dur, dmost = pair()
        qty, qmost = pair()
        timer = 0 if fn == 3 or rng.random() < 0.6 else rng.choice((1, 100, INT_MAX, rng.randrange(1, INT_MAX)))
        print(case, fn, rng.randrange(2), int(rng.random() < 0.7), int(rng.random() < 0.7), rng.choice((0, 1, 2, 3, 3, 3)),
              rate(), dur, dmost, rate(), qty, qmost, timer, sep="\t")


class Game:
    def __init__(self, exe=None):
        e = self.e = emu.Emu(exe)
        self.game, self.item, self.owner = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200)
        self.now, self.stats, self.out = {}, {}, {}

        def check(what, got, want):
            if got != want:
                raise AssertionError(f"{what}: got {got!r}, want {want!r}")

        def stat_get(h):
            check("stat read", (h.arg(0), h.arg(2)), (self.item, 0))
            return self.stats[h.arg(1)] & 0xFFFFFFFF

        def stat_set(h):
            check("stat write", (h.arg(0), h.arg(3)), (self.item, 0))
            self.stats[h.arg(1)] = s32(h.arg(2))
            return 0

        def flag(h):
            check("flag", (h.arg(0), h.arg(1)), (self.item, 0x100))
            return self.now["broken"]

        def find_unit(h):
            check("unit lookup", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0)), (self.game, 0, OWNER_ID))
            return self.owner if self.now["owner"] >= 2 else 0

        def client(h):
            check("client of", h.mu.reg_read(UC_X86_REG_ECX), self.owner)
            return CLIENT if self.now["owner"] == 3 else 0

        def tell(h):
            check("stat packet", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0), h.arg(3)), (CLIENT, self.item, 1, 0))
            check("stat packet's value", s32(h.arg(2)), self.stats[h.arg(1)])
            self.out["told"] = 1
            return 0

        def mend(h):
            check("mend", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0)), (self.game, self.owner, self.item))
            self.out["mend"] = 1
            return 0

        def queue(h):
            check("queue", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0), h.arg(2), h.arg(3)), (self.game, self.item, 3, 0, 0))
            check("queued once", self.out["queued"], 0)
            self.out.update(queued=1, wait=s32(h.arg(1) - self.now["frame"]))
            return 0

        def timer(h):
            check("timer lookup", (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX), h.arg(0)), (self.game, self.item, 3))
            return self.now["timer"]

        def of_item(key):
            def answer(h):
                check(key, h.arg(0), self.item)
                return self.now[key] & 0xFFFFFFFF
            return answer

        e.hook(0x625480, stat_get, 3)
        e.hook(0x627260, stat_set, 4)
        e.hook(0x6280A0, flag, 4)
        e.hook(0x629930, of_item("wears"), 1)
        e.hook(0x625E00, of_item("dmost"), 1)
        e.hook(0x6289F0, of_item("stacks"), 1)
        e.hook(0x6295B0, of_item("qmost"), 1)
        e.hook(0x629F20, lambda h: OWNER_ID if self.now["owner"] else 0xFFFFFFFF, 1)
        e.hook(0x552F60, find_unit, 1)
        e.hook(0x5531C0, client, 1)
        e.hook(0x53D130, tell, 4)
        e.hook(0x55F900, mend, 1)
        e.hook(0x5417D0, queue, 4)
        e.hook(0x5415A0, timer, 1)

    def run(self, fn, broken, wears, stacks, owner, drate, dur, dmost, qrate, qty, qmost, timer):
        e = self.e
        frame = (dur * 31 + qty) & 0xFFFFFFFF                # any frame: the wait is a difference
        e.w32(self.game + 0xA8, frame)
        self.now = {"broken": broken, "wears": wears, "stacks": stacks, "owner": owner, "dmost": dmost, "qmost": qmost, "timer": timer, "frame": frame}
        self.stats = {0xFC: drate, 0x48: dur, 0xFD: qrate, 0x46: qty}
        self.out = {"queued": 0, "wait": 0, "mend": 0, "told": 0}
        e.call({3: 0x562D30, 252: 0x558530, 253: 0x558580}[fn], ecx=self.game, edx=self.item, limit=10000)
        if e.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError("did not return")
        return self.stats[0x48], self.stats[0x46], self.out["queued"], self.out["wait"], self.out["mend"], self.out["told"]


def cases(path):
    for line in open(path):
        if line.strip():
            yield [int(field) for field in line.split()[:13]]


def game(path, exe=None):
    g = Game(exe)
    for case in cases(path):
        print(*case, *g.run(*case[1:]), sep="\t")


def coverage(path, exe=None):
    g = Game(exe)
    seen = set()
    g.e.mu.hook_add(UC_HOOK_CODE, lambda mu, address, size, user: seen.add(address))
    for case in cases(path):
        g.run(*case[1:])
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    hooked = {0x625480, 0x627260, 0x6280A0, 0x629930, 0x625E00, 0x6289F0, 0x6295B0, 0x629F20, 0x552F60, 0x5531C0, 0x53D130, 0x55F900, 0x5417D0, 0x5415A0}
    missed = 0
    for start, size in sorted(BODIES.items()):
        assert start not in hooked
        for ins in md.disasm(g.e.read(start, size), start):
            if ins.address not in seen and ins.mnemonic not in ("int3", "nop"):
                print(f"not reached: {ins.address:08x}  {ins.mnemonic} {ins.op_str}")
                missed += 1
    print(f"{missed} instructions of {len(BODIES)} bodies not reached")
    return 1 if missed else 0


def inc(path):
    lines = [line.split() for line in open(path) if line.strip()]
    print(f"// Stock game.exe 1.14d, FUN_00562d30 (fn 3), FUN_00558530 (252), FUN_00558580 (253); generated by tools/emu/replenish.py (gen {len(lines)} 1, then game, then inc).")
    print("// fn, broken, wears, stacks, owner, stat 0xfc, stat 0x48, its most, stat 0xfd, stat 0x46, its most, the timer queued; then stat 0x48, stat 0x46, queued, the wait, mended, told")
    for fields in lines:
        print("    { " + ", ".join(str(int(field)) if int(field) != INT_MIN else "(-2147483647 - 1)" for field in fields[1:]) + " },")


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
