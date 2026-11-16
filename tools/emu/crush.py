#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A player's Crushing Blow, by game.exe's own code: the oracle for `player_blow`'s crush (FUN_005bffc0, the order in FUN_0057c6c0).

    cd <d2decomped>/tools/emu
    uv run python crush.py gen 2000 7 > cases.tsv
    uv run python crush.py game cases.tsv > game.tsv       # id crush life_at_event life_after, 256ths
    uv run python crush.py order cases.tsv > order.tsv     # FUN_0057c6c0: the life the strike events see, and after the damage
    g++ -O2 -std=c++23 -I<d2d>/components/rules -I<d2d>/components -I<d2d>/components/d2s -I<d2d>/components/txt \\
        -o crush_harness crush_harness.cpp && ./crush_harness < cases.tsv > d2d.tsv
    uv run python crush.py compare cases.tsv game.tsv d2d.tsv        # exit 1 on a mismatch
    uv run python crush.py gen-pair 2000 7 > pair.tsv                 # Crushing Blow and leech on one blow
    uv run python crush.py pair pair.tsv > game_pair.tsv              # id crush life mana, 256ths
    ./crush_harness --pair < pair.tsv > d2d_pair.tsv
    uv run python crush.py compare-pair pair.tsv game_pair.tsv d2d_pair.tsv

A case line is `id life phys res div`: life = the defender's current life (256ths), phys = the record's damage (256ths), res
= the defender's stat 0x24 (percent), div = 4 (a plain monster), 8 (boss or unique), 10 (hireling): the branch of FUN_005bffc0.

