# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's load of a character's life, mana and stamina as the oracle.

    uv run python fl_load.py gen [N [SEED]] > cases.tsv
    uv run python fl_load.py game cases.tsv > game.tsv
    ../../build/tests/test_game --load-stats cases.tsv | grep -E '^[A-Za-z_]\\w* -?[0-9]+( -?[0-9]+){5}$' > d2d.tsv
    diff game.tsv d2d.tsv          # no output: every case agrees
    uv run python fl_load.py client cases.tsv   # what the client's copy of the same load shows

A case is `id life maxlife mana maxmana stamina maxstamina`, each in 256ths
(the file's own unit). `game` prints `id life mana stamina maxlife maxmana
maxstamina` (256ths) of the player unit after the load; with `--stages` each
case also prints a line `id D ...`, the stat vector straight after the stat
block's decode (before the restore). `client` prints the same vector as the
client's player unit holds it after the server's whole-point update packet.

What runs for real: FUN_0056b180, the d2s loader a join or a single-player
start calls for a save of version 0x5c and up (FUN_00534330 -> FUN_0056b180; the
join is FUN_00539760 -> FUN_005345a0 -> FUN_00534520 -> FUN_00534330), with the
real stat block decode FUN_0056a620 -> FUN_0056a4f0 (9-bit stat ids, the save
bits and param bits of the loaded ItemStatCost rows, the real FUN_00627260 /
FUN_006270b0 stat setter and FUN_00625480 / FUN_00624f60 getter on a real stat
list), the two reads of stats 6 and 8 after the decode (0x56b283, 0x56b28f) and
the restore FUN_0056af80 (stats 6, 8 set from them at 0x56b0ff / 0x56b10d,
stamina 0xa from the maximum 0xb at 0x56b01d). Hooked (not the claim): the
other parts of the file (the header, quest and waypoint blocks, skills, the item
list, the corpse list, the mercenary, FUN_0056a090 / 0056a370 / 0056a3e0 /
0056a470 / 0056a710 / 0056a7e0 / 0056a830 / 0056aa50 / 0056ac10 / 0056ae50),
the client's equipment slot table (FUN_005531c0, FUN_005390d0, FUN_00539220) and
the class record (FUN_006221a0, FUN_00611800). The unit's other stats are zero,
so no gear or level adds to the maxima: what the file says is what the unit gets.
"""
import random
import sys

import skill_tips

STAT = {"life": 6, "maxlife": 7, "mana": 8, "maxmana": 9, "stamina": 10, "maxstamina": 11}
ORDER = ("life", "maxlife", "mana", "maxmana", "stamina", "maxstamina")


class Bits:
    """LSB-first, the order of FUN_00411020 (and d2d::d2s::detail::Bits)."""
    def __init__(self): self.v, self.n = 0, 0
    def put(self, value, bits): self.v |= (value & ((1 << bits) - 1)) << self.n; self.n += bits
    def bytes(self): return self.v.to_bytes((self.n + 7) // 8, "little")


class Oracle:
    def __init__(self):
        self.e = e = skill_tips.boot()
        tables = e.r32(0x744304)
        self.isc, self.isc_count = e.r32(tables + 0xbcc), e.r32(tables + 0xbd4)
        self.unit = e.alloc(0x200)                         # dword 0 = 0: a player
        self.stat_list = e.alloc(0x100)
        e.w32(self.unit + 0x5C, self.stat_list)
        e.w32(self.stat_list + 0x10, 0x80000000)            # a base list (as freeze_rules.py builds one)
        self.pd = e.alloc(0x400)                            # the class record FUN_006221a0 returns: all zero
        self.slots = e.alloc(0x100)                         # FUN_005531c0's object
        self.buf = e.alloc(0x400)
        self.stage = {}
        # the unit's creation, headers, skills, items, merc: not the claim
        e.hook(0x56a090, self.make_unit, 3)
        for addr, n in ((0x56a370, 3), (0x56a3e0, 3), (0x56a710, 4), (0x56a7e0, 5), (0x56a830, 4), (0x56aa50, 2),
                        (0x56ac10, 5), (0x56ae50, 3)):
            e.hook(addr, lambda e: 0, n)
        e.hook(0x56a470, self.to_stat_block, 3)
        # FUN_0056af80's helpers: the equipment slot object and the class record
        e.hook(0x5531c0, lambda e: self.slots, 1)
        e.hook(0x5390d0, lambda e: e.w32(e.arg(2), 0) or 0, 3)
        e.hook(0x539220, lambda e: 0, 0)
        e.hook(0x6221a0, lambda e: self.pd, 1)
        e.hook(0x611800, lambda e: 0, 2)
        # stage D / R observation points: wrap the real getters' result is not needed; read the list after the call
        self.block = 0

    def make_unit(self, e):
        e.w32(e.arg(2), self.unit)
        return 0

    def to_stat_block(self, e):
        e.w32(e.arg(0), self.block)
        return 0

    def row(self, stat):
        r = self.isc + stat * 0x144
        return self.e.read(r + 0xa, 1)[0], self.e.read(r + 0xb, 1)[0]

    def block_for(self, vals):
        b = Bits()
        b.put(0x6667, 16)
        for name in ORDER:
            stat = STAT[name]
            bits, pbits = self.row(stat)
            b.put(stat, 9)
            if pbits: b.put(0, pbits)
            b.put(vals[name], bits)
        b.put(0x1ff, 9)
        return b.bytes() + b"\0" * 4

    def reset(self):
        e = self.e
        e.mu.mem_write(self.stat_list, bytes(0x100))
        e.w32(self.stat_list + 0x10, 0x80000000)

    def get(self, stat): return self.e.call(0x625480, self.unit, stat, 0) & 0xFFFFFFFF

    def vector(self): return [self.get(STAT[n]) for n in ("life", "mana", "stamina", "maxlife", "maxmana", "maxstamina")]

    def load(self, vals):
        e = self.e
        self.reset()
        data = self.block_for(vals)
        e.mu.mem_write(self.buf, bytes(0x400))
        e.w32(self.buf, 0xaa55aa55); e.w32(self.buf + 4, 0x60)       # magic, version
        self.block = self.buf + 0x100
        e.mu.mem_write(self.block, data)
        out = e.alloc(0x10)
        e.call(0x56b180, self.buf, 0x300, out, 0, 0, 0, ecx=0, edx=self.unit)
        return self.vector()

    def decode_only(self, vals):
        e = self.e
        self.reset()
        data = self.block_for(vals)
        e.mu.mem_write(self.buf + 0x100, data)
        cur = e.alloc(8)
        e.w32(cur, self.buf + 0x100)
        e.call(0x56a4f0, cur, self.buf + 0x300, 0, edx=self.unit)
        return self.vector()


def cases(path):
    for line in open(path):
        f = line.split()
        if f: yield f[0], dict(zip(ORDER, map(int, f[1:7])))


def gen(n=400, seed=1):
    r = random.Random(seed)
    rows = [  # id life maxlife mana maxmana stamina maxstamina
        ("freshe_life", 22921, 23296, 6244, 11520, 26112, 26112),     # 89.54 of 91, mana 24.39 of 45
        ("pally_life", 19907, 22528, 4992, 4992, 20224, 20224),       # 77.76 of 88, mana 19.5 of 19.5
        ("full", 23296, 23296, 11520, 11520, 26112, 26112),
        ("zero_life", 0, 23296, 11520, 11520, 26112, 26112),
        ("zero_all", 0, 23296, 0, 11520, 0, 26112),
        ("one_256th", 1, 23296, 1, 11520, 1, 26112),
        ("frac_ff", 22783 + 0, 23296, 6399, 11520, 100 * 256 + 255, 26112),
        ("frac_80", 89 * 256 + 128, 23296, 24 * 256 + 128, 11520, 50 * 256 + 128, 26112),
        ("above_max_life", 95 * 256, 23296, 11520, 11520, 26112, 26112),
        ("above_max_mana", 23296, 23296, 50 * 256, 11520, 26112, 26112),
        ("above_max_stamina", 23296, 23296, 11520, 11520, 120 * 256, 26112),
        ("stamina_half", 23296, 23296, 11520, 11520, 50 * 256, 102 * 256),
        ("stamina_zero", 23296, 23296, 11520, 11520, 0, 102 * 256),
        ("max_zero", 5000, 0, 5000, 0, 5000, 0),
    ]
    for i in range(n):
        ml, mm, ms = r.randrange(256, 1 << 20), r.randrange(0, 1 << 18), r.randrange(256, 1 << 18)
        pick = lambda m: r.choice((0, m, r.randrange(0, m + 1), r.randrange(0, m + 1) & ~0xff, min(m + r.randrange(1, 4000), (1 << 21) - 1)))
        rows.append((f"r{i}", pick(ml), ml, pick(mm), mm, pick(ms), ms))
    for row in rows: print(*row)


def client_view(o, life, mana, stamina):
    """The client's side of the same numbers: the server sends stats 6, 8, 0xa as whole points (FUN_00548760:
    `sar 8` at 0x5487b7 for life), the client's 0x18 handler FUN_0045d9b0 sets `value << 8` (0x45d9d8 .. 0x45d9dc)
    on its player unit. Returns the client unit's (life, mana, stamina) after the handler ran on that packet."""
    e = o.e
    e.hook(0x463de0, lambda e: o.unit, 0)
    b = Bits()
    b.put(0x18, 8)
    for v in (life >> 8, mana >> 8, stamina >> 8): b.put(v, 15)
    b.put(0, 7); b.put(0, 7); b.put(0x1000, 16); b.put(0x1000, 16); b.put(0, 8); b.put(0, 8)
    pkt = e.put(b.bytes() + b"\0" * 8)
    o.reset()
    e.call(0x45d9b0, ecx=pkt)
    return [o.get(6), o.get(8), o.get(10)]


def main():
    a = sys.argv[1:]
    if a and a[0] == "gen":
        if len(a) > 3 or not all(arg.lstrip("-").isdigit() for arg in a[1:]): sys.exit(__doc__)
        return gen(*map(int, a[1:]))
    stages = "--stages" in a
    a = [x for x in a if x != "--stages"]
    if len(a) < 2 or a[0] not in ("game", "client"): sys.exit(__doc__)
    o = Oracle()
    for cid, vals in cases(a[1]):
        if a[0] == "client":                      # the client's copy of what the server then holds
            server = o.load(vals)
            print(cid, *client_view(o, server[0], server[1], server[2]))
            continue
        if stages:
            print(cid, "D", *o.decode_only(vals))
        print(cid, *o.load(vals))


if __name__ == "__main__":
    main()
