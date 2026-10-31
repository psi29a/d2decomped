# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's vendor price (FUN_0062efb0: buy 0, sell 1, repair 3) as the oracle.

    uv run python price_oracle.py gen 100000 7 > cases.tsv
    uv run python price_oracle.py game cases.tsv > game.tsv
    ../../build/tests/test_game --price cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv
    diff game.tsv d2d.tsv

FUN_0062efb0 and everything it calls run natively on the loaded 1.14d
tables. Hooked: the stat getters (FUN_00625480 total, FUN_006253b0 base,
FUN_00625e00 max durability, FUN_006261d0 one stat's layers, FUN_00625c30
the whole list) answer from the case, so an item has no stat list beyond
the case's durability / quantity / defence (no charges, no staff mods, no
magic stats); FUN_0065c310 (quest flags) answers 0. The item is a fake unit
{type 4, class, data +0x14, inventory +0x60 = 0 (no sockets)} with item data
{+0 quality, +0x18 flags (0x10 identified, 0x400000 ethereal), +0x28
unique / set row, +0x38 prefixes, +0x3e suffixes}. Affix ids: game.exe's
are combined-table ids (FUN_00633ee0); the case carries d2d's MagicPrefix /
MagicSuffix rows (d2d's prefix_cost index p is game.exe's record
prefixes + (p - 1) * 0x90: `map` and test_game --price-tables line them up).

