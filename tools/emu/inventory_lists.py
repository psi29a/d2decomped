# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's inventory list and grid linking (Inventory.cpp) on random fake inventories.

Each case builds an inventory with items in its item list (item data +0x60 previous, +0x64 next) and in grids
(+0x6c previous, +0x70 next, grid byte +0x68, 1-based), runs game.exe's function, and compares the whole arena
with a Python model written from the decompile, plus the calls the claim stops at. Hooked and recorded:
FUN_00629ed0 (item id write) and FUN_006277f0 (stats detach) are recorded; FUN_006286c0 (an item's footprint)
answers from a table the case sets; FUN_0040b480 (pool free) records (EDX, line); the four edge-search callees
of FUN_0063b5e0 and the walk choosers are recorded with their arguments. Everything else
runs for real: FUN_0063a810, FUN_0063a970 and FUN_0063aaf0 inside FUN_0063ac40 and FUN_0063ad90.

Covers FUN_0063a970 (item out of a grid), FUN_0063aaf0 (item out of an inventory), FUN_0063ac40 (free an
inventory), FUN_0063e0b0 (empty an inventory), FUN_0063ad90 (remove an owned item), FUN_0063af20 (append to the item list), FUN_0063af70 (append
to a grid list), FUN_0063b300 (does a spot fit) and FUN_0063b5e0 / 0063b790 / 0063b7d0 / 0063b810 (edge search
and free-spot walk choice).

Usage: D2_MPQ_DIR=<MPQ folder> uv run python inventory_lists.py
"""
import random

from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_EDX

import emu
from _inventory_world import Arena, cases, check, signed


class World:
    """An emulator with an arena and the recorded hooks."""

    def __init__(self):
        self.e = e = emu.Emu()
        self.arena = Arena(e)
        self.log = []
        self.footprint = {}
        e.hook(0x629ED0, self._set_id, 2)
        e.hook(0x6277F0, self._detach, 2)
        e.hook(0x6286C0, self._size, 5)
        e.hook(0x40B480, self._free, 3)
        self.calls = []
        for address in (0x63B490, 0x63B540, 0x63B620, 0x63B6E0):
            e.hook(address, lambda x, a=address: self._edge(x, a), 4)

    def _set_id(self, e):
        self.log.append(("id", e.arg(0), e.arg(1)))

    def _detach(self, e):
        self.log.append(("detach", e.arg(0), e.arg(1)))

    def _size(self, e):
        wh = self.footprint.get(e.arg(0))
        if wh:
            e.mu.mem_write(e.arg(1), bytes([wh[0]]))
            e.mu.mem_write(e.arg(2), bytes([wh[1]]))

    def _free(self, e):
        self.log.append(("free", e.mu.reg_read(UC_X86_REG_EDX), e.arg(1)))

    def _edge(self, e, address):
        self.calls.append((address, e.arg(0), e.arg(1), e.arg(2), e.arg(3), e.mu.reg_read(UC_X86_REG_EBX)))
        return 0xC0DE0000 + address


# ---- models, written from the decompile -----------------------------------------------------------------------

def item_data(m, item):
    """The item's data pointer when it is a type 4 unit with data, else 0."""
    if item == 0 or m.r32(item) != 4:
        return 0
    return m.r32(item + 0x14)


def m_a810(m, listhead, item):
    """FUN_0063a810 (ESI list head/tail, EDX item): unlink from the +0x60 / +0x64 list."""
    d = item_data(m, item)
    if not d:
        return
    prev, nxt = m.r32(d + 0x60), m.r32(d + 0x64)
    if prev == 0:
        if m.r32(listhead) == item:
            m.w32(listhead, nxt)
    elif item_data(m, prev):
        m.w32(item_data(m, prev) + 0x64, nxt)
    nxt = m.r32(d + 0x64)
    if nxt == 0:
        if m.r32(listhead + 4) == item:
            m.w32(listhead + 4, m.r32(d + 0x60))
    elif item_data(m, nxt):
        m.w32(item_data(m, nxt) + 0x60, m.r32(d + 0x60))
    m.w32(d + 0x64, 0)
    m.w32(d + 0x60, 0)


def m_a970(m, item, grid, flag, footprint):
    """FUN_0063a970 (EAX item, grid, flag): unlink from the grid's +0x6c / +0x70 list and clear its cells."""
    d = item_data(m, item)
    if not d:
        return
    prev = m.r32(d + 0x6C)
    if prev == 0:
        if m.r32(grid) == item:
            m.w32(grid, m.r32(d + 0x70))
    elif item_data(m, prev):
        m.w32(item_data(m, prev) + 0x70, m.r32(d + 0x70))
    nxt = m.r32(d + 0x70)
    if nxt == 0:
        if m.r32(grid + 4) == item:
            m.w32(grid + 4, m.r32(d + 0x6C))
    elif item_data(m, nxt):
        m.w32(item_data(m, nxt) + 0x6C, m.r32(d + 0x6C))
    cells = m.r32(grid + 0xC)
    if cells:
        path = m.r32(item + 0x2C)
        x, y = signed(m.r32(path + 0xC)), signed(m.r32(path + 0x10))
        width = m.r8(grid + 8)
        if flag == 0:
            m.w32(cells + 4 * (width * y + x), 0)
        else:
            w, h = footprint[item]
            for row in range(y, y + h):
                for col in range(x, x + w):
                    m.w32(cells + 4 * (width * row + col), 0)
    m.w32(d + 0x70, 0)
    m.w32(d + 0x6C, 0)
    m.w8(d + 0x68, 0)


def m_aaf0(m, item, footprint, log):
    """FUN_0063aaf0 (ECX item): out of its grid, the cursor or the item list; clears its place; true when it was in an inventory."""
    d = item_data(m, item)
    if item == 0 or m.r32(item) != 4 or not d:
        return 0
    inv = m.r32(d + 0x5C)
    if inv == 0 or m.r32(inv) != 0x1020304:
        return 0
    grid_byte = m.r8(d + 0x68)
    if 0 < grid_byte < 0x80:
        index = grid_byte - 1
        if index < signed(m.r32(inv + 0x18)):
            m_a970(m, item, index * 16 + m.r32(inv + 0x14), int(index >= 1), footprint)
    if m.r32(inv + 0x20) == item:
        m.w32(inv + 0x20, 0)
    else:
        m_a810(m, inv + 0xC, item)
        m.w32(inv + 0x28, m.r32(inv + 0x28) - 1)
    m.w32(d + 0x5C, 0)
    m.w8(d + 0x68, 0)
    m.w8(d + 0x69, 0)
    log.append(("id", item, 0xFFFFFFFF))
    if m.r32(inv + 8):
        log.append(("detach", m.r32(inv + 8), item))
    if m.r32(inv + 0x1C) == m.r32(item + 0xC):
        m.w32(inv + 0x1C, 0xFFFFFFFF)
    return 1


def m_ac40(m, inv, footprint, log):
    """FUN_0063ac40: cursor and every item removed, the cell arrays, the grid array, the owner link, both node lists, the record freed."""
    if inv == 0 or m.r32(inv) != 0x1020304:
        return
    if m.r32(inv + 0x20):
        m_aaf0(m, m.r32(inv + 0x20), footprint, log)
    while m.r32(inv + 0xC):
        m_aaf0(m, m.r32(inv + 0xC), footprint, log)
    grids = m.r32(inv + 0x14)
    for g in range(signed(m.r32(inv + 0x18))):
        cells = m.r32(grids + 16 * g + 0xC)
        if cells:
            log.append(("free", cells, 0x261))
    if grids:
        log.append(("free", grids, 0x267))
    if m.r32(inv + 8):
        m.w32(m.r32(inv + 8) + 0x60, 0)
    node = m.r32(inv + 0x2C)
    while node:
        log.append(("free", node, 0x273))
        node = m.r32(node + 4)
    node = m.r32(inv + 0x34)
    while node:
        log.append(("free", node, 0x27B))
        node = m.r32(node + 0xC)
    log.append(("free", inv, 0x27F))


def m_af20(m, item, inv):
    """FUN_0063af20 (ECX item, ESI inventory): append to the inventory's item list."""
    d = item_data(m, item)
    if not d:
        return
    tail = m.r32(inv + 0x10)
    if tail == 0:
        m.w32(inv + 0xC, item)
    elif item_data(m, tail):
        m.w32(item_data(m, tail) + 0x64, item)
    m.w32(d + 0x60, m.r32(inv + 0x10))
    m.w32(inv + 0x10, item)
    m.w32(d + 0x5C, inv)


def m_af70(m, item, grid, inv):
    """FUN_0063af70 (ECX item, ESI grid, stack inventory): append to the grid's list when the item belongs to that inventory."""
    d = item_data(m, item)
    if not d or m.r32(d + 0x5C) != inv:
        return
    tail = m.r32(grid + 4)
    if tail == 0:
        m.w32(grid, item)
    elif item_data(m, tail):
        m.w32(item_data(m, tail) + 0x70, item)
    m.w32(d + 0x6C, m.r32(grid + 4))
    m.w32(grid + 4, item)


# ---- case builders ----------------------------------------------------------------------------------------------

def build(w, items=6, grids=3, consistent=True):
    """A random inventory: grids (contiguous 16-byte descriptors), items linked in the item list and in grids."""
    a, e = w.arena, w.e
    inv = a.inventory()
    dims = [(random.randint(1, 10), random.randint(1, 8)) for _ in range(grids)]
    table = a.new(16 * grids)
    for g, (gw, gh) in enumerate(dims):
        cells = a.new(4 * gw * gh)
        for n in range(gw * gh):
            e.w32(cells + 4 * n, random.getrandbits(32) | 1)
        e.mu.mem_write(table + 16 * g + 8, bytes([gw, gh]))
        e.w32(table + 16 * g + 0xC, cells)
    e.w32(inv + 0x14, table)
    e.w32(inv + 0x18, grids)
    placed = []
    for _ in range(items):
        g = random.randint(0, grids)
        fw, fh = (random.randint(1, 3), random.randint(1, 3))
        if g:
            fw, fh = min(fw, dims[g - 1][0]), min(fh, dims[g - 1][1])
            x, y = random.randint(0, dims[g - 1][0] - fw), random.randint(0, dims[g - 1][1] - fh)
        else:
            x, y = random.randint(0, 5), random.randint(0, 5)
        item = a.item(x, y, inv=inv)
        w.footprint[item] = (fw, fh)
        e.mu.mem_write(a.data(item) + 0x68, bytes([g]))
        e.mu.mem_write(a.data(item) + 0x69, bytes([random.randint(0, 3)]))
        placed.append((item, g))
    prev = 0
    for item, _g in placed:
        d = a.data(item)
        e.w32(d + 0x60, prev)
        if prev:
            e.w32(a.data(prev) + 0x64, item)
        else:
            e.w32(inv + 0xC, item)
        prev = item
    e.w32(inv + 0x10, prev)
    e.w32(inv + 0x28, len(placed))
    for g in range(1, grids + 1):
        mine = [item for item, gg in placed if gg == g]
        last = 0
        for item in mine:
            e.w32(a.data(item) + 0x6C, last)
            if last:
                e.w32(a.data(last) + 0x70, item)
            else:
                e.w32(table + 16 * (g - 1), item)
            last = item
        e.w32(table + 16 * (g - 1) + 4, last)
    if random.random() < 0.5:
        e.w32(inv + 0x1C, e.r32(random.choice(placed)[0] + 0xC) if placed else 5)
    if random.random() < 0.5:
        e.w32(inv + 8, a.new(0x80))
    for offset in (0x2C, 0x34):
        head = 0
        for _ in range(random.randint(0, 3)):
            node = a.new(0x10)
            e.w32(node + (4 if offset == 0x2C else 0xC), head)
            head = node
        e.w32(inv + offset, head)
    if random.random() < 0.3:
        cursor = a.item(inv=inv)
        w.footprint[cursor] = (1, 1)
        e.w32(inv + 0x20, cursor)
    return inv, [item for item, _g in placed]


def run(w, address, args, **kw):
    """Run game.exe's function on the arena, return (eax, arena after, hook log); the model gets the arena before."""
    w.log.clear()
    before = w.arena.snapshot()
    result = w.e.call(address, *args, **kw)
    return result, before, w.arena.snapshot(), list(w.log)


def same_arena(model, after, label):
    """The model's memory and game.exe's arena are identical."""
    if model.data != after.data:
        first = next(n for n in range(len(model.data)) if model.data[n] != after.data[n])
        raise AssertionError(f"{label}: arena differs at {model.base + first:#x}: game.exe {after.data[first]:#x}, "
                             f"model {model.data[first]:#x}")
    check(1, 1, label)


def test_remove(w):
    """FUN_0063aaf0 and FUN_0063ad90 on random inventories, for items in a grid, the list, the cursor, and bad ones."""
    for _ in range(300):
        w.arena.reset()
        w.footprint.clear()
        inv, items = build(w, items=random.randint(1, 6), grids=random.randint(1, 3))
        victim = random.choice(items + [w.e.r32(inv + 0x20) or items[0]])
        if random.random() < 0.1:
            w.e.w32(w.arena.data(victim) + 0x5C, random.choice((0, inv + 0x40)))
        result, before, after, log = run(w, 0x63AAF0, (), ecx=victim)
        model_log = []
        expect = m_aaf0(before, victim, w.footprint, model_log)
        check(result, expect, "63aaf0 result")
        same_arena(before, after, "63aaf0")
        check(log, model_log, "63aaf0 calls")
    w.arena.reset()
    build(w)
    check(w.e.call(0x63AAF0, ecx=0), 0, "63aaf0 null item")
    w.arena.reset()
    wrong = w.arena.item(kind=3)
    check(w.e.call(0x63AAF0, ecx=wrong), 0, "63aaf0 not an item")
    for _ in range(100):
        w.arena.reset()
        inv, items = build(w, items=random.randint(1, 5))
        victim = random.choice(items)
        owner = random.choice((inv, inv + 0x40, 0))
        w.log.clear()
        before = w.arena.snapshot()
        result = w.e.call(0x63AD90, owner, victim)
        model_log = []
        owned = owner != 0 and m_owner(before, owner, victim)
        if owned:
            m_aaf0(before, victim, w.footprint, model_log)
        check(result, victim if owned else 0, "63ad90 result")
        same_arena(before, w.arena.snapshot(), "63ad90")
        check(list(w.log), model_log, "63ad90 calls")
    check(w.e.call(0x63AD90, 0, w.arena.item()), 0, "63ad90 null inventory")
    check(w.e.call(0x63AD90, w.arena.inventory(), 0), 0, "63ad90 null item")


def m_owner(m, owner, item):
    """FUN_0063ad50: the item is a type 4 unit with data and its inventory field is owner."""
    d = item_data(m, item)
    return bool(d) and m.r32(d + 0x5C) == owner


def test_grid_out(w):
    """FUN_0063a970 directly, both flags, on items in the middle, at either end, alone."""
    for _ in range(300):
        w.arena.reset()
        w.footprint.clear()
        inv, items = build(w, items=random.randint(1, 6), grids=random.randint(1, 3))
        grid_index = random.randrange(signed(w.e.r32(inv + 0x18)))
        grid = w.e.r32(inv + 0x14) + 16 * grid_index
        victim = random.choice(items)
        flag = random.choice((0, 1))
        result, before, after, log = run(w, 0x63A970, (grid, flag), regs={"eax": victim})
        m_a970(before, victim, grid, flag, w.footprint)
        same_arena(before, after, "63a970")
    for flag in (0, 1):
        w.arena.reset()
        grid = w.arena.new(16)
        w.e.call(0x63A970, grid, flag, regs={"eax": 0})
        w.e.call(0x63A970, grid, flag, regs={"eax": w.arena.item(kind=3)})


def test_free(w):
    """FUN_0063ac40: free an inventory, with and without cursor, owner, node lists."""
    for _ in range(200):
        w.arena.reset()
        w.footprint.clear()
        inv, items = build(w, items=random.randint(0, 6), grids=random.randint(1, 3))
        result, before, after, log = run(w, 0x63AC40, (inv,))
        model_log = []
        m_ac40(before, inv, w.footprint, model_log)
        same_arena(before, after, "63ac40")
        check(log, model_log, "63ac40 calls")
    w.arena.reset()
    bad = w.arena.inventory()
    w.e.w32(bad, 4)
    w.log.clear()
    w.e.call(0x63AC40, bad)
    w.e.call(0x63AC40, 0)
    check(w.log, [], "63ac40 bad inventory freed something")


def test_append(w):
    """FUN_0063af20 and FUN_0063af70."""
    for _ in range(300):
        w.arena.reset()
        inv, items = build(w, items=random.randint(0, 4), grids=2)
        item = w.arena.item()
        if random.random() < 0.15:
            w.e.w32(item + 0x14, 0)
        result, before, after, _ = run(w, 0x63AF20, (), ecx=item, regs={"esi": inv})
        m_af20(before, item, inv)
        same_arena(before, after, "63af20")
        grid = w.e.r32(inv + 0x14) + 16 * random.randint(0, 1)
        item = random.choice(items + [w.arena.item()]) if items else w.arena.item()
        w.e.w32(w.arena.data(item) + 0x5C, random.choice((inv, inv, 0, inv + 0x40)))
        arg = random.choice((inv, inv, inv + 0x40))
        before = w.arena.snapshot()
        w.e.call(0x63AF70, arg, ecx=item, regs={"esi": grid})
        m_af70(before, item, grid, arg)
        same_arena(before, w.arena.snapshot(), "63af70")
    w.arena.reset()
    w.e.call(0x63AF20, ecx=0, regs={"esi": w.arena.inventory()})
    w.e.call(0x63AF20, ecx=w.arena.item(kind=3), regs={"esi": w.arena.inventory()})
    w.e.call(0x63AF70, 0, ecx=0, regs={"esi": w.arena.new(16)})


def test_fits(w):
    """FUN_0063b300 (EAX grid, DL w, ESI x, BL h, stack y): inside the grid and every cell free (FUN_0063a8a0 for real)."""
    e = w.e
    for _ in range(600):
        w.arena.reset()
        gw, gh = random.randint(1, 10), random.randint(1, 8)
        grid = w.arena.grid(gw, gh, filled=False)
        cells = e.r32(grid + 0xC)
        for n in range(gw * gh):
            if random.random() < 0.15:
                e.w32(cells + 4 * n, random.getrandbits(32) | 1)
        x, y = random.randint(0, gw), random.randint(0, gh)
        cw, ch = random.randint(0, 4), random.randint(0, 4)
        inside = x + cw <= gw and y + ch <= gh
        free = inside and all(e.r32(cells + 4 * (gw * row + col)) == 0
                              for row in range(y, y + ch) for col in range(x, x + cw))
        result = e.call(0x63B300, y & 0xFFFFFFFF, regs={"eax": grid, "edx": cw, "esi": x & 0xFFFFFFFF, "ebx": ch})
        check(result & 0xFF, int(free), f"63b300 ({x},{y}) {cw}x{ch} in {gw}x{gh}")


def test_edges(w):
    """FUN_0063b5e0 and the three free-spot walk choosers: a player owner takes the first callee, anything else the second."""
    e = w.e
    pairs = {0x63B5E0: (0x63B490, 0x63B540), 0x63B790: (0x63B620, 0x63B6E0), 0x63B7D0: (0x63B620, 0x63B6E0),
             0x63B810: (0x63B620, 0x63B6E0)}
    for address, (player, other) in pairs.items():
        for kind in (0, 1, 2, 3, 4, 5, None, "none"):
            for _ in range(5):
                w.arena.reset()
                inv = w.arena.inventory()
                if kind == "none":
                    e.w32(inv + 8, 0)
                elif kind is not None:
                    owner = w.arena.new(8)
                    e.w32(owner, kind)
                    e.w32(inv + 8, owner)
                regs = {"ecx": random.getrandbits(32), "edx": random.getrandbits(32), "esi": random.getrandbits(32),
                        "edi": random.getrandbits(32)}
                extra = random.getrandbits(32)
                w.calls.clear()
                result = e.call(address, inv, extra, ecx=regs["ecx"], edx=regs["edx"],
                                regs={"esi": regs["esi"], "edi": regs["edi"]})
                target = player if kind == 0 else other
                # the callee gets ECX, EDX, ESI, EDI pushed in that order (EDI is the first argument) and EBX = the second stack argument
                check(w.calls, [(target, regs["edi"], regs["esi"], regs["edx"], regs["ecx"], extra)],
                      f"{address:08x} owner {kind}")
                check(result, 0xC0DE0000 + target, f"{address:08x} result")
        w.calls.clear()
        e.call(address, 0, 5)
        check(w.calls, [], f"{address:08x} null inventory")


def test_empty(w):
    """FUN_0063e0b0: cursor cleared, wielded id -1, every item removed; false when one cannot be removed."""
    for case in range(200):
        w.arena.reset()
        w.footprint.clear()
        inv, items = build(w, items=random.randint(0, 6), grids=random.randint(1, 3))
        if items and random.random() < 0.25:
            w.e.w32(w.arena.data(random.choice(items)) + 0x5C, inv + 0x40)
        w.e.w32(inv + 0x1C, random.getrandbits(32))
        result, before, after, log = run(w, 0x63E0B0, (inv,))
        model_log = []
        before.w32(inv + 0x20, 0)
        before.w32(inv + 0x1C, 0xFFFFFFFF)
        expect = 1
        while before.r32(inv + 0xC):
            if not m_aaf0(before, before.r32(inv + 0xC), w.footprint, model_log):
                expect = 0
                break
        check(result, expect, f"63e0b0 result {case}")
        same_arena(before, after, "63e0b0")
        check(log, model_log, "63e0b0 calls")
    w.arena.reset()
    check(w.e.call(0x63E0B0, 0), 0, "63e0b0 null")
    bad = w.arena.inventory()
    w.e.w32(bad, 4)
    check(w.e.call(0x63E0B0, bad), 0, "63e0b0 bad magic")


def main():
    random.seed(0x1A7E5)
    w = World()
    for step in (test_remove, test_grid_out, test_free, test_empty, test_append, test_fits, test_edges):
        step(w)
    print(f"ok: {cases()} Inventory.cpp list and grid cases")


if __name__ == "__main__":
    main()
