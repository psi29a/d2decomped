#!/usr/bin/env python3
"""MISSILEDIFF: a monster's missile record, game.exe's real code against the REAL d2d build (card missilediff; extends mm_launch.py).

Copy this file next to mm_launch.py in a checkout of upstream tools/emu (mm_diff_run.sh does) and run on the worker host:

    mm_diff.py game TABLES_DIR N SEED OUTDIR      game.exe side: writes OUTDIR/cases.txt and OUTDIR/game.tsv
    mm_diff.py cmp  OUTDIR D2D_OUTPUT             compares game.tsv with `test_monster_missile --monster-missile cases.txt`

Cases: every Act 1 / 2 shooter row whose source is MissA1 / MissA2 / MissS1 / MissC (the path ai.cpp:2014 area builds inline; the srvmissilea
rows of a monster's skills go through andariel_missile and are not on it), for each difficulty (a fresh game: the game's difficulty byte),
each missile level 1..40, owner damage pairs (boundaries and 32-bit masked xorshift32 draws). game.exe: the real monster is spawned
(FUN_00555230), its mode stat overlay set (FUN_005a4f50), its stats 0x15 / 0x16 (the owner's damage in points) REWRITTEN in its stat list to the
case's pair, the missile made by FUN_0059fa30 at the case's level, the missile's stats read with FUN_00625480: physical 0x15 / 0x16 (256ths),
elements 0x30..0x3b. d2d: the real monster_shot_phys / monster_shot_stats of components/game/ai.hpp, built into test_monster_missile.

Owner elements: a shooter whose own El row fires in the shot's mode adds its element to the game's record; those cases compare the physical
part only (elements: skipped, counted). A chance El (Pct 1..99) the same.
"""
import os
import sys

import mm_launch
from mm_launch import G, MODE, num, rows

M32 = 0xFFFFFFFF
ELEM = (0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b)
# game stat pairs -> d2d etype (Skill::etype order: fire 0, lightning 1, cold 2, poison 3, magic 4)
PAIRS = ((0x30, 0x31, 0, None), (0x32, 0x33, 1, None), (0x36, 0x37, 2, 0x38), (0x39, 0x3a, 3, 0x3b), (0x34, 0x35, 4, None))


class Xs:
    """xorshift32, a 32-bit masked seed (the shared generator of the earlier differentials)."""
    def __init__(self, seed): self.s = (seed or 1) & M32
    def next(self):
        x = self.s
        x ^= (x << 13) & M32; x ^= x >> 17; x ^= (x << 5) & M32
        self.s = x & M32
        return self.s
    def below(self, n): return self.next() % n


def sources(d):
    mons = {m["Id"]: m for m in rows(d, "monstats.txt")}
    mis = {m["Missile"]: m for m in rows(d, "missiles.txt")}
    lv = rows(d, "levels.txt")
    ids = set()
    for l in lv:
        if num(l.get("Act", "")) in (0, 1) and l.get("Name"):
            for k in range(1, 11):
                for p in ("mon", "nmon", "umon"):
                    if l.get(f"{p}{k}", "") in mons: ids.add(l[f"{p}{k}"])
    ids |= {b for b in ("andariel", "bloodraven", "radament", "summoner") if b in mons}
    ids |= {b for b in ("fetishblow1", "fetishblow2", "fetishblow3", "fetishblow4", "regurgitator1", "regurgitator2", "regurgitator3",
                        "maggotqueen1", "maggotqueen2", "maggotqueen3") if b in mons}
    todo = []
    for mid in sorted(ids, key=lambda i: int(mons[i]["hcIdx"])):
        m = mons[mid]
        for c, mode in (("MissA1", "A1"), ("MissA2", "A2"), ("MissS1", "S1"), ("MissC", "SC")):
            if m.get(c) in mis: todo.append((mid, mode, c, m[c]))
    return mons, mis, todo


def setstat(e, u, stat, value):
    """Set one stat of the unit's list as the getter FUN_00625480 finds it: in place when the entry exists, else inserted in key order
    (entries are 8 bytes {key = stat << 16 | layer, value}, sorted; head+4 holds count (word) and capacity (word)). Stats 0x15 / 0x16 are
    held as the getter returns them (points). The list grows when full."""
    lst = e.r32(u + 0x5c)
    head = lst + (0x48 if e.r32(lst + 0x10) & 0x80000000 else 0x24)
    arr = e.r32(head)
    count, cap = e.r16(head + 4), e.r16(head + 6)
    key = stat << 16
    pos = 0
    while pos < count and e.r32(arr + 8 * pos) < key: pos += 1
    if pos < count and e.r32(arr + 8 * pos) == key:
        e.w32(arr + 8 * pos + 4, value & M32)
        return True
    if count >= cap:                      # grow it: a new 64-entry array, the old one left behind
        new = e.alloc(8 * 64)
        for i in range(count * 2): e.w32(new + 4 * i, e.r32(arr + 4 * i))
        e.w32(head, new); e.mu.mem_write(head + 6, (64).to_bytes(2, "little")); arr = new
    for i in range(count, pos, -1):
        e.w32(arr + 8 * i, e.r32(arr + 8 * (i - 1))); e.w32(arr + 8 * i + 4, e.r32(arr + 8 * (i - 1) + 4))
    e.w32(arr + 8 * pos, key); e.w32(arr + 8 * pos + 4, value & M32)
    e.mu.mem_write(head + 4, int(count + 1).to_bytes(2, "little"))
    return True


