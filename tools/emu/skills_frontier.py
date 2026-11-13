# SPDX-License-Identifier: GPL-3.0-or-later
"""Run selected SKILLS frontier predicates and helpers in game.exe 1.14d.

Usage: uv run python skills_frontier.py --exe /path/to/Game.exe [--mutation-check]

Stock code runs under Unicorn with the named downstream functions scripted.
The checks cover return values and ordered interactions, not d2d parity.
No MPQs, network, native game processes or OS input are used.
"""
import argparse
import hashlib
import itertools
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

MUTATION = None
GAME_SHA256 = '631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf'


def equal(actual, expected, label):
    """Reject a mismatch with enough context to reproduce the case."""
    if actual != expected:
        raise AssertionError(f'{label}: {actual!r} != {expected!r}')


class Rig:
    """Bounded machine with scripted game callees and a per-call trace."""

    def __init__(self, exe):
        """Map the original executable and install observable helper stubs."""
        self.machine = emu.Emu(str(exe))
        self.log = []
        self.answers = {}
        self.control_values = (0, 0)
        self.machine.hook(0x58F090, self.control, 1)
        self.descriptor = self.machine.alloc(0x20)
        for addr, nargs in ((0x4638A0, 0), (0x5541B0, 0), (0x645510, 2), (0x56E2F0, 0),
                            (0x46C140, 0), (0x5A0180, 0), (0x554200, 1),
                            (0x58F0D0, 0), (0x58F260, 0), (0x58F160, 0),
                            (0x6633B0, 1), (0x6633F0, 1), (0x5C49E0, 3), (0x5C4470, 4), (0x5C40D0, 0), (0x553540, 2), (0x620BB0, 1), (0x61AB00, 1), (0x620150, 1), (0x646CA0, 4), (0x644AA0, 2), (0x647BC0, 4), (0x647D00, 4), (0x6449F0, 3), (0x45AE20, 0), (0x45ADF0, 0), (0x56EDE0, 5), (0x463740, 1), (0x540E60, 2), (0x5417D0, 4), (0x56F7F0, 5), (0x639DB0, 3), (0x6256B0, 2), (0x6277E0, 2), (0x626CD0, 1), (0x649180, 1), (0x64EC10, 5), (0x648C30, 2), (0x648CE0, 2), (0x648CF0, 2), (0x464820, 0), (0x649F70, 2), (0x6633D0, 1), (0x6212C0, 2), (0x535BC0, 0), (0x63D340, 2)):
            self.machine.hook(addr, self.handler(addr, nargs), nargs)

    def control(self, machine):
        """Supply two scratch outputs and record the AI-control query."""
        ecx = machine.mu.reg_read(UC_X86_REG_ECX)
        machine.w32(machine.mu.reg_read(UC_X86_REG_EDX), self.control_values[1])
        machine.w32(machine.arg(0), self.control_values[0])
        self.log.append((0x58F090, ecx))
        return 0

    def handler(self, addr, nargs):
        """Build a stub recording ECX, EDX and the declared stack args."""
        def record(machine):
            """Record this invocation and return the current scripted value."""
            self.log.append((addr, machine.mu.reg_read(UC_X86_REG_ECX), machine.mu.reg_read(UC_X86_REG_EDX),
                             tuple(machine.arg(index) for index in range(nargs))))
            return self.answers.get(addr, 0)
        return record

    def run(self, addr, *args, ecx=0, edx=0, regs=None):
        """Run a bounded call and verify its return address and stack cleanup."""
        self.log.clear()
        result = self.machine.call(addr, *args, ecx=ecx, edx=edx, regs=regs, limit=100_000)
        equal(self.machine.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, 'return address')
        equal(self.machine.mu.reg_read(UC_X86_REG_ESP), emu.STACK + emu.STACK_SIZE - 0x1000, 'callee stack cleanup')
        return result


def corpse_predicate(rig):
    """FUN_005c3300 requires type 1 and four successful checks in order."""
    e = rig.machine
    unit = e.alloc(0x100)
    view = e.view('unit', unit)
    view['class'] = 123
    calls = (0x4638A0, 0x5541B0, 0x645510, 0x56E2F0)
    cases = 0
    for present, kind, mask in itertools.product((False, True), (0, 1, 2), range(16)):
        view.type = kind
        rig.answers = {address: (mask >> index) & 1 for index, address in enumerate(calls)}
        result = rig.run(0x5C3300, regs={'esi': unit if present else 0})
        expected = []
        if present and kind == 1:
            for index, address in enumerate(calls):
                expected.append(address)
                if not mask & (1 << index):
                    break
        equal(result, int(present and kind == 1 and mask == 15), 'corpse predicate return')
        equal([entry[0] for entry in rig.log], expected, 'corpse predicate order')
        if rig.log:
            equal(rig.log[0][1:3], (123, 8), 'class check arguments')
        cases += 1
    return cases


def flag_predicate(rig):
    """FUN_005c3420 gates a target by type, flags and scripted checks."""
    e = rig.machine
    unit = e.alloc(0x100)
    view = e.view('unit', unit)
    view['class'] = 31
    cases = 0
    for present, kind, flags, mask in itertools.product((False, True), (0, 1, 2), range(16), range(16)):
        view.type, view.flags = kind, flags
        rig.answers = {0x46C140: mask & 1, 0x5A0180: (mask >> 1) & 1,
                       0x5541B0: (mask >> 2) & 1, 0x554200: (mask >> 3) & 1}
        result = rig.run(0x5C3420, unit if present else 0, ecx=0x1234, edx=0x5678)
        expected = []
        if present:
            if kind == 1:
                expected.append(0x46C140)
                if mask & 1:
                    expected.append(0x5A0180)
            gate = kind != 1 or bool(mask & 1) and not bool(mask & 2)
            if gate and (flags & (6 if MUTATION == 'flag-mask' else 14)) == (6 if MUTATION == 'flag-mask' else 14):
                expected.append(0x5541B0)
                if not (mask & 4):
                    expected.append(0x554200)
        wanted = bool(expected and expected[-1] == 0x554200 and mask & 8)
        equal(result, int(wanted), 'flag predicate return')
        equal([entry[0] for entry in rig.log], expected, 'flag predicate order')
        if expected and expected[-1] == 0x554200:
            equal(rig.log[-1][1:], (0x1234, 0x5678, (unit,)), 'final check arguments')
        cases += 1
    return cases


def minion_detach(rig):
    """FUN_0056e580 selects clear, remove or neither from its owner lookup."""
    e = rig.machine
    unit = e.alloc(0x100)
    owner = e.alloc(0x100)
    cases = 0
    for selection, kind in itertools.product(('null', 'self', 'other'), (0, 1, 2)):
        pointer = {'null': 0, 'self': unit, 'other': owner}[selection]
        e.view('unit', pointer or owner).type = kind
        rig.answers = {0x58F0D0: pointer}
        rig.run(0x56E580, ecx=unit)
        wanted = [0x58F0D0]
        if pointer and kind == 1:
            wanted.append(0x58F260 if pointer == unit else 0x58F160)
        equal([entry[0] for entry in rig.log], wanted, 'minion detach')
        if len(wanted) > 1:
            equal(rig.log[-1][1], unit, 'detach ECX')
        cases += 1
    return cases


