# SPDX-License-Identifier: GPL-3.0-or-later
"""Check 34 stock 1.14d H/stpl.h list, arithmetic and wrapper routines.

Usage: D2_MPQ_DIR=/path/to/game-data python h_lists.py [--mutation-check]
Synthetic lists are encoded from node order; compare the whole arena after
real insert/remove operations. External string/hash/free/virtual calls are
recording hooks. Only the local executable is read; no MPQs, user ignore
files or game process are opened.
"""

import argparse
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MASK = 0xffffffff
ARENA_SIZE = 0x2000


def equal(actual, expected, label):
    """Fail on an observable mismatch, including when assertions are disabled."""
    if actual != expected:
        raise AssertionError(f"{label}: got {actual!r}, expected {expected!r}")


def write(image, base, address, value):
    """Write a 32-bit field in an independent expected arena image."""
    struct.pack_into("<I", image, address - base, value & MASK)


class Rig:
    """Bounded emulator calls, arena comparisons and executed-entry accounting."""

    def __init__(self):
        """Pin stock bytes and reserve an arena with no filesystem or process mocks."""
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        equal(hashlib.sha256(exe.read_bytes()).hexdigest(), GAME_SHA256, "binary identity")
        self.machine = emu.Emu(str(exe))
        self.base = self.machine.alloc(ARENA_SIZE)
        self.calls = 0
        self.entries = set()

    def reset(self):
        """Restore deterministic nonzero guard bytes and return the model copy."""
        image = bytes((index * 37 + 11) & 255 for index in range(ARENA_SIZE))
        self.machine.mu.mem_write(self.base, image)
        return bytearray(image)

    def run(self, address, *stack, ecx=0, pop=None):
        """Require normal return, the declared RET cleanup, and record real entries."""
        result = self.machine.call(address, *stack, ecx=ecx, limit=100000)
        equal(self.machine.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, "instruction cap")
        cleaned = len(stack) if pop is None else pop
        expected_sp = emu.STACK + emu.STACK_SIZE - 0x1000 - 4 * len(stack) + 4 * cleaned
        equal(self.machine.mu.reg_read(UC_X86_REG_ESP), expected_sp, f"RET {address:08x}")
        self.calls += 1
        self.entries.add(address)
        return result

    def compare(self, image, label):
        """Check the complete arena, including untouched fields and guard bytes."""
        got = self.machine.read(self.base, ARENA_SIZE)
        differences = [index for index, value in enumerate(got) if value != image[index]]
        equal(differences[:8], [], f"{label}: differing arena offsets")

    def install(self, image):
        """Load a freshly built synthetic scenario into the original-code arena."""
        self.machine.mu.mem_write(self.base, bytes(image))


def encode_list(image, base, header, offset, nodes):
    """Encode a valid intrusive chain from its high-level node ordering."""
    terminator = header + 4
    write(image, base, header, offset)
    write(image, base, terminator, nodes[-1] + offset if nodes else terminator)
    write(image, base, terminator + 4, nodes[0] if nodes else ~terminator)
    for index, node in enumerate(nodes):
        previous = nodes[index - 1] + offset if index else terminator
        following = nodes[index + 1] if index + 1 < len(nodes) else ~terminator
        write(image, base, node + offset, previous)
        write(image, base, node + offset + 4, following)


