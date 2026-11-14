# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00620290 unit allocation in local game.exe 1.14d.

Run: uv run python unit_allocate.py --exe /path/to/Game.exe [--mutation-check]
The unit allocator and CRT memset run in Unicorn; the pool allocator is
scripted, logs its arguments, and returns poisoned memory or injected null.
No MPQs, native game processes, network, or d2d comparison are involved.
"""
import argparse
import hashlib
import itertools
import random
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

SHA256 = '631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf'
FUNCTION, ALLOC, FILE = 0x620290, 0x40B430, 0x6E8E10
MUTATION = None


def check(actual, expected, label):
    """Reject a discrepancy with its case label."""
    if actual != expected:
        raise AssertionError(f'{label}: {actual!r} != {expected!r}')


class Rig:
    """Execute the original unit allocator with a controlled pool allocator."""

    def __init__(self, exe):
        """Map game.exe and replace only its pool allocation routine."""
        self.machine = emu.Emu(str(exe))
        self.allocations = []
        self.fail_index = None
        self.machine.hook(ALLOC, self.allocate, 3)
        check(self.machine.cstr(FILE), '.\\UNITS\\Units.cpp', 'source filename')

    def allocate(self, machine):
        """Log allocation arguments and return poisoned guarded storage."""
        pool = machine.mu.reg_read(UC_X86_REG_ECX)
        size = machine.mu.reg_read(UC_X86_REG_EDX)
        source, line, flag = (machine.arg(index) for index in range(3))
        pointer = 0
        if len(self.allocations) != self.fail_index:
            base = machine.alloc(size + 32)
            machine.mu.mem_write(base, b'\xa5' * (size + 32))
            pointer = base + 16
        self.allocations.append((pool, size, source, line, flag, pointer))
        return pointer

    def call(self, pool, kind):
        """Call with two stack arguments and require return and stack cleanup."""
        self.allocations.clear()
        result = self.machine.call(FUNCTION, pool, kind, limit=100000)
        check(self.machine.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, 'return sentinel')
        check(self.machine.mu.reg_read(UC_X86_REG_ESP), emu.STACK + emu.STACK_SIZE - 0x1000, 'stack cleanup')
        return result


def successful(rig):
    """Compare allocated blocks, guards, arguments, and return across types."""
    rng = random.Random(0x620290)
    cases = 0
    types = [0, 1, 2, 3, 4, 5, 6, 7, 0xffffffff, 0x80000000]
    types += [rng.getrandbits(32) for _ in range(50)]
    for pool, kind in itertools.product((0, 1, 0x12345678, 0xffffffff), types):
        rig.fail_index = None
        unit = rig.call(pool, kind)
        monster = kind == 1
        wanted = [(pool, 0xf4, FILE, 0x66e, 0)]
        if monster:
            wanted.append((pool, 0x60, FILE, 0x67b, 0))
        check([entry[:5] for entry in rig.allocations], wanted, 'allocation calls')
        check(unit, rig.allocations[0][5], 'returned unit')
        expected = bytearray(0xf4)
        struct.pack_into('<I', expected, 0, kind)
        struct.pack_into('<I', expected, 8, pool)
        if monster:
            struct.pack_into('<I', expected, 0x14, rig.allocations[1][5])
        if MUTATION == 'wrong-pool-field':
            struct.pack_into('<I', expected, 8, pool ^ 1)
        if MUTATION == 'nonzero-padding':
            expected[-1] = 0xa5
        check(rig.machine.read(unit, 0xf4), bytes(expected), 'unit bytes')
        for _, size, _, _, _, pointer in rig.allocations:
            check(rig.machine.read(pointer - 16, 16), b'\xa5' * 16, 'leading guard')
            check(rig.machine.read(pointer + size, 16), b'\xa5' * 16, 'trailing guard')
        if monster:
            check(rig.machine.read(rig.allocations[1][5], 0x60), bytes(0x60), 'monster data zeroed')
        cases += 1
    return cases


def allocation_failure(rig):
    """Inject null on each allocation and require a fault, not a null return."""
    cases = 0
    for kind, failure in ((0, 0), (1, 0), (1, 1)):
        rig.fail_index = failure
        try:
            rig.call(0x1234, kind)
        except RuntimeError as error:
            check('emu fault at 00681f4f' in str(error), True, 'null memset fault')
            check(len(rig.allocations), failure + 1, 'allocation failure position')
            check(rig.allocations[-1][5], 0, 'injected null')
            if failure == 1:
                partial = bytearray(0xf4)
                struct.pack_into('<I', partial, 0, 1)
                struct.pack_into('<I', partial, 8, 0x1234)
                check(rig.machine.read(rig.allocations[0][5], 0xf4), bytes(partial), 'partial unit before null fault')
        else:
            raise AssertionError(f'null allocation {kind}/{failure} unexpectedly returned')
        cases += 1
    return cases


def main():
    """Validate the executable and run stock-code checks and negative controls."""
    global MUTATION
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--mutation-check', action='store_true')
    options = parser.parse_args()
    check(hashlib.sha256(options.exe.read_bytes()).hexdigest(), SHA256, 'game.exe SHA-256')
    rig = Rig(options.exe)
    print(f'success: {successful(rig)} cases', flush=True)
    print(f'injected null: {allocation_failure(rig)} fault cases', flush=True)
    if options.mutation_check:
        for mutation in ('wrong-pool-field', 'nonzero-padding'):
            MUTATION = mutation
            try:
                successful(Rig(options.exe))
            except AssertionError as error:
                if not str(error).startswith('unit bytes:'):
                    raise
                print(f'caught mutation: {mutation}', flush=True)
            else:
                raise AssertionError(f'mutation survived: {mutation}')
            finally:
                MUTATION = None


if __name__ == '__main__':
    main()
