# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's respawn after death (client packet 0x41) as the oracle for the life, mana and stamina a player gets back.

    uv run python fr_revive.py gen [N [SEED]] > cases.tsv
    uv run python fr_revive.py game cases.tsv > game.tsv
    ../../build/tests/test_respawn_refill --revive-stats cases.tsv | grep -E '^[A-Za-z_]\\w* -?[0-9]+( -?[0-9]+){5}$' > d2d.tsv
    diff game.tsv d2d.tsv          # no output: every case agrees
    uv run python fr_revive.py alive cases.tsv   # the same handler on a unit that is not dead: nothing changes

A case is `id life maxlife mana maxmana stamina maxstamina`, each in 256ths:
the stats of the dead unit when the respawn arrives. `game` prints `id life
mana stamina maxlife maxmana maxstamina` after it ran.

What runs for real: FUN_0054c0e0, the handler server packet table entry 0x41
(table 0x6e0d18, 8 bytes an entry, entry 0x41 at 0x6e0f20; FUN_0054d750 lets
it run only while the unit's mode [unit+0x10] is 0x11, dead). In it the maxima
come from FUN_00625d10 / FUN_00625d60 / FUN_00625db0 (stats 7, 9, 0xb through
the full stat list getter FUN_00624f60) and stats 6, 8, 0xa are set to them
with the real setter FUN_00627260 (0x54c17c, 0x54c18a, 0x54c198). Hooked (not
the claim): the equipment slot object (FUN_005531c0), the item state re-apply
(FUN_0056dfa0 reads an empty inventory), the client updates (FUN_00548520),
state set (FUN_00639db0), the move to town (FUN_0053aec0, FUN_005809d0, the
level and random helpers), the weapon slot re-equip (FUN_00620190 /
FUN_006201d0 / FUN_005701b0). The unit's stats other than the six are zero, so
no item or state adds to the maxima: what the case says is what the unit has.
"""
import random
import sys

import skill_tips

STAT = {"life": 6, "maxlife": 7, "mana": 8, "maxmana": 9, "stamina": 10, "maxstamina": 11}
ORDER = ("life", "maxlife", "mana", "maxmana", "stamina", "maxstamina")
HANDLER = 0x54c0e0


class Oracle:
    def __init__(self):
        self.e = e = skill_tips.boot()
        self.unit = e.alloc(0x200)                          # dword 0 = 0: a player
        self.stat_list = e.alloc(0x100)
        e.w32(self.unit + 0x5C, self.stat_list)
        self.slots = e.alloc(0x100)                         # FUN_005531c0's object: its word at +0xa & 4 must be clear
        self.game = e.alloc(0x2000)
        self.packet = e.alloc(0x10)
        e.mu.mem_write(self.packet, b"\x41")
        self.sent = []
        self.states = []
        e.hook(0x5531c0, lambda e: self.slots, 1)
        e.hook(0x548520, self.send, 2)                      # (value, unit) with ecx = unit, edx = stat id: the client update
        e.hook(0x639db0, self.state, 3)                     # (unit, state, on)
        for addr, n in ((0x53aec0, 2), (0x5809d0, 5), (0x620190, 1), (0x6201d0, 1), (0x5701b0, 1), (0x6427f0, 1),
                        (0x620bb0, 1), (0x61a1b0, 1), (0x61ab70, 1), (0x643ad0, 1), (0x643ce0, 4), (0x52caf0, 1)):
            e.hook(addr, lambda e: 0, n)
        self.reset()

    def send(self, e):
        self.sent.append(e.arg(0))
        return 0

    def state(self, e):
        self.states.append((e.arg(1), e.arg(2)))
        return 0

    def reset(self):
        e = self.e
        e.mu.mem_write(self.stat_list, bytes(0x100))
        e.w32(self.stat_list + 0x10, 0x80000000)            # a base list
        self.sent.clear(); self.states.clear()

    def get(self, stat): return self.e.call(0x625480, self.unit, stat, 0) & 0xFFFFFFFF

    def put(self, stat, value): self.e.call(0x627260, self.unit, stat, value, 0)

    def vector(self): return [self.get(STAT[n]) for n in ("life", "mana", "stamina", "maxlife", "maxmana", "maxstamina")]

    def run(self, vals, mode):
        self.reset()
        for name in ORDER: self.put(STAT[name], vals[name])
        self.e.w32(self.unit + 0x10, mode)
        self.e.call(HANDLER, self.packet, 1, ecx=self.game, edx=self.unit)
        return self.vector()


def cases(path):
    for line in open(path):
        f = line.split()
        if f: yield f[0], dict(zip(ORDER, map(int, f[1:7])))


def gen(n=400, seed=1):
    r = random.Random(seed)
    rows = [  # id life maxlife mana maxmana stamina maxstamina
        ("dead_full", 0, 23296, 11520, 11520, 26112, 26112),
        ("dead_mana_low", 0, 23296, 100, 11520, 26112, 26112),
        ("dead_all_low", 0, 23296, 0, 11520, 0, 26112),
        ("dead_stamina_low", 0, 23296, 11520, 11520, 3 * 256, 26112),
        ("life_left", 5 * 256, 23296, 5 * 256, 11520, 5 * 256, 26112),
        ("frac", 1, 23296 + 77, 1, 11520 + 99, 1, 26112 + 13),
        ("above_max", 30000, 23296, 12000, 11520, 27000, 26112),
        ("max_zero", 5000, 0, 5000, 0, 5000, 0),
    ]
    for i in range(n):
        ml, mm, ms = r.randrange(256, 1 << 20), r.randrange(0, 1 << 18), r.randrange(256, 1 << 18)
        pick = lambda m: r.choice((0, m, r.randrange(0, m + 1), r.randrange(0, m + 1) & ~0xff, min(m + r.randrange(1, 4000), (1 << 21) - 1)))
        rows.append((f"r{i}", 0 if r.random() < 0.6 else pick(ml), ml, pick(mm), mm, pick(ms), ms))
    for row in rows: print(*row)


def main():
    a = sys.argv[1:]
    if a and a[0] == "gen":
        if len(a) > 3 or not all(arg.lstrip("-").isdigit() for arg in a[1:]): sys.exit(__doc__)
        return gen(*map(int, a[1:]))
    if len(a) != 2 or a[0] not in ("game", "alive"): sys.exit(__doc__)
    o = Oracle()
    for cid, vals in cases(a[1]):
        print(cid, *o.run(vals, 0x11 if a[0] == "game" else 1))


if __name__ == "__main__":
    main()