def scalar_cases(rig, mutation=None):
    """Check 23 arithmetic, initialization and direct field-access entry points."""
    base = rig.base
    this, node, parameter = base + 0x100, base + 0x300, base + 0x500
    for address in (0x60e0e0, 0x60e110):
        for index in (0, 1, 7, 0xffffffff):
            image = rig.reset()
            write(image, base, this + 8, node)
            rig.install(image)
            equal(rig.run(address, index, ecx=this), (node + index * 12) & MASK, "bucket arithmetic")
            rig.compare(image, "bucket guards")
    for address in (0x60e170, 0x60e1c0):
        for next_value in (0, 1, node, 0x7fffffff, 0x80000000, 0xffffffff):
            for offset in (0, 12, 0xffffffff):
                image = rig.reset()
                write(image, base, this, parameter)
                write(image, base, this + 4, next_value)
                write(image, base, parameter + 4, node)
                rig.install(image)
                expected = (~next_value & MASK) if next_value & 0x80000000 else (
                    next_value + (this - node if offset & 0x80000000 else offset)) & MASK
                equal(rig.run(address, offset, ecx=this), expected, "next link address")
                rig.compare(image, "next link guards")
    for address in (0x60e1f0, 0x60e210):
        for mask in (0, 7, 0x80000000, MASK):
            for value in (0, 13, 0x89abcdef):
                image = rig.reset()
                write(image, base, this + 0x24, mask)
                rig.install(image)
                equal(rig.run(address, value, ecx=this), value & mask, "hash mask")
                rig.compare(image, "hash guards")
    for address in (0x60e2a0, 0x60e300):
        for value in (*range(23), 31, 32, 255, 0x80000000, MASK):
            image = rig.reset()
            rig.install(image)
            expected = max(1, 1 << (value.bit_length() - 1)) if value else 1
            if value >= 21:
                expected = 20 if mutation == "grow-cap" else 21
                write(image, base, this + 12, expected)
            equal(rig.run(address, value, ecx=this), expected, "growth cap")
            rig.compare(image, "growth writes")
    for address in (0x60e2e0, 0x60e340):
        for value in (0, 1, 20, 21, 22, 250, MASK):
            for step in (1, 2, 3, 21, MASK):
                image = rig.reset()
                expected = (value + (-value % step)) & MASK
                if mutation == "round-up" and value % step:
                    expected = (value - value % step) & MASK
                equal(rig.run(address, value, step), expected, "unsigned round up")
                rig.compare(image, "rounding guards")
    for address in (0x60e370, 0x60e390, 0x60e470, 0x60e5b0, 0x60e9b0, 0x60e9e0):
        for offset in (0, 4, 12, MASK):
            image = rig.reset()
            write(image, base, parameter, offset)
            rig.install(image)
            by_value = address in (0x60e470, 0x60e5b0)
            rig.run(address, offset if by_value else parameter, ecx=this)
            encode_list(image, base, this, offset, [])
            rig.compare(image, "list initialization")
    for address in (0x60e450, 0x60e630, 0x60e540, 0x60e940, 0x60e650, 0x60e7c0, 0x60eaa0):
        for offset in (4, 12):
            for pointer in (0, node):
                for value in (0, 1, 0x7fffffff, 0x80000000, MASK):
                    image = rig.reset()
                    header = this + 4 if address == 0x60eaa0 else this
                    write(image, base, header, offset)
                    location = pointer + offset if pointer else header + 4
                    write(image, base, location + 4, value)
                    rig.install(image)
                    if address in (0x60e450, 0x60e630):
                        expected = location
                    elif address in (0x60e540, 0x60e940):
                        expected = value
                    else:
                        expected = value if 0 < value < 0x80000000 else 0
                    equal(rig.run(address, pointer, ecx=this), expected, "link/next getter")
                    rig.compare(image, "getter guards")


def link_cases(rig, mutation=None):
    """Check real insert/unlink operations using lists encoded from node order."""
    base = rig.base
    header, other = base + 0x100, base + 0x180
    nodes = [base + 0x300 + 0x100 * index for index in range(4)]
    for address in (0x60e490, 0x60e890):
        for offset in (4, 12):
            for attached in (False, True):
                for target in (0, nodes[0], nodes[1]):
                    for after in (0, 1, 2):
                        image = rig.reset()
                        encode_list(image, base, header, offset, nodes[:2])
                        encode_list(image, base, other, offset, [nodes[2]] if attached else [])
                        if not attached:
                            write(image, base, nodes[2] + offset, 0)
                            write(image, base, nodes[2] + offset + 4, 0)
                        rig.install(image)
                        rig.run(address, nodes[2], after, target, ecx=header)
                        order = nodes[:2]
                        position = (0 if after == 1 else len(order)) if not target else (
                            order.index(target) + (after == 1))
                        if mutation == "insert-side":
                            position = len(order) if position == 0 else 0
                        order.insert(position, nodes[2])
                        encode_list(image, base, header, offset, order)
                        if attached:
                            encode_list(image, base, other, offset, [])
                        rig.compare(image, "insert ordering/unlink")
    for address in (0x60e560, 0x60e960):
        for offset in (4, 12):
            for count in (0, 1, 3):
                for pointer in (nodes[:count] if count else [0]):
                    image = rig.reset()
                    encode_list(image, base, header, offset, nodes[:count])
                    rig.install(image)
                    rig.run(address, pointer, ecx=header)
                    if pointer:
                        order = [node for node in nodes[:count] if node != pointer]
                        encode_list(image, base, header, offset, order)
                        write(image, base, pointer + offset, 0)
                        write(image, base, pointer + offset + 4, 0)
                    else:
                        terminator = header + 4
                        if count:
                            write(image, base, nodes[0] + offset, nodes[count - 1] + offset)
                            write(image, base, nodes[count - 1] + offset + 4, nodes[0])
                        write(image, base, terminator, 0)
                        write(image, base, terminator + 4, 0)
                    rig.compare(image, "unlink node/terminator")
    for offset in (4, 12):
        for count in range(5):
            image = rig.reset()
            encode_list(image, base, header, offset, nodes[:count])
            rig.install(image)
            rig.run(0x60e820, ecx=header)
            encode_list(image, base, header, offset, [])
            for pointer in nodes[:count]:
                write(image, base, pointer + offset, 0)
                write(image, base, pointer + offset + 4, 0)
            rig.compare(image, "unlink all")


