# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace fifteen SOURCE (Storm) functions on game.exe 1.14d: file handles, error text, window records.

Run: tools/emu/.venv/bin/python tools/emu/source_storm.py --exe /path/to/Game.exe [--mutation-check]

The fifteen entry points execute from stock game.exe on synthetic handle,
archive and list records.  What runs for real besides them: the handle-slot
pass-through (0x415950), name copy (0x4135d0), CRT copy/fill, the Storm
file-record constructor and list linker (0x418f20, 0x417b80), handle teardown
(0x419070), the loose-file read helpers (0x415560, 0x4154b0), the crypt-table
build and name hash (0x415390, 0x415330), the string duplicator (0x414310),
the message-table registration (0x41c0f0, 0x41c140, 0x41bef0), the window
lookup (0x4209e0) and the two list constructors (0x6ca810, 0x6ca8e0).
Hooked and logged: the archive search (0x417f10), the sector reader
(0x417040), the asynchronous request queue (0x419790, 0x41a880, 0x41a020),
the running checksum (0x41d810), archive close (0x418a50), the error reporter
(0x41caf0), the event table (0x426420), allocation, and the Win32 imports.
No MPQs, files, network, UI or native game processes are used.  The check is
stock behaviour against independent Python expectations (an MPQ name hash, a
seek/size model, FormatMessage routing); d2d has no port of these functions.

# gaps-runs: 00416230 00416370 004165a0 004192f0 004198d0 00419980 0041aad0
# gaps-runs: 0041c170 0041c1a0 0041c1e0 0041c360 0041c430
# gaps-runs: 004208e0 00420970 00420a70
"""
import argparse
import hashlib
import itertools
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGETS = (0x416230, 0x416370, 0x4165A0, 0x4192F0, 0x4198D0, 0x419980, 0x41AAD0,
           0x41C170, 0x41C1A0, 0x41C1E0, 0x41C360, 0x41C430,
           0x4208E0, 0x420970, 0x420A70)
# Stock helpers that must stay game.exe's own code in every group.
REAL_HELPERS = (0x415950, 0x4135D0, 0x41D2A0, 0x41D2C0, 0x41D300, 0x418F20, 0x417B80, 0x419070,
                0x415560, 0x4154B0, 0x415390, 0x415330, 0x414310, 0x41C0F0, 0x41C140, 0x41BEF0,
                0x4209E0, 0x41C340, 0x418B50, 0x415B90)
COUNTS = dict.fromkeys(TARGETS, 0)
MUTATION = None
CS_FILE, CS_MSG = 0x779068, 0x7790CC
FILE_LIST, FILE_HEAD = 0x70836C, 0x708370       # TSList of file records and its head link
WND_LIST, WND_HEAD = 0x708860, 0x708864         # TSList of window records
NAME_MAX = 0x104


def check(actual, expected, label):
    """Compare an observation with its independently specified expectation."""
    if label == MUTATION:
        expected = ("deliberately wrong expectation", expected)
    if actual != expected:
        raise AssertionError(f"{label}: {actual!r} != {expected!r}")


def mpq_hash(name, kind):
    """MPQ name hash (crypt table seeded 0x00100001, upper-cased, kind 0..4) as a Python reference."""
    table, seed = {}, 0x00100001
    for index1 in range(0x100):
        index2 = index1
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            high = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            table[index2] = high | (seed & 0xFFFF)
            index2 += 0x100
    seed1, seed2 = 0x7FED7FED, 0xEEEEEEEE
    for char in name.upper().encode('latin-1'):
        seed1 = (table[kind * 0x100 + char] ^ (seed1 + seed2)) & 0xFFFFFFFF
        seed2 = (char + seed1 + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF
    return seed1


class Rig:
    """An emulator with deterministic allocation, a call log and scripted Win32 imports."""

    def __init__(self, exe):
        """Create the emulator, install allocation hooks and the two list constructors' state."""
        self.machine = emu.Emu(exe, real=TARGETS + REAL_HELPERS)
        self.sizes, self.events, self.log = {}, [], []
        self.machine.hook(0x413020, self.allocate, 4)
        self.machine.hook(0x412650, self.free, 4)
        self.machine.hook(0x683824, lambda machine: 0, 0)      # atexit, called by the static list constructors
        for constructor in (0x6CA810, 0x6CA8E0):
            self.machine.call(constructor, limit=1000)
        self.imported('EnterCriticalSection', lambda m: (self.log.append(('enter', m.arg(0))), 0)[1], 1)
        self.imported('LeaveCriticalSection', lambda m: (self.log.append(('leave', m.arg(0))), 0)[1], 1)

    def allocate(self, machine):
        """Allocate zeroed emulated memory and record the source metadata (size, file, line, flags)."""
        size = machine.arg(0)
        pointer = machine.alloc(max(size, 1))
        self.events.append(('alloc', size, machine.cstr(machine.arg(1)), machine.arg(2), machine.arg(3), pointer))
        self.sizes[pointer] = size
        return pointer

    def free(self, machine):
        """Record a free (pointer, file, line, flags) without reclaiming the emulated allocation."""
        self.events.append(('free', machine.arg(0), machine.cstr(machine.arg(1)), machine.arg(2), machine.arg(3)))
        return 1

    def imported(self, name, handler, count):
        """Replace a named import with a scripted stdcall handler taking count arguments."""
        for slot, (candidate, _) in list(self.machine.slots.items()):
            if candidate == name:
                self.machine.slots[slot] = (name, lambda machine: (handler(machine), count))
                return
        raise AssertionError(f'missing import {name}')

    def hook(self, address, handler, count):
        """Intercept a game.exe callee that is outside the claim; refuse the claimed entries."""
        if address in TARGETS or address in REAL_HELPERS:
            raise AssertionError(f'{address:08x} must run for real')
        self.machine.hook(address, handler, count)

    def callback(self, handler, count):
        """A callable code address inside the stub area, for function-pointer globals."""
        machine = self.machine
        address = emu.STUB + 16 * (len(machine.slots) + 1)
        machine.slots[address] = (f'callback_{address:x}', lambda current: (handler(current), count))
        return address

    def run(self, address, *args, regs=None, ecx=0, balanced=True):
        """Call a function and require normal return, argument cleanup and a balanced lock."""
        machine = self.machine
        self.log.clear()
        result = machine.call(address, *args, regs=regs, ecx=ecx, limit=5_000_000)
        check(machine.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, 'instruction budget / return')
        check(machine.mu.reg_read(UC_X86_REG_ESP), emu.STACK + emu.STACK_SIZE - 0x1000, 'stack cleanup')
        for lock in (CS_FILE, CS_MSG) if balanced else ():
            enters = sum(1 for kind, where in self.log if kind == 'enter' and where == lock)
            leaves = sum(1 for kind, where in self.log if kind == 'leave' and where == lock)
            check(enters, leaves, f'lock {lock:x} balance')
        if address in COUNTS:
            COUNTS[address] += 1
        return result

    def locked(self):
        """Number of times the global file lock was taken during the last run."""
        return sum(1 for kind, where in self.log if kind == 'enter' and where == CS_FILE)

    def record(self, size=0x180, name=b'', **fields):
        """A zeroed file record with chosen dword fields (offset -> value) and a name at +8."""
        machine = self.machine
        address = machine.alloc(size)
        machine.mu.mem_write(address + 8, name + b'\0')
        for offset, value in fields.items():
            machine.w32(address + int(offset[1:], 16), value)
        return address


