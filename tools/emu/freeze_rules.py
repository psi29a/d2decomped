# SPDX-License-Identifier: GPL-3.0-or-later
"""A freeze on a monster (Glacial Spike's record +0x34), by game.exe's own code.

    uv run python freeze_rules.py gen N SEED > cases.tsv
    uv run python freeze_rules.py game cases.tsv > game.tsv
    ../../build/tests/test_freeze_rules --freeze-rules cases.tsv > d2d.tsv
    uv run python freeze_rules.py compare cases.tsv game.tsv d2d.tsv
    uv run python freeze_rules.py flags > flags.tsv      # game side only: every flag byte
    uv run python freeze_rules.py state                  # FUN_00639db0(unit, 0x76, 1) natively

Runs the real FUN_0057c6c0 (apply a damage record to a monster) on a record
with cold damage (+0x24, 256ths), no chill length (+0x30 = 0: Glacial Spike
has no ELen) and the freeze length (+0x34, ticks) FUN_005aa8b0 puts there
through FUN_005a8f20 (EType frze). Run for real inside it: FUN_0057c1e0
with FUN_0057c140 (stats 0x99 / 0x76, 0 here), FUN_0057c060, FUN_0057be00,
FUN_0057bf80, FUN_0057bf10, FUN_00483360 and the table at 0x732980 (row 6:
field 0x34 through cold resistance 0x2b less cold pierce 0x14f); then
FUN_0057af80 (chill) and FUN_0057b230 (freeze) with FUN_0057af30
(coldeffect), FUN_0063e9f0 (MonStats boss, +0xc & [0x6ce280]), FUN_005a0180
(monster data +0x16 & 8) and FUN_0063ee90 (hirelings).

Hooked (not the claim): the stat getters / setter (defender life 10^6
points and cold resistance, attacker cold pierce, the rest 0), the
DifficultyLevels row (ResistPenalty, MonsterFreezeDivisor +0x14,
MonsterColdDivisor +0x18, read from the MPQ), the MonStats row lookup (a
fake row: boss byte, coldeffect), the unit-kind and event / logging calls,
state 0x36 (FUN_00639df0: 0) and the state-list calls, which only carry each
state's end frame. Prints `id chill freeze`: ticks from now to the end of
state 0xb and of state 1, or `-`.

A case: id difficulty coldeffect res pierce roll freeze prior_chill
prior_freeze kind bcol half. kind is d2d's Boss kind (none, champion,
unique, superunique, minion); the game side gives it the monster-data flags
that kind is made with (KIND_FLAGS); bcol is the MonStats boss column; half
is d2d's Monster::half_freeze (Blood Raven, the Countess): game.exe gives
those two state 118 (`state` below), not stat 0x76, so the game side has no
stat for it.
"""
import os
import random
import struct
import sys

import emu as emu_module

STAT_LIFE, STAT_COLD_RES, STAT_COLD_PIERCE = 6, 0x2B, 0x14F
FALLEN, HIRELINGS = 19, (0x10F, 0x152, 0x167, 0x230, 0x231)
FRAME = 2000
# FUN_005a48c0 (champions, 0x54e24c / 0x54e8fe): FUN_005a0320 (| 8), then | 5;
# FUN_005a4940 (uniques): FUN_005a0320 (| 8); superuniques | 2 (0x5a4599) and
# FUN_005a4850(mod, 1) -> FUN_005a0320 (| 8); minions | 0x10 (0x5a47df).
KIND_FLAGS = (0, 0xD, 0x8, 0xA, 0x10)


