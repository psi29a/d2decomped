# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the stat-list array primitives in stock game.exe 1.14d (Stats\\StatsEx.cpp).

    uv run python stats_array_ops.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random stat lists
(see _stats_world.py) and compares the return value, the allocator log
(pool, size, assert line, in call order), the callback log, every list's
fields and arrays, the output buffers and the bytes the `ret n` pops.
Registers are named where a function does not take its arguments on the stack.

  FUN_00624d90  EAX array, EDI key: insert position, or -1 when the key is there
  FUN_00624c80  EAX key, ESI array, [pool]: insert a {key, 0} entry, grow by 4
  FUN_00624d10  ESI array, EDX entry, [pool]: remove an entry, shrink by 4
  FUN_00624dd0  EBX list, [key]: queue a changed stat (flag 0x80000000, row mask)
  FUN_00624ed0  EBX list, EDI key, [row]: plain-array value with the row's floor
  FUN_00624f40  EAX list: the expanded or the plain array
  FUN_00624fe0  EAX list, [key, row]: own value plus the children's
  FUN_006250b0  EBX list, [key, delta, row, ctx]: add to the expanded array
  FUN_00625150  EBX list, [key, value, row, ctx]: set in the expanded array
  FUN_00625650  [list, state]: the child list that holds a state
  FUN_006256e0  [list, flags]: first list with any of the flag bits
  FUN_00625730  [list, mask]: first later sibling with any of the mask bits
  FUN_00625870  [unit, ctx, stat, callback]: callback for one queued stat
  FUN_006258d0  [unit, ctx, callback]: callback for every queued stat
  FUN_00626110  EAX list, ECX out, [stat, max]: copy one stat's layers
  FUN_006261b0  [list, stat, out, max]: FUN_00626110 with stack arguments

