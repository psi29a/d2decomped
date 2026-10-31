# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's Town Portal read from the pack (C->S 0x20) as the oracle.

The server's path runs natively: the dispatcher FUN_0054d750 (its test
FUN_0057eec0 refuses a dead player, mode 0 / 0x11), the handler FUN_0054b1e0,
FUN_0055e170 (use an inventory item: no item in hand, a tome's charges
stat 0x46 > 0, the charge or the scroll spent after a use that worked),
FUN_005678a0, FUN_005bf240 (books.txt pSpell, the table at 0x741790),
FUN_005be290 (pSpell 2: not in town, the old pair closed, the portal made),
FUN_0055e050 and FUN_005be1c0. The player's mode word (+0x10) is set per case.

Hooked (not the claim): the trace line (0x55bf50), the player's states
(0x639df0: none), the item's owner (0x63ad50) and the player's x, y (0x6488c0,
0x648900) for the range test, the item lookup (0x552f60), the item in hand
(0x63c1e0, per case), the item's requirements (0x628c20: met), its type
(0x62b400, 0x629bb0: 0x12 tome, 0x16 scroll), its stat 0x46 (0x625480, per
case), no trade partner (0x554100), its class and rows (0x627f80, 0x6374b0
books.txt pSpell 2, 0x6335f0), item flags (0x6280a0, 0x6280d0), the room and
level (0x620bb0, 0x61a1b0 Blood Moor, 0x6427f0, 0x61ab70), the town test
(0x61ab00, per case), the portal's coordinates (0x45ae20, 0x45adf0), packets
and voice (0x5353b0, 0x552af0, 0x58cf00, 0x553720, 0x5531c0, 0x53b3d0,
0x53d130, 0x53c520, 0x553380, 0x55e0d0, 0x6439b0, 0x63b2c0, 0x543520).
Recorded: the old pair closed (FUN_00535430), the portal made (FUN_0056d130:
its free-spot search is hooked and returns the portal or 0, per case), the
stat change (FUN_006272b0), the scroll removed (FUN_0055e000).

    uv run python tome_use.py gen 100000 7 > cases.tsv
    uv run python tome_use.py game cases.tsv > game.tsv
    ../../build/tests/test_game --tome-use cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv
    diff game.tsv d2d.tsv