def sequence_probe(rig):
    """FUN_005cf8c0 repeatedly tests the descriptor's same byte at +5."""
    e = rig.machine
    unit = e.alloc(0x100)
    cases = 0
    for present, byte, length in itertools.product((0, 1), (0, 1, 255), (-1, 0, 1, 2, 8, 50)):
        e.mu.mem_write(rig.descriptor, b'\xa5' * 5 + bytes((byte,)) + b'\0' * 26)
        rig.answers = {0x6633B0: rig.descriptor if present else 0, 0x6633F0: length & 0xFFFFFFFF}
        result = rig.run(0x5CF8C0, ecx=unit)
        wanted = 0xFFFFFFFF if present and byte == 0 and length > 0 and MUTATION != 'sequence-zero' else 7
        equal(result, wanted, 'sequence probe return')
        equal([entry[0] for entry in rig.log], [0x6633B0] + ([0x6633F0] if present else []), 'sequence helper order')
        cases += 1
    return cases


def wrapper_order(rig):
    """FUN_005c50c0 forwards three stack and two register inputs in order."""
    cases = 0
    for first, second, third, ecx, edx in itertools.product((0, 0x11), (0, 0x22), (0, 0x33), (0, 0x44), (0, 0x55)):
        rig.answers = {}
        rig.run(0x5C50C0, first, second, third, ecx=ecx, edx=edx)
        equal([entry[0] for entry in rig.log], [0x5C49E0, 0x5C4470, 0x5C40D0], 'wrapper order')
        equal(rig.log[0][3], (first, 0, third), 'first stack')
        equal(rig.log[1][1:], (ecx, edx, (first, second, third, 0)), 'second arguments')
        equal(rig.log[2][1:3], (edx, first), 'third registers')
        cases += 1
    return cases


def target_room_gate(rig):
    """FUN_005d30f0 requires a target, two clear room checks, and type < 2."""
    machine = rig.machine
    actor, target = machine.alloc(0x100), machine.alloc(0x100)
    calls = 0
    for present, kind, blocked in itertools.product((0, 1), (0, 1, 2, 0xFFFFFFFF), (0, 1)):
        machine.view('unit', target).type = kind
        rig.answers = {0x553540: target if present else 0, 0x620BB0: 0x1234, 0x61AB00: blocked}
        got = rig.run(0x5D30F0, 0, 0, ecx=0x777, edx=actor)
        expected = [0x553540]
        if present:
            expected += [0x620BB0, 0x61AB00]
            if not blocked:
                expected += [0x620BB0, 0x61AB00]
        equal([entry[0] for entry in rig.log], expected, 'target room gate order')
        equal(got, int(bool(present) and not blocked and kind < 2), 'target room gate result')
        equal(rig.log[0][3], (0x777, actor), 'target lookup arguments')
        calls += 1
    return calls


def skill_chain(rig):
    """FUN_006446c0 scans a nonempty linked skill chain by pointer identity."""
    machine = rig.machine
    calls = 0
    for length, match in itertools.product((1, 2, 5), (-1, 0, 1, 4)):
        nodes = [machine.put(bytes(8)) for _ in range(length)]
        for index, node in enumerate(nodes):
            machine.mu.mem_write(node, (0x4000 + index).to_bytes(4, 'little') + (nodes[index + 1] if index + 1 < length else 0).to_bytes(4, 'little'))
        wanted = 0x4000 + match if match >= 0 else 0x8888
        rig.answers = {0x620150: nodes[0]}
        got = rig.run(0x6446C0, regs={'eax': 0x1234, 'esi': wanted})
        equal(got, int(0 <= match < length), 'skill chain result')
        equal([entry[0] for entry in rig.log], [0x620150], 'skill chain call')
        equal(rig.log[0][3], (0x1234,), 'skill chain unit argument')
        calls += 1
    return calls