Callees are hooked and logged, not claimed: the allocator (0x40_b4b0,
0x40_b480), the plain-array lookups 0x62_4b90, 0x62_4bd0 and 0x62_4c10 run
for real. `--mutation-check` breaks the model on purpose, one defect at a
time, and requires each to fail. No MPQs are read.
"""
import argparse
import bisect
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import _stats_world as sw
from _stats_world import EXPANDED, L, Mismatch, normal_entries, require

SEARCH_QUEUE, INSERT, REMOVE, QUEUE_STAT = 0x624D90, 0x624C80, 0x624D10, 0x624DD0
FLOOR_VALUE, ARRAY_OF, SUM_VALUE = 0x624ED0, 0x624F40, 0x624FE0
ADD_STAT, SET_STAT = 0x6250B0, 0x625150
FIND_STATE, FIND_FLAGS, NEXT_SIBLING = 0x625650, 0x6256E0, 0x625730
ONE_QUEUED, ALL_QUEUED, COPY_LAYERS, COPY_STACK = 0x625870, 0x6258D0, 0x626110, 0x6261B0
FILE = ".\\Stats\\StatsEx.cpp"
SKIP_STATS = {6, 8, 10, 13, 14}
MUTATIONS = ("insert-no-grow", "remove-no-shrink", "queue-skips-7", "floor-always", "add-sets", "set-zero-keeps",
             "sibling-from-self", "copy-ignores-max", "children-ignore-off", "queued-zero-ctx")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def skey(key):
    """A key as the signed value the array searches compare."""
    return sw.s32(key)


class Rig:
    """The machine, the callbacks and the case builders for this oracle."""

    def __init__(self):
        """Create the world and the two callback targets."""
        self.w = sw.World()
        self.cases = 0
        self.cb4 = self.w.slot(self.callback4, 4)
        self.cb2 = self.w.slot(self.callback2, 2)

    def callback4(self, current):
        """Change callback (ECX, EDX, four stack args): log it."""
        self.w.log.append(("cb", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX))
                          + tuple(current.arg(i) for i in range(4)))
        return 0

    def callback2(self, current):
        """Queued-stat callback (ECX unit, EDX stat, two stack args): log it."""
        self.w.log.append(("qcb", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX))
                          + tuple(current.arg(i) for i in range(2)))
        return 0

    def rows(self, rng):
        """Randomise the ItemStatCost bytes the primitives read, and the three masks."""
        w = self.w
        for index in range(sw.ROWS):
            w.set_row(index, b04=rng.randrange(256), b05=rng.randrange(256), b18=rng.randrange(40),
                      d2c=rng.choice((0, 1, 5, 100, 0xFFFFFFFF, 0xFFFFFF9C)), b50=rng.choice((0, 0, 1)),
                      b53=rng.choice((0, 1)))
        w.set_masks(*[rng.choice((0, 1, 2, 4, 8, 0x10, 0xFF)) for _ in range(3)])

    def make_list(self, rng, flags=None, entries=6, **fields):
        """Install a random list (plain and expanded arrays filled); returns its address."""
        image = L()
        image.f[0x00] = rng.randrange(1, 1 << 32)
        image.f[0x10] = rng.randrange(1 << 32) & ~EXPANDED if flags is None else flags
        for name in ("plain", "exp"):
            n = rng.randrange(0, entries + 1)
            image.arr[name] = normal_entries(rng, n, stats=12, layers=4)
            image.cap[name] = n + rng.choice((0, 0, 1, 3, 4, 9, 11))
        n = rng.randrange(0, 5)
        image.arr["queue"] = sorted(rng.sample([(s << 16) | layer for s in range(12) for layer in range(2)], n))
        image.cap["queue"] = n + rng.choice((0, 0, 2, 4))
        image.f.update({k: v for k, v in fields.items()})
        return self.w.install(image)

    def ptr(self, list_address, name):
        """Whether the array's buffer pointer is non-null right now."""
        return self.w.m.r32(list_address + sw.ARRAYS[name][0]) != 0

    def done(self, what, value, expected, log, popped_args, popped):
        """Compare return, log and lists after one call."""
        self.cases += 1
        if expected is not None:
            require(value == expected & 0xFFFFFFFF, f"{what}: returned {value:#x}, expected {expected & 0xFFFFFFFF:#x}")
        require(self.w.log == log, f"{what}: log {self.w.log!r} != {log!r}")
        self.w.check_lists(what)


def grow(image, name, size_per, pool, ptr_before, line, log):
    """The capacity step shared by the inserts: add 4 when full, log the allocator call."""
    if len(image.arr[name]) >= image.cap[name] and not mut("insert-no-grow"):
        image.cap[name] += 4
        log.append(("realloc", pool, ptr_before, sw.s16(image.cap[name]) * size_per, line, FILE))
        return True
    return False


def model_insert(image, name, key, pool, ptr_before, log):
    """FUN_00624c80: the new entry's index, or None when the key is there."""
    keys = [skey(e[0]) for e in image.arr[name]]
    index = bisect.bisect_left(keys, skey(key))
    if index < len(keys) and keys[index] == skey(key):
        return None
    grow(image, name, 8, pool, ptr_before, 0xE8, log)
    image.arr[name].insert(index, (key, 0))
    return index


def model_remove(image, name, index, pool, log):
    """FUN_00624d10: remove entry `index`, shrink the buffer by 4 entries when more than 8 are free."""
    if index >= len(image.arr[name]):
        return
    del image.arr[name][index]
    free = image.cap[name] - len(image.arr[name])
    if free > 8 and not mut("remove-no-shrink"):
        image.cap[name] -= 4
        log.append(("realloc", pool, True, sw.s16(image.cap[name]) * 8, 0x110, FILE))


