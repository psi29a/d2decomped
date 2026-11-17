# SPDX-License-Identifier: GPL-3.0-or-later
"""A merc's share of its owner's kill in game.exe as the oracle: its experience, its level and its life.

    uv run python merc_levelup.py gen 100000 1 > cases.txt       # `hid level into mexp mlvl plvl blow addexp gear kind value`
    uv run python merc_levelup.py game < cases.txt > game.txt     # each case and `exp_after level_after life_after`
    ../../build/tests/test_merc_levelup --merc-levelup < cases.txt | grep -E '^[0-9]+( -?[0-9]+){13}$' > d2d.txt
    uv run python merc_levelup.py compare game.txt d2d.txt        # `N cases, M differ`; exit 1 on a difference or a bad input

A case: a merc of Hireling.txt Id `hid` (an expansion row) at merc level `level`, `into` thousandths of
the way to its next level, `gear` more max life (whole points) on its items and `addexp` percent more
experience (stat 0x55, its items'), its life `kind`/`value`; its owner at character level `plvl` kills a
monster of experience `mexp` (stat 0xd) at monster level `mlvl`, the blow the merc's (`blow` 1) or the
owner's. FUN_0057e990, the kill's experience for the killer's side, runs for real from its start through
its merc branch (0x57ea0d..0x57ea46): FUN_0057e480 (the award at the merc's level: FUN_0057e2f0,
FUN_0057e390, stat 0x55, the merc's cap FUN_0057e3f0), 86 / 256 of it unless the merc struck, then
FUN_0057e860 (the experience added while under the owner's level; a new level runs FUN_00572840, the
level-up, which sets the base max life stat 7 and the life stat 6 to it, 0x5729cd..0x5729dc).
Hooked (inputs and side effects, not the claim): the stat getters FUN_00625480 / FUN_006253b0 and the
setter FUN_00627260 (a stat table a unit), FUN_0057e7b0 (the killer's owner: the player), FUN_00574ec0
(the owner's merc), FUN_0058f0d0 (the merc's owner), FUN_00574bd0 (the Hireling.txt record: its Id),
FUN_005de330 (the level-up speech), FUN_0056deb0 (skill levels), the state lookups FUN_006256b0 /
FUN_00639df0 / FUN_0063a750 (none), FUN_005531c0 / FUN_0053bfd0 (the experience message),
FUN_005726c0 (the stats sent), FUN_00553380 (the level-up overlay), FUN_005c0c30, and the owner's own
share after the merc branch (FUN_00554630, FUN_0057e6c0, FUN_0057e510).
"""
import random
import struct
import sys

USAGE = "usage: merc_levelup.py gen N SEED | game < CASES | compare GAME D2D"
EXPANSION_VERSION = 100
HIRELING_ROW = 0x118


def s32(value):
    return struct.unpack("<i", struct.pack("<I", value & 0xffffffff))[0]


def boot():
    import drlg
    return drlg.boot()


def hirelings(e):
    """The expansion rows (0x96bdd0, 0x118 bytes a row): Id -> its first row's ExpPerLvl (+0x20)."""
    base, count = e.r32(0x96bdd0), e.r32(0x96bdd4)
    rows = {}
    for row in range(count):
        version, hid = struct.unpack_from("<Hxxi", e.read(base + row * HIRELING_ROW, 8), 0)
        if version == EXPANSION_VERSION and hid not in rows:
            rows[hid] = e.r32(base + row * HIRELING_ROW + 0x20)
    return rows


def exp_at(level, per_level):
    """The experience a merc level starts at: (L + 1) x ExpPerLvl x L^2 (FUN_00663790), 32 bits."""
    return ((level + 1) * per_level * level * level) & 0xffffffff


class Rig:
    def __init__(self, e):
        self.e = e
        self.stats = {}
        self.hid = 0
        self.game, self.player, self.merc, self.monster = e.alloc(0x400), e.alloc(0x400), e.alloc(0x400), e.alloc(0x400)
        e.w32(self.game + 0x70, 1)                                     # an expansion game
        e.w32(self.player, 0)                                          # unit type 0, a player
        e.w32(self.merc, 1)                                            # unit type 1, a monster
        e.w32(self.monster, 1)
        e.hook(0x625480, self.get, 3)
        e.hook(0x6253b0, self.get, 3)
        e.hook(0x627260, self.put, 4)
        e.hook(0x57e7b0, lambda _e: self.player, 2)
        e.hook(0x574ec0, lambda _e: self.merc, 2)
        e.hook(0x58f0d0, lambda _e: self.player, 0)
        e.hook(0x574bd0, self.record, 0)
        e.hook(0x5de330, lambda _e: 0, 3)
        e.hook(0x56deb0, lambda _e: 0, 2)
        e.hook(0x6256b0, lambda _e: 0, 2)
        e.hook(0x639df0, lambda _e: 0, 2)
        e.hook(0x63a750, lambda _e: 0, 1)
        e.hook(0x5531c0, lambda _e: 0, 4)
        e.hook(0x53bfd0, lambda _e: 0, 0)
        e.hook(0x5726c0, lambda _e: 0, 1)
        e.hook(0x553380, lambda _e: 0, 1)
        e.hook(0x5c0c30, lambda _e: 0, 3)
        e.hook(0x554630, lambda _e: 0xffff, 0)
        e.hook(0x57e6c0, lambda _e: 0, 4)
        e.hook(0x57e510, lambda _e: 0, 3)

    def get(self, e):
        return self.stats.get((e.arg(0), e.arg(1)), 0) & 0xffffffff

    def put(self, e):
        self.stats[(e.arg(0), e.arg(1))] = s32(e.arg(2))
        return 0

    def record(self, e):
        rec = e.alloc(0x20)
        e.w32(rec + 8, self.hid)
        return rec

    def run(self, hid, level, exp, mexp, mlvl, plvl, blow, addexp, life):
        """FUN_0057e990 for the kill: the merc's (experience, level, life in 256ths) after it."""
        m, p, x = self.merc, self.player, self.monster
        self.hid = hid
        self.stats = {(m, 0xc): level, (m, 0xd): exp, (m, 0x55): addexp, (m, 6): life,
                      (p, 0xc): plvl, (x, 0xc): mlvl, (x, 0xd): mexp}
        self.e.call(0x57e990, x, ecx=self.game, edx=m if blow else p)
        return self.stats[(m, 0xd)] & 0xffffffff, self.stats[(m, 0xc)], self.stats[(m, 6)]