def event_interval(rig):
    """FUN_0056cd50 clamps a scripted period and rounds the event frame."""
    machine = rig.machine
    game = machine.alloc(0x200)
    row = machine.put(bytes(0x128) + struct.pack('<I', 0xAABBCCDD))
    calls = 0
    for frame, period in itertools.product((0, 1, 4, 5, 6, 10, 11, 47, 1001), (-1, 0, 1, 4, 5, 6, 20)):
        machine.view('game', game).frame = frame
        rig.answers = {0x646CA0: period & 0xFFFFFFFF}
        got = rig.run(0x56CD50, row, 0x22, 0x33, ecx=game, edx=0x44)
        bounded = max(period, 5)
        numerator = frame - 1 + bounded
        expected = (numerator // bounded) * bounded + 1
        equal(got, expected, 'event interval')
        equal([entry[0] for entry in rig.log], [0x646CA0], 'event calc call')
        equal(rig.log[0][3], (0x44, 0xAABBCCDD, 0x22, 0x33), 'event calc arguments')
        calls += 1
    return calls


def stat_bundle(rig):
    """FUN_0056e000 collects three results and arithmetically shifts damage."""
    machine = rig.machine
    result = machine.alloc(20)
    cases = 0
    for minimum, maximum, hit in itertools.product((-513, -256, -1, 0, 255, 256, 513), (-257, 0, 257), (0, 1, 0xFFFFFFFF)):
        machine.mu.mem_write(result, bytes([0xA5]) * 20)
        rig.answers = {0x644AA0: 17, 0x647BC0: minimum & 0xFFFFFFFF, 0x647D00: maximum & 0xFFFFFFFF, 0x6449F0: hit}
        rig.run(0x56E000, result, regs={'edi': 0x111, 'esi': 0x222})
        equal([entry[0] for entry in rig.log], [0x644AA0, 0x647BC0, 0x647D00, 0x6449F0], 'stat bundle call order')
        equal(rig.log[0][3], (0x111, 0x222), 'stat bundle level args')
        equal(rig.log[1][3], (0x111, 0x222, 17, 1), 'minimum args')
        equal(rig.log[2][3], (0x111, 0x222, 17, 1), 'maximum args')
        equal(rig.log[3][3], (0x111, 0x222, 17), 'to-hit args')
        equal(machine.read(result, 20), struct.pack('<III', (minimum >> 8) & 0xFFFFFFFF, (maximum >> 8) & 0xFFFFFFFF, hit) + b'\xa5' * 8, 'stat bundle result')
        cases += 1
    return cases


def control_predicate(rig):
    """FUN_005d8510 gates an AI-control pair behind three checks."""
    machine = rig.machine
    unit = machine.alloc(0x100)
    cases = 0
    for present, kind, alive, refused, first, second in itertools.product((0, 1), (0, 1, 2), (0, 1), (0, 1), (0, 1), (0, 0xFFFFFFFF)):
        machine.view('unit', unit).type = kind
        rig.answers = {0x56E2F0: alive, 0x5541B0: refused}
        rig.control_values = (first, second)
        got = rig.run(0x5D8510, edx=unit if present else 0)
        expected = []
        if present and kind == 1:
            expected.append(0x56E2F0)
            if alive:
                expected.append(0x5541B0)
                if not refused:
                    expected.append(0x58F090)
        equal([entry[0] for entry in rig.log], expected, 'control predicate order')
        equal(got, int(bool(expected) and expected[-1] == 0x58F090 and (first != 0 or second == 0xFFFFFFFF)), 'control predicate result')
        if expected:
            equal(rig.log[0][1:3], (unit, 12), 'control gate args')
        cases += 1
    return cases


def paired_room_gate(rig):
    """FUN_005ca470 passes two nonnull, unrestricted units to a final helper."""
    machine = rig.machine
    actor, target = machine.alloc(0x100), machine.alloc(0x100)
    blocked = set()
    def room(current):
        """Use each unit pointer as its synthetic room and record the lookup."""
        unit = current.arg(0)
        rig.log.append((0x620BB0, unit))
        return unit
    def unavailable(current):
        """Block only the selected synthetic rooms."""
        unit = current.arg(0)
        rig.log.append((0x61AB00, unit))
        return int(unit in blocked)
    machine.hook(0x620BB0, room, 1)
    machine.hook(0x61AB00, unavailable, 1)
    cases = 0
    for actor_present, target_present, first_blocked, second_blocked in itertools.product((0, 1), repeat=4):
        blocked.clear()
        if first_blocked:
            blocked.add(actor)
        if second_blocked:
            blocked.add(target)
        rig.answers = {0x45AE20: 0x123, 0x45ADF0: 0x456, 0x56EDE0: 0x999}
        got = rig.run(0x5CA470, 0x11, 0x22, 0x33, 0x44, regs={'edi': actor if actor_present else 0, 'esi': target if target_present else 0})
        expected = []
        if actor_present and target_present:
            expected += [0x620BB0, 0x61AB00]
            if not first_blocked:
                expected += [0x620BB0, 0x61AB00]
                if not second_blocked:
                    expected += [0x45AE20, 0x45ADF0, 0x56EDE0]
        equal([entry[0] for entry in rig.log], expected, 'paired room gate order')
        succeeds = bool(actor_present and target_present and not first_blocked and not second_blocked)
        equal(got, 0x999 if succeeds else 0, 'paired room gate result')
        if succeeds:
            equal(rig.log[-1][1:], (0x11, actor, (0x33, 0x44, 0x22, 0x456, 0x123)), 'paired room final args')
        cases += 1
    return cases


def skill_room_gate(rig):
    """FUN_005d9ce0 validates its row and room pointers before a mask gate."""
    machine = rig.machine
    row = machine.alloc(0x23C)
    table_data = bytearray(0xBA4)
    struct.pack_into('<I', table_data, 0xB98, row)
    struct.pack_into('<I', table_data, 0xBA0, 1)
    tables = machine.put(table_data)
    machine.w32(0x744304, tables)
    cases = 0
    for ident, mask, first_room, second_room, blocked in itertools.product((-1, 0, 1), (0, 1), (0, 1), (0, 1), (0, 1)):
        raw = bytearray(0x23C)
        raw[5] = mask
        machine.mu.mem_write(row, bytes(raw))
        rig.answers = {0x620BB0: first_room, 0x463740: second_room, 0x61AB00: blocked}
        got = rig.run(0x5D9CE0, 0x11, 0x22, 0x33, regs={'eax': ident & 0xFFFFFFFF})
        expected = []
        if ident == 0:
            expected.append(0x620BB0)
            if first_room:
                expected.append(0x463740)
                if second_room and not mask:
                    expected.append(0x61AB00)
        equal([entry[0] for entry in rig.log], expected, 'skill room call order')
        wanted = ident == 0 and first_room and second_room and (mask or not blocked)
        equal(got, int(bool(wanted)), 'skill room result')
        if len(expected) > 1:
            equal(rig.log[1][1:], (first_room, 0x22, (0x33,)), 'room lookup args')
        cases += 1
    return cases


def item_timed_state(rig):
    """FUN_005bf510/005bf5d0 validate rows then schedule or remove state."""
    machine = rig.machine
    row = machine.alloc(0x23C)
    table_data = bytearray(0xBA4)
    struct.pack_into('<I', table_data, 0xB98, row)
    struct.pack_into('<I', table_data, 0xBA0, 1)
    struct.pack_into('<I', table_data, 0xC4, 3)
    machine.w32(0x744304, machine.put(table_data))
    machine.hook(0x56CD50, rig.handler(0x56CD50, 3), 3)
    cases = 0
    for present, ident, enabled, state, repeat, has_list in itertools.product((0, 1), (-1, 0, 1), (0, 1), (-1, 0, 2, 3), (0, 1), (0, 1)):
        raw = bytearray(0x23C)
        raw[4], raw[5] = 0x20 if enabled else 0, 0x80 if repeat else 0
        struct.pack_into('<h', raw, 0x80, state)
        machine.mu.mem_write(row, bytes(raw))
        rig.answers = {0x56CD50: 0x1234, 0x6256B0: 0x8888 if has_list else 0}
        valid = bool(present and ident == 0 and enabled and 0 <= state < 3)
        rig.run(0x5BF510, 0x33, ident & 0xFFFFFFFF, 0x44, ecx=0x11, edx=0x22 if present else 0)
        expected = [0x540E60, 0x56CD50, 0x5417D0] + ([0x56F7F0] if repeat else []) if valid else []
        equal([entry[0] for entry in rig.log], expected, 'item timed schedule order')
        if valid:
            equal(rig.log[0][1:], (0x11, 0x22, (9, 0x33)), 'event remove args')
            equal(rig.log[1][1:], (0x11, 0x22, (row, 0, 0x44)), 'period args')
            equal(rig.log[2][1:], (0x11, 0x22, (9, 0x1234, 0x33, 0)), 'event schedule args')
            if repeat:
                equal(rig.log[3][1:], (0x11, 0x22, (0, 0x44, 1, 1, 0)), 'repeat callback args')
        rig.run(0x5BF5D0, 0x33, ident & 0xFFFFFFFF, ecx=0x11, edx=0x22 if present else 0)
        expected = ([0x639DB0, 0x6256B0] + ([0x6277E0, 0x626CD0] if has_list else []) + [0x540E60]) if valid else []
        equal([entry[0] for entry in rig.log], expected, 'item timed removal order')
        if valid:
            equal(rig.log[0][3], (0x22, state, 0), 'clear state args')
            equal(rig.log[1][3], (0x22, state), 'stat list args')
            equal(rig.log[-1][1:], (0x11, 0x22, (9, 0x33)), 'remove event args')
        cases += 2
    return cases


def aura_search(rig):
    """FUN_0056bbc0 passes a position and search context to the aura walker."""
    machine = rig.machine
    unit, path = machine.alloc(0x100), machine.alloc(0x100)
    calls = []
    def walk(current):
        """Capture the synthetic search context, then supply the result."""
        context = current.arg(5)
        calls.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      tuple(current.arg(index) for index in range(9)), current.read(context, 20)))
        current.w32(context, 0x88776655)
        return 0
    machine.hook(0x56B7E0, walk, 9)
    count = 0
    for kind, has_path in itertools.product((0, 1, 2, 3, 4, 5), (0, 1)):
        if kind in (2, 4, 5) and not has_path:
            continue
        view = machine.view('unit', unit)
        view.type, view.path = kind, path if has_path else 0
        if kind in (2, 4, 5):
            machine.view('static_path', path).x = 123
            machine.view('static_path', path).y = 456
        else:
            machine.view('path', path).x = 123
            machine.view('path', path).y = 456
        calls.clear()
        got = rig.run(0x56BBC0, 0x55, 0x66, ecx=0x77, edx=unit)
        equal(got, 0x88776655, 'aura search result')
        equal(len(calls), 1, 'aura search calls')
        ecx, edx, args, context = calls[0]
        equal((ecx, edx), (0x77, unit), 'aura search registers')
        equal(args[:5], (0, 0, 0x55, 0x8783, 0x56BB20), 'aura search prefix')
        equal((args[6], args[8]), (0, 0x2B8), 'aura search suffix')
        equal(machine.cstr(args[7]), '.\\SKILLS\\Skills.cpp', 'aura search source')
        position = (123, 456) if has_path else (0, 0)
        equal(struct.unpack('<IIIII', context), (0, 0x7FFFFFFF, *position, 0x66), 'aura search context')
        count += 1
    return count


