# SPDX-License-Identifier: GPL-3.0-or-later
"""What the MonUMod spawn inits do to a monster's stats, by game.exe 1.14d's own code .

    uv run python umod_stats.py [N] [SEED] > game.tsv     (N random cases after the exhaustive singles; default 100000, seed 1)
    build/tests/test_umod_stats --umod-stats game.tsv > d2d.tsv     (d2d's rules::boss_stats on the same cases)
    python umod_stats.py --compare game.tsv d2d.tsv

Runs, with every data table loaded by game.exe itself (drlg.boot: MonStats,
MonLvl, MonUMod, DifficultyLevels), the real FUN_005a2120 on a synthetic
monster unit (no minions): the fixed mods 1..4 (0x6e2168: rndname,
hpmultiply, light, leveladd) and the unit's own mod bytes (+0x1c), each
through its init in the table at 0x73c008 with the boss flag 1. For a
minion it runs what FUN_005a2120's party callback (FUN_0058f380 ->
FUN_005a2100) does to each party member: every one of those mods' inits with
the flag 0. Run for real: the inits (FUN_005a0dc0 / FUN_005a0d20 life,
FUN_005a0e40 level, FUN_005a0e80 champion, FUN_005a17e0 strong, LAB_005a1910
fast, FUN_005a1370 resists, FUN_005a1990 / FUN_005a1b00 / FUN_005a1c70
enchanted, FUN_005a1e00 poison, FUN_005a1f90 mana burn, FUN_005a1080 ghostly,
FUN_005a11f0 fanatic, FUN_005a1230 possessed, FUN_005a1280 berserker), the
MonUMod constants (FUN_005a00f0), the MonLvl row (FUN_005a0000), the
difficulty row (FUN_00611d30), the BaseId test (FUN_00463860).
Hooked (not the claim): the stat getters / setter / adder (FUN_00625480,
FUN_006253b0, FUN_00627260, FUN_006272b0: a dict), max life (FUN_00625d10 =
stat 7), the party walk FUN_0058f380 (recorded), teleport's init FUN_005a1600 (skill 184
and an AI flag, no stat) and aura's FUN_005a1650 (its aura skill: structural),
the event of mod 41 (FUN_005417d0).

A case starts from fixed base stats (level = MonStats Level for the difficulty,
exp 1000, life 1000.0, defence 100, the row's own resistances, regen 5) so
every delta shows. Output line (tab separated):
  id row diff kind mods | lvl exp life256 dmg% th% vel% dr mr fr lr cr pr def armor% regen
  fire_lo fire_hi ltng_lo ltng_hi cold_lo cold_hi cold_len mana_lo256 mana_hi256 dmlvl
kind: c champion, u unique, s superunique (same inits), m minion; mods `.`-joined
(a champion carries its kind mod: 16, 36..39). The header lines `#lvl L DM0 DM1
DM2` give MonLvl's expansion damage column (L-DM) for the harness.
"""
import os
import random
import sys

sys.path.insert(0, os.getcwd())

UPICK = (5, 6, 7, 8, 9, 17, 18, 25, 26, 27, 28, 29, 30)
CHAMP = (16, 36, 37, 38, 39)
FIELDS = ("lvl exp life256 dmg th vel dr mr fr lr cr pr def armor regen "
          "fire_lo fire_hi ltng_lo ltng_hi cold_lo cold_hi cold_len mana_lo mana_hi").split()


def s32(v): return v - (1 << 32) if v >> 31 else v


