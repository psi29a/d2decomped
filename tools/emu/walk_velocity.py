#!/usr/bin/env python3
"""Walk speed oracle: game.exe's own FUN_00623f50 against d2d's real code (upstream tools/emu style).

game side: FUN_00623f50 (a unit's mode speed) runs under unicorn (emu.py) on a fake unit in a moving
mode with a path; FUN_00621740 (the diminished stats table at 0x6e8e24, 12 bytes a row: row 4 = stat 96
faster run/walk, 150 v / (150 + v), v != 0) and FUN_00621360 (the path velocity's base: MonStats +0x32
<< 8 for a monster in ANY mode, CharStats +0x40 WalkVelocity << 8 for a player) run natively against a
fake data-table block at [0x744304] (+0xa78 MonStats rows of 0x1a8, +0xbc4 CharStats rows of 0xc4).
The velocity it hands the path (FUN_00648690, hooked: recorded) and the walk % are printed. Hooked (not
the claim): FUN_00645270 (shape / class remap: identity), FUN_00620f00, FUN_006216e0 (no special
mode), FUN_006214a0 (moving mode: yes), FUN_006213d0 (animation rate), FUN_00625480 / FUN_00625500 (stat
0x43 velocitypercent and stat 96 faster run/walk: the case's values).
Velocity is kept to 1..60 (MonStats has far less): game.exe's 32-bit Velocity x 256 x pct wraps from
2^31 on, d2d's float does not; no 1.14d row comes near.
A monster's base stat 0x43 is game.exe's own: its stat initialiser FUN_00573cb0 runs (monster_base43: the
MonStats row / difficulty lookups and the helpers it calls hooked, SetStat FUN_00627260 recorded) up to its
SetStat(0x45), and the value it set for stat 0x43 (velocitypercent) is the base: 75 (0x573f16), not 100.
Modelled, not traced (the inputs of stat 0x43, d2d's own split of it): a monster's that base + d2d speed_pct
+ boss_speed + its think's pace (move) - 100 laid + coldeffect chilled; a merc's 100 + move; a pet's
100 + move + speed_pct + its aura's velocitypercent; a player's 100 (running: RunVelocity x 100 /
WalkVelocity) + velocitypercent - 50 chilled.

d2d side: the REAL build (`test_walk_velocity --walk-velocity`, tests/game/test_walk_velocity.cpp): monster_update (one
frame), World::walk_percent, Fight::merc_turn, Fight::pet_walking; a player's walk %, the others' path
velocity (the 16.16 distance a frame moved them along x, x 4096), against the velocity game.exe set.

    uv run python walk_velocity.py gen N SEED > cases.tsv
    uv run python walk_velocity.py game cases.tsv [game.exe] > game.tsv   (default: <data dir>/bin/game.exe)
    build/tests/test_walk_velocity --walk-velocity cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv
    uv run python walk_velocity.py compare cases.tsv game.tsv d2d.tsv

cases.tsv (tab separated), kind 0 a monster: num 0 monstats-id mode(2 WL / 15 RN) difficulty velocity
coldeffect speed_pct boss_speed move laid chilled; kind 1 a player: num 1 class run frw vp chilled;
kind 2 a merc: num 2 merc-type mode velocity move; kind 3 a pet: num 3 monstats-id mode velocity move
speed_pct buff. game.tsv: num pct velocity (velocity = what FUN_00648690 gets; pct = the walk %).
"""
import random
import struct
import sys

# a spread of MonStats rows with traced AIs (d2d rules::traced_ai) and real Velocity / Run
MON_ROWS = ["zombie1", "fallen1", "skeleton1", "quillrat1", "goatman1", "brute1", "bighead1", "wraith1", "sandraider1",
            "baboon1", "fetish1", "vampire1", "arach1", "mummy1", "sandleaper1", "pantherwoman1", "scarab1", "vilemother1",
            "succubus1", "overseer1", "minion1", "imp1", "deathmauler1", "frozenhorror1", "bloodlord1", "doomknight1"]


# Hireling Ids of the four classes' first rows (hireling.txt Class 271 / 338 / 359 / 561: rogue, guard, iron wolf, barbarian)
MERC_TYPES = [0, 10, 15, 24]


def tables():
    sys.path.insert(0, ".")
    from emu import mpq_cache

    def tab(name):
        t = mpq_cache().read(name).decode("latin1").split("\r\n")
        h = t[0].split("\t")
        return h, [r.split("\t") for r in t[1:] if r]
    h, rows = tab("data\\global\\excel\\monstats.txt")
    mon = {}
    for idx, r in enumerate(rows):
        def g(key, row=r, head=h):
            return int(row[head.index(key)] or 0)
        mon[r[0]] = (idx, g("Velocity"), g("Run"), (g("coldeffect"), g("coldeffect(N)"), g("coldeffect(H)")))
    h, rows = tab("data\\global\\excel\\charstats.txt")
    chars = [(int(r[h.index("WalkVelocity")]), int(r[h.index("RunVelocity")])) for r in rows if r[0] != "Expansion"][:7]
    return mon, chars


