# SPDX-License-Identifier: GPL-3.0-or-later
"""A plain monster's unit seed after its stat init, by game.exe 1.14d's own FUN_00573cb0.

    uv run python unit_seed_init.py gen [N] [SEED] > cases.tsv     # every row x difficulty, then N random (default 2000)
    uv run python unit_seed_init.py game cases.tsv > game.tsv      # game.exe, needs D2_MPQ_DIR
    build/tests/test_unit_seed_init --unit-seed cases.tsv > d2d.tsv
    diff game.tsv d2d.tsv
    uv run python unit_seed_init.py inc cases.tsv game.tsv > ../../tests/game/unit_seed_init_cases.inc
    uv run python unit_seed_init.py gen-region [N] [SEED] > region.tsv  # the same in a level region (FUN_00547bc0 first)
    uv run python unit_seed_init.py game region.tsv > game-region.tsv
    uv run python unit_seed_init.py inc-region region.tsv game-region.tsv > ../../tests/game/unit_seed_init_region_cases.inc

What a monster's first think draws from is its unit seed (+0x20 / +0x24) as
the stat init left it. A case: `id monstats_id row difficulty seed`, the seed
the unit was made with ({seed, 666}, FUN_00555230 -> FUN_00650e40). game.exe
runs FUN_00573cb0 on a synthetic unit with no region (its third argument 0:
FUN_005739d0 rolls each layer), as boss_level.py's plain case does, but with
these run for real: the look FUN_005739d0, rows 0x137 / 0x138's rand(4), the
life roll, the MonProp row (FUN_00573470, its chance draws and FUN_0065fd70)
and the MonEquip walk FUN_005d6b60 / FUN_005d30a0. Hooked as well as
boss_level.py's (not the claim): FUN_00536b20 and FUN_0063abd0 (a
monster's inventory, unit +0x60: a marker, so the walk runs), FUN_0063bde0 (no body slot is filled) and
FUN_00573b20 (makes the item: none) and FUN_0065cbf0 (a MonProp
stat's list: a marker; what goes in it is not the claim).

Output `id | seed_low seed_high life256 look`: the seed after the init, stat 7
and the sixteen component bytes (unit data +4) in hex.

A region case adds `others present sets_seed`: the level's region (FUN_0061a1b0
hooked to level 0) holds `others` rows from 0x40 on, then the case's row with
three sets FUN_005bdb20 rolls on {sets_seed, 666} when `present`. FUN_00547bc0
runs for real on the unit (its join rolls the row's sets on the unit seed),
then FUN_00573cb0 with the entry it gives. Output `id | count sets ...`: the
region's rows after and the row's sets, then as above.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import boss_level  # noqa: E402

INVENTORY = 0x4E000000
STATLIST = 0x4D000000


class Game(boss_level.Game):
    def __init__(self):
        super().__init__(area=False, real=(0x5739d0, 0x573470, 0x5d6b60))
        e = self.e
        e.hook(0x536b20, lambda e: INVENTORY, 0)
        e.hook(0x63abd0, lambda e: e.w32(e.arg(1) + 0x60, INVENTORY) or INVENTORY, 2)   # the unit's inventory (+0x60)
        e.hook(0x63bde0, lambda e: 0, 2)
        e.hook(0x573b20, lambda e: 0, 3)
        e.hook(0x65cbf0, lambda e: STATLIST, 2)   # a MonProp stat's list (d2d_stat_param_check): a marker

    def region(self, row, others, present, sets_seed):
        """A level region (record +0x12 its count, +0x14 its 0x34-byte entries) holding `others`
        rows (0x40.., not `row`), then `row` itself when `present` with its sets rolled by
        FUN_005bdb20 on {sets_seed, 666}; the game's regions by level id with it at level 0
        (FUN_0061a1b0 is hooked to 0)."""
        e = self.e
        record = self.region_record = getattr(self, "region_record", None) or e.alloc(0x14 + 14 * 0x34)
        regions = self.regions = getattr(self, "regions", None) or e.alloc(0x10)
        e.mu.mem_write(record, b"\0" * (0x14 + 14 * 0x34))
        e.w32(regions, record)
        types = [t for t in range(0x40, 0x40 + others + 1) if t != row][:others] + ([row] if present else [])
        for i, t in enumerate(types):
            e.mu.mem_write(record + 0x14 + i * 0x34, (t & 0xFFFF).to_bytes(2, "little"))
        e.mu.mem_write(record + 0x12, bytes([len(types)]))
        if present:
            tmp = e.alloc(8) if not hasattr(self, "tmp_seed") else self.tmp_seed
            self.tmp_seed = tmp
            e.w32(tmp, sets_seed); e.w32(tmp + 4, 666)
            e.call(0x5bdb20, tmp, record + 0x14 + (len(types) - 1) * 0x34, 3)
        return regions

    def init_region(self, row, diff, seed, others, present, sets_seed):
        """FUN_00547bc0 for real on that region (ECX the regions, EDX a room, the unit), then the
        stat init on the entry it gives: the region's count after, and init's answer."""
        e, u = self.e, self.unit
        regions = self.region(row, others, present, sets_seed)
        e.mu.mem_write(u, b"\0" * 0x100)
        e.w32(u, 1); e.w32(u + 4, row); e.w32(u + 0x20, seed); e.w32(u + 0x24, 666)
        entry = e.call(0x547bc0, u, ecx=regions, edx=0x1000)
        count = e.mu.mem_read(self.region_record + 0x12, 1)[0]
        sets = e.mu.mem_read(entry + 3, 1)[0] if entry else 0
        low, high = e.r32(u + 0x20), e.r32(u + 0x24)
        return [count, sets] + self.init(row, diff, (low, high), entry)

    def init(self, row, diff, seed, entry=0):
        e, u = self.e, self.unit
        e.mu.mem_write(u, b"\0" * 0x100); e.mu.mem_write(self.data, b"\0" * 0x40)
        e.mu.mem_write(self.game + 0x6d, bytes([diff]))
        e.w32(u, 1); e.w32(u + 4, row); e.w32(u + 0x14, self.data); e.w32(u + 0x80, self.game)
        low, high = seed if isinstance(seed, tuple) else (seed, 666)
        e.w32(u + 0x20, low); e.w32(u + 0x24, high)
        e.w32(self.data, self.rec(row))
        self.stats.clear(); self.hit.clear()
        self.level_now = self.level(row, diff)
        self.area_now = 0
        e.call(0x573cb0, self.game, 0, entry, regs={"esi": u})
        look = bytes(e.mu.mem_read(self.data + 4, 16)).hex()
        return [e.r32(u + 0x20), e.r32(u + 0x24), self.stats.get((u, 7), 0), look]


def gen(n, seed):
    import drlg
    e = drlg.boot()
    ids = boss_level.monstats_ids()
    tables = e.r32(0x744304)
    monstats, count = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
    level = lambda row, diff: e.r16(monstats + row * 0x1a8 + 0xaa + 2 * diff)
    live = [(r, d) for r in range(min(count, len(ids))) for d in range(3) if level(r, d) > 0]
    rng = random.Random(seed & 0xFFFFFFFF)
    out = live + [rng.choice(live) for _ in range(n)]
    for i, (row, diff) in enumerate(out):
        print(i, ids[row], row, diff, rng.getrandbits(32))


def gen_region(n, seed):
    """Region cases: every row x difficulty with a random region (0..13 other rows, the row itself
    present or not), then n random. `id monstats_id row difficulty seed others present sets_seed`."""
    import drlg
    e = drlg.boot()
    ids = boss_level.monstats_ids()
    tables = e.r32(0x744304)
    monstats, count = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
    level = lambda row, diff: e.r16(monstats + row * 0x1a8 + 0xaa + 2 * diff)
    live = [(r, d) for r in range(min(count, len(ids))) for d in range(3) if level(r, d) > 0]
    rng = random.Random(seed & 0xFFFFFFFF)
    out = live + [rng.choice(live) for _ in range(n)]
    for i, (row, diff) in enumerate(out):
        others = rng.choice((0, 0, 1, 3, 6, 12, 13, 13))
        present = int(others < 13 and rng.random() < 0.3)
        print(i, ids[row], row, diff, rng.getrandbits(32), others, present, rng.getrandbits(32))


def game(path):
    g = Game()
    for line in open(path):
        f = line.split()
        if not f: continue
        if len(f) > 5:
            print(f[0], "|", *g.init_region(int(f[2]), int(f[3]), int(f[4]) & 0xFFFFFFFF, int(f[5]), int(f[6]), int(f[7]) & 0xFFFFFFFF))
        else:
            print(f[0], "|", *g.init(int(f[2]), int(f[3]), int(f[4]) & 0xFFFFFFFF))


def inc(cases_path, game_path):
    """The cases joined to game.exe's answers, as tests/game/test_unit_seed_init.cpp reads them."""
    answers = {}
    for line in open(game_path):
        left, _, right = line.partition("|")
        answers[left.strip()] = right.split()
    print("// tools/emu/unit_seed_init.py inc: game.exe 1.14d's FUN_00573cb0 on each case (see the script)")
    print("// {monstats id, row, difficulty, seed, seed_low, seed_high, life256, look}")
    for line in open(cases_path):
        f = line.split()
        if not f: continue
        low, high, life, look = answers[f[0]]
        print(f'{{ "{f[1]}", {f[2]}, {f[3]}, {f[4]}u, {low}u, {high}u, {life}, "{look}" }},')