def cleanup_cases(rig, mutation=None):
    """Check both embedded links, real unlink-before-delete and cleanup/free order."""
    machine, base = rig.machine, rig.base
    table, node = base + 0x100, base + 0x500
    events = []

    def free(current):
        """Record exact Storm deallocation arguments without freeing synthetic memory."""
        events.append(("free", *(current.arg(index) for index in range(4))))
        return 1

    def virtual(current):
        """Observe node links after the real unlink and record virtual-delete arguments."""
        pointer = current.arg(0)
        equal([current.r32(pointer + index) for index in (4, 8, 12, 16)], [0, 0, 0, 0], "unlink before virtual")
        events.append(("virtual", current.mu.reg_read(UC_X86_REG_ECX), pointer))
        return 0

    machine.hook(0x412650, free, 4)
    machine.hook(0x6615e4, virtual, 1)
    for address in (0x60e6e0, 0x60e7a0, 0x60ea10, 0x60eae0):
        for first in (False, True):
            for second in (False, True):
                for string in (0, base + 0x800):
                    image = rig.reset()
                    for offset in (4, 12):
                        header = table + offset * 4
                        present = first if offset == 4 else second
                        encode_list(image, base, header, offset, [node] if present else [])
                        if not present:
                            write(image, base, node + offset, 0)
                            write(image, base, node + offset + 4, 0)
                    write(image, base, node + 20, string)
                    write(image, base, table, base + 0x900)
                    write(image, base, base + 0x900, 0x6615e4)
                    rig.install(image)
                    events.clear()
                    if address == 0x60e6e0:
                        rig.run(address, node)
                    elif address == 0x60e7a0:
                        # This wrapper calls the guarded two-link unlink: absent first
                        # link suppresses both, so don't ask the virtual checker to
                        # accept an intentionally remaining second link.
                        if not first and second:
                            continue
                        rig.run(address, node, ecx=table)
                    else:
                        rig.run(address, ecx=node)
                    active = address in (0x60ea10, 0x60eae0) or first
                    if active:
                        for offset, present in ((4, first), (12, second)):
                            if present:
                                encode_list(image, base, table + offset * 4, offset, [])
                                write(image, base, node + offset, 0)
                                write(image, base, node + offset + 4, 0)
                    expected = []
                    if address in (0x60ea10, 0x60eae0) and string:
                        expected.append(("free", string, 0x6e6218, 0x5e4, 0))
                        if mutation == "free-line":
                            expected[0] = ("free", string, 0x6e6218, 0x5e5, 0)
                    if address == 0x60e7a0:
                        expected.append(("virtual", table, node))
                    equal(events, expected, "node cleanup calls")
                    rig.compare(image, "node cleanup memory")


def wrapper_cases(rig):
    """Run the real string/hash forwarding wrappers, hooking only their callees."""
    machine, base = rig.machine, rig.base
    text, duplicate, table = base + 0x100, base + 0x200, base + 0x300
    state, calls = {}, []

    def dup(current):
        """Supply the duplicate pointer or null and record its source argument."""
        calls.append(("dup", current.arg(0)))
        return state["dup"]

    def hash_text(current):
        """Record the original text and two hash-options arguments."""
        calls.append(("hash", *(current.arg(index) for index in range(3))))
        return 0x12345678

    def lookup(current):
        """Record table/hash/key values rather than stack-local pointer addresses."""
        calls.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0),
                      current.r32(current.arg(1))))
        return state["result"]

    def insert(current):
        """Record insert forwarding, including the two trailing caller arguments."""
        calls.append(("insert", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0),
                      current.r32(current.arg(1)), current.arg(2), current.arg(3)))
        return state["result"]

    def free(current):
        """Record destruction of the temporary duplicated string."""
        calls.append(("free", *(current.arg(index) for index in range(4))))
        return 1

    machine.hook(0x414310, dup, 1)
    machine.hook(0x413ce0, hash_text, 3)
    machine.hook(0x60f1d0, lookup, 2)
    machine.hook(0x610df0, insert, 4)
    machine.hook(0x412650, free, 4)
    for address in (0x60f510, 0x611170):
        for pointer in (0, duplicate):
            for result in (0, 0x2468ace0):
                image = rig.reset()
                state.update(dup=pointer, result=result)
                calls.clear()
                stack = [text] if address == 0x60f510 else [text, 7, 9]
                equal(rig.run(address, *stack, ecx=table), result, "lookup/insert wrapper result")
                operation = ("lookup", table, 0x12345678, pointer) if address == 0x60f510 else (
                    "insert", table, 0x12345678, pointer, 7, 9)
                expected = [("dup", text), ("hash", text, 0, 0), operation]
                if pointer:
                    expected.append(("free", pointer, 0x6e6218, 0x5e4, 0))
                equal(calls, expected, "temporary string forwarding/order")
                rig.compare(image, "wrapper arena guards")


def main():
    """Run the bounded cases and optional deliberately incorrect model checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig = Rig()
    scalar_cases(rig)
    link_cases(rig)
    cleanup_cases(rig)
    wrapper_cases(rig)
    equal(len(rig.entries), 34, "executed entry count")
    print(f"PASS H lists: {rig.calls} cases, {len(rig.entries)} original functions")
    if args.mutation_check:
        for name, suite in (("grow-cap", scalar_cases), ("round-up", scalar_cases),
                            ("insert-side", link_cases), ("free-line", cleanup_cases)):
            try:
                suite(Rig(), mutation=name)
            except AssertionError:
                print(f"PASS rejected model mutation: {name}")
            else:
                raise AssertionError(f"mutation was not detected: {name}")


if __name__ == "__main__":
    main()
