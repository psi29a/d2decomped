# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify five live ITEMS/Items.cpp routines in stock game.exe 1.14d.

    python items_common_live.py [--mutation-check]

The checks execute FUN_0062ac80, FUN_0062af80, FUN_0062bb20,
FUN_0062be00 and FUN_0062c9f0 on synthetic item/stat records.  External
bit, table and stat-list operations are hooked and their complete arguments
are compared with small models.  No MPQ is read.  ``--mutation-check``
breaks one independently observed rule per routine and requires every
negative control to be detected.
"""
import argparse
import sys

import emu

LIMIT = 200000
mutation = None


def require(value, message):
    """Raise a useful error without relying on Python's optional asserts."""
    if not value:
        raise AssertionError(message)


def changed(name):
    """Whether the current negative control changes this model rule."""
    return mutation == name


def stat_decoder():
    """FUN_0062ac80: legacy widths/adds/remaps, parameter bits and shift."""
    machine = emu.Emu()
    row, reader, stat_list = machine.alloc(0x80), machine.alloc(4), machine.alloc(4)
    reads, writes, answers = [], [], []

    def read_bits(current):
        reads.append((current.arg(0), current.arg(1)))
        return answers.pop(0)

    def set_stat(current):
        writes.append(tuple(current.arg(index) for index in range(4)))
        return 0

    machine.hook(0x411020, read_bits, 2)
    machine.hook(0x627150, set_stat, 4)
    cases = (
        # version, stat, column, param-bits, value-bits, add, shift, raw-param, raw-value, final-stat
        (0x60, 1, 0, 3, 9, 2, 1, 5, 300, 1),
        (0x59, 0x17, 1, 0, 5, 0, 0, 0, 31, 0x17),
        (0x59, 0x18, 2, 2, 6, 0, 2, 2, 63, 0x18),
        (0x59, 0x4A, 0, 0, 5, 10, 0, 0, 19, 0x4A),
        (0x59, 0x4F, 1, 1, 8, 20, 1, 1, 200, 0x4F),
        (0x59, 0x50, 2, 0, 7, 20, 0, 0, 100, 0x50),
        (0x5C, 0x5C, 0, 0, 9, 0, 0, 0, 70, 0x5D),
        (0x5C, 0x61, 1, 0, 9, 0, 0, 0, 71, 0x60),
        (0x5C, 0x64, 2, 0, 9, 0, 0, 0, 72, 0x63),
        (0x5C, 0x67, 0, 0, 9, 0, 0, 0, 73, 0x66),
        (0x5C, 0x6A, 1, 0, 9, 0, 0, 0, 74, 0x69),
        (0x57, 0x60, 2, 0, 6, 0, 0, 0, 63, 0x60),
    )
    for version, stat, column, pbits, vbits, add, shift, raw_param, raw_value, final_stat in cases:
        machine.w32(row + 0x24 + column * 4, pbits)
        machine.mu.mem_write(row + 0x19 + column, bytes((9,)))
        machine.w32(row + 0x1C + column * 4, add)
        machine.mu.mem_write(row + 0x18, bytes((shift,)))
        answers[:] = ([raw_param] if pbits else []) + [raw_value]
        reads.clear()
        writes.clear()
        machine.call(0x62AC80, reader, stat_list, ecx=version, edx=stat,
                     regs={"eax": column, "esi": row}, limit=LIMIT)
        expected_bits = ([pbits] if pbits else []) + [vbits]
        require(reads == [(reader, bits) for bits in expected_bits],
                f"0062ac80 reads version={version:#x} stat={stat:#x}: {reads}")
        wanted_stat = final_stat + (1 if changed("decoder-remap") else 0)
        value = ((raw_value - add) << shift) & 0xFFFFFFFF
        require(writes == [(stat_list, wanted_stat, value, raw_param if pbits else 0)],
                f"0062ac80 write {writes}")
    return len(cases)