def gen(n, seed):
    rng = random.Random(seed & 0xFFFFFFFF)
    rows = []

    def add(diff, ce, res, pierce, roll, freeze, prior_chill=0, prior_freeze=0, kind=0, bcol=0, half=0):
        rows.append((diff, ce, res, pierce, roll, freeze, prior_chill, prior_freeze, kind, bcol, half))

    reses = (-300, -150, -101, -100, -99, -50, -1, 0, 1, 33, 50, 75, 98, 99, 100, 101, 150)
    freezes = (-5, 0, 1, 2, 3, 4, 5, 7, 8, 50, 51, 99, 100, 101, 250)
    for diff in range(3):
        for kind in range(5):
            for bcol in (0, 1):
                for half in (0, 1):
                    for ce in (-128, -100, -50, -1, 0, 1, 25, 127):
                        for res in reses:
                            for freeze in freezes:
                                add(diff, ce, res, 0, 128, freeze, 0, 0, kind, bcol, half)
        for res in reses:
            for pierce in (1, 50, 99, 100, 101, 250):
                for kind in (0, 2):
                    for freeze in (1, 50, 101):
                        add(diff, -50, res, pierce, 128, freeze, 0, 0, kind, 0, 0)
        for prior_chill in (0, 1, 24, 25, 26, 400):
            for prior_freeze in (0, 1, 12, 13, 25, 26, 400):
                for kind in (0, 1, 2):
                    for freeze in (0, 25, 50, 51):
                        add(diff, -50, rng.choice((0, 50)), 0, 128, freeze, prior_chill, prior_freeze, kind, 0, 0)
    n += len(rows)
    while len(rows) < n:
        diff = rng.randrange(3)
        ce = rng.choice((-50, -50, -33, -25, -75, -100, 0, rng.randint(-128, 127)))
        res = rng.choice((rng.randint(-100, 99), rng.randint(-200, 300), rng.randint(90, 110), 0))
        pierce = rng.choice((0, 0, 0, rng.randint(0, 100), rng.randint(0, 300)))
        roll = rng.choice((rng.randint(1, 255), rng.randint(1, 0x4000)))
        freeze = rng.choice((rng.randint(0, 25), rng.randint(1, 120), rng.randint(1, 2000), rng.randint(-20, 20)))
        prior_chill = rng.choice((0, 0, 0, rng.randint(1, 600)))
        prior_freeze = rng.choice((0, 0, 0, rng.randint(1, 600)))
        kind = rng.choice((0, 0, 0, 1, 2, 3, 4))
        bcol = int(rng.random() < 0.1)
        half = int(rng.random() < 0.1)
        add(diff, ce, res, pierce, roll, freeze, prior_chill, prior_freeze, kind, bcol, half)
    for i, r in enumerate(rows):
        print(i, *r, sep="\t")


def diff_levels(e):
    text = e.mpq_file("data\\global\\excel\\DifficultyLevels.txt").decode("latin-1").splitlines()
    head = text[0].split("\t")
    rows = [dict(zip(head, line.split("\t"), strict=False)) for line in text[1:4]]
    return [(int(r["ResistPenalty"]), int(r["MonsterFreezeDivisor"]), int(r["MonsterColdDivisor"])) for r in rows]