A case is `id item charges town held mode spot`: tbk or tsc, the tome's
stat 0x46, in town, an item in hand, the player's mode (0..19), a free spot
for the portal. `game` prints `id opened spent kept`: the tick after the use
the new pair stood (0: the use's own, -1: none), the charge or the scroll
spent, the player's old pair still there.
"""
import random
import sys

import emu
from unicorn.x86_const import UC_X86_REG_EDX

KIND = {"tbk": 0x12, "tsc": 0x16}
CHARGES = list(range(-2, 21)) + [255, 1 << 20]


def gen(n_random, seed):
    rng = random.Random(seed)
    cases = [(k, q, t, h, m, s) for k in KIND for q in CHARGES for t in (0, 1) for h in (0, 1) for m in range(20) for s in (0, 1)]
    for _ in range(n_random):
        charges = rng.randrange(-5, 1 << 24) if rng.random() < 0.2 else rng.randrange(-2, 21)
        cases.append((rng.choice(list(KIND)), charges, rng.randrange(2), rng.randrange(2), rng.randrange(20), rng.randrange(2)))
    for i, case in enumerate(cases):
        print(i, *case)


class Oracle:
    def __init__(self):
        self.e = e = emu.Emu()
        self.cur, self.out = {}, {}
        cur, out = self.cur, self.out
        self.game = e.alloc(0x1000)
        self.player = player = e.alloc(0x200)
        inv, item, books, items_row = e.alloc(0x100), e.alloc(0x200), e.alloc(0x40), e.alloc(0x200)
        room, portal, client, path = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
        self.packet = packet = e.alloc(16)
        e.mu.mem_write(packet, bytes([0x20]) + (77).to_bytes(4, "little") + (100).to_bytes(4, "little") + (200).to_bytes(4, "little"))
        e.w32(player + 0x2c, path)
        e.w32(player, 0); e.w32(player + 0xc, 1); e.w32(player + 0x60, inv)
        e.w32(item, 4); e.w32(item + 4, 0x200); e.w32(item + 0xc, 77); e.w32(item + 0x10, 0)
        e.w32(books + 4, 2); e.w32(books + 8, 219); e.w32(books + 0xc, 220)   # pSpell 2, ScrollSkill, BookSkill
        e.w32(items_row + 0x94, 2)
        e.w32(portal, 2); e.w32(portal + 4, 0x3b); e.w32(portal + 0xc, 900)

        def count(name):
            return lambda e_: out.__setitem__(name, out.get(name, 0) + 1) or 0

        def stat(e_):
            if e_.arg(1) == 0x46: out["delta"] = out.get("delta", 0) + (e_.arg(2) ^ 0x80000000) - 0x80000000
            return 0

        def made(e_):
            out["made"] = out.get("made", 0) + 1
            return portal if cur["spot"] else 0

        for at, fn, args in (
                (0x55bf50, lambda e_: 0, 1), (0x639df0, lambda e_: 0, 2), (0x63ad50, lambda e_: 1, 2),
                (0x6488c0, lambda e_: 100, 1), (0x648900, lambda e_: 200, 1),
                (0x552f60, lambda e_: item if e_.mu.reg_read(UC_X86_REG_EDX) == 4 and e_.arg(0) == 77 else 0, 1),
                (0x63c1e0, lambda e_: cur["held"], 1), (0x628c20, lambda e_: 1, 1), (0x62b400, lambda e_: cur["kind"], 1),
                (0x629bb0, lambda e_: 1 if e_.arg(1) == cur["kind"] else 0, 2),
                (0x625480, lambda e_: cur["charges"] & 0xffffffff if e_.arg(1) == 0x46 else 0, 3),
                (0x554100, lambda e_: 0, 3), (0x627f80, lambda e_: 0x200, 2), (0x6374b0, lambda e_: books, 1),
                (0x6335f0, lambda e_: items_row, 1), (0x6280a0, lambda e_: 0, 4), (0x6280d0, lambda e_: 0, 3),
                (0x620bb0, lambda e_: room, 1), (0x61a1b0, lambda e_: 2, 1), (0x6427f0, lambda e_: 0, 1),
                (0x61ab70, lambda e_: 1, 1), (0x61ab00, lambda e_: cur["town"], 1), (0x535430, count("closed"), 0),
                (0x45ae20, lambda e_: 100, 4), (0x45adf0, lambda e_: 200, 1), (0x56d130, made, 7),
                (0x553380, lambda e_: 0, 1), (0x5353b0, lambda e_: 0, 0), (0x552af0, lambda e_: 0, 0),
                (0x58cf00, lambda e_: 0, 0), (0x553720, lambda e_: 0, 0), (0x5531c0, lambda e_: client, 1),
                (0x53b3d0, lambda e_: 0, 2), (0x53d130, lambda e_: 0, 3), (0x53c520, lambda e_: 0, 3),
                (0x6272b0, stat, 4), (0x55e0d0, lambda e_: 0, 1), (0x6439b0, lambda e_: 0, 1),
                (0x55e000, count("removed"), 1), (0x63b2c0, lambda e_: 0, 1), (0x543520, lambda e_: 0, 0)):
            e.hook(at, fn, args)

    def use(self, kind, charges, town, held, mode, spot):
        e, out = self.e, self.out
        self.cur.update(kind=KIND[kind], charges=charges, town=town, held=held, spot=spot)
        out.clear()
        e.w32(self.player + 0x10, mode)
        e.w32(self.player + 0xc4, 0)
        e.w32(self.game + 0x1dc4, 0)
        e.call(0x54d750, self.packet, 13, ecx=self.game, edx=self.player)
        opened = 0 if out.get("made") and spot else -1
        spent = int(out.get("delta") == -1) if kind == "tbk" else int(bool(out.get("removed")))
        return opened, spent, int(not out.get("closed"))


def game(path):
    oracle = Oracle()
    with open(path) as cases:
        for line in cases:
            f = line.split()
            print(f[0], *oracle.use(f[1], *map(int, f[2:7])))


if __name__ == "__main__":
    if sys.argv[1:2] == ["gen"]:
        gen(int(sys.argv[2]) if len(sys.argv) > 2 else 100000, int(sys.argv[3]) if len(sys.argv) > 3 else 7)
    elif sys.argv[1:2] == ["game"]:
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
