#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's get-hit after a Crushing Blow, by game.exe's own code: the oracle for the size the get-hit test sees.

    cd <d2decomped>/tools/emu
    uv run python crush_hit.py gen 4000 7 > cases.tsv
    uv run python crush_hit.py game cases.tsv > game.tsv            # id gh seedlo seedhi life total crush
    <build>/tests/test_crush_get_hit --crush-hit cases.tsv > d2d.tsv   # id gh seedlo seedhi life
    (--no-crush on `game` and a trailing --no-crush on the test: the same blows without the crush, the control that
    the two sides agree on everything but the crush)
    uv run python crush_hit.py compare cases.tsv game.tsv d2d.tsv     # exit 1 on a mismatch, or an empty or bad file

A case line is `id maxlife life phys hc div seedlo seedhi fire missile`: the monster's max life and current life (256ths,
the crush reads the current life), the landed record's physical (256ths, before the crush), the record's hit class (+0x60),
the crush divisor of FUN_005bffc0 (4 a plain monster, 8 a boss row or a superunique), the monster's seed (+0x20 / +0x24),
the record's fire (+0x10, 256ths, whole points: an element marks the blow for FUN_0057ce30) and 1 for a missile's blow.
The monster survives the blow (life > crush + physical + fire + two points), so the get-hit test is reached.