class Game:
    def __init__(self):
        e = self.e = emu_module.Emu()
        self.levels = diff_levels(e)
        self.boss_mask = e.r32(0x6CE280) & 0xFF
        assert self.boss_mask, "[0x6ce280] is 0 in the image"
        self.game = e.alloc(0x200)
        self.attacker = e.alloc(0x200)
        self.defender = e.alloc(0x200)
        self.mdata = e.alloc(0x100)
        self.diffrec = e.alloc(0x100)
        self.record = e.alloc(0x80)
        self.nrows = 0x240
        self.monstats = e.alloc(0x1A8 * self.nrows)
        tables = e.alloc(0x1000)
        e.w32(tables + 0xA78, self.monstats)
        e.w32(tables + 0xA80, self.nrows)
        e.w32(0x744304, tables)
        e.w32(self.attacker, 0)
        e.w32(self.defender, 1)
        e.w32(self.defender + 0x14, self.mdata)
        e.w32(self.game + 0xA8, FRAME)
        e.w32(self.game + 0x70, 1)
        self.stats = {}
        self.lists = {}                   # list address -> [state, end]
        self.unit_list = {}               # (unit, state) -> list address
        self._hooks()

    def _hooks(self):
        import unicorn.x86_const as x86
        e = self.e

        def stat(h):
            return self.stats.get((h.arg(0), h.arg(1)), 0) & 0xFFFFFFFF

        def monstats_row(h):
            cls = h.mu.reg_read(x86.UC_X86_REG_ECX)
            return self.monstats + 0x1A8 * cls if cls < self.nrows else 0

        def new_list(h):                  # FUN_006251f0(pool, 2, end, owner type, owner id)
            a = h.alloc(0x40)
            self.lists[a] = [None, h.arg(2)]
            return a

        def set_state(h):                 # FUN_006252d0(list, state)
            self.lists[h.arg(0)][0] = h.arg(1)
            return 0

        def attach(h):                    # FUN_00626e10(unit, list, 1)
            self.unit_list[(h.arg(0), self.lists[h.arg(1)][0])] = h.arg(1)
            return 0

        def set_end(h):
            self.lists[h.arg(0)][1] = h.arg(1)
            return 0

        zero = (0x627260, 4), (0x621D50, 2), (0x57C420, 1), (0x57A980, 1), (0x57AA60, 0), (0x57AAA0, 0), \
            (0x57AAE0, 2), (0x57AC50, 3), (0x57ADD0, 3), (0x5D6410, 0), (0x540E60, 2), (0x5C0C30, 3), \
            (0x5347D0, 7), (0x5347E0, 3), (0x63E990, 1), (0x63E940, 1), (0x639DF0, 2), (0x61AB00, 1), \
            (0x5541B0, 0), (0x639DB0, 3), (0x5417D0, 4), (0x625CE0, 2), (0x6270B0, 4), (0x623F50, 3)
        for addr, n in zero:
            e.hook(addr, lambda h: 0, n)
        for addr, n in ((0x457490, 0), (0x554200, 1), (0x620BB0, 1), (0x4638A0, 0)):
            e.hook(addr, lambda h: 1, n)
        for addr, fn, n in (
            (0x625480, stat, 3), (0x625500, stat, 3), (0x611D30, lambda h: self.diffrec, 1),
            (0x451F80, monstats_row, 0), (0x554010, lambda h: self.game, 0),
            (0x6256B0, lambda h: self.unit_list.get((h.arg(0), h.arg(1)), 0), 2),
            (0x6251F0, new_list, 5), (0x6252D0, set_state, 2), (0x626E10, attach, 3),
            (0x625330, lambda h: self.lists[h.arg(0)][1], 1), (0x625310, set_end, 2),
        ):
            e.hook(addr, fn, n)

    def run(self, cid, diff, ce, res, pierce, roll, freeze, prior_chill, prior_freeze, uflag, bcol, hire=0):
        e = self.e
        cls = HIRELINGS[cid % len(HIRELINGS)] if hire else FALLEN
        e.mu.mem_write(self.game + 0x6D, bytes([diff]))
        penalty, fdiv, cdiv = self.levels[diff]
        e.w32(self.diffrec, penalty)
        e.w32(self.diffrec + 0x14, fdiv)
        e.w32(self.diffrec + 0x18, cdiv)
        row = self.monstats + 0x1A8 * cls
        e.mu.mem_write(row, b"\0" * 0x1A8)
        e.mu.mem_write(row + 0xC, bytes([self.boss_mask if bcol else 0]))
        e.mu.mem_write(row + 0x168 + diff, struct.pack("<b", ce))
        e.w32(self.defender + 4, cls)
        e.w32(self.defender + 0x20, (0x1234567 + cid) & 0xFFFFFFFF)
        e.w32(self.defender + 0x24, 0)
        e.mu.mem_write(self.mdata + 0x16, struct.pack("<H", uflag))
        self.stats.clear()
        self.stats[(self.defender, STAT_LIFE)] = 1000000 << 8
        self.stats[(self.defender, STAT_COLD_RES)] = res
        self.stats[(self.attacker, STAT_COLD_PIERCE)] = pierce
        self.lists.clear()
        self.unit_list.clear()
        for state, prior in ((0xB, prior_chill), (1, prior_freeze)):
            if prior > 0:
                a = e.alloc(0x40)
                self.lists[a] = [state, FRAME + prior]
                self.unit_list[(self.defender, state)] = a
        e.mu.mem_write(self.record, b"\0" * 0x80)
        e.w32(self.record + 0x24, roll)
        e.w32(self.record + 0x34, freeze & 0xFFFFFFFF)
        e.call(0x57C6C0, self.defender, 1, self.record, ecx=self.game, edx=self.attacker)

        def end(state):
            a = self.unit_list.get((self.defender, state))
            return str(self.lists[a][1] - FRAME) if a and self.lists[a][1] > FRAME else "-"
        return end(0xB), end(1)


def cases(path):
    for line in open(path):
        f = line.split()
        if f:
            yield list(map(int, f))


def game(path):
    g = Game()
    for cid, diff, ce, res, pierce, roll, freeze, prior_chill, prior_freeze, kind, bcol, _half in cases(path):
        print(cid, *g.run(cid, diff, ce, res, pierce, roll, freeze, prior_chill, prior_freeze, KIND_FLAGS[kind], bcol))