def timed_monster_state(rig):
    """FUN_005c8c10 derives an event delay from optional monster rows."""
    machine = rig.machine
    game, unit = machine.alloc(0x200), machine.alloc(0x100)
    monster2 = machine.alloc(0x134)
    monster = machine.alloc(0x1A8)
    table = bytearray(0xA9C)
    struct.pack_into('<I', table, 0xA78, monster)
    struct.pack_into('<I', table, 0xA80, 1)
    struct.pack_into('<I', table, 0xA90, monster2)
    struct.pack_into('<I', table, 0xA98, 1)
    machine.w32(0x744304, machine.put(table))
    machine.view('game', game).frame = 100
    cases = 0
    for present, cls, row, delay in itertools.product((0, 1), (-1, 0, 1), (-1, 0, 1), (0, 1, 255)):
        machine.view('unit', unit)['class'] = cls & 0xFFFFFFFF
        raw = bytearray(0x1A8)
        struct.pack_into('<h', raw, 0x18, row)
        machine.mu.mem_write(monster, bytes(raw))
        raw2 = bytearray(0x134)
        raw2[0x108] = delay
        machine.mu.mem_write(monster2, bytes(raw2))
        rig.answers = {}
        rig.run(0x5C8C10, regs={'edi': game, 'esi': unit if present else 0})
        equal([entry[0] for entry in rig.log], [0x639DB0, 0x540E60, 0x5417D0], 'monster state calls')
        expected_delay = delay if present and cls == 0 and row == 0 else (0 if MUTATION == 'monster-delay' else 1)
        equal(rig.log[0][3], (unit if present else 0, 12, 0), 'monster state clear')
        equal(rig.log[1][1:], (game, unit if present else 0, (0, 0)), 'monster state event removal')
        equal(rig.log[2][1:], (game, unit if present else 0, (1, 100 + expected_delay, 0, 0)), 'monster state scheduling')
        cases += 1
    return cases


def path_reconfigure(rig):
    """FUN_004c8890 configures collision and path modes by unit type."""
    machine = rig.machine
    unit, path = machine.alloc(0x100), machine.alloc(0x100)
    machine.hook(0x620BB0, rig.handler(0x620BB0, 1), 1)
    cases = 0
    for kind, path_present, room_present, x, y, extra in itertools.product((0, 1, 2), (0, 1), (0, 1), (0, 0x11), (0, 0x22), (0, 1)):
        view = machine.view('unit', unit)
        view.type, view.path = kind, path if path_present else 0
        rig.answers = {0x620BB0: 0x33 if room_present else 0, 0x649180: 0x44, 0x464820: extra}
        rig.run(0x4C8890, y, regs={'esi': unit, 'ebx': x})
        expected = [0x620BB0]
        if path_present and room_present:
            if x and y:
                expected += [0x649180, 0x64EC10]
            expected += [0x648C30, 0x648CE0, 0x648CF0, 0x464820]
            if extra:
                expected += [0x649F70]
        equal([entry[0] for entry in rig.log], expected, 'path reconfigure order')
        if path_present and room_present:
            mask, flags, mode = (0x100, 0x3C01, 2) if kind == 1 else (0x80, 0x1C09, 7)
            operations = {entry[0]: entry for entry in rig.log}
            equal(operations[0x648C30][3], (path, mask), 'path collision')
            equal(operations[0x648CE0][3], (path, flags), 'path mask')
            equal(operations[0x648CF0][3], (path, mode), 'path mode')
            if x and y:
                equal(operations[0x64EC10][3], (0x33, x, y, 0x44, mask), 'path collision coordinates')
        cases += 1
    return cases


def monster_mode_fallback(rig):
    """FUN_00645190 resolves a table mode through its supported-bit fallback."""
    machine = rig.machine
    monster, monster2 = machine.alloc(0x1A8), machine.alloc(0x134)
    table = bytearray(0xA9C)
    struct.pack_into('<I', table, 0xA78, monster)
    struct.pack_into('<I', table, 0xA80, 1)
    struct.pack_into('<I', table, 0xA90, monster2)
    struct.pack_into('<I', table, 0xA98, 1)
    machine.w32(0x744304, machine.put(table))
    first = bytearray(0x1A8)
    struct.pack_into('<h', first, 0x18, 0)
    machine.mu.mem_write(monster, bytes(first))
    modes = [0, 1, 2, 15, 3, 1, 2, 4, 5, 6, 7, 4, 11, 8, 9, 10, 11, 12, 14, 13, 1]
    equal([machine.r32(0x6EB348 + 4 * index) for index in range(len(modes))], modes, 'initial mode table')
    cases = 0
    for selector, cls, bits in itertools.product(range(len(modes)), (-1, 0, 1), (0, 1, 2, 4, 8, 0xFFFF)):
        second = bytearray(0x134)
        struct.pack_into('<H', second, 0xF0, bits)
        machine.mu.mem_write(monster2, bytes(second))
        mode = modes[selector]
        while not (cls == 0 and bits & (1 << mode)) and mode != 1:
            mode = {5: 4, 7: 4, 6: 3, 9: 8, 10: 8, 11: 8, 15: 2}.get(mode, 1)
        got = rig.run(0x645190, selector, cls & 0xFFFFFFFF)
        equal(got, mode, 'monster mode fallback')
        cases += 1
    return cases


def skill_requirements(rig):
    """FUN_00644920 short-circuits level and four signed stat requirements."""
    machine = rig.machine
    row = machine.alloc(0x23C)
    table = bytearray(0xBA4)
    struct.pack_into('<I', table, 0xB98, row)
    struct.pack_into('<I', table, 0xBA0, 1)
    machine.w32(0x744304, machine.put(table))
    stats = {}
    def stat(current):
        """Return a selected unit stat and record its exact stack arguments."""
        args = tuple(current.arg(index) for index in range(3))
        rig.log.append((0x625480, args))
        return stats[args[1]]
    machine.hook(0x625480, stat, 3)
    machine.hook(0x644750, rig.handler(0x644750, 2), 2)
    cases = 0
    stat_order = (0, 2, 1, 3)
    for ident, enabled, threshold, failed in itertools.product((-1, 0, 1), (0, 1), (-1, 0, 4), range(6)):
        raw = bytearray(0x23C)
        raw[5] = 4 if enabled else 0
        for index in range(4):
            struct.pack_into('<h', raw, 0x176 + 2 * index, threshold)
        machine.mu.mem_write(row, bytes(raw))
        stats.clear()
        stats.update({12: 4, 0: threshold, 2: threshold, 1: threshold, 3: threshold})
        if failed >= 2:
            stats[stat_order[failed - 2]] = threshold - 1
        rig.answers = {0x644750: 5 if failed == 1 else 4}
        got = rig.run(0x644920, 0x777, ident & 0xFFFFFFFF)
        valid = ident == 0 and enabled
        expected = []
        if valid:
            expected = [0x625480, 0x644750]
            if failed != 1:
                for index, stat_id in enumerate(stat_order):
                    expected.append(0x625480)
                    if failed == index + 2:
                        break
        equal([entry[0] for entry in rig.log], expected, 'skill requirements call order')
        equal(got, int(valid and failed == 0), 'skill requirements result')
        stat_calls = [entry[1][1] for entry in rig.log if entry[0] == 0x625480]
        equal(stat_calls, [12] + list(stat_order[:len(stat_calls) - 1]) if valid else [], 'skill stat order')
        cases += 1
    return cases


