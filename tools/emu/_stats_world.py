# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared harness for the stat-list oracles (stats_*.py): not an oracle itself.

It builds one emulated game.exe 1.14d with a fake ItemStatCost table, hooks the
allocator entries (realloc 0x40_b4b0, free 0x40_b480) and gives each oracle
three things: `World` (the machine, the list images and the call log), `L` (a
Python image of one stat list) and `run` (one bounded stock call that checks
the return and the bytes the callee popped).

A stat list is the 0x70-byte record at unit +0x5c (docs/research/re/stats.md).
`L.f` holds its scalar fields by offset; `L.arr` holds the three sorted arrays
(plain at +0x24, expanded at +0x48, changed-stat queue at +0x50) as Python
lists and `L.cap` their capacities. Arrays live in 1 KiB reserve buffers, so
the realloc hook returns the same pointer and only the log shows a resize.
"""
import copy
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TABLE_PTR = 0x744304
MASK_STAT = 0x6CE270       # byte masks the ItemStatCost flag bytes are tested against
MASK_CALLBACK = 0x6CE274
MASK_QUEUE = 0x6CE278
REALLOC = 0x40_B4B0
FREE = 0x40_B480
STATS_FILE = 0x6E90A4
ROWS = 170
ROW_SIZE = 0x144
RESERVE = 0x400
LIST_SIZE = 0x70
INSTRUCTION_LIMIT = 20000
SCALARS = tuple(offset for offset in range(0, 0x64, 4) if offset not in (0x24, 0x28, 0x48, 0x4C, 0x50, 0x54))
# name -> (pointer offset, count offset, capacity offset, entry size)
ARRAYS = {"plain": (0x24, 0x28, 0x2A, 8), "exp": (0x48, 0x4C, 0x4E, 8), "queue": (0x50, 0x54, 0x56, 4)}
EXPANDED = 0x80000000


class Mismatch(Exception):
    """The emulated function and the model disagree."""


def require(condition, message):
    """Stop the probe when an observation is absent or differs."""
    if not condition:
        raise Mismatch(message)


def s32(value):
    """A dword as a signed integer."""
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def s16(value):
    """A word as a signed integer."""
    value &= 0xFFFF
    return value - (1 << 16) if value & 0x8000 else value


class L:
    """A Python image of one stat list: scalar fields, three arrays and their capacities."""

    def __init__(self):
        """An empty list: every scalar zero, no entries, no capacity."""
        self.f = {offset: 0 for offset in SCALARS}
        self.arr = {name: [] for name in ARRAYS}
        self.cap = {name: 0 for name in ARRAYS}

    def copy(self):
        """A deep copy the model can change."""
        return copy.deepcopy(self)

    def __eq__(self, other):
        """Equal when every field, entry and capacity matches."""
        return self.f == other.f and self.arr == other.arr and self.cap == other.cap

    def diff(self, other):
        """A short description of the first difference from `other`."""
        for offset in SCALARS:
            if self.f[offset] != other.f[offset]:
                return f"field +{offset:#x}: {self.f[offset]:#x} != {other.f[offset]:#x}"
        for name in ARRAYS:
            if self.arr[name] != other.arr[name]:
                return f"array {name}: {self.arr[name]!r} != {other.arr[name]!r}"
            if self.cap[name] != other.cap[name]:
                return f"capacity {name}: {self.cap[name]} != {other.cap[name]}"
        return "equal"


class World:
    """One emulated game.exe with a fake stat table, hooked allocator and list images."""

    def __init__(self):
        """Build the machine, the fake ItemStatCost table and the allocator hooks."""
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        digest = hashlib.sha256(exe.read_bytes()).hexdigest()
        require(digest == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
        self.m = machine = emu.Emu(str(exe))
        self.log = []
        self.models = {}
        self.stock_masks = tuple(machine.read(a, 1)[0] for a in (MASK_STAT, MASK_CALLBACK, MASK_QUEUE))
        self.tables = machine.alloc(0xD00)
        self.rows = machine.alloc(ROWS * ROW_SIZE)
        machine.w32(TABLE_PTR, self.tables)
        machine.w32(self.tables + 0xBCC, self.rows)
        machine.w32(self.tables + 0xBD4, ROWS)
        machine.hook(REALLOC, self.realloc, 4)
        machine.hook(FREE, self.free, 3)

    def realloc(self, current):
        """Allocator resize: ECX pool, EDX old pointer; log it, keep the pointer inside its reserve."""
        pool = current.mu.reg_read(UC_X86_REG_ECX)
        old = current.mu.reg_read(UC_X86_REG_EDX)
        size, line = current.arg(0), current.arg(2)
        self.log.append(("realloc", pool, old != 0, size, line, current.cstr(current.arg(1))))
        require(size <= RESERVE, f"realloc past the reserve: {size}")
        return old if old else current.alloc(RESERVE)

    def free(self, current):
        """Allocator release: ECX pool, EDX pointer; only logged (the emulator never frees)."""
        pool = current.mu.reg_read(UC_X86_REG_ECX)
        old = current.mu.reg_read(UC_X86_REG_EDX)
        self.log.append(("free", pool, old != 0, current.arg(1), current.cstr(current.arg(0))))
        return 0

    def slot(self, fn, nstack):
        """A fresh code address that runs `fn(emu) -> eax` and pops `nstack` args (a callback target)."""
        machine = self.m
        address = emu.STUB + 16 * (len(machine.slots) + 1)
        machine.slots[address] = ("slot", lambda current: (fn(current), nstack))
        return address

    def set_masks(self, stat, callback, queue):
        """Write the three ItemStatCost flag masks."""
        for address, value in ((MASK_STAT, stat), (MASK_CALLBACK, callback), (MASK_QUEUE, queue)):
            self.m.mu.mem_write(address, bytes([value]))

    def restore_masks(self):
        """Put the stock mask bytes back."""
        self.set_masks(*self.stock_masks)

    def set_row(self, index, **fields):
        """Write bytes of ItemStatCost row `index`: keyword `bNN` a byte at +NN (hex), `wNN` a word, `dNN` a dword."""
        base = self.rows + index * ROW_SIZE
        for name, value in fields.items():
            offset = int(name[1:], 16)
            packed = struct.pack({"b": "<B", "w": "<H", "d": "<I"}[name[0]], value & (1 << 8 * {"b": 1, "w": 2, "d": 4}[name[0]]) - 1)
            self.m.mu.mem_write(base + offset, packed)

    def row(self, index):
        """The emulated address of ItemStatCost row `index`."""
        return self.rows + index * ROW_SIZE

    def clear_rows(self):
        """Zero every ItemStatCost row."""
        self.m.mu.mem_write(self.rows, b"\0" * (ROWS * ROW_SIZE))

    def row_bytes(self, index):
        """The 0x144 bytes of a row."""
        return self.m.read(self.row(index), ROW_SIZE)

    def install(self, image, address=None):
        """Write `image` to emulator memory at `address` (new when None); registers it as a model."""
        machine = self.m
        address = address or machine.alloc(LIST_SIZE)
        machine.mu.mem_write(address, b"\0" * LIST_SIZE)
        for offset in SCALARS:
            machine.w32(address + offset, image.f[offset])
        for name, (pointer, count, _capacity, size) in ARRAYS.items():
            entries = image.arr[name]
            if image.cap[name] or entries:
                require(len(entries) <= image.cap[name], "image holds more entries than its capacity")
                buffer = machine.alloc(RESERVE)
                data = b"".join(struct.pack("<II", *e) if size == 8 else struct.pack("<I", e) for e in entries)
                machine.mu.mem_write(buffer, data)
                machine.w32(address + pointer, buffer)
            machine.mu.mem_write(address + count, struct.pack("<hH", len(entries), image.cap[name]))
        self.models[address] = image.copy()
        return address

    def read_list(self, address):
        """Decode the list at `address` back into an image."""
        machine = self.m
        image = L()
        for offset in SCALARS:
            image.f[offset] = machine.r32(address + offset)
        for name, (pointer, count, _capacity, size) in ARRAYS.items():
            header = machine.read(address + count, 4)
            n, cap = s16(int.from_bytes(header[:2], "little")), int.from_bytes(header[2:], "little")
            image.cap[name] = cap
            if n:
                data = machine.read(machine.r32(address + pointer), n * size)
                words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
                image.arr[name] = list(zip(words[0::2], words[1::2])) if size == 8 else words
        return image

    def check_lists(self, what):
        """Every installed list in memory equals its model; reports the first difference."""
        for address, model in self.models.items():
            actual = self.read_list(address)
            require(actual == model, f"{what}: list {address:#x} {actual.diff(model)}")

    def run(self, address, *args, nstack=None, **kwargs):
        """One bounded stock call; returns (eax, bytes popped by the callee beyond the return address)."""
        machine = self.m
        sp0 = emu.STACK + emu.STACK_SIZE - 0x1000 - 4 * (len(args) + 1)
        self.log.clear()
        value = machine.call(address, *args, limit=INSTRUCTION_LIMIT, **kwargs)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x} did not return")
        popped = machine.mu.reg_read(UC_X86_REG_ESP) - sp0 - 4
        if nstack is not None:
            require(popped == 4 * nstack, f"{address:#x} popped {popped} bytes, expected {4 * nstack}")
        return value, popped


def normal_entries(rng, count, stats=32, layers=3):
    """`count` sorted unique {key, value} entries with stat ids below `stats` and layers below `layers`."""
    keys = sorted(rng.sample([(s << 16) | layer for s in range(stats) for layer in range(layers)], count))
    return [(key, rng.choice((0, 1, 5, 100, 0xFFFFFFFF, 0xFFFFFF00, 0x7FFFFFFF, rng.randrange(1 << 32)))) for key in keys]