def flags():
    """Game side only: every monster-data flag byte x boss column x hireling; what decides chill vs freeze."""
    g = Game()
    seen = {}
    for flag in range(0x100):
        for bcol in (0, 1):
            for hire in (0, 1):
                out = g.run(flag * 4 + bcol * 2 + hire, 0, -50, 0, 0, 128, 50, 0, 0, flag, bcol, hire)
                seen.setdefault((bool(flag & 8) or bool(bcol) or bool(hire), out), []).append((flag, bcol, hire))
    for (special, out), keys in sorted(seen.items()):
        print(f"special={int(special)} chill/freeze={out} cases={len(keys)}")


def state():
    """FUN_00639db0(unit, 0x76, 1), the call at Blood Raven's 0x5b1fb0 and the Countess's 0x5a45d1 / 0x5a4c00,
    run natively on a unit with an empty stat list: it sets bit 118 of the state bits (+0x58) and changes no other
    byte of the list (FUN_00625a70 also marks it changed: the same bit in the second half, 118 + the state
    count rounded up to 32); the real stat getter FUN_00625480(unit, 0x76, 0) then reads 0."""
    e = emu_module.Emu()
    e.hook(0x64C040, lambda h: 0, 1)          # the unit's client update: not the claim
    row = e.alloc(0x40)
    e.hook(0x452040, lambda h: row, 0)       # the States row (ecx): +0x12 0, no client flag
    tables = e.alloc(0x1000)
    e.w32(tables + 0xC4, 0x200)               # states in the table (bounds only)
    e.w32(0x744304, tables)
    unit = e.alloc(0x200)
    stat_list = e.alloc(0x100)
    bits = e.alloc(0x80)
    e.w32(unit, 1)
    e.w32(unit + 0x5C, stat_list)
    e.w32(stat_list + 0x10, 0x80000000)
    e.w32(stat_list + 0x58, bits)
    before = bytes(e.mu.mem_read(stat_list, 0x100)), bytes(e.mu.mem_read(bits, 0x80))
    e.call(0x639DB0, unit, 0x76, 1)
    after = bytes(e.mu.mem_read(stat_list, 0x100)), bytes(e.mu.mem_read(bits, 0x80))
    changed = [i for i in range(0x100) if before[0][i] != after[0][i]]
    set_bits = [i * 8 + b for i in range(0x80) for b in range(8) if (after[1][i] ^ before[1][i]) >> b & 1]
    value = e.call(0x625480, unit, 0x76, 0)
    print(f"state bits set: {set_bits}; stat-list bytes changed: {changed}; stat 0x76 reads {value}")


def compare(case_path, game_path, d2d_path):
    rows = {r[0]: r for r in cases(case_path)}
    g = {f[0]: f[1:3] for f in (line.split() for line in open(game_path)) if f}
    d = {f[0]: f[1:3] for f in (line.split() for line in open(d2d_path)) if f and f[0].isdigit()}
    resfail = sum(1 for line in open(d2d_path) if "RESFAIL" in line)
    bad = [k for k in rows if g.get(str(k)) != d.get(str(k))]
    per = {}
    for k in bad:
        per.setdefault(classify(rows[k]), []).append(k)
    print(f"cases {len(rows)} game {len(g)} d2d {len(d)} mismatches {len(bad)} resfail {resfail}")
    for claim, keys in sorted(per.items()):
        print(f"  {len(keys):7d}  {claim}")
        for k in keys[:3]:
            print("           ", *rows[k], "game", g.get(str(k)), "d2d", d.get(str(k)))


def classify(r):
    _cid, _diff, _ce, _res, _pierce, _roll, _freeze, _pc, _pf, kind, bcol, half = r
    special = bool(KIND_FLAGS[kind] & 8) or bool(bcol)
    if half:
        return "C4 d2d's half_freeze set (Blood Raven, the Countess)"
    if special:
        return "C3 special monster (flag 8 / MonStats boss): chill, no freeze"
    return "C5 plain monster: resisted / MonsterFreezeDivisor / coldeffect < 0, no chill"


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen" and len(sys.argv) == 4:
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == "game" and len(sys.argv) == 3:
        game(sys.argv[2])
    elif cmd == "compare" and len(sys.argv) == 5:
        compare(*sys.argv[2:5])
    elif cmd == "flags":
        flags()
    elif cmd == "state":
        state()
    else:
        sys.exit(__doc__)
