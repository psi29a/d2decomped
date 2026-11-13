# SPDX-License-Identifier: GPL-3.0-or-later
"""An armor's pool over a frame's damage records, then the element mark: the oracle for `test_combat --get-hit-absorb`.

    uv run python hit_absorb.py gen 100000 1 > cases.tsv
    uv run python hit_absorb.py game cases.tsv > g.txt
    ../../build/tests/test_combat --get-hit-absorb < cases.tsv | diff g.txt -

A case line is one damage record landing on one unit: `id armor pool
physical fire ltng cold poison next`. armor 0 is Bone Armor (FUN_005c6370),
1 Cyclone Armor (FUN_005c87a0); pool the armor's pool stat in 256ths at the
frame's first record; physical the record's +0x08 in 256ths; fire / ltng /
cold its +0x10 / +0x1c / +0x24 in whole points (written << 8); poison 1 puts
256 in its +0x28. `next` 1: the following line lands on the same unit in the
same frame, the pool carrying on.

`game` runs, per record, two pieces of game.exe natively and prints
`id total mark pool`:

1. FUN_0057c1e0 (the unit takes the record's fields): the state event 0xb
   it raises through FUN_005c0c30 (0x57c2fc) runs the armor's handler on
   this record before the reduction loop and the total. `total` is the
   record's +0x4c less its poison field, which is in the sum and which
   neither armor reads.
2. FUN_0057ce30 on the record as FUN_0057c1e0 left it (EAX 0, ESI the
   record): the hit class with its element bits. `mark` is 1 when a bit of
   0xf0 is set: a fire, lightning, cold or poison field still above 0 (the
   record's flag 0x2000 is off).

`pool` is the pool stat after the frame's last record (0 once the state
came off), -1 on the records before it.

Runs on monster_damage.py's harness (the stat getter, the state list as a
dict, FUN_0057c060's pair percent 100; the Skills row ids are placeholders
the hooks key on). Every defender stat is 0: no reduction, no resistance.

Not covered: the reductions and resistances after the absorb
(monster_damage.py, cyclone_armor.py), Energy Shield, magic (+0x20), the
get-hit size test on what is left (hit_rule.py).
"""
import random
import sys

import monster_damage

TAKE_FIELDS, HIT_CLASS = 0x57C1E0, 0x57CE30
HANDLERS = (monster_damage.BONE, 0x5C87A0)
FIRE, LTNG, COLD, POISON = 0x10, 0x1C, 0x24, 0x28


def gen(count, seed):
    """Print `count` case lines from `seed`: frames of one to three records with a pool near what they carry."""
    rng = random.Random(seed)
    index = 0
    while index < count:
        armor = rng.randint(0, 1)
        records = []
        for _ in range(rng.choice((1, 1, 2, 3))):
            physical = rng.choice((0, rng.randint(1, 40) << 8, rng.randint(1, 0x3000)))
            elements = [0, 0, 0]
            for _ in range(rng.choice((0, 0, 1, 1, 2, 3))):
                elements[rng.randint(0, 2)] = rng.randint(1, 40)
            records.append((physical, *elements, int(rng.random() < 0.15)))
        mine = [physical if armor == 0 else sum(elements) << 8 for physical, *elements, _ in records]
        edges = [0, mine[0], sum(mine), sum(mine[:-1]), mine[0] - 1, sum(mine) + 1, sum(mine) - 1]
        pool = max(0, rng.choice(edges + [rng.randint(0, sum(mine) + 0x400)] * 5))
        for at, record in enumerate(records):
            print(index, armor, pool, *record, int(at + 1 < len(records)))
            index += 1


def game(path):
    """Run each case line of `path` through FUN_0057c1e0 and FUN_0057ce30; print `id total mark pool`."""
    oracle = monster_damage.Oracle()
    e = oracle.e
    unit, carry = None, False
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        index, armor, pool, physical, fire, ltng, cold, poison, nxt = map(int, line.split())
        if not carry:
            unit_addr = e.alloc(0x200)
            unit = e.view("unit", unit_addr)
            unit.type, unit["class"], unit.mode = 0, 0x10, 1
            oracle.stats.clear()
            oracle.state.clear()
            oracle.cur["list"] = e.alloc(0x40)
            if pool > 0:
                node_addr = e.alloc(0x40)
                node = e.view("event", node_addr)
                node.event, node.active, node.skill, node.handler = 0x0B, 1, monster_damage.SKILL_INDEX, HANDLERS[armor]
                unit.events = node_addr
                oracle.state[monster_damage.POOL_STAT], oracle.state[monster_damage.MAX_STAT] = pool, max(pool, 1)
        record_addr = e.alloc(0x100)
        record = e.view("damage", record_addr)
        record.flags, record.physical = 1 | 4, physical
        for offset, value in ((FIRE, fire << 8), (LTNG, ltng << 8), (COLD, cold << 8), (POISON, 256 if poison else 0)):
            e.w32(record_addr + offset, value)
        oracle.out.clear()
        e.call(TAKE_FIELDS, unit_addr, record_addr, ecx=oracle.game, edx=oracle.attacker)
        if oracle.out.get("removed"):
            unit.events = 0
        hit_class = e.call(HIT_CLASS, regs={"eax": 0, "esi": record_addr})
        carry = nxt != 0
        left = -1 if carry else 0 if not unit.events else oracle.state.get(monster_damage.POOL_STAT, 0)
        print(index, record.total - e.s32(record_addr + POISON), int(hit_class & 0xF0 != 0), left)


def main():
    """`gen N SEED` or `game CASES`."""
    args = sys.argv[1:]
    if len(args) == 3 and args[0] == "gen":
        gen(int(args[1]), int(args[2]))
    elif len(args) == 2 and args[0] == "game":
        game(args[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
