# SPDX-License-Identifier: GPL-3.0-or-later
"""A missile's element damage applied to a monster's life, by game.exe's own code.

    uv run python life_apply.py [N] [SEED]      (default 100000 random cases, seed 1)

Runs the real FUN_0057c6c0 (apply a damage record to a unit) on a record whose
lightning field (+0x1c, 256ths) is set, with the target's resistance (stat 0x29),
the attacker's pierce (stat 0x14e) and heals (FUN_0057a980, rec 0x12's function),
and prints the life (stat 6, 256ths) after every event. Inside it run for real:
FUN_0057c1e0 (the record's elements through the resistances, FUN_0057bf80 /
FUN_0057be00, MulDiv FUN_00483360, absorb FUN_0057bf10), FUN_0057a980 (heal,
capped at max life) and the life subtraction (LAB_0057c852..0057c874: under
0x100 after the hit is dead, set to 0). Hooked: the stat getter/setter
(FUN_00625480 / FUN_00627260, a dict), max life (FUN_00625d10), the unit-kind
predicates (FUN_0063ee90 hireling: 0, FUN_0063e990 / FUN_0063e940 / FUN_0063edc0: 0),
the difficulty row (FUN_00611d30), and the side effects that are not the claim
(events FUN_005c0c30, stun / cold / freeze / poison / burn FUN_0057aa60..FUN_0057add0,
logging FUN_005347d0 / FUN_005347e0, FUN_005d6410).

The same cases come out of d2d's `test_game --life-apply SEED N`; `diff` them.
To stderr: how many events a line-for-line copy of the OLD d2d (upstream/main:
`resisted(roll >> 8, res)`, whole-point life) and of the NEW d2d get wrong.

Domain (what the claim covers): a plain monster (no hireling: no resist penalty,
no 75 % cap), stat 0x23 (magic damage reduction) 0, absorb stats 0, pierce only
against a non-immune (pierce through an immunity is a separate d2d gap), whole-point
heals, resistance -100..150.
"""
import sys

import emu


def xorshift(state):
    x = state[0]
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    state[0] = x
    return x


class Game:
    def __init__(self):
        e = self.e = emu.Emu()
        self.stats = {}
        self.game = e.alloc(0x100); e.w32(self.game + 0x6c, 0x0100)             # byte +0x6d: difficulty 1; +0x70 expansion
        e.w32(self.game + 0x70, 1)
        self.attacker = e.alloc(0x100)                                        # type 0: a player
        self.target = e.alloc(0x100); e.w32(self.target, 1); e.w32(self.target + 4, 60)   # a monster, class 60 (no hireling)
        self.rec = e.alloc(0x80)
        self.diffrow = e.alloc(0x40)
        self.max_life = 0
        e.hook(0x625480, lambda e: self.stats.get((e.arg(0), e.arg(1)), 0), 3)
        e.hook(0x627260, lambda e: self.stats.__setitem__((e.arg(0), e.arg(1)), e.arg(2) - (1 << 32 if e.arg(2) >> 31 else 0)) or 0, 4)
        e.hook(0x625d10, lambda e: self.max_life, 1)
        e.hook(0x611d30, lambda e: self.diffrow, 1)
        for addr, n in ((0x63ee90, 1), (0x63e990, 1), (0x63e940, 1), (0x63edc0, 1), (0x63e9f0, 2), (0x5c0c30, 3), (0x5347d0, 7),
                        (0x5347e0, 3), (0x61ab00, 1), (0x5541b0, 0), (0x621d50, 2), (0x639df0, 2), (0x57aa60, 0), (0x57aaa0, 0),
                        (0x57aae0, 2), (0x57af80, 2), (0x57b230, 2), (0x57ac50, 3), (0x57add0, 3), (0x5d6410, 0)):
            e.hook(addr, lambda e: 0, n)
        for addr, n in ((0x554200, 1), (0x620bb0, 1), (0x457490, 0)):
            e.hook(addr, lambda e: 1, n)

    def reset(self, max_hp, res, pierce):
        self.stats.clear()
        self.max_life = max_hp << 8
        self.stats[(self.target, 6)] = max_hp << 8
        self.stats[(self.target, 7)] = max_hp << 8
        self.stats[(self.target, 0x29)] = res & 0xFFFFFFFF if res < 0 else res
        self.stats[(self.attacker, 0x14e)] = pierce

    def life(self): return self.stats[(self.target, 6)]

    def hit(self, roll):
        e = self.e
        e.mu.mem_write(self.rec, b"\0" * 0x80)
        e.w32(self.rec, 1); e.w32(self.rec + 4, 1)                            # flags 1 (FUN_005ad730), result word: hit
        e.w32(self.rec + 0x1c, roll & 0xFFFFFFFF)
        e.call(0x57c6c0, self.target, 1, self.rec, ecx=self.game, edx=self.attacker)
        return self.life()

    def heal(self, points):
        self.e.call(0x57a980, points << 8, regs={"edi": self.target})
        return self.life()


