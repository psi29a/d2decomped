# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's Inventory.cpp / InvMode.cpp queries and node lists on random fake inventories.

The queries walk the body grid (cells for body locations, the +0x70 chain for the list) and ask item-property
helpers about each item. Those helpers are hooked and answer from a memo of random answers keyed by (function,
arguments); the emulator and the Python model both ask the memo, and both log each ask, so a function passes only
when it returns the same value, leaves the arena the same, AND calls the same helpers with the same arguments in
the same order. FUN_0063add0 (the grid finder) and FUN_0063a7e0 run for real; FUN_0040b430 / FUN_0040b480
(pool alloc and free) are recorded.

Covers FUN_0063be40, 0063bf90, 0063c050, 0063c3b0, 0063c830, 0063c8f0, 0063cab0, 0063cbd0, 0063cc70, 0063cd80,
0063ce60, 0063cf30, 0063d010 (grid 2, still checked against the body grid's size), 0063d0f0, 0063d470, 0063d4e0,
0063d590 and 0063e020.

Usage: D2_MPQ_DIR=<MPQ folder> uv run python inventory_queries.py
"""
import random

from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_ESI

import emu
from _inventory_world import Arena, cases, check, signed

BODY = (0x0D, 0x01)
BELT = (0x10, 0x01)
# helper -> (stack args, generator of one answer)
HELPERS = {
    0x629BB0: (2, lambda: int(random.random() < 0.4)),                 # item is of ItemTypes row n
    0x62A4E0: (1, lambda: int(random.random() < 0.7)),
    0x628950: (2, lambda: int(random.random() < 0.5)),
    0x629FE0: (1, lambda: random.choice((0, 2, 3, 0xC, 5, 9))),        # a short
    0x628660: (1, lambda: random.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 0x10, 0x20, 0x1FF))),
    0x62B400: (1, lambda: random.choice((0x12, 0x12, 5, 7))),
    0x627F80: (2, lambda: random.randint(1, 2)),                        # a short
    0x625480: (3, lambda: random.randint(0, 4)),
    0x6295B0: (1, lambda: random.randint(0, 4)),
    0x627E70: (1, lambda: random.choice((5, 5, 1, 2))),
    0x629DA0: (1, lambda: random.randint(0, 2)),
    0x62BAD0: (1, lambda: int(random.random() < 0.8)),                  # d2d_rules_beltable
    0x63BEF0: (1, None),                                                # d2d_rules_wielded_weapon: set per case
    0x6488C0: (1, lambda: random.randint(0, 15)),                       # d2d_path_get_x
    0x621050: (3, lambda: random.getrandbits(8)),
    0x627D40: (1, lambda: random.choice((4, 4, 5, 5, 3, 0))),          # a byte
    0x628250: (1, lambda: random.choice((2, 2, 2, 1, 0))),             # the item's page (a byte)
}


class World:
    """An emulator with an arena, the memoised helpers and a call log."""

    def __init__(self, real=()):
        self.e = e = emu.Emu(real=real)
        self.arena = Arena(e)
        self.memo = {}
        self.log = []
        self.wielded = 0
        self.footprint = {}
        for address, (nstack, _gen) in HELPERS.items():
            e.hook(address, lambda x, a=address, n=nstack: self._ask_emu(x, a, n), nstack)
        e.hook(0x633640, self._class_of_code, 2)
        e.hook(0x65C1F0, self._record_dims, 3)
        e.hook(0x6286C0, self._footprint, 5)
        e.hook(0x6335F0, self._record, 1)
        e.hook(0x40B430, self._alloc, 3)
        e.hook(0x40B4B0, self._alloc_size, 4)
        e.hook(0x63B850, self._place, 6)
        e.hook(0x40B480, self._free, 3)

    # emulator side
    def _ask_emu(self, e, address, nstack):
        key = (address,) + tuple(e.arg(i) for i in range(nstack))
        return self.answer(key)

    def answer(self, key):
        """The memoised answer for (function, args...), logged; shared by game.exe's hook and the model."""
        self.log.append(key)
        if key not in self.memo:
            if key[0] == 0x63BEF0:
                self.memo[key] = self.wielded
            else:
                self.memo[key] = HELPERS[key[0]][1]()
        return self.memo[key]

    def _class_of_code(self, e):
        """FUN_00633640 (code, &class): writes a memoised class, returns whether it exists."""
        found, klass = self.answer_code(e.arg(0))
        if found:
            e.w32(e.arg(1), klass)
        return found     # the record pointer when found

    def answer_code(self, code):
        """The memoised (found, class) for a code, logged."""
        key = (0x633640, code)
        self.log.append(key)
        if key not in self.memo:
            record = self.arena_rec() if random.random() < 0.85 else 0
            if record:
                self.e.w32(record + 0x80, random.choice((0x2074696C, random.getrandbits(32))))
                self.e.w32(record + 0x90, random.choice((0, random.getrandbits(32))))
            self.memo[key] = (record, random.randint(0, 3))
        return self.memo[key]

    def _record_dims(self, e):
        """FUN_0065c1f0 (id, 0, &record): the Inventory.txt grid size bytes."""
        e.mu.mem_write(e.arg(2), bytes(self.player_dims))
        self.log.append(("dims", e.arg(0)))
        return 0

    player_dims = BODY
    pack_dims = BODY
    pack_items = ()

    def _footprint(self, e):
        w, h = self.footprint.get(e.arg(0), (1, 1))
        e.mu.mem_write(e.arg(1), bytes([w]))
        e.mu.mem_write(e.arg(2), bytes([h]))
        self.log.append(("size", e.arg(0)))

    def _record(self, e):
        """FUN_006335f0 (class): an Items-like record whose bytes +0x11d and +0x130 are memoised 0/1."""
        key = ("rec", e.arg(0))
        if key not in self.memo:
            rec = self.arena_rec()
            e.mu.mem_write(rec + 0x11D, bytes([random.choice((0, 1, 1))]))
            e.mu.mem_write(rec + 0x130, bytes([random.choice((0, 1, 1))]))
            e.w32(rec + 0x80, random.getrandbits(32))
            e.w32(rec + 0x90, random.choice((0, random.getrandbits(32))))
            self.memo[key] = rec
        self.log.append(key)
        return self.memo[key]

    def arena_rec(self):
        """A zeroed block outside the arena that survives arena resets."""
        return self.e.alloc(0x200)

    def _alloc(self, e):
        """FUN_0040b430 (pool ECX, size EDX, file, line, 0): the case's block, else a fresh zeroed arena block."""
        self.log.append(("alloc", e.arg(1)))
        block = self.alloc_result or self.arena.new(max(e.mu.reg_read(UC_X86_REG_EDX), 16))
        self.alloc_addrs.append(block)
        return block

    def _alloc_size(self, e):
        """FUN_0040b4b0 (size, file, line, 0)."""
        self.log.append(("alloc", e.arg(2)))
        block = self.arena.new(max(e.arg(0), 16))
        self.alloc_addrs.append(block)
        return block

    def answer_with(self, key, gen, keep_tuple=False):
        """A memoised answer for key from gen, logged; shared by game.exe's hook and the model."""
        self.log.append(key)
        if key not in self.memo:
            self.memo[key] = gen()
        return self.memo[key] if keep_tuple or not isinstance(self.memo[key], tuple) else self.memo[key][0]

    def _place(self, e):
        """FUN_0063b850 (inventory, item, row, &x, &y, 0): a memoised (found, x, y); x and y are written as signed words."""
        key = ("b850", e.arg(0), e.arg(1), e.arg(2))
        self.log.append(key)
        if key not in self.memo:
            self.memo[key] = (int(random.random() < 0.8), random.randint(-1, 11), random.randint(-1, 9))
        found, x, y = self.memo[key]
        e.w32(e.arg(3), x)
        e.w32(e.arg(4), y)
        return found

    def _free(self, e):
        self.log.append(("free", e.mu.reg_read(UC_X86_REG_EDX), e.arg(1)))

    alloc_result = 0
    alloc_addrs = []


def ask(w, address, *args):
    """The model's version of a hooked helper call."""
    return w.answer((address,) + tuple(args))


# ---- fixtures -----------------------------------------------------------------------------------------------

def make(w, body_dims=BODY, belt_dims=BELT, owner_kind=1, chain=None, keep=False):
    """An inventory with a body grid (cells + list) and a belt grid; returns (inventory, body cell items, chain items)."""
    a, e = w.arena, w.e
    if not keep:
        w.log.clear()
        w.memo.clear()
    inv = a.inventory()
    table = a.new(48)
    pack_dims = BODY if random.random() < 0.3 else (random.randint(2, 10), random.randint(2, 8))
    w.pack_dims = pack_dims
    for g, (gw, gh) in enumerate((body_dims, belt_dims, pack_dims)):
        cells = a.new(4 * max(gw * gh, 256))
        e.mu.mem_write(table + 16 * g + 8, bytes([gw, gh]))
        e.w32(table + 16 * g + 0xC, cells)
    e.w32(inv + 0x14, table)
    e.w32(inv + 0x18, 3)
    owner = a.new(0x80)
    e.w32(owner, owner_kind)
    e.w32(inv + 8, owner)
    body = e.r32(table + 0xC)
    cell_items = []
    for slot in range(13):
        item = 0
        if random.random() < 0.55:
            item = a.item(x=slot, inv=inv)
            e.w32(item + 4, random.randint(0, 3))
        e.w32(body + 4 * slot, item)
        cell_items.append(item)
    items = chain_of(w, inv, table, 0, random.randint(0, 6) if chain is None else chain)
    w.pack_items = chain_of(w, inv, table, 2, random.randint(0, 6) if chain is None else chain)
    return inv, cell_items, items


def chain_of(w, inv, table, index, count):
    """Link `count` fresh units into grid `index`'s +0x6c / +0x70 chain; the last may be a non-item unit."""
    a, e = w.arena, w.e
    items, prev = [], 0
    for n in range(count):
        kind = 4 if (n < count - 1 or random.random() < 0.7) else random.choice((1, 2, 5))
        item = a.item(x=random.randint(0, 14), kind=kind, inv=inv)
        e.w32(item + 4, random.randint(0, 3))
        if kind in (2, 4, 5) or random.random() < 0.5:
            pass
        else:
            e.w32(item + 0x2C, 0 if random.random() < 0.5 else a.new(0x20))
        e.w32(a.data(item) + 0x6C, prev)
        if prev:
            e.w32(a.data(prev) + 0x70, item)
        else:
            e.w32(table + 16 * index, item)
        prev = item
        items.append(item)
    e.w32(table + 16 * index + 4, prev)
    return items


def x_of(m, w, unit):
    """The x the walks compare: path +0xc for kinds 2, 4, 5; None when a kind-0/1 unit has no path; else the path getter's answer."""
    kind = m.r32(unit)
    path = m.r32(unit + 0x2C)
    if kind == 2 or 3 < kind < 6:
        return m.r32(path + 0xC)
    if path == 0:
        return None
    return ask(w, 0x6488C0, path)


def body_cells(m, inv):
    """The body grid's cells base, or 0 when the finder fails (magic, count, size)."""
    if m.r32(inv) != 0x1020304:
        return 0
    grids = m.r32(inv + 0x14)
    if signed(m.r32(inv + 0x18)) <= 0:
        return 0
    if (m.r8(grids + 8), m.r8(grids + 9)) != BODY:
        return 0
    return m.r32(grids + 0xC)


def grid_first(m, inv, player_dims=None, index=0):
    """The first item of grid `index` as the finders reach it (no dims for a non-player owner), or None when the finder fails."""
    grids = m.r32(inv + 0x14) + 16 * index
    if signed(m.r32(inv + 0x18)) <= index:
        return None
    if player_dims is not None and (m.r8(grids + 8), m.r8(grids + 9)) != tuple(player_dims):
        return None
    return m.r32(grids)


def chain_next(m, unit):
    """The next unit on the grid chain, 'stop' when the walk ends there."""
    if m.r32(unit) != 4:
        return "stop"
    data = m.r32(unit + 0x14)
    if data == 0:
        return "stop"
    return m.r32(data + 0x70) or "stop"


# ---- tests --------------------------------------------------------------------------------------------------

def arena_equal(m, after, label):
    """The model's memory equals game.exe's arena."""
    if m.data != after.data:
        first = next(n for n in range(len(m.data)) if m.data[n] != after.data[n])
        raise AssertionError(f"{label}: arena differs at {m.base + first:#x}")
    check(1, 1, label)


def run(w, address, *args, **kw):
    """Run game.exe's function; returns its result, the arena before (the model's copy) and after, the call log."""
    before = w.arena.snapshot()
    w.log.clear()
    w.alloc_addrs = []
    result = w.e.call(address, *args, **kw)
    log = list(w.log)
    w.log.clear()
    return result, before, w.arena.snapshot(), log


HITS = {}


def finish(w, address_name, result, expected, before, after, game_log, case):
    """Compare the result, the arena and the helper-call log of one case; count the cases that found something."""
    name = address_name.split()[0]
    HITS.setdefault(name, [0, 0])[0] += 1
    HITS[name][1] += int(bool(expected))
    check(result & 0xFFFFFFFF, expected & 0xFFFFFFFF, f"{address_name} result {case}")
    arena_equal(before, after, f"{address_name} arena {case}")
    check(game_log, list(w.log), f"{address_name} helper calls {case}")


def test_slots(w):
    """FUN_0063be40 (two hand slots), FUN_0063c8f0 (a bow-like type in either hand), FUN_0063bf90 (the other hand's weapon)."""
    e, a = w.e, w.arena
    for case in range(400):
        a.reset()
        dims = BODY if random.random() < 0.85 else (random.randint(1, 9), 1)
        inv, cell_items, _ = make(w, body_dims=dims)
        if random.random() < 0.1:
            e.w32(inv, random.choice((0, 7)))
        n = random.randint(0, 5)
        # FUN_0063be40 (inventory, n)
        result, before, after, game_log = run(w, 0x63BE40, inv, n)
        w.log.clear()
        cells = body_cells(before, inv)
        expect = 0
        for slot in (5, 4):
            item = before.r32(cells + 4 * slot) if cells else 0
            if ask(w, 0x62A4E0, item) and ask(w, 0x628950, item, n):
                expect = 1
                break
        finish(w, "63be40", result, expect, before, after, game_log, case)
        # FUN_0063c8f0 (inventory, &out)
        out = a.new(4)
        e.w32(out, 0x55)
        result, before, after, game_log = run(w, 0x63C8F0, inv, out)
        w.log.clear()
        found, chosen = 0, 0
        for slot in (4, 5):
            item = before.r32(cells + 4 * slot) if cells else 0
            if ask(w, 0x62A4E0, item) and ask(w, 0x629BB0, item, 0x33):
                found, chosen = 1, item
                break
        before.w32(out, chosen)
        finish(w, "63c8f0", result, found, before, after, game_log, case)
        # null out pointer, bad magic
        result, before, after, game_log = run(w, 0x63C8F0, inv, 0)
        w.log.clear()
        found = 0
        for slot in (4, 5):
            item = before.r32(cells + 4 * slot) if cells else 0
            if ask(w, 0x62A4E0, item) and ask(w, 0x629BB0, item, 0x33):
                found = 1
                break
        finish(w, "63c8f0 null out", result, found, before, after, game_log, case)
        # FUN_0063bf90 (inventory)
        wield = random.choice([0x77, 0xFFFFFFFF] + [before.r32(i + 0xC) for i in cell_items if i])
        e.w32(inv + 0x1C, wield)
        result, before, after, game_log = run(w, 0x63BF90, inv)
        w.log.clear()
        expect = 0
        if wield != 0xFFFFFFFF:
            first = before.r32(cells + 4 * 5) if cells else 0
            if not first or not ask(w, 0x629BB0, first, 0x2D):
                first = 0
            second = before.r32(cells + 4 * 4) if cells else 0
            if not second or not ask(w, 0x629BB0, second, 0x2D):
                second = 0
            if first and before.r32(first + 0xC) == wield:
                expect = second
            elif second and before.r32(second + 0xC) == wield:
                expect = first
        finish(w, "63bf90", result, expect, before, after, game_log, case)
    a.reset()
    inv, _c, _i = make(w)
    check(w.e.call(0x63BE40, 0, 3), 0, "63be40 null")
    check(w.e.call(0x63C8F0, 0, 0), 0, "63c8f0 null")
    check(w.e.call(0x63BF90, 0), 0, "63bf90 null")


def test_look(w):
    """FUN_0063c050 (inventory, component): the worn item drawn on that component, the wielded hand split for weapons."""
    e, a = w.e, w.arena
    for case in range(400):
        a.reset()
        inv, cell_items, _ = make(w)
        w.wielded = random.choice([0] + [i for i in cell_items if i])
        comp = random.choice((5, 6, 0, 1, 2, 3, 4, 7, 8))
        if random.random() < 0.1:
            e.w32(inv, 9)
        result, before, after, game_log = run(w, 0x63C050, inv, comp)
        w.log.clear()
        expect = 0
        cells = body_cells(before, inv)
        if cells:
            wielded = ask(w, 0x63BEF0, inv)
            for slot in range(11):
                item = before.r32(cells + 4 * slot)
                if not item:
                    continue
                kind = ask(w, 0x629FE0, item) & 0xFFFF
                if kind in (2, 3, 0xC):
                    if comp == 6:
                        if item != wielded:
                            expect = item
                            break
                    elif comp == 5 and item == wielded:
                        expect = item
                        break
                elif ask(w, 0x628660, item) & 0xFF == comp:
                    expect = item
                    break
        finish(w, "63c050", result, expect, before, after, game_log, case)


def run_walk(w, inv, address, args, regs=None):
    """Call a stack-argument walk and return (result, before, after, log)."""
    return run(w, address, inv, *args)


def test_walks(w):
    """FUN_0063ce60, 0063cf30, 0063d010, 0063d0f0, 0063c3b0, 0063cd80: searches down the body grid's chain."""
    e, a = w.e, w.arena
    for case in range(500):
        a.reset()
        inv, _cells, items = make(w, chain=random.randint(0, 6))
        pool = items
        after_item = random.choice(pool + [0, 0]) if pool else 0
        if random.random() < 0.1:
            after_item = a.item()
        kind_type = random.randint(0, 3)
        # FUN_0063ce60 (inventory, ItemTypes row, after)
        result, before, post, game_log = run(w, 0x63CE60, inv, kind_type, after_item)
        w.log.clear()
        expect = walk_ce60(w, before, inv, kind_type, after_item)
        finish(w, "63ce60", result, expect, before, post, game_log, case)
        pack_start = random.choice(list(w.pack_items) + [0]) if w.pack_items else 0
        # FUN_0063cf30 and FUN_0063d010 (inventory, code, after): the code is resolved to a class through FUN_00633640
        code = random.getrandbits(32)
        for address, index, start in ((0x63CF30, 0, after_item), (0x63D010, 2, pack_start)):
            result, before, post, game_log = run(w, address, inv, code, start)
            w.log.clear()
            expect = walk_code(w, before, inv, code, start, index)
            finish(w, f"{address:06x}", result, expect, before, post, game_log, case)
        # FUN_0063d0f0 (inventory, set row)
        row = random.randint(0, 2)
        result, before, post, game_log = run(w, 0x63D0F0, inv, row)
        w.log.clear()
        expect = count_set(w, before, inv, row)
        finish(w, "63d0f0", result, expect, before, post, game_log, case)
        # FUN_0063c3b0 (inventory, template item, after)
        template = random.choice(list(w.pack_items) + items) if (w.pack_items or items) else a.item()
        pack_after = random.choice(list(w.pack_items) + [0, 0]) if w.pack_items else 0
        for owner_kind, dims in ((1, None), (0, "pack"), (0, (1, 11))):
            e.w32(e.r32(inv + 8), owner_kind)
            w.player_dims = w.pack_dims if dims == "pack" else (dims or BODY)
            result, before, post, game_log = run(w, 0x63C3B0, inv, template, pack_after)
            w.log.clear()
            expect = walk_c3b0(w, before, inv, template, pack_after, owner_kind, dims)
            finish(w, "63c3b0", result, expect, before, post, game_log, case)
            # FUN_0063cd80 (inventory, ItemTypes row, after)
            result, before, post, game_log = run(w, 0x63CD80, inv, kind_type, pack_after)
            w.log.clear()
            expect = walk_cd80(w, before, inv, kind_type, pack_after, owner_kind, dims)
            finish(w, "63cd80", result, expect, before, post, game_log, case)
    w.player_dims = BODY
    for address in (0x63CE60, 0x63CD80, 0x63CF30, 0x63D010, 0x63C3B0):
        check(w.e.call(address, 0, 1, 0), 0, f"{address:06x} null inventory")
    check(w.e.call(0x63D0F0, 0, 1), 0, "63d0f0 null")


def seek(m, first, after):
    """Walk the chain to `after` (None when it is missing or the chain breaks); with no `after`, stay at the first."""
    cur = first
    if after == 0:
        return cur
    while cur:
        if cur == after:
            return cur
        nxt = chain_next(m, cur)
        if nxt == "stop":
            return None
        cur = nxt
    return None


def walk_ce60(w, m, inv, kind_type, after):
    """FUN_0063ce60."""
    if m.r32(inv) != 0x1020304:
        return 0
    first = grid_first(m, inv, BODY)
    if first is None:
        return 0
    cur = seek(m, first, after)
    while cur:
        if ask(w, 0x629BB0, cur, kind_type):
            x = x_of(m, w, cur)
            if x is None or x < 0xB:
                return cur
        if m.r32(cur) != 4:
            return 0
        nxt = chain_next(m, cur)
        if nxt == "stop":
            return 0
        cur = nxt
    return 0


def walk_code(w, m, inv, code, after, index=0):
    """FUN_0063cf30 (grid 0) / FUN_0063d010 (grid 2, still checked against the body grid's 13 x 1 size): the first unit
    of that item code (its class) at a body location, from `after`."""
    if m.r32(inv) != 0x1020304:
        return 0
    found, klass = w.answer_code(code)
    if not found:
        return 0
    first = grid_first(m, inv, BODY, index)
    if first is None:
        return 0
    cur = seek(m, first, after)
    while cur:
        if m.r32(cur + 4) == klass:
            x = x_of(m, w, cur)
            if x is None or x < 0xB:
                return cur
        if m.r32(cur) != 4:
            return 0
        nxt = chain_next(m, cur)
        if nxt == "stop":
            return 0
        cur = nxt
    return 0


def count_set(w, m, inv, row):
    """FUN_0063d0f0: worn set items (quality 5) of that named row."""
    if m.r32(inv) != 0x1020304:
        return 0
    first = grid_first(m, inv, BODY)
    if first is None:
        return 0
    count, cur = 0, first
    while cur:
        if ask(w, 0x627E70, cur) == 5 and ask(w, 0x629DA0, cur) == row:
            kind = m.r32(cur)
            if kind == 2 or 3 < kind < 6:
                x = m.r32(m.r32(cur + 0x2C) + 0xC)
                if x <= 10:
                    count += 1
            elif m.r32(cur + 0x2C) != 0:
                x = ask(w, 0x6488C0, m.r32(cur + 0x2C))
                if x <= 10:
                    count += 1
            else:
                count += 1
        if m.r32(cur) != 4:
            return count
        nxt = chain_next(m, cur)
        if nxt == "stop":
            return count
        cur = nxt
    return count


def finder_dims(m, inv, owner_kind, dims, w):
    """The grid-0 pointer the way FUN_0063c3b0 / 0063cd80 reach it: a player owner asks Inventory.txt for the grid size."""
    owner = m.r32(inv + 8)
    if owner and m.r32(owner) == 0:
        ask(w, 0x621050, owner, 0, 1)
        w.log.append(("dims", w.memo[(0x621050, owner, 0, 1)]))
        return tuple(w.player_dims)
    return None


def walk_c3b0(w, m, inv, template, after, owner_kind, dims):
    """FUN_0063c3b0: the first charged-like (type 0x12) item of the template's kind with charges left, from `after`."""
    if m.r32(inv) != 0x1020304:
        return 0
    want = finder_dims(m, inv, owner_kind, dims, w)
    first = grid_first(m, inv, want, 2)
    if first is None:
        return 0
    cur = first
    if after:
        cur = seek(m, first, after)
        if cur is None:
            return 0
    while cur:
        if ask(w, 0x62B400, cur) == 0x12:
            a1 = ask(w, 0x627F80, cur, 0) & 0xFFFF
            a2 = ask(w, 0x627F80, template, 0) & 0xFFFF
            if a1 == a2:
                charges = ask(w, 0x625480, cur, 0x46, 0)
                limit = ask(w, 0x6295B0, cur)
                if charges < limit:
                    return cur
        if m.r32(cur) != 4:
            return 0
        nxt = chain_next(m, cur)
        if nxt == "stop":
            return 0
        cur = nxt
    return 0


def walk_cd80(w, m, inv, kind_type, after, owner_kind, dims):
    """FUN_0063cd80: the first item of that ItemTypes row, starting after `after`."""
    if m.r32(inv) != 0x1020304:
        return 0
    want = finder_dims(m, inv, owner_kind, dims, w)
    first = grid_first(m, inv, want, 2)
    if first is None:
        return 0
    cur = first
    if after:
        cur = seek(m, first, after)
        if cur is None:
            return 0
        data = m.r32(cur + 0x14) if m.r32(cur) == 4 else 0
        if data == 0:
            return 0
        cur = m.r32(data + 0x70)
    while cur:
        if ask(w, 0x629BB0, cur, kind_type):
            return cur
        if m.r32(cur) != 4:
            return 0
        nxt = chain_next(m, cur)
        if nxt == "stop":
            return 0
        cur = nxt
    return 0


def nodes(w, head, link):
    """The node addresses of a singly linked list in the emulator."""
    out, cur = [], head
    while cur:
        out.append(cur)
        cur = w.e.r32(cur + link)
    return out


def test_item_pages(w):
    """FUN_0063e020 (the signed byte at item data +0x69), FUN_0063cab0 (body location 3 is of ItemTypes row 3)."""
    e, a = w.e, w.arena
    for case in range(300):
        a.reset()
        inv, cell_items, _ = make(w)
        item = a.item()
        value = random.getrandbits(8)
        e.mu.mem_write(a.data(item) + 0x69, bytes([value]))
        expect = value - 256 if value > 127 else value
        check(e.call(0x63E020, item) & 0xFFFFFFFF, expect & 0xFFFFFFFF, "63e020 byte")
        e.w32(item, random.choice((0, 1, 3, 5)))
        check(e.call(0x63E020, item), 0, "63e020 not an item")
        e.w32(item, 4)
        e.w32(item + 0x14, 0)
        check(e.call(0x63E020, item), 0, "63e020 no data")
        check(e.call(0x63E020, 0), 0, "63e020 null")
        result, before, after, game_log = run(w, 0x63CAB0, inv)
        w.log.clear()
        cells = body_cells(before, inv)
        third = before.r32(cells + 12) if cells else 0
        expect = int(bool(third) and bool(ask(w, 0x629BB0, third, 3)))
        finish(w, "63cab0", result, expect, before, after, game_log, case)


def test_belt_item(w):
    """FUN_0063c830 (inventory, item, slot, &out): a beltable 1 x 1 item whose record bytes +0x11d and +0x130 are 1 gets the belt cell."""
    e, a = w.e, w.arena
    for case in range(400):
        a.reset()
        inv, cell_items, _ = make(w, belt_dims=BELT if random.random() < 0.85 else (4, 4))
        belt = e.r32(inv + 0x14) + 16
        for n in range(256):
            e.w32(e.r32(belt + 0xC) + 4 * n, random.getrandbits(32) | 1)
        item = a.item(kind=4 if random.random() < 0.9 else 3)
        w.footprint = {item: random.choice(((1, 1), (1, 1), (1, 2), (2, 1)))}
        slot = random.choice((0, 5, 15, 16, 200))
        out = a.new(4)
        e.w32(out, 0x66)
        if random.random() < 0.1:
            e.w32(inv, 0)
        result, before, after, game_log = run(w, 0x63C830, inv, item, slot, out)
        w.log.clear()
        expect, value = 0, 0x66
        if before.r32(inv) == 0x1020304:
            grids = before.r32(inv + 0x14) + 16
            if (before.r8(grids + 8), before.r8(grids + 9)) == BELT and before.r32(inv + 0x18) > 1:
                if before.r32(item) == 4 and ask(w, 0x62BAD0, item):
                    w.log.append(("size", item))
                    if w.footprint[item] == (1, 1):
                        rec = ask_record(w, before.r32(item + 4))
                        if w.e.read(rec + 0x11D, 1) == b"\1" and w.e.read(rec + 0x130, 1) == b"\1":
                            value = before.r32(before.r32(grids + 0xC) + 4 * slot) if slot < 16 else 0
                            expect = 1
        before.w32(out, value)
        finish(w, "63c830", result, expect, before, after, game_log, case)


def ask_record(w, klass):
    """The model's FUN_006335f0 call: the memoised record, logged."""
    key = ("rec", klass)
    if key not in w.memo:
        raise AssertionError("the record is created by the game's call first")
    w.log.append(key)
    return w.memo[key]


def test_node_lists(w):
    """The changed-unit list (FUN_0063cc70 add, FUN_0063cbd0 free all) and the corpse list (FUN_0063d470 add, FUN_0063d4e0
    remove one, FUN_0063d590 free all): node order, the tail pointers, the flag count, and every pool call."""
    e, a = w.e, w.arena
    for case in range(300):
        a.reset()
        inv, _cells, _items = make(w, chain=0)
        w.pack_items = ()
        ids = random.sample(range(1, 40), random.randint(0, 5))
        head = tail = 0
        for unit_id in ids:
            node = a.new(8)
            e.w32(node, unit_id)
            if tail:
                e.w32(tail + 4, node)
            else:
                head = node
            tail = node
        e.w32(inv + 0x2C, head)
        e.w32(inv + 0x30, tail)
        # FUN_0063cc70 (inventory, item): appended unless its id is on the list
        item = a.item()
        if ids and random.random() < 0.4:
            e.w32(item + 0xC, random.choice(ids))
        w.alloc_result = a.new(8)
        e.w32(w.alloc_result, 0x1111)
        e.w32(w.alloc_result + 4, 0x2222)
        result, before, after, game_log = run(w, 0x63CC70, inv, item)
        w.log.clear()
        item_id = before.r32(item + 0xC)
        if item_id not in ids:
            w.log.append(("alloc", 0xD09))
            node = w.alloc_result
            before.w32(node, item_id)
            before.w32(node + 4, 0)
            if before.r32(inv + 0x2C) == 0:
                before.w32(inv + 0x2C, node)
                before.w32(inv + 0x30, node)
            else:
                before.w32(before.r32(inv + 0x30) + 4, node)
                before.w32(inv + 0x30, node)
        arena_equal(before, after, "63cc70 list")
        check(game_log, list(w.log), "63cc70 pool calls")
        for bad in (0, 4):
            bad_inv = a.inventory()
            e.w32(bad_inv, bad)
            e.call(0x63CC70, bad_inv, item)
        e.call(0x63CC70, 0, item)
        e.call(0x63CC70, inv, 0)
        # FUN_0063cbd0 (inventory): free the nodes
        result, before, after, game_log = run(w, 0x63CBD0, inv)
        w.log.clear()
        expect = []
        node = before.r32(inv + 0x2C)
        while node:
            expect.append(("free", node, 0xCBB))
            node = before.r32(node + 4)
        before.w32(inv + 0x2C, 0)
        before.w32(inv + 0x30, 0)
        arena_equal(before, after, "63cbd0 list")
        check(game_log, expect, "63cbd0 frees")
        # corpse list: FUN_0063d470 (inventory, id, word, flag) adds
        for _ in range(random.randint(0, 5)):
            unit_id, word, flag = random.randint(1, 20), random.getrandbits(16), random.choice((0, 1, 1, 2))
            w.alloc_result = a.new(16)
            for field in range(4):
                e.w32(w.alloc_result + 4 * field, 0x77777777)
            result, before, after, game_log = run(w, 0x63D470, inv, unit_id, word, flag)
            w.log.clear()
            node = w.alloc_result
            tail = before.r32(inv + 0x38)
            before.w32(node, flag)
            before.w32(node + 4, unit_id)
            before.w32(node + 8, word)
            before.w32(node + 0xC, 0)
            before.w32(inv + 0x38, node)
            if before.r32(inv + 0x34) == 0:
                before.w32(inv + 0x34, node)
            else:
                before.w32(tail + 0xC, node)
            if flag:
                before.w32(inv + 0x3C, before.r32(inv + 0x3C) + 1)
            arena_equal(before, after, "63d470 add")
            check(game_log, [("alloc", 0xFEA)], "63d470 alloc")
        # FUN_0063d4e0 (inventory, id, flag): remove the first node with both
        for _ in range(6):
            chain = nodes(w, e.r32(inv + 0x34), 0xC)
            if chain and random.random() < 0.7:
                target = random.choice(chain)
                unit_id, flag = e.r32(target + 4), e.r32(target)
            else:
                unit_id, flag = random.randint(1, 25), random.choice((0, 1, 2))
            result, before, after, game_log = run(w, 0x63D4E0, inv, unit_id, flag)
            w.log.clear()
            prev, node, expect_log = 0, before.r32(inv + 0x34), []
            while node and not (before.r32(node + 4) == unit_id and before.r32(node) == flag):
                prev, node = node, before.r32(node + 0xC)
            check(result, int(bool(node)), "63d4e0 result")
            if node:
                nxt = before.r32(node + 0xC)
                if prev == 0:
                    before.w32(inv + 0x34, nxt)
                    if nxt == 0:
                        before.w32(inv + 0x38, 0)
                else:
                    before.w32(prev + 0xC, nxt)
                    if nxt == 0:
                        before.w32(inv + 0x38, prev)
                if before.r32(node) != 0:
                    before.w32(inv + 0x3C, before.r32(inv + 0x3C) - 1)
                expect_log = [("free", node, 0x1034)]
            arena_equal(before, after, "63d4e0 remove")
            check(game_log, expect_log, "63d4e0 frees")
        # FUN_0063d590: free all
        result, before, after, game_log = run(w, 0x63D590, inv)
        w.log.clear()
        expect, node = [], before.r32(inv + 0x34)
        while node:
            expect.append(("free", node, 0x1071))
            node = before.r32(node + 0xC)
        before.w32(inv + 0x34, 0)
        before.w32(inv + 0x38, 0)
        arena_equal(before, after, "63d590")
        check(game_log, expect, "63d590 frees")
    a.reset()
    w.log.clear()
    for address in (0x63CBD0, 0x63D590):
        e.call(address, 0)
        bad = a.inventory()
        e.w32(bad, 4)
        e.call(address, bad)
    e.call(0x63D470, 0, 1, 2, 3)
    check(e.call(0x63D4E0, 0, 1, 1), 0, "63d4e0 null")
    check(w.log, [], "null inventory touched the pool")


def main():
    random.seed(0x1A7E6)
    w = World()
    for step in (test_slots, test_look, test_walks, test_item_pages, test_belt_item, test_node_lists):
        step(w)
    for name, (total, found) in sorted(HITS.items()):
        if not 0 < found < total:
            raise AssertionError(f"{name}: {found} of {total} cases found something; the fixture must reach both outcomes")
    print(f"ok: {cases()} Inventory.cpp query cases")


if __name__ == "__main__":
    main()