def check_search_insert_remove(rig, rng):
    """FUN_00624d90, FUN_00624c80, FUN_00624d10 on the three arrays."""
    w = rig.w
    for _ in range(150):
        w.models.clear()
        lst = rig.make_list(rng)
        image = w.models[lst]
        # FUN_00624d90 over the queue, with keys of both signs.
        queue = sorted({skey(rng.choice((rng.randrange(1 << 20), 0x80000000 + rng.randrange(1 << 20)))) for _ in range(5)})
        image.arr["queue"] = [k & 0xFFFFFFFF for k in queue]
        image.cap["queue"] = max(image.cap["queue"], len(queue))
        w.install(image, lst)
        for key in queue[:2] + [rng.randrange(1 << 20), 0x80000005, 0, 0xFFFFFFFF]:
            keys = [skey(k) for k in image.arr["queue"]]
            index = bisect.bisect_left(keys, skey(key))
            expected = 0xFFFFFFFF if index < len(keys) and keys[index] == skey(key) else index
            value, _ = w.run(SEARCH_QUEUE, regs={"eax": lst + 0x50, "edi": key}, nstack=0)
            rig.done("search", value, expected, [], 0, 0)
        w.models.clear()
        lst = rig.make_list(rng)
        for name, offset in (("plain", 0x24), ("exp", 0x48)):
            for _ in range(3):
                image = w.models[lst]
                pool = image.f[0x00]
                key = rng.choice([e[0] for e in image.arr[name]] + [(rng.randrange(14) << 16) | rng.randrange(5)])
                log = []
                before = rig.ptr(lst, name)
                expect_image = image.copy()
                index = model_insert(expect_image, name, key, pool, before, log)
                value, _ = w.run(INSERT, pool, regs={"eax": key, "esi": lst + offset}, nstack=1)
                pointer = w.m.r32(lst + offset)
                expected = 0 if index is None else pointer + 8 * index
                w.models[lst] = expect_image
                rig.done(f"insert-{name}", value, expected, log, 0, 0)
            for _ in range(3):
                image = w.models[lst]
                if not image.arr[name]:
                    break
                pool = image.f[0x00]
                index = rng.randrange(len(image.arr[name]) + 1)
                log = []
                expect_image = image.copy()
                model_remove(expect_image, name, index, pool, log)
                w.run(REMOVE, pool, regs={"esi": lst + offset, "edx": w.m.r32(lst + offset) + 8 * index}, nstack=1)
                w.models[lst] = expect_image
                rig.done(f"remove-{name}", 0, None, log, 0, 0)
        # Removing down to the shrink threshold.
        big = L()
        big.f[0x00] = 0x1234
        big.arr["exp"] = normal_entries(rng, 12, stats=12, layers=4)
        big.cap["exp"] = 24
        lst = w.install(big)
        while w.models[lst].arr["exp"]:
            image = w.models[lst]
            log = []
            expect_image = image.copy()
            model_remove(expect_image, "exp", 0, 0x1234, log)
            w.run(REMOVE, 0x1234, regs={"esi": lst + 0x48, "edx": w.m.r32(lst + 0x48)}, nstack=1)
            w.models[lst] = expect_image
            rig.done("remove-shrink", 0, None, log, 0, 0)


def check_queue_stat(rig, rng):
    """FUN_00624dd0: append a key to the changed-stat queue when the row's mask bit allows."""
    w = rig.w
    for _ in range(200):
        w.models.clear()
        rig.rows(rng)
        flags = rng.choice((0, EXPANDED)) | rng.randrange(1 << 30)
        lst = rig.make_list(rng, flags=flags)
        image = w.models[lst]
        for _ in range(3):
            stat = rng.randrange(0, 20)
            key = (stat << 16) | rng.randrange(3)
            image = w.models[lst]
            expect_image = image.copy()
            log = []
            row_byte = w.row_bytes(stat)[5]
            mask = w.m.read(sw.MASK_QUEUE, 1)[0]
            blocked = stat in SKIP_STATS or (mut("queue-skips-7") and stat == 7)
            if flags & EXPANDED and not blocked and row_byte & mask:
                keys = [skey(k) for k in expect_image.arr["queue"]]
                index = bisect.bisect_left(keys, skey(key))
                if not (index < len(keys) and keys[index] == skey(key)):
                    pool = expect_image.f[0x00]
                    before = rig.ptr(lst, "queue")
                    if len(keys) >= expect_image.cap["queue"]:
                        expect_image.cap["queue"] += 4
                        log.append(("realloc", pool, before, expect_image.cap["queue"] * 4, 0x162, FILE))
                    expect_image.arr["queue"].insert(index, key)
            w.run(QUEUE_STAT, key, regs={"ebx": lst}, nstack=1)
            w.models[lst] = expect_image
            rig.done("queue-stat", 0, None, log, 0, 0)


