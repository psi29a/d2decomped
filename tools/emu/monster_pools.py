# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify five MONSTER pool allocate / release helpers in stock game.exe 1.14d.

    uv run python monster_pools.py [--mutation-check]

Runs game.exe's own code against a hand-written model with the pool entries
(0x40_b430 allocate, 0x40_b480 release) hooked and logged, and compares the
return value, the ordered pool-call log (pool, size or block, source file,
line, flag), the memory written and the bytes the `ret n` popped:

  FUN_00547b50 (MonsterRegion.cpp) ECX pool, EDX array: releases array[i] for
      i = 1 .. count-1 (count = signed dword at [0x744304]+0xc5c), skipping
      zero entries and array[0]; line 0xc5. No return value is claimed.
  FUN_00572ba0 (MonsterAI.cpp) ECX game: allocates 4 bytes from the pool at
      game +0x1c (line 0x17b), zeroes them, returns the block.
  FUN_00572bc0 (MonsterAI.cpp) ECX game, EDX head cell: releases each node of
      the chain whose next link is the dword at node +8 (line 0x188), then the
      cell itself (line 0x18b); the cell is not cleared.
  FUN_005a6490 (MonsterMode.cpp) ECX game: allocates 0x24 bytes (line 0x13f),
      zeroes nine dwords, returns the block.
  FUN_005a64d0 (MonsterMode.cpp) ECX game, EDX block: releases it (line 0x147).

Callee addresses are written with an underscore so the gap map counts only the
functions this oracle claims. `--mutation-check` breaks the model on purpose,
one defect at a time, and requires each to fail. No MPQs are read.
"""
import random
import sys

import _monster_world as mw
from _monster_world import GARBAGE, require

REGION_FREE, TALK_NEW, TALK_FREE, MODE_NEW, MODE_FREE = 0x547B50, 0x572BA0, 0x572BC0, 0x5A6490, 0x5A64D0
FILE_REGION, FILE_AI, FILE_MODE = 0x6E0CF8, 0x6E1560, 0x6E23B0
COUNT_OFFSET = 0xC5C
TABLE_SIZE = 0xE00
MUTATIONS = ("region-from-zero", "region-includes-last", "region-frees-null", "talk-new-unzeroed",
             "talk-free-keeps-cell", "talk-free-skips-last", "mode-new-short", "mode-free-line")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def check_region(world, rng):
    """FUN_00547b50 on a random array and count."""
    machine = world.m
    count = rng.choice((-3, 0, 1, 2, 3, 5, 9, rng.randrange(0, 12)))
    table = machine.alloc(TABLE_SIZE)
    machine.w32(mw.TABLE_PTR, table)
    machine.w32(table + COUNT_OFFSET, count)
    entries = [rng.choice((0, 0, rng.randrange(1, 1 << 30))) for _ in range(12)]
    array = machine.alloc(48)
    world.put_words(array, entries)
    pool = rng.randrange(1, 1 << 30)
    world.run(REGION_FREE, ecx=pool, edx=array, nstack=0)
    start = 0 if mut("region-from-zero") else 1
    stop = count if not mut("region-includes-last") else count + 1
    file_name = world.file_of(FILE_REGION)
    want = [("free", pool, entries[i], file_name, 0xC5, 0) for i in range(start, max(stop, 0))
            if entries[i] or mut("region-frees-null")]
    require(world.log == want, f"region count {count}: log {world.log} != {want}")
    require(world.read_words(array, 12) == entries, "region: the array changed")


def check_talk_new(world, rng):
    """FUN_00572ba0: a zeroed 4-byte head cell from the pool at game +0x1c."""
    game = world.m.alloc(0x40)
    pool = rng.randrange(1, 1 << 30)
    world.m.w32(game + 0x1C, pool)
    cell = world.run(TALK_NEW, ecx=game, nstack=0)
    require(world.log == [("alloc", pool, 4, world.file_of(FILE_AI), 0x17B, 0)], f"talk new: log {world.log}")
    want = bytes([GARBAGE] * 4) if mut("talk-new-unzeroed") else bytes(4)
    require(world.m.read(cell, 4) == want, "talk new: the cell is not zero")


def check_talk_free(world, rng):
    """FUN_00572bc0: release a chain of nodes, then the cell."""
    machine = world.m
    game = machine.alloc(0x40)
    pool = rng.randrange(1, 1 << 30)
    machine.w32(game + 0x1C, pool)
    length = rng.choice((0, 1, 2, 3, 6))
    nodes = [machine.alloc(12) for _ in range(length)]
    for index, node in enumerate(nodes):
        world.put_words(node, [rng.randrange(1 << 32), rng.randrange(1 << 32), nodes[index + 1] if index + 1 < length else 0])
    cell = machine.alloc(4)
    machine.w32(cell, nodes[0] if nodes else 0)
    before = world.read_words(cell, 1)
    world.run(TALK_FREE, ecx=game, edx=cell, nstack=0)
    file_name = world.file_of(FILE_AI)
    freed = nodes[:-1] if mut("talk-free-skips-last") and nodes else nodes
    want = [("free", pool, node, file_name, 0x188, 0) for node in freed]
    want.append(("free", pool, cell, file_name, 0x18B, 0))
    if mut("talk-free-keeps-cell"):
        want.pop()
    require(world.log == want, f"talk free {length} nodes: log {world.log} != {want}")
    require(world.read_words(cell, 1) == before, "talk free: the cell changed")


def check_mode_new(world, rng):
    """FUN_005a6490: a 0x24-byte block with nine zeroed dwords."""
    game = world.m.alloc(0x40)
    pool = rng.randrange(1, 1 << 30)
    world.m.w32(game + 0x1C, pool)
    block = world.run(MODE_NEW, ecx=game, nstack=0)
    require(world.log == [("alloc", pool, 0x24, world.file_of(FILE_MODE), 0x13F, 0)], f"mode new: log {world.log}")
    zeroed = 8 if mut("mode-new-short") else 9
    want = bytes(4 * zeroed) + bytes([GARBAGE] * (4 * (9 - zeroed)))
    require(world.m.read(block, 0x24) == want, "mode new: the block is not nine zero dwords")


def check_mode_free(world, rng):
    """FUN_005a64d0: release the block in EDX."""
    game = world.m.alloc(0x40)
    pool = rng.randrange(1, 1 << 30)
    world.m.w32(game + 0x1C, pool)
    block = rng.randrange(1, 1 << 30)
    world.run(MODE_FREE, ecx=game, edx=block, nstack=0)
    line = 0x148 if mut("mode-free-line") else 0x147
    require(world.log == [("free", pool, block, world.file_of(FILE_MODE), line, 0)], f"mode free: log {world.log}")


def run_all():
    """Run every check on seeded random inputs; returns the case count."""
    rng = random.Random(0x547B50)
    world = mw.World()
    cases = 0
    for check, repeat in ((check_region, 60), (check_talk_new, 10), (check_talk_free, 40),
                          (check_mode_new, 10), (check_mode_free, 10)):
        for _ in range(repeat):
            check(world, rng)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "MONSTER pool allocate and release cases")


if __name__ == "__main__":
    main()