def list_walk(machine, head_link):
    """Items from a TSList head link: head+4 is the first item, each link's +4 the next, <=0 ends."""
    items, node = [], machine.r32(head_link + 4)
    while 0 < node < 0x80000000 and len(items) < 64:
        items.append(node)
        node = machine.r32(node + 4)
    return items


# ---------------------------------------------------------------- file handle accessors
def file_accessors(exe):
    """SFileGetFileSize 0x416370, SFileSetFilePointer 0x4165a0, SFileGetFileName 0x416230."""
    rig = Rig(exe)
    machine = rig.machine
    # ---- size: loose file asks the OS (with the high word zeroed first), archive file reads the block size
    os_calls = []
    def get_file_size(m):
        """Record GetFileSize and show the caller zeroed the high word first."""
        high = m.arg(1)
        os_calls.append(('size', m.arg(0), high, m.r32(high) if high else None))
        if high:
            m.w32(high, 7)
        return 0x55
    rig.imported('GetFileSize', get_file_size, 2)
    entry_size = 0
    for entry_size in (0, 1, 0x1000, 0x12345678, 0xFFFFFFFF):
        entry = machine.alloc(0x20)
        machine.w32(entry + 8, entry_size)
        archive_file = rig.record(**{'+110': 0xFFFFFFFF, '+118': entry})
        high = machine.put(b'\xa5' * 4)
        check(rig.run(0x416370, archive_file, high), entry_size, 'archive size is block entry +8')
        check(machine.r32(high), 0, 'archive size clears the high word')
        check(rig.run(0x416370, archive_file, 0), entry_size, 'archive size without a high pointer')
        check((rig.locked(), os_calls), (1, []), 'archive size takes the lock once, no OS call')
    for handle in (0x1234, 0, 0xFFFFFFFE):
        loose = rig.record(**{'+110': handle})
        high = machine.put(b'\xa5\xa5\xa5\xa5')
        os_calls.clear()
        check(rig.run(0x416370, loose, high), 0x55, 'loose size passes the OS result through')
        check(os_calls, [('size', handle, high, 0)], 'loose size: OS handle, high word zeroed first')
        check(machine.r32(high), 7, 'loose size lets the OS write the high word')
        os_calls.clear()
        check(rig.run(0x416370, loose, 0), 0x55, 'loose size with null high pointer')
        check(os_calls, [('size', handle, 0, None)], 'loose size null high pointer is forwarded')
        check(rig.locked(), 1, 'loose size takes the lock once')
    # ---- seek
    seeks = []
    def set_file_pointer(m):
        """Record SetFilePointer(handle, low, high pointer, method)."""
        seeks.append(tuple(m.arg(index) for index in range(4)))
        return 0x66
    rig.imported('SetFilePointer', set_file_pointer, 4)
    def model(position, size, offset, method):
        """Reference seek: absolute, relative and end-relative; a backwards move past zero clamps to 0."""
        signed = offset - (1 << 32) if offset >> 31 else offset
        if method == 0:
            return offset
        if method == 1:
            return position + signed if signed >= 0 or position >= -signed else 0
        if method == 2:
            return size + signed if signed >= 0 or size >= -signed else 0
        return position
    cases = 0
    for size, position, offset, method in itertools.product((0, 100, 0x10000), (0, 40, 0xFFFFFFF0), (0, 1, 60, 0xFFFFFFFF, 0xFFFFFFD8, 0x80000000), (0, 1, 2, 3, 0x7FFFFFFF)):
        entry = machine.alloc(0x20)
        machine.w32(entry + 8, size)
        handle = rig.record(**{'+110': 0xFFFFFFFF, '+118': entry, '+120': position, '+13c': 0x11, '+140': 0x22})
        expected = model(position, size, offset, method) & 0xFFFFFFFF
        check(rig.run(0x4165A0, handle, offset, 0, method), expected, 'archive seek position')
        check(machine.r32(handle + 0x120), expected, 'archive seek stores the position')
        check((machine.r32(handle + 0x13C), machine.r32(handle + 0x140)), (0, 0), 'seek drops the cached sector bytes')
        check(rig.locked(), 1, 'seek takes the lock once')
        cases += 1
    loose = rig.record(**{'+110': 0x4321})
    zero = machine.put(b'\0\0\0\0')
    for high, method in ((0, 0), (zero, 1), (zero, 2)):
        seeks.clear()
        check(rig.run(0x4165A0, loose, 0xFFFFFFF0, high, method), 0x66, 'loose seek result')
        check(seeks, [(0x4321, 0xFFFFFFF0, 0, method)], 'loose seek forwards low word and method, no high word')
    nonzero = machine.put(struct.pack('<I', 1))
    machine.last_error = 0
    seeks.clear()
    check(rig.run(0x4165A0, loose, 5, nonzero, 0), 0xFFFFFFFF, 'nonzero high word is rejected')
    check((machine.last_error, machine.r32(0x7790F4), seeks, rig.locked()), (0x57, 0x57, [], 0), 'rejected seek: error 0x57, no lock, no OS call')
    # ---- name
    for name, size in itertools.product((b'', b'a', b'DATA\\GLOBAL\\x.txt', b'n' * 0x103), (0, 1, 2, 5, 0x104)):
        handle = rig.record(name=name)
        out = machine.put(b'\xa5' * 0x120)
        expected = (name[:size - 1] + b'\0') if size else b'\0'
        check(rig.run(0x416230, handle, out, size), 1, 'file name result')
        check(machine.read(out, 0x120), expected + b'\xa5' * (0x120 - len(expected)), 'file name copy is truncated with a NUL, guard bytes intact')
        check(rig.locked(), 1, 'file name takes the lock once')
    machine.last_error = 0
    check(rig.run(0x416230, rig.record(name=b'x'), 0, 8), 0, 'file name null buffer')
    check((machine.last_error, rig.locked()), (0x57, 0), 'file name null buffer: error 0x57, no lock')
    print(f'  accessor cases: {cases} seeks')