def owner_pairs(rng):
    """Owner damage pairs in points: boundaries of the 128ths fraction and of 256 / 1000, then masked draws."""
    out = [(0, 0), (1, 1), (1, 2), (3, 5), (127, 127), (128, 129), (255, 256), (1000, 1500)]
    for _ in range(3):
        a = rng.below(700)
        out.append((a, a + rng.below(300)))
    return out


def game(d, n_cases, seed, outdir):
    mons, mis, todo = sources(d)
    os.makedirs(outdir, exist_ok=True)
    rng = Xs(seed)
    cf = open(os.path.join(outdir, "cases.txt"), "w")
    gf = open(os.path.join(outdir, "game.tsv"), "w")
    case = 0
    skipped_nostat = 0
    per_row = {}
    for diff in (0, 1, 2):
        g = G(diff)
        e = g.e
        sfx = ("", "(N)", "(H)")[diff]
        units = {}
        for n, (mid, mode, src, mn) in enumerate(todo):
            m = mons[mid]
            u, x, y = g.unit(int(m["hcIdx"]), n)
            if not u:
                print(f"spawn failed {mid}"); continue
            e.call(0x5a4f50, ecx=u, edx=MODE[mode])
            units[n] = (u, x, y)
        # the grid (every source x levels 1..40 x owner pairs), then random cases up to the budget
        grid = [(n, lvl, p) for n in sorted(units) for lvl in range(1, 41) for p in owner_pairs(rng)]
        budget = n_cases // 3
        while len(grid) < budget:
            n = sorted(units)[rng.below(len(units))]
            a = rng.below(1500)
            grid.append((n, 1 + rng.below(60), (a, a + rng.below(500))))
        for i in range(len(grid) - 1, 0, -1):          # a Fisher-Yates shuffle on the seed: a short budget still touches every row
            j = rng.below(i + 1); grid[i], grid[j] = grid[j], grid[i]
        grid = grid[:budget]
        for n, lvl, (omin, omax) in grid:
            mid, mode, src, mn = todo[n]
            m, r = mons[mid], mis[mn]
            u, x, y = units[n]
            if not (setstat(e, u, 0x15, omin) and setstat(e, u, 0x16, omax)):
                skipped_nostat += 1
                if skipped_nostat <= 3:
                    lst = e.r32(u + 0x5c); hd = lst + (0x48 if e.r32(lst + 0x10) & 0x80000000 else 0x24)
                    print(f"  list full: diff {diff} {mid} {mode} count {e.r16(hd + 4)} cap {e.r16(hd + 6)}")
                continue
            got = (e.call(0x625480, u, 0x15, 0), e.call(0x625480, u, 0x16, 0))
            assert got == (omin, omax), (mid, mode, omin, omax, got)
            p = e.alloc(0x60)
            for i, v in {0: 0x21, 1: u, 4: int(r["Id"]), 5: x, 6: y, 7: x + 8, 8: y + 3, 0xc: lvl}.items(): e.w32(p + 4 * i, v)
            mu = e.call(0x59fa30, ecx=g.g, edx=p)
            if not mu:
                print(f"no missile {mid} {mn}"); continue
            st = {s: e.call(0x625480, mu, s, 0) for s in ELEM}
            st = {s: (v - (1 << 32) if v & 0x80000000 else v) for s, v in st.items()}
            pmin, pmax = e.call(0x625480, mu, 0x15, 0), e.call(0x625480, mu, 0x16, 0)
            # owner elements in the shot's mode (they ride on the missile too): the element comparison is skipped for them
            ownel = 0
            for k in (1, 2, 3):
                if m.get(f"El{k}Mode", "") == mode and m.get(f"El{k}Type", "") not in ("", "none") and num(m.get(f"El{k}Pct{sfx}", "")) > 0: ownel = 1
            first, cast, s1 = int(mode == "A1"), int(mode == "SC"), int(mode == "S1")
            cf.write(f"{case} {mn} {first} {cast} {s1} {lvl} {omin} {omax} {omin} {omax} {omin} {omax}\n")
            gf.write("\t".join(str(v) for v in (case, diff, mid, mode, mn, lvl, omin, omax, ownel, pmin, pmax, *[st[s] for s in ELEM])) + "\n")
            per_row[mn] = per_row.get(mn, 0) + 1
            case += 1
        print(f"difficulty {diff}: cases so far {case}")
    cf.close(); gf.close()
    print(f"cases {case} rows {len(per_row)} skipped (no owner stat entry) {skipped_nostat}")


