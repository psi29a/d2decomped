# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's small Inventory.cpp / InvMode.cpp helpers on synthetic records.

Runs game.exe's own code on fake inventories (magic 0x1020304) and fake items
(type 4, class, item data at unit +0x14, path at unit +0x2c) and compares each
answer with a model written from the decompile. Hooked, because the claim
stops there: FUN_0063afd0 (the placement, recorded: its four stack args and
EBX) and FUN_0063aaf0 (the removal, recorded: ECX and EAX). The belt cell
reader FUN_0063c7f0 runs with FUN_0063add0 for real.

Covers: the empty stubs FUN_0063bcf0, 0063bd00, 0063be30, 0063c550 (they only
pop their stack, 0x20 / 4 / 8 / 8 bytes), the field getters FUN_0063b2e0,
0063cbb0, 0063cd60, 0063d450, 0063d570, 0063d5f0, the node getters
FUN_0063cd20, 0063cd40, 0063d610, 0063d630, 0063d650, 0063d670, 0063e000,
the owner-id setter FUN_0063d430, FUN_0063c110, 0063c150, 0063cc30, 0063c7f0,
0063c180, 0063cb70, 0063d190, 0063db90, 0063e070 and the placement wrappers
FUN_0063bcc0, 0063bdb0, 0063c4c0.

Usage: D2_MPQ_DIR=<MPQ folder> uv run python inventory_small.py
"""
import random

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX

import emu

MAGIC = 0x1020304
PLACE = 0x63AFD0
REMOVE = 0x63AAF0
SENTINEL = 0x7E57ED00
CASES = 0


def check(actual, expected, label):
    """Count one case; raise with the label when game.exe's answer differs from the model."""
    global CASES
    CASES += 1
    if actual != expected:
        show = (lambda v: f"{v:#x}" if isinstance(v, int) else repr(v))
        raise AssertionError(f"{label}: game.exe {show(actual)}, model {show(expected)}")


class World:
    """One emulator with a fake inventory, items and a recorder on the two hooked callees."""

    def __init__(self):
        self.e = e = emu.Emu()
        self.placed = []
        self.removed = []
        self.remove_head = False
        e.hook(PLACE, self._place, 4)
        e.hook(REMOVE, self._remove, 0)
        self.inv = self.new_inventory()

    def _place(self, e):
        self.placed.append(tuple(e.arg(i) for i in range(4)) + (e.mu.reg_read(UC_X86_REG_EBX),))
        return SENTINEL

    def _remove(self, e):
        """Recorded; with remove_head set it also unlinks the inventory's first item so FUN_0063cb70's loop ends."""
        item, where = e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EAX)
        self.removed.append((item, where))
        if self.remove_head:
            data = e.r32(item + 0x14)
            e.w32(e.r32(data + 0x5C) + 0xC, e.r32(data + 0x60))
        return 0

    def new_inventory(self):
        inv = self.e.alloc(0x40)
        self.e.w32(inv, MAGIC)
        return inv

    def item(self, x=0, kind=4):
        """A fake unit of the given type with item data and a path whose +0xc is x."""
        e = self.e
        unit, data, path = e.alloc(0x60), e.alloc(0x80), e.alloc(0x20)
        e.w32(unit, kind)
        e.w32(unit + 4, random.getrandbits(32))
        e.w32(unit + 0xC, random.getrandbits(32))
        e.w32(unit + 0x14, data)
        e.w32(unit + 0x2C, path)
        e.w32(path + 0xC, x & 0xFFFFFFFF)
        return unit

    def belt_inventory(self, width=16, height=1):
        """An inventory with grid 0 (body) and grid 1 (belt, width x height) whose cells hold random item ids."""
        e = self.e
        inv = self.new_inventory()
        grids = e.alloc(0x20)
        cells = e.alloc(16 * 4)
        body = e.alloc(13 * 4)
        e.mu.mem_write(grids + 8, bytes([13, 1]))
        e.w32(grids + 0xC, body)
        e.mu.mem_write(grids + 0x18, bytes([width, height]))
        e.w32(grids + 0x1C, cells)
        e.w32(inv + 0x14, grids)
        e.w32(inv + 0x18, 2)
        for n in range(16):
            e.w32(cells + 4 * n, random.getrandbits(32))
        return inv, cells


def signed(value):
    """A 32-bit value as a signed int."""
    return value - (1 << 32) if value & 0x80000000 else value


