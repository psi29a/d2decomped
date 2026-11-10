# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ten H/stpl.h hash and array helpers in stock 1.14d.

Run: D2_MPQ_DIR=/path/to/game-data python h_hasharrays.py
Only the local executable is read; no MPQs, user ignore files or game
processes are opened. External allocation,
string, table-growth and list-clear calls are intercepted; see h-hasharrays.md
for the precise boundary. The original pointer writes execute in Unicorn.
"""

import argparse
import hashlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MASK = 0xffffffff
STPL = 0x6e6218
MAPPING_TAG = 0x7436ac
STRING_TAG = 0x74366c
VIRTUAL = 0x6615e4
MUTATION = None
CLAIMED = {0x60f590, 0x60f610, 0x60f690, 0x60f6d0, 0x60f720,
           0x60f8b0, 0x610be0, 0x610ca0, 0x610d30, 0x610df0}
EXECUTED = set()


def equal(actual, expected, label):
    """Stop on a mismatching observable even with Python optimization enabled."""
    if actual != expected:
        raise AssertionError(f"{label}: got {actual!r}, expected {expected!r}")


class Probe:
    """An isolated machine with recorded dependency calls and bounded returns."""

    def __init__(self):
        """Pin the local executable and create one emulator per independent group."""
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        equal(hashlib.sha256(exe.read_bytes()).hexdigest(), GAME_SHA256, "game identity")
        self.machine = emu.Emu(str(exe))
        if MUTATION is not None:
            address, original, changed = MUTATION
            if self.machine.read(address, 1) != bytes([original]):
                raise ValueError(f"unexpected mutation instruction at {address:08x}")
            self.machine.mu.mem_write(address, bytes([changed]))
        self.log = []
        self.cases = 0

    def hook(self, address, name, argc, result=0, this=False):
        """Record exact register/stack arguments and delegate an optional stand-in."""
        def call(machine):
            """Record one dependency boundary without executing that subsystem."""
            args = tuple(machine.arg(index) for index in range(argc))
            if this:
                args = (machine.mu.reg_read(UC_X86_REG_ECX),) + args
            self.log.append((name,) + args)
            return result(machine) if callable(result) else result
        self.machine.hook(address, call, argc)

    def run(self, address, *args, ecx=0):
        """Require a normal return and the stack cleanup observed in assembly."""
        self.log.clear()
        value = self.machine.call(address, *args, ecx=ecx, limit=100000)
        equal(self.machine.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, "instruction budget")
        equal(self.machine.mu.reg_read(UC_X86_REG_ESP),
              emu.STACK + emu.STACK_SIZE - 0x1000, f"{address:08x} stack cleanup")
        self.cases += 1
        EXECUTED.add(address)
        return value


def empty_list(machine, pointer, offset=4):
    """Write an empty intrusive-list header with its complemented terminator."""
    machine.w32(pointer, offset)
    machine.w32(pointer + 4, pointer + 4)
    machine.w32(pointer + 8, ~(pointer + 4))


def lookups():
    """Check FUN_0060f590 chain filtering and FUN_0060f8b0's temporary string lifecycle."""
    probe = Probe()
    machine = probe.machine

    def compare(current):
        """Supply equality while checking the original string-comparison arguments."""
        equal(current.arg(2), 0x7fffffff, "comparison bound")
        return 0 if current.cstr(current.arg(0)) == current.cstr(current.arg(1)) else 1

    probe.hook(0x413590, "compare", 3, compare)
    table, buckets, key = machine.alloc(0x30), machine.alloc(48), machine.alloc(4)
    text = machine.put(b"match\0")
    machine.w32(key, text)
    machine.w32(table + 0x1c, buckets)
    machine.w32(table + 0x24, 3)
    for hash_value in (0, 1, 0x1234, 0xffffffff):
        bucket = buckets + (hash_value & 3) * 12
        machine.w32(bucket, 4)
        for node_count in (0, 1, 3):
            for matching in (False, True):
                nodes = [machine.alloc(0x20) for _ in range(node_count)]
                for index, node in enumerate(nodes):
                    machine.w32(node, hash_value if index else (hash_value ^ 0x100))
                    machine.w32(node + 0x14, machine.put(b"other\0"))
                    machine.w32(node + 8, nodes[index + 1] if index + 1 < len(nodes) else ~(bucket + 4))
                expected = nodes[-1] if matching and len(nodes) > 1 else 0
                if expected:
                    machine.w32(expected, hash_value)
                    machine.w32(expected + 0x14, text)
                machine.w32(bucket + 8, nodes[0] if nodes else ~(bucket + 4))
                equal(probe.run(0x60f590, hash_value, key, ecx=table), expected, "hash lookup")
                expected_log = []
                for node in nodes:
                    if machine.r32(node) == hash_value:
                        expected_log.append(("compare", machine.r32(node + 0x14), text, 0x7fffffff))
                    if node == expected:
                        break
                equal(probe.log, expected_log, "hash comparison selection")
    for terminal in (0, 0x80000000, 0xffffffff):
        machine.w32(buckets + 8, terminal)
        equal(probe.run(0x60f590, 0, key, ecx=table), 0, "nonpositive first link")
        equal(probe.log, [], "no comparison at terminal")

    # The wrapper's lookup is a separately tested dependency here.
    wrapped = Probe()
    machine = wrapped.machine
    state = {}
    wrapped.hook(0x414310, "dup", 1, lambda current: state["duplicate"])
    wrapped.hook(0x413ce0, "hash", 3, 0x12345678)

    def lookup(current):
        """Check the temporary string object's contents before its stack expires."""
        equal(current.r32(current.arg(1)), state["duplicate"], "temporary string")
        return state["found"]

    wrapped.hook(0x60f590, "lookup", 2, lookup, this=True)
    wrapped.hook(0x412650, "free", 4)
    table, source = machine.alloc(0x30), machine.put(b"account\0")
    for duplicate in (0, machine.put(b"account\0")):
        for found in (0, machine.alloc(0x20)):
            state.update(duplicate=duplicate, found=found)
            equal(wrapped.run(0x60f8b0, source, ecx=table), found, "lookup wrapper return")
            equal(wrapped.log[0], ("dup", source), "wrapper dup")
            equal(wrapped.log[1], ("hash", source, 0, 0), "wrapper hash")
            equal(wrapped.log[2][0:3], ("lookup", table, 0x12345678), "wrapper lookup arguments")
            equal(wrapped.log[3:], [("free", duplicate, STPL, 0x5e4, 0)] if duplicate else [], "wrapper free")
    return probe.cases + wrapped.cases


