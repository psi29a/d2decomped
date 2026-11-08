# SPDX-License-Identifier: GPL-3.0-or-later
"""Run five remaining Chat/Ignorelist.cpp functions from game.exe 1.14d.

    uv run python ignorelist_remaining.py

Tests node cleanup, hash-table deletion and clearing, and eager/lazy singleton
initialization. Allocation, construction and the virtual delete are hooked;
the table unlinking and node cleanup execute in game.exe. No MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FILE_NAME = 0x6E6290
GLOBAL = 0x96BC20
NODE_CLEANUP = 0x60ED30
TABLE_DELETE = 0x60ED70
TABLE_CLEAR = 0x60EE60
EAGER = 0x6114E0
LAZY = 0x611560
SMEM_ALLOC = 0x413020
SMEM_FREE = 0x412650
CONSTRUCTOR = 0x611200
UNLINK = 0x60E6E0
VIRTUAL = 0x6615E4
LIMIT = 100000


def require(condition, message):
    """Fail on a mismatch rather than silently accepting an incomplete run."""
    if not condition:
        raise RuntimeError(message)


def run(machine, address, *args, **kwargs):
    """Call game code and require a normal return before the instruction cap."""
    result = machine.call(address, *args, limit=LIMIT, **kwargs)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: stopped at instruction cap")
    return result


def main():
    """Check the stock routines with controlled allocators and callbacks."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME) == ".\\Chat\\Ignorelist.cpp", "unexpected source string")
    frees = []
    machine.hook(SMEM_FREE, lambda current: frees.append(tuple(current.arg(i) for i in range(4))) or 1, 4)
    for value in (False, True):
        for extra in (False, True):
            node = machine.alloc(0x40)
            value_ptr = machine.alloc(8) if value else 0
            extra_ptr = machine.alloc(8) if extra else 0
            machine.w32(node + 0x18, value_ptr)
            machine.w32(node + 0x14, extra_ptr)
            frees.clear()
            run(machine, NODE_CLEANUP, ecx=node)
            expected = ([(value_ptr, FILE_NAME, 0x11, 0)] if value else [])
            expected += ([(extra_ptr, 0x6E6218, 0x5E4, 0)] if extra else [])
            require(frees == expected, f"node cleanup: {frees}")

    events = []
    machine.hook(UNLINK, lambda current: events.append(("unlink", current.arg(0))) or 0, 1)
    machine.hook(VIRTUAL, lambda current: events.append(("delete", current.arg(0), current.mu.reg_read(UC_X86_REG_ECX))) or 0, 1)
    for node_value in (False, True):
        for link in (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
            table = machine.alloc(0x30)
            vtable = machine.alloc(4)
            node = machine.alloc(0x40) if node_value else 0
            machine.w32(vtable, VIRTUAL)
            machine.w32(table, vtable)
            machine.w32(table + 4, 4)
            machine.w32(node + 8 if node else table + 12, link)
            events.clear()
            got = run(machine, TABLE_DELETE, node, ecx=table)
            require(got == (link if 0 < link < 0x80000000 else 0), f"delete next {link:#x}: {got:#x}")
            require(events == [("unlink", node), ("delete", node, table)], f"delete sequence: {events}")

    for bucket_count in (0, 1, 3):
        table = machine.alloc(0x40)
        machine.w32(table + 8, table + 8)
        machine.w32(table + 12, ~(table + 8))
        machine.w32(table + 0x10, 123)
        buckets = machine.alloc(max(bucket_count * 12, 1))
        machine.w32(table + 0x18, bucket_count)
        machine.w32(table + 0x1C, buckets)
        for index in range(bucket_count):
            bucket = buckets + index * 12
            machine.w32(bucket + 4, bucket + 4)
            machine.w32(bucket + 8, ~(bucket + 4))
        for mode in (0, 1):
            machine.w32(table + 0x10, 123)
            run(machine, TABLE_CLEAR, mode, ecx=table)
            require(machine.r32(table + 0x10) == 0, "clear did not zero count")
            for index in range(bucket_count):
                bucket = buckets + index * 12
                require(machine.r32(bucket + 4) == bucket + 4 and machine.r32(bucket + 8) == (~(bucket + 4) & 0xFFFFFFFF), "empty bucket changed")

    bucket_for_node = {}

    def delete_node(current):
        """Emulate the virtual delete by unlinking its known bucket node."""
        node = current.arg(0)
        bucket = bucket_for_node[node]
        events.append(("delete", node, current.mu.reg_read(UC_X86_REG_ECX)))
        current.w32(bucket + 4, bucket + 4)
        current.w32(bucket + 8, ~(bucket + 4))
        return 0

    machine.hook(VIRTUAL, delete_node, 1)
    for mode in (0, 1):
        table = machine.alloc(0x40)
        vtable = machine.alloc(4)
        buckets = machine.alloc(24)
        machine.w32(vtable, VIRTUAL)
        machine.w32(table, vtable)
        machine.w32(table + 8, table + 8)
        machine.w32(table + 12, ~(table + 8))
        machine.w32(table + 0x10, 2)
        machine.w32(table + 0x18, 2)
        machine.w32(table + 0x1C, buckets)
        bucket_for_node.clear()
        for index in range(2):
            bucket = buckets + index * 12
            node = machine.alloc(16)
            machine.w32(bucket + 4, node)
            machine.w32(bucket + 8, node)
            machine.w32(node, bucket + 4)
            machine.w32(node + 4, ~(bucket + 4))
            bucket_for_node[node] = bucket
        events.clear()
        run(machine, TABLE_CLEAR, mode, ecx=table)
        require(machine.r32(table + 0x10) == 0, "populated clear did not reset count")
        require(events == ([] if mode else [("delete", node, table) for node in bucket_for_node]), f"clear callbacks: {events}")
        for node, bucket in bucket_for_node.items():
            require(machine.r32(bucket + 4) == bucket + 4 and machine.r32(bucket + 8) == (~(bucket + 4) & 0xFFFFFFFF), "bucket not empty")
            if mode:
                require(machine.r32(node) == machine.r32(node + 4) == 0, "node still linked")

    allocs = []
    ctors = []
    state = {"fail": False}

    def alloc(current):
        """Record SMemAlloc arguments and optionally emulate allocation failure."""
        allocs.append(tuple(current.arg(i) for i in range(4)))
        return 0 if state["fail"] else current.alloc(current.arg(0))

    def construct(current):
        """Record the constructor input and return a distinct singleton pointer."""
        ctors.append(current.mu.reg_read(UC_X86_REG_ECX))
        return ctors[-1] + 4

    machine.hook(SMEM_ALLOC, alloc, 4)
    machine.hook(CONSTRUCTOR, construct, 0)
    for address, line in ((EAGER, 0x41), (LAZY, 0x48)):
        for fail in (False, True):
            state["fail"] = fail
            machine.w32(GLOBAL, 0)
            allocs.clear()
            ctors.clear()
            result = run(machine, address)
            require(allocs == [(0x188, FILE_NAME, line, 0)], f"allocation: {allocs}")
            expected = 0 if fail else ctors[0] + 4
            require(len(ctors) == (0 if fail else 1) and machine.r32(GLOBAL) == expected, "singleton construction")
            if address == LAZY:
                require(result == expected, "lazy return")
        machine.w32(GLOBAL, 0x12345678)
        allocs.clear()
        ctors.clear()
        if address == LAZY:
            require(run(machine, LAZY) == 0x12345678 and not allocs and not ctors, "lazy existing singleton")
        else:
            state["fail"] = False
            run(machine, EAGER)
            require(allocs == [(0x188, FILE_NAME, line, 0)] and len(ctors) == 1 and machine.r32(GLOBAL) == ctors[0] + 4,
                    "eager init did not replace existing singleton")
    print("ok: node cleanup, hash deletion, empty and populated table clear, eager and lazy singleton")


if __name__ == "__main__":
    main()
