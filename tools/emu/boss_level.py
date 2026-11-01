# SPDX-License-Identifier: GPL-3.0-or-later
"""Which MonLvl level a champion's, unique's, superunique's and minion's stats come from, by game.exe 1.14d's own code.

    uv run python boss_level.py gen [N] [SEED] > cases.tsv           # every row x difficulty x kind, then N random (default 100000)
    uv run python boss_level.py game cases.tsv > game.tsv            # game.exe, needs D2_MPQ_DIR
    build/tests/test_boss_level --boss-level cases.tsv > d2d.tsv     # d2d's real make_boss on the same cases
    uv run python boss_level.py compare cases.tsv game.tsv d2d.tsv   # mismatches by field, kind and mods

A case: `id monstats_id row difficulty kind mods seed`; kind c champion (mods
holds its kind: 16, 36..39), u unique, s superunique, m minion; seed is the
monster's unit seed (masked to 32 bits).

Game side, every table loaded by game.exe (drlg.boot), on a synthetic unit:
1. FUN_00573cb0, the stat init: level (stat 12) from the MonStats row, life
   rolled on the unit seed (+0x20 = seed, +0x24 = 666; FUN_0045c3e0 run),
   defence and experience from FUN_006538a0 at THAT level.
2. FUN_005a2120 (the fixed mods 1..4 rndname, hpmultiply, light, leveladd,
   then the unit's own mod bytes, boss flag 1) for c / u / s; for m what its
   party callback does to a minion: the fixed mods' inits with flag 0 (the
   unique's own mods on its minions are not d2d's and not compared).
3. FUN_005a4f50 mode 4 (A1), what a hit reads: to-hit and A1 damage from
   FUN_006538a0 at the level stat 12 holds then.
Hooked (none is the claim; stdcall pops from each function's `ret`):
FUN_00573cb0's helpers FUN_005739d0, FUN_00573930 (player count 1, no
bonus), FUN_0063ee90, FUN_0061a1b0 / FUN_0061dca0 (the area level of an
expansion Nightmare / Hell game: the case's MonStats level instead),
FUN_00626d40, FUN_0063eef0, FUN_006251f0, FUN_00626e10, FUN_00536b20,
FUN_0063abd0, FUN_006438b0, the skills (FUN_0056deb0, FUN_006439f0,
FUN_00644340, FUN_0063ebc0), MonProp (FUN_00573470: none), FUN_005d6b60;
the stat getters / setters (FUN_00625480, FUN_006253b0, FUN_00627260,
FUN_006272b0, FUN_00627150: one dict a unit, the hit's list apart), max life
(FUN_00625d10 = stat 7), the party walk FUN_0058f380, teleport's and aura's
inits (FUN_005a1600, FUN_005a1650: no stat), mod 41's event FUN_005417d0;
in FUN_005a4f50 FUN_0056e050 (no bonus), FUN_00625760 / FUN_00627340 (the
list), FUN_006259b0 (1: no player scaling), FUN_00451f60 (elements: not
compared). Rows 0x137 / 0x138 (their own extra roll) are left out.

`compare` keys a life at most 2 points off as `life~2` (game.exe keeps life in
256ths through the percent inits; d2d in whole points: not this claim).

Output `id | lvl life def exp ar dmg_lo dmg_hi vel`: life whole points
(stat 7 >> 8), def with item_armor_percent folded in as d2d does, ar and
damage with the to-hit / damage percent added as d2d's make_boss does (the
hit applies them to its roll: not compared), vel what the mods add to stat
0x43 (FUN_00573cb0 sets it first).
"""
import csv
import os
import random
import sys

sys.path.insert(0, os.getcwd())

UPICK = (5, 6, 7, 8, 9, 17, 18, 25, 26, 27, 28, 29, 30)
CHAMP = (16, 36, 37, 38, 39)
FIELDS = "lvl life def exp ar dmg_lo dmg_hi vel".split()
SKIP_ROWS = (0x137, 0x138)


def s32(v): return v - (1 << 32) if v >> 31 else v


def monstats_ids():
    import emu
    text = emu.mpq_cache().read("data\\global\\excel\\monstats.txt")
    rows = list(csv.reader(text.decode("latin1").splitlines(), delimiter="\t"))
    return [r[0] for r in rows[1:] if r and r[0] and r[0] != "Expansion"]


