# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's trade fit test (Inventory.cpp FUN_0063e370 and the pieces it runs) on fake players.

FUN_0063e370 makes two scratch inventories, copies each player's pack grid into one (FUN_0063e100), then puts
each player's page-2 items into the OTHER player's copy (FUN_0063e2e0 -> FUN_0063e1c0, which asks the free-spot
search FUN_0063b850 and fills the cells with -1), and frees both (FUN_0063ac40). The same fixtures and memoised
helpers as inventory_queries.py: FUN_0063b850 answers (found, x, y) from a memo, FUN_00628250 gives each item's page,
FUN_00621050 / FUN_0065c1f0 give the Inventory.txt grid size, FUN_006286c0 the item footprint, and the pool
(FUN_0040b430 / FUN_0040b4b0 / FUN_0040b480) is recorded. FUN_0063add0, memset, memcpy and FUN_0063ac40 run for real.

Covers FUN_0063e100 (copy a pack grid), FUN_0063e1c0 (give an item a spot), FUN_0063e2e0 (every page-2 item gets a
spot) and FUN_0063e370 (the trade fit test: result, and *out = 0 / 1 for which side fails).

Usage: D2_MPQ_DIR=<MPQ folder> uv run python inventory_trade.py
"""
import random

from _inventory_world import cases, check, signed
from inventory_lists import m_ac40
from inventory_queries import (arena_equal, ask, make, run, World)

MAGIC = 0x1020304


def m_add0(w, m, inv, index, dims, allocs):
    """FUN_0063add0 for the pack grid with a size record: the grid when it has that size; a missing grid is allocated
    (FUN_0040b4b0 line 0x2e4 for the array, FUN_0040b430 line 0x2eb for the cells) when the inventory has none yet."""
    count = signed(m.r32(inv + 0x18))
    if index < count:
        grid = m.r32(inv + 0x14) + 16 * index
        assert m.r32(grid + 0xC), "fixtures keep the cell array"
        return grid if (m.r8(grid + 8), m.r8(grid + 9)) == tuple(dims) else 0
    assert count == 0, "fixtures grow only an empty inventory"
    array, cells = next(allocs), next(allocs)
    w.log.append(("alloc", 0x2E4))
    w.log.append(("alloc", 0x2EB))
    m.w32(inv + 0x14, array)
    m.w32(inv + 0x18, index + 1)
    grid = array + 16 * index
    m.w8(grid + 8, dims[0])
    m.w8(grid + 9, dims[1])
    m.w32(grid + 0xC, cells)
    return grid


def m_e100(w, m, inv, unit, allocs):
    """FUN_0063e100 (EAX inventory, EBX unit): copy the unit's pack grid into this inventory's."""
    if inv == 0 or m.r32(inv) != MAGIC:
        return 0
    row = ask(w, 0x621050, unit, 0, 1)
    w.log.append(("dims", row))
    dest = m_add0(w, m, inv, 2, w.player_dims, allocs)
    source_inv = m.r32(unit + 0x60)
    if not dest or not source_inv or m.r32(source_inv) != MAGIC:
        return 0
    source = m_add0(w, m, source_inv, 2, w.player_dims, allocs)
    if not source:
        return 0
    width, height = m.r8(source + 8), m.r8(source + 9)
    if not width or not height or (width, height) != (m.r8(dest + 8), m.r8(dest + 9)):
        return 0
    if not m.r32(source + 0xC) or not m.r32(dest + 0xC):
        return 0
    for n in range(width * height):
        m.w32(m.r32(dest + 0xC) + 4 * n, m.r32(m.r32(source + 0xC) + 4 * n))
    return 1


def m_e1c0(w, m, inv, unit, item):
    """FUN_0063e1c0 (EAX inventory, unit, item): the free-spot answer, if it fits the pack grid, fills its cells with -1."""
    if inv == 0 or m.r32(inv) != MAGIC:
        return 0
    row = ask(w, 0x621050, unit, 0, 1)
    w.log.append(("dims", row))
    grid = m_add0(w, m, inv, 2, w.player_dims, iter(()))
    if not grid:
        return 0
    key = ("b850", inv, item, row)
    w.log.append(key)
    found, x, y = w.memo[key]
    if not found:
        return 0
    w.log.append(("size", item))
    fw, fh = w.footprint.get(item, (1, 1))
    gw, gh = m.r8(grid + 8), m.r8(grid + 9)
    if not fw or not fh or x < 0 or fw + x > gw or y < 0 or fh + y > gh:
        return 0
    for row_ in range(y, y + fh):
        for col in range(x, x + fw):
            m.w32(m.r32(grid + 0xC) + 4 * (gw * row_ + col), 0xFFFFFFFF)
    return 1


def m_e2e0(w, m, unit, other, scratch):
    """FUN_0063e2e0 (EAX unit, stack other, EBX scratch inventory): every page-2 item of the unit gets a spot in scratch."""
    inv = m.r32(unit + 0x60)
    if inv and m.r32(inv) == MAGIC:
        node = m.r32(inv + 0xC)
        while node:
            if m.r32(node) != 4:
                item, nxt = 0, 0
            else:
                item = node
                data = m.r32(node + 0x14)
                nxt = m.r32(data + 0x64) if data else 0
            if ask(w, 0x628250, item) & 0xFF == 2 and not m_e1c0(w, m, scratch, other, item):
                return 0
            node = nxt
    return 1


def player(w, dims, items):
    """A fake player: unit (type 0) whose +0x60 inventory has a dims pack grid and `items` page items on its list."""
    a, e = w.arena, w.e
    inv, _cells, _chain = make(w, chain=0, keep=True)
    unit = e.r32(inv + 8)
    e.w32(unit, 0)
    e.w32(unit + 0x60, inv)
    grid = e.r32(inv + 0x14) + 32
    e.mu.mem_write(grid + 8, bytes(dims))
    cells = e.r32(grid + 0xC)
    for n in range(dims[0] * dims[1]):
        e.w32(cells + 4 * n, random.getrandbits(32))
    prev = 0
    for _ in range(items):
        item = a.item(kind=4 if random.random() < 0.9 else random.choice((1, 3)), inv=inv)
        w.footprint[item] = (random.randint(1, 2), random.randint(1, 2))
        e.w32(a.data(item) + 0x60, prev)
        if prev:
            e.w32(a.data(prev) + 0x64, item)
        else:
            e.w32(inv + 0xC, item)
        prev = item
    e.w32(inv + 0x10, prev)
    return unit, inv


def fresh(w, pool):
    """An empty inventory record the way FUN_0063e370 builds it, or one with a pack grid."""
    inv = w.arena.inventory()
    w.e.w32(inv + 4, pool)
    return inv


def test_pieces(w):
    """FUN_0063e100, 0063e1c0 and 0063e2e0 on their own."""
    e, a = w.e, w.arena
    for case in range(300):
        a.reset()
        w.footprint.clear()
        w.log.clear()
        w.memo.clear()
        dims = (random.randint(2, 8), random.randint(2, 6))
        unit, inv = player(w, dims, random.randint(0, 4))
        w.player_dims = dims if random.random() < 0.8 else (dims[0] + 1, dims[1])
        # FUN_0063e100 into an empty inventory (the grid is allocated) and into one that has the grid
        for target in ("fresh", "grid"):
            if target == "fresh":
                dest = fresh(w, 7)
            else:
                dest, _ = player(w, dims, 0)[1], None
                e.w32(dest, MAGIC)
            result, before, after, game_log = run(w, 0x63E100, regs={"eax": dest, "ebx": unit})
            w.log.clear()
            allocs = iter(list(w.alloc_addrs))
            expect = m_e100(w, before, dest, unit, allocs)
            check(result, expect, f"63e100 result {target} {case}")
            arena_equal(before, after, f"63e100 {target}")
            check(game_log, list(w.log), f"63e100 calls {target}")
        # FUN_0063e1c0 (EAX inventory, unit, item)
        scratch, _ = player(w, dims, 0)[1], None
        item = random.choice([0] + [e.r32(inv + 0xC)]) or a.item()
        w.footprint.setdefault(item, (random.randint(1, 2), random.randint(1, 2)))
        result, before, after, game_log = run(w, 0x63E1C0, unit, item, regs={"eax": scratch})
        w.log.clear()
        expect = m_e1c0(w, before, scratch, unit, item)
        check(result, expect, f"63e1c0 result {case}")
        arena_equal(before, after, "63e1c0")
        check(game_log, list(w.log), "63e1c0 calls")
        # FUN_0063e2e0 (EAX unit, other, EBX scratch)
        other, other_inv = player(w, dims, 0)
        result, before, after, game_log = run(w, 0x63E2E0, other, regs={"eax": unit, "ebx": other_inv})
        w.log.clear()
        expect = m_e2e0(w, before, unit, other, other_inv)
        check(result, expect, f"63e2e0 result {case}")
        arena_equal(before, after, "63e2e0")
        check(game_log, list(w.log), "63e2e0 calls")


def test_trade(w):
    """FUN_0063e370 (pool, player A, player B, &out)."""
    e, a = w.e, w.arena
    seen = set()
    for case in range(400):
        a.reset()
        w.footprint.clear()
        w.log.clear()
        w.memo.clear()
        dims = (random.randint(2, 8), random.randint(2, 6))
        unit_a, _ = player(w, dims, random.randint(0, 4))
        unit_b, _ = player(w, dims, random.randint(0, 4))
        w.player_dims = dims if random.random() < 0.9 else (dims[0], dims[1] + 1)
        out = a.new(4)
        e.w32(out, 0x99)
        pool = random.getrandbits(32)
        result, before, after, game_log = run(w, 0x63E370, pool, unit_a, unit_b, out)
        w.log.clear()
        allocs = iter(list(w.alloc_addrs))
        log = w.log
        inv_a = next(allocs)
        w.log.append(("alloc", 0x22D))
        before.w32(inv_a, MAGIC)
        before.w32(inv_a + 4, pool)
        inv_b = next(allocs)
        w.log.append(("alloc", 0x22D))
        before.w32(inv_b, MAGIC)
        before.w32(inv_b + 4, pool)
        expect = 0
        if m_e100(w, before, inv_a, unit_a, allocs) and m_e100(w, before, inv_b, unit_b, allocs):
            if not m_e2e0(w, before, unit_a, unit_b, inv_b):
                before.w32(out, 0)
            elif not m_e2e0(w, before, unit_b, unit_a, inv_a):
                before.w32(out, 1)
            else:
                expect = 1
        m_ac40(before, inv_a, w.footprint, log)
        m_ac40(before, inv_b, w.footprint, log)
        check(result, expect, f"63e370 result {case}")
        seen.add((expect, before.r32(out)))
        arena_equal(before, after, "63e370")
        check(game_log, list(log), "63e370 calls")
    if len({o for _, o in seen} & {0, 1, 0x99}) < 3 or not any(r for r, _ in seen):
        raise AssertionError(f"the trade fixture must reach a fit, both failing sides and a skipped write: {sorted(seen)}")


def main():
    random.seed(0x1A7E7)
    w = World()
    test_pieces(w)
    test_trade(w)
    print(f"ok: {cases()} Inventory.cpp trade fit cases")


if __name__ == "__main__":
    main()
