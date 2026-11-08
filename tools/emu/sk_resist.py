# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's resistance after a curse or aura, the pierce, the
-100 floor and the damage it lets through, by game.exe's own code.

    python sk_resist.py tbl                    # MonStats resist columns by difficulty (game.exe's MPQ stack)
    python sk_resist.py gen N SEED > cases     # boundary grid + N random cases
    python sk_resist.py game cases > game.tsv  # game.exe 1.14d, run natively under the emulator
    python sk_resist.py port cases > port.tsv  # a line-for-line copy of d2d's rules (with the curse fix)
    test_sk_resist --resist cases > d2d.tsv    # the REAL d2d build (tests/game/test_sk_resist.cpp)
    python sk_resist.py cmp cases game.tsv d2d.tsv

Runs next to upstream tools/emu (emu.py, life_apply.py).

A case is `id elem base v pierce roll`: elem 0 physical (stat 0x24), 1 magic
(0x25), 2 fire (0x27, pierce 0x14d), 3 lightning (0x29, 0x14e), 4 cold
(0x2b, 0x14f), 5 poison (0x2d, 0x150); base = the monster's BASE resistance
(MonStats column of the difficulty plus its boss-mod adds, both base stats);
v = one curse / aura aurastat value on that stat (Lower Resist, Conviction,
Amplify Damage, Decrepify; 0 = none); pierce = the attacker's pierce stat of
that element; roll = the record's field in 256ths.

`game` prints `id adj effres dealt`:
  adj    FUN_005c3540(monster, stat, v): the value the curse / aura callback
         (FUN_005c35c0 for srvdofunc 30, FUN_005cedc0 for 65 / 66) stores in
         the state's stat list; the monster's total = base + adj.
  effres FUN_0057be00 on the descriptor row of table 0x732980 with the
         defender a plain monster (context +0x14 = 1): the resistance the
         hit uses (pierce, difficulty penalty, cap, -100 floor).
  dealt  FUN_0057c6c0(target, 1, record) with only that element's field set:
         the life (256ths) the hit takes (FUN_0057c1e0 -> FUN_0057bf80 ->
         FUN_0057be00, MulDiv FUN_00483360).
Hooked (not the claim): the stat getters (FUN_00625480 total, FUN_006253b0
base: a dict), life_apply.py's hooks (unit kinds, events, side effects).