def empties(w):
    """The four `ret n` stubs return without touching anything (stack sizes from the disassembly)."""
    e = w.e
    for address, args in ((0x63BCF0, 8), (0x63BD00, 1), (0x63BE30, 2), (0x63C550, 2)):
        before = e.read(w.inv, 0x40)
        e.call(address, *([0xDEAD0000 + n for n in range(args)]), ecx=0x1111, edx=0x2222)
        check(int(e.read(w.inv, 0x40) == before), 1, f"{address:08x} touched memory")
        check(len(w.placed) + len(w.removed), 0, f"{address:08x} called a callee")


def getters(w):
    """The null-checked field readers, with a bad magic and a null pointer each."""
    e = w.e
    for address, offset, none in ((0x63B2E0, 0x10, 0), (0x63CBB0, 0x2C, 0), (0x63CD60, 0x28, 0), (0x63D450, 0x24, 0xFFFFFFFF),
                                  (0x63D570, 0x34, 0), (0x63D5F0, 0x3C, 0)):
        for _ in range(20):
            inv = w.new_inventory()
            value = random.getrandbits(32)
            e.w32(inv + offset, value)
            check(e.call(address, inv), value, f"{address:08x} field {offset:#x}")
            e.w32(inv, random.choice((0, 4, 0x1020305, 0xFFFFFFFF)))
            check(e.call(address, inv), none, f"{address:08x} bad magic")
        check(e.call(address, 0), none, f"{address:08x} null")
    for address, offset in ((0x63CD20, 4), (0x63D610, 0xC), (0x63D630, 4), (0x63D650, 8)):
        node = e.alloc(0x10)
        for _ in range(20):
            value = random.getrandbits(32)
            e.w32(node + offset, value)
            check(e.call(address, node), value, f"{address:08x} node {offset:#x}")
        check(e.call(address, 0), 0, f"{address:08x} null")
    for address in (0x63CD40, 0x63D670):
        node = e.alloc(0x10)
        for _ in range(20):
            value = random.getrandbits(32)
            e.w32(node, value)
            check(e.call(address, node), value, f"{address:08x} first word")
        check(e.call(address, 0), 0, f"{address:08x} null")
    for _ in range(20):
        item = w.item()
        check(e.call(0x63E000, item), e.r32(item + 0xC), "63e000 item id")
        e.w32(item, random.choice((0, 1, 3, 5)))
        check(e.call(0x63E000, item), 0, "63e000 not an item")
    check(e.call(0x63E000, 0), 0, "63e000 null")


def owner_id(w):
    """FUN_0063d430 writes +0x24 only into a real inventory."""
    e = w.e
    for _ in range(20):
        inv = w.new_inventory()
        value = random.getrandbits(32)
        e.w32(inv + 0x24, 0xA5A5A5A5)
        e.call(0x63D430, inv, value)
        check(e.r32(inv + 0x24), value, "63d430 set")
        e.w32(inv, random.choice((0, 4, 0x1020305)))
        e.call(0x63D430, inv, 0x12345678)
        check(e.r32(inv + 0x24), value, "63d430 bad magic written")
    e.call(0x63D430, 0, 5)


def item_x(w):
    """FUN_0063c150 is the item's x (FUN_0045adf0); FUN_0063c110 keeps it only when 10 or less (signed)."""
    e = w.e
    xs = [0, 1, 9, 10, 11, 12, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFF6]
    xs += [random.getrandbits(32) for _ in range(20)]
    for x in xs:
        inv, item = w.new_inventory(), w.item(x)
        check(e.call(0x63C150, inv, item), x, f"63c150 x {x:#x}")
        check(e.call(0x63C110, inv, item), x if signed(x) <= 10 else 0, f"63c110 x {x:#x}")
        for address in (0x63C150, 0x63C110):
            bad = w.new_inventory()
            e.w32(bad, 0)
            check(e.call(address, bad, item), 0, f"{address:08x} bad inventory")
            check(e.call(address, inv, 0), 0, f"{address:08x} null item")
            wrong = w.item(x, kind=random.choice((0, 1, 3, 5)))
            check(e.call(address, inv, wrong), 0, f"{address:08x} not an item")
            check(e.call(address, 0, item), 0, f"{address:08x} null inventory")


def changed_list(w):
    """FUN_0063cc30: is this unit id on the +0x2c list of 8-byte nodes (id, next)."""
    e = w.e
    for _ in range(40):
        inv = w.new_inventory()
        ids = random.sample(range(1, 1000), random.randint(0, 6))
        head = 0
        for unit_id in reversed(ids):
            node = e.alloc(8)
            e.w32(node, unit_id)
            e.w32(node + 4, head)
            head = node
        e.w32(inv + 0x2C, head)
        for probe in ids + [0, 1000, 1001, random.randint(1, 2000)]:
            check(e.call(0x63CC30, inv, probe), int(probe in ids), f"63cc30 id {probe}")
        e.w32(inv, 5)
        check(e.call(0x63CC30, inv, ids[0] if ids else 0), 0, "63cc30 bad magic")
    check(e.call(0x63CC30, 0, 1), 0, "63cc30 null")