def gen(n, seed):
    mon, chars = tables()
    rng = random.Random(seed)
    rows = []
    rows_ok = [m for m in MON_ROWS if m in mon]
    # boundaries: around the floor (sum 24/25/26 and 9/10/11 where the old floor was), both modes,
    # every difficulty, data coldeffect and synthetic ones, laid, Velocity 1 and big
    for name in rows_ok:
        idx, vel, run, cold = mon[name]
        for mode in (2, 15):
            for diff in range(3):
                for chilled in (0, 1):
                    for laid in (0, 1):
                        for target_sum in (-200, 0, 9, 10, 11, 24, 25, 26, 50, 100, 101, 150, 400):
                            ce = cold[diff]
                            move = target_sum - 100 - (ce if chilled else 0) + (100 if laid else 0)
                            rows.append((0, name, mode, diff, vel, ce, 0, 0, move, laid, chilled))
                for ce in (-100, -90, -50, -33, -1, 0, 1, 50):
                    for v in (1, 2, 3, 7, 13, 25, 60):
                        rows.append((0, name, mode, rng.randrange(3), v, ce, rng.choice((0, 20, 50)), rng.choice((0, 20)), rng.choice((0, 30, 120, -100)), 0, 1))
    for cls in range(7):
        for run in (0, 1):
            for chilled in (0, 1):
                for frw in (-300, -151, -149, -100, -1, 0, 1, 20, 40, 150, 200, 1000, 30000):
                    for vp in (-200, -100, -75, -50, -1, 0, 1, 13, 25, 50, 100, 300):
                        rows.append((1, cls, run, frw, vp, chilled))
    for name in rows_ok:                          # a merc's rows (its Hireling's MonStats) and a pet's
        idx, vel, run, cold = mon[name]
        for mode in (2, 15):
            for target_sum in (-200, 0, 9, 10, 11, 24, 25, 26, 50, 100, 101, 150, 400):
                for merc_type in MERC_TYPES if name == rows_ok[0] else MERC_TYPES[:1]:
                    rows.append((2, merc_type, mode, vel, target_sum - 100))
                split = (target_sum - 100) // 3
                rows.append((3, name, mode, vel, split, split, target_sum - 100 - 2 * split))
    n += len(rows)
    while len(rows) < n:
        pick = rng.random()
        if pick < 0.15:
            rows.append((2, rng.choice(MERC_TYPES), rng.choice((2, 15)), rng.choice((rng.randint(1, 20), rng.randint(1, 60))),
                         rng.choice((0, 0, rng.randint(-150, 150)))))
        elif pick < 0.3:
            name = rng.choice(rows_ok)
            rows.append((3, name, rng.choice((2, 15)), rng.choice((mon[name][1], rng.randint(1, 60))),
                         rng.choice((0, 0, rng.randint(-150, 150))), rng.choice((0, 0, rng.randint(-50, 100))), rng.choice((0, 0, rng.randint(-50, 100)))))
        elif pick < 0.75:
            name = rng.choice(rows_ok)
            idx, vel, run, cold = mon[name]
            diff = rng.randrange(3)
            v = vel if rng.random() < 0.6 else rng.choice((rng.randint(1, 20), rng.randint(1, 60)))
            ce = cold[diff] if rng.random() < 0.6 else rng.randint(-100, 100)
            rows.append((0, name, rng.choice((2, 15)), diff, v, ce, rng.choice((0, 0, rng.randint(-100, 200))),
                         rng.choice((0, 0, 20, rng.randint(0, 100))), rng.choice((0, 0, rng.randint(-150, 150), rng.randint(0, 120))),
                         int(rng.random() < 0.1), int(rng.random() < 0.4)))
        else:
            frw = rng.choice((0, rng.randint(0, 200), rng.randint(-400, 400), rng.randint(-2000, 5000)))
            if frw == -150:
                frw = -149
            rows.append((1, rng.randrange(7), rng.choice((0, 1)), frw, rng.choice((0, 0, rng.randint(-100, 150), rng.randint(-400, 400))), int(rng.random() < 0.3)))
    for i, r in enumerate(rows):
        print(i, *r, sep="\t")


