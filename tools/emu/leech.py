#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A player's life and mana leech of a melee blow, by game.exe's own code: the oracle for `player_blow`'s `Blow::life` / `Blow::mana`.

    cd <d2decomped>/tools/emu
    uv run python leech.py gen 20000 7 > cases.tsv
    uv run python leech.py game cases.tsv > game.tsv
    g++ -O2 -std=c++23 -I<d2d>/components/rules -I<d2d>/components -I<d2d>/components/d2s -I<d2d>/components/txt \\
        -o leech_harness leech_harness.cpp <d2d>/build/components/rules/librules.a && ./leech_harness < cases.tsv > d2d.tsv
    uv run python leech.py compare cases.tsv game.tsv d2d.tsv        # exit 1 on a mismatch

A case line is `id diff steal_life steal_mana drain phys life`: diff 0 / 1 / 2 (the game's difficulty, byte +0x6d),
steal_life / steal_mana the attacker's stats 60 / 62 (percent), drain the defender's MonStats Drain of that difficulty
(percent), phys the record's physical damage in 256ths (record +8), life the defender's current life in 256ths (stat 6).

`game` runs FUN_0057c6c0 on game.exe natively for a player attacker and a monster defender (class 60); FUN_0057c420 raises
the leech, FUN_0057a980 (life) and FUN_0057aa00 (mana) receive it in 256ths: printed as `id life mana physical_after`.
`selfcheck` re-derives the rule from the disassembly (physical cut to the defender's life, the steal % << 6 over the
difficulty's LifeStealDivisor / ManaStealDivisor, MulDiv by the physical, MulDiv by Drain unless 100, / 64 toward zero) and
compares it with the game column. `d2d.tsv` is `id life mana physical` in 256ths, the leech d2d credits.
"""
import random, sys

M32 = 0xFFFFFFFF
LIFE_DIV = {0: 1, 1: 2, 2: 3}          # DifficultyLevels LifeStealDivisor / ManaStealDivisor, read from the table in game()
COLS = "id diff sl sm drain phys life".split()


def s32(v): return v - (1 << 32) if v >> 31 else v


def gen(n, seed):
    rnd = random.Random(seed)
    rows = []
    def add(diff, sl, sm, drain, phys, life): rows.append((len(rows), diff, sl, sm, drain, phys, life))
    for diff in (0, 1, 2):
        for sl in (0, 1, 3, 5, 6, 7, 8, 10, 15, 20, 30, 50, 100):
            for phys in (1 << 8, 3 << 8, 8 << 8, 10 << 8, 40 << 8, 100 << 8, 0x80, 0x1ff):
                add(diff, sl, sl // 2, 100, phys, 1 << 30)
    for diff in (0, 1, 2):                                               # overkill: the defender nearly dead
        for life in (1, 100, 256, 3 << 8, 5 << 8, 10 << 8):
            add(diff, 10, 10, 100, 20 << 8, life)
    for diff in (0, 1, 2):                                               # drain below 100
        for drain in (0, 1, 25, 50, 75, 100, 125):
            add(diff, 10, 10, drain, 20 << 8, 1 << 30)
    nb = len(rows)
    for _ in range(n):
        phys = rnd.choice((rnd.randrange(1, 0x4000), rnd.randrange(1, 0x100), rnd.randrange(0x100, 0x800), rnd.randrange(1, 0x100000)))
        life = rnd.choice((1 << 30, 1 << 30, rnd.randrange(1, 0x8000), rnd.randrange(0x100, 0x4000)))
        add(rnd.randrange(3), rnd.choice((0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15, 20, 25, 33, 50, rnd.randrange(0, 101))),
            rnd.choice((0, 0, 1, 2, 3, 5, 8, 10, 15, 20, 30, rnd.randrange(0, 101))),
            rnd.choice((100, 100, 100, 100, 50, 75, 25, 0, rnd.randrange(0, 126))), phys, life)
    print("#", " ".join(COLS), f"boundary={nb}", f"random={n}", f"seed={seed}")
    for r in rows: print("\t".join(str(v) for v in r))


def read_cases(path):
    for k, line in enumerate(open(path), 1):
        if line.startswith("#") or not line.strip(): continue
        try:
            row = [int(v) for v in line.split()]
        except ValueError:
            row = []
        if len(row) != len(COLS): raise SystemExit(f"{path}:{k}: malformed case line: {line.rstrip()!r}")
        yield dict(zip(COLS, row))


def difficulty_divisors(emu):
    import csv
    text = emu.mpq_cache().read("data\\global\\excel\\difficultylevels.txt").decode("latin1").splitlines()
    rows = list(csv.reader(text, delimiter="\t"))
    h = rows[0]
    li, mi = h.index("LifeStealDivisor"), h.index("ManaStealDivisor")
    return [(int(r[li] or 0), int(r[mi] or 0)) for r in rows[1:4]]


def game(exe, path):
    import emu
    e = emu.Emu(exe) if exe else emu.Emu()
    div = difficulty_divisors(emu)
    print("# DifficultyLevels Life/ManaStealDivisor per difficulty:", div, file=sys.stderr)
    st = {}
    out = {"life": 0, "mana": 0}
    game_ptr = e.alloc(0x1000); e.w32(game_ptr + 0x70, 1)
    attacker = e.alloc(0x100)                                                 # type 0
    target = e.alloc(0x200); e.w32(target, 1); e.w32(target + 4, 60)
    data = e.alloc(0x40); row = e.alloc(0x400); e.w32(target + 0x14, data); e.w32(data, row)
    diffrow = [e.alloc(0x80) for _ in range(3)]
    for d in range(3):
        e.w32(diffrow[d] + 0x28, div[d][0]); e.w32(diffrow[d] + 0x2c, div[d][1])
    e.hook(0x625480, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x627260, lambda e_: st.__setitem__((e_.arg(0), e_.arg(1)), s32(e_.arg(2))) or 0, 4)
    e.hook(0x625d10, lambda e_: 1 << 30, 1)
    e.hook(0x611d30, lambda e_: diffrow[min(e_.arg(0) & 0xff, 2)], 1)
    e.hook(0x554010, lambda e_: game_ptr, 0)
    e.hook(0x620bb0, lambda e_: 1, 1)
    e.hook(0x57a980, lambda e_: out.__setitem__("life", out["life"] + s32(e_.arg(0))) or 0, 1)
    e.hook(0x57aa00, lambda e_: out.__setitem__("mana", out["mana"] + s32(e_.arg(0))) or 0, 1)
    for addr, n in ((0x63ee90, 1), (0x63e990, 1), (0x63e940, 1), (0x63edc0, 1), (0x63e9f0, 2), (0x5c0c30, 3), (0x5347d0, 7),
                    (0x5347e0, 3), (0x61ab00, 1), (0x5541b0, 0), (0x621d50, 2), (0x639df0, 2), (0x57aa60, 0), (0x57aaa0, 0),
                    (0x57aae0, 2), (0x57af80, 2), (0x57b230, 2), (0x57ac50, 3), (0x57add0, 3), (0x5d6410, 0),
                    (0x6256b0, 2), (0x5a54f0, 2), (0x5a5650, 0), (0x5734c0, 0), (0x5a43a0, 0), (0x5a7c20, 3), (0x621e40, 3)):
        e.hook(addr, lambda e_: 0, n)
    for addr, n in ((0x554200, 1), (0x457490, 0)):
        e.hook(addr, lambda e_: 1, n)
    e.hook(0x46c140, lambda e_: 1 if e_.mu.reg_read(emu.UC_X86_REG_EDX) == 3 else 0, 0)
    e.hook(0x5a7e60, lambda e_: 0, 3)
    e.hook(0x5809d0, lambda e_: 0, 5)
    e.hook(0x64c040, lambda e_: 0, 1)
    n = 0
    for c in read_cases(path):
        st.clear()
        out["life"] = out["mana"] = 0
        e.w32(game_ptr + 0x6c, c["diff"] << 8)
        e.w32(row + 0xa0, 0); e.w32(row + 0xa4, 0)
        e.mu.mem_write(row + 0xa0 + c["diff"], bytes([c["drain"] & 0xff]))
        st[(target, 6)] = st[(target, 7)] = c["life"]
        rec = e.alloc(0x100)
        e.w32(rec, 1); e.w32(rec + 4, 1 | 4)
        e.w32(rec + 8, c["phys"]); e.w32(rec + 0x38, c["sl"]); e.w32(rec + 0x3c, c["sm"])
        e.call(0x57c6c0, target, 1, rec, ecx=game_ptr, edx=attacker)
        print(c["id"], out["life"], out["mana"], s32(e.r32(rec + 8)))
        n += 1
    print("#", n, "cases", file=sys.stderr)


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def i32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def muldiv(a, b, c):
    """FUN_00483360 (V read, 0x483360..0x4833d6): a x b / c, truncating; a > 0x100000 (and c <= a >> 4) divides first,
    b > 0x10000 (and c <= b >> 4) likewise, else a 32-bit product that wraps."""
    if c == 0: return 0
    if a > 0x100000:
        return tdiv(a * b, c) if c > (a >> 4) else i32(tdiv(a, c) * b)
    if b > 0x10000:
        return tdiv(a * b, c) if c > (b >> 4) else i32(tdiv(b, c) * a)
    return tdiv(i32(a * b), c)


def game_model(c):
    """The rule read from FUN_0057c6c0 (0x57c7f8..0x57c826) and FUN_0057c420: the physical is cut to the defender's life,
    steal << 6 over the difficulty's divisor, MulDiv by the physical, MulDiv by Drain (not at 100), / 64 toward 0, in 256ths."""
    if c["drain"] <= 0: return 0, 0
    phys = min(c["phys"], c["life"])
    out = []
    for steal in (c["sl"], c["sm"]):
        v = (steal << 6) // LIFE_DIV[c["diff"]] if steal else 0
        if not v or phys <= 0: out.append(0); continue
        x = muldiv(phys, v, 100)
        if c["drain"] != 100: x = muldiv(x, c["drain"], 100)
        out.append(tdiv(x, 64))
    return out[0], out[1]


def load(path, width):
    """The rows of a game or d2d file: `width` integers each, '#' lines skipped; None (with a message) on a malformed line."""
    rows = []
    try:
        lines = open(path).read().splitlines()
    except OSError as err:
        print(f"{path}: {err}", file=sys.stderr)
        return None
    for k, line in enumerate(lines, 1):
        if line.startswith("#") or not line.strip(): continue
        try:
            row = [int(v) for v in line.split()]
        except ValueError:
            row = []
        if len(row) != width:
            print(f"{path}:{k}: malformed line: {line!r}", file=sys.stderr)
            return None
        rows.append(row)
    return rows


def aligned(cases, columns):
    """The case rows and each answer file in step: same length, not empty, ids equal; else a message and False."""
    if not cases:
        print("no cases: an empty file is not a pass", file=sys.stderr)
        return False
    for name, rows in columns:
        if rows is None: return False
        if len(rows) != len(cases):
            print(f"{name}: {len(rows)} lines against {len(cases)} cases", file=sys.stderr)
            return False
        for c, r in zip(cases, rows):
            if r[0] != c["id"]:
                print(f"{name}: id {r[0]} where case {c['id']} was expected", file=sys.stderr)
                return False
    return True


def compare(cpath, gpath, dpath):
    cases = list(read_cases(cpath))
    g, d = load(gpath, 4), load(dpath, 4)
    if not aligned(cases, (("game", g), ("d2d", d))): return 1
    cat = {}
    for c, gl, dl in zip(cases, g, d):
        factors = []
        if c["diff"] > 0 and (c["sl"] or c["sm"]): factors.append("divisor")
        if c["life"] < c["phys"]: factors.append("overkill")
        key = "+".join(factors) or "plain"
        r = cat.setdefault(key, [0, 0, 0, 0, 0])
        r[0] += 1
        diff = (gl[1], gl[2]) != (dl[1], dl[2])
        r[1] += diff
        r[2] += gl[1] - dl[1]; r[3] += gl[2] - dl[2]
        r[4] += (gl[1] > 0 and dl[1] == 0)
    tot = 0
    for k, r in sorted(cat.items()):
        print(f"  {k}: {r[0]} cases, d2d differs in {r[1]} (game credits life where d2d credits 0: {r[4]}); mean game - d2d life {r[2] / max(r[0], 1) / 256:+.3f} hp, mana {r[3] / max(r[0], 1) / 256:+.3f}")
        tot += r[1]
    print(f"compare: {len(cases)} cases, d2d differs from game.exe in {tot}")
    return 0 if tot == 0 else 1


def selfcheck(cpath, gpath):
    cases = list(read_cases(cpath))
    g = load(gpath, 4)
    if not aligned(cases, (("game", g),)): return 1
    bad = sum(game_model(c) != (gl[1], gl[2]) for c, gl in zip(cases, g))
    print(f"selfcheck: {len(cases)} cases, the rule read from the code differs from game.exe in {bad}")
    return 0 if bad == 0 else 1


USAGE = "usage: leech.py gen N SEED | game CASES [--exe GAME.EXE] | selfcheck CASES GAME | compare CASES GAME D2D"


if __name__ == "__main__":
    argv, exe = sys.argv[1:], None
    if "--exe" in argv:
        k = argv.index("--exe")
        if k + 1 >= len(argv): raise SystemExit(USAGE)
        exe = argv[k + 1]; del argv[k:k + 2]
    arity = {"gen": 3, "game": 2, "selfcheck": 3, "compare": 4}
    if not argv or arity.get(argv[0]) != len(argv): raise SystemExit(USAGE)
    if argv[0] == "gen":
        try:
            n, seed = int(argv[1]), int(argv[2])
        except ValueError:
            raise SystemExit(USAGE) from None
        gen(n, seed)
    elif argv[0] == "game": game(exe, argv[1])
    elif argv[0] == "selfcheck": sys.exit(selfcheck(argv[1], argv[2]))
    else: sys.exit(compare(argv[1], argv[2], argv[3]))
