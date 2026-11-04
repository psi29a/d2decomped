# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's item data accessors on synthetic items.

Usage: uv run python item_accessors.py
"""
import emu

HIGH = 0x5A5A0000


def w16(machine, address, value):
    machine.mu.mem_write(address, (value & 0xFFFF).to_bytes(2, "little"))


def main():
    machine = emu.Emu()
    unit, data = machine.alloc(0x20), machine.alloc(0x60)

    def select(kind=4, with_data=True):
        machine.w32(unit, kind)
        machine.w32(unit + 0x14, data if with_data else 0)

    def expect(actual, expected, label):
        if actual != expected:
            raise AssertionError(f"{label}: got {actual:08x}, want {expected:08x}")

    def read16(address, offset, args=()):
        select()
        for value in (0, 1, 0x7FFF, 0x8000, 0xFFFF):
            w16(machine, data + offset, value)
            actual = machine.call(address, unit, *args, regs={"eax": HIGH})
            expect(actual, (data & 0xFFFF0000) | value, f"{address:08x} read {offset:#x}")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            actual = machine.call(address, unit, *args, regs={"eax": HIGH})
            expected = unit & 0xFFFF0000 if kind != 4 else 0
            expect(actual, expected, f"{address:08x} rejected read")
        actual = machine.call(address, 0, *args, regs={"eax": HIGH})
        expect(actual, 0, f"{address:08x} null read")

    def read32(address, offset):
        select()
        for value in (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x89ABCDEF):
            machine.w32(data + offset, value)
            expect(machine.call(address, unit), value, f"{address:08x} read {offset:#x}")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            expect(machine.call(address, unit), 0, f"{address:08x} rejected read")
        expect(machine.call(address, 0), 0, f"{address:08x} null read")

    def mask32(address, offset):
        select()
        for mask in (0, 1, 0x0F0F0F0F, 0x80000000, 0xFFFFFFFF):
            for enabled in (0, 1, 2, 0xFFFFFFFF):
                initial = 0xA5A55A5A
                machine.w32(data + offset, initial)
                machine.call(address, unit, mask, enabled)
                expected = initial | mask if enabled else initial & (~mask & 0xFFFFFFFF)
                expect(machine.r32(data + offset), expected, f"{address:08x} mask {offset:#x}")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            before = machine.read(data, 0x60)
            machine.call(address, unit, 0xFFFFFFFF, 1)
            if machine.read(data, 0x60) != before:
                raise AssertionError(f"{address:08x} rejected mask changed item data")
        machine.call(address, 0, 0xFFFFFFFF, 1)

    def write16(address, offset, args=()):
        select()
        for value in (0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x12345678):
            w16(machine, data + offset, 0xA55A)
            machine.call(address, unit, value, *args)
            expect(machine.r16(data + offset), value & 0xFFFF, f"{address:08x} write {offset:#x}")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            before = machine.read(data, 0x60)
            machine.call(address, unit, 0xFFFF, *args)
            if machine.read(data, 0x60) != before:
                raise AssertionError(f"{address:08x} rejected write changed item data")
        machine.call(address, 0, 0xFFFF, *args)

    def write8(address, offset):
        select()
        for value in (0, 1, 0x7F, 0x80, 0xFF, 0x12345678):
            machine.mu.mem_write(data + offset, b"\xa5")
            machine.call(address, unit, value)
            actual = machine.read(data + offset, 1)[0]
            expect(actual, value & 0xFF, f"{address:08x} write {offset:#x}")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            before = machine.read(data, 0x60)
            machine.call(address, unit, 0xFF)
            if machine.read(data, 0x60) != before:
                raise AssertionError(f"{address:08x} rejected write changed item data")
        machine.call(address, 0, 0xFF)

    def item_level_get():
        select()
        for value in (0x80000000, 0xFFFFFFFF, 0, 1, 2, 0x7FFFFFFF):
            machine.w32(data + 0x2C, value)
            expected = 1 if value == 0 or value & 0x80000000 else value
            expect(machine.call(0x6281E0, unit), expected, "item level read")
            expect(machine.r32(data + 0x2C), expected, "item level clamp")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            before = machine.read(data, 0x60)
            expect(machine.call(0x6281E0, unit), 1, "item level rejected read")
            if machine.read(data, 0x60) != before:
                raise AssertionError("item level rejected read changed item data")
        expect(machine.call(0x6281E0, 0), 1, "item level null read")

    def ear_name_get():
        select()
        expect(machine.call(0x628340, unit), data + 0x4A, "ear name pointer")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            expect(machine.call(0x628340, unit), 0, "ear name rejected read")
        expect(machine.call(0x628340, 0), 0, "ear name null read")

    def quality_get():
        select()
        for value in (0, 1, 2, 9, 0x89ABCDEF):
            machine.w32(data, value)
            expect(machine.call(0x627E70, unit), value, "quality read")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            expect(machine.call(0x627E70, unit), 2, "quality rejected read")
        expect(machine.call(0x627E70, 0), 2, "quality null read")

    def quality_set():
        select()
        for value in (0, 1, 2, 9, 0x89ABCDEF):
            machine.call(0x627EA0, unit, value)
            expect(machine.r32(data), value, "quality write")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            before = machine.read(data, 0x60)
            machine.call(0x627EA0, unit, 9)
            if machine.read(data, 0x60) != before:
                raise AssertionError("quality rejected write changed item data")
        machine.call(0x627EA0, 0, 9)

    quality_get()
    quality_set()
    for index in range(3):
        read16(0x627EC0, 0x38 + index * 2, (index,))
        write16(0x627EF0, 0x38 + index * 2, (index,))
    read16(0x627F20, 0x36)
    write16(0x627F50, 0x36)
    for index in range(3):
        write16(0x627FB0, 0x3E + index * 2, (index,))
    read16(0x627FE0, 0x32)
    write16(0x628010, 0x32)
    read16(0x628040, 0x34)
    write16(0x628070, 0x34)
    read32(0x628110, 0x18)
    mask32(0x628170, 0x14)
    item_level_get()
    write8(0x628280, 0x45)
    write8(0x628320, 0x47)
    ear_name_get()
    print("item accessors: all game.exe checks passed")


if __name__ == "__main__":
    main()