def monster_base43(emu, monstats, rows):
    """FUN_00573cb0, game.exe's monster stat initialiser, run from its entry to its SetStat(0x45) on a fake monster unit and
    MonStats row, for every class in `rows` and the three difficulties; returns the one value it sets stat 0x43 to.
    Hooked (not the claim): the unit-type setup FUN_005739d0, the random draws FUN_0045c390, the level / difficulty block
    FUN_00573930 (level 30), FUN_0063ee90 (no special spawn), FUN_00626d40 (a stat list's set-up), and SetStat FUN_00627260
    (recorded: unit, stat, value). The fake data block's MonStats type index (+0x18) points at a zeroed type row."""
    from emu import UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_ESI, UC_X86_REG_EDX
    dt = emu.r32(0x744304)
    types = emu.alloc(0x134 * 4)
    emu.w32(dt + 0xa90, types)
    emu.w32(dt + 0xa98, 4)
    unit, data, ctx, frame = emu.alloc(0x100), emu.alloc(0x80), emu.alloc(0x100), emu.alloc(0x400)
    emu.w32(unit, 1)
    emu.w32(unit + 0x14, data)
    seen = []
    emu.hook(0x5739d0, lambda e: 0, 0)
    emu.hook(0x45c390, lambda e: 0, 0)
    emu.hook(0x63ee90, lambda e: 0, 1)
    emu.hook(0x626d40, lambda e: 0, 4)

    def level_block(e):                         # FUN_00573930(edx -> out block, 2 stack args): level 30 at +0x10, the rest zero
        out = e.mu.reg_read(UC_X86_REG_EDX)
        e.mu.mem_write(out, b"\0" * 0x24)
        e.w32(out + 0x10, 30)
        return 0
    emu.hook(0x573930, level_block, 2)
    emu.hook(0x627260, lambda e: seen.append((e.arg(1), e.arg(2))) or 0, 4)
    values = set()
    for row in rows:
        for diff in range(3):
            emu.mu.mem_write(ctx + 0x6d, bytes([diff]))
            emu.w32(ctx + 0x70, 0)
            emu.w32(unit + 4, monstats[row])
            emu.w32(data, monstats["row_ptr"] + monstats[row] * 0x1a8)
            seen.clear()
            ebp = frame + 0x200
            emu.w32(ebp + 8, ctx); emu.w32(ebp + 0xc, 0); emu.w32(ebp + 0x10, 0)
            emu.mu.reg_write(UC_X86_REG_EBP, ebp)
            emu.mu.reg_write(UC_X86_REG_ESP, ebp - 0x80)
            emu.mu.reg_write(UC_X86_REG_ESI, unit)
            emu.mu.emu_start(0x573cb3, 0x573f3e)            # after `push ebp; mov ebp, esp` the frame is ours, to just past SetStat(0x45)
            got = [v for (s, v) in seen if s == 0x43]
            assert len(got) == 1, (row, diff, seen)
            values.add(got[0])
    assert len(values) == 1, values
    return values.pop()


