# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's NPC gossip text list (Text/Text.cpp, 13 functions).

    uv run python text_list.py

The list behind the 0x27 NPC-info packet's text. A list header is 12 bytes:
+0 pool, +4 count (word), +8 head. A node is 12 bytes: +0 message id (word),
+4 type (dword; 0 / 1 / 2 are the three kinds the getters pick), +8 next. The
functions, all run here from game.exe with the allocator and the error
handler hooked:

* 0x661240 new list (stdcall pool; alloc line 0x2b), 0x6612f0 push a node at
  the head (list, id, type; alloc line 0x5a, count + 1, id kept as a word),
  0x661330 remove the first node with an id (free line 0x7b, count - 1);
* 0x661270 free every node (EDI = list; free line 0x3c; **count 0, head left
  as it was**), 0x6612a0 that plus the header (free line 0x4c);
* 0x661390 count type 2, 0x6613c0 the nth type 2 (none: 0xe8c), 0x661400 the
  nth type 0 and 0x661440 the nth type 1 (none: 0xffff);
* 0x661480 write the list into a 34-byte packet block (byte 0 the count; entry
  i from the head: type at 2 + 4i, id at 4 + 4i), 0x661510 read one back
  (pushes in packet order, so the list reverses), 0x6615f0 an insertion sort
  of 12-byte entries by a fastcall compare (0x6615d0 compares ids), 0x6616e0
  sorts a list's ids ascending, stable, through a count * 12 buffer.
The error handler halts on a null list or block (0xed / 0xee build, 0x10b /
0x10c store, 0x165 sort), a ninth entry in build (0xf8) and a packet count of
8 or more in store (0x112, after the old nodes are already freed), so a list
of 8 builds but does not store back. No MPQs are read.
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FILE_NAME = 0x6EDEA8
ALLOC = 0x40B430
FREE = 0x40B480
FATAL = 0x408A60
NEW_LIST, FREE_ALL, FREE_LIST, ADD, REMOVE = 0x661240, 0x661270, 0x6612A0, 0x6612F0, 0x661330
COUNT2, BY_INDEX2, BY_INDEX0, BY_INDEX1 = 0x661390, 0x6613C0, 0x661400, 0x661440
BUILD, STORE, SORT, SORT_LIST = 0x661480, 0x661510, 0x6615F0, 0x6616E0
COMPARE = 0x6615D0
DESCENDING = 0x6615E4       # int3 padding after the compare, hooked as a second compare
POOL = 0x1234ABCD
LIMIT = 100000
PACKET_SIZE = 34


class Halt(Exception):
    """game.exe's unrecoverable-error call, with the source line it passed."""


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