def cmp(outdir, d2d_out):
    gl = [l.rstrip("\n").split("\t") for l in open(os.path.join(outdir, "game.tsv"))]
    dl = {}
    for l in open(d2d_out):
        f = l.split()
        if len(f) >= 3 and f[0].isdigit(): dl[int(f[0])] = [int(v) for v in f[1:]]
    tot = len(gl)
    bad = {"phys": 0, "elem": 0, "any": 0}
    elem_cmp = 0
    by_row, by_cause = {}, {}
    ex = []
    missing = 0
    real = [0, 0]                                  # cases at the level the game really uses (1 / 4 / 8 by difficulty): total, differ
    for g in gl:
        cid = int(g[0]); diff, mid, mode, mn, lvl, omin, omax, ownel = int(g[1]), g[2], g[3], g[4], int(g[5]), int(g[6]), int(g[7]), int(g[8])
        pmin, pmax = int(g[9]), int(g[10])
        st = {s: int(v) for s, v in zip(ELEM, g[11:])}
        d = dl.get(cid)
        if d is None: missing += 1; continue
        dphys = (d[0], d[1])
        delems = {}
        for i in range(2, len(d), 4): delems[d[i]] = tuple(d[i + 1:i + 4])
        weapon = mid == "bloodraven"            # her record carries her weapon's random damage (D5 of the trace): not compared
        bad_phys = dphys != (pmin, pmax) and not weapon
        ownel = ownel or weapon
        bad_el = False
        if not ownel:
            elem_cmp += 1
            gel = {}
            for lo, hi, et, ln in PAIRS:
                vlo, vhi = st[lo], st[hi]
                if not (vlo or vhi): continue
                length = st[ln] if ln else 0
                if et == 3: gel[et] = ((vlo * length) >> 8, (vhi * length) >> 8, length)
                elif et == 2: gel[et] = (vlo >> 8, vhi >> 8, length)
                else: gel[et] = (vlo >> 8, vhi >> 8, 0)
            dd = {k: (v[0], v[1], v[2] if k in (2, 3) else 0) for k, v in delems.items()}
            bad_el = gel != dd
            if bad_el: bad["elem"] += 1
        if bad_phys: bad["phys"] += 1
        if lvl == (1, 4, 8)[diff]:
            real[0] += 1
            real[1] += bad_phys or bad_el
        if bad_phys or bad_el:
            bad["any"] += 1
            by_row[mn] = by_row.get(mn, 0) + 1
            cause = []
            if bad_phys: cause.append("phys")
            if bad_el: cause.append("elem")
            by_cause["+".join(cause)] = by_cause.get("+".join(cause), 0) + 1
            if len(ex) < 12: ex.append(f"  case {cid} diff {diff} {mid} {mode} {mn} lvl {lvl} owner {omin}-{omax}: game phys {pmin}-{pmax} d2d {dphys[0]}-{dphys[1]}; game elems {gel if not ownel else 'skipped'} d2d {delems}")
    print(f"cases {tot}: d2d lines missing {missing}; physical differ {bad['phys']}; element compared {elem_cmp} differ {bad['elem']}; any differ {bad['any']} {by_cause}")
    print(f"at the game's own missile level (1 / 4 / 8): {real[0]} cases, {real[1]} differ")
    print("rows with a difference: " + " ".join(f"{k}:{v}" for k, v in sorted(by_row.items())))
    print("\n".join(ex))
    tab = os.environ.get("MMTAB", os.path.expanduser("~/mmtab"))
    mis = {m["Missile"]: m for m in rows(tab, "missiles.txt")}
    print("row data of the rows with a difference (D1 = MinLevDam / MaxLevDam, D2 = HitShift, D3 = EType): row | MinDamage MaxDamage | MinLevDam1 MaxLevDam1 | HitShift | SrcDamage | EType EMin Emax ELen")
    for k in sorted(by_row):
        r = mis[k]
        print(f"  {k} | {r['MinDamage']} {r['MaxDamage']} | {r['MinLevDam1']} {r['MaxLevDam1']} | {r['HitShift']} | {r['SrcDamage']} | {r['EType']} {r['EMin']} {r['Emax']} {r['ELen']}")
    return bad["any"]


def main():
    a = sys.argv[1:]
    if a and a[0] == "game": game(a[1], int(a[2]), int(a[3]), a[4])
    elif a and a[0] == "cmp": sys.exit(1 if cmp(a[1], a[2]) else 0)
    else: print(__doc__)


if __name__ == "__main__": main()
