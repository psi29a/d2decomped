# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's monster damage record as the oracle: FUN_0057b420 (the physical
roll on the attacker's seed) and FUN_0057c1e0 (the record's absorb event 0xb,
Bone Armor's FUN_005c6370 through the dispatcher FUN_005c0c30, then the
reduction loop with FUN_0057bf80: the flat, then the percent), run natively on
a defender unit.

    uv run python monster_damage.py gen 20000 20261103 > cases.tsv
    uv run python monster_damage.py game cases.tsv > game.tsv
    ../../build/tests/test_combat --monster-damage cases.tsv > d2d.tsv     # tests/test_combat.cpp
    uv run python monster_damage.py compare cases.tsv game.tsv d2d.tsv

A case is one frame: `id kind class pool pct flat n seed_low seed_high
{min max} x 3`: up to three sure blows from one attacker seed, each rolled
from its class's A1 range (the Act 1 classes' MonStats, fixed in CLASSES),
the defender's percent (stat 36) and flat (stat 34) reduction, and a Bone
Armor pool in 256ths. `game` and d2d's test print `id life pool seed_low
seed_high`: what the life stat loses (the sum of the records' totals +0x4c
above 0, in 256ths), the pool left, the attacker's seed after.

Kinds (N frames per class, 8 classes): roll (no reduction, no pool, 1 blow),
reduce (percent 0..50 and flat, 1 blow), absorb (1..3 blows into a pool whose
fraction is set in a third of the cases), negpct (Andariel only: a percent
-60..-1), both (a reduction and a pool: game.exe absorbs before it reduces),
then `exact` (6 x N/4 frames): a record built without a roll, `min << 8`
straight into FUN_0057c1e0, as Corpse Explosion (FUN_005c4d10) and Diablo's
cold touch (FUN_005cce80) do; the seed must not move.

Hooked (not the claim): the stat getter FUN_00625480 (the defender's stat 34
and 36, the attacker's 21 and 22; every other stat 0), FUN_0057c060 (the
record's damage percent: 100) and the state list calls Bone Armor reads and
writes (FUN_006256b0, FUN_00625d00, FUN_00627150: a dict), the state's
removal and graphics, the mode calls, the Skills row lookup. Nothing else
runs but the two functions above and their callees.

Not covered: Energy Shield, Cyclone Armor, the flat's +0x54 / 1024 scale,
elements, a missile's FUN_005a89a0 roll, a merc or a pet as the defender.
Tags: the roll and the order (absorb, then flat, then percent) are V (this
oracle's run against game.exe's code); the Bone Armor row ids (state 0x10,
pool stat 0xa4, max stat 0xa5, skill row 1) are placeholders the hooks key on
(U: game.exe reads them from the row).
"""
import random
import sys

CLASSES = [("fallen1", 1, 2), ("zombie1", 1, 3), ("skeleton1", 1, 3), ("quillrat1", 1, 2), ("corruptrogue1", 1, 3),
           ("brute1", 2, 3), ("bloodraven", 4, 6), ("andariel", 6, 19)]
BONE = 0x5c6370
STATE, POOL_STAT, MAX_STAT, SKILL_INDEX = 0x10, 0xa4, 0xa5, 1
SKILL_ROW = 0x23c


def frame_line(index, case):
    kind, name, low, high, pool, pct, flat, count, seed_low, seed_high = case
    ranges = f"{low} {high} " * count + "0 0 " * (3 - count)
    return f"{index} {kind} {name} {pool} {pct} {flat} {count} {seed_low} {seed_high} {ranges.strip()}"


def parse(line):
    word = line.split()
    index, kind, name = int(word[0]), word[1], word[2]
    pool, pct, flat, count, seed_low, seed_high = map(int, word[3:9])
    ranges = [(int(word[9 + 2 * k]), int(word[10 + 2 * k])) for k in range(count)]
    return index, kind, name, pool, pct, flat, seed_low, seed_high, ranges


def blank(rng, cls, kind, pool, pct, flat, blows):
    """A case of class `cls` (name, min, max) drawing its seed state."""
    name, low, high = cls
    return (kind, name, low, high, pool, pct, flat, blows, rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))))


def gen(count, seed):
    """Case lines. The classes' sets come first in the order a run of N frames per class draws them."""
    rng, cases = random.Random(seed), []
    for cls in CLASSES:
        name, low, high = cls
        for _ in range(count):
            cases.append(blank(rng, cls, "roll", 0, 0, 0, 1))
        for _ in range(count):
            pct = rng.choice((0, 50, rng.randrange(0, 51), rng.randrange(0, 51)))
            flat = rng.choice((0, rng.randrange(0, high + 3), rng.randrange(0, 3)))
            cases.append(blank(rng, cls, "reduce", 0, pct, flat, 1))
        for _ in range(count):
            blows = rng.choice((1, 2, 2, 3))
            pool = 0 if rng.random() < 0.1 else rng.randrange(1, blows * high * 256 + 600)
            if rng.random() < 0.5:
                pool = (pool >> 8) << 8
            cases.append(blank(rng, cls, "absorb", pool, 0, 0, blows))
        if name == "andariel":
            for _ in range(max(count // 2, 1)):
                cases.append(blank(rng, cls, "negpct", 0, -rng.randrange(1, 61), rng.randrange(0, 4), 1))
        for _ in range(max(count // 4, 1)):
            blows = rng.choice((1, 2, 3))
            cases.append(blank(rng, cls, "both", rng.randrange(1, blows * high * 256 + 600), rng.randrange(1, 51), rng.randrange(1, high + 1), blows))
    for _ in range(6 * max(count // 4, 1)):
        value = rng.randint(0, 40)
        pct = rng.choice((0, rng.randint(1, 50)))
        flat = rng.choice((0, rng.randint(0, value + 2)))
        pool = rng.randrange(1, 2 * value * 256 + 600) if rng.random() < 0.75 else 0
        cases.append(("exact", "x0_40", value, value, pool, pct, flat, 1, rng.getrandbits(32), rng.choice((666, rng.getrandbits(32)))))
    return [frame_line(index, case) for index, case in enumerate(cases)]


class Oracle:
    def __init__(self):
        import emu
        self.e = e = emu.Emu()
        self.stats, self.state, self.out, self.cur = {}, {}, {}, {}
        zero_row = e.alloc(0x400)
        e.hook(0x625480, lambda e: self.stats.get((e.arg(0), e.arg(1)), 0) & 0xFFFFFFFF, 3)
        e.hook(0x6256b0, lambda e: self.cur["list"] if e.arg(1) == STATE else 0, 2)
        e.hook(0x625d00, lambda e: self.state.get(e.arg(1), 0) if e.arg(0) == self.cur["list"] else 0, 3)
        e.hook(0x627150, self.set_state, 4)
        e.hook(0x639db0, lambda e: self.out.__setitem__("removed", 1), 3)
        e.stub(0, {0x625500: 3, 0x535bc0: 0, 0x620bb0: 1, 0x639df0: 2, 0x639e30: 3, 0x6277e0: 2, 0x626cd0: 1, 0x40b480: 3,
                   0x63ee90: 1, 0x63e990: 1, 0x63e940: 1})
        e.stub(zero_row, {0x611d30: 1})
        e.stub(100, {0x57c060: 0})
        data = e.alloc(0x1000)
        skills = e.alloc(SKILL_ROW * 4)
        e.w32(0x744304, data)                                    # DataTables: the Skills table at +0xb98, its count at +0xba0
        e.w32(data + 0xba0, 4)
        e.w32(data + 0xb98, skills)
        e.w32(data + 0xc4, 0x100)
        e.w32(data + 0xbd4, 0x200)
        row = e.view("skill", skills + SKILL_INDEX * SKILL_ROW)
        row.state, row.max_stat, row.pool_stat = STATE, MAX_STAT, POOL_STAT
        self.game = e.alloc(0x1000)
        self.attacker = e.alloc(0x200)
        attacker = e.view("unit", self.attacker)
        attacker.type, attacker["class"] = 1, 0x10

    def set_state(self, e):
        if e.arg(0) == self.cur["list"]:
            self.state[e.arg(1)] = e.arg(2)

    def run(self, case):
        e = self.e
        _, kind, _, pool, pct, flat, seed_low, seed_high, ranges = case
        unit_addr = e.alloc(0x200)
        unit = e.view("unit", unit_addr)
        unit.type, unit["class"], unit.mode = 0, 0x10, 1
        self.stats.clear()
        self.stats[(unit_addr, 34)], self.stats[(unit_addr, 36)] = flat, pct
        attacker = e.view("unit", self.attacker)
        attacker.seed_low, attacker.seed_high = seed_low, seed_high
        self.cur["list"] = e.alloc(0x40)
        self.state.clear()
        if pool > 0:
            node_addr = e.alloc(0x40)
            node = e.view("event", node_addr)
            node.event, node.active, node.skill, node.handler = 0x0b, 1, SKILL_INDEX, BONE
            unit.events = node_addr
            self.state[POOL_STAT], self.state[MAX_STAT] = pool, max(pool, 1)
        total = 0
        for low, high in ranges:
            self.stats[(self.attacker, 21)], self.stats[(self.attacker, 22)] = low, high
            record_addr = e.alloc(0x100)
            record = e.view("damage", record_addr)
            if kind == "exact":
                fine = low << 8
            else:
                fine = signed(e.call(0x57b420, 0, 0, 0, 0, 0, 0x80, ecx=self.attacker, edx=1) & 0xFFFFFFFF, 32)
            record.flags, record.physical = 1 | 4, fine
            self.out.clear()
            e.call(0x57c1e0, unit_addr, record_addr, ecx=self.game, edx=self.attacker)
            if self.out.get("removed"):
                unit.events = 0
            total += max(record.total, 0)
        left = 0 if pool > 0 and not unit.events else self.state.get(POOL_STAT, 0)
        return total, left, attacker.seed_low, attacker.seed_high


def signed(value, bits):
    return value - (1 << bits) if value >= 1 << (bits - 1) else value


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read().splitlines()


def compare(cases, game, d2d):
    """{(kind, class): [frames, life differ, pool differ, seed differ]} from three line lists."""
    answers = {line.split()[0]: tuple(map(int, line.split()[1:])) for line in game}
    sets = {}
    for case_line, theirs in zip(cases, d2d, strict=True):
        index, kind, name = case_line.split()[:3]
        ours, mine = answers[index], tuple(map(int, theirs.split()[1:]))
        row = sets.setdefault((kind, name), [0, 0, 0, 0])
        row[0] += 1
        row[1] += ours[0] != mine[0]
        row[2] += ours[1] != mine[1]
        row[3] += ours[2:] != mine[2:]
    return sets


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen":
        print("\n".join(gen(int(sys.argv[2]), int(sys.argv[3]))))
    elif cmd == "game":
        oracle = Oracle()
        for line in read(sys.argv[2]):
            case = parse(line)
            print(case[0], *oracle.run(case))
    elif cmd == "compare":
        sets = compare(*(read(path) for path in sys.argv[2:5]))
        print("kind class frames | life differ, pool differ, seed differ")
        kinds = {}
        for (kind, name), row in sets.items():
            print(kind, name, row[0], "|", *row[1:])
            total = kinds.setdefault(kind, [0, 0, 0, 0])
            for k in range(4):
                total[k] += row[k]
        for kind, row in kinds.items():
            print(f"# {kind}: frames {row[0]} life differ {row[1]} pool differ {row[2]} seed differ {row[3]}")
        sys.exit(1 if any(any(row[1:]) for row in kinds.values()) else 0)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