def floor_value(w, image, row, key):
    """FUN_00624ed0 and the shared lookup rule: value, raised to the row's floor for a player or monster owner."""
    entries = dict((k, v) for k, v in image.arr["plain"])
    if key not in entries:
        return 0
    value = entries[key]
    if row == 0:
        return value
    row_bytes = w.m.read(row, sw.ROW_SIZE)
    mask = w.m.read(sw.MASK_STAT, 1)[0]
    owner = image.f[0x44]
    if not (row_bytes[5] & mask) or not image.f[0x10] & EXPANDED or not owner:
        return value
    owner_type = w.m.r32(owner)
    floor = sw.s32(int.from_bytes(row_bytes[0x2C:0x30], "little"))
    if (owner_type in (0, 1) or mut("floor-always")) and sw.s32(value) < floor:
        return (floor << (row_bytes[0x18] & 31)) & 0xFFFFFFFF
    return value


def check_floor_and_array(rig, rng):
    """FUN_00624ed0 and FUN_00624f40."""
    w = rig.w
    for _ in range(150):
        w.models.clear()
        rig.rows(rng)
        owner = w.m.alloc(16)
        w.m.w32(owner, rng.choice((0, 1, 2, 4, 6)))
        flags = rng.choice((0, EXPANDED)) | rng.randrange(1 << 30)
        lst = rig.make_list(rng, flags=flags)
        image = w.models[lst]
        image.f[0x44] = rng.choice((owner, 0))
        w.install(image, lst)
        keys = [e[0] for e in image.arr["plain"]] + [(30 << 16) | 1]
        for key in keys[:4] + keys[-1:]:
            for row in (w.row(key >> 16) if key >> 16 < sw.ROWS else 0, 0):
                value, _ = w.run(FLOOR_VALUE, row, regs={"ebx": lst, "edi": key}, nstack=1)
                rig.done("floor", value, floor_value(w, image, row, key), [], 0, 0)
        expected = lst + (0x48 if flags & EXPANDED else 0x24)
        value, _ = w.run(ARRAY_OF, regs={"eax": lst}, nstack=0)
        rig.done("array-of", value, expected, [], 0, 0)


def child_value(image, key):
    """The value of `key` in a child's expanded array when it has one, else its plain array."""
    entries = dict(image.arr["exp"] if image.f[0x10] & EXPANDED else image.arr["plain"])
    return entries.get(key, 0)