def tdiv(a, b):
    """C / (x86 idiv): truncates toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def game(path, exe=None):
    mon, chars = tables()
    from emu import Emu
    emu = Emu(exe)
    nmon = max(v[0] for v in mon.values()) + 1
    dt = emu.alloc(0x1000)
    monstats = emu.alloc(0x1a8 * nmon)
    charstats = emu.alloc(0xc4 * 7)
    emu.w32(0x744304, dt)
    emu.w32(dt + 0xa78, monstats)
    emu.w32(dt + 0xa80, nmon)
    emu.w32(dt + 0xbc4, charstats)
    emu.w32(dt + 0xbc8, 7)
    for c, (walk, _run) in enumerate(chars):
        emu.mu.mem_write(charstats + c * 0xc4 + 0x40, bytes([walk & 0xFF]))
    base43 = monster_base43(emu, {**{name: v[0] for name, v in mon.items()}, "row_ptr": monstats}, [name for name in MON_ROWS if name in mon])
    sys.stderr.write(f"monster base stat 0x43 (FUN_00573cb0): {base43}\n")
    unit = emu.alloc(0x200)
    path_ = emu.alloc(0x100)
    st = {}
    got = {}
    emu.hook(0x645270, lambda e: 0, 6)
    emu.hook(0x620f00, lambda e: 0, 4)
    emu.hook(0x6216e0, lambda e: 0, 0)
    emu.hook(0x6214a0, lambda e: 1, 1)
    emu.hook(0x6213d0, lambda e: 256, 0)
    emu.hook(0x625480, lambda e: st.get(e.arg(1), 0) & 0xFFFFFFFF, 3)
    emu.hook(0x625500, lambda e: st.get(e.arg(1), 0) & 0xFFFFFFFF, 3)

    def setvel(e):
        got["vel"] = struct.unpack("<i", struct.pack("<I", e.arg(1)))[0]
        return 0

    emu.hook(0x648690, setvel, 4)
    for line in open(path):
        f = line.split()
        if not f:
            continue
        cid, kind = int(f[0]), int(f[1])
        st.clear()
        if kind in (0, 2, 3):
            if kind == 0:
                name, mode, _diff, vel, ce, speed, boss, move, laid, chilled = f[2], *map(int, f[3:])
                st[0x43] = base43 + speed + boss + move - (100 if laid else 0) + (ce if chilled else 0)
                idx = mon[name][0]
            elif kind == 2:
                mode, vel, move = map(int, f[3:6])
                st[0x43] = 100 + move
                idx = 0
            else:
                name, mode, vel, move, speed, buff = f[2], *map(int, f[3:])
                st[0x43] = 100 + move + speed + buff
                idx = mon[name][0]
            emu.mu.mem_write(monstats + idx * 0x1a8 + 0x32, struct.pack("<h", vel))
            emu.w32(unit, 1)
            emu.w32(unit + 4, idx)
            emu.w32(unit + 0x10, mode)
            st[96] = 0
        else:
            cls, run, frw, vp, chilled = map(int, f[2:])
            walk, runv = chars[cls]
            emu.w32(unit, 0)
            emu.w32(unit + 4, cls)
            emu.w32(unit + 0x10, 3 if run else 2)
            st[0x43] = (runv * 100 // walk if run else 100) + vp + (-50 if chilled else 0)
            st[96] = frw
        emu.w32(unit + 0x2c, path_)
        got.clear()
        emu.call(0x623f50, unit, 0, 0)
        vel = got.get("vel")
        base = (int(f[{0: 5, 2: 4, 3: 4}[kind]]) if kind != 1 else chars[int(f[2])][0]) << 8
        # self-check of the derivation: the walk % as read from the disassembly (FUN_00621740's
        # 150 v / (150 + v) for v != 0, idiv truncating, + stat 0x43, at least 25) must give the
        # velocity game.exe set (base x pct / 100, truncated toward zero)
        frw_eff = tdiv(150 * st[96], 150 + st[96]) if st[96] else 0
        pct = max(frw_eff + st[0x43], 25)
        print(cid, pct, vel, "" if vel == tdiv(base * pct, 100) else "VELFAIL")


def compare(cases, a, b):
    rows = {l.split()[0]: l.split() for l in open(cases) if l.strip()}
    ga = {l.split()[0]: l.split() for l in open(a) if l.strip()}
    gb = {l.split()[0]: l.split() for l in open(b) if l.strip()}
    velfail = sum(1 for v in ga.values() if "VELFAIL" in v)
    skipped = [k for k, v in gb.items() if v[1] == "skip"]
    names = {"0": "monster", "1": "player", "2": "merc", "3": "pet"}
    bad, per, total, wrapped = [], {}, {}, 0
    compared = 0
    for k, r in rows.items():
        if k not in gb or gb[k][1] == "skip" or k not in ga:
            continue
        compared += 1
        # a player: the walk %; the others: the path velocity (d2d's frame moves velocity / 4096 subtiles)
        g_pct, d_pct = (int(ga[k][1]), float(gb[k][1])) if r[1] == "1" else (int(ga[k][2]), float(gb[k][1]))
        key = names[r[1]] + ("" if r[1] == "1" else " RN" if r[3] == "15" else " WL")
        total[key] = total.get(key, 0) + 1
        # from 0x8000 the frame's step (velocity x 16 x 4096, FUN_006502d0) wraps in 32 bits, in game.exe as in d2d
        # (tools/emu/walk_step.py has the step): the distance moved is no velocity then; no 1.14d row comes near
        if r[1] != "1" and g_pct >= 0x8000:
            wrapped += 1
        elif abs(g_pct - d_pct) > 0.01:
            bad.append(k)
            per[key] = per.get(key, 0) + 1
    print(f"cases {len(rows)} compared {compared} skipped {len(skipped)} mismatches {len(bad)} velfail {velfail}")
    print("  per kind (cases, mismatches):", {k: (total[k], per.get(k, 0)) for k in sorted(total)})
    print(f"  step-wrapped {wrapped} (velocity 0x8000 or more: not compared)")
    for k in bad[:12]:
        print("  ", " ".join(rows[k]), "game", ga[k][1], "d2d", gb[k][1])
    from collections import Counter
    print("  skip reasons", Counter(" ".join(gb[k][1:]) for k in skipped).most_common(5))
    # a case d2d neither answered nor skipped would pass unseen (an empty d2d file compares nothing)
    missing = [k for k in rows if not (k in gb and gb[k][1] == "skip") and (k not in ga or k not in gb)]
    if missing:
        print(f"  missing results for {len(missing)} cases, first: {missing[:5]}")
    return 1 if bad or velfail or missing or compared == wrapped else 0   # none compared (all wrapped) is no pass


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]) & 0xFFFFFFFF)
    elif cmd == "game":
        game(*sys.argv[2:4])
    elif cmd == "compare":
        sys.exit(compare(*sys.argv[2:5]))
    else:
        sys.exit(__doc__)
