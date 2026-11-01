# SPDX-License-Identifier: GPL-3.0-or-later
"""A cold missile's chill on a monster, by game.exe's own code.

    uv run python chill_length.py [N] [SEED] > game.txt     (default 100000 random cases, seed 1)
    ../../build/tests/test_game --cold-length SEED N | grep -E '^[BR][0-9]' | diff game.txt -

Runs the real FUN_0057c6c0 (apply a damage record to a monster) on a record
with cold damage (+0x24, 256ths) and its length (+0x30, ticks), as
FUN_0056c8e0 fills it for a cold skill, and then the real FUN_0057af80 it
calls, and prints how many ticks from now the monster's chill (state 0xb)
ends, or `none`. Inside it run for real: FUN_0057c1e0 with FUN_0057c140
(stats 0x99 / 0x76, 0 here: no 1.14d monster has them) and the element
table at 0x732980 (row 5: field 0x30 through cold resistance 0x2b less
the attacker's cold pierce 0x14f, FUN_0057be00 / FUN_0057bf80, no flat
reduction, no absorb), the call to FUN_0057af80 whatever the damage came
to (0057c8a0..0057c8a8), and FUN_0057af80: nothing at length <= 0, the
MonStats coldeffect (row +0x168 + difficulty, signed) 0: nothing, under 0:
length / DifficultyLevels MonsterColdDivisor (+0x18), at least 1, a
running chill only lengthened.

Hooked (not the claim), as tools/emu/life_apply.py hooks them: the stat
getters / setter, max life, the unit-kind predicates, the difficulty row
(MonsterColdDivisor 1 / 2 / 4, the 1.14d DifficultyLevels values), events,
logging, the other side effects of FUN_0057c6c0. In FUN_0057af80: the
state list calls (FUN_006256b0 finds the running chill, FUN_00625330 /
FUN_00625310 read and set its end, FUN_006251f0 makes a new one: these
hooks record the end), the speed stats and the 20 % state 0x6b roll
(FUN_006270b0, FUN_00623f50, FUN_00639db0, FUN_004638a0: the walk speed
is not this claim). MonStats is a one-row table (class 60) holding the
case's coldeffect.

A case: `id roll len res pierce coldeffect difficulty prior` (roll in
256ths, len and prior in ticks; prior 0: not chilled).
"""
import sys

import emu
import life_apply
from static_field import xorshift

DIVISOR = (1, 2, 4)                 # DifficultyLevels MonsterColdDivisor, 1.14d
FRAME = 1000
CLASS = 60


class Game(life_apply.Game):
    def __init__(self):
        plain = emu.Emu.hook
        emu.Emu.hook = lambda e, addr, fn, n: None if addr == 0x57af80 else plain(e, addr, fn, n)   # run it for real
        try:
            super().__init__()
        finally:
            emu.Emu.hook = plain
        e = self.e
        self.tables = e.alloc(0xb00)
        self.rows = e.alloc((CLASS + 1) * 0x1a8)
        e.w32(self.tables + 0xa78, self.rows); e.w32(self.tables + 0xa80, CLASS + 1)
        e.w32(0x744304, self.tables)
        e.w32(self.game + 0xa8, FRAME)
        self.prior = 0
        self.end = None
        e.hook(0x625500, lambda e: self.stats.get((e.arg(0), e.arg(1)), 0), 3)
        e.hook(0x554010, lambda e: self.game, 0)
        e.hook(0x6256b0, lambda e: 0x1000 if self.prior and e.arg(1) == 0xb else 0, 2)
        e.hook(0x625330, lambda e: FRAME + self.prior, 1)
        e.hook(0x625310, lambda e: setattr(self, "end", e.arg(1)) or 0, 2)
        e.hook(0x6251f0, lambda e: setattr(self, "end", e.arg(2)) or 0x2000, 5)
        for addr, n in ((0x5417d0, 4), (0x6252d0, 2), (0x625ce0, 2), (0x626e10, 3), (0x6270b0, 4), (0x623f50, 3),
                        (0x639db0, 3), (0x4638a0, 0)):
            e.hook(addr, lambda e: 0, n)

    def chill(self, roll, length, res, pierce, cold_effect, difficulty, prior):
        e = self.e
        self.reset(100000, 0, 0)
        self.stats[(self.target, 0x2b)] = res & 0xFFFFFFFF
        self.stats[(self.attacker, 0x14f)] = pierce
        e.mu.mem_write(self.game + 0x6d, bytes([difficulty]))
        e.w32(self.diffrow + 0x18, DIVISOR[difficulty])
        e.mu.mem_write(self.rows + CLASS * 0x1a8 + 0x168, bytes((cold_effect & 0xFF if d == difficulty else 0) for d in range(3)))
        self.prior, self.end = prior, None
        e.mu.mem_write(self.rec, b"\0" * 0x80)
        e.w32(self.rec, 1); e.w32(self.rec + 4, 1)
        e.w32(self.rec + 0x24, roll); e.w32(self.rec + 0x30, length); e.w32(self.rec + 0x60, 0x30)
        e.call(0x57c6c0, self.target, 1, self.rec, ecx=self.game, edx=self.attacker)
        ticks = self.end - FRAME if self.end is not None else prior
        return str(ticks) if ticks > 0 else "none"


def cases(n, seed):
    """The same cases, in the same order, as test_game --cold-length SEED N."""
    i = 0
    for difficulty in range(3):
        for cold_effect in (-50, -1, 0, 25):
            for res in (-150, -100, -99, -50, -1, 0, 1, 25, 50, 75, 90, 99, 100, 101, 150):
                for roll in (1, 127, 128, 255, 256, 257, 511, 512, 1024):
                    for length in (0, 1, 2, 3, 4, 5, 7, 50, 100, 101, 150, 375):
                        yield (f"B{i}", roll, length, res, 0, cold_effect, difficulty, 0); i += 1
            for prior in (1, 37, 1000):
                for res in (0, 75):
                    for roll in (128, 512):
                        for length in (0, 1, 2, 3, 4, 5, 7, 50, 100, 101, 150, 375):
                            yield (f"B{i}", roll, length, res, 0, cold_effect, difficulty, prior); i += 1
        for pierce in (1, 40, 100):
            for res in (50, 99, 100, 150):
                for roll in (128, 256):
                    for length in (50, 150):
                        yield (f"B{i}", roll, length, res, pierce, -50, difficulty, 0); i += 1
    state = [seed or 1]
    effects = (-50, -50, -50, -33, -25, 0, 10, -100)
    for k in range(n):
        roll = 1 + xorshift(state) % (0x800 if xorshift(state) % 2 else 0x4000)
        length = 0 if xorshift(state) % 10 == 0 else 1 + xorshift(state) % 500
        r = xorshift(state) % 100
        if r < 10: res = 100 + xorshift(state) % 51
        elif r < 25: res = -(xorshift(state) % 151)
        else: res = xorshift(state) % 100
        pierce = 0 if res >= 100 or xorshift(state) % 4 else xorshift(state) % 61
        cold_effect = effects[xorshift(state) % 8] if xorshift(state) % 4 else xorshift(state) % 201 - 100
        difficulty = xorshift(state) % 3
        prior = 0 if xorshift(state) % 10 < 7 else 1 + xorshift(state) % 600
        yield (f"R{k}", roll, length, res, pierce, cold_effect, difficulty, prior)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    g = Game()
    total = 0
    for case in cases(n, seed):
        print(" ".join(map(str, case)), g.chill(*case[1:]))
        total += 1
    print(f"{total} cases", file=sys.stderr)


if __name__ == "__main__": main()