def compact_writer():
    """FUN_0062af80: header fields, ground/stored layout, code/ear/gold and save tail."""
    machine = emu.Emu()
    item, data, path, row, writer = (machine.alloc(size) for size in (0x100, 0x80, 0x40, 0x180, 4))
    unit = machine.view("unit", item)
    unit.type, unit.data, unit.path = 4, data, path
    calls = []
    state = {"level": 7, "code": 0x20786F62, "quality": 2, "gold": 17,
             "is_gold": 0, "file": 0, "stat164": 0, "saved": (0, 0)}

    def hook(name, result, stack):
        def run(current):
            calls.append((name,) + tuple(current.arg(index) for index in range(stack)))
            return result() if callable(result) else result
        return run

    machine.hook(0x6335F0, hook("row", lambda: row, 1), 1)
    machine.hook(0x62A670, hook("level", lambda: state["level"], 1), 1)
    machine.hook(0x410EB0, hook("bits", 0, 3), 3)
    machine.hook(0x628590, hook("code", lambda: state["code"], 1), 1)
    machine.hook(0x629BB0, hook("type", lambda: state["is_gold"], 2), 2)
    machine.hook(0x625480, hook("stat", lambda: state["gold"] if calls[-1][2] == 0xE else state["stat164"], 3), 3)
    machine.hook(0x62AF50, hook("bounded", 0, 1), 1)
    machine.hook(0x629DA0, hook("file", lambda: state["file"], 1), 1)

    def saved(current):
        machine.w32(current.arg(1), state["saved"][0])
        machine.w32(current.arg(2), state["saved"][1])
        calls.append(("saved", current.arg(0)))
        return 0

    machine.hook(0x629E40, saved, 3)
    machine.w32(row + 0x12A, 0)
    machine.w32(0x744304, machine.alloc(0xC00))
    count = 0
    # The exact bit stream is the observable; callees that merely source fields are also logged.
    for mode, x, y, page, save, saved_pair in ((3, 12, 34, 0, 0, (0, 0)),
                                                (5, 0x1FFFF, 9, 0, 0, (0, 0)),
                                                (0, 4, 7, 3, 0, (0, 0)),
                                                (1, 15, 15, 6, 1, (11, 22)),
                                                (2, 1, 2, 0xFF, 1, (0, 0))):
        unit.mode = mode
        machine.w32(path + 0x0C, x)
        machine.w32(path + 0x10, y)
        machine.mu.mem_write(data + 0x44, bytes((x & 0xFF,)))
        machine.mu.mem_write(data + 0x45, bytes((page & 0xFF,)))
        machine.w32(data + 0x18, 0)
        state["saved"] = saved_pair
        calls.clear()
        machine.call(0x62AF80, save, regs={"eax": item, "ebx": writer}, limit=LIMIT)
        bits = [entry[1:] for entry in calls if entry[0] == "bits"]
        prefix = [(writer, 7, 10), (writer, mode, 3)]
        if mode in (3, 5):
            prefix += [(writer, min(x, 0xFFFF), 16), (writer, min(y, 0xFFFF), 16)]
        else:
            prefix += [(writer, x & 0xF, 4), (writer, x & 0xF, 4),
                       (writer, y & 0xF, 4), (writer, min(((page + 1) & 0xFF), 7), 3)]
        if changed("writer-mode"):
            prefix[1] = (writer, mode ^ 1, 3)
        require(bits[:len(prefix)] == prefix, f"0062af80 header {mode}: {bits}")
        require(("code", item) in calls and ("type", item, 4) in calls, f"0062af80 body {calls}")
        if save:
            require(("saved", item) in calls, "0062af80 omitted save tail")
            tail = bits[-4:] if saved_pair[1] else bits[-1:]
            wanted = ([(writer, 1, 1), (writer, saved_pair[0], 32),
                       (writer, saved_pair[1], 32), (writer, 0, 32)] if saved_pair[1]
                      else [(writer, 0, 1)])
            require(tail == wanted, f"0062af80 save tail {tail}")
        count += 1
    return count


def book_charges():
    """FUN_0062bb20: book lookup, ordinary row byte, and missing-book result."""
    machine = emu.Emu()
    item, data, row, book = (machine.alloc(size) for size in (0x40, 0x60, 0x140, 8))
    machine.w32(item, 4)
    machine.w32(item + 4, 9)
    machine.w32(item + 0x14, data)
    machine.mu.mem_write(data + 0x3E, (12).to_bytes(2, "little"))
    machine.mu.mem_write(row + 0x134, b"\x2a")
    machine.mu.mem_write(book + 2, b"\x19")
    ancestry, found, asked = [0, 0], [book], []
    machine.hook(0x629BB0, lambda current: ancestry[current.arg(1) == 0x12], 2)
    machine.hook(0x6335F0, lambda current: row, 1)

    def lookup(current):
        asked.append(current.arg(0))
        return found[0]

    machine.hook(0x6374B0, lookup, 1)
    require(machine.call(0x62BB20, item, limit=LIMIT) == (0x2B if changed("book-row") else 0x2A), "ordinary charge")
    ancestry[0] = 1
    require(machine.call(0x62BB20, item, limit=LIMIT) == 0x19 and asked[-1] == 12, "book charge")
    found[0] = 0
    require(machine.call(0x62BB20, item, limit=LIMIT) == 0xFFFFFFFF, "missing book")
    return 3