def reserve():
    """Check FUN_0060f6d0 capacity gates, unsigned addition and optional step rounding."""
    probe = Probe()
    machine = probe.machine
    probe.hook(0x60f390, "resize", 1, this=True)
    array = machine.alloc(16)
    for capacity in (0, 8, 64):
        for count in (0, 3, 20):
            for addition in (0, 1, 3, 21, 0xffffffff):
                for rounding in (0, 1):
                    for step in (0, 3, 21):
                        for offset, value in ((0, capacity), (4, count), (8, 0), (12, step)):
                            machine.w32(array + offset, value)
                        target = (count + addition) & MASK
                        expected_step = step
                        expected = []
                        if target > capacity:
                            if rounding:
                                if not step:
                                    step = 21 if target >= 21 else 1 << (target.bit_length() - 1)
                                    if target >= 21:
                                        expected_step = 21
                                target = (target + (-target % step)) & MASK
                            expected = [("resize", array, target)]
                        probe.run(0x60f6d0, addition, rounding, ecx=array)
                        equal(probe.log, expected, "reserve dispatch")
                        equal(machine.r32(array + 12), expected_step, "reserve cached step")
    return probe.cases


def destructors():
    """Check FUN_0060f610/0060f690/00610d30 cleanup order and terminator writes."""
    probe = Probe()
    machine = probe.machine
    probe.hook(0x60e820, "clear_strings", 0, this=True)
    probe.hook(0x60ecb0, "clear_mapping", 0, this=True)
    probe.hook(0x610aa0, "table_dtor", 0, this=True)
    probe.hook(0x412650, "free", 4)
    for count in (0, 1, 3):
        for with_array in (False, True):
            if count and not with_array:
                continue
            array = machine.alloc(16)
            buckets = machine.alloc(max(12 * count, 12)) if with_array else 0
            machine.w32(array + 4, count)
            machine.w32(array + 8, buckets)
            for index in range(count):
                empty_list(machine, buckets + index * 12)
            probe.run(0x60f610, ecx=array)
            expected = [("clear_strings", buckets + index * 12) for index in range(count)]
            if buckets:
                expected.append(("free", buckets, STRING_TAG, 0xfffffffe, 0))
            equal(probe.log, expected, "bucket array destruction")
            for index in range(count):
                equal(machine.read(buckets + index * 12 + 4, 8), b"\0" * 8, "destroyed terminator")
            equal(machine.r32(array + 8), buckets, "array retains freed pointer")
    for linked in (False, True):
        pointer = machine.alloc(12)
        if linked:
            empty_list(machine, pointer)
        probe.run(0x60f690, ecx=pointer)
        equal(probe.log, [("clear_mapping", pointer)], "list destructor dispatch")
        equal(machine.read(pointer + 4, 8), b"\0" * 8, "list destructor terminator")
    for pointer in (0, machine.alloc(0x30)):
        for flags in (0, 1, 2, 3, 0x100):
            equal(probe.run(0x610d30, flags, ecx=pointer), pointer, "deleting dtor return")
            expected = [("table_dtor", pointer)]
            if pointer and flags & 1:
                expected.append(("free", pointer, 0x6cc824, 0xffffffff, 0))
            equal(probe.log, expected, "deleting dtor flags")
    return probe.cases