def belt_slot(w):
    """FUN_0063c7f0 (inventory, n): grid 1 (the belt, FUN_0063add0 with 0x7447b4) cell n, 0 past 15 or on a size mismatch."""
    e = w.e
    for _ in range(10):
        inv, cells = w.belt_inventory()
        for n in range(16):
            check(e.call(0x63C7F0, inv, n), e.r32(cells + 4 * n), f"63c7f0 slot {n}")
        for n in (16, 17, 255, 0xFFFFFFFF):
            check(e.call(0x63C7F0, inv, n), 0, f"63c7f0 slot {n:#x}")
        bad, _cells = w.belt_inventory(width=4, height=4)
        check(e.call(0x63C7F0, bad, 3), 0, "63c7f0 belt size mismatch")
        e.w32(inv, 0)
        check(e.call(0x63C7F0, inv, 3), 0, "63c7f0 bad magic")
    check(e.call(0x63C7F0, 0, 3), 0, "63c7f0 null")


def cursor(w):
    """FUN_0063c180 (inventory, item): an item becomes the cursor item and points back; null removes the cursor item."""
    e = w.e
    for _ in range(20):
        inv, item = w.new_inventory(), w.item()
        data = e.r32(item + 0x14)
        e.call(0x63C180, inv, item)
        check(e.r32(inv + 0x20), item, "63c180 cursor set")
        check(e.r32(data + 0x5C), inv, "63c180 back pointer")
        w.removed.clear()
        e.call(0x63C180, inv, 0)
        check(len(w.removed), 1, "63c180 null removes")
        check(w.removed[0], (item, data + 0x5C), "63c180 removal args")
        other, wrong = w.new_inventory(), w.item(kind=random.choice((0, 1, 3, 5)))
        e.call(0x63C180, other, wrong)
        check(e.r32(other + 0x20), 0, "63c180 wrong type set")
        no_data = w.item()
        e.w32(no_data + 0x14, 0)
        e.call(0x63C180, other, no_data)
        check(e.r32(other + 0x20), 0, "63c180 no item data set")
        w.removed.clear()
        e.call(0x63C180, w.new_inventory(), 0)
        check(len(w.removed), 0, "63c180 null with no cursor item")
        bad = w.new_inventory()
        e.w32(bad, 0)
        e.call(0x63C180, bad, item)
        check(e.r32(bad + 0x20), 0, "63c180 bad magic")
    e.call(0x63C180, 0, 0)


def clear_all(w):
    """FUN_0063cb70 removes the first item until the list is empty (FUN_0063aaf0 each time, ECX the item)."""
    e = w.e
    w.remove_head = True
    for count in (0, 1, 2, 5):
        inv = w.new_inventory()
        items = [w.item() for _ in range(count)]
        for number, item in enumerate(items):
            data = e.r32(item + 0x14)
            e.w32(data + 0x5C, inv)
            e.w32(data + 0x60, items[number + 1] if number + 1 < count else 0)
        e.w32(inv + 0xC, items[0] if items else 0)
        w.removed.clear()
        e.call(0x63CB70, inv)
        check(len(w.removed), count, f"63cb70 {count} items")
        check([r[0] for r in w.removed] == items, True, "63cb70 order")
        check(e.r32(inv + 0xC), 0, "63cb70 emptied")
    w.remove_head = False
    w.removed.clear()
    bad = w.new_inventory()
    e.w32(bad + 0xC, w.item())
    e.w32(bad, 1)
    e.call(0x63CB70, bad)
    e.call(0x63CB70, 0)
    check(len(w.removed), 0, "63cb70 bad inventory removed something")


