# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the order the client walks its unit tables in, in stock game.exe 1.14d.

    uv run python unit_table_order.py [--mutation-check]

FUN_00463a50 (the unit in ESI) inserts a unit into the chain of bucket
`id & 0x7f` of table A (0x7a5e70 + type * 0x200), before the first node whose
id is not above its own (unsigned). FUN_00464950 / 00464990 / 004649d0 (the
callback in ECX) then walk the players' (type 0), monsters' (1) and objects'
(2) tables from bucket 0 to 0x7f, each chain from its head: the order
FUN_004a0e70 draws overhead texts in. The oracle inserts random units in a
random order (many ids sharing a bucket, ids above 0x80000000 too), walks the
three tables for real and compares the callback order with the model of d2d's
`overhead_walk_before` (apps/d2d/render/overhead.hpp, tests/test_overhead_walk.cpp):
a lower bucket first, a higher id first within a bucket, unsigned.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import sys
from functools import cmp_to_key

import _monster_world as mw
import unit_client as uc
from _monster_world import require
from units_small import MASK, Scene

TABLE_A = 0x7A_5E70
INSERT = 0x46_3A50
WALKS = (0x46_4950, 0x46_4990, 0x46_49D0)   # players, monsters, objects
MUTATIONS = ("ids-ascending", "bucket-descending", "id-signed", "tables-swapped", "bucket-by-id-low6")
mutation = None
CASES = 0


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def walk_before(first, second):
    """d2d's overhead_walk_before: bucket (id & 0x7f) ascending, then the higher id first (unsigned)."""
    mask = 0x3F if mut("bucket-by-id-low6") else 0x7F
    if mut("id-signed"):
        first, second = (first ^ 0x80000000), (second ^ 0x80000000)
    first_bucket, second_bucket = first & mask, second & mask
    if first_bucket != second_bucket:
        return (first_bucket > second_bucket) if mut("bucket-descending") else first_bucket < second_bucket
    return (first < second) if mut("ids-ascending") else first > second


def model_order(units):
    """Unit ids of one table in the model's walk order."""
    def compare(first, second):
        """-1 when `first` is walked first."""
        return -1 if walk_before(first, second) else 1 if walk_before(second, first) else 0
    return sorted(units, key=cmp_to_key(compare))


def run_all():
    """Insert random units for real, walk the three tables for real, compare. Returns the case count."""
    global CASES
    CASES = 0
    rng = random.Random(20261124)
    rig = uc.Rig()
    rig.logger(0x4C_C160, 0, "hook-4cc160", ret=0, regs=())   # the client hook FUN_00463a50 calls before linking
    for _ in range(60):
        rig.write(TABLE_A, bytes(0xC00))
        sc = Scene(rig, rng)
        by_table = {0: [], 1: [], 2: []}
        units = {}
        taken = set()
        for _ in range(rng.randrange(2, 24)):
            kind = rng.randrange(3)
            bucket = rng.choice((0, 1, 2, 0x7F, rng.randrange(128)))
            unit_id = (rng.randrange(1 << 25) << 7 | bucket) & MASK
            if rng.random() < 0.2:
                unit_id |= 0x8000_0000
            if (kind, unit_id) in taken:
                continue
            taken.add((kind, unit_id))
            unit = sc.block(0x100)
            sc.put(unit, kind)
            sc.put(unit + 0xC, unit_id)
            sc.put(unit + 0xC4, 0)   # no 0x200000: table A
            sc.put(unit + 0xE4, 0)
            require(rig.run(INSERT, regs={"esi": unit}) == 1, f"insert type {kind} id {unit_id:#x}")
            by_table[kind].append(unit_id)
            units[unit] = unit_id
        tables = [0, 1, 2]
        if mut("tables-swapped"):
            tables = [1, 0, 2]
        for position, walk in zip(tables, WALKS):
            seen = []
            callback = rig.slot(lambda c, log=seen: log.append(c.mu.reg_read(mw.UC_X86_REG_ECX)) or 0, 0)
            rig.run(walk, ecx=callback, nstack=0)
            got = [units[node] for node in seen]
            want = model_order(by_table[position])
            CASES += 1
            require(got == want, f"walk {walk:#x}: game.exe {[hex(i) for i in got]} != model {[hex(i) for i in want]}")
    return CASES


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "unit table order cases")


if __name__ == "__main__":
    main()