Seeds: xorshift32 on a 32-bit masked state (the C++ side reads the cases, it
draws nothing).
"""
import os
import sys

sys.path.insert(0, os.getcwd())

MASK = 0xFFFFFFFF
# elem -> (resist stat, pierce stat or -1, record field)
ELEM = {0: (0x24, -1, 0x08), 1: (0x25, -1, 0x20), 2: (0x27, 0x14d, 0x10), 3: (0x29, 0x14e, 0x1c),
        4: (0x2b, 0x14f, 0x24), 5: (0x2d, 0x150, 0x28)}
DESC = 0x732980
BIG_LIFE = 30000                       # points: no hit in the cases kills


def xorshift(state):
    x = state[0]
    x ^= (x << 13) & MASK
    x ^= x >> 17
    x ^= (x << 5) & MASK
    state[0] = x & MASK
    return state[0]


def gen(n, seed):
    st = [(seed & MASK) or 1]
    cid = 0
    bases = (-50, -1, 0, 1, 20, 50, 75, 95, 99, 100, 101, 104, 105, 110, 115, 120, 125, 145, 150, 200, 1000)
    vs = (0, -1, -4, -5, -9, -10, -25, -31, -50, -70, -100, -150, 20, 75)
    pierces = (0, 15, 60, 115)
    rolls = (1, 255, 256, 257, 1000, 4096, 25600, 0x100000, 0x100001)
    out = []
    for elem in range(6):
        for base in bases:
            for v in vs:
                for p in (pierces if ELEM[elem][1] >= 0 else (0,)):
                    for r in rolls:
                        cid += 1
                        out.append(f"{cid} {elem} {base} {v} {p} {r}")
    for _ in range(n):
        cid += 1
        elem = xorshift(st) % 6
        base = xorshift(st) % 1151 - 150
        v = 0 if xorshift(st) % 8 == 0 else -(xorshift(st) % 201) + (40 if xorshift(st) % 10 == 0 else 0)
        p = xorshift(st) % 201 if ELEM[elem][1] >= 0 else 0
        r = 1 + xorshift(st) % 0x180000
        out.append(f"{cid} {elem} {base} {v} {p} {r}")
    return out


def parse(path):
    with open(path) as fh:
        for no, line in enumerate(fh, 1):
            f = line.split()
            if not f:
                continue
            if len(f) != 6:
                raise ValueError(f"{path}:{no}: expected 6 fields, got {len(f)}")
            yield tuple(int(x) for x in f)


def s32(v):
    v &= MASK
    return v - (1 << 32) if v >> 31 else v


def game(path):
    import life_apply
    g = life_apply.Game()
    e = g.e
    base = {}
    e.hook(0x6253b0, lambda e: base.get((e.arg(0), e.arg(1)), 0) & MASK, 3)
    ctx = e.alloc(0x40)
    e.w32(ctx, g.game); e.w32(ctx + 4, g.diffrow); e.w32(ctx + 8, g.attacker); e.w32(ctx + 0xc, g.target)
    e.w32(ctx + 0x10, 0); e.w32(ctx + 0x14, 1)                       # attacker a player, defender a plain monster
    rows = {}
    for k in range(12):                                                # the descriptor rows by resist stat (first wins)
        rows.setdefault(e.r32(DESC + 0x2c * k + 4), DESC + 0x2c * k)
    for cid, elem, b, v, p, r in parse(path):
        stat, pstat, field = ELEM[elem]
        base.clear()
        base[(g.target, stat)] = b
        adj = s32(e.call(0x5c3540, v & MASK, ecx=g.target, edx=stat))
        g.reset(BIG_LIFE, 0, 0)
        g.stats.pop((g.target, 0x29), None)
        g.stats[(g.target, stat)] = (b + adj) & MASK
        if pstat >= 0:
            g.stats[(g.attacker, pstat)] = p
        eff = s32(e.call(0x57be00, regs={"ebx": rows[stat], "edi": ctx}))
        e.mu.mem_write(g.rec, b"\0" * 0x80)
        e.w32(g.rec, 1); e.w32(g.rec + 4, 1)
        e.w32(g.rec + field, r)
        before = g.life()
        e.call(0x57c6c0, g.target, 1, g.rec, ecx=g.game, edx=g.attacker)
        print(cid, adj, eff, before - g.life())


# d2d with the fix, line for line: Fight::apply_target_stats takes a fifth of a
# lowering (toward 0) when the base is 100 or more; rules::missile_blow:
# elements fire..poison take the pierce when res < 100. All six resistances
# are floored at -100.
def resisted_fine(fine, res):
    if fine <= 0 or res >= 100:
        return 0
    if res == 0:
        return fine
    if fine > 0x100000:
        return max(s32((fine // 100) * (100 - res)), 0)
    return max(int(fine * (100 - res) / 100), 0)


def port_line(elem, b, v, p, r):
    adj = -(-v // 5) if v < 0 and b >= 100 else v
    res = b + adj
    if 2 <= elem <= 5 and res < 100:
        res -= p
    res = max(res, -100)
    return adj, res, resisted_fine(r, res)


def port(path):
    for cid, elem, b, v, p, r in parse(path):
        print(cid, *port_line(elem, b, v, p, r))


def cmp(cases, gpath, dpath):
    cs = {c[0]: c for c in parse(cases)}
    def rows(path):   # the d2d build also logs timestamped lines: keep `id adj effres dealt`
        return {int(f[0]): f[1:] for f in (l.split() for l in open(path)) if len(f) == 4 and f[0].isdigit()}
    g, d = rows(gpath), rows(dpath)
    if not cs:
        raise SystemExit("cmp: the case file holds no cases")
    for name, got in (("game", g), ("d2d", d)):
        missing = [cid for cid in cs if cid not in got]
        if missing:
            raise SystemExit(f"cmp: the {name} results lack {len(missing)} of {len(cs)} cases (first id {missing[0]})")
    cols = ("adj", "effres", "dealt")
    tot = {c: 0 for c in cols}
    cat = {}
    n = 0
    for cid, (_, elem, b, v, p, _roll) in cs.items():
        n += 1
        for i, c in enumerate(cols):
            if d[cid][i] == "-" or g[cid][i] == d[cid][i]:
                continue
            tot[c] += 1
            why = ("immune base, curse" if b >= 100 and v < 0 else "under -100" if b + v - p < -100 or b + v < -100 else "other")
            key = (c, "phys mag fire ltng cold pois".split()[elem], why)
            cat[key] = cat.get(key, 0) + 1
    print(f"cases {n}: " + ", ".join(f"{c} differ {tot[c]}" for c in cols))
    for k in sorted(cat):
        print("  %-7s %-5s %-20s %d" % (*k, cat[k]))


def tbl():
    import emu
    e = emu.Emu()
    t = e.mpq_file("data\\global\\excel\\MonStats.txt").decode("latin-1").splitlines()
    h = t[0].split("\t")
    rows = [dict(zip(h, ln.split("\t"), strict=False)) for ln in t[1:]]
    print("difficulty column rows(non-empty) min max immune(>=100) negative")
    for suf, name in (("", "Normal"), ("(N)", "Nightmare"), ("(H)", "Hell")):
        for c in ("ResDm", "ResMa", "ResFi", "ResLi", "ResCo", "ResPo"):
            v = [int(r[c + suf]) for r in rows if r.get(c + suf, "").lstrip("-").isdigit()]
            print(name, c, len(v), min(v), max(v), sum(x >= 100 for x in v), sum(x < 0 for x in v))
    for r in rows:
        if r.get("Id") in ("zombie1", "fallen1", "quillrat1", "andariel", "duriel", "mephisto", "diablo", "baalcrab"):
            print(r["Id"], *(r.get(c + s, "") for s in ("", "(N)", "(H)") for c in ("ResDm", "ResMa", "ResFi", "ResLi", "ResCo", "ResPo")))


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "tbl"
    if what == "gen":
        print("\n".join(gen(int(sys.argv[2]), int(sys.argv[3]))))
    elif what == "game":
        game(sys.argv[2])
    elif what == "port":
        port(sys.argv[2])
    elif what == "cmp":
        cmp(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        tbl()


if __name__ == "__main__":
    main()