`game` runs FUN_005bffc0 natively (event 5: a melee strike) on a defender whose stat 6 is `life`: the handler takes
life / divisor (256ths, whole division), less MulDiv(that, min(res, 100), 100) when res > 0, and sets stat 6 (a life that
reaches 0 sets the kill bit). `order` runs FUN_0057c6c0 natively (melee: its second argument 0) with the state-event
dispatcher FUN_005c0c30 replaced by a recorder: the defender's life at events 5 and 1 is the life BEFORE the record's damage,
which LAB_0057c852 takes after them (end life = life - damage, 0 under one point). So the handler's `life` is the life
before the hit. (Which events the item property registers the handler on is FUN_005bffc0's own test: event 6 doubles the divisor.)

`pair` (cases `id diff sl sm life phys div`, no resistance) runs both on one melee blow: FUN_005bffc0 natively for the life it
leaves, then FUN_0057c6c0 natively with the dispatcher's event 5 setting the defender's stat 6 to that life, so its cut of the
physical to the life (0x57c7f8) and the leech FUN_0057c420 see the life AFTER the crush: `id crush life mana`, 256ths.
"""
import random, sys

M32 = 0xFFFFFFFF
COLS = "id life phys res div".split()
PAIR_COLS = "id diff sl sm life phys div".split()


def s32(v): return v - (1 << 32) if v >> 31 else v


def gen(n, seed):
    rnd = random.Random(seed)
    rows = []
    def add(life, phys, res, div): rows.append((len(rows), life, phys, res, div))
    for life in (1 << 8, 5 << 8, 20 << 8, 100 << 8, 400 << 8, 2000 << 8):
        for phys in (1 << 8, 6 << 8, 30 << 8, 150 << 8):
            for res in (0, 25, 50, 100, -20):
                for div in (4, 8, 10): add(life, phys, res, div)
    nb = len(rows)
    for _ in range(n):
        life = rnd.choice((rnd.randrange(256, 40 << 8), rnd.randrange(256, 4000 << 8)))
        phys = rnd.choice((rnd.randrange(1, 12 << 8), rnd.randrange(1, 200 << 8)))
        add(life, phys, rnd.choice((0, 0, 0, 20, 50, 75, rnd.randrange(-50, 120))), rnd.choice((4, 4, 4, 8, 10)))
    print("#", " ".join(COLS), f"boundary={nb}", f"random={n}", f"seed={seed}")
    for r in rows: print("\t".join(str(v) for v in r))


def read_cases(path, cols=COLS):
    for k, line in enumerate(open(path), 1):
        if line.startswith("#") or not line.strip(): continue
        try:
            row = [int(v) for v in line.split()]
        except ValueError:
            row = []
        if len(row) != len(cols): raise SystemExit(f"{path}:{k}: malformed case line: {line.rstrip()!r}")
        yield dict(zip(cols, row))


def gen_pair(n, seed):
    rnd = random.Random(seed)
    rows = []
    def add(diff, sl, sm, life, phys, div): rows.append((len(rows), diff, sl, sm, life, phys, div))
    for diff in (0, 1, 2):
        for sl, sm in ((10, 0), (20, 10), (0, 5)):
            for life in (1 << 8, 5 << 8, 20 << 8, 100 << 8, 400 << 8):
                for phys in (0x80, 6 << 8, 30 << 8, 90 << 8, 150 << 8, 1000 << 8):
                    for div in (4, 8, 10): add(diff, sl, sm, life, phys, div)
    nb = len(rows)
    for _ in range(n):
        life = rnd.choice((rnd.randrange(1, 40 << 8), rnd.randrange(256, 4000 << 8)))
        phys = rnd.choice((rnd.randrange(1, 12 << 8), rnd.randrange(1, 200 << 8), rnd.randrange(life // 2 + 1, life + 2)))
        add(rnd.randrange(3), rnd.choice((0, 3, 5, 8, 10, 15, 20, rnd.randrange(0, 101))), rnd.choice((0, 0, 5, 10, rnd.randrange(0, 101))),
            life, phys, rnd.choice((4, 4, 4, 8, 10)))
    print("#", " ".join(PAIR_COLS), f"boundary={nb}", f"random={n}", f"seed={seed}")
    for r in rows: print("\t".join(str(v) for v in r))


def game(exe, path):
    import emu
    e = emu.Emu(exe) if exe else emu.Emu()
    st = {}
    attacker = e.alloc(0x100)
    target = e.alloc(0x200); e.w32(target, 1); e.w32(target + 4, 60)
    rec = e.alloc(0x100)
    kind = {"div": 4}
    e.hook(0x625480, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x625500, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x627260, lambda e_: st.__setitem__((e_.arg(0), e_.arg(1)), s32(e_.arg(2))) or 0, 4)
    e.hook(0x63ee90, lambda e_: 1 if kind["div"] == 10 else 0, 1)                 # hireling (the 10 branch)
    e.hook(0x63e9f0, lambda e_: 1 if kind["div"] == 8 else 0, 2)                  # boss / unique (the 8 branch)
    e.hook(0x5a0180, lambda e_: 0, 0)
    e.hook(0x5738f0, lambda e_: 0, 0)                                              # no extra players: no life share
    e.hook(0x621e40, lambda e_: 0, 3)
    n = 0
    for c in read_cases(path):
        st.clear()
        kind["div"] = c["div"]
        st[(attacker, 136)] = 100                                                  # crushing blow 100 %: always rolls under
        st[(target, 6)] = st[(target, 7)] = c["life"]
        st[(target, 0x24)] = c["res"]
        st[(target, 100)] = 1
        e.w32(attacker + 0x20, 12345); e.w32(attacker + 0x24, 678)
        e.w32(rec + 4, 1)
        life_event = st[(target, 6)]                                               # what a handler at the event reads: the life before LAB_0057c852 takes the damage
        ok = e.call(0x5bffc0, attacker, target, rec, (136 << 16) | 0, edx=5)
        crushed = life_event - st[(target, 6)] if ok else 0
        print(c["id"], crushed, life_event, st[(target, 6)])
        n += 1
    print("#", n, "cases", file=sys.stderr)


def order(exe, path):
    """FUN_0057c6c0 natively (hooks as d35_leech.py): the defender's life as FUN_005c0c30's event 5 (the attacker's melee strike,
    where the crushing blow handler runs) sees it, and after the call. phys = the record's +0x4c, the damage LAB_0057c852 takes."""
    import emu
    e = emu.Emu(exe) if exe else emu.Emu()
    st, seen = {}, []
    game_ptr = e.alloc(0x1000); e.w32(game_ptr + 0x70, 1)
    attacker = e.alloc(0x100)
    target = e.alloc(0x200); e.w32(target, 1); e.w32(target + 4, 60)
    data = e.alloc(0x40); row = e.alloc(0x400); e.w32(target + 0x14, data); e.w32(data, row)
    diffrow = e.alloc(0x80)
    e.hook(0x625480, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x627260, lambda e_: st.__setitem__((e_.arg(0), e_.arg(1)), s32(e_.arg(2))) or 0, 4)
    e.hook(0x625d10, lambda e_: 1 << 30, 1)
    e.hook(0x611d30, lambda e_: diffrow, 1)
    e.hook(0x554010, lambda e_: game_ptr, 0)
    e.hook(0x620bb0, lambda e_: 1, 1)
    e.hook(0x57a980, lambda e_: 0, 1)
    e.hook(0x57aa00, lambda e_: 0, 1)
    e.hook(0x5c0c30, lambda e_: seen.append((e_.mu.reg_read(emu.UC_X86_REG_EDX), st.get((target, 6), 0))) or 0, 3)
    for addr, n in ((0x63ee90, 1), (0x63e990, 1), (0x63e940, 1), (0x63edc0, 1), (0x63e9f0, 2), (0x5347d0, 7),
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
    e.hook(0x57c1e0, lambda e_: 0, 2)                                              # the record is the input: no reduction pass
    for c in read_cases(path):
        st.clear(); seen.clear()
        st[(target, 6)] = st[(target, 7)] = c["life"]
        r = e.alloc(0x100)
        e.w32(r, 1); e.w32(r + 4, 1 | 4); e.w32(r + 8, c["phys"]); e.w32(r + 0x4c, c["phys"])
        e.call(0x57c6c0, target, 0, r, ecx=game_ptr, edx=attacker)
        ev = {k: v for k, v in seen}
        print(c["id"], ev.get(5, -1), ev.get(1, -1), st[(target, 6)])


def pair(exe, path):
    """FUN_005bffc0 natively for the life a crush leaves, then FUN_0057c6c0 natively (hooks as `order`) with event 5 setting
    the defender's stat 6 to that life: the leech FUN_0057a980 / FUN_0057aa00 receive (256ths)."""
    import emu
    from leech import difficulty_divisors
    e = emu.Emu(exe) if exe else emu.Emu()
    div = difficulty_divisors(emu)
    st, out, after = {}, {"life": 0, "mana": 0}, {"life": None}
    game_ptr = e.alloc(0x1000); e.w32(game_ptr + 0x70, 1)
    attacker = e.alloc(0x100)
    target = e.alloc(0x200); e.w32(target, 1); e.w32(target + 4, 60)
    data = e.alloc(0x40); row = e.alloc(0x400); e.w32(target + 0x14, data); e.w32(data, row)
    diffrow = [e.alloc(0x80) for _ in range(3)]
    for d in range(3):
        e.w32(diffrow[d] + 0x28, div[d][0]); e.w32(diffrow[d] + 0x2c, div[d][1])
    kind = {"div": 4}

    def event(e_):
        if e_.mu.reg_read(emu.UC_X86_REG_EDX) == 5 and after["life"] is not None: st[(target, 6)] = after["life"]
        return 0
    e.hook(0x625480, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x625500, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x627260, lambda e_: st.__setitem__((e_.arg(0), e_.arg(1)), s32(e_.arg(2))) or 0, 4)
    e.hook(0x625d10, lambda e_: 1 << 30, 1)
    e.hook(0x611d30, lambda e_: diffrow[min(e_.arg(0) & 0xff, 2)], 1)
    e.hook(0x554010, lambda e_: game_ptr, 0)
    e.hook(0x620bb0, lambda e_: 1, 1)
    e.hook(0x57a980, lambda e_: out.__setitem__("life", out["life"] + s32(e_.arg(0))) or 0, 1)
    e.hook(0x57aa00, lambda e_: out.__setitem__("mana", out["mana"] + s32(e_.arg(0))) or 0, 1)
    e.hook(0x5c0c30, event, 3)
    e.hook(0x63ee90, lambda e_: 1 if kind["div"] == 10 else 0, 1)
    e.hook(0x63e9f0, lambda e_: 1 if kind["div"] == 8 else 0, 2)
    e.hook(0x5a0180, lambda e_: 0, 0)
    e.hook(0x5738f0, lambda e_: 0, 0)
    for addr, n in ((0x63e990, 1), (0x63e940, 1), (0x63edc0, 1), (0x5347d0, 7),
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
    for c in read_cases(path, PAIR_COLS):
        st.clear(); after["life"] = None
        out["life"] = out["mana"] = 0
        kind["div"] = c["div"]
        st[(attacker, 136)] = 100
        st[(target, 6)] = st[(target, 7)] = c["life"]
        st[(target, 100)] = 1
        e.w32(attacker + 0x20, 12345); e.w32(attacker + 0x24, 678)
        crec = e.alloc(0x100); e.w32(crec + 4, 1)
        ok = e.call(0x5bffc0, attacker, target, crec, (136 << 16) | 0, edx=5)
        after["life"] = st[(target, 6)]
        crushed = c["life"] - after["life"] if ok else 0
        e.w32(game_ptr + 0x6c, c["diff"] << 8)
        e.w32(row + 0xa0, 0); e.w32(row + 0xa4, 0)
        e.mu.mem_write(row + 0xa0 + c["diff"], bytes([100]))
        st[(target, 6)] = c["life"]
        rec = e.alloc(0x100)
        e.w32(rec, 1); e.w32(rec + 4, 1 | 4)
        e.w32(rec + 8, c["phys"]); e.w32(rec + 0x38, c["sl"]); e.w32(rec + 0x3c, c["sm"])
        e.call(0x57c6c0, target, 0, rec, ecx=game_ptr, edx=attacker)
        print(c["id"], crushed, out["life"], out["mana"])
        n += 1
    print("#", n, "cases", file=sys.stderr)


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
    g, d = load(gpath, 4), load(dpath, 2)
    if not aligned(cases, (("game", g), ("d2d", d))): return 1
    cat, tot = {}, 0
    for c, gl, dl in zip(cases, g, d):
        key = "res %s" % ("0" if c["res"] == 0 else "<0" if c["res"] < 0 else ">0")
        r = cat.setdefault(key, [0, 0, 0])
        r[0] += 1
        r[1] += gl[1] != dl[1]
        r[2] += gl[1] - dl[1]
        tot += gl[1] != dl[1]
    for k, r in sorted(cat.items()):
        print(f"  {k}: {r[0]} cases, d2d differs in {r[1]}, mean game - d2d crush {r[2] / max(r[0], 1) / 256:+.3f} hp")
    print(f"compare: {len(cases)} cases, d2d differs from game.exe in {tot}")
    return 0 if tot == 0 else 1


def compare_pair(cpath, gpath, dpath):
    cases = list(read_cases(cpath, PAIR_COLS))
    g, d = load(gpath, 4), load(dpath, 4)
    if not aligned(cases, (("game", g), ("d2d", d))): return 1
    cat, tot = {}, 0
    for c, gl, dl in zip(cases, g, d):
        key = "physical over the life the crush leaves" if c["phys"] > c["life"] - gl[1] else "physical within it"
        r = cat.setdefault(key, [0, 0, 0])
        r[0] += 1
        r[1] += gl[1] != dl[1]
        r[2] += (gl[2], gl[3]) != (dl[2], dl[3])
        tot += gl[1:] != dl[1:]
    for k, r in sorted(cat.items()):
        print(f"  {k}: {r[0]} cases, crush differs in {r[1]}, leech differs in {r[2]}")
    print(f"compare-pair: {len(cases)} cases, d2d differs from game.exe in {tot}")
    return 0 if tot == 0 else 1


USAGE = ("usage: crush.py gen N SEED | game CASES | order CASES | compare CASES GAME D2D"
         " | gen-pair N SEED | pair CASES | compare-pair CASES GAME D2D   (game, order, pair take --exe GAME.EXE)")


if __name__ == "__main__":
    argv, exe = sys.argv[1:], None
    if "--exe" in argv:
        k = argv.index("--exe")
        if k + 1 >= len(argv): raise SystemExit(USAGE)
        exe = argv[k + 1]; del argv[k:k + 2]
    arity = {"gen": 3, "gen-pair": 3, "game": 2, "order": 2, "pair": 2, "compare": 4, "compare-pair": 4}
    if not argv or arity.get(argv[0]) != len(argv): raise SystemExit(USAGE)
    if argv[0] in ("gen", "gen-pair"):
        try:
            n, seed = int(argv[1]), int(argv[2])
        except ValueError:
            raise SystemExit(USAGE) from None
        (gen if argv[0] == "gen" else gen_pair)(n, seed)
    elif argv[0] == "game": game(exe, argv[1])
    elif argv[0] == "order": order(exe, argv[1])
    elif argv[0] == "pair": pair(exe, argv[1])
    elif argv[0] == "compare": sys.exit(compare(argv[1], argv[2], argv[3]))
    else: sys.exit(compare_pair(argv[1], argv[2], argv[3]))
