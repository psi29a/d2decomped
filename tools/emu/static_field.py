# SPDX-License-Identifier: GPL-3.0-or-later
"""Static Field's hit on one monster, by game.exe's own code.

    uv run python static_field.py [N] [SEED] > game.txt     (default 100000 random cases, seed 1)
    ../../build/tests/test_game --static-field SEED N | diff game.txt -

Runs the real FUN_005c96a0 (Static Field's per-unit callback, handed to the
area walk FUN_0056b7e0 by the skill's do function FUN_005c9800) on one
monster, then the real damage application it calls, and prints the
monster's life (stat 6, 256ths) after the hit, or `skip` when the callback
returns 0 (no record made). Inside it run for real: the life read and the
`life >> 8 < 1` exit, the difficulty floor (life <= MulDiv(max life >> 8,
floor, 100): nothing), `MulDiv(life, calc1, 100)` capped at `life - 1`,
`<< 8`, at least calc2, the record (FUN_0056c8e0, lightning -> +0x1c, the
resistance stat 0x29), the negative-resistance step (stat 0x29 < 0:
MulDiv(dmg, 100, 100 - res)), then FUN_0057c6c0 with FUN_0057c1e0 (the
resistance with the attacker's pierce, FUN_0057be00 / FUN_0057bf80, MulDiv
FUN_00483360) and the life subtraction (0057c852..0057c874).

Hooked (not the claim), as tools/emu/life_apply.py hooks them: the stat
getter / setter (a dict), max life (FUN_00625d10), the unit-kind predicates,
the difficulty row, events, logging, the stun / cold / poison side effects;
plus FUN_0057cee0 (the after-hit: get-hit, sounds), not part of the life.

The params block FUN_005c9800 builds (+0 calc2 (256ths), +4 calc1 (%),
+8 DifficultyLevels StaticFieldMin when the game is an expansion one
(game +0x70) else 0, +0xc the skill's ELen, +0x10 its EType) is the case's.
Skills.txt 1.14d: calc1 = par4 = 25, calc2 = par3 = 0; StaticFieldMin
0 / 33 / 50. The cases also vary calc1 and calc2 to reach the `life - 1`
cap and the minimum.

A case: `id max_hp life res pierce floor pct min` (life and min in 256ths).
To stderr: how many cases a line-for-line copy of d2d's old Static Field
(components/game/fight/strike.cpp, `kStaticField` in Fight::spot, upstream
72132b99: `resisted(hit_points * pct / 100, res)`, whole points, no floor,
no cap, no pierce) and of the new one get wrong.
"""
import sys

import life_apply


def xorshift(state):
    x = state[0]
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    state[0] = x
    return x


def s32(v): return v - (1 << 32) if v >> 31 else v


class Game(life_apply.Game):
    def __init__(self):
        super().__init__()
        e = self.e
        e.hook(0x57cee0, lambda e: 0, 2)
        self.params = e.alloc(0x20)
        self.ctx = e.alloc(0x20)
        e.w32(self.ctx, self.game); e.w32(self.ctx + 4, self.attacker); e.w32(self.ctx + 0xc, self.params)

    def static(self, max_hp, life, res, pierce, floor, pct, low):
        e = self.e
        self.reset(max_hp, res, pierce)
        self.stats[(self.target, 6)] = life
        for off, v in ((0, low), (4, pct), (8, floor), (0xc, 0), (0x10, 2)): e.w32(self.params + off, v)
        if not self.e.call(0x5c96a0, ecx=self.ctx, edx=self.target): return "skip"
        return str(s32(self.life() & 0xFFFFFFFF))


def tdiv(a, b): return a // b if (a >= 0) == (b > 0) else -(-a // b)    # C's truncating division


def mul_div(a, b, c):
    """FUN_00483360 as d2d's mul_div (components/rules/combat.hpp) writes it."""
    if c == 0: return 0
    if a <= 0x100000:
        if b <= 0x10000: return tdiv(s32((a * b) & 0xFFFFFFFF), c)
        if c <= b >> 4: return s32((tdiv(b, c) * a) & 0xFFFFFFFF)
    elif c <= a >> 4:
        return s32((tdiv(a, c) * b) & 0xFFFFFFFF)
    return tdiv(a * b, c)


def hurt(life, damage, frac):
    """d2d components/game/ai.cpp hurt(): life = hp x 256 - life_frac."""
    hp, lf = (life + 255) >> 8, (-life) & 255
    if damage <= 0 and frac <= 0: return str(life)
    lf += max(frac, 0)
    damage += lf >> 8
    lf &= 255
    hp -= damage
    if hp < 1 or (hp == 1 and lf > 0): return "0"
    return str((hp << 8) - lf)


def old_d2d(max_hp, life, res, pierce, floor, pct, low):
    hp = (life + 255) >> 8
    dmg = hp * pct // 100
    dmg = 0 if res >= 100 else tdiv(dmg * (100 - res), 100)
    return hurt(life, dmg, 0)


def resisted_fine(fine, res):
    if fine <= 0 or res >= 100: return 0
    if res == 0: return fine
    if fine > 0x100000: return max(s32(((fine // 100) * (100 - res)) & 0xFFFFFFFF), 0)
    return max(fine * (100 - res) // 100, 0)


def new_d2d(max_hp, life, res, pierce, floor, pct, low):
    whole = life >> 8
    if whole < 1: return "skip"
    if floor and whole <= mul_div(max_hp, floor, 100): return "skip"
    dmg = mul_div(whole, pct, 100)
    if whole - dmg < 1: dmg = whole - 1
    dmg = max(dmg << 8, low)
    if res < 0: dmg = mul_div(dmg, 100, 100 - res)
    eff = max(res - pierce, -100) if res < 100 else res
    fine = resisted_fine(dmg, eff)
    return hurt(life, fine >> 8, fine & 255)


def cases(n, seed):
    """The boundaries, exhaustive, then n random cases (test_game --static-field makes the same)."""
    i = 0
    for floor in (0, 33, 50):
        for res in (-150, -100, -99, -50, -1, 0, 1, 33, 50, 99, 100, 101, 150):
            for max_hp in range(1, 7):
                for life in range(1, (max_hp << 8) + 1):
                    yield (f"B{i}", max_hp, life, res, 0, floor, 25, 0); i += 1
    state = [seed or 1]
    for i in range(n):
        max_hp = 1 + xorshift(state) % (3000 if xorshift(state) % 4 == 0 else 300)
        life = 1 + xorshift(state) % (max_hp << 8)
        r = xorshift(state) % 100
        if r < 10: res = 100 + xorshift(state) % 51
        elif r < 30: res = -(xorshift(state) % 201)
        else: res = xorshift(state) % 100
        pierce = 0 if res >= 100 or xorshift(state) % 4 else xorshift(state) % 61
        floor = (0, 33, 50, xorshift(state) % 101)[xorshift(state) % 4]
        pct = 25 if xorshift(state) % 4 else xorshift(state) % 151
        low = 0 if xorshift(state) % 4 else xorshift(state) % 0x2000
        yield (f"R{i}", max_hp, life, res, pierce, floor, pct, low)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    g = Game()
    bad = {"old": 0, "new": 0}
    total = 0
    for case in cases(n, seed):
        got = g.static(*case[1:])
        total += 1
        bad["old"] += old_d2d(*case[1:]) != (str(case[2]) if got == "skip" else got)
        bad["new"] += new_d2d(*case[1:]) != got
        print(" ".join(map(str, case)), got)
    print(f"{total} cases: old d2d wrong on {bad['old']}, new d2d wrong on {bad['new']}", file=sys.stderr)


if __name__ == "__main__": main()