class Game:
    def __init__(self):
        import drlg
        from unicorn.x86_const import UC_X86_REG_EDX
        e = self.e = drlg.boot()
        self.edx = UC_X86_REG_EDX
        self.stats, self.hit = {}, {}
        self.level_now = 0
        self.game = e.alloc(0x200)
        e.w32(self.game + 0x70, 1); e.w32(self.game + 0x74, 1)     # expansion, L- columns
        self.unit, self.data, self.list = e.alloc(0x100), e.alloc(0x40), e.alloc(0x40)
        tables = e.r32(0x744304)
        self.monstats, self.rows = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
        get = lambda e: self.stats.get((e.arg(0), e.arg(1)), 0) & 0xFFFFFFFF
        e.hook(0x625480, get, 3)
        e.hook(0x6253b0, get, 3)
        e.hook(0x627260, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), s32(e.arg(2))) or 0, 4)
        e.hook(0x6272b0, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), self.stats.get((e.arg(0), e.arg(1)), 0) + s32(e.arg(2))) or 0, 4)
        e.hook(0x627150, lambda e: self.hit.__setitem__(e.arg(1), self.hit.get(e.arg(1), 0) + s32(e.arg(2))) or 0, 4)
        e.hook(0x625d10, lambda e: self.stats.get((e.arg(0), 7), 0) & 0xFFFFFFFF, 1)
        e.hook(0x573930, self.players, 2)
        e.hook(0x61dca0, lambda e: self.level_now, 3)
        e.hook(0x625760, lambda e: self.list, 2)
        e.hook(0x6259b0, lambda e: 1, 1)
        for addr, n in ((0x5739d0, 0), (0x63ee90, 1), (0x61a1b0, 1), (0x626d40, 4), (0x63eef0, 3), (0x6251f0, 5), (0x626e10, 3),
                        (0x536b20, 0), (0x63abd0, 2), (0x6438b0, 1), (0x56deb0, 2), (0x6439f0, 0), (0x644340, 2), (0x63ebc0, 2),
                        (0x573470, 0), (0x5d6b60, 5), (0x58f380, 2), (0x5a1600, 1), (0x5a1650, 1), (0x5417d0, 3),
                        (0x56e050, 1), (0x627340, 1), (0x451f60, 0)):
            e.hook(addr, lambda e: 0, n)

    def players(self, e):
        out = e.mu.reg_read(self.edx)
        for off, value in ((0, 0), (4, 0), (8, 0), (0xc, 0), (0x10, 1)): e.w32(out + off, value)
        return 0

    def rec(self, row): return self.monstats + row * 0x1a8

    def level(self, row, diff): return self.e.r16(self.rec(row) + 0xaa + 2 * diff)

    def run(self, row, diff, kind, mods, seed):
        e, u = self.e, self.unit
        e.mu.mem_write(u, b"\0" * 0x100); e.mu.mem_write(self.data, b"\0" * 0x40)
        e.mu.mem_write(self.game + 0x6d, bytes([diff]))
        e.w32(u, 1); e.w32(u + 4, row); e.w32(u + 0x14, self.data); e.w32(u + 0x80, self.game)
        e.w32(u + 0x20, seed); e.w32(u + 0x24, 666)
        e.w32(self.data, self.rec(row))
        self.stats.clear(); self.hit.clear()
        self.level_now = self.level(row, diff)
        e.call(0x573cb0, self.game, 0, 0, regs={"esi": u})
        velocity = self.stats.get((u, 0x43), 0)
        flags = {"c": 0xd, "u": 0x9, "s": 0xb, "m": 0x11}[kind]
        e.mu.mem_write(self.data + 0x16, flags.to_bytes(2, "little"))
        if kind == "m":
            for mod in (1, 2, 3, 4):
                fn = e.r32(0x73c008 + 4 * mod)
                if fn: e.call(fn, 0, ecx=u, edx=mod)
        else:
            e.mu.mem_write(self.data + 0x1c, bytes(mods) + b"\0" * (9 - len(mods)))
            e.call(0x5a2120, self.game, u, 0)
        e.call(0x5a4f50, ecx=u, edx=4)
        g = lambda s: self.stats.get((u, s), 0)
        pct = lambda v, p: v + int(v * p / 100)
        return [g(0xc), g(7) >> 8, pct(g(0x1f), g(0x10)), g(0xd), pct(self.hit.get(0x13, 0), g(0x77)),
                pct(self.hit.get(0x15, 0), g(0x19)), pct(self.hit.get(0x16, 0), g(0x19)), g(0x43) - velocity]


