# SPDX-License-Identifier: GPL-3.0-or-later
"""Cyclone Armor's absorb as the oracle: FUN_005c87a0, the absorb event 0xb
that FUN_0057c1e0 raises (FUN_005c0c30, 0x57c2fc) before its reduction loop
(0x57c378: FUN_0057bf80 per field, the flat then the resistance).

    uv run python cyclone_armor.py gen 20000 1 > cases.tsv
    uv run python cyclone_armor.py game cases.tsv > g.txt

A case line is `id pool mdr fres cres lres fire cold light magic`: the pool
stat in 256ths, the defender's stat 35 (magic_damage_reduction, whole points)
and fire / cold / lightning resistance (stats 39 / 43 / 41; max resistance
75, normal difficulty), and the record's fire (+0x10), cold (+0x24),
lightning (+0x1c) and magic (+0x20) fields in 256ths, unresisted. `game`
prints `id fire cold light magic total pool on`: the fields after
FUN_0057c1e0 (a field below 0 kept, as FUN_0057bf80 leaves it), the record
total +0x4c, the pool left (256ths) and whether the state is still on.

FUN_005c87a0, as run: with the max stat (row +0x56) above 0 and the pool
(row +0x54) not 0, the pool soaks fire, then cold, then lightning, each only
when above 0 (FUN_005c8780: pool <= field: field -= pool, pool 0; else pool
-= field, field 0), all in 256ths; magic and physical untouched. The pool
stat is written back; at 0 the state comes off. Then each field takes the
flat MDR off and the resistance.

Runs on monster_damage.py's harness (stat getter, state list as a dict,
FUN_0057c060's pair percent 100); the Skills row ids (state 0x10, pool stat
0xa4, max stat 0xa5) are placeholders the hooks key on.
"""
import random
import sys

import monster_damage

CYCLONE = 0x5C87A0
POOLS = (0, 0x100, 0x1800, 0x10000)
RESISTS = (0, 40, 75, -30, 100)


def gen(count, seed):
    rng = random.Random(seed)
    for index in range(count):
        pool = rng.choice(POOLS + (rng.randint(1, 0x20000), rng.randint(1, 0x20000)))
        mdr = rng.choice((0, 0, 3, rng.randint(0, 40)))
        res = [rng.choice(RESISTS + (rng.randint(-100, 100),)) for _ in range(3)]
        fields = [rng.choice((0, 0, rng.randint(1, 0x800), rng.randint(1, 0x20000), -rng.randint(1, 0x400)))
                  for _ in range(4)]
        print(index, pool, mdr, *res, *fields)


def game(path):
    oracle = monster_damage.Oracle()
    e = oracle.e
    for line in open(path, encoding="utf-8"):
        index, pool, mdr, fres, cres, lres, fire, cold, light, magic = map(int, line.split())
        unit_addr = e.alloc(0x200)
        unit = e.view("unit", unit_addr)
        unit.type, unit["class"], unit.mode = 0, 0x10, 1
        oracle.stats.clear()
        oracle.stats.update({(unit_addr, 35): mdr, (unit_addr, 39): fres, (unit_addr, 43): cres, (unit_addr, 41): lres})
        oracle.cur["list"] = e.alloc(0x40)
        oracle.state.clear()
        node_addr = e.alloc(0x40)
        node = e.view("event", node_addr)
        node.event, node.active, node.skill, node.handler = 0x0B, 1, monster_damage.SKILL_INDEX, CYCLONE
        unit.events = node_addr
        oracle.state[monster_damage.POOL_STAT], oracle.state[monster_damage.MAX_STAT] = pool, max(pool, 1)
        record_addr = e.alloc(0x100)
        record = e.view("damage", record_addr)
        record.flags = 1 | 4
        for offset, value in ((0x10, fire), (0x24, cold), (0x1C, light), (0x20, magic)):
            e.w32(record_addr + offset, value)
        oracle.out.clear()
        e.call(0x57C1E0, unit_addr, record_addr, ecx=oracle.game, edx=oracle.attacker)
        removed = int(bool(oracle.out.get("removed")))
        fields = [e.s32(record_addr + offset) for offset in (0x10, 0x24, 0x1C, 0x20)]
        print(index, *fields, record.total, 0 if removed else oracle.state[monster_damage.POOL_STAT], 1 - removed)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