# Line-for-line copies of d2d's arithmetic (components/rules/combat.hpp missile_blow,
# components/game/ai.cpp hurt / heal), old = upstream/main af825eda, new = this branch.
def effective_res(res, pierce):
    return max(res - pierce, -100) if res < 100 else res


class Old:
    def __init__(self, max_hp, res, pierce): self.max, self.hp, self.res = max_hp, max_hp, effective_res(res, pierce)
    def life(self): return self.hp << 8 if self.hp > 0 else 0
    def hit(self, roll):
        dmg = 0 if self.res >= 100 else (roll >> 8) * (100 - self.res) // 100
        if dmg <= 0 or self.hp <= 0: return self.life()
        self.hp -= dmg
        return self.life()
    def heal(self, points):
        if self.hp > 0: self.hp = min(self.hp + points, self.max)
        return self.life()


def resisted_fine(fine, res):
    if fine <= 0 or res >= 100: return 0
    if res == 0: return fine
    if fine > 0x100000:
        wrapped = (fine // 100 * (100 - res)) & 0xFFFFFFFF
        return max(wrapped - (1 << 32 if wrapped >> 31 else 0), 0)
    return fine * (100 - res) // 100


class New:
    def __init__(self, max_hp, res, pierce): self.max, self.hp, self.frac, self.res = max_hp, max_hp, 0, effective_res(res, pierce)
    def life(self): return (self.hp << 8) - self.frac if self.hp > 0 else 0
    def hit(self, roll):
        fine = resisted_fine(roll, self.res)
        dmg, frac = fine >> 8, fine & 255
        if (dmg <= 0 and frac <= 0) or self.hp <= 0: return self.life()
        self.frac += frac
        dmg += self.frac >> 8
        self.frac &= 255
        self.hp -= dmg
        if self.hp < 1 or (self.hp == 1 and self.frac > 0): self.hp, self.frac = 0, 0
        return self.life()
    def heal(self, points):
        if self.hp <= 0 or points <= 0: return self.life()
        healed = self.hp + points
        if healed > self.max: self.hp, self.frac = self.max, 0
        else: self.hp = healed
        return self.life()


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 100000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    g = Game()
    bad = {"old": 0, "new": 0}
    events = 0

    def run(tag, max_hp, res, pierce, evs):
        nonlocal events
        g.reset(max_hp, res, pierce)
        old, new = Old(max_hp, res, pierce), New(max_hp, res, pierce)
        out = [f"{tag} {max_hp} {res} {pierce}:"]
        for kind, value in evs:
            got = g.hit(value) if kind == "h" else g.heal(value)
            o = old.hit(value) if kind == "h" else old.heal(value)
            n = new.hit(value) if kind == "h" else new.heal(value)
            events += 1
            bad["old"] += o != got
            bad["new"] += n != got
            out.append(f"{kind}{value}={got}")
            if got == 0: break
        print(" ".join(out))

    # Boundaries, exhaustive: every roll to 1100 (256ths) against the resistances
    # that matter and lives around the death threshold; MulDiv's 0x100000 edge.
    for res in (-100, -99, -50, -1, 0, 1, 33, 50, 67, 99, 100, 101, 150):
        for life in (1, 2, 3, 4):
            for roll in range(1, 1101):
                run("B", life, res, 0, [("h", roll)])
        for roll in (0x100000 - 1, 0x100000, 0x100000 + 1, 0x100000 + 99, 0x100000 + 100, 0x100000 + 101, 0x200000, 0x7fff00, 0, -1, -256, 0x7fffffff):
            run("M", 0x7fffff >> 8, res, 0, [("h", roll)])
    # Random: a Charged Bolt's rolls and beyond, heals between hits.
    state = [seed or 1]
    for i in range(cases):
        max_hp = 1 + xorshift(state) % 300
        r = xorshift(state) % 100
        if r < 10: res = 100 + xorshift(state) % 51
        elif r < 20: res = -(xorshift(state) % 101)
        else: res = xorshift(state) % 100
        pierce = 0 if res >= 100 or xorshift(state) % 4 else xorshift(state) % 61
        evs = []
        for _ in range(1 + xorshift(state) % 12):
            if xorshift(state) % 5 == 0: evs.append(("+", xorshift(state) % 40))
            else:
                k = xorshift(state) % 8
                if k == 0: roll = 1 + xorshift(state) % 0x400
                elif k == 1: roll = (255, 256, 257, 511, 512, 513, (max_hp << 8) - 1, max_hp << 8, (max_hp << 8) + 1, (max_hp << 8) - 255, (max_hp << 8) - 256, (max_hp << 8) - 257)[xorshift(state) % 12]
                elif k == 2: roll = 1 + xorshift(state) % 0x300000
                else: roll = 1 + xorshift(state) % 0x2000
                evs.append(("h", max(roll, 1)))
        run(f"R{i}", max_hp, res, pierce, evs)
    print(f"{events} events: old d2d wrong on {bad['old']}, new d2d wrong on {bad['new']}", file=sys.stderr)


if __name__ == "__main__": main()
