# SPDX-License-Identifier: GPL-3.0-or-later
"""Which self states a new one switches off, as the oracle: FUN_0056c740.

    python3 armor_group.py game > game.tsv
    ../../build/tests/test_armor_group --armor-group | grep -E '^[0-9]+ ' | diff game.tsv -

For every ordered pair of skills X, Y that give a state (Skills.txt
aurastate, +0x80), `X Y kept`: a unit with X's state gets Y's, and kept is 1
when X's state is still on. States of one States.txt group (the word at +0x1e
of a 60-byte row) are switched off by each other: FUN_0056c740 goes through
every state, and for each other one of the same group the unit has clears it
(FUN_00639db0). Frozen Armor, Shiver Armor and Chilling Armor share group 1.

Run for real: FUN_0056c740 with the States table game.exe loads. Hooked (not
the claim): whether the unit has a state (FUN_00639df0), the clear
(FUN_00639db0, noted) and the stat list lookup (FUN_006256b0: none).

A cast reaches FUN_0056c740 through FUN_005c9480 (srvdofunc 18: Frozen,
Shiver, Chilling and Bone Armor, Burst of Speed, Fade, Cyclone Armor, Venom;
call at 0x5c94f4; V read). Groups (States.txt): 1 frozenarmor, shiverarmor,
chillingarmor, bonearmor; 2 quickness, fade (both through do 18); 3 wolf,
bear, feralrage, maul, delerium (states 223, 228, 232, 233, 350; dos 116 and
120, not read, U whether they call FUN_0056c740). The oracle passes 0 as the
argument, the do passes 1; it only decides the new state's own index.
"""
import sys


class Game:
    def __init__(self):
        import skill_tips
        self.e = e = skill_tips.boot()
        tables = e.r32(0x744304)
        self.skills, self.count = e.r32(tables + 0xB98), e.r32(tables + 0xBA0)
        self.unit = e.alloc(0x200)
        self.on = set()
        e.hook(0x639DF0, lambda h: int(h.arg(1) in self.on), 2)
        e.hook(0x639DB0, self.clear, 3)
        e.hook(0x6256B0, lambda h: 0, 2)

    def clear(self, h):
        self.on.discard(h.arg(1))
        return 0

    def state(self, skill):
        return int.from_bytes(self.e.read(self.skills + skill * 0x23C + 0x80, 2), "little", signed=True)

    def kept(self, first, second):
        self.on = {first}
        self.e.call(0x56C740, 0, ecx=self.unit, edx=second)
        return int(first in self.on)


def pairs(g):
    have = [s for s in range(g.count) if g.state(s) > 0]
    return have


def game():
    g = Game()
    have = pairs(g)
    for x in have:
        for y in have:
            sx, sy = g.state(x), g.state(y)
            print(x, y, g.kept(sx, sy) if sx != sy else 1)


if __name__ == "__main__":
    if sys.argv[1:] == ["game"]:
        game()
    else:
        sys.exit(__doc__)
