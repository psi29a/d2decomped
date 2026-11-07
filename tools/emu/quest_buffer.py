# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the quest flag buffer's allocation and release in stock game.exe 1.14d.

    uv run python quest_buffer.py

FUN_0065c430 and FUN_0065c490 run for real, including the constructor's
memset and bit-buffer initialization. Only pool allocation/free are hooked:
the test checks their arguments and order, poisoned storage, guard bytes,
independent live buffers, and the null release paths. This checks the stock
lifetime contract; d2d owns its 96 quest bytes by value instead.
"""
import struct
import sys

import emu

HEADER_SIZE = 0x14
FLAGS_SIZE = 0x60
GUARD_SIZE = 16
LIMIT = 10000


def expect(actual, expected, label):
    """Fail with the observed values, including under Python's -O mode."""
    if actual != expected:
        raise AssertionError(f"{label}: got {actual!r}, expected {expected!r}")


class Buffers:
    """Run the real lifetime helpers against guarded, poisoned pool storage."""

    def __init__(self):
        """Hook only the allocation and free boundaries, once per emulator."""
        self.machine = emu.Emu()
        self.blocks = {}
        self.live = set()
        self.allocations = []
        self.frees = []
        self.poison = 0xA5
        self.machine.hook(0x40B430, self.allocate, 3)
        self.machine.hook(0x40B480, self.free, 3)

    def allocate(self, machine):
        """Record pool arguments and return nonzero storage with both guards."""
        pool = machine.mu.reg_read(emu.UC_X86_REG_ECX)
        size = machine.mu.reg_read(emu.UC_X86_REG_EDX)
        if size not in (HEADER_SIZE, FLAGS_SIZE):
            raise AssertionError(f"unexpected allocation size {size}")
        self.allocations.append((pool, size, machine.arg(1), machine.arg(2)))
        raw = machine.alloc(size + 2 * GUARD_SIZE, zero=False)
        machine.mu.mem_write(raw, bytes([self.poison]) * (size + 2 * GUARD_SIZE))
        address = raw + GUARD_SIZE
        self.blocks[address] = (size, self.poison)
        self.live.add(address)
        return address

    def free(self, machine):
        """Record release order, reject duplicate frees, and poison released data."""
        pool = machine.mu.reg_read(emu.UC_X86_REG_ECX)
        address = machine.mu.reg_read(emu.UC_X86_REG_EDX)
        self.frees.append((pool, address, machine.arg(1), machine.arg(2)))
        if address:
            if address not in self.live:
                raise AssertionError(f"free of non-live block {address:#x}")
            self.live.remove(address)
            size, _ = self.blocks[address]
            machine.mu.mem_write(address, b"\xDD" * size)
        return 0

    def call(self, address, *args):
        """Bound each stock call and require its return and stack cleanup."""
        machine = self.machine
        result = machine.call(address, *args, limit=LIMIT)
        expect(machine.mu.reg_read(emu.UC_X86_REG_EIP), emu.RET_MAGIC, "return reached")
        expect(machine.mu.reg_read(emu.UC_X86_REG_ESP),
               emu.STACK + emu.STACK_SIZE - 0x1000, "callee stack cleanup")
        return result

    def guards(self):
        """Check that constructor and destructor stayed inside every allocation."""
        for address, (size, poison) in self.blocks.items():
            expected = bytes([poison]) * GUARD_SIZE
            expect(self.machine.read(address - GUARD_SIZE, GUARD_SIZE), expected, "leading guard")
            expect(self.machine.read(address + size, GUARD_SIZE), expected, "trailing guard")

    def create(self, pool):
        """Verify the 20-byte header, 96 zero bytes, and allocation arguments."""
        start = len(self.allocations)
        header = self.call(0x65C430, pool)
        expect(self.allocations[start:],
               [(pool, HEADER_SIZE, 0x50, 0), (pool, FLAGS_SIZE, 0x51, 0)],
               "constructor allocations")
        expect(self.blocks.get(header), (HEADER_SIZE, self.poison), "returned header")
        data, bits, cursor, field_c, field_10 = struct.unpack("<5I", self.machine.read(header, HEADER_SIZE))
        expect(self.blocks.get(data), (FLAGS_SIZE, self.poison), "payload allocation")
        expect((bits, cursor, field_c, field_10), (768, 0, 0, 0), "bit-buffer header")
        expect(self.machine.read(data, FLAGS_SIZE), bytes(FLAGS_SIZE), "zeroed quest flags")
        self.guards()
        return header, data

    def release(self, pool, header, data):
        """Verify that payload and header are freed in order through the given pool."""
        start = len(self.frees)
        self.call(0x65C490, pool, header)
        expect(self.frees[start:], [(pool, data, 0x60, 0), (pool, header, 0x61, 0)],
               "release calls")
        self.guards()


def main():
    """Exercise distinct pools, poison patterns, live siblings and null paths."""
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    harness = Buffers()
    machine = harness.machine
    pools = (0, 17, machine.alloc(4), 0xDEADBEEF)
    cases = 0
    for poison in (0x5A, 0xA5, 0xFF):
        harness.poison = poison
        for pool in pools:
            first, first_data = harness.create(pool)
            machine.mu.mem_write(first_data, bytes([poison]) * FLAGS_SIZE)
            second, second_data = harness.create(pool)
            if first == second or first_data == second_data:
                raise AssertionError("live quest buffers alias")
            expect(machine.read(first_data, FLAGS_SIZE), bytes([poison]) * FLAGS_SIZE,
                   "second constructor preserves first buffer")
            harness.release(pool, first, first_data)
            expect(machine.read(second_data, FLAGS_SIZE), bytes(FLAGS_SIZE),
                   "first release preserves second buffer")
            harness.release(pool, second, second_data)
            cases += 2
    for pool in pools:
        before = len(harness.frees)
        harness.call(0x65C490, pool, 0)
        expect(len(harness.frees), before, "null header releases nothing")
        cases += 1
    # A non-null header with a null payload still forwards both free calls.
    header, data = harness.create(pools[2])
    machine.w32(header, 0)
    harness.release(pools[2], header, 0)
    # The deliberately detached payload belongs to the harness, not the helper.
    harness.live.remove(data)
    expect(harness.live, set(), "no live harness blocks")
    print(f"ok: {cases + 1} quest buffer lifetime cases")


if __name__ == "__main__":
    main()