`game` runs one landed blow as game.exe does it. FUN_005bffc0 natively (Crushing Blow 100 percent; the strike event 5, or 6
for a missile, which doubles the divisor) takes life / div off the monster's life (stat 6); FUN_0057c6c0 natively (the damage
handler, the dispatcher FUN_005c0c30 setting stat 6 to the crush's life at that event) cuts the record's physical to that life
and takes the record's total (+0x4c) off it (0x57c852); FUN_0057cee0 natively then takes the record (the get-hit:
FUN_0057ce30 and FUN_0057cb00 on the SAME record, FUN_00472210's rolls on the unit's seed, as hit_rule.py runs it). A melee
record (FUN_0057c6c0's second argument 0, the caller 0x57d5c7 hands it a stored record) has its total from FUN_0057c1e0 run
first; a missile's (second argument 1, the caller 0x56ba73, record flag 0x20) has FUN_0057c6c0 call FUN_0057c1e0 itself
(0x57c7a0) before the strike events 6 and 2. `life` is stat 6 after the handler, `total` the record's +0x4c after it (the
crush is never in it) and `crush` the life the crush took.
Hooked as in crush.py `pair` (the unit's stats as a table, the room, the dispatcher) and hit_rule.py (the monster-mode
requests, the unit's state list); the room hook is 1 for the handler and 0 for FUN_0057cee0, as in the two oracles.
"""
import random
import sys

M32 = 0xFFFFFFFF
COLS = "id maxlife life phys hc div seedlo seedhi fire missile".split()
TAKE_RECORD, HANDLER, CRUSH, REDUCE = 0x57CEE0, 0x57C6C0, 0x5BFFC0, 0x57C1E0
MONSTER_GH = 3
DIVISORS = {0: 16, 1: 16, 2: 8, 3: 16, 4: 32, 5: 64, 6: 8, 7: 16, 8: 32, 9: 16, 10: 8, 11: 8, 12: 16, 13: 16}


def s32(v): return v - (1 << 32) if v >> 31 else v


def gen(n, seed):
    rnd = random.Random(seed)
    rows = []

    def add(maxlife, life, phys, hc, div, fire=0, missile=0):
        crush = life // (div * (2 if missile else 1))
        fire = min(fire, max(life - crush - 768, 0)) >> 8 << 8
        phys = min(phys, life - crush - fire - 512)
        if phys < 0 or life > maxlife: return
        rows.append((len(rows), maxlife, life, phys, hc, div, rnd.getrandbits(32), rnd.getrandbits(32), fire, missile))

    for maxlife in (20, 100, 400, 3000):
        for hc in (0, 2, 4, 5, 13):
            for div in (4, 8):
                share = maxlife * 256 // DIVISORS.get(hc, 16)
                for missile in (0, 1):
                    for life_pct in (100, 80, 50, 25):
                        life = maxlife * 256 * life_pct // 100
                        crush = life // (div * (2 if missile else 1))
                        for phys in (share - 1, share, share - crush, share - crush + 1, share // 2):
                            add(maxlife * 256, life, max(phys, 0), hc, div, 0, missile)
                        # an element marks the blow (the divisor 16 for every class): fire alone, and fire with a physical
                        marked = maxlife * 256 // 16
                        for fire in (marked // 256 * 256, (marked + 255) // 256 * 256, marked * 2 // 256 * 256 + 256):
                            add(maxlife * 256, life, 0, hc, div, fire, missile)
                            add(maxlife * 256, life, max(share // 4, 0), hc, div, fire, missile)
    nb = len(rows)
    while len(rows) < nb + n:
        maxlife = rnd.choice((rnd.randint(1, 60), rnd.randint(1, 600), rnd.randint(1, 4000))) << 8 | rnd.randint(0, 255)
        maxlife = max(maxlife, 1024)
        life = max(1024, maxlife * rnd.choice((100, 100, 80, 60, 40, 20, 10)) // 100) if rnd.random() < 0.8 else rnd.randint(1024, maxlife)
        hc = rnd.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, rnd.randint(0, 0x5f)))
        div = rnd.choice((4, 4, 4, 8))
        share = maxlife // DIVISORS.get(hc, 16)
        crush = life // div
        phys = rnd.choice((share, share - crush, share // 2, rnd.randint(0, share + 400), rnd.randint(0, life)))
        phys = max(phys + rnd.choice((-1, 0, 1, rnd.randint(-300, 300))), 0)
        missile = int(rnd.random() < 0.3)
        fire = 0 if rnd.random() < 0.7 else rnd.choice((rnd.randint(1, 8), rnd.randint(1, max(share >> 8, 1) * 2))) << 8
        add(maxlife, life, phys, hc, div, fire, missile)
    print("#", " ".join(COLS), f"boundary={nb}", f"random={n}", f"seed={seed}")
    for r in rows: print("\t".join(str(v) for v in r))


def read_cases(path):
    try:
        lines = open(path).read().splitlines()
    except OSError as err:
        raise SystemExit(f"{path}: {err}") from None
    for k, line in enumerate(lines, 1):
        if line.startswith("#") or not line.strip(): continue
        try:
            row = [int(v) for v in line.split()]
        except ValueError:
            row = []
        if len(row) != len(COLS): raise SystemExit(f"{path}:{k}: malformed case line: {line!r}")
        yield dict(zip(COLS, row))


def game(exe, path, control=False):
    import emu
    e = emu.Emu(exe) if exe else emu.Emu()
    cases = list(read_cases(path))
    if not cases: raise SystemExit(f"{path}: no cases: an empty file is not a pass")
    st, asked, room = {}, [], {"v": 1}
    game_ptr = e.alloc(0x1000); e.w32(game_ptr + 0x70, 1)
    attacker = e.alloc(0x100)
    target = e.alloc(0x200)
    data = e.alloc(0x40); row = e.alloc(0x400)
    e.w32(target + 0x14, data); e.w32(data, row)
    diffrow = [e.alloc(0x80) for _ in range(3)]
    kind = {"div": 4, "maxlife": 0, "strike": 5}

    def event(e_):
        if e_.mu.reg_read(emu.UC_X86_REG_EDX) == kind["strike"] and kind.get("after") is not None: st[(target, 6)] = kind["after"]
        return 0
    e.hook(0x625480, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x625500, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0), 3)
    e.hook(0x627260, lambda e_: st.__setitem__((e_.arg(0), e_.arg(1)), s32(e_.arg(2))) or 0, 4)
    e.hook(0x625d10, lambda e_: kind["maxlife"], 1)
    e.hook(0x611d30, lambda e_: diffrow[min(e_.arg(0) & 0xff, 2)], 1)
    e.hook(0x554010, lambda e_: game_ptr, 0)
    e.hook(0x620bb0, lambda e_: room["v"], 1)
    e.hook(0x5c0c30, event, 3)
    e.hook(0x63ee90, lambda e_: 0, 1)
    e.hook(0x63e9f0, lambda e_: 1 if kind["div"] == 8 else 0, 2)
    e.hook(0x5a0180, lambda e_: 0, 0)
    e.hook(0x5738f0, lambda e_: 0, 0)
    for addr, n in ((0x63e990, 1), (0x63e940, 1), (0x63edc0, 1), (0x5347d0, 7),
                    (0x5347e0, 3), (0x61ab00, 1), (0x5541b0, 0), (0x621d50, 2), (0x639df0, 2), (0x57aa60, 0), (0x57aaa0, 0),
                    (0x57aae0, 2), (0x57af80, 2), (0x57b230, 2), (0x57ac50, 3), (0x57add0, 3), (0x5d6410, 0),
                    (0x6256b0, 2), (0x5a54f0, 2), (0x5a5650, 0), (0x5734c0, 0), (0x5a43a0, 0), (0x5a7c20, 3), (0x621e40, 3),
                    (0x57a980, 1)):
        e.hook(addr, lambda e_: 0, n)
    for addr, n in ((0x554200, 1), (0x457490, 0)):
        e.hook(addr, lambda e_: 1, n)
    e.hook(0x46c140, lambda e_: 1 if e_.mu.reg_read(emu.UC_X86_REG_EDX) == MONSTER_GH else 0, 0)
    e.hook(0x5a7e60, lambda e_: asked.append(e_.arg(1)) or 0, 3)
    e.hook(0x5809d0, lambda e_: 0, 5)
    e.hook(0x64c040, lambda e_: 0, 1)
    for c in cases:
        st.clear(); asked.clear(); kind["after"] = None
        kind["div"], kind["maxlife"], kind["strike"] = c["div"], c["maxlife"], 6 if c["missile"] else 5
        e.mu.mem_write(target + 0x20, b"\0" * 0x20)
        e.w32(target, 1); e.w32(target + 4, 60); e.w32(target + 0x10, 1)
        e.w32(target + 0x20, c["seedlo"]); e.w32(target + 0x24, c["seedhi"])
        st[(attacker, 136)] = 100
        st[(target, 6)] = st[(target, 7)] = c["life"]
        st[(target, 100)] = 1
        e.w32(attacker + 0x20, 12345); e.w32(attacker + 0x24, 678)
        crec = e.alloc(0x100); e.w32(crec + 4, 1)
        ok = (not control) and e.call(CRUSH, attacker, target, crec, (136 << 16) | 0, edx=kind["strike"])
        after = st[(target, 6)]
        crush = c["life"] - after if ok else 0
        kind["after"] = after
        e.w32(game_ptr + 0x6c, 0)
        e.w32(row + 0xa0, 0); e.w32(row + 0xa4, 0)
        st[(target, 6)] = c["life"]
        rec = e.alloc(0x100)
        e.w32(rec, 0x21 if c["missile"] else 1); e.w32(rec + 4, 1 | 4)
        e.w32(rec + 8, c["phys"]); e.w32(rec + 0x10, c["fire"]); e.w32(rec + 0x60, c["hc"])
        room["v"] = 1
        if not c["missile"]:
            e.call(REDUCE, target, rec, ecx=game_ptr, edx=attacker)     # a melee record's total +0x4c, before the strike events
        e.call(HANDLER, target, c["missile"], rec, ecx=game_ptr, edx=attacker)   # a missile's: FUN_0057c6c0 sums it (0x57c7a0)
        total = e.r32(rec + 0x4c)
        life = s32(st.get((target, 6), 0))
        if "CH_DEBUG" in __import__("os").environ: print("dbg", c["id"], e.r32(rec + 4), e.r32(rec + 8), total, st.get((target, 6)), file=sys.stderr)
        room["v"] = 0
        asked.clear()
        e.call(TAKE_RECORD, target, rec, ecx=game_ptr, edx=0)
        print(c["id"], int(MONSTER_GH in asked), e.r32(target + 0x20), e.r32(target + 0x24), life, total, crush)
    print("#", len(cases), "cases", file=sys.stderr)


def load(path, width):
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


def compare(cpath, gpath, dpath):
    cases = list(read_cases(cpath))
    g, d = load(gpath, 7), load(dpath, 5)
    if not cases:
        print("no cases: an empty file is not a pass", file=sys.stderr)
        return 1
    for name, rows in (("game", g), ("d2d", d)):
        if rows is None: return 1
        if len(rows) != len(cases):
            print(f"{name}: {len(rows)} lines against {len(cases)} cases", file=sys.stderr)
            return 1
        for c, r in zip(cases, rows):
            if r[0] != c["id"]:
                print(f"{name}: id {r[0]} where case {c['id']} was expected", file=sys.stderr)
                return 1
    gh = seed = life = flips = total = 0
    for gl, dl in zip(g, d):
        bad_gh, bad_seed, bad_life = gl[1] != dl[1], gl[2:4] != dl[2:4], gl[4] != dl[4]
        gh += bad_gh
        seed += bad_seed
        life += bad_life
        flips += bad_gh and gl[1] == 0 and dl[1] == 1
        total += bad_gh or bad_seed or bad_life
    print(f"  get-hit differs {gh}, seed differs {seed}, life differs {life}, d2d staggers where game.exe does not {flips}")
    print(f"compare: {len(cases)} cases, d2d differs from game.exe in {total}")
    return 0 if total == 0 else 1


USAGE = "usage: crush_hit.py gen N SEED | game CASES | compare CASES GAME D2D   (game takes --exe GAME.EXE and --no-crush, the control: no Crushing Blow)"

if __name__ == "__main__":
    argv, exe = sys.argv[1:], None
    if "--exe" in argv:
        k = argv.index("--exe")
        if k + 1 >= len(argv): raise SystemExit(USAGE)
        exe = argv[k + 1]; del argv[k:k + 2]
    control = "--no-crush" in argv
    if control: argv.remove("--no-crush")
    arity = {"gen": 3, "game": 2, "compare": 4}
    if not argv or arity.get(argv[0]) != len(argv): raise SystemExit(USAGE)
    if argv[0] == "gen":
        try:
            n, seed = int(argv[1]), int(argv[2])
        except ValueError:
            raise SystemExit(USAGE) from None
        gen(n, seed)
    elif argv[0] == "game": game(exe, argv[1], control)
    else: sys.exit(compare(argv[1], argv[2], argv[3]))