class Harness:
    """game.exe with a logging allocator, a raising error handler and list helpers."""

    def __init__(self, machine):
        """Hook the allocator (ECX pool, EDX size or pointer; file, line, flag on the stack)."""
        self.machine = machine
        self.allocs = []
        self.frees = []
        machine.hook(ALLOC, self.alloc, 3)
        machine.hook(FREE, self.free, 3)
        machine.hook(FATAL, self.fatal, 0)
        machine.hook(DESCENDING, self.descending, 0)

    def alloc(self, current):
        """Log (pool, size, file, line, flag) and return a block poisoned with 0xab."""
        size = current.mu.reg_read(UC_X86_REG_EDX)
        self.allocs.append((current.mu.reg_read(UC_X86_REG_ECX), size, current.arg(0), current.arg(1), current.arg(2)))
        block = current.alloc(size)
        current.mu.mem_write(block, b"\xab" * size)
        return block

    def free(self, current):
        """Log (pool, block, file, line, flag)."""
        self.frees.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                           current.arg(0), current.arg(1), current.arg(2)))
        return 0

    def fatal(self, current):
        """Stop at the unrecoverable-error call: arg 2 is the line."""
        raise Halt(current.arg(2))

    def descending(self, current):
        """A fastcall compare (ECX, EDX point at entries): 1 when the left id is below the right."""
        left = current.r16(current.mu.reg_read(UC_X86_REG_ECX))
        right = current.r16(current.mu.reg_read(UC_X86_REG_EDX))
        return 1 if left < right else (-1 & 0xFFFFFFFF if left > right else 0)

    def call(self, address, *args, **kwargs):
        """Run a function, check it returned to the stub and clear the logs first."""
        self.allocs.clear()
        self.frees.clear()
        result = self.machine.call(address, *args, limit=LIMIT, **kwargs)
        require(self.machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: stopped at the cap")
        return result

    def make(self, nodes):
        """A list in memory with nodes [(id, type)] from the head; returns its address."""
        machine = self.machine
        header = machine.alloc(12)
        machine.w32(header, POOL)
        head = 0
        for message, kind in reversed(nodes):
            node = machine.alloc(12)
            machine.mu.mem_write(node, message.to_bytes(2, "little") + b"\xcd\xcd")
            machine.w32(node + 4, kind)
            machine.w32(node + 8, head)
            head = node
        machine.mu.mem_write(header + 4, len(nodes).to_bytes(2, "little") + b"\xcd\xcd")
        machine.w32(header + 8, head)
        return header

    def nodes(self, header):
        """The list's (address, id, type) from the head."""
        machine = self.machine
        found = []
        node = machine.r32(header + 8)
        while node:
            found.append((node, machine.r16(node), machine.r32(node + 4)))
            node = machine.r32(node + 8)
            require(len(found) < 64, "list does not end")
        return found


def expect_halt(harness, line, address, *args, **kwargs):
    """Run a function that must reach the error handler with this line."""
    try:
        harness.call(address, *args, **kwargs)
    except Halt as halt:
        require(halt.args[0] == line, f"{address:#x}: halt line {halt.args[0]:#x}, expected {line:#x}")
        return
    raise RuntimeError(f"{address:#x}: no halt, expected line {line:#x}")


def main():
    """Run every function on lists built in emulated memory and compare with a model."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME) == ".\\Text\\Text.cpp", "0x6edea8 is not .\\Text\\Text.cpp")
    require(machine.cstr(0x6CC837) == "", "0x6cc837 is not the empty message")
    harness = Harness(machine)
    rng = random.Random(27)
    cases = 0

    # new list
    header = harness.call(NEW_LIST, POOL)
    require(harness.allocs == [(POOL, 12, FILE_NAME, 0x2B, 0)], f"new list alloc {harness.allocs}")
    require((machine.r32(header), machine.r16(header + 4), machine.r32(header + 8)) == (POOL, 0, 0), "new list header")

    # add: head insert, id kept as a word, type a dword
    for message, kind in ((0x0E8C, 2), (0x12345, 1), (7, 0), (0xFFFF, 2)):
        before = harness.nodes(header)
        harness.call(ADD, header, message, kind)
        require(harness.allocs == [(POOL, 12, FILE_NAME, 0x5A, 0)], f"add alloc {harness.allocs}")
        after = harness.nodes(header)
        require([(m, k) for _, m, k in after] == [(message & 0xFFFF, kind)] + [(m, k) for _, m, k in before], "add order")
        require(machine.r16(header + 4) == len(after), "add count")
        cases += 1

    # remove: head, middle, last, absent, and an empty list
    for target in (0xFFFF, 7, 0x1111, 0x0E8C, 0x2345, 0x0E8C):
        before = harness.nodes(header)
        harness.call(REMOVE, header, target)
        after = harness.nodes(header)
        hit = next((entry for entry in before if entry[1] == target), None)
        if hit is None:
            require(not harness.frees and after == before, f"remove absent {target:#x}")
        else:
            require(harness.frees == [(POOL, hit[0], FILE_NAME, 0x7B, 0)], f"remove free {harness.frees}")
            require(after == [entry for entry in before if entry[0] != hit[0]], f"remove {target:#x}")
            require(machine.r16(header + 4) == len(after), "remove count")
        cases += 1
    twice = harness.make([(5, 2), (5, 1), (6, 0)])
    harness.call(REMOVE, twice, 5)
    require([(m, k) for _, m, k in harness.nodes(twice)] == [(5, 1), (6, 0)], "remove drops only the first match")
    harness.call(REMOVE, harness.make([]), 5)
    require(not harness.frees, "remove on an empty list")
    cases += 2

    # getters and the count, against a model on random lists
    for _ in range(300):
        nodes = [(rng.choice((0, 1, 2, 3, 0xE8C, 0xFFFF, rng.randrange(0x10000))),
                  rng.choice((0, 1, 2, 3))) for _ in range(rng.randint(0, 7))]
        lst = harness.make(nodes)
        require(harness.call(COUNT2, lst) == sum(1 for _, k in nodes if k == 2), f"count2 {nodes}")
        for kind, address, none in ((2, BY_INDEX2, 0xE8C), (0, BY_INDEX0, 0xFFFF), (1, BY_INDEX1, 0xFFFF)):
            ids = [m for m, k in nodes if k == kind]
            for index in range(len(ids) + 2):
                got = harness.call(address, lst, index) & 0xFFFF
                want = ids[index] if index < len(ids) else none
                require(got == want, f"getter {address:#x} kind {kind} index {index}: {got:#x} vs {want:#x} in {nodes}")
        cases += 1

    # free all: frees in list order, count 0, head untouched; the header version also frees the header
    nodes = [(1, 2), (2, 1), (3, 0)]
    lst = harness.make(nodes)
    entries = harness.nodes(lst)
    harness.call(FREE_ALL, regs={"edi": lst})
    require([f[1] for f in harness.frees] == [a for a, _, _ in entries], f"free-all order {harness.frees}")
    require(all(f == (POOL, f[1], FILE_NAME, 0x3C, 0) for f in harness.frees), "free-all file or line")
    require(machine.r16(lst + 4) == 0, "free-all count")
    require(machine.r32(lst + 8) == entries[0][0], "free-all leaves the head pointer as it was")
    harness.call(FREE_ALL, regs={"edi": harness.make([])})
    require(not harness.frees, "free-all on an empty list")
    lst = harness.make(nodes)
    entries = harness.nodes(lst)
    harness.call(FREE_LIST, lst)
    require([f[1] for f in harness.frees] == [a for a, _, _ in entries] + [lst], f"free-list {harness.frees}")
    require([f[3] for f in harness.frees] == [0x3C] * 3 + [0x4C], "free-list lines")
    cases += 3

    # build the packet block
    for count in range(0, 9):
        nodes = [(rng.randrange(0x10000), rng.randrange(256)) for _ in range(count)]
        lst = harness.make(nodes)
        block = machine.put(b"\x77" * PACKET_SIZE)
        harness.call(BUILD, lst, block)
        want = bytearray(PACKET_SIZE)
        want[0] = count
        for index, (message, kind) in enumerate(nodes):
            want[2 + 4 * index] = kind
            want[4 + 4 * index:6 + 4 * index] = message.to_bytes(2, "little")
        require(machine.read(block, PACKET_SIZE) == bytes(want), f"build {count}: {machine.read(block, PACKET_SIZE).hex()}")
        # store it back: the nodes reverse, a fresh list for each
        fresh = harness.make([])
        if count == 8:
            # the builder fills up to 8 entries but the reader refuses a count of 8 (byte < 8)
            expect_halt(harness, 0x112, STORE, fresh, block)
            continue
        harness.call(STORE, fresh, block)
        require([(m, k) for _, m, k in harness.nodes(fresh)] == [(m, k) for m, k in reversed(nodes)], f"store {count}")
        require(machine.r16(fresh + 4) == count, f"store count {count}")
        require(len(harness.allocs) == count and all(a[3] == 0x5A for a in harness.allocs), "store allocs")
        cases += 1
    ids = [(i, i % 3) for i in range(9)]
    expect_halt(harness, 0xF8, BUILD, harness.make(ids), machine.put(b"\0" * PACKET_SIZE))
    expect_halt(harness, 0xED, BUILD, 0, machine.put(b"\0" * PACKET_SIZE))
    expect_halt(harness, 0xEE, BUILD, harness.make([]), 0)
    packet = machine.put(bytes([8]) + b"\0" * 40)
    expect_halt(harness, 0x112, STORE, harness.make([]), packet)
    expect_halt(harness, 0x10B, STORE, 0, packet)
    expect_halt(harness, 0x10C, STORE, harness.make([]), 0)
    expect_halt(harness, 0x165, SORT_LIST, 0)
    cases += 7
    # a store frees the old nodes first (line 0x3c) and the 8 case halts after that
    lst = harness.make([(1, 1), (2, 1)])
    expect_halt(harness, 0x112, STORE, lst, packet)
    require(len(harness.frees) == 2 and machine.r16(lst + 4) == 0, "store frees the old nodes before the count check")

    # sort: the entry sorter with the id compare and with a hooked descending compare
    for _ in range(300):
        count = rng.randint(0, 9)
        entries = [(rng.randrange(6), rng.randrange(1000)) for _ in range(count)]
        for compare, reverse in ((COMPARE, False), (DESCENDING, True)):
            buffer = machine.alloc(12 * max(count, 1))
            for index, (message, tag) in enumerate(entries):
                machine.mu.mem_write(buffer + 12 * index, message.to_bytes(2, "little"))
                machine.w32(buffer + 12 * index + 4, tag)
            harness.call(SORT, buffer, compare, ecx=count)
            got = [(machine.r16(buffer + 12 * i), machine.r32(buffer + 12 * i + 4)) for i in range(count)]
            # stable either way: equal ids keep their order
            want = sorted(entries, key=lambda entry: -entry[0] if reverse else entry[0])
            require(got == want, f"sort {compare:#x} {entries}: {got}")
        cases += 1

    # sort a list: ids ascending, stable, through one count * 12 buffer
    for _ in range(300):
        count = rng.randint(0, 9)
        nodes = [(rng.randrange(5), rng.randrange(4)) for _ in range(count)]
        lst = harness.make(nodes)
        harness.call(SORT_LIST, lst)
        got = [(m, k) for _, m, k in harness.nodes(lst)]
        require(got == sorted(nodes, key=lambda entry: entry[0]), f"sort-list {nodes}: {got}")
        if count < 2:
            require(not harness.allocs and not harness.frees, "sort-list of 0 or 1 touches no memory")
        else:
            require(len(harness.allocs) == 1 and harness.allocs[0][1:] == (count * 12, FILE_NAME, 0x16C, 0),
                    f"sort-list alloc {harness.allocs}")
            require(len(harness.frees) == 1 and harness.frees[0][2:] == (FILE_NAME, 0x17E, 0), "sort-list free")
        cases += 1
    print(f"{cases} cases: new / add / remove / free / getters / build / store / sort; "
          "free-all leaves the head, the 9th packet entry halts at 0xf8, a store of 8 at 0x112")
    print("ok")


if __name__ == "__main__":
    main()
