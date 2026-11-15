# SPDX-License-Identifier: GPL-3.0-or-later
"""A player's walk to a clicked warp's tile unit, in game.exe (the oracle of tests/game/test_warp_walk.cpp).

    warp_walk_path.py game DUMP > game.tsv     # DUMP = test_warp_walk --dump SEEDS FILE
    warp_walk_path.py compare DUMP game.tsv [--outcome-max N] [--outcome-known LEVEL:WARP[-WARP],..]
        # per start: the plan World::tick made against game.exe's first path, and whether the warp was taken against the game.exe walk model;
        # exit 1 on a plan that differs, a start missing or listed twice, a start that left the window, or more than N outcome differences
        # (those at the --outcome-known level:warp pairs are listed and not counted)

What runs for real (game.exe 1.14d under unicorn): FUN_00649970 (the path request) with
FUN_006498a0 (the end = the tile unit's own subtile: a static path's +0xc / +0x10, type 5),
FUN_00648cf0 (path type 7), FUN_00648120 and FUN_00648050 (a blocked end walked back, a
player's push-on), FUN_00679ed0 (the player's pather: toward, then the search), FUN_0064fe40,
FUN_00650840 (a walker's frame) and FUN_00641530 (the unit distance).
Hooked (the oracle's inputs, not claims): FUN_0064d910 and FUN_0064ff90 answer from the
dump's blocked bits (the same bits d2d's player walk asks), FUN_00463740 (the room lookup:
always a room), FUN_00649560 / FUN_00649400 (stamps: none), FUN_00620510 (unit size: the
player 2, a tile unit 0, as docs/research/re/drlg.md reads it), FUN_006503f0 (the chase
check: go on; a tile unit never moves).
Not shown by the oracle: it builds the path record with the tile unit as the target (type 5) itself; that a click
reaches game.exe as that walk is the read of FUN_00548b00 / FUN_00548a50 / FUN_0057f190 (docs/research/re/collision.md).

The walk model (read in FUN_00548b00 case 5, FUN_00548a50, FUN_0057f190, FUN_00580c20, the
fork's collision.md "The pending interact's fields"): a walk is a path request plus frames
until FUN_00650840 returns 0; when it ends, FUN_00548b00 asks the unit distance (taken at 4
or less); out of reach it makes ONE more walk (pending +0x150), then goes idle.
"""
import collections
import sys

try:
    import emu
except ImportError:     # compare mode needs no emulator
    emu = None

BASE = 1 << 14                           # the walk's subtile frame: the unit at BASE, BASE


class Rig:
    def __init__(self):
        self.e = e = emu.Emu()
        self.bits = None
        self.ux = self.uy = 0
        self.outside = 0
        e.hook(0x64d910, self._wall5, 5)
        e.hook(0x64ff90, self._wall_frame, 2)
        e.hook(0x463740, lambda cur: 1, 1)
        e.hook(0x649560, lambda cur: 0, 2)
        e.hook(0x649400, lambda cur: 0, 1)
        e.hook(0x620510, lambda cur: 2 if cur.r32(cur.arg(0)) == 0 else 0, 1)
        e.hook(0x6503f0, lambda cur: 1, 0)
        self.mark = e.brk

    def blocked(self, x, y):
        gx, gy = x - BASE + self.half, y - BASE + self.half
        if not (0 <= gx < self.side and 0 <= gy < self.side):
            self.outside += 1
            return True                    # off the window: counted, answered blocked
        return self.bits[gy * self.side + gx] == "1"

    def _wall5(self, cur):
        return int(self.blocked(cur.arg(1) & 0xffff, cur.arg(2) & 0xffff))

    def _wall_frame(self, cur):
        v = cur.arg(0)
        return 0 if self.blocked(v & 0xffff, v >> 16) else 1

    # -- one case ---------------------------------------------------------------
    def setup(self, bits, side):
        e = self.e
        e.brk = self.mark
        self.bits, self.side, self.half = bits, side, side // 2
        self.unit, self.path = e.alloc(0x100), e.alloc(0x200)
        self.tile, self.tile_path = e.alloc(0x100), e.alloc(0x40)
        e.w32(self.tile, 5); e.w32(self.tile + 0x2c, self.tile_path)
        e.w32(self.tile_path + 0xc, BASE); e.w32(self.tile_path + 0x10, BASE)
        e.w32(self.unit, 0); e.w32(self.unit + 0x2c, self.path)
        p = self.path
        room = e.alloc(0x100)               # FUN_00647fb0 tests every point against the room's subtile rectangle: all of the window
        e.w32(room + 0x4c, 0); e.w32(room + 0x50, 0); e.w32(room + 0x54, 0x10000); e.w32(room + 0x58, 0x10000)
        e.w32(p + 0x30, self.unit); e.w32(p + 0x1c, room); e.w32(p + 0x58, self.tile)
        e.w32(p + 0x50, 0x1c09); e.w32(p + 0x48, 0)
        e.mu.mem_write(p + 0x91, bytes((0x49, 0x46)))
        e.w32(p + 0x5c, 5)

    def place(self, fx, fy, velocity):
        e, p = self.e, self.path
        e.w32(p + 0, fx & 0xffffffff); e.w32(p + 4, fy & 0xffffffff)
        e.w32(p + 0x7c, velocity)

    def pos(self):
        return self.e.r32(self.path), self.e.r32(self.path + 4)

    def request(self):
        e, p = self.e, self.path
        e.call(0x648cf0, p, 7)
        e.w32(p + 0x10, 0)               # FUN_006498a0 sets it from the tile unit
        n = e.call(0x649970, p, self.unit, 0, limit=2000000) & 0xffffffff
        pts = [(e.r32(p + 0x9c + 4 * i) & 0xffff, e.r32(p + 0x9c + 4 * i) >> 16) for i in range(e.r32(p + 0x28))]
        return n, pts

    def frames(self):
        e = self.e
        for k in range(600):
            if not e.call(0x650840, self.unit, 0x400, limit=2000000) & 1: return k
        return -1

    def distance(self):
        return self.e.call(0x641530, self.unit, self.tile) & 0xffffffff


