# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's load clamp of a character's gold and stash gold as the oracle.

    uv run python fl_gold.py gen > cases.tsv
    uv run python fl_gold.py game cases.tsv > game.tsv
    ../../build/tests/test_game --load-gold cases.tsv | grep -E '^[A-Za-z_][A-Za-z_0-9]* -?[0-9]+( -?[0-9]+){1}$' > d2d.tsv
    diff game.tsv d2d.tsv
    uv run python fl_gold.py limits       # FUN_00622e70 / FUN_00623460 for clvl 1..99

A case is `id level gold bank`; `game` prints `id gold bank` after the real FUN_0056b180 -> FUN_0056af80
(gold and bank clamped at 0x56afb4..0x56b00f through FUN_00622e70 / FUN_00623460, run natively on a real
stat list). Same hooks as fl_load.py (the other file parts are not the claim); lives next to it.
"""
import sys

import fl_load

STATS = {"level": 12, "gold": 14, "bank": 15}


class GoldOracle(fl_load.Oracle):
    def block_for(self, vals):
        b = fl_load.Bits()
        b.put(0x6667, 16)
        for name, stat in STATS.items():
            bits, pbits = self.row(stat)
            b.put(stat, 9)
            if pbits: b.put(0, pbits)
            b.put(vals[name], bits)
        # life, mana and stamina maxima present so the restore sees a normal unit
        for stat in (6, 7, 8, 9, 10, 11):
            bits, pbits = self.row(stat)
            b.put(stat, 9)
            if pbits: b.put(0, pbits)
            b.put(256 * 50, bits)
        b.put(0x1ff, 9)
        return b.bytes() + b"\0" * 4

    def gold(self, vals):
        self.load_raw(vals)
        return self.get(14), self.get(15)

    def load_raw(self, vals):
        e = self.e
        self.reset()
        data = self.block_for(vals)
        e.mu.mem_write(self.buf, bytes(0x400))
        e.w32(self.buf, 0xaa55aa55); e.w32(self.buf + 4, 0x60)
        self.block = self.buf + 0x100
        e.mu.mem_write(self.block, data)
        out = e.alloc(0x10)
        e.call(0x56b180, self.buf, 0x300, out, 0, 0, 0, ecx=0, edx=self.unit)


def gen():
    rows = []
    for lv in (1, 2, 3, 4, 9, 10, 25, 50, 75, 98, 99):
        cap, stash = lv * 10000, 2500000
        for g in (0, 1, cap - 1, cap, cap + 1, 2 * cap, (1 << 25) - 1):
            for b in (0, 1, stash - 1, stash, stash + 1, 2 * stash, (1 << 25) - 1):
                rows.append((f"l{lv}g{g}b{b}", lv, g, b))
    for row in rows: print(*row)


def main():
    a = sys.argv[1:]
    if not a: sys.exit(__doc__)
    if a[0] == "gen": return gen()
    o = GoldOracle()
    if a[0] == "limits":
        e = o.e
        for lv in range(1, 100):
            o.reset()
            e.call(0x627260, o.unit, 12, lv, 0)
            purse = e.call(0x622e70, o.unit) & 0xffffffff
            bank = e.call(0x623460, o.unit) & 0xffffffff
            print(lv, purse, bank, "OK" if purse == lv * 10000 and bank == 2500000 else "DIFF")
        return
    if a[0] == "game":
        for line in open(a[1]):
            f = line.split()
            if not f: continue
            print(f[0], *o.gold(dict(level=int(f[1]), gold=int(f[2]), bank=int(f[3]))))


if __name__ == "__main__":
    main()