def check_sum(rig, rng):
    """FUN_00624fe0: own value plus the children's, skipping switched-off children by the row's mask."""
    w = rig.w
    for _ in range(150):
        w.models.clear()
        rig.rows(rng)
        children = []
        previous = 0
        for _ in range(rng.randrange(0, 4)):
            child = L()
            child.f[0x10] = rng.randrange(1 << 32)
            child.arr["plain"] = normal_entries(rng, rng.randrange(0, 5), stats=6, layers=2)
            child.arr["exp"] = normal_entries(rng, rng.randrange(0, 5), stats=6, layers=2)
            child.cap["plain"], child.cap["exp"] = len(child.arr["plain"]), len(child.arr["exp"])
            child.f[0x2C] = previous
            previous = w.install(child)
            children.append(previous)
        parent = L()
        parent.f[0x10] = rng.choice((0, EXPANDED)) | rng.randrange(1 << 30)
        parent.f[0x3C] = previous
        parent.arr["plain"] = normal_entries(rng, rng.randrange(0, 5), stats=6, layers=2)
        parent.cap["plain"] = len(parent.arr["plain"])
        lst = w.install(parent)
        for stat in range(6):
            key = (stat << 16) | rng.randrange(2)
            row = w.row(stat)
            mask = w.m.read(sw.MASK_STAT, 1)[0]
            total = dict(parent.arr["plain"]).get(key, 0)
            if parent.f[0x10] & EXPANDED:
                walk = previous
                while walk:
                    child = w.models[walk]
                    off = w.row_bytes(stat)[4] & mask
                    off = 0 if mut("children-ignore-off") else off
                    if not (off and child.f[0x10] & 0x40000000):
                        total += child_value(child, key)
                    walk = child.f[0x2C]
            value, _ = w.run(SUM_VALUE, key, row, regs={"eax": lst}, nstack=2)
            rig.done("sum", value, total, [], 0, 0)


def model_add_set(rig, w, image, lst, key, new_value, delta_mode, row_index, ctx, log):
    """Shared model of FUN_006250b0 (add) and FUN_00625150 (set) on the expanded array.

    Returns the model's eax for the add form, None for the set form (which leaves eax unspecified)."""
    pool = image.f[0x00]
    row = w.row_bytes(row_index)
    entries = image.arr["exp"]
    index = next((i for i, e in enumerate(entries) if e[0] == key), None)
    if index is None:
        if not delta_mode and new_value == 0:
            return None
        before = rig.ptr(lst, "exp")
        index = model_insert(image, "exp", key, pool, before, log)
    old = image.arr["exp"][index][1]
    if delta_mode:
        new = (old + new_value) & 0xFFFFFFFF
        stored = new if not mut("add-sets") else new_value & 0xFFFFFFFF
        image.arr["exp"][index] = (key, stored)
        new = stored
        if sw.s32(new) > 0 and row[0x53]:
            image.f[0x10] |= 0x20000000
        remove = new == 0 and row[0x50] == 0
    else:
        new = new_value
        remove = new == 0 and row[0x50] == 0 and not mut("set-zero-keeps")
        if not remove:
            image.arr["exp"][index] = (key, new)
            if row[0x53]:
                image.f[0x10] |= 0x20000000
    if remove:
        model_remove(image, "exp", index, pool, log)
    changed = (delta_mode or old != new)
    if image.f[0x5C] and row[5] & w.m.read(sw.MASK_CALLBACK, 1)[0] and changed:
        log.append(("cb", image.f[0x60], image.f[0x44], ctx, key, old, new))
    return new if delta_mode else None


def check_add_set(rig, rng):
    """FUN_006250b0 (add) and FUN_00625150 (set)."""
    w = rig.w
    for _ in range(300):
        w.models.clear()
        rig.rows(rng)
        flags = rng.randrange(1 << 30) | EXPANDED
        lst = rig.make_list(rng, flags=flags)
        image = w.models[lst]
        image.f[0x5C] = rng.choice((0, rig.cb4))
        image.f[0x60] = rng.randrange(1 << 32)
        image.f[0x44] = rng.randrange(1 << 32)
        w.install(image, lst)
        for delta_mode in (True, False):
            for _ in range(4):
                image = w.models[lst]
                stat = rng.randrange(0, 14)
                key = rng.choice([e[0] for e in image.arr["exp"]] + [(stat << 16) | rng.randrange(4)])
                row_index = key >> 16
                old = dict(image.arr["exp"]).get(key, 0)
                value = rng.choice((0, 1, 5, (-old) & 0xFFFFFFFF, (-old) & 0xFFFFFFFF, old, 0xFFFFFFFF,
                                    rng.randrange(1 << 32)))
                ctx = rng.randrange(1 << 32)
                expect_image = image.copy()
                log = []
                eax = model_add_set(rig, w, expect_image, lst, key, value, delta_mode, row_index, ctx, log)
                got, _ = w.run(ADD_STAT if delta_mode else SET_STAT, key, value, w.row(row_index), ctx,
                               regs={"ebx": lst}, nstack=4)
                w.models[lst] = expect_image
                rig.done("add" if delta_mode else "set", got, eax, log, 0, 0)