def resize():
    """Check FUN_0060f720 truncation and realloc/fallback paths with empty list records."""
    probe = Probe()
    machine = probe.machine
    state = {}
    probe.hook(0x60ecb0, "clear", 0, this=True)
    probe.hook(0x4133d0, "realloc", 5, lambda current: state["reallocated"])
    probe.hook(0x413020, "alloc", 4, lambda current: state["allocated"])
    probe.hook(0x412650, "free", 4)
    for old_count in (0, 1, 3):
        for capacity in (0, 1, 3, 5):
            for fallback in (False, True):
                array = machine.alloc(16)
                old = machine.alloc(max(old_count * 12, 12))
                replacement = machine.alloc(max(capacity * 12, 12))
                for index in range(old_count):
                    empty_list(machine, old + index * 12, 4 + index * 4)
                machine.w32(array, old_count)
                machine.w32(array + 4, old_count)
                machine.w32(array + 8, old)
                state.update(reallocated=0 if fallback else replacement, allocated=replacement)
                probe.run(0x60f720, capacity, ecx=array)
                expected = [("clear", old + index * 12) for index in range(capacity, old_count)]
                expected.append(("realloc", old, capacity * 12, MAPPING_TAG, 0xfffffffe, 0x10))
                if fallback:
                    expected.append(("alloc", capacity * 12, MAPPING_TAG, 0xfffffffe, 0))
                    expected.extend(("clear", old + index * 12) for index in range(min(old_count, capacity)))
                    expected.append(("free", old, MAPPING_TAG, 0xfffffffe, 0))
                equal(probe.log, expected, "resize dependency order")
                equal((machine.r32(array), machine.r32(array + 4), machine.r32(array + 8)),
                      (capacity, old_count, replacement), "resize leaves count unchanged")
                for index in range(old_count):
                    cleared = fallback or index >= capacity
                    term = old + index * 12 + 4
                    equal(machine.read(term, 8), b"\0" * 8 if cleared else
                          term.to_bytes(4, "little") + ((~term) & MASK).to_bytes(4, "little"), "old list state")
                if fallback:
                    for index in range(min(old_count, capacity)):
                        row = replacement + index * 12
                        equal((machine.r32(row), machine.r32(row + 4), machine.r32(row + 8)),
                              (4 + index * 4, row + 4, (~(row + 4)) & MASK), "fallback empty list initialization")
    return probe.cases


def grow_check():
    """Check FUN_00610ca0's persistent collision counter and strict threshold 13."""
    probe = Probe()
    machine = probe.machine
    probe.hook(0x6108d0, "rehash", 0, this=True)
    table, buckets = machine.alloc(0x30), machine.alloc(12)
    machine.w32(table + 0x1c, buckets)
    machine.w32(buckets, 4)
    for mask in (0, 0x3fe, 0x3ff, 0xffffffff):
        for initial in (0, 3, 4, 13, 16, 0xffffffff):
            for length in (0, 1, 13, 14, 17):
                nodes = [machine.alloc(12) for _ in range(length)]
                for index, node in enumerate(nodes):
                    machine.w32(node + 8, nodes[index + 1] if index + 1 < length else ~(buckets + 4))
                machine.w32(buckets + 8, nodes[0] if nodes else ~(buckets + 4))
                machine.w32(table + 0x10, initial)
                machine.w32(table + 0x24, mask)
                counter = initial
                changed = False
                if mask < 0x3ff:
                    counter = initial - 3 if initial > 3 else 0
                    for _ in nodes:
                        counter = (counter + 1) & MASK
                        if counter > 13:
                            counter, changed = 0, True
                            break
                equal(probe.run(0x610ca0, 0, ecx=table), int(changed), "grow return")
                equal(machine.r32(table + 0x10), counter, "grow counter")
                equal(probe.log, [("rehash", table)] if changed else [], "grow threshold")
    return probe.cases


