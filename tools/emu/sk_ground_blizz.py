# SPDX-License-Identifier: GPL-3.0-or-later
"""SKGROUND: where Blizzard's shards fall, by game.exe's own code.

    python sk_ground_blizz.py gen N SEED > cases     # grid + N random cases
    python sk_ground_blizz.py game cases > game.tsv  # FUN_005a9820 natively under the emulator
    python sk_ground_blizz.py cmp cases game.tsv     # vs the reading (port) and vs d2d's rule
    python sk_ground_blizz.py d2d cases game.tsv d2d.txt   # a real d2d build's shards vs game.exe's offsets

Runs next to upstream's tools/emu (emu.py), like sk_resist.py.

FUN_005a9820 is the shard spawner of missile srvdofunc 10 (FUN_005aea60,
blizzardcenter; also do 25): EAX = calc2 (frequency), ESI = the centre
missile, stack (game, calc1 radius, SubMissile1 row, collision mask 5).
Native: the seed set FUN_00650e40, the draws FUN_0045c3e0, the memset.
Hooked (not the claim): the age FUN_0064a3b0 (Range - Range left), the owner
FUN_00552fd0, the room FUN_00620bb0, the centre's subtile FUN_0045adf0 /
FUN_0045ae20, skill / level FUN_0064a280 / FUN_0064a210, the spot's
collision FUN_0064cb30 (always free here) and the missile maker FUN_0059fa30
(records the params block's start subtile, makes nothing).

A case is `id cell_x cell_y age freq radius`. `game` prints `id made x y` (made 0:
no shard this frame). `cmp` checks every line against
  reading: made = age % freq == 0; seed {cell_x + age, 666}; r = radius - 1;
           x = cell_x + rand(2r) - r, y = cell_y + rand(2r) - r (x drawn first);
           rand(n) = 0 for n < 1, else the seed step's low % n
  d2d:     Fight::missile_tick (components/game/fight/missiles.cpp, do 10 / 25):
           reach = radius - 1; offset = rng(2 reach) - reach on each axis, the fight's Rng
           (-(radius - 1)..(radius - 2); before the fix rng(2 radius - 1) - (radius - 1),
           -(radius - 1)..(radius - 1)).
d2d's draw is on another generator, so the d2d check is on the RANGE of the
offsets only (values game.exe can never produce), not draw for draw.
"""
import os
import sys

sys.path.insert(0, os.getcwd())

MASK = 0xFFFFFFFF


def xorshift(state):
    x = state[0]
    x ^= (x << 13) & MASK
    x ^= x >> 17
    x ^= (x << 5) & MASK
    state[0] = x & MASK
    return state[0]


def gen(n, seed):
    st = [seed & MASK or 1]
    i = 0
    for radius in (1, 2, 3, 7, 8, 12, 20):
        for age in range(0, 100):
            for cell_x, cell_y in ((5000, 5000), (5123, 4877), (25000, 5600)):
                print(i, cell_x, cell_y, age, 4, radius)
                i += 1
    for _ in range(n):
        cell_x, cell_y = 1000 + xorshift(st) % 60000, 1000 + xorshift(st) % 60000
        age, freq = xorshift(st) % 100, 1 + xorshift(st) % 6
        radius = (7, 7, 7, 1 + xorshift(st) % 25)[xorshift(st) % 4]
        print(i, cell_x, cell_y, age, freq, radius)
        i += 1


def cases(path):
    with open(path) as f:
        for ln in f:
            p = ln.split()
            if len(p) == 6:
                yield tuple(int(v) for v in p)


def game(path):
    import emu
    from unicorn.x86_const import UC_X86_REG_EDX
    e = emu.Emu()
    cur = {}
    made = []

    def ret(v):
        return lambda _e: v
    e.hook(0x64a3b0, lambda _e: cur["age"], 1)
    e.hook(0x552fd0, ret(0x1000), 0)
    e.hook(0x620bb0, ret(0x2000), 1)
    e.hook(0x45adf0, lambda _e: cur["cell_x"], 0)
    e.hook(0x45ae20, lambda _e: cur["cell_y"], 0)
    e.hook(0x64a280, ret(59), 1)
    e.hook(0x64a210, ret(20), 1)
    e.hook(0x64cb30, ret(0), 4)

    def on_make(e):
        p = e.mu.reg_read(UC_X86_REG_EDX)
        made.append((e.s32(p + 0x14), e.s32(p + 0x18), e.r32(p + 0x10)))
        return 0
    e.hook(0x59fa30, on_make, 0)
    unit = e.alloc(0x200)
    e.w32(unit, 3)                                   # a missile unit
    for cid, cell_x, cell_y, age, freq, radius in cases(path):
        cur.update(age=age, cell_x=cell_x, cell_y=cell_y)
        made.clear()
        e.call(0x5a9820, 1, radius, 159, 5, regs={"eax": freq, "esi": unit})
        if made:
            print(cid, 1, made[0][0], made[0][1])
        else:
            print(cid, 0, "-", "-")