def check_chains(rig, rng):
    """FUN_00625650, FUN_006256e0, FUN_00625730 on chains of child lists."""
    w = rig.w
    for _ in range(150):
        w.models.clear()
        chain, other = [], []
        previous = 0
        for _ in range(rng.randrange(0, 5)):
            child = L()
            child.f[0x10] = rng.randrange(1 << 32) & rng.randrange(1 << 32)
            child.f[0x14] = rng.randrange(0, 6)
            child.f[0x2C] = previous
            previous = w.install(child)
            chain.append(previous)
        other_head = 0
        for _ in range(rng.randrange(0, 3)):
            child = L()
            child.f[0x10] = rng.randrange(1 << 32) & rng.randrange(1 << 32)
            child.f[0x14] = rng.randrange(0, 6)
            child.f[0x2C] = other_head
            other_head = w.install(child)
            other.append(other_head)
        parent = L()
        parent.f[0x10] = rng.choice((0, EXPANDED)) | (rng.randrange(1 << 30) & rng.randrange(1 << 30))
        parent.f[0x3C], parent.f[0x40] = previous, other_head
        lst = w.install(parent)
        expanded = bool(parent.f[0x10] & EXPANDED)

        def walk(head):
            """Addresses along a sibling chain."""
            out = []
            while head:
                out.append(head)
                head = w.models[head].f[0x2C]
            return out
        for state in range(0, 6):
            found = 0
            if state and expanded:
                found = next((a for a in walk(previous) + walk(other_head) if w.models[a].f[0x14] == state), 0)
            value, _ = w.run(FIND_STATE, lst, state, nstack=2)
            rig.done("find-state", value, found, [], 0, 0)
        rig.done("find-state-null", w.run(FIND_STATE, 0, 1, nstack=2)[0], 0, [], 0, 0)
        for flags in (0, 1, 2, 0x2000, 0x2004, 0x4000, 0x80, rng.randrange(1 << 32), 0x40000000):
            mask = flags & 0xFFFFDFFF
            head = (other_head if flags & 0x2000 else previous) if expanded else lst
            found = next((a for a in walk(head) if w.models[a].f[0x10] & mask), 0)
            value, _ = w.run(FIND_FLAGS, lst, flags, nstack=2)
            rig.done("find-flags", value, found, [], 0, 0)
        rig.done("find-flags-null", w.run(FIND_FLAGS, 0, 1, nstack=2)[0], 0, [], 0, 0)
        for mask in (1, 2, 0x2000, 0x40000000, rng.randrange(1 << 32)):
            for start in [lst] + chain + other:
                begin = start if mut("sibling-from-self") else w.models[start].f[0x2C]
                found = next((a for a in walk(begin) if w.models[a].f[0x10] & mask), 0)
                value, _ = w.run(NEXT_SIBLING, start, mask, nstack=2)
                rig.done("next-sibling", value, found, [], 0, 0)
        rig.done("next-sibling-null", w.run(NEXT_SIBLING, 0, 1, nstack=2)[0], 0, [], 0, 0)