# ---------------------------------------------------------------- open, close, SFileOpenFile
class Archive:
    """A synthetic archive record and block entry in emulator memory."""

    def __init__(self, machine, sector=0x1000, size=0x2500, flags=0x80000200, offset=0x1234, checksum=0, base=0x77):
        self.address = machine.alloc(0x180)
        machine.mu.mem_write(self.address + 8, b'C:\\D2\\d2data.mpq\0')
        machine.w32(self.address + 0x124, sector)
        machine.w32(self.address + 0x130, base)
        self.entry = machine.alloc(0x20)
        for index, value in enumerate((offset, 0, size, flags, 0, 0, checksum)):
            machine.w32(self.entry + 4 * index, value)


def open_and_close(exe):
    """SFileOpenFileEx 0x4192f0, SFileOpenFile 0x419980 and SFileCloseFile 0x4198d0."""
    rig = Rig(exe)
    machine = rig.machine
    search = {'result': 0, 'archive': None, 'path': b'', 'calls': []}
    def archive_search(m):
        """Stand in for the archive search: record (ECX, name, flags) and publish scripted outputs."""
        entry_out, name_flags = m.mu.reg_read(UC_X86_REG_EDX), (m.cstr(m.arg(0)), m.arg(1))
        search['calls'].append((m.mu.reg_read(UC_X86_REG_ECX), entry_out != 0) + name_flags)
        if search['archive']:
            m.w32(m.arg(2), search['archive'].address)
            m.w32(entry_out, search['archive'].entry)
        m.mu.mem_write(m.arg(3), search['path'] + b'\0')
        return search['result']
    rig.hook(0x417F10, archive_search, 4)
    os_calls = []
    def create_file(m):
        """Record CreateFileA(path, access, share, security, disposition, attributes, template)."""
        os_calls.append(('create', m.cstr(m.arg(0)), tuple(m.arg(index) for index in range(1, 7))))
        return create_file.handle
    create_file.handle = 0x7777
    rig.imported('CreateFileA', create_file, 7)
    rig.imported('SetFilePointer', lambda m: (os_calls.append(('seek', tuple(m.arg(i) for i in range(4)))), 0)[1], 4)
    notified = []
    machine.w32(0x779060, rig.callback(lambda m: (notified.append((m.cstr(m.arg(0)), m.arg(1))), 0)[1], 2))
    machine.w32(0x779064, 0xC0FFEE)
    def open_ex(name, flags, archive=0, handle_out=None):
        """Call SFileOpenFileEx with a fresh output slot filled with a canary."""
        slot = machine.put(b'\xa5\xa5\xa5\xa5') if handle_out is None else handle_out
        search['calls'].clear(); os_calls.clear(); notified.clear(); rig.events.clear()
        result = rig.run(0x4192F0, archive, machine.put(name + b'\0') if name is not None else 0, flags, slot)
        return result, machine.r32(slot) if slot else None
    # rejected inputs: error 0x57, nothing else happens, no lock
    for name, slot in ((b'a.txt', 0), (b'', 1)):
        machine.last_error = 0
        search['calls'].clear()
        out = machine.put(b'\xa5\xa5\xa5\xa5')
        result = rig.run(0x4192F0, 0, machine.put(name + b'\0') if name is not None else 0, 0, out if slot else 0)
        check((result, machine.last_error, search['calls'], rig.locked()), (0, 0x57, [], 0), 'open rejects a null handle slot or an empty name (a null name is read before the test and faults)')
        check(machine.r32(out), 0 if slot else 0xA5A5A5A5, 'open clears a valid handle slot before validating the name')
    # not found: slot cleared, no callback
    search['result'] = 0
    check(open_ex(b'x.txt', 0x11, archive=0x4444), (0, 0), 'open not found')
    check((search['calls'], rig.locked(), notified, os_calls), ([(0x4444, True, 'x.txt', 0x11)], 1, [], []), 'open not found: search arguments, lock, no callback')
    # loose file on disk
    search['result'], search['path'] = 2, b'C:\\loose\\a.txt'
    create_file.handle = 0xFFFFFFFF
    check(open_ex(b'a.txt', 0), (0, 0), 'loose open fails when the OS refuses')
    check((os_calls, notified), ([('create', 'C:\\loose\\a.txt', (0x80000000, 1, 0, 3, 0x8000000, 0))], []), 'loose open arguments, no callback on failure')
    create_file.handle = 0x7777
    check(open_ex(b'a.txt', 4), (2, 0x7777), 'loose open raw handle')
    check((rig.events, notified), ([], [('a.txt', 0xC0FFEE)]), 'loose raw open makes no record, notifies')
    result, handle = open_ex(b'dir\\a.txt', 0)
    check(result, 2, 'loose open record result')
    view = lambda offset, handle=handle: machine.r32(handle + offset)
    check(machine.cstr(handle + 8), 'dir\\a.txt', 'loose record name')
    check((view(0x110), view(0x144), view(0x14C), view(0x114), view(0x118)), (0x7777, 1, 0, 0, 0), 'loose record fields')
    check(machine.cstr(view(0x10C)), 'C:\\loose\\a.txt', 'loose record keeps a duplicate of the disk path')
    check([event[:5] for event in rig.events if event[2] != '.\\SOURCE\\SSTR.CPP'], [('alloc', 0x158, '.?AUFILEREC@@', 0xFFFFFFFE, 8)], 'loose record allocation')
    check(list_walk(machine, FILE_HEAD), [handle], 'loose record is linked into the file list')
    check(notified, [('dir\\a.txt', 0xC0FFEE)], 'loose open callback')
    # archive file with the raw-stream flag
    archive = Archive(machine)
    search['result'], search['archive'] = 1, archive
    create_file.handle = 0xFFFFFFFF
    machine.last_error = 0
    check(open_ex(b'f.bin', 4), (0, 0xFFFFFFFF), 'raw archive open failure stores the invalid handle')
    check((machine.last_error, notified), (2, []), 'raw archive open failure: error 2, no callback')
    create_file.handle = 0x8888
    check(open_ex(b'f.bin', 4), (1, 0x8888), 'raw archive open')
    check(os_calls, [('create', 'C:\\D2\\d2data.mpq', (0x80000000, 1, 0, 3, 0x8000000, 0)), ('seek', (0x8888, 0x1234, 0, 0))], 'raw archive open reopens the archive file at the block offset')
    check(notified, [('f.bin', 0xC0FFEE)], 'raw archive open callback')
    # archive file record
    for name, flags, size, sector, checksum, enabled in (
            (b'DATA\\GLOBAL\\gateways.txt', 0x80000000, 0x2500, 0x1000, 0, 0),
            (b'd2data:data\\global\\Excel\\Levels.txt', 0x80000200, 0x1000, 0x1000, 0xBEEF, 0x5A),
            (b'C:dir\\sub\\name.dc6', 0x80020300, 0x1001, 0x200, 0, 0),
            (b'noext', 0x80020000, 0, 0x1000, 1, 1),
            (b'a:b\\c:d', 0x80000100, 0xFFFF, 0x10000, 9, 0x77)):
        archive = Archive(machine, sector=sector, size=size, flags=flags, offset=0x2468, checksum=checksum, base=0x300)
        search['archive'] = archive
        machine.w32(0x7790B0, enabled)
        base = name.decode().rsplit(':', 1)[-1].rsplit('\\', 1)[-1]
        key = mpq_hash(base, 3)
        if flags & 0x20000:
            key = ((key + 0x2468 - 0x300) & 0xFFFFFFFF) ^ size
        sectors = -(-size // sector)
        refs = machine.r32(archive.address + 0x148)
        result, handle = open_ex(name, 0x3)
        check(result, 1, 'archive open result')
        view = lambda offset, handle=handle: machine.r32(handle + offset)
        check(machine.cstr(handle + 8), name.decode(), 'archive record keeps the caller name')
        check((view(0x110), view(0x114), view(0x118), view(0x11C), view(0x128)), (0xFFFFFFFF, archive.address, archive.entry, key, sectors), 'archive record: stream, archive, entry, file key, sector count')
        table = view(0x12C)
        expected_allocs = []
        if flags & 0xFF00:
            expected_allocs.append(('alloc', sectors * 4 + 4, '.\\SOURCE\\SFILE.CPP', 0x1231, 0, table))
        else:
            check(table, 0, 'no sector table when the entry has no compression or encryption bits')
        expected_allocs += [('alloc', 0x158, '.?AUFILEREC@@', 0xFFFFFFFE, 8, handle), ('alloc', 0x1000, '.\\SOURCE\\SFILE.CPP', 0x123C, 0, view(0x138))]
        check(sorted(rig.events, key=lambda event: event[3] if event[2] != '.?AUFILEREC@@' else 0), sorted(expected_allocs, key=lambda event: event[3] if event[2] != '.?AUFILEREC@@' else 0), 'archive record allocations')
        check((view(0x144), view(0x150), view(0x14C), view(0x120)), (1, 1, enabled if checksum else 0, 0), 'archive record: refcount, state 1, checksum flag only with an expected checksum, position 0')
        check(machine.r32(archive.address + 0x148), refs + 1, 'archive reference count rises')
        check(search['calls'], [(0, True, name.decode(), 3)], 'archive open search arguments')
        check((notified, os_calls, rig.locked()), ([(name.decode(), 0xC0FFEE)], [], 1), 'archive open callback and no OS call')
        check(list_walk(machine, FILE_HEAD)[0], handle, 'newest record is first in the file list')
    # no callback registered: the open still succeeds
    machine.w32(0x779060, 0)
    check(open_ex(b'x', 0x3)[0], 1, 'archive open without callback')
    machine.w32(0x779060, rig.callback(lambda m: 0, 2))
    # SFileOpenFile: SFileOpenFileEx(0, name, flags from 0x418b50, slot)
    for global58, count in itertools.product((0, 1, 2, 3), (0, 1, 5)):
        machine.w32(0x779058, global58)
        machine.w32(0x70835C, count)
        expected = (global58 & 1) | (global58 & 2) | (1 if count <= 0 and global58 == 0 else 0)
        search['calls'].clear()
        slot = machine.put(b'\xa5' * 4)
        check(rig.run(0x419980, machine.put(b'q.txt\0'), slot), 1, 'SFileOpenFile result is the open result')
        check(search['calls'], [(0, True, 'q.txt', expected)], 'SFileOpenFile forwards a null archive and derived flags')
    machine.w32(0x779058, 0)
    machine.w32(0x70835C, 0)
    # ---- close
    freed = []
    rig.hook(0x418A50, lambda m: freed.append(('archive', m.mu.reg_read(UC_X86_REG_ECX))) or 0, 0)
    os_closed = []
    rig.imported('CloseHandle', lambda m: (os_closed.append(m.arg(0)), 1)[1], 1)
    machine.w32(0x779060, 0)
    # empty the file list before building a known one
    while list_walk(machine, FILE_HEAD):
        rig.run(0x4198D0, list_walk(machine, FILE_HEAD)[0])
    check((machine.r32(FILE_HEAD), machine.r32(FILE_HEAD + 4)), (FILE_HEAD, (~FILE_HEAD) & 0xFFFFFFFF), 'file list returns to its constructed empty state')
    handles = []
    archive = Archive(machine)
    for index in range(4):
        search['archive'], search['result'] = archive, 1
        handles.append(open_ex(b'f%d' % index, 3)[1])
    check(list_walk(machine, FILE_HEAD), handles[::-1], 'four opens link newest first')
    def close(handle):
        """Close a handle and return (result, list order, frees, OS closes)."""
        rig.events.clear(); freed.clear(); os_closed.clear()
        result = rig.run(0x4198D0, handle)
        return result, list_walk(machine, FILE_HEAD), [event for event in rig.events if event[0] == 'free'], list(os_closed)
    # a handle with two references is only unlinked and decremented, not freed
    machine.w32(handles[1] + 0x144, 2)
    result, order, frees, closed = close(handles[1])
    check((result, order, frees, closed, machine.r32(handles[1] + 0x144)), (1, [handles[3], handles[2], handles[0]], [], [], 1), 'close of a shared handle unlinks and decrements')
    check((machine.r32(handles[1]), machine.r32(handles[1] + 4)), (0, 0), 'close clears the record link')
    # middle, newest (list head side) and oldest (list tail side)
    for victim, remaining in ((handles[2], [handles[3], handles[0]]), (handles[3], [handles[0]]), (handles[0], [])):
        archive_refs = machine.r32(archive.address + 0x148)
        result, order, frees, closed = close(victim)
        check((result, order), (1, remaining), 'close unlinks the record')
        names = [event[2] for event in frees]
        check(names[-1], 'delete', 'close frees the record last')
        check(frees[-1][1], victim, 'close frees the closed record')
        check('.\\SOURCE\\SFILE.CPP' in names, True, 'teardown frees the sector table and cache buffer')
        check(machine.r32(archive.address + 0x148), archive_refs - 1, 'close releases the archive reference')
    check(list_walk(machine, FILE_HEAD), [], 'file list empty again')
    # a loose handle: the OS handle is closed on teardown
    search['result'], search['path'] = 2, b'C:\\x'
    create_file.handle = 0x9999
    loose = open_ex(b'loose', 0)[1]
    result, order, frees, closed = close(loose)
    check((result, order, closed), (1, [], [0x9999]), 'close of a loose handle closes the OS handle')
    # NULL handle: operates on the default list head, never frees, leaves the head link cleared
    check(rig.run(0x4198D0, 0), 1, 'close null handle')
    check((machine.r32(FILE_HEAD), machine.r32(FILE_HEAD + 4), rig.locked()), (0, 0, 1), 'close null clears the default list head link (original quirk)')
    print('  open/close cases ok')


# ---------------------------------------------------------------- SFileReadFileEx2
def read_file(exe):
    """SFileReadFileEx2 0x41aad0: argument gates, loose files, the sector cache, sync and queued reads, checksum."""
    rig = Rig(exe)
    machine = rig.machine
    calls = []
    content = bytes((index * 7 + 3) & 0xFF for index in range(0x2500))
    state = {'size': len(content), 'final': 0}
    def sector_read(m):
        """0x417040 stand-in: copy synthetic file bytes at EAX into the buffer, return the count."""
        offset, buffer, length = m.mu.reg_read(UC_X86_REG_EAX), m.arg(0), m.arg(1)
        data = content[offset:offset + length]
        m.mu.mem_write(buffer, data)
        handle = m.mu.reg_read(UC_X86_REG_ECX)
        calls.append(('read', handle, offset, buffer, length))
        if m.r32(handle + 0x14C):                 # the running-checksum bookkeeping 0x417040 does for 0x41aad0
            if offset == 0:
                m.w32(handle + 0x150, 2)
                m.w32(handle + 0x154, 0)
            if m.r32(handle + 0x150) == 2:
                m.w32(handle + 0x154, m.r32(handle + 0x154) + len(data))
        return len(data)
    rig.hook(0x417040, sector_read, 2)
    def checksum_step(m):
        """0x41D810 stand-in: record (accumulator pointer, mode) and set a final checksum value."""
        calls.append(('checksum', m.arg(0), m.arg(1), m.mu.reg_read(UC_X86_REG_ECX), m.mu.reg_read(UC_X86_REG_EDX)))
        m.w32(m.arg(0), state['final'])
        return 0
    rig.hook(0x41D810, checksum_step, 2)
    rig.hook(0x41CAF0, lambda m: calls.append(('report', m.arg(0), m.cstr(m.arg(1)), m.arg(2), m.arg(3), m.arg(4), m.arg(5))) or 0, 6)
    rig.hook(0x41A880, lambda m: calls.append(('queue-init',)) or 0, 0)
    rig.imported('SetEvent', lambda m: (calls.append(('set-event', m.arg(0))), 1)[1], 1)
    rig.imported('CreateEventA', lambda m: (calls.append(('create-event', tuple(m.arg(i) for i in range(4)))), 0xE111)[1], 4)
    rig.imported('WaitForSingleObject', lambda m: (calls.append(('wait', m.arg(0), m.arg(1))), 0)[1], 2)
    rig.imported('CloseHandle', lambda m: (calls.append(('close', m.arg(0))), 1)[1], 1)
    rig.imported('ReadFile', lambda m: read_file_os(m), 5)
    machine.w32(0x708348, 0x20000)
    loose_script = {'data': b'', 'ok': 1}
    def read_file_os(m):
        """ReadFile stand-in for loose files: honour the scripted data and success flag."""
        handle, buffer, want, got = m.arg(0), m.arg(1), m.arg(2), m.arg(3)
        ok = loose_script['ok'].pop(0) if isinstance(loose_script['ok'], list) else loose_script['ok']
        data = loose_script['data'][:want]
        m.mu.mem_write(buffer, data)
        m.w32(got, len(data))
        calls.append(('os-read', handle, want, m.arg(4)))
        m.last_error = 5
        return ok
    entry = machine.alloc(0x20)
    machine.w32(entry + 8, len(content))
    archive = Archive(machine, sector=0x1000, size=len(content))
    def handle_for(position=0, enabled=0, queued=0):
        """A sync archive handle at a position, with a cache buffer, optionally checksummed or queued."""
        machine.w32(archive.address + 0x110, queued)
        cache = machine.alloc(0x1000)
        return rig.record(name=b'DATA\\file.bin', **{'+110': 0xFFFFFFFF, '+114': archive.address, '+118': archive.entry,
                                                   '+120': position, '+138': cache, '+14c': enabled, '+150': 1})
    def read(handle, count, overlapped=0, mask=0x5A5A, reply=None):
        """Call SFileReadFileEx2 and return (result, bytes, reported count)."""
        buffer = machine.put(b'\xcc' * (count + 8))
        got = machine.put(b'\xa5' * 4)
        calls.clear()
        machine.last_error = 0
        result = rig.run(0x41AAD0, handle, buffer, count, got, overlapped, 0x1111, mask)
        return result, machine.read(buffer, count + 8), machine.r32(got)
    # ---- argument gates
    machine.last_error = 0
    got = machine.put(b'\xa5' * 4)
    check(rig.run(0x41AAD0, handle_for(), 0, 4, got, 0, 0, 0), 0, 'read null buffer')
    check((machine.r32(got), machine.last_error, rig.locked()), (0, 0x57, 0), 'read null buffer: count cleared, error 0x57, no lock')
    handle = handle_for()
    result, data, reported = read(handle, 0)
    check((result, reported, calls, rig.locked()), (1, 0, [], 1), 'zero-length read does nothing')
    over = machine.put(struct.pack('<IIIII', 0, 0, 0x400, 0, 0xEEE1))
    check(read(handle, 0, over)[0], 1, 'zero-length overlapped read')
    check(calls, [('set-event', 0xEEE1)], 'zero-length overlapped read signals the event')
    machine.w32(over + 0x10, 0)
    read(handle, 0, over)
    check(calls, [], 'zero-length overlapped read without an event signals nothing')
    # ---- loose file
    loose = rig.record(name=b'loose.txt', **{'+110': 0x4242})
    check(machine.r32(0x70834C), 2, 'stock I/O-error policy is 2 (report, then retry)')
    for data, ok, expected in ((b'abcdefgh', 1, 1), (b'abc', 1, 0), (b'', 1, 0)):
        loose_script.update(data=data, ok=ok)
        result, out, reported = read(loose, 8)
        check((result, reported, out[:len(data)]), (expected, len(data), data), 'loose read result')
        check(calls, [('os-read', 0x4242, 8, 0)], 'loose read goes straight to ReadFile')
        if len(data) != 8:
            check(machine.last_error, 0x26, 'short loose read sets the end-of-file error')
    # a failing ReadFile is reported through the error reporter and retried until it succeeds
    loose_script.update(data=b'abcdefgh', ok=[0, 0, 1])
    result, out, reported = read(loose, 8)
    check((result, reported, out[:8]), (1, 8, b'abcdefgh'), 'loose read retries after a failed ReadFile')
    check([call[0] for call in calls], ['os-read', 'report', 'os-read', 'report', 'os-read'], 'each failure is reported once and retried')
    check(calls[1], ('report', 5, 'loose.txt', 0xFFFFFFFC, 0, 0, 1), 'failed read report arguments (OS error, name, -4, 0, 0, allow-continue)')
    # ---- sync archive reads: model sweep (data, position, count, return)
    cases = 0
    for start, counts in ((0, (1, 0x10, 0xFFF, 0x1000, 0x1001, 0x2500, 0x3000)), (0x10, (0x20, 0xFF0, 0x2000)), (0xFF0, (0x10, 0x11, 0x30)),
                          (0x2400, (0x10, 0x100, 0x101, 0x400)), (0x2500, (1, 0x10)), (0x3000, (4,))):
        for count in counts:
            handle = handle_for(start)
            result, data, reported = read(handle, count)
            expected = content[start:start + count]
            check((result, reported, data[:len(expected)], data[count:]), (int(len(expected) == count), len(expected), expected, b'\xcc' * 8), 'sync read data and count')
            check(machine.r32(handle + 0x120), start + len(expected), 'sync read advances the position by the bytes returned')
            check(machine.last_error, 0 if len(expected) == count else 0x26, 'short sync read sets error 0x26')
            check(rig.locked(), 1, 'sync read takes the lock once')
            cases += 1
    # sequence of small reads, served from the cached sector bytes
    handle = handle_for(0)
    pieces = []
    for count in (5, 0x10, 0x20, 0xFE0, 0x30, 0x100):
        before = machine.r32(handle + 0x120)
        result, data, reported = read(handle, count)
        reads = [call for call in calls if call[0] == 'read']
        pieces.append((before, count, [(call[2], call[4]) for call in reads]))
        check(data[:count], content[before:before + count], 'cached sequence data')
    check(pieces[0], (0, 5, [(0, 0x1000)]), 'first small read fills the cache with the rest of the sector')
    check(pieces[1][2] + pieces[2][2], [], 'next small reads come from the cache')
    check(pieces[3][2][0][0] % 0x1000, 0, 'cache refill starts at the next sector boundary or the first uncached byte')
    # large read with a user buffer, unaligned start
    handle = handle_for(0x10)
    read(handle, 0x2000)
    check([(c[2], c[4]) for c in calls if c[0] == 'read'], [(0x10, 0x2000)], 'large read bypasses the cache and reads the whole length at the position')
    handle = handle_for(0x10)
    read(handle, 0x20)
    check([(c[2], c[4]) for c in calls if c[0] == 'read'], [(0x10, 0xFF0)], 'small read fills the cache from the position to the sector end')
    check((machine.r32(handle + 0x13C), machine.r32(handle + 0x140)), (0x20, 0xFF0), 'cache cursor and valid length after a small read')
    handle = handle_for(0x2400)
    read(handle, 0x10)
    check((machine.r32(handle + 0x13C), machine.r32(handle + 0x140)), (0x10, 0x100), 'cache valid length is what the reader returned at end of file')
    # overlapped sync read: offset from the request, position untouched, event signalled
    handle = handle_for(0x40)
    over = machine.put(struct.pack('<IIIII', 0, 0, 0x800, 0, 0xEEE2))
    result, data, reported = read(handle, 0x30, over)
    check((result, data[:0x30], reported, machine.r32(handle + 0x120)), (1, content[0x800:0x830], 0x30, 0x40), 'overlapped read uses the request offset and leaves the position')
    check([c for c in calls if c[0] in ('read', 'set-event')][-1], ('set-event', 0xEEE2), 'overlapped read signals its event')
    check([c[2] for c in calls if c[0] == 'read'], [0x800], 'overlapped read reads at the request offset')
    # ---- checksum
    entry_checksum = machine.r32(archive.entry + 0x18)
    for final, expected_report in ((entry_checksum, False), (entry_checksum ^ 1, True)):
        state['final'] = final
        handle = handle_for(0, enabled=0x5A)
        machine.w32(handle + 0x150, 2)
        machine.w32(handle + 0x154, 0)
        # entry +0x18 must be non-zero for a checksummed file
        machine.w32(archive.entry + 0x18, 0xBEEF)
        entry_checksum = 0xBEEF
        state['final'] = 0xBEEF if not expected_report else 0xBEEE
        result, data, reported = read(handle, len(content))
        steps = [c for c in calls if c[0] == 'checksum']
        check(result, 1, 'checksummed whole-file read result')
        check(machine.r32(handle + 0x150), 4, 'checksum state advances to complete')
        check([c for c in calls if c[0] == 'report'], [('report', 0x85100083, 'DATA\\file.bin', 0xFFFFFFFC, 0, 0, 1)] if expected_report else [], 'checksum mismatch is reported once, match is silent')
        check(len(steps), 1, 'checksum finish step runs once')
    # ---- queued reads (archive opened for overlapped IO)
    def queued_request(m):
        """0x419790 stand-in: record the 13 arguments (register EDI = handle) and return a request with +0x3c bytes."""
        calls.append(('queue', m.mu.reg_read(UC_X86_REG_EDI), tuple(m.arg(index) for index in range(13))))
        request = m.alloc(0x50)
        m.w32(request + 0x3C, queued_request.last)
        return request
    queued_request.last = 0
    rig.hook(0x419790, queued_request, 13)
    rig.hook(0x41A020, lambda m: calls.append(('request-release', m.mu.reg_read(UC_X86_REG_ECX))) or 0, 0)
    for count in (0x100, 0x20000, 0x20001, 0x50000):
        handle = handle_for(0, queued=1)
        queued_request.last = 0x77
        result, data, reported = read(handle, count)
        queue = [c for c in calls if c[0] == 'queue']
        span = 0x1000 if count < 0x1000 else count          # a small read fills the cache buffer up to the sector end
        chunks = -(-span // 0x20000)
        check(len(queue), chunks, 'queued read splits into DAT_00708348-sized requests')
        check(calls[0], ('queue-init',), 'queued read initialises the queue first')
        check(('create-event', (0, 1, 0, 0)) in calls, True, 'queued read makes a manual-reset event')
        for index, (_, handle_reg, args) in enumerate(queue):
            last = index == chunks - 1
            length = min(0x20000, span - index * 0x20000)
            check((handle_reg, args[0], args[6], args[7], args[8], args[9], args[10], args[11], args[12]),
                  (handle, index * 0x20000, length, 0x7FFFFFFF, 1, 0xE111 if last else 0, 0 if last else 1, int(last), 0x5A5A if last else 0), 'queued request arguments')
        check([c for c in calls if c[0] in ('wait', 'close', 'request-release')][:2], [('wait', 0xE111, 0xFFFFFFFF), ('close', 0xE111)], 'queued read waits on, then closes, its event')
        expected_got = (span - length) + 0x77
        check(reported, min(count, expected_got), 'queued read count is the earlier chunks plus the last request result')
        check(machine.r32(handle + 0x120), min(count, expected_got), 'queued read advances the position')
        check([event for event in rig.events if event[0] == 'free' and event[2] == 'delete'] != [], True, 'queued request record is freed')
        check(machine.last_error, 0 if expected_got >= count else 0x26, 'queued read short count error')
        cases += 1
    handle = handle_for(0x40, queued=1)
    over = machine.put(struct.pack('<IIIII', 0, 0, 0x800, 0, 0xEEE3))
    queued_request.last = 0
    result, data, reported = read(handle, 0x30, over)
    check((result, machine.r32(handle + 0x120), [c for c in calls if c[0] in ('wait', 'close')]), (1, 0x40, []), 'overlapped queued read returns at once without waiting')
    print(f'  read cases: {cases} sweeps')


# ---------------------------------------------------------------- message text, locks and the error flag
def message_text(exe):
    """0x41c170 string lookup, 0x41c1a0 / 0x41c1e0 lock, 0x41c360 reporter flag, 0x41c430 FormatMessage wrapper."""
    rig = Rig(exe)
    machine = rig.machine
    os_calls = []
    rig.imported('GetModuleHandleA', lambda m: (os_calls.append(('module', m.arg(0))), 0x400000)[1], 1)
    loaded = {'result': 0}
    def load_string(m):
        """LoadStringA(module, id, buffer, size): record and return the scripted length."""
        os_calls.append(('load-string', m.arg(0), m.arg(1), m.arg(2), m.arg(3)))
        return loaded['result']
    rig.imported('LoadStringA', load_string, 4)
    # ---- 0x41c170: ESI = message index; resource string 0x5100 + index, else the built-in table entry
    for index in range(0, 8):
        for loaded['result'] in (0, 9):
            os_calls.clear()
            pointer = rig.run(0x41C170, regs={'esi': index})
            check(os_calls, [('module', 0), ('load-string', 0x400000, 0x5100 + index, 0x779100, 0x100)], 'string lookup asks the executable resource')
            if loaded['result']:
                check(pointer, 0x779100, 'resource string is returned from the shared buffer')
            else:
                check(pointer, machine.r32(0x6CF2F4 + 4 * index), 'missing resource falls back to the built-in table entry')
    check(machine.cstr(machine.r32(0x6CF2F4)), 'ERROR #%u (0x%08x)', 'built-in entry 0 is the generic error format')
    # ---- 0x41c360
    for value in (0, 1, 0x7FFFFFFF, 0xFFFFFFFF):
        rig.run(0x41C360, value)
        check(machine.r32(0x7790F0), value, 'reporter-disable flag stores its argument')
    # ---- locks: the first caller initialises the critical section behind an interlocked counter
    machine.w32(0x70841C, 0xFFFFFFFF)
    rig.imported('InterlockedIncrement', lambda m: (os_calls.append(('inc', m.arg(0))), machine.w32(m.arg(0), machine.r32(m.arg(0)) + 1), machine.r32(m.arg(0)))[2], 1)
    rig.imported('InterlockedDecrement', lambda m: (os_calls.append(('dec', m.arg(0))), machine.w32(m.arg(0), machine.r32(m.arg(0)) - 1), machine.r32(m.arg(0)))[2], 1)
    rig.imported('InitializeCriticalSection', lambda m: (os_calls.append(('init', m.arg(0))), 0)[1], 1)
    os_calls.clear()
    rig.run(0x41C1A0, balanced=False)
    check(os_calls, [('inc', 0x70841C), ('init', CS_MSG)], 'first lock initialises the message critical section')
    check((rig.log, machine.r32(0x70841C)), ([('enter', CS_MSG)], 0), 'first lock enters once, counter stays 0')
    os_calls.clear()
    rig.run(0x41C1A0, balanced=False)
    check(os_calls, [('inc', 0x70841C), ('dec', 0x70841C)], 'later locks skip the initialisation and undo the increment')
    check((rig.log, machine.r32(0x70841C)), ([('enter', CS_MSG)], 0), 'later lock enters once')
    rig.run(0x41C1E0, balanced=False)
    check(rig.log, [('leave', CS_MSG)], 'unlock leaves the message critical section')
    # ---- 0x41c430: FormatMessage wrapper
    format_calls = []
    def format_message(m):
        """FormatMessageA stand-in: record the 7 arguments and the buffer's first byte, write scripted text."""
        buffer = m.arg(4)
        format_calls.append(tuple(m.arg(i) for i in range(7)) + (m.read(buffer, 1)[0],))
        text = format_message.text
        if text:
            m.mu.mem_write(buffer, text + b'\0')
        return len(text)
    format_message.text = b'boom'
    rig.imported('FormatMessageA', format_message, 7)
    for buffer_ptr, size in ((0, 16), (1, 0)):
        machine.last_error = 0
        machine.w32(0x7790F4, 0)
        format_calls.clear()
        out = 0 if buffer_ptr == 0 else machine.put(b'\xa5' * 8)
        check(rig.run(0x41C430, 0x85100083, out, size if buffer_ptr == 0 else 0), 0, 'format rejects a null buffer or zero size')
        check((format_calls, machine.last_error, machine.r32(0x7790F4)), ([], 0x57, 0x57), 'format rejection: error 0x57, no OS call')
        if buffer_ptr:
            check(machine.read(out, 4), b'\xa5' * 4, 'rejected format leaves the buffer alone')
    machine.w32(0x7790EC, 0)
    machine.w32(0x7790E8, 0)
    for code, expected_source in ((0x85100083, True), (0x88760001, True), (0x88780000, True), (0x87600001, False), (0x00000005, False), (0x85000000, False), (0x0511FFFF, False)):
        out = machine.put(b'\xa5' * 40)
        format_calls.clear()
        format_message.text = b'boom'
        result = rig.run(0x41C430, code, out, 40)
        facilities = []
        node = machine.r32(0x7790E8)
        while node:
            facilities.append((machine.r16(node), machine.r32(node + 4)))
            node = machine.r32(node + 8)
        check(facilities, [(0x510, 0x400000), (0x876, 0x400000), (0x878, 0x400000)], 'first use registers the three module facilities once')
        check(machine.r32(0x7790EC), 1, 'registration flag set')
        source = 0x400000 if expected_source else 0
        check(format_calls, [((0x800 if source else 0x1000), source, code, 0x400, out, 40, 0, 0)], 'FormatMessage flags: module table when the facility is registered, else system text')
        check((result, machine.cstr(out)), (1, 'boom'), 'format result and text')
        format_message.text = b''
        check(rig.run(0x41C430, code, out, 40), 0, 'format failure returns zero')
        check(machine.read(out, 1), b'\0', 'format clears the first byte before the OS call')
    print('  message cases ok')


# ---------------------------------------------------------------- window records
def window_records(exe):
    """0x4208e0 create, 0x420970 remove and 0x420a70 register: the AUWNDREC list and its event registration."""
    rig = Rig(exe)
    machine = rig.machine
    events = []
    rig.hook(0x426420, lambda m: (events.append(tuple(m.arg(i) for i in range(5))), 0x1D)[1], 5)
    check((machine.r32(WND_HEAD), machine.r32(WND_HEAD + 4)), (WND_HEAD, (~WND_HEAD) & 0xFFFFFFFF), 'window list starts as the constructed empty list')
    records = {}
    for window in (0x1001, 0x1002, 0x1003):
        rig.events.clear()
        rig.run(0x4208E0, window)
        check([event[:5] for event in rig.events], [('alloc', 12, '.?AUWNDREC@@', 0xFFFFFFFE, 8)], 'window record is 12 bytes of tag .?AUWNDREC@@')
        records[window] = rig.events[0][5]
        check(machine.r32(records[window] + 8), window, 'record stores the window')
        check([machine.r32(r + 8) for r in list_walk(machine, WND_HEAD)], list(records), 'new records append at the tail')
        check(machine.r32(WND_HEAD + 4), records[0x1001], 'first record is the list head item')
    check(machine.r32(WND_HEAD), records[0x1003] + 0, 'head link points back at the tail record')
    for window, record in records.items():
        check(rig.run(0x4209E0, ecx=window), record, 'lookup by window')
    check(rig.run(0x4209E0, ecx=0x9999), 0, 'lookup miss')
    # ---- register: handler in ESI, window in EDI, stack (tag, code)
    machine.last_error = 0
    events.clear()
    rig.events.clear()
    check(rig.run(0x420A70, 0x534D5347, 7, regs={'esi': 0, 'edi': 0x1001}), 0, 'register rejects a null handler')
    check((machine.last_error, events, rig.events), (0x57, [], []), 'null handler: error 0x57, nothing registered or created')
    check(rig.run(0x420A70, 0x534D5348, 9, regs={'esi': 0xAAAA, 'edi': 0x1002}), 0x1D, 'register passes the event-table result through')
    check((events, rig.events), ([(0x534D5348, 0x1002, 9, 0, 0xAAAA)], []), 'known window: only the event-table call, with tag, window, code, 0, handler')
    events.clear()
    check(rig.run(0x420A70, 0x534D5349, 3, regs={'esi': 0xBBBB, 'edi': 0x2002}), 0x1D, 'register of a new window')
    check([event[:3] for event in rig.events], [('alloc', 12, '.?AUWNDREC@@')], 'unknown window gets a record first')
    check(events, [(0x534D5349, 0x2002, 3, 0, 0xBBBB)], 'then the event-table call')
    records[0x2002] = rig.events[0][5]
    # ---- remove: ECX = list, stack record; returns the next record or 0
    order = list(records)
    for victim in (0x1002, 0x1001, 0x2002, 0x1003):
        position = order.index(victim)
        nxt = records[order[position + 1]] if position + 1 < len(order) else 0
        rig.events.clear()
        check(rig.run(0x420970, records[victim], ecx=WND_LIST), nxt, 'remove returns the following record')
        order.remove(victim)
        check([machine.r32(r + 8) for r in list_walk(machine, WND_HEAD)], order, 'removed record leaves the list in order')
        check([(event[1], event[2], event[3], event[4]) for event in rig.events], [(records[victim], '.?AUWNDREC@@', 0xFFFFFFFE, 0)], 'removed record is freed with its tag')
    check((machine.r32(WND_HEAD), machine.r32(WND_HEAD + 4)), (WND_HEAD, (~WND_HEAD) & 0xFFFFFFFF), 'emptied list is back to its constructed state')
    print('  window cases ok')


def main():
    """Run every group and optional negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--mutation-check', action='store_true')
    args = parser.parse_args()
    check(hashlib.sha256(args.exe.read_bytes()).hexdigest(), SHA256, 'game.exe 1.14d SHA-256')
    for group in GROUPS:
        group(args.exe)
        print(f'{group.__name__}: PASS', flush=True)
    for address, count in COUNTS.items():
        if count == 0:
            raise AssertionError(f'unexercised target {address:08x}')
        print(f'{address:08x}: {count} calls')
    print(f'PASS: {len(COUNTS)} functions, {sum(COUNTS.values())} calls; stock code with scoped hooks (see the module docstring)')
    if args.mutation_check:
        global MUTATION
        probes = ((file_accessors, 'archive seek position'),
                  (file_accessors, 'loose size: OS handle, high word zeroed first'),
                  (file_accessors, 'file name copy is truncated with a NUL, guard bytes intact'),
                  (open_and_close, 'archive record: stream, archive, entry, file key, sector count'),
                  (open_and_close, 'close of a shared handle unlinks and decrements'),
                  (read_file, 'sync read data and count'),
                  (read_file, 'queued request arguments'),
                  (read_file, 'checksum mismatch is reported once, match is silent'),
                  (message_text, 'FormatMessage flags: module table when the facility is registered, else system text'),
                  (window_records, 'removed record leaves the list in order'))
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


GROUPS = (file_accessors, open_and_close, read_file, message_text, window_records)

if __name__ == '__main__':
    main()