def wield(w):
    """FUN_0063d190 (inventory ECX, item EDX, flag): flag sets +0x1c to the item's id; else clears it (-1) if it was this item."""
    e = w.e
    for _ in range(40):
        inv, item = w.new_inventory(), w.item()
        item_id = e.r32(item + 0xC)
        for flag in (0, 1, 2, 0xFFFFFFFF):
            e.w32(inv + 0x1C, 0x5555)
            e.call(0x63D190, flag, ecx=inv, edx=item)
            check(e.r32(inv + 0x1C), item_id if flag else 0x5555, f"63d190 flag {flag:#x}")
            e.w32(inv + 0x1C, item_id)
            e.call(0x63D190, flag, ecx=inv, edx=item)
            check(e.r32(inv + 0x1C), item_id if flag else 0xFFFFFFFF, f"63d190 same id flag {flag:#x}")
        e.w32(inv + 0x1C, 0x77)
        e.call(0x63D190, 1, ecx=inv, edx=w.item(kind=random.choice((0, 1, 3, 5))))
        e.call(0x63D190, 1, ecx=inv, edx=0)
        e.call(0x63D190, 1, ecx=0, edx=item)
        bad = w.new_inventory()
        e.w32(bad, 0)
        e.call(0x63D190, 1, ecx=bad, edx=item)
        check(e.r32(inv + 0x1C), 0x77, "63d190 guards")
        check(e.r32(bad + 0x1C), 0, "63d190 bad magic")


def small_mapping(w):
    """FUN_0063db90 (EAX a, stack b): b set -> 5 or 1, else 3 or 0, by whether a is non-zero."""
    e = w.e
    for a in (0, 1, 2, 0x80000000, 0xFFFFFFFF):
        for b in (0, 1, 2, 0x80000000, 0xFFFFFFFF):
            expected = (5 if a else 1) if b else (3 if a else 0)
            check(e.call(0x63DB90, b, regs={"eax": a}), expected, f"63db90 a={a:#x} b={b:#x}")


def owned_item(w):
    """FUN_0063e070 (inventory, item): the item when its item data +0x5c is this inventory, else 0."""
    e = w.e
    for _ in range(40):
        inv, item = w.new_inventory(), w.item()
        data = e.r32(item + 0x14)
        e.w32(data + 0x5C, inv)
        check(e.call(0x63E070, inv, item), item, "63e070 owned")
        e.w32(data + 0x5C, inv + 0x40)
        check(e.call(0x63E070, inv, item), 0, "63e070 other inventory")
        e.w32(data + 0x5C, 0)
        check(e.call(0x63E070, inv, item), 0, "63e070 free item")
        check(e.call(0x63E070, 0, item), 0, "63e070 null inventory")
        check(e.call(0x63E070, inv, 0), 0, "63e070 null item")
        wrong = w.item(kind=random.choice((0, 1, 3, 5)))
        e.w32(e.r32(wrong + 0x14) + 0x5C, inv)
        check(e.call(0x63E070, inv, wrong), 0, "63e070 not an item")
        e.w32(item + 0x14, 0)
        check(e.call(0x63E070, inv, item), 0, "63e070 no item data")


def wrappers(w):
    """The three thin callers of the placement FUN_0063afd0: their argument mapping and the guards in front."""
    e = w.e
    for _ in range(60):
        a = [random.getrandbits(32) for _ in range(7)]
        a[6] &= 0xFF if random.random() < 0.5 else 0xFFFFFFFF
        w.placed.clear()
        check(e.call(0x63BCC0, *a), SENTINEL, "63bcc0 result")
        check(w.placed, [(a[2], a[3], (a[6] & 0xFF) + 2, a[4], a[1])], "63bcc0 args")
        w.placed.clear()
        e.call(0x63BCC0, 0, *a[1:])
        check(len(w.placed), 0, "63bcc0 null first arg")
    for first in (0, 0x1234):
        for loc in range(0, 0x12):
            for second in (0, 0xCAFE):
                w.placed.clear()
                result = e.call(0x63BDB0, first, second, loc)
                if first and loc < 0xD:
                    check((result, w.placed), (SENTINEL, [(loc, 0, 0, 0, second)]), f"63bdb0 loc {loc}")
                else:
                    check((result, w.placed), (0, []), f"63bdb0 guard loc {loc}")
    for eax in (0, 0x1234):
        for loc in list(range(0, 0x12)) + [0xFFFFFFFF]:
            for unit in (0, w.item(), w.item(kind=3)):
                w.placed.clear()
                result = e.call(0x63C4C0, ecx=loc, edx=unit, regs={"eax": eax})
                if eax and unit and e.r32(unit) == 4 and loc < 0x10:
                    check((result, w.placed), (SENTINEL, [(loc, 0, 1, 0, unit)]), f"63c4c0 loc {loc}")
                else:
                    check((result, w.placed), (0, []), f"63c4c0 guard loc {loc}")


def main():
    random.seed(0x1A7E)
    w = World()
    for step in (empties, getters, owner_id, item_x, changed_list, belt_slot, cursor, clear_all, wield, small_mapping,
                 owned_item, wrappers):
        step(w)
    print(f"ok: {CASES} small Inventory.cpp helper cases")


if __name__ == "__main__":
    main()