def check_queued(rig, rng):
    """FUN_00625870 and FUN_006258d0: callbacks for the changed-stat queue."""
    w = rig.w
    for _ in range(200):
        w.models.clear()
        lst = rig.make_list(rng, flags=rng.choice((0, EXPANDED, EXPANDED)) | rng.randrange(1 << 30))
        image = w.models[lst]
        unit = w.m.alloc(0x120)
        w.m.w32(unit + 0x5C, lst)
        expanded = bool(image.f[0x10] & EXPANDED)
        plain = dict(image.arr["plain"])
        for stat in range(0, 12):
            log = []
            key = stat << 16
            if expanded and key in image.arr["queue"] and key in plain:
                log = [("qcb", unit, stat, plain[key], 0x77)]
            w.run(ONE_QUEUED, unit, 0x77, stat, rig.cb2, nstack=4)
            rig.done("one-queued", 0, None, log, 0, 0)
        for ctx in (0x55, 0):
            log = []
            if expanded and (ctx or mut("queued-zero-ctx")):
                log = [("qcb", unit, key >> 16, plain.get(key, 0), ctx) for key in image.arr["queue"]]
            w.run(ALL_QUEUED, unit, ctx, rig.cb2, nstack=3)
            rig.done("all-queued", 0, None, log, 0, 0)
        w.run(ALL_QUEUED, 0, 0x55, rig.cb2, nstack=3)
        rig.done("all-queued-null", 0, None, [], 0, 0)
        w.run(ONE_QUEUED, 0, 1, 1, rig.cb2, nstack=4)
        rig.done("one-queued-null", 0, None, [], 0, 0)
        # A zero context with a null callback and an empty queue runs nothing.
        empty = L()
        empty.f[0x10] = EXPANDED
        unit2 = w.m.alloc(0x120)
        w.m.w32(unit2 + 0x5C, w.install(empty))
        w.run(ALL_QUEUED, unit2, 0, 0, nstack=3)
        rig.done("all-queued-empty", 0, None, [], 0, 0)


def check_copy(rig, rng):
    """FUN_00626110 and FUN_006261b0: copy the layers of one stat."""
    w = rig.w
    for _ in range(150):
        w.models.clear()
        lst = rig.make_list(rng, flags=rng.choice((0, EXPANDED)) | rng.randrange(1 << 30))
        image = w.models[lst]
        entries = image.arr["exp"] if image.f[0x10] & EXPANDED else image.arr["plain"]
        for stat in range(0, 12):
            for limit in (0, 1, 2, 9):
                for address in (COPY_LAYERS, COPY_STACK):
                    out = w.m.alloc(0x100)
                    w.m.mu.mem_write(out, b"\xaa" * 0x100)
                    wanted = [e for e in entries if e[0] >> 16 == stat]
                    take = wanted[:limit + 1] if mut("copy-ignores-max") else wanted[:limit]
                    if address == COPY_LAYERS:
                        value, _ = w.run(address, stat, limit, regs={"eax": lst}, ecx=out, nstack=2)
                    else:
                        value, _ = w.run(address, lst, stat, out, limit, nstack=4)
                    expected = b"".join(struct.pack("<II", *e) for e in take)
                    expected += b"\xaa" * (0x100 - len(expected))
                    require(w.m.read(out, 0x100) == expected, f"copy buffer differs (stat {stat}, max {limit})")
                    rig.done("copy-layers", value, len(take), [], 0, 0)
        out = w.m.alloc(0x40)
        rig.done("copy-null", w.run(COPY_LAYERS, 1, 3, regs={"eax": 0}, ecx=out, nstack=2)[0], 0, [], 0, 0)


def run_all(seed, rounds):
    """Run every group; returns the case count."""
    rng = random.Random(seed)
    rig = Rig()
    for _ in range(rounds):
        check_search_insert_remove(rig, rng)
        check_queue_stat(rig, rng)
        check_floor_and_array(rig, rng)
        check_sum(rig, rng)
        check_add_set(rig, rng)
        check_chains(rig, rng)
        check_queued(rig, rng)
        check_copy(rig, rng)
    return rig.cases


def main():
    """Run the oracle, or with --mutation-check require every model defect to be caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="break the model on purpose and require failures")
    options = parser.parse_args()
    if options.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all(1, 1)
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"mutation {name} was not caught")
        print(f"ok: {len(MUTATIONS)} model mutations caught")
        return
    cases = run_all(1, 1)
    print(f"ok: {cases} stat-list array primitive cases")


if __name__ == "__main__":
    main()