def current_frame_probe(rig):
    """FUN_005cdd30 repeatedly inspects the current frame, never later ones."""
    machine = rig.machine
    frame_data = machine.alloc(0x30)
    raw_unit = bytearray(0x100)
    struct.pack_into('<I', raw_unit, 0x50, frame_data)
    unit = machine.put(raw_unit)
    sequence = machine.alloc(0x20)
    calls = []
    selected = [0]
    def sequence_event(current):
        """Record the requested sequence frame and return a scripted event."""
        args = tuple(current.arg(index) for index in range(3))
        calls.append(args[:2])
        current.w32(args[2], selected[0])
        return 0
    machine.hook(0x6634C0, sequence_event, 3)
    cases = 0
    for has_sequence, current, end, event in itertools.product((0, 1), (0, 1, 3), (0, 1, 2, 4), (0, 7)):
        view = machine.view('unit', unit)
        view.anim_pos, view.anim_length, view.seq_pos = current << 8, end << 8, current << 8
        view.seq = sequence
        frame = bytearray(0x30)
        struct.pack_into('<I', frame, 8, 4)
        frame[0x10:0x14] = bytes((7 if event == 0 else 0,)) * 4
        frame[0x10 + current] = event
        machine.mu.mem_write(frame_data, bytes(frame))
        selected[0] = event
        calls.clear()
        rig.answers = {0x6633B0: rig.descriptor if has_sequence else 0, 0x6633D0: end << 8}
        got = rig.run(0x5CDD30, ecx=unit)
        iterations = max(0, end - current - 1)
        expected = event if iterations else (event if MUTATION == 'frame-edge' else 0)
        equal(got, expected, 'current frame result')
        if has_sequence:
            count = 0 if not iterations else (1 if event else iterations)
            equal(calls, [(sequence, current << 8)] * count, 'repeated sequence query')
        else:
            equal(calls, [], 'non-sequence skips sequence query')
        cases += 1
    return cases


def animation_length(rig):
    """FUN_005cc440 sets frame bounds according to unit type and game mode."""
    machine = rig.machine
    unit, monster, monster2 = machine.alloc(0x100), machine.alloc(0x1A8), machine.alloc(0x134)
    table = bytearray(0xA9C)
    struct.pack_into('<I', table, 0xA78, monster)
    struct.pack_into('<I', table, 0xA80, 1)
    struct.pack_into('<I', table, 0xA90, monster2)
    struct.pack_into('<I', table, 0xA98, 1)
    machine.w32(0x744304, machine.put(table))
    cases = 0
    for kind, cls, row, game_mode, frames in itertools.product((0, 1, 2), (-1, 0, 1), (-1, 0, 1), (0, 14), (0, 1, 255)):
        view = machine.view('unit', unit)
        view.type, view['class'], view.anim_pos, view.anim_length = kind, cls & 0xFFFFFFFF, 0xAABBCCDD, 0x11223344
        raw = bytearray(0x1A8)
        struct.pack_into('<h', raw, 0x18, row)
        machine.mu.mem_write(monster, bytes(raw))
        raw2 = bytearray(0x134)
        raw2[0x109] = frames
        machine.mu.mem_write(monster2, bytes(raw2))
        game_data = bytearray(0x20)
        game_data[0x11] = game_mode
        game = machine.put(game_data)
        rig.answers = {}
        rig.run(0x5CC440, ecx=game, edx=unit)
        expected = []
        position, length = 0xAABBCCDD, 0x11223344
        if kind != 1:
            expected = [0x6212C0]
            length = 0x500
        elif cls == 0 and row == 0:
            if game_mode == 14:
                expected = [0x6212C0]
                length = (frames + 1) << 8
            else:
                position = frames << 8
        equal([entry[0] for entry in rig.log], expected, 'animation length calls')
        equal((view.anim_pos, view.anim_length), (position, length), 'animation length fields')
        if expected:
            equal(rig.log[0][3], (unit, 8 if kind != 1 else (frames - 1) & 0xFFFFFFFF), 'animation setter args')
        cases += 1
    return cases


def weapon_damage_roll(rig):
    """FUN_005cfd20 chooses stat bounds and advances the unit's two-word seed."""
    machine = rig.machine
    unit = machine.alloc(0x100)
    view = machine.view('unit', unit)
    view.inventory = 0x1221
    stats = {}
    def stat(current):
        """Return the selected synthetic stat and capture the requested ID."""
        args = tuple(current.arg(index) for index in range(3))
        rig.log.append((0x625480, args))
        return stats[args[1]] & 0xFFFFFFFF
    machine.hook(0x625480, stat, 3)
    cases = 0
    for weapon, category, lower, upper, low, high in itertools.product((0, 1), (0, 2), (-2, 0, 1, 3), (0, 1, 2, 5), (0, 1, 0xFFFFFFFF), (0, 666)):
        view.seed_low, view.seed_high = low, high
        stats.clear()
        stats.update({0x15: lower, 0x16: upper, 0x17: lower + 1, 0x18: upper + 1})
        rig.answers = {0x535BC0: weapon, 0x63D340: category}
        got = rig.run(0x5CFD20, regs={'esi': unit})
        ids = (0x17, 0x18) if weapon and category == 2 else (0x15, 0x16)
        minimum, maximum = stats[ids[0]], stats[ids[1]]
        if not weapon:
            minimum, maximum = max(minimum, 1), max(maximum, 2)
        minimum, maximum = max(minimum * 256, 256), maximum * 256
        maximum = max(maximum, minimum + 256)
        seed = low * 0x6AC690C5 + high
        equal(got, minimum + (seed & 0xFFFFFFFF) % (maximum - minimum) + (1 if MUTATION == 'damage-roll' else 0), 'damage roll')
        equal((view.seed_low, view.seed_high), (seed & 0xFFFFFFFF, seed >> 32), 'damage seed')
        expected = [0x535BC0] + ([0x63D340] if weapon else []) + [0x625480, 0x625480]
        equal([entry[0] for entry in rig.log], expected, 'damage roll calls')
        equal(tuple(entry[1][1] for entry in rig.log if entry[0] == 0x625480), ids, 'damage stat ids')
        cases += 1
    return cases


def reflected_damage(rig):
    """FUN_005d10c0 gates damage, applies reduction and constructs a record."""
    machine = rig.machine
    target, source = machine.alloc(0x100), machine.alloc(0x100)
    seed_context = machine.put(bytes(8) + struct.pack('<I', 0x12345678))
    amount = [0]
    captured = []
    def stat(current):
        """Return the synthetic stat 0x83 and record its stack arguments."""
        args = tuple(current.arg(index) for index in range(3))
        rig.log.append((0x625480, args))
        return amount[0] & 0xFFFFFFFF
    def apply(current):
        """Capture the damage record passed to the first damage helper."""
        args = tuple(current.arg(index) for index in range(3))
        rig.log.append((0x57C6C0, args))
        captured.append(current.read(args[2], 0x70))
        return 0
    machine.hook(0x625480, stat, 3)
    machine.hook(0x63EE90, rig.handler(0x63EE90, 1), 1)
    machine.hook(0x483360, rig.handler(0x483360, 1), 1)
    machine.hook(0x57C6C0, apply, 3)
    machine.hook(0x57CEE0, rig.handler(0x57CEE0, 2), 2)
    cases = 0
    for present, kind, stat_value, eligible in itertools.product((0, 1), (0, 1, 2), (-1, 0, 1, 3, 4, 7, 8, 12, 100), (0, 1)):
        machine.view('unit', target).type = kind
        amount[0] = stat_value
        captured.clear()
        rig.answers = {0x63EE90: eligible, 0x483360: 0x777}
        rig.run(0x5D10C0, source, seed_context, ecx=0x11, edx=target if present else 0)
        expected = [0x625480]
        reduced = bool((present and kind == 0) or (not (present and kind == 0) and eligible))
        if stat_value > 0 and not (present and kind == 0):
            expected.append(0x63EE90)
        effective = (stat_value + 4) // 8 if reduced and stat_value > 0 else stat_value
        if effective > 0:
            expected += [0x483360, 0x57C6C0, 0x57CEE0]
        equal([entry[0] for entry in rig.log], expected, 'reflected damage calls')
        if effective > 0:
            random_call = next(entry for entry in rig.log if entry[0] == 0x483360)
            equal(random_call[1:], (0x12345678, effective, (100,)), 'damage roll args')
            expected_payload = bytearray(0x70)
            struct.pack_into('<H', expected_payload, 4, 0x4021)
            struct.pack_into('<I', expected_payload, 8, 0x777)
            struct.pack_into('<I', expected_payload, 0x60, 0x8D)
            equal(captured, [bytes(expected_payload)], 'damage record')
        cases += 1
    return cases