A case is `id code quality p0 s0 p1 s1 p2 s2 uid identified ethereal dur
maxdur qty ac npc diff`; `game` prints `id buy sell repair`.
"""
import random
import struct
import sys

import skill_tips

STAT_QTY, STAT_DUR, STAT_MAXDUR, STAT_AC, STAT_CLVL, STAT_REDUCED = 0x46, 0x48, 0x49, 0x1f, 0xc, 0x57
VENDORS = ["Akara", "Gheed", "Charsi", "Fara", "Lysander", "Drognan", "Elzix", "Hratli", "Alkor", "Ormus",
           "Asheara", "Halbu", "Jamella", "Malah", "Larzuk", "Drehya", "Nihlathak"]


def txt(e, name):
    raw = e.mpq_file(f"data\\global\\excel\\{name}.txt").decode("latin1").split("\r\n")
    head = raw[0].split("\t")
    return [dict(zip(head, line.split("\t"))) for line in raw[1:] if line]


class Oracle:
    def __init__(self):
        self.e = e = skill_tips.boot()
        self.items = []
        for c in range(e.r32(0x96ca58)):
            rec = e.call(0x6335f0, c)
            code = e.read(rec + 0x80, 4).decode("latin1").strip("\0 ")
            self.items.append((code, rec))
        self.cls = {}
        for c, (code, _) in enumerate(self.items): self.cls.setdefault(code, c)
        mon = txt(e, "monstats")
        self.hc = {}
        for row in mon:
            if row.get("Id") and row.get("hcIdx", "").lstrip("-").isdigit(): self.hc[row["Id"].lower()] = int(row["hcIdx"])
        self.npc_ids = [r["npc"] for r in txt(e, "npc") if r.get("npc")]
        aff = e.call(0x633ed0)
        rows, self.suffixes, self.prefixes = e.r32(aff + 4), e.r32(aff + 8), e.r32(aff + 12)   # {count, rows, suffixes, prefixes, automagic}
        self.pre0 = (self.prefixes - rows) // 0x90       # combined id of MagicPrefix row r: pre0 + r + 1
        self.suf0 = (self.suffixes - rows) // 0x90
        self.npre = (e.r32(aff + 16) - self.prefixes) // 0x90
        self.nsuf = (self.prefixes - self.suffixes) // 0x90
        self.nuniq = e.r32(e.r32(0x744304) + 0xc28)
        self.nset = e.r32(e.r32(0x744304) + 0xc1c)
        self.player, self.item, self.data = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
        e.w32(self.item, 4)
        e.w32(self.item + 0x14, self.data)
        self.quests = e.alloc(0x100)
        self.pstats, self.istats = {}, {}

        def stats(u):
            return self.pstats if u == self.player else self.istats if u == self.item else {}
        e.hook(0x625480, lambda e: stats(e.arg(0)).get(e.arg(1), 0) & 0xFFFFFFFF, 3)
        e.hook(0x6253b0, lambda e: stats(e.arg(0)).get(e.arg(1), 0) & 0xFFFFFFFF, 3)
        e.hook(0x625e00, lambda e: stats(e.arg(0)).get(STAT_MAXDUR, 0), 1)
        e.hook(0x6261d0, lambda e: 0, 4)
        e.hook(0x625c30, lambda e: 0, 3)
        e.hook(0x65c310, lambda e: 0, 3)

    def price(self, case, mode):
        e = self.e
        (_, code, quality, p0, s0, p1, s1, p2, s2, uid, ident, eth, dur, maxdur, qty, ac, npc, diff) = case
        mem = bytearray(0x100)
        struct.pack_into("<I", mem, 0, quality)
        struct.pack_into("<I", mem, 0x18, (0x10 if ident else 0) | (0x400000 if eth else 0))
        struct.pack_into("<i", mem, 0x28, uid)
        for i, (p, s) in enumerate(((p0, s0), (p1, s1), (p2, s2))):
            struct.pack_into("<H", mem, 0x38 + 2 * i, self.pre0 + p if p else 0)
            struct.pack_into("<H", mem, 0x3e + 2 * i, self.suf0 + s if s else 0)
        e.mu.mem_write(self.data, bytes(mem))
        e.w32(self.item + 4, self.cls[code])
        self.istats = {STAT_DUR: dur, STAT_MAXDUR: maxdur, STAT_QTY: qty, STAT_AC: ac}
        self.pstats = {STAT_CLVL: 1, STAT_REDUCED: 0}
        r = e.call(0x62efb0, diff, self.quests, self.hc[npc.lower()], mode, ecx=self.player, edx=self.item)
        return r - (1 << 32) if r & 0x80000000 else r   # signed

    def gen(self, n, seed):
        rnd = random.Random(seed)
        probe = self.e.alloc(0x20)
        self.e.w32(probe, 4)
        def body(code):                                   # ItemTypes 0x28 (body parts): the price reads the monster row in +0x28
            self.e.w32(probe + 4, self.cls[code])
            return self.e.call(0x629bb0, probe, 0x28)
        pool = [(code, rec) for code, rec in self.items if code in self.cls and not body(code)]
        pool = [(code, rec) for code, rec in pool if self.e.s32(rec + 0xe0) > 0 and code in self.cls and code not in ("tbk", "ibk")]   # a tome's book row is its suffix word (PR #127)
        out = []
        for i in range(n):
            code, rec = rnd.choice(pool)
            c = self.cls[code]
            e = self.e
            quality = rnd.choice([1, 2, 2, 3, 4, 4, 5, 6, 7, 8])
            aff = [0] * 6
            uid = -1
            if quality == 4:
                aff[0], aff[1] = rnd.randrange(self.npre) * rnd.randrange(2), rnd.randrange(self.nsuf) * rnd.randrange(2)
            elif quality in (6, 8):
                aff = [rnd.randrange(self.npre if k % 2 == 0 else self.nsuf) * rnd.randrange(2) for k in range(6)]
            elif quality == 5: uid = rnd.randrange(self.nset)
            elif quality == 7: uid = rnd.randrange(self.nuniq)
            stack = e.read(rec + 0x132, 1)[0]
            maxstack = e.call(0x629570, c) if stack else 0
            qty = rnd.randint(1, max(maxstack, 1)) if stack else 0
            basedur = e.read(rec + 0x112, 1)[0] if not e.read(rec + 0x113, 1)[0] else 0
            maxdur = basedur if rnd.random() < 0.7 else rnd.randint(0, 255)
            dur = rnd.randint(0, maxdur) if rnd.random() < 0.8 else maxdur
            lo, hi = e.s32(rec + 0xcc), e.s32(rec + 0xd0)
            ac = rnd.randint(min(lo, hi), max(lo, hi)) if hi > 0 else 0
            npc = rnd.choice(VENDORS).lower()   # npc.txt keys (d2d looks them up as written)
            out.append(f"{i} {code} {quality} {' '.join(map(str, aff))} {uid} {rnd.randrange(2) if rnd.random() < 0.3 else 1} "
                       f"{int(rnd.random() < 0.15)} {dur} {maxdur} {qty} {ac} {npc} {rnd.randrange(3)}")
        return out


def parse(line):
    f = line.split()
    return [f[0], f[1]] + [int(x) for x in f[2:16]] + [f[16], int(f[17])]


def main():
    if sys.argv[1] == "gen":
        print("\n".join(Oracle().gen(int(sys.argv[2]), int(sys.argv[3]))))
    elif sys.argv[1] == "game":
        o = Oracle()
        for line in open(sys.argv[2]):
            if not line.strip(): continue
            case = parse(line)
            print(case[0], o.price(case, 0), o.price(case, 1), o.price(case, 3))
    elif sys.argv[1] == "map":
        o = Oracle()
        e = o.e
        pre = [f"{e.s32(o.prefixes + r * 0x90 + 0x88)},{e.s32(o.prefixes + r * 0x90 + 0x8c)}" for r in range(o.npre - 1)]
        suf = [f"{e.s32(o.suffixes + r * 0x90 + 0x88)},{e.s32(o.suffixes + r * 0x90 + 0x8c)}" for r in range(o.nsuf - 1)]
        print(" ".join(pre)); print(" ".join(suf))
        print("pre0", o.pre0, "suf0", o.suf0, "npre", o.npre, "nsuf", o.nsuf, "vendors", [v for v in VENDORS if v.lower() not in o.hc], o.npc_ids)


if __name__ == "__main__":
    main()