def parse(path):
    """The dump's lines: G (window bits, per start), D / F / T (plans), W (outcome) and the start list (the D lines)."""
    grids, starts = {}, []
    for line in open(path):
        f = line.split()
        if not f: continue
        if f[0] == "G":
            side = round((len(f[8]) * 4) ** 0.5)                  # the window is a square of side x side subtiles, 4 bits per hex digit
            raw = bin(int("1" + f[8], 16))[3:][:side * side]     # (a leading 1 keeps the zeros)
            grids[tuple(f[1:5])] = (int(f[5]), int(f[6]), int(f[7]), raw, side)
        elif f[0] == "D": starts.append(f)
    return grids, starts


def game(dump):
    grids, starts = parse(dump)
    rig = Rig()
    for f in starts:
        seed, level, warp, d = f[1:5]
        ux, uy, vel, bits, side = grids[(seed, level, warp, d)]
        sx, sy = int(f[5]), int(f[6])        # 16.16 subtiles, absolute
        rig.setup(bits, side)
        rig.outside = 0
        fx, fy = sx - (ux << 16) + (BASE << 16), sy - (uy << 16) + (BASE << 16)
        v = (vel << 8) * 100 // 100
        row = [seed, level, warp, d]
        walks = []
        taken = 0
        first = []
        for walk in (1, 2):
            if walk == 1: rig.place(fx, fy, v)
            n, pts = rig.request()
            if walk == 1: first = pts
            steps = rig.frames() if n else 0
            fx, fy = rig.pos()
            dist = rig.distance()
            walks.append((n, steps, fx, fy, dist))
            if dist <= 4: taken = walk; break
        pts1 = " ".join(f"{a - BASE + ux},{b - BASE + uy}" for a, b in first)
        w = ";".join(f"{n}/{s}/{(x >> 16) - BASE + ux},{(y >> 16) - BASE + uy}/{dd}" for n, s, x, y, dd in walks)
        print("\t".join(map(str, row + [taken, len(first), pts1, w, rig.outside])))


def read_dump(path):
    """Per start key (seed, level, warp, dir): D (the unit plan, as the harness plans it), F (the spot plan), T (the plan World::tick made) and W (taken)."""
    plans = {"D": {}, "F": {}, "T": {}}
    taken = {}
    seen = collections.Counter()
    for line in open(path):
        f = line.split()
        if not f: continue
        if f[0] in plans:
            key = tuple(f[1:5])
            plans[f[0]][key] = f[8:] if f[0] != "T" else f[6:]
            if f[0] == "D": seen[key] += 1
        elif f[0] == "W" and f[6] != "-1":
            taken[(f[1], f[2], f[3], f[5])] = int(f[6])
    return plans, taken, seen