def state_timers(rig):
    """FUN_005cec90 reschedules eligible state timers and reports found lists."""
    machine = rig.machine
    game, unit = machine.alloc(0x200), machine.alloc(0x100)
    machine.view('game', game).frame = 100
    states = machine.put(struct.pack('<hh', 3, 4))
    table = bytearray(0x180)
    struct.pack_into('<I', table, 0x174, states)
    struct.pack_into('<h', table, 0x178, 2)
    machine.w32(0x744304, machine.put(table))
    selected = [0]
    list_enabled = [0]
    property_enabled = [0]
    def present(current):
        """Report whether this state is marked in the selected mask."""
        args = tuple(current.arg(index) for index in range(2))
        rig.log.append((0x639DF0, args))
        return int(bool(selected[0] & (1 << (args[1] - 2))))
    def get_list(current):
        """Return a distinct synthetic list for each state when enabled."""
        args = tuple(current.arg(index) for index in range(2))
        rig.log.append((0x6256B0, args))
        return 0x1000 + args[1] if list_enabled[0] else 0
    def prop(current):
        """Report whether a non-special state has the reschedule property."""
        state = current.arg(0)
        rig.log.append((0x63A460, state))
        return property_enabled[0]
    machine.hook(0x639DF0, present, 2)
    machine.hook(0x6256B0, get_list, 2)
    machine.hook(0x63A460, prop, 1)
    machine.hook(0x626090, rig.handler(0x626090, 1), 1)
    machine.hook(0x483360, rig.handler(0x483360, 1), 1)
    machine.hook(0x6260B0, rig.handler(0x6260B0, 2), 2)
    cases = 0
    for mask, enabled, prop_enabled in itertools.product(range(8), (0, 1), (0, 1)):
        selected[0], list_enabled[0], property_enabled[0] = mask, enabled, prop_enabled
        rig.answers = {0x626090: 150, 0x483360: 7}
        got = rig.run(0x5CEC90, game, unit, 0x555)
        expected = []
        for state in (2, 3, 4):
            expected.append(0x639DF0)
            if mask & (1 << (state - 2)):
                expected.append(0x6256B0)
                if enabled:
                    if state != 2:
                        expected.append(0x63A460)
                    if state == 2 or prop_enabled:
                        expected += [0x626090, 0x483360, 0x6260B0, 0x5417D0]
        equal([entry[0] for entry in rig.log], expected, 'state timer call order')
        equal(got, int(bool(mask and enabled and (prop_enabled or MUTATION != 'timer-found'))), 'state timer found result')
        for entry in rig.log:
            if entry[0] == 0x483360:
                equal(entry[1:], (50, 0x555, (100,)), 'state timer random args')
            if entry[0] == 0x5417D0:
                equal(entry[1:], (game, unit, (12, 107, 0, 0)), 'state timer schedule args')
        cases += 1
    return cases


def missile_request(rig):
    """FUN_004c57b0 builds a request, using a fallback for a missing pair."""
    machine = rig.machine
    unit, path = machine.alloc(0x100), machine.alloc(0x100)
    fallback = [0, 0x44, 0x55]
    captured = []
    def select(current):
        """Supply the fallback pair through EDX and the stack pointer."""
        first = current.mu.reg_read(UC_X86_REG_EDX)
        second = current.arg(0)
        rig.log.append((0x4C52E0, current.mu.reg_read(UC_X86_REG_ECX)))
        current.w32(first, fallback[1])
        current.w32(second, fallback[2])
        return fallback[0]
    def spawn(current):
        """Capture the constructed request before returning a marker."""
        pointer = current.mu.reg_read(UC_X86_REG_ECX)
        rig.log.append((0x4CD540,))
        captured.append(current.read(pointer, 0x5C))
        return 0xA123
    machine.hook(0x4C52E0, select, 1)
    machine.hook(0x4CD540, spawn, 0)
    cases = 0
    for kind, has_path, first, second, fallback_ok in itertools.product((0, 1, 2, 4, 5), (0, 1), (0, 11), (0, 22), (0, 1)):
        if kind in (2, 4, 5) and not has_path:
            continue
        view = machine.view('unit', unit)
        view.type, view.path = kind, path if has_path else 0
        if kind in (2, 4, 5):
            machine.view('static_path', path).x = 123
            machine.view('static_path', path).y = 456
        else:
            machine.view('path', path).x = 123
            machine.view('path', path).y = 456
        fallback[0] = fallback_ok
        captured.clear()
        rig.answers = {}
        got = rig.run(0x4C57B0, 0x33, 0x55, 7, 8, first, second, ecx=0x77, edx=unit)
        need_fallback = first == 0 or second == 0
        succeeds = not need_fallback or fallback_ok
        expected = ([0x4C52E0] if need_fallback else []) + ([0x4CD540] if succeeds else [])
        equal([entry[0] for entry in rig.log], expected, 'missile request calls')
        equal(got, 0xA123 if succeeds else 0, 'missile request result')
        if succeeds:
            data = captured[0]
            expected_data = bytearray(0x5C)
            for offset, value in ((0, 0x21), (4, unit), (0x10, 0x77), (0x14, (123 if has_path else 0) + 7),
                                  (0x18, (456 if has_path else 0) + 8), (0x1C, fallback[1] if need_fallback else first),
                                  (0x20, fallback[2] if need_fallback else second), (0x2C, 0x33), (0x30, 0x55)):
                struct.pack_into('<I', expected_data, offset, value)
            equal(data, bytes(expected_data), 'missile request record')
        cases += 1
    return cases


def skill_effect_request(rig):
    """FUN_005c39c0 builds stat effects for row 0x47 and dispatches them."""
    machine = rig.machine
    game, unit = machine.alloc(0x200), machine.alloc(0x100)
    machine.view('game', game).difficulty = 1
    skill_rows = machine.alloc(0x48 * 0x23C)
    skill_row = skill_rows + 0x47 * 0x23C
    stats = machine.alloc(6 * 0x144)
    difficulty = machine.put(bytes(0x1C) + struct.pack('<I', 2))
    table = bytearray(0xBD8)
    struct.pack_into('<I', table, 0xB98, skill_rows)
    struct.pack_into('<I', table, 0xBCC, stats)
    struct.pack_into('<I', table, 0xBD4, 6)
    tables = machine.put(table)
    machine.w32(0x744304, tables)
    captured = []
    def calc(current):
        """Return each synthetic calc ID as its value and log all inputs."""
        args = tuple(current.arg(index) for index in range(4))
        rig.log.append((0x646CA0, args))
        return args[1]
    def execute(current):
        """Capture the request delivered through EDX and its other argument."""
        pointer = current.mu.reg_read(UC_X86_REG_EDX)
        captured.append((current.mu.reg_read(UC_X86_REG_ECX), current.read(pointer, 0x68)))
        rig.log.append((0x5C35C0,))
        return 0
    machine.hook(0x646CA0, calc, 4)
    machine.hook(0x611D30, rig.handler(0x611D30, 1), 1)
    machine.hook(0x5C35C0, execute, 0)
    cases = 0
    for present, count, flagged, divide in itertools.product((0, 1), (0, 1, 3, 6), (0, 1), (0, 1)):
        machine.w32(tables + 0xBA0, 0x48 if present else 0x47)
        raw = bytearray(0x23C)
        struct.pack_into('<I', raw, 0x60, 16)
        struct.pack_into('<h', raw, 0x82, -7)
        for index in range(6):
            struct.pack_into('<h', raw, 0x54 + 2 * index, index if index < count else -1)
            struct.pack_into('<I', raw, 0x68 + 4 * index, 100 + index)
        machine.mu.mem_write(skill_row, bytes(raw))
        raw_stats = bytearray(6 * 0x144)
        if count and flagged:
            raw_stats[5] = 2
        machine.mu.mem_write(stats, bytes(raw_stats))
        rig.answers = {0x611D30: difficulty if divide else 0}
        captured.clear()
        rig.run(0x5C39C0, 0x66, 0x77, ecx=game, edx=unit)
        if not present:
            equal(rig.log, [], 'missing skill row')
        else:
            expected = [0x646CA0, 0x611D30] + [0x646CA0] * count + [0x5C35C0]
            equal([entry[0] for entry in rig.log], expected, 'effect request call order')
            equal(rig.log[0][1], (unit, 16, 0x47, 0x77), 'effect base calc args')
            equal(len(captured), 1, 'effect dispatch count')
            context, request = captured[0]
            equal(context, 0x66, 'effect dispatch ECX')
            equal(struct.unpack_from('<7I', request), (game, unit, 1, int(bool(count and flagged)), 0x47, 0x77, 8 if divide else 16), 'effect request header')
            for index in range(count):
                equal(struct.unpack_from('<I', request, 0x1C + 4 * index)[0], index, 'effect stat id')
                equal(struct.unpack_from('<I', request, 0x34 + 4 * index)[0], 100 + index, 'effect stat calc')
            if count < 6:
                equal(struct.unpack_from('<I', request, 0x1C + 4 * count)[0], 0xFFFFFFFF, 'effect stat sentinel')
            equal(struct.unpack_from('<i', request, 0x4C)[0], -7, 'effect trailing parameter')
        cases += 1
    return cases