def rand(seed, n):
    lo, hi = seed
    v = lo * 0x6AC690C5 + hi
    seed[0], seed[1] = v & MASK, (v >> 32) & MASK
    if n < 1:
        return 0
    return seed[0] % n


def reading(cell_x, cell_y, age, freq, radius):
    if freq == 0 or age % freq:
        return (0, None, None)
    seed = [(cell_x + age) & MASK, 666]
    r = radius - 1
    dx = rand(seed, 2 * r) - r
    dy = rand(seed, 2 * r) - r
    return (1, cell_x + dx, cell_y + dy)


def cmp(cpath, gpath):
    g = {}
    with open(gpath) as f:
        for ln in f:
            p = ln.split()
            if len(p) == 4 and p[0].isdigit():
                g[int(p[0])] = (int(p[1]), None if p[2] == "-" else int(p[2]), None if p[3] == "-" else int(p[3]))
    n = bad = made = outside_d2d = 0
    d2d_only = {}
    seen_off = {}
    for cid, cell_x, cell_y, age, freq, radius in cases(cpath):
        n += 1
        got = g.get(cid)
        want = reading(cell_x, cell_y, age, freq, radius)
        if got != want:
            bad += 1
            if bad <= 5:
                print("reading differs", cid, got, want)
        if got and got[0]:
            made += 1
            dx, dy = got[1] - cell_x, got[2] - cell_y
            lo, hi = -(radius - 1), max(radius - 2, 0)  # d2d: rng(2R-2) - (R-1); R = 1 draws nothing
            if not (lo <= dx <= hi and lo <= dy <= hi):
                outside_d2d += 1
            s = seen_off.setdefault(radius, set())
            s.add(dx)
            s.add(dy)
    for radius, s in sorted(seen_off.items()):
        d2d_vals = set(range(-(radius - 1), max(radius - 1, 1)))
        d2d_only[radius] = sorted(d2d_vals - s)
        print("radius %d: game.exe offsets %d..%d (%d values); d2d %d..%d (%d values); d2d-only %s"
              % (radius, min(s), max(s), len(s), -(radius - 1), max(radius - 2, 0), len(d2d_vals), d2d_only[radius]))
    print("cases %d, shards made %d, reading differs %d, game offset outside d2d's range %d" % (n, made, bad, outside_d2d))
    for radius in (7,):
        if radius in seen_off:
            k = 2 * radius - 1
            print("radius %d: share of d2d shards with an offset game.exe never makes: 0 now; "
                  "before the fix (offsets -(R-1)..(R-1)): 1 - (%d/%d)^2 = %.4f"
                  % (radius, k - 1, k, 1 - ((k - 1) / k) ** 2))


def d2d(cpath, gpath, dpath):
    """d2d.txt = `test_sk_ground --blizz SEED N` (lines `S<case> <frame> <dx> <dy>`, radius 7 = Blizzard).
    game.exe's offset set at radius 7 comes from the `game` run of the same cases file (made shards only)."""
    g = {}
    with open(gpath) as f:
        for ln in f:
            p = ln.split()
            if len(p) == 4 and p[0].isdigit() and p[1] == "1":
                g[int(p[0])] = (int(p[2]), int(p[3]))
    allowed = set()
    for cid, cell_x, cell_y, _age, _freq, radius in cases(cpath):
        if radius == 7 and cid in g:
            allowed.add(g[cid][0] - cell_x)
            allowed.add(g[cid][1] - cell_y)
    n = bad = 0
    made = set()
    per_cast = {}
    with open(dpath) as f:
        for ln in f:
            p = ln.split()
            if len(p) == 4 and p[0].startswith("S"):
                n += 1
                dx, dy = int(p[2]), int(p[3])
                made.update((dx, dy))
                per_cast[p[0]] = per_cast.get(p[0], 0) + 1
                if dx not in allowed or dy not in allowed:
                    bad += 1
    print("game.exe offsets at radius 7 (%d): %d..%d" % (len(allowed), min(allowed), max(allowed)))
    print("d2d offsets (%d): %d..%d; never made by d2d: %s" % (len(made), min(made), max(made), sorted(allowed - made)))
    print("d2d shards %d in %d casts; shards with an offset game.exe never makes: %d (%.2f%%)"
          % (n, len(per_cast), bad, 100.0 * bad / max(n, 1)))
    print("MISMATCH %d" % bad)


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else ""
    if what == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif what == "game":
        game(sys.argv[2])
    elif what == "cmp":
        cmp(sys.argv[2], sys.argv[3])
    elif what == "d2d":
        d2d(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(__doc__)