class Game:
    def __init__(self):
        import drlg
        e = self.e = drlg.boot()
        self.stats = {}
        self.party = []
        self.game = e.alloc(0x200)
        e.w32(self.game + 0x70, 1); e.w32(self.game + 0x74, 1)     # expansion (+0x6a / +0x74: L- columns)
        self.unit = e.alloc(0x100)
        self.data = e.alloc(0x40)
        tables = e.r32(0x744304)
        self.monstats, self.rows = e.r32(tables + 0xa78), e.r32(tables + 0xa80)
        self.monlvl, self.lvls = e.r32(tables + 0xb70), e.r32(tables + 0xb74)
        get = lambda e: self.stats.get((e.arg(0), e.arg(1)), 0) & 0xFFFFFFFF
        e.hook(0x625480, get, 3)
        e.hook(0x6253b0, get, 3)
        e.hook(0x627260, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), s32(e.arg(2))) or 0, 4)
        e.hook(0x6272b0, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), self.stats.get((e.arg(0), e.arg(1)), 0) + s32(e.arg(2))) or 0, 4)
        e.hook(0x625d10, lambda e: self.stats.get((e.arg(0), 7), 0) & 0xFFFFFFFF, 1)
        e.hook(0x58f380, lambda e: self.party.append(e.mu.reg_read(self.EDX)) or 0, 2)
        e.hook(0x5a1600, lambda e: 0, 1)                             # teleport: skill 184 + AI flag only, no stat (structural)
        e.hook(0x5a1650, lambda e: 0, 1)                             # aura: the aura skill and its level (structural, boss_aura)
        for addr, n in ((0x56deb0, 3), (0x5dd250, 1), (0x6439f0, 2), (0x644340, 3), (0x5701b0, 3), (0x5417d0, 3)):
            e.hook(addr, lambda e: 0, n)

    from unicorn.x86_const import UC_X86_REG_EDX as EDX

    def rec(self, row): return self.monstats + row * 0x1a8

    def level(self, row, diff): return self.e.r16(self.rec(row) + 0xaa + 2 * diff)

    def dm(self, level, diff): return self.e.s32(self.monlvl + level * 0x78 + 4 * (21 + diff))

    def res(self, row, diff):
        r = self.rec(row)
        def s16(v): return v - 0x10000 if v >> 15 else v
        return [s16(self.e.r16(r + off + 2 * diff)) for off in (0x144, 0x14a, 0x150, 0x156, 0x15c, 0x162)]

    def run(self, row, diff, kind, mods):
        e, u = self.e, self.unit
        e.mu.mem_write(u, b"\0" * 0x100); e.mu.mem_write(self.data, b"\0" * 0x40)
        e.mu.mem_write(self.game + 0x6d, bytes([diff]))
        e.w32(u, 1); e.w32(u + 4, row); e.w32(u + 0x14, self.data); e.w32(u + 0x80, self.game)
        e.w32(u + 0x20, 0x1234567 + row); e.w32(u + 0x24, 666)
        flags = {"c": 0xd, "u": 0x9, "s": 0xb, "m": 0x11}[kind]
        e.mu.mem_write(self.data + 0x16, flags.to_bytes(2, "little"))
        self.stats.clear(); self.party.clear()
        base = {0xc: self.level(row, diff), 0xd: 1000, 7: 1000 << 8, 6: 1000 << 8, 0x1f: 100, 0x4a: 5}
        for stat, value in zip((0x24, 0x25, 0x27, 0x29, 0x2b, 0x2d), self.res(row, diff)): base[stat] = value
        for stat, value in base.items(): self.stats[(u, stat)] = value
        if kind == "m":
            for mod in (1, 2, 3, 4) + tuple(mods):
                fn = e.r32(0x73c008 + 4 * mod)
                if fn: e.call(fn, 0, ecx=u, edx=mod)
        else:
            e.mu.mem_write(self.data + 0x1c, bytes(mods) + b"\0" * (9 - len(mods)))
            e.call(0x5a2120, self.game, u, 0)
        g = lambda s: self.stats.get((u, s), 0)
        return [g(0xc), g(0xd), g(7), g(0x19), g(0x77), g(0x43), g(0x24), g(0x25), g(0x27), g(0x29), g(0x2b), g(0x2d), g(0x1f), g(0x10), g(0x4a),
                g(0x30), g(0x31), g(0x32), g(0x33), g(0x36), g(0x37), g(0x38), g(0x3e), g(0x3f)]


def cases(g, n, seed):
    rows = [r for r in range(g.rows) if g.level(r, 0) > 0]
    for row in rows:
        for diff in range(3):
            for m in CHAMP: yield row, diff, "c", (m,)
            for m in UPICK: yield row, diff, "u", (m,)
            for m in UPICK: yield row, diff, "m", (m,)
            yield row, diff, "u", ()
            yield row, diff, "m", ()
    rng = random.Random(seed)
    for _ in range(n):
        row, diff = rng.choice(rows), rng.randrange(3)
        kind = rng.choice("usm")
        yield row, diff, kind, tuple(rng.sample(UPICK, rng.randint(1, 1 + diff + (kind == "s") * 3)))


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    g = Game()
    umods = g.e.r32(g.e.r32(0x744304) + 0xc50)
    print("#k\t" + "\t".join(str(g.e.s32(umods + 0x20 * r + 0x1c)) for r in range(34)))
    for level in range(g.lvls):
        print(f"#lvl\t{level}\t" + "\t".join(str(g.dm(level, d)) for d in range(3)))
    for i, (row, diff, kind, mods) in enumerate(cases(g, n, seed)):
        res = g.res(row, diff)
        vel = g.e.r16(g.rec(row) + 0x32)
        vel = vel - 0x10000 if vel >> 15 else vel
        base_id = g.e.r16(g.rec(row) + 2)
        out = g.run(row, diff, kind, mods)
        print("\t".join(map(str, [i, row, diff, kind, ".".join(map(str, mods)) or "-", g.level(row, diff), vel, base_id, *res, "|", *out])))


def compare(game_path, d2d_path):
    """Per field: cases that differ, and the first few, keyed by (kind, mods)."""
    game = {l.split("\t")[0]: l.rstrip("\n").split("\t") for l in open(game_path) if not l.startswith("#")}
    bad, total, examples = {}, 0, {}
    for line in open(d2d_path):
        if line.startswith("#"): continue
        d = line.rstrip("\n").split("\t")
        gl = game.get(d[0])
        if not gl: continue
        total += 1
        head, gv = gl[:5], gl[gl.index("|") + 1:]
        defence, armor = int(gv[12]), int(gv[13])
        gv[12] = str(defence + int(defence * armor / 100))       # d2d folds armor% into its defence
        dv = d[1:]
        for f, a, b in zip(FIELDS, gv, dv):
            if b == "*" or a == b: continue
            key = (f, head[3], head[4] if head[3] != "u" or len(head[4]) < 3 else "multi")
            bad[key] = bad.get(key, 0) + 1
            examples.setdefault(key, (head, a, b))
    print(f"cases {total}")
    for key in sorted(bad, key=lambda k: -bad[k]):
        print(f"{bad[key]:7d}  {key[0]:8s} kind {key[1]} mods {key[2]:6s}  e.g. row {examples[key][0][1]} diff {examples[key][0][2]}: game {examples[key][1]} d2d {examples[key][2]}")
    print(f"differ (any field): {len({k for k in bad})} keys")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--compare":
        compare(sys.argv[2], sys.argv[3])
    else:
        main()