def socket_capacity():
    """FUN_0062be00: area/capacity/count clamps, flag and stat write."""
    machine = emu.Emu()
    item, data, row = machine.alloc(0x40), machine.alloc(0x40), machine.alloc(0x140)
    machine.w32(item, 4)
    machine.w32(item + 4, 1)
    machine.w32(item + 0x14, data)
    capacity, writes = [6], []
    machine.hook(0x6335F0, lambda current: row, 1)
    machine.hook(0x62BC20, lambda current: capacity[0], 1)
    machine.hook(0x627260, lambda current: writes.append(tuple(current.arg(i) for i in range(4))) or 0, 4)
    count = 0
    for width, height, cap, asked, wanted in ((0, 4, 6, 3, None), (1, 1, 6, -2, 1),
                                               (2, 3, 9, 9, 6), (4, 4, 3, 9, 3),
                                               (2, 2, 0, 2, 0)):
        machine.mu.mem_write(row + 0x10F, bytes((width, height)))
        capacity[0] = cap
        machine.w32(data + 0x18, 0)
        writes.clear()
        machine.call(0x62BE00, item, asked & 0xFFFFFFFF, limit=LIMIT)
        if wanted is None:
            require(not writes and machine.r32(data + 0x18) == 0, "zero area")
        else:
            expected = wanted + (1 if changed("socket-clamp") else 0)
            require(writes == [(item, 0xC2, expected, 0)], f"socket write {writes}")
            require(machine.r32(data + 0x18) & 0x800, "socket flag")
        count += 1
    return count


def base_floor():
    """FUN_0062c9f0: armour, one/two-hand and thrown minimum/maximum floors."""
    machine = emu.Emu()
    item, row = machine.alloc(0x40), machine.alloc(0x140)
    gets, sets = {}, []
    machine.hook(0x628590, lambda current: 0x20786F62, 1)
    machine.hook(0x633640, lambda current: row, 2)
    machine.hook(0x629BB0, lambda current: 1, 2)
    machine.hook(0x62BA80, lambda current: 1, 1)
    machine.hook(0x6253B0, lambda current: gets.get(current.arg(1), 0), 3)
    machine.hook(0x627260, lambda current: sets.append(tuple(current.arg(i) for i in range(4))) or 0, 4)
    count = 0
    machine.w32(row + 0xD0, 40)
    gets[0x1F] = 10
    machine.call(0x62C9F0, 0x10, regs={"esi": item}, limit=LIMIT)
    require(sets == [(item, 0x1F, 42 if changed("armour-plus-one") else 41, 0)], f"armour floor {sets}")
    count += 1
    for trigger, fields in ((0x11, ((0x16, 0xFF, 8), (0x18, 0x103, 9), (0xA0, 0x101, 10))),
                            (0x12, ((0x15, 0xFE, 11), (0x17, 0x102, 12), (0x9F, 0x100, 13)))):
        sets.clear()
        for stat, offset, floor in fields:
            machine.mu.mem_write(row + offset, bytes((floor,)))
            gets[stat] = floor - 1
        machine.call(0x62C9F0, trigger, regs={"esi": item}, limit=LIMIT)
        expected = [(item, stat, floor, 0) for stat, _offset, floor in fields]
        if changed("damage-floor"):
            expected[-1] = (item, fields[-1][0], fields[-1][2] + 1, 0)
        require(sets == expected, f"damage floor {trigger:#x}: {sets}")
        count += 1
    return count


CHECKS = (stat_decoder, compact_writer, book_charges, socket_capacity, base_floor)
MUTATIONS = ("decoder-remap", "writer-mode", "book-row", "socket-clamp",
             "armour-plus-one", "damage-floor")


def run_all():
    """Run the complete common-items group and return its scenario count."""
    return sum(check() for check in CHECKS)


def main():
    """Run normally, or prove each model rule is independently observed."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutation = None
    total = run_all()
    print(f"ok: {total} common ITEMS scenarios over 5 functions")
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all()
            except AssertionError:
                print(f"mutation caught: {name}")
            else:
                raise AssertionError(f"mutation survived: {name}")


if __name__ == "__main__":
    main()
