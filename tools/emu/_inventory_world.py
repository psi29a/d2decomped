# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared fixtures for the Inventory.cpp oracles (inventory_*.py): a counted check, a signed helper, and an
arena of fake inventories, grids and items whose bytes a Python model can mirror and compare with game.exe's.

An item is a fake unit (type 4 at +0, class +4, id +0xc, item data +0x14, path +0x2c) with item data
(inventory +0x5c, list prev +0x60 / next +0x64, grid byte +0x68, +0x69, grid prev +0x6c / next +0x70) and a path
(x at +0xc, y at +0x10). An inventory is 0x40 bytes (magic 0x1020304) with 16-byte grids at +0x14.
"""
import random

import emu

MAGIC = 0x1020304
CASES = 0


def check(actual, expected, label):
    """Count one case; raise with the label when game.exe's answer differs from the model."""
    global CASES
    CASES += 1
    if actual != expected:
        show = (lambda v: f"{v:#x}" if isinstance(v, int) else repr(v))
        raise AssertionError(f"{label}: game.exe {show(actual)}, model {show(expected)}")


def cases():
    """How many checks have run."""
    return CASES


def signed(value):
    """A 32-bit value as a signed int."""
    return value - (1 << 32) if value & 0x80000000 else value


class Mem:
    """A byte-for-byte copy of the arena that a model reads and writes at the same absolute addresses."""

    def __init__(self, base, data):
        self.base, self.data = base, bytearray(data)

    def r8(self, a):
        return self.data[a - self.base]

    def w8(self, a, v):
        self.data[a - self.base] = v & 0xFF

    def r32(self, a):
        return int.from_bytes(self.data[a - self.base:a - self.base + 4], "little")

    def w32(self, a, v):
        self.data[a - self.base:a - self.base + 4] = (v & 0xFFFFFFFF).to_bytes(4, "little")


class Arena:
    """One zeroed region of emulator memory that fake objects are carved from, so its whole content can be compared."""

    def __init__(self, e, size=0x10000):
        self.e, self.size = e, size
        self.base = e.alloc(size)
        self.reset()

    def reset(self):
        """Zero the region and start carving again from its top."""
        self.e.mu.mem_write(self.base, b"\0" * self.size)
        self.top = 0x10

    def new(self, n):
        """A 16-aligned zeroed block."""
        addr = self.base + self.top
        self.top = (self.top + n + 15) & ~15
        assert self.top < self.size, "arena full"
        return addr

    def snapshot(self):
        """A model-side copy of the region now."""
        return Mem(self.base, self.e.read(self.base, self.size))

    def w32(self, a, v):
        self.e.w32(a, v)

    def inventory(self):
        """A bare inventory record."""
        inv = self.new(0x40)
        self.e.w32(inv, MAGIC)
        return inv

    def item(self, x=0, y=0, kind=4, inv=0):
        """A fake item unit; x and y go in its path."""
        e = self.e
        unit, data, path = self.new(0x60), self.new(0x80), self.new(0x20)
        e.w32(unit, kind)
        e.w32(unit + 4, random.getrandbits(8))
        e.w32(unit + 0xC, random.getrandbits(30) | 0x100)
        e.w32(unit + 0x14, data)
        e.w32(unit + 0x2C, path)
        e.w32(path + 0xC, x)
        e.w32(path + 0x10, y)
        e.w32(data + 0x5C, inv)
        return unit

    def data(self, item):
        """The item data pointer of a fake item."""
        return self.e.r32(item + 0x14)

    def grid(self, width, height, filled=True):
        """A 16-byte grid descriptor with its width x height cell array, cells random non-zero words when filled."""
        e = self.e
        grid = self.new(0x10)
        cells = self.new(4 * width * height)
        e.mu.mem_write(grid + 8, bytes([width, height]))
        e.w32(grid + 0xC, cells)
        if filled:
            for n in range(width * height):
                e.w32(cells + 4 * n, random.getrandbits(32) | 1)
        return grid
