# SPDX-License-Identifier: GPL-3.0-or-later
"""A burner's hit: the flat reductions scaled by the record's +0x54 / 1024, as
the oracle. FUN_0057c1e0 (0x57c252..0x57c2a4) takes the defender's stat 34
(normal_damage_reduction) and stat 35 (magic_damage_reduction) << 8 and, when
that is above 0 and the record's +0x54 is above 0, scales each by
FUN_00483360(flat << 8, +0x54, 0x400); FUN_0057bf80 then takes the flat off
each field before its percent. +0x54 is the missile's stat 327
damage_framerate (Missiles.txt DamageRate, FUN_0059fa30 / FUN_005ad730).

    uv run python frame_rate_flat.py gen 20000 1 > cases.tsv
    uv run python frame_rate_flat.py game cases.tsv > g.txt     # `id physical fire total` after FUN_0057c1e0

A case line is `id rate dr mdr pct physical fire`: the record's +0x54, the
defender's stats 34, 35 and 36 (whole points, percent), and the record's
physical (+0x08) and fire (+0x10) fields in 256ths. The printed fields are
what FUN_0057c1e0 leaves (game.exe keeps a field below 0, as
rules::reduce_fine_signed does), and the record total +0x4c.

d2d's side: physical > 0 ? physical - frame_rate_flat(dr, rate), then the
percent when that is above 0 : physical; fire > 0 ? fire - frame_rate_flat(mdr,
rate) : fire (fire resistance 0); total their sum.

Runs FUN_0057c1e0 natively with monster_damage.py's harness (stat getter,
state calls, FUN_0057c060's pair percent 100 hooked; no Bone Armor event).
"""
import random
import sys

import monster_damage

RATES = (0, 41, 82, 205, 1024)


def gen(count, seed):
    rng = random.Random(seed)
    for index in range(count):
        rate = rng.choice(RATES + (rng.randint(1, 2048), -rng.randint(1, 100)))
        dr, mdr = (rng.choice((0, 1, 5, 17, rng.randint(0, 60))) for _ in range(2))
        pct = rng.choice((0, 0, rng.randint(1, 50)))
        physical, fire = (rng.choice((0, rng.randint(1, 20000), rng.randint(1, 20000) << 8, rng.randint(1, 4096)))
                          for _ in range(2))
        print(index, rate, dr, mdr, pct, physical, fire)


def game(path):
    oracle = monster_damage.Oracle()
    e = oracle.e
    for line in open(path, encoding="utf-8"):
        index, rate, dr, mdr, pct, physical, fire = map(int, line.split())
        unit_addr = e.alloc(0x200)
        unit = e.view("unit", unit_addr)
        unit.type, unit["class"], unit.mode = 0, 0x10, 1
        oracle.stats.clear()
        oracle.stats.update({(unit_addr, 34): dr, (unit_addr, 35): mdr, (unit_addr, 36): pct})
        oracle.cur["list"] = e.alloc(0x40)
        oracle.state.clear()
        record_addr = e.alloc(0x100)
        record = e.view("damage", record_addr)
        record.flags, record.physical = 1 | 4, physical
        e.w32(record_addr + 0x10, fire)
        e.w32(record_addr + 0x54, rate)
        e.call(0x57C1E0, unit_addr, record_addr, ecx=oracle.game, edx=oracle.attacker)
        print(index, record.physical, e.s32(record_addr + 0x10), record.total)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