def inc_region(cases_path, game_path):
    """The region cases joined to game.exe's answers (tests/game/unit_seed_init_region_cases.inc)."""
    answers = {}
    for line in open(game_path):
        left, _, right = line.partition("|")
        answers[left.strip()] = right.split()
    print("// tools/emu/unit_seed_init.py inc-region: game.exe 1.14d's FUN_00547bc0 then FUN_00573cb0 on each case (see the script)")
    print("// {monstats id, row, difficulty, seed, others, present, sets seed, count, sets, seed_low, seed_high, life256, look}")
    for line in open(cases_path):
        f = line.split()
        if not f: continue
        count, sets, low, high, life, look = answers[f[0]]
        print(f'{{ "{f[1]}", {f[2]}, {f[3]}, {f[4]}u, {f[5]}, {f[6]}, {f[7]}u, {count}, {sets}, {low}u, {high}u, {life}, "{look}" }},')


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "gen": gen(int(sys.argv[2]) if len(sys.argv) > 2 else 2000, int(sys.argv[3]) if len(sys.argv) > 3 else 20261127)
    elif mode == "gen-region": gen_region(int(sys.argv[2]) if len(sys.argv) > 2 else 1000, int(sys.argv[3]) if len(sys.argv) > 3 else 20261128)
    elif mode == "game": game(sys.argv[2])
    elif mode == "inc": inc(sys.argv[2], sys.argv[3])
    elif mode == "inc-region": inc_region(sys.argv[2], sys.argv[3])
    else: sys.exit(__doc__)