TOWNS = {1, 40, 75, 103, 109}      # a town's walk meets NPCs (World::tick's crowd); the oracle has no crowd


def known_pairs(spec):
    """'92:0,81:0-3' -> {('92', '0'), ('81', '0'), ..., ('81', '3')}"""
    pairs = set()
    for item in filter(None, spec.split(",")):
        level, warps = item.split(":")
        low, _, high = warps.partition("-")
        pairs |= {(level, str(w)) for w in range(int(low), int(high or low) + 1)}
    return pairs


def compare(dump, gamefile, outcome_max=0, known=frozenset()):
    """d2d against game.exe, per start. Exit 1 if a start is missing on either side or listed twice, no start was compared,
    a start left the oracle's window, a plan differs (outside a town), or the outcomes differ in more than outcome_max starts (not counting the known level/warp pairs)."""
    plans, taken, seen = read_dump(dump)
    g = {}
    dup = 0
    for line in open(gamefile):
        c = line.rstrip("\n").split("\t")
        dup += tuple(c[:4]) in g
        g[tuple(c[:4])] = c
    dup += sum(1 for n in seen.values() if n > 1)
    missing = sorted(set(plans["D"]) ^ set(g))
    seen_plan = collections.Counter()      # how each start's plan was judged
    passed = collections.Counter()
    bad, outside = [], 0
    outcome = collections.Counter()
    odiff, odiff_known = [], []
    spot = 0
    for key, c in sorted(g.items()):
        if key not in plans["D"]: continue
        gp = c[6].split() if c[6] else []
        spot += plans["F"][key] == gp
        t = plans["T"].get(key)
        k = len(gp) - len(t) if t is not None else -1
        if t == [] and gp:
            seen_plan["not visible"] += 1             # the tick left no path (the walker passed it, or the first step is blocked and the walk ends in the tick): nothing to compare
        elif t is not None and 0 <= k <= 2 and gp[k:] == t:
            seen_plan["equal"] += 1
            seen_plan["equal, spot plan differs"] += plans["F"][key] != gp
            passed[k] += 1
        elif int(key[1]) in TOWNS:
            seen_plan["town"] += 1                     # NPCs in the walk's crowd
        else:
            seen_plan["differs"] += 1
            bad.append((key, t, gp))
        outside += int(c[8]) != 0
        model = min(int(c[4]), 1)
        outcome[(taken[key], model)] += 1
        if taken[key] != model:
            (odiff_known if (key[1], key[2]) in known else odiff).append((key, taken[key], model))
    n = seen_plan["equal"] + seen_plan["differs"] + seen_plan["not visible"] + seen_plan["town"]
    for key, t, gp in bad[:8]: print("PLAN DIFFERS", key, "d2d tick", None if t is None else t[:8], "game", gp[:8])
    for key, d2d, model in odiff[:8]: print("OUTCOME DIFFERS", key, "d2d taken", d2d, "game.exe model", model)
    print(f"starts: {n} (each once on both sides; {len(missing)} missing, {dup} twice, {outside} touching the window edge)")
    print(f"the plan World::tick made against game.exe's path (FUN_00649970): {seen_plan['equal']} equal ({seen_plan['equal, spot plan differs']} of them where a walk to a spot"
          f" would differ), {seen_plan['differs']} differ; not compared: {seen_plan['not visible']} with no path left after the tick, {seen_plan['town']} in a town (NPCs); of {n}")
    print(f"  (first points the walker had passed in the first tick: {dict(sorted(passed.items()))}; a walk to a spot, the plan before this change, equals game.exe in {spot})")
    print(f"outcome: d2d takes {outcome[(1, 0)] + outcome[(1, 1)]}, the game.exe model takes {outcome[(0, 1)] + outcome[(1, 1)]},"
          f" differ in {len(odiff)} starts (allowed {outcome_max}) and in {len(odiff_known)} more at the known level:warp pairs")
    ok = n > 0 and not missing and not dup and not outside and not bad and len(odiff) <= outcome_max
    print("OK" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1] == "game": game(sys.argv[2])
    elif sys.argv[1] == "compare":
        args = sys.argv[4:]
        option = lambda name, default: args[args.index(name) + 1] if name in args else default
        sys.exit(compare(sys.argv[2], sys.argv[3], int(option("--outcome-max", 0)), known_pairs(option("--outcome-known", ""))))
    else: sys.exit(__doc__)