def item_creation_request(rig):
    """FUN_0056dab0 builds an item request after class and destination gates."""
    machine = rig.machine
    game = machine.put(bytes(0x78) + struct.pack('<H', 0x3322) + bytes(0x80))
    unit = machine.alloc(0x100)
    requested = []
    def position(current):
        """Supply a synthetic position structure for the placement helper."""
        args = tuple(current.arg(index) for index in range(2))
        rig.log.append((0x620870, args))
        current.mu.mem_write(args[1], struct.pack('<IIII', 0, 0, 123, 456))
        return 0
    def create(current):
        """Capture the item creation request delivered through EDX."""
        request = current.mu.reg_read(UC_X86_REG_EDX)
        requested.append((current.mu.reg_read(UC_X86_REG_ECX), current.arg(0), current.read(request, 0x84)))
        rig.log.append((0x558D90,))
        return 0
    for address, stack in ((0x633680, 1), (0x620BB0, 1), (0x555DA0, 3), (0x558200, 0)):
        machine.hook(address, rig.handler(address, stack), stack)
    machine.hook(0x620870, position, 2)
    machine.hook(0x558D90, create, 1)
    cases = 0
    for item_class, destination, level in itertools.product((-1, 0, 5), (0, 0x9999), (1, 77)):
        rig.answers = {0x633680: item_class & 0xFFFFFFFF, 0x620BB0: 0x3333, 0x555DA0: destination, 0x558200: level}
        requested.clear()
        rig.run(0x56DAB0, 0x41424344, 0x1234, ecx=game, edx=unit)
        expected = [0x633680]
        if item_class != -1:
            expected += [0x620870, 0x620BB0, 0x555DA0]
            if destination:
                expected += [0x558200, 0x558D90]
        equal([entry[0] for entry in rig.log], expected, 'item creation call order')
        if item_class != -1 and destination:
            equal(len(requested), 1, 'item creation count')
            context, flag, request = requested[0]
            equal((context, flag), (game, 0), 'item creation dispatch')
            expected_request = bytearray(0x84)
            for offset, value in ((0, unit), (8, game), (0xC, level), (0x14, item_class), (0x18, 3), (0x1C, 123), (0x20, 456), (0x24, destination), (0x30, 0x1234)):
                struct.pack_into('<I', expected_request, offset, value)
            struct.pack_into('<H', expected_request, 0x28, 1)
            struct.pack_into('<H', expected_request, 0x2A, 0x3322)
            equal(request, bytes(expected_request), 'item creation request')
        cases += 1
    return cases


def path_launch(rig):
    """FUN_005da020 mutates a path before an optional destination-room gate."""
    machine = rig.machine
    unit, path = machine.alloc(0x100), machine.alloc(0x100)
    hooks = ((0x644530, 1), (0x644500, 2), (0x552F60, 0), (0x644660, 2), (0x6212C0, 2),
             (0x650590, 1), (0x648E70, 2), (0x649070, 2), (0x648B90, 2), (0x45ADF0, 0),
             (0x45AE20, 0), (0x620BB0, 1), (0x463740, 1), (0x61AB00, 1),
             (0x648AD0, 3), (0x648CF0, 2), (0x648E40, 2), (0x649970, 3))
    for address, count in hooks:
        machine.hook(address, rig.handler(address, count), count)
    cases = 0
    for skill, path_ok, gate, room, blocked in itertools.product((0, 0x99), (0, 1), (0, 1), (0, 1), (0, 1)):
        view = machine.view('unit', unit)
        view.path, view.anim_length = path if path_ok else 0, 0xAABBCCDD
        rig.answers = {0x644530: 0x123, 0x644500: 0x345, 0x552F60: 0x222 if gate else 0,
                       0x45ADF0: 5, 0x45AE20: 7, 0x620BB0: 0x333,
                       0x463740: 0x444 if room else 0, 0x61AB00: blocked}
        got = rig.run(0x5DA020, 0x88, 20, 30, regs={'eax': skill, 'edi': unit})
        expected = []
        active = bool(skill and path_ok and gate)
        if skill and path_ok:
            expected = [0x644530, 0x644500, 0x552F60]
        if active:
            expected += [0x644660, 0x6212C0, 0x650590, 0x648E70, 0x649070, 0x648B90,
                         0x45ADF0, 0x45AE20, 0x620BB0, 0x463740]
            if room:
                expected += [0x61AB00]
            if not (room and blocked):
                expected += [0x648AD0, 0x648CF0, 0x648E40, 0x649970]
        equal([entry[0] for entry in rig.log], expected, 'path launch order')
        equal(got, int(active and not (room and blocked)), 'path launch result')
        equal(view.anim_length, 0xAABBCD00 if active else 0xAABBCCDD, 'path launch anim length')
        if active and not (room and blocked):
            operations = {entry[0]: entry for entry in rig.log}
            equal(operations[0x648AD0][3], (path, 50, 76), 'path launch target')
            equal(operations[0x648CF0][3], (path, 8), 'path launch mode')
            equal(operations[0x649970][3], (path, unit, 0), 'path launch final args')
        cases += 1
    return cases