def gen(n, seed):
    import drlg
    e = drlg.boot()
    ids = monstats_ids()
    tables = e.r32(0x744304)
    monstats, count = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
    level = lambda row, diff: e.r16(monstats + row * 0x1a8 + 0xaa + 2 * diff)
    rows = [r for r in range(min(count, len(ids))) if r not in SKIP_ROWS]
    rng = random.Random(seed & 0xFFFFFFFF)
    unit_seed = lambda: rng.getrandbits(32) & 0xFFFFFFFF
    out = []
    for row in rows:
        for diff in range(3):
            if level(row, diff) <= 0: continue
            for m in CHAMP: out.append((row, diff, "c", (m,)))
            for kind in "usm": out.append((row, diff, kind, ()))
            for m in UPICK: out.append((row, diff, "u", (m,)))
    live = [(r, d) for r in rows for d in range(3) if level(r, d) > 0]
    for _ in range(n):
        row, diff = rng.choice(live)
        kind = rng.choice("cusm")
        if kind == "c": mods = (rng.choice(CHAMP),)
        elif kind == "m": mods = ()
        else: mods = tuple(rng.sample(UPICK, rng.randint(1, 1 + diff + (kind == "s") * 3)))
        out.append((row, diff, kind, mods))
    for i, (row, diff, kind, mods) in enumerate(out):
        print(i, ids[row], row, diff, kind, ".".join(map(str, mods)) or "-", unit_seed())


def game(path):
    g = Game()
    for line in open(path):
        i, _, row, diff, kind, mods, seed = line.split()
        mods = tuple(int(m) for m in mods.split(".")) if mods != "-" else ()
        print(i, "|", *g.run(int(row), int(diff), kind, mods, int(seed) & 0xFFFFFFFF))


def claim(field, kind, mods):
    """The commit a mismatch belongs to: D1 a boss's life / defence / experience
    row, D2 a minion's level, D3 ghostly speed, B a berserker's level."""
    if field == "vel" and kind == "c" and mods == "36": return "D3"
    leveled = field in ("lvl", "exp", "ar", "dmg_lo", "dmg_hi")
    if kind == "c" and mods == "39" and leveled: return "B"
    if kind == "m" and leveled: return "D2"
    if kind in "cus" and field in ("life", "def", "exp"): return "D1"
    return "other"


def compare(cases_path, game_path, d2d_path):
    cases = {l.split()[0]: l.split() for l in open(cases_path)}
    want = {l.split()[0]: l.split()[2:] for l in open(game_path)}
    bad, examples, total, differ, skipped = {}, {}, 0, 0, 0
    claims = {}
    for line in open(d2d_path):
        got = line.split()
        if not got or got[0] not in want: continue
        if got[2:] == ["no-type"]:
            skipped += 1
            continue
        total += 1
        case = cases[got[0]]
        kind, mods = case[4], case[5]
        mods_key = mods if kind == "c" or "." not in mods else "multi"
        one = False
        for field, a, b in zip(FIELDS, want[got[0]], got[2:], strict=True):
            if a == b: continue
            one = True
            # game.exe keeps life in 256ths through the percent inits (and over
            # 4096 points FUN_005a0d20 divides first); d2d in whole points.
            rounding = field == "life" and abs(int(a) - int(b)) <= 2
            key = (field + ("~2" if rounding else ""), kind, mods_key)
            bad[key] = bad.get(key, 0) + 1
            slice_ = "life within 2 (256ths)" if rounding else "life 0 in game.exe" if field == "life" and a == "0" else claim(field, kind, mods)
            claims[slice_] = claims.get(slice_, 0) + 1
            examples.setdefault(key, (case[1], case[3], a, b))
        differ += one
    print(f"cases {total}, mismatching cases {differ}, not d2d's row {skipped}")
    print(f"apart from life within 2 points: {sum(v for k, v in bad.items() if not k[0].endswith('~2'))} field mismatches")
    for name in ("D1", "D2", "D3", "B", "other", "life within 2 (256ths)", "life 0 in game.exe"):
        print(f"claim {name}: {claims.get(name, 0)} field mismatches")
    for key in sorted(bad, key=lambda k: -bad[k]):
        mon, diff, a, b = examples[key]
        print(f"{bad[key]:7d}  {key[0]:6s} kind {key[1]} mods {key[2]:6s}  e.g. {mon} diff {diff}: game {a} d2d {b}")
    return 0


def main():
    args = sys.argv[1:]
    if args[:1] == ["gen"]:
        gen(int(args[1]) if len(args) > 1 else 100000, int(args[2]) if len(args) > 2 else 20261101)
    elif args[:1] == ["game"]:
        game(args[1])
    elif args[:1] == ["compare"]:
        return compare(args[1], args[2], args[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
