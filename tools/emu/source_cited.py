# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace the 21 formerly cited SOURCE functions on local game.exe 1.14d.

Run: tools/emu/.venv/bin/python tools/emu/source_cited.py --exe /path/to/Game.exe
No files, network, UI or native game processes are opened by the emulated code.
Memory, big-number arithmetic and event-level copying run in the original code.
OS calls, allocation, bitmap decoders and selected event callees are intercepted.
This checks stock behaviour against independent expectations, not d2d parity.
"""
import argparse
import hashlib
import itertools
import random
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGETS = (0x41B680, 0x41CAF0, 0x41D2A0, 0x41D2C0, 0x41D300,
           0x420320, 0x420360, 0x420A10, 0x420AB0, 0x420B00,
           0x420BF0, 0x420C10, 0x420C30, 0x420C50, 0x423850,
           0x423950, 0x423B30, 0x425330, 0x426210, 0x426370, 0x6C6A80)
COUNTS = dict.fromkeys(TARGETS, 0)
MUTATION = None


def check(actual, expected, label):
    """Compare an observation with its independently specified expectation."""
    if label == MUTATION:
        expected = ("deliberately wrong expectation", expected)
    if actual != expected:
        raise AssertionError(f"{label}: {actual!r} != {expected!r}")


class Rig:
    def __init__(self, exe):
        """Create the emulator and install allocation hooks."""
        self.machine = emu.Emu(exe, real=TARGETS)
        self.sizes = {}
        self.events = []
        self.fail_alloc = False
        self.machine.hook(0x413020, self.allocate, 4)
        self.machine.hook(0x412650, self.free, 4)
        self.machine.hook(0x4133D0, self.reallocate, 5)

    def allocate(self, machine):
        """Allocate emulated memory and record the source metadata."""
        size = machine.arg(0)
        pointer = 0 if self.fail_alloc else machine.alloc(max(size, 1))
        self.events.append(('alloc', size, machine.cstr(machine.arg(1)), machine.arg(2), machine.arg(3), pointer))
        if pointer:
            self.sizes[pointer] = size
        return pointer

    def free(self, machine):
        """Record a free without reclaiming the emulated allocation."""
        self.events.append(('free', machine.arg(0), machine.cstr(machine.arg(1)), machine.arg(2), machine.arg(3)))
        return 1

    def reallocate(self, machine):
        """Resize a tracked allocation while preserving its existing bytes."""
        old, size = machine.arg(0), machine.arg(1)
        pointer = machine.alloc(max(size, 1))
        if old:
            if old not in self.sizes:
                raise AssertionError('realloc of unknown allocation')
            machine.mu.mem_write(pointer, machine.read(old, min(size, self.sizes[old])))
        self.sizes[pointer] = size
        return pointer

    def imported(self, name, handler, count):
        """Replace a named imported function with a scripted handler."""
        for slot, (candidate, _) in list(self.machine.slots.items()):
            if candidate == name:
                self.machine.slots[slot] = (name, lambda machine: (handler(machine), count))
                return
        raise AssertionError(f'missing import {name}')

    def run(self, address, *args, regs=None):
        """Call a function and require normal return and stack cleanup."""
        machine = self.machine
        result = machine.call(address, *args, regs=regs, limit=5_000_000)
        check(machine.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, 'instruction budget / return')
        check(machine.mu.reg_read(UC_X86_REG_ESP), emu.STACK + emu.STACK_SIZE - 0x1000, 'stack cleanup')
        if address in COUNTS:
            COUNTS[address] += 1
        return result


def memory_and_file(exe):
    """Check memory primitives and file-wrapper argument forwarding."""
    rig = Rig(exe)
    machine = rig.machine
    rng = random.Random(114)
    for size in (0, 1, 2, 3, 4, 7, 16, 31, 32, 63, 128, 257):
        for alignment in range(4):
            source = bytes(rng.getrandbits(8) for _ in range(size))
            src = machine.put(source + b'!')
            dst = machine.put(b'\xa5' * (size + 16))
            rig.run(0x41D2A0, dst + alignment, src, size)
            check(machine.read(dst, size + 16), b'\xa5' * alignment + source + b'\xa5' * (16 - alignment), 'copy with guards')
            for value in (0, 1, 255, 256, 0xFEDCBA98):
                machine.mu.mem_write(dst, b'\xa5' * (size + 16))
                rig.run(0x41D2C0, dst + alignment, size, value)
                check(machine.read(dst, size + 16), b'\xa5' * alignment + bytes([value & 255]) * size + b'\xa5' * (16 - alignment), 'fill byte truncation')
            machine.mu.mem_write(dst, b'\xa5' * (size + 16))
            rig.run(0x41D300, dst + alignment, size)
            check(machine.read(dst, size + 16), b'\xa5' * alignment + bytes(size) + b'\xa5' * (16 - alignment), 'zero with guards')
    calls = []
    machine.hook(0x41AF60, lambda current: (calls.append(tuple(current.arg(index) for index in range(8))), 0x1234)[1], 8)
    for values in ((0,) * 7, tuple(range(1, 8)), (0xFFFFFFFF,) * 7):
        calls.clear()
        check(rig.run(0x41B680, *values), 0x1234, 'file wrapper result')
        check(calls, [values + (0,)], 'file wrapper forwards seven args and zero')


def events(exe):
    """Check event registration, teardown and message dispatch."""
    rig = Rig(exe)
    machine = rig.machine
    calls = []
    # Wrappers are checked in a separate image: target functions remain real.
    for address, callee in ((0x420320, 0x4269C0), (0x420360, 0x426370)):
        wrapper = emu.Emu(exe)
        wrapper.hook(callee, lambda current: (calls.append((current.arg(0), current.arg(1))), 0)[1], 2)
        for window in (0, 0x12345678, 0xFFFFFFFF):
            calls.clear()
            check(wrapper.call(address, window, limit=1000), 1, 'four-tag wrapper result')
            check(wrapper.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, 'wrapper return')
            check(wrapper.mu.reg_read(UC_X86_REG_ESP), emu.STACK + emu.STACK_SIZE - 0x1000, 'wrapper stack')
            check(calls, [(tag, window) for tag in (0x534D5348, 0x534D5349, 0x534D534A, 0x534D5347)], 'four-tag order')
            COUNTS[address] += 1
    machine.hook(0x420A70, lambda current: (calls.append((current.arg(0), current.arg(1), current.mu.reg_read(UC_X86_REG_EDI), current.mu.reg_read(UC_X86_REG_ESI))), 17)[1], 2)
    for address, tag in zip((0x420BF0, 0x420C10, 0x420C30, 0x420C50), (0x534D5348, 0x534D5349, 0x534D534A, 0x534D5347)):
        for window, code, handler in ((0, 0, 0), (0x1111, 0x2222, 0x3333), (0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)):
            calls.clear()
            check(rig.run(address, window, code, handler), 17, 'registration result')
            check(calls, [(tag, code, window, handler)], 'registration register arguments')
    found = [0]
    machine.hook(0x4209E0, lambda current: (calls.append(('find', current.mu.reg_read(UC_X86_REG_ECX))), found[0])[1], 0)
    machine.hook(0x4208E0, lambda current: (calls.append(('create', current.arg(0))), 0)[1], 1)
    machine.hook(0x426720, lambda current: (calls.append(('remove',) + tuple(current.arg(index) for index in range(4))), 31)[1], 4)
    machine.hook(0x426810, lambda current: (calls.append(('clear', current.arg(0), current.arg(1))), 0)[1], 2)
    machine.hook(0x420970, lambda current: (calls.append(('erase', current.mu.reg_read(UC_X86_REG_ECX), current.arg(0))), 0)[1], 1)
    for present in (0, 0x1234):
        found[0] = present
        calls.clear()
        check(rig.run(0x420AB0, 11, 22, 33, regs={'esi': 44}), 31, 'remove result')
        check(calls, [('find', 44)] + ([] if present else [('create', 44)]) + [('remove', 11, 44, 22, 33)], 'remove sequence')
        for active in (44, 55):
            calls.clear()
            machine.w32(0x7792F4, active)
            rig.run(0x420A10, regs={'esi': 44})
            check(machine.r32(0x7792F4), 0 if active == 44 else active, 'clear current window')
            expected = [('find', 44)]
            if present:
                expected += [('clear', tag, 44) for tag in (0x534D5347, 0x534D5348, 0x534D534A, 0x534D5349)] + [('erase', 0x708860, present)]
            check(calls, expected, 'window teardown')
    def dispatch(current):
        """Record dispatch arguments and set synthetic result fields."""
        tag, window, code, record = (current.arg(index) for index in range(4))
        view = current.view('storm_message', record)
        fields = (view.window, view.message, view.wparam, view.lparam, view.notification)
        calls.append(('dispatch', tag, window, code, fields))
        view.result += 1
        view.handled = 0xCAFE
        return 0
    machine.hook(0x426F90, dispatch, 4)
    for message, active, outputs in itertools.product((0x111, 0x100, 0x101, 0x82, 0x200), (0, 77), (False, True)):
        found[0] = 0
        machine.w32(0x7792F4, active)
        out = machine.put(b'\xa5' * 8)
        calls.clear()
        check(rig.run(0x420B00, 77, message, 0xABCD1234, 0x5678, out if outputs else 0, out + 4 if outputs else 0), 1, 'dispatch result')
        fields = (77, message, 0xABCD1234, 0x5678, 0xABCD if message == 0x111 else 0)
        expected = []
        for window in ([77, 0] if active else [77]):
            expected.append(('dispatch', 0x534D5347, window, message, fields))
            if message in (0x111, 0x100, 0x101):
                expected.append(('dispatch', {0x111: 0x534D5348, 0x100: 0x534D5349, 0x101: 0x534D534A}[message], window, 0x1234 if message == 0x111 else 0xABCD1234, fields))
        count = len(expected)
        if message == 0x82:
            expected.append(('find', 77))
        check(calls, expected, 'dispatch order, secondary tags, global replay')
        check(machine.read(out, 8), struct.pack('<II', count, 0xCAFE) if outputs else b'\xa5' * 8, 'dispatch outputs')


def event_levels(exe):
    """Check handler-level teardown, lookup and deep copying."""
    rig = Rig(exe)
    machine = rig.machine
    for count, chain_lengths in ((0, ()), (1, (0,)), (3, (2, 0, 3))):
        buckets = machine.alloc(max(4, count * 4))
        record = machine.alloc(16)
        machine.view('storm_event_level', record).buckets = buckets
        machine.view('storm_event_level', record).count = count
        expected = []
        for index, length in enumerate(chain_lengths):
            previous = 0
            nodes = []
            for _ in range(length):
                node = machine.alloc(16)
                machine.view('storm_handler', node).next = previous
                previous = node
                nodes.insert(0, node)
            machine.w32(buckets + index * 4, previous)
            expected.extend(('free', node, 'delete', 0xFFFFFFFF, 0) for node in nodes)
        rig.events.clear()
        rig.run(0x426210, regs={'edi': record})
        check(rig.events, expected + [('free', buckets, '.\\SOURCE\\SEVT.CPP', 0x58, 0), ('free', record, 'delete', 0xFFFFFFFF, 0)], 'level release order')
        check(machine.read(buckets, count * 4), bytes(count * 4), 'bucket heads drained')
    locks = []
    rig.imported('EnterCriticalSection', lambda current: (locks.append(('enter', current.arg(0))), 0)[1], 1)
    rig.imported('LeaveCriticalSection', lambda current: (locks.append(('leave', current.arg(0))), 0)[1], 1)
    for mode in ('no-table', 'zero-size', 'empty', 'collision-miss', 'collision-hit'):
        buckets = machine.alloc(16)
        node = machine.alloc(20)
        old = machine.alloc(16)
        old_buckets = machine.alloc(8)
        item = machine.put(struct.pack('<IIII', 11, 22, 33, 0))
        machine.w32(old_buckets, item)
        machine.view('storm_event_level', old).buckets = old_buckets
        machine.view('storm_event_level', old).count = 2
        machine.view('storm_event_level', old).state = 99
        tag, window = 0x534D5347, 0x1234
        machine.view('storm_event_node', node).tag = tag
        machine.view('storm_event_node', node).window = window
        machine.view('storm_event_node', node).level = old
        collision = machine.alloc(20)
        machine.view('storm_event_node', collision).tag = tag ^ 1
        machine.view('storm_event_node', collision).next = node if mode == 'collision-hit' else 0
        machine.w32(buckets + ((tag ^ window) & 3) * 4, collision if mode.startswith('collision') else 0)
        machine.w32(0x779300, 0 if mode == 'no-table' else buckets)
        machine.w32(0x779304, 0 if mode == 'zero-size' else 4)
        machine.w32(0x7792F8, 0)
        locks.clear()
        rig.events.clear()
        hit = mode == 'collision-hit'
        check(rig.run(0x426370, tag, window), int(hit), 'push lookup result')
        check(locks, [('enter', 0x77930C), ('leave', 0x77930C)], 'push lock balance')
        check(machine.r32(0x7792F8), int(hit), 'changed flag')
        if hit:
            new = machine.view('storm_event_node', node).level
            new_level = machine.view('storm_event_level', new)
            check((new_level.count, new_level.state, new_level.previous), (2, 99, old), 'copied level and prior link')
            copied_buckets = new_level.buckets
            copied_item = machine.r32(copied_buckets)
            if copied_item == item or copied_buckets == old_buckets:
                raise AssertionError('level copy aliases old storage')
            check(machine.read(copied_item, 16), machine.read(item, 16), 'deep copy of handler node')
            check(machine.r32(copied_buckets + 4), 0, 'empty copied bucket')
            check([(event[1], event[3]) for event in rig.events], [(16, 0x11C), (8, 0x3E), (16, 0x45)], 'copy allocations')
        else:
            check(rig.events, [], 'miss allocates nothing')
            check(machine.view('storm_event_node', node).level, old, 'miss leaves level')


def bignums(exe):
    """Check construction, byte conversion and modular exponentiation."""
    rig = Rig(exe)
    machine = rig.machine
    slot = machine.alloc(4)
    def create(value):
        """Construct a bignum and import a little-endian integer."""
        rig.run(0x423950, slot)
        handle = machine.r32(slot)
        data = value.to_bytes(max(1, (value.bit_length() + 7) // 8), 'little')
        rig.run(0x423850, handle, machine.put(data), len(data))
        return handle
    def export(handle, value, capacity, output_count=True):
        """Check exported bytes, reported size and destination guards."""
        buffer = machine.put(b'\xa5' * (capacity + 8))
        used = machine.put(b'\xcc' * 4)
        rig.run(0x423B30, handle, buffer, capacity, used if output_count else 0)
        data = value.to_bytes((value.bit_length() + 7) // 8, 'little')[:capacity]
        check(machine.read(buffer, capacity + 8), data + b'\xa5' * (capacity + 8 - len(data)), 'bignum LE export and guards')
        check(machine.r32(used), len(data) if output_count else 0xCCCCCCCC, 'export size')
    rig.fail_alloc = True
    machine.w32(slot, 0xFFFFFFFF)
    rig.run(0x423950, slot)
    check(machine.r32(slot), 0, 'allocation failure clears handle')
    rig.fail_alloc = False
    for value in (0, 1, 255, 256, 0x12345678, 2**128 + 17, 2**2048 - 1):
        handle = create(value)
        for capacity in (0, 1, 3, 4, 32, 257):
            export(handle, value, capacity)
        export(handle, value, 257, False)
        rig.run(0x423850, handle, machine.put(b'\0'), 0)
        export(handle, 0, 8)
    rng = random.Random(0x53424947)
    for bits in (8, 32, 128, 256, 512):
        for exponent in (0, 1, 2, 17, 65537):
            modulus = rng.getrandbits(bits) | 3
            base = rng.getrandbits(bits + 8)
            output, number, power, mod = (create(value) for value in (0, base, exponent, modulus))
            rig.run(0x425330, output, number, power, mod)
            export(output, pow(base, exponent, modulus), 257)


def bitmap(exe):
    """Check bitmap validation and routing with decoders intercepted."""
    rig = Rig(exe)
    machine = rig.machine
    calls = []
    for address, count in ((0x6C60F0, 3), (0x6C6990, 6), (0x6C5730, 7)):
        def decoder(current, address=address, count=count):
            """Record the selected decoder and its register and stack arguments."""
            calls.append((address, tuple(current.arg(index) for index in range(count)), tuple(current.mu.reg_read(register) for register in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EDI, UC_X86_REG_ESI))))
            return 123
        machine.hook(address, decoder, count)
    for kind, header, length, has_data, destination, capacity, outputs in itertools.product((0, 1, 2, 3, 4), (b'BM', b'\x0a\x05', b'xx'), (0, 1, 2), (False, True), (0, 0x1234), (0, 12), (False, True)):
        data = machine.put(header)
        out = machine.put(b'\xa5' * 12)
        pointers = tuple(out + offset if outputs else 0 for offset in (0, 4, 8))
        machine.last_error = 0
        calls.clear()
        result = rig.run(0x6C6A80, kind, data if has_data else 0, length, 0x5678, destination, capacity, *pointers)
        invalid = not has_data or not length or (destination != 0 and capacity == 0)
        selected = kind
        if kind == 0 and length >= 2:
            selected = {b'BM': 1, b'\x0a\x05': 2}.get(header, 3)
        dispatch = not invalid and selected in (1, 2, 3)
        check(result, 123 if dispatch else 0, 'bitmap result')
        check(machine.last_error, 0x57 if invalid else 0, 'bitmap invalid parameter')
        check(machine.read(out, 12), bytes(12) if outputs else b'\xa5' * 12, 'bitmap clears optional outputs')
        if not dispatch:
            check(calls, [], 'bitmap rejected without decode')
            continue
        address, args, registers = calls[0]
        check(len(calls), 1, 'one decoder')
        check(address, {1: 0x6C60F0, 2: 0x6C6990, 3: 0x6C5730}[selected], 'decoder selection')
        if selected == 1:
            check(args, (0x5678, destination, capacity), 'BMP stack')
            check(registers[:4], (pointers[0], pointers[1], pointers[2], data), 'BMP register outputs')
        elif selected == 2:
            check(args, (length, 0x5678, destination, capacity, pointers[1], pointers[2]), 'PCX stack')
            check(registers[:2], (pointers[0], data), 'PCX register outputs')
        else:
            check(args, (length, 0x5678, destination, capacity, *pointers), 'third decoder stack')
            check(registers[4], data, 'third decoder ESI input')


def fatal_report(exe):
    """Check error-report suppression and scripted UI decisions."""
    rig = Rig(exe)
    machine = rig.machine
    # Strings/formatting and OS UI are deliberately scripted; test control flow.
    empty = machine.put(b'\0')
    machine.hook(0x41C170, lambda current: empty, 0)
    machine.hook(0x41C430, lambda current: (current.mu.mem_write(current.arg(1), b'error\0'), 1)[1], 3)
    machine.hook(0x41C1A0, lambda current: 0, 0)
    machine.hook(0x41C1E0, lambda current: 0, 0)
    machine.hook(0x41C360, lambda current: 0, 1)
    machine.hook(0x413A40, lambda current: (current.mu.mem_write(current.arg(0), b'callback\0'), 0)[1], 0)
    def format_text(current):
        """Supply a synthetic formatted string for the report."""
        current.mu.mem_write(current.arg(0), b'text\0')
        return 4
    rig.imported('wsprintfA', format_text, 0)
    rig.imported('FindFirstFileA', lambda current: 0, 2)
    rig.imported('GetVersion', lambda current: 0, 0)
    rig.imported('GetCurrentProcess', lambda current: 0x1234, 0)
    calls = []
    reply = [7]
    rig.imported('IsWindow', lambda current: 0, 1)
    rig.imported('MessageBoxA', lambda current: (calls.append(('message', current.arg(0), current.arg(3))), reply[0])[1], 4)
    rig.imported('TerminateProcess', lambda current: (calls.append(('terminate', current.arg(0), current.arg(1))), 1)[1], 2)
    for disabled, busy in ((1, 0), (0, 1), (1, 1)):
        machine.w32(0x7790F0, disabled)
        machine.w32(0x7790E4, busy)
        calls.clear()
        check(rig.run(0x41CAF0, 0, 0, 0, 0, 0, 99), 0, 'fatal suppression return')
        check(calls, [], 'fatal suppression no UI or exit')
        check(machine.r32(0x7790E4), busy, 'suppression preserves busy state')
    machine.w32(0x7790F0, 0)
    for allow_continue, answer in itertools.product((0, 1), (1, 7)):
        machine.w32(0x7790E4, 0)
        machine.w32(0x7790F8, 0)
        machine.w32(0x708428, 0)
        reply[0] = answer
        calls.clear()
        continued = bool(allow_continue and answer == 7)
        check(rig.run(0x41CAF0, 0x85100000, 0, 0, 0, allow_continue, 99), int(continued), 'fatal continuation return')
        check(calls, [('message', 0, 0x52013 if allow_continue else 0x52011)] + ([] if continued else [('terminate', 0x1234, 99)]), 'fatal UI and termination decisions')
        check(machine.r32(0x7790E4), 0, 'fatal busy reset')


def main():
    """Run every group and optional negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--mutation-check', action='store_true', help='require representative deliberately wrong expectations to fail')
    args = parser.parse_args()
    check(hashlib.sha256(args.exe.read_bytes()).hexdigest(), SHA256, 'game.exe 1.14d SHA-256')
    for group in (memory_and_file, events, event_levels, bignums, bitmap, fatal_report):
        group(args.exe)
        print(f'{group.__name__}: PASS', flush=True)
    for address, count in COUNTS.items():
        if count == 0:
            raise AssertionError(f'unexercised target {address:08x}')
        print(f'{address:08x}: {count} cases')
    print(f'PASS: {len(COUNTS)} functions, {sum(COUNTS.values())} calls; stock-only, scoped hooks described in source-module.md')
    if args.mutation_check:
        global MUTATION
        probes = ((memory_and_file, 'fill byte truncation'),
                  (events, 'registration register arguments'),
                  (events, 'dispatch order, secondary tags, global replay'),
                  (event_levels, 'level release order'),
                  (event_levels, 'deep copy of handler node'),
                  (bignums, 'bignum LE export and guards'),
                  (bitmap, 'decoder selection'),
                  (fatal_report, 'fatal UI and termination decisions'))
        for group, label in probes:
            MUTATION = label
            try:
                group(args.exe)
            except AssertionError as error:
                if not str(error).startswith(label + ':'):
                    raise
                print(f'negative control caught: {label}', flush=True)
            else:
                raise AssertionError(f'negative control escaped: {label}')
            finally:
                MUTATION = None


if __name__ == '__main__':
    main()
