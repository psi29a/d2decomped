# SPDX-License-Identifier: GPL-3.0-or-later
"""The vendor price as read off FUN_0062efb0, in Python, checked against game.exe.

    uv run python price_model.py cases.tsv game.tsv     # prints mismatches, then 'model N/N'

Reads the tables through game.exe (items, ItemTypes, npc, unique / set
rows) and prices each case of price_oracle.py the way docs/research/
price-trace.md writes the formula down; a clean run is the proof that
the written formula is game.exe's for the oracle's case space (no stat
list: no magic stats, staff mods, charges or sockets; no quests; no
reduced prices).
"""
import sys

import price_oracle


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def mul(base, m, test=None):
    """base x m / 1024 as FUN_0062efb0 does it: (base / 1024) x m above 0xffff."""
    if base > 0xffff and (m if test is None else test):
        return cdiv(base, 1024) * m
    return cdiv(m * base, 1024)


class Model:
    def __init__(self, o):
        self.o, e = o, o.e
        t = e.r32(0x744304)
        self.types = e.r32(t + 0xbf8)
        self.item = e.alloc(0x20)
        e.w32(self.item, 4)
        self.npc = {}
        npcs, n = e.r32(0x96c5d8), e.r32(0x96c5dc)
        for i in range(n):
            r = npcs + i * 0x4c
            self.npc[e.s32(r)] = [e.s32(r + k) for k in range(0, 0x4c, 4)]
        self.kinds = {}
        self.off = set()   # price_terms.py: game.exe terms switched to d2d's behaviour

    def kind(self, code):
        if code in self.kinds: return self.kinds[code]
        o, e = self.o, self.o.e
        c = o.cls[code]
        e.w32(self.item + 4, c)
        rec = o.items[c][1]
        ty = e.call(0x628700, c)
        tr = self.types + ty * 0xe4
        k = dict(cost=e.s32(rec + 0xe0), minac=e.s32(rec + 0xcc), maxac=e.s32(rec + 0xd0),
                 armor=bool(e.call(0x629bb0, self.item, 0x32)), book=bool(e.call(0x629bb0, self.item, 0x12)),
                 cls=e.read(tr + 0x21, 1)[0] < 7, stack=bool(e.read(rec + 0x132, 1)[0]), throw=bool(e.read(tr + 0x10, 1)[0]),
                 quiver=e.s16(tr + 0xe) if hasattr(e, "s16") else ((e.r16(tr + 0xe) ^ 0x8000) - 0x8000),
                 repair=bool(e.read(tr + 8, 1)[0]), nodur=bool(e.read(rec + 0x113, 1)[0]), durcol=e.read(rec + 0x112, 1)[0],
                 maxstack=e.call(0x629570, c))
        self.kinds[code] = k
        return k

    def unique(self, uid):
        e = self.o.e
        t = e.r32(0x744304)
        if uid < 0 or uid >= e.r32(t + 0xc28): return None
        r = e.r32(t + 0xc24) + uid * 0x14c
        return e.s32(r + 0x7c), e.s32(r + 0x80)

    def set(self, uid):
        e = self.o.e
        t = e.r32(0x744304)
        if uid < 0 or uid >= e.r32(t + 0xc1c): return None
        r = e.r32(t + 0xc18) + uid * 0x1b8
        return e.s32(r + 0x38), e.s32(r + 0x3c)

    def price(self, case):
        (_, code, q, p0, s0, p1, s1, p2, s2, uid, ident, eth, dur, maxdur, qty, ac, npc, diff) = case
        k = self.kind(code)
        cost, count, div = k["cost"], max(qty, 1), 1
        hasdur = not k["nodur"] and k["durcol"] > 0 and maxdur != 0
        if "nodur" in self.off:   # d2d: the saved max durability alone, any ItemTypes
            hasdur = maxdur > 0
        b = s = r = cost
        if k["quiver"]:
            b = s = cdiv(cost * count, 1024)
            r = cdiv(k["maxstack"] * cost, 1024)
        elif k["stack"] and k["maxstack"] > 1:
            div = k["maxstack"]
        if k["armor"] and "armor" not in self.off and k["maxac"] - k["minac"] + 1 != 0 and k["maxac"] != 0:
            b = s = r = cdiv(cost * ac, k["maxac"])
        if ident or "unid" in self.off:
            xb = xs = xr = 0
            row = self.set(uid) if q == 5 else self.unique(uid) if q == 7 else None
            affs = [(case[3], self.o.prefixes), (case[4], self.o.suffixes)] if q in (4, 7) and not row else \
                [(case[3 + i], self.o.prefixes if i % 2 == 0 else self.o.suffixes) for i in range(6)] if q in (6, 8) else []
            for a, table in affs:
                if not a: continue
                rec = table + (a - 1) * 0x90
                m_, a_ = self.o.e.s32(rec + 0x88), self.o.e.s32(rec + 0x8c)
                big = b > 0xffff and m_
                xb += (cdiv(b, 1024) * m_ if big else cdiv(m_ * b, 1024)) + a_
                xs += (cdiv(s, 1024) * m_ if big else cdiv(m_ * s, 1024)) + a_
                xr += (cdiv(r, 1024) * m_ if big else cdiv(m_ * r, 1024)) + a_
            if q == 1:
                xb, xs, xr = -cdiv(b, 2), -cdiv(s, 2), -cdiv(r, 2)
            elif row:
                m, a = row
                xb, xs, xr = mul(b, m, b > 0xffff) + a, (cdiv(s, 1024) * m if b > 0xffff and m else cdiv(m * s, 1024)) + a, (cdiv(r, 1024) * m if b > 0xffff and m else cdiv(m * r, 1024)) + a
            b, s, r = b + cdiv(xb, div), s + cdiv(xs, div), r + cdiv(xr, div)
        if eth: s = cdiv(s, 4)
        if k["cls"] and "class" not in self.off: s = cdiv(s, 4)
        if eth and hasdur and dur <= 0 and "ethzero" not in self.off: s_zero = True
        else: s_zero = False
        if not k["quiver"] and not k["throw"] and hasdur:
            r = cdiv((maxdur - dur) * r, maxdur) if dur < maxdur else 0
        row = self.npc[self.o.hc[npc]]
        bm, sm, rm = row[1], row[2], row[3]
        b2 = mul(b, bm)
        s2 = 0 if s_zero else (cdiv(s, 1024) * sm if s > 0xffff and bm else cdiv(sm * s, 1024))
        r2 = mul(r, rm)
        if "bigmul" in self.off:   # d2d: x mult / 1024 without dividing first above 0xffff
            b2, s2, r2 = cdiv(bm * b, 1024), 0 if s_zero else cdiv(sm * s, 1024), cdiv(rm * r, 1024)
        b, s, r = b2, s2, r2
        repairable = (ident or "unidrep" in self.off) and not eth and (k["repair"] or "nodur" in self.off) and ((k["throw"] and k["stack"]) or hasdur)
        if not k["book"] and not k["quiver"]:
            b *= count
            if k["stack"] and repairable and "stack" not in self.off:
                ms = k["maxstack"]
                if count < ms:
                    r = r * (ms - count); s = ms * s - r
                else:
                    s = ms * s; r = 0
            else:
                s *= count
        s = min(s, row[16 + diff])
        return max(b, 1), max(s, 1), ((r if "whole" in self.off and r <= 0 else max(r, 1)) if repairable else 0)


def main():
    o = price_oracle.Oracle()
    m = Model(o)
    game = {l.split()[0]: l.split()[1:] for l in open(sys.argv[2]) if l.strip()}
    n = bad = 0
    for line in open(sys.argv[1]):
        if not line.strip(): continue
        case = price_oracle.parse(line)
        got = [str(v) for v in m.price(case)]
        n += 1
        if got != game[case[0]]:
            bad += 1
            if bad <= 15: print("MISMATCH", line.strip(), "game", game[case[0]], "model", got, m.kind(case[1]))
    print(f"model {n - bad}/{n}")


if __name__ == "__main__":
    main()