def corpse_restore(rig):
    """FUN_005c5430 stamps a location then gates the restoration sequence."""
    machine = rig.machine
    unit, path = machine.alloc(0x100), machine.alloc(0x100)
    hooks = ((0x649180, 1), (0x620BB0, 1), (0x64EC10, 5), (0x620510, 1), (0x64E7B0, 5),
             (0x573780, 0), (0x5A7E60, 3), (0x5A7C20, 3), (0x56E580, 0), (0x540E60, 2),
             (0x6201D0, 1), (0x643C50, 3), (0x554EA0, 5))
    for address, count in hooks:
        machine.hook(address, rig.handler(address, count), count)
    cases = 0
    for kind, has_path, found, right_skill in itertools.product((0, 1, 2, 4, 5), (0, 1), (0, 1), (0, 1)):
        if kind in (2, 4, 5) and not has_path:
            continue
        view = machine.view('unit', unit)
        view.type, view.path, view.flags = kind, path if has_path else 0, 0x80
        if kind in (2, 4, 5):
            machine.view('static_path', path).x = 123
            machine.view('static_path', path).y = 456
        else:
            machine.view('path', path).x = 123
            machine.view('path', path).y = 456
        rig.answers = {0x649180: 0x55, 0x620BB0: 0x33, 0x620510: 2, 0x64E7B0: 0x999 if found else 0,
                       0x6201D0: right_skill}
        rig.run(0x5C5430, 0x77, regs={'esi': unit})
        expected = [0x649180, 0x620BB0, 0x64EC10, 0x620510, 0x620BB0, 0x64E7B0]
        if found:
            expected += [0x573780, 0x5A7E60, 0x5A7C20, 0x56E580, 0x540E60, 0x6201D0]
            if right_skill:
                expected += [0x643C50]
            expected += [0x554EA0]
        equal([entry[0] for entry in rig.log], expected, 'corpse restore calls')
        equal(view.flags, 0x80 | 0x402000E if found else 0x80, 'corpse restore flags')
        coords = (123, 456) if has_path else (0, 0)
        equal(rig.log[2][3], (0x33, *coords, 0x55, 0x8000), 'corpse restore collision args')
        if found:
            equal(rig.log[-1][1:], (0x77, unit, (0x999, *coords, 0, 0)), 'corpse restore destination')
        cases += 1
    return cases


def skill_activation(rig):
    """FUN_0056faf0 selects skill, validates gates and dispatches or reports."""
    machine = rig.machine
    unit, row = machine.alloc(0x100), machine.alloc(0x23C)
    table = bytearray(0xBA4)
    struct.pack_into('<I', table, 0xB98, row)
    struct.pack_into('<I', table, 0xBA0, 1)
    machine.w32(0x744304, machine.put(table))
    item = [0, 1]
    def item_info(current):
        """Return a scripted item-info gate and its final output value."""
        args = tuple(current.arg(index) for index in range(5))
        rig.log.append((0x643B00, args))
        current.w32(args[4], item[1])
        return item[0]
    hooks = ((0x620250, 1), (0x643CE0, 3), (0x56CC60, 2), (0x6442A0, 3),
             (0x553540, 2), (0x554D20, 2), (0x620BB0, 1), (0x61AB00, 1),
             (0x620210, 2), (0x56F640, 3), (0x5809D0, 5))
    for address, count in hooks:
        machine.hook(address, rig.handler(address, count), count)
    machine.hook(0x643B00, item_info, 5)
    scenarios = (
        ('no-skill', {'skill': 0}), ('blocked-player', {'eligible': 0}),
        ('blocked-monster', {'eligible': 0, 'kind': 1}), ('empty-charges', {'item': 1, 'quantity': 0}),
        ('negative-id', {'ident': -1}), ('past-id', {'ident': 1}),
        ('target-refuses', {'target': 1, 'target_block': 1}),
        ('room-player', {'room_block': 1}), ('room-monster', {'room_block': 1, 'kind': 1}),
        ('allowed-room', {'room_block': 1, 'room_flag': 1}),
        ('dispatch-fails', {'dispatch': 0}), ('monster-dispatch-fails', {'dispatch': 0, 'kind': 1}),
        ('dispatch-ok', {'dispatch': 0x77}), ('target-ignored', {'target': 1, 'target_block': 1, 'target_flag': 1}),
    )
    for name, patch in scenarios:
        case = {'skill': 0x111, 'eligible': 1, 'kind': 0, 'item': 0, 'quantity': 1, 'ident': 0,
                'target': 0, 'target_block': 0, 'target_flag': 0, 'room_block': 0, 'room_flag': 0, 'dispatch': 0x77}
        case.update(patch)
        machine.view('unit', unit).type = case['kind']
        raw = bytearray(0x23C)
        raw[5], raw[7] = case['room_flag'], 4 if case['target_flag'] else 0
        machine.mu.mem_write(row, bytes(raw))
        item[:] = (case['item'], case['quantity'])
        rig.answers = {0x620250: case['skill'], 0x643CE0: case['ident'] & 0xFFFFFFFF,
                       0x56CC60: case['eligible'], 0x6442A0: 3, 0x553540: 0x222 if case['target'] else 0,
                       0x554D20: case['target_block'], 0x620BB0: 0x333, 0x61AB00: case['room_block'],
                       0x56F640: case['dispatch']}
        got = rig.run(0x56FAF0, ecx=0x99, edx=unit)
        expected = [0x620250]
        result = 0
        if case['skill']:
            expected += [0x643CE0, 0x56CC60]
            if not case['eligible']:
                if case['kind'] == 0:
                    expected += [0x5809D0]
            else:
                expected += [0x6442A0, 0x643B00]
                if not (case['item'] and not case['quantity']) and case['ident'] == 0:
                    expected += [0x553540]
                    target_refuses = case['target'] and not case['target_flag'] and case['target_block']
                    if case['target'] and not case['target_flag']:
                        expected += [0x554D20]
                    if not target_refuses:
                        if not case['room_flag']:
                            expected += [0x620BB0, 0x61AB00]
                        if not case['room_flag'] and case['room_block']:
                            expected += [0x620210]
                            if case['kind'] == 0:
                                expected += [0x5809D0]
                        else:
                            expected += [0x56F640]
                            result = case['dispatch']
                            if not result and case['kind'] == 0:
                                expected += [0x5809D0]
        equal([entry[0] for entry in rig.log], expected, f'{name}: activation calls')
        equal(got, result, f'{name}: activation result')
        if 0x56F640 in expected:
            entry = next(entry for entry in rig.log if entry[0] == 0x56F640)
            equal(entry[1:], (0x99, unit, (0, 3, 0)), f'{name}: dispatch args')
    return len(scenarios)


def main():
    """Verify the input image and print each group's executed case count."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--mutation-check', action='store_true', help='verify deliberate model defects are caught')
    options = parser.parse_args()
    equal(hashlib.sha256(options.exe.read_bytes()).hexdigest(), GAME_SHA256, 'game.exe SHA-256')
    rig = Rig(options.exe)
    for group in (corpse_predicate, flag_predicate, minion_detach, sequence_probe, wrapper_order, target_room_gate, skill_chain, event_interval, stat_bundle, control_predicate, skill_room_gate, paired_room_gate, item_timed_state, aura_search, timed_monster_state, path_reconfigure, monster_mode_fallback, skill_requirements, current_frame_probe, animation_length, weapon_damage_roll, reflected_damage, state_timers, missile_request, skill_effect_request, item_creation_request, path_launch, corpse_restore, skill_activation):
        print(f'{group.__name__}: {group(rig)} cases passed', flush=True)
    if options.mutation_check:
        global MUTATION
        probes = (('flag-mask', flag_predicate, 'flag predicate order'),
                  ('sequence-zero', sequence_probe, 'sequence probe return'),
                  ('damage-roll', weapon_damage_roll, 'damage roll'),
                  ('timer-found', state_timers, 'state timer found result'),
                  ('monster-delay', timed_monster_state, 'monster state scheduling'),
                  ('frame-edge', current_frame_probe, 'current frame result'))
        for mutation, group, label in probes:
            MUTATION = mutation
            try:
                group(Rig(options.exe))
            except AssertionError as error:
                if not str(error).startswith(label + ':'):
                    raise
                print(f'caught model mutation: {mutation}', flush=True)
            else:
                raise AssertionError(f'missed model mutation: {mutation}')
            finally:
                MUTATION = None


if __name__ == '__main__':
    main()