def gen(count, seed):
    e = boot()
    rows = hirelings(e)
    ids = sorted(rows)
    rng = random.Random(seed)
    for _ in range(count):
        hid = rng.choice(ids)
        level = rng.choice((1, 2, 97, 98, rng.randint(1, 98), rng.randint(1, 98), rng.randint(1, 30)))
        into = rng.choice((0, 999, 990, rng.randint(900, 999), rng.randint(0, 999)))
        mexp = rng.choice((0, 1, rng.randint(1, 100), rng.randint(1, 20000), rng.randint(1, 0x7fffff)))
        mlvl = max(1, min(99, rng.choice((level, level + rng.randint(-12, 12), rng.randint(1, 99)))))
        plvl = rng.choice((level, level + 1, rng.randint(1, 99), min(99, level + rng.randint(1, 20))))
        blow = rng.choice((0, 1))
        addexp = rng.choice((0, 0, 0, rng.randint(1, 10), rng.randint(1, 200)))
        gear = rng.choice((0, 0, rng.randint(1, 40), rng.randint(1, 600)))
        kind = rng.choice((0, 1, 1, 2))
        value = (rng.choice((0x100, rng.randint(0x100, 0x2000), rng.randint(0x100, 0x40000))) if kind == 0
                 else rng.choice((0, 1, rng.randint(0, 0x2000))) if kind == 1
                 else rng.choice((1, rng.randint(1, 0x2000))))
        print(hid, level, into, mexp, mlvl, plvl, blow, addexp, gear, kind, value)


def game():
    e = boot()
    rows = hirelings(e)
    rig = Rig(e)
    base = {}
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        hid, level, into, mexp, mlvl, plvl, blow, addexp, gear, kind, value = (int(f) for f in field)
        per = rows[hid]
        start, end = exp_at(level, per), exp_at(level + 1, per)
        exp = start + (end - start) * into // 1000
        if (hid, level) not in base:                                   # the merc's base max life at its level: FUN_00572840
            rig.hid = hid
            rig.stats = {(rig.merc, 0xc): level - 1}
            e.call(0x572840, rig.merc, level, ecx=rig.game, edx=rig.player)
            base[(hid, level)] = rig.stats[(rig.merc, 7)]
        top = base[(hid, level)] + (gear << 8)
        life = value if kind == 0 else max(top - value, 0x100) if kind == 1 else top + value
        exp_after, level_after, life_after = rig.run(hid, level, exp, mexp, mlvl, plvl, blow, addexp, life)
        print(hid, level, exp, mexp, mlvl, plvl, blow, addexp, gear, life, base[(hid, level)], exp_after, level_after, life_after)


def rows_of(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            field = line.split()
            if not field:
                continue
            if len(field) != 14 or not all(f.lstrip("-").isdigit() for f in field):
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            out.append(tuple(int(f) for f in field))
    return out


def compare(game_path, d2d_path):
    game_rows, d2d_rows = rows_of(game_path), rows_of(d2d_path)
    if not game_rows or len(game_rows) != len(d2d_rows):
        sys.exit(f"empty or unequal: {len(game_rows)} game, {len(d2d_rows)} d2d (an empty file is not a pass)")
    differ = 0
    for g, d in zip(game_rows, d2d_rows, strict=True):
        if g[:10] != d[:10]:
            sys.exit(f"cases out of step: game {g[:10]}, d2d {d[:10]}")
        if g[10:] != d[10:]:
            differ += 1
            if differ <= 20:
                print(f"DIFF hid {g[0]} level {g[1]} exp {g[2]} mexp {g[3]} mlvl {g[4]} plvl {g[5]} blow {g[6]} addexp {g[7]} gear {g[8]} life {g[9]}: "
                      f"game base {g[10]} exp {g[11]} level {g[12]} life {g[13]}, d2d base {d[10]} exp {d[11]} level {d[12]} life {d[13]}")
    print(f"{len(game_rows)} cases, {differ} differ")
    sys.exit(1 if differ else 0)


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("gen", 3), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "gen":
        gen(int(a[1]), int(a[2]))
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