def inserts():
    """Check FUN_00610be0/00610df0 node linkage, growth remasking and key replacement."""
    total = 0
    for address, growth in ((0x610be0, 0x610840), (0x610df0, 0x610ca0)):
        probe = Probe()
        machine = probe.machine
        state = {}

        def grow(current, state=state):
            """Optionally change the mask as a real rehash could before node allocation."""
            if state["grown"]:
                current.w32(current.mu.reg_read(UC_X86_REG_ECX) + 0x24, 7)
            return state["grown"]

        probe.hook(growth, "grow", 1, grow, this=True)
        probe.hook(VIRTUAL, "new", 3, lambda current, state=state: state["node"], this=True)
        probe.hook(0x412650, "free", 4)
        probe.hook(0x414310, "dup", 1, lambda current, state=state: state["duplicate"])
        table, buckets, vtable = machine.alloc(0x30), machine.alloc(96), machine.alloc(8)
        node, key, source = machine.alloc(0x30), machine.alloc(4), machine.put(b"key\0")
        machine.w32(vtable + 4, VIRTUAL)
        machine.w32(table, vtable)
        machine.w32(table + 4, 4)
        machine.w32(table + 0x1c, buckets)
        machine.w32(key, source)
        for grown in (0, 1):
            for has_old_string in (False, True):
                for already_linked in (False, True):
                    machine.w32(table + 8, table + 8)
                    machine.w32(table + 12, ~(table + 8))
                    machine.w32(table + 0x24, 3)
                    machine.w32(node + 4, node + 4 if already_linked else 0)
                    machine.w32(node + 8, ~(node + 4) if already_linked else 0)
                    old_string = machine.put(b"old\0") if has_old_string else 0
                    duplicate = machine.put(b"key\0")
                    machine.w32(node + 0x14, old_string)
                    state.update(grown=grown, node=node, duplicate=duplicate)
                    hash_value = 5
                    equal(probe.run(address, hash_value, key, 0x1234, 0x5678, ecx=table), node, "insert result")
                    expected = [("grow", table, 1), ("new", table, buckets + (5 if grown else 1) * 12, 0x1234, 0x5678)]
                    if old_string:
                        expected.append(("free", old_string, STPL, 0x5e9, 0))
                    expected.append(("dup", source))
                    equal(probe.log, expected, "insert dependency sequence")
                    equal((machine.r32(node), machine.r32(node + 4), machine.r32(node + 8), machine.r32(node + 0x14)),
                          (hash_value, table + 8, (~(table + 8)) & MASK, duplicate), "insert node writes")
                    equal((machine.r32(table + 8), machine.r32(table + 12)), (node + 4, node), "insert all-node list")
        total += probe.cases
    return total


def mutation_check():
    """Require six altered original-code branches to fail an existing observation."""
    global MUTATION
    changes = (("lookup hash branch", lookups, (0x60f5c2, 0x75, 0x74)),
               ("reserve equal capacity", reserve, (0x60f6df, 0x76, 0x72)),
               ("growth threshold", grow_check, (0x610cf7, 13, 12)),
               ("delete flag", destructors, (0x610d3e, 1, 2)),
               ("first insert remask", inserts, (0x610bf8, 0x74, 0xeb)),
               ("second insert remask", inserts, (0x610e08, 0x74, 0xeb)))
    try:
        for label, group, change in changes:
            MUTATION = change
            try:
                group()
            except AssertionError:
                print(f"PASS H hash/arrays mutation caught: {label}")
            else:
                raise RuntimeError(f"mutation survived: {label}")
    finally:
        MUTATION = None
    return len(changes)


def main():
    """Run the ten selected routines and optionally audit observation sensitivity."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    EXECUTED.clear()
    total = 0
    for label, group in (("lookups", lookups), ("reserve", reserve), ("destructors", destructors),
                         ("resize", resize), ("growth threshold", grow_check), ("insertions", inserts)):
        count = group()
        print(f"PASS H hash/arrays {label}: {count} cases")
        total += count
    equal(EXECUTED, CLAIMED, "executed hash/array entry coverage")
    print(f"PASS H hash/arrays: ten selected functions, {total} cases")
    if args.mutation_check:
        print(f"PASS H hash/arrays: {mutation_check()} mutations rejected")


if __name__ == "__main__":
    main()
