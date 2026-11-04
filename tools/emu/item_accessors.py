# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's item accessors and helpers on synthetic records.

Usage: uv run python item_accessors.py
"""
import emu

HIGH = 0x5A5A0000


def w16(machine, address, value):
    machine.mu.mem_write(address, (value & 0xFFFF).to_bytes(2, "little"))


def main():
    machine = emu.Emu()
    unit, data = machine.alloc(0x20), machine.alloc(0x60)
    record = machine.alloc(0x1A8)
    machine.hook(0x6335F0, lambda current: record if current.arg(0) == 7 else 0, 1)

    def select(kind=4, with_data=True):
        unit_view = machine.view("unit", unit)
        unit_view.type = kind
        unit_view.data = data if with_data else 0

    def select_record(class_id=7):
        select()
        machine.view("unit", unit)["class"] = class_id

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

    def ear_name_set():
        source = machine.alloc(0x20)
        select()
        for text in (b"\0", b"Earl\0", b"Kimi\0"):
            machine.mu.mem_write(source, text)
            machine.mu.mem_write(data + 0x4A, b"\xa5" * 0x16)
            before = machine.read(data, 0x60)
            machine.call(0x628370, unit, source)
            expected = before[:0x4A] + text + before[0x4A + len(text):]
            if machine.read(data, 0x60) != expected:
                raise AssertionError(f"ear name write failed for {text!r}")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            before = machine.read(data, 0x60)
            machine.call(0x628370, unit, source)
            if machine.read(data, 0x60) != before:
                raise AssertionError("ear name rejected write changed item data")
        machine.call(0x628370, 0, source)

    def ear_class_get():
        select()
        for value in (0, 1, 0x7F, 0x80, 0xFF):
            machine.mu.mem_write(data + 0x48, bytes([value]))
            actual = machine.call(0x6283A0, unit, regs={"eax": HIGH})
            expect(actual, (data & 0xFFFFFF00) | value, "ear class read")
        for kind, with_data in ((3, True), (4, False)):
            select(kind, with_data)
            actual = machine.call(0x6283A0, unit, regs={"eax": HIGH})
            expected = (unit & 0xFFFFFF00) | 1 if kind != 4 else 1
            expect(actual, expected, "ear class rejected read")
        expect(machine.call(0x6283A0, 0, regs={"eax": HIGH}), 1, "ear class null read")

    def record_c0_get():
        for value in (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x89ABCDEF):
            select_record()
            machine.view("item_record", record).field_c0 = value
            expect(machine.call(0x628500, unit), value, "item record +0xc0 read")
        select(3)
        expect(machine.call(0x628500, unit), 0, "item record +0xc0 wrong type")
        select_record(8)
        expect(machine.call(0x628500, unit), 0, "item record +0xc0 missing record")
        expect(machine.call(0x628500, 0), 0, "item record +0xc0 null read")

    def item_code_get():
        select_record()
        for value in (0, 1, 0x20786F62, 0x2064736C, 0xFFFFFFFF):
            machine.view("item_record", record).code = value
            expect(machine.call(0x628590, unit), value, "item code read")

    def record_pair_get():
        first, second = machine.alloc(1), machine.alloc(1)
        for value in (0, 1, 0x7F, 0x80, 0xFF):
            select_record()
            record_view = machine.view("item_record", record)
            record_view.field_10f = value
            record_view.field_110 = 0xFF - value
            machine.call(0x6286C0, unit, first, second, 0, 0)
            expect(machine.read(first, 1)[0], value, "item record +0x10f output")
            expect(machine.read(second, 1)[0], 0xFF - value, "item record +0x110 output")
        for rejected in (lambda: select(3), lambda: select_record(8)):
            rejected()
            machine.mu.mem_write(first, b"\xa5")
            machine.mu.mem_write(second, b"\xa5")
            machine.call(0x6286C0, unit, first, second, 0, 0)
            expect(machine.read(first, 1)[0], 0xA5, "item record pair rejected output")
            expect(machine.read(second, 1)[0], 0xA5, "item record pair rejected output")
        machine.mu.mem_write(first, b"\xa5")
        machine.mu.mem_write(second, b"\xa5")
        machine.call(0x6286C0, 0, first, second, 0, 0)
        expect(machine.read(first, 1)[0], 0xA5, "item record pair null output")
        expect(machine.read(second, 1)[0], 0xA5, "item record pair null output")

    def base_level_get():
        select_record()
        for value in (0, 1, 0x7F, 0x80, 0xFF):
            machine.view("item_record", record).level = value
            actual = machine.call(0x628740, unit, regs={"eax": HIGH})
            expect(actual, (record & 0xFFFFFF00) | value, "base item level read")

    def bit_and():
        for left in (0, 1, 0x0F0F0F0F, 0x80000000, 0xFFFFFFFF, 0x89ABCDEF):
            for right in (0, 1, 0x33333333, 0x80000000, 0xFFFFFFFF):
                expect(machine.call(0x6287A0, left, right), left & right, "bitwise AND")

    def bitmask_set():
        target = machine.alloc(4)
        for mask in (0, 1, 0x0F0F0F0F, 0x80000000, 0xFFFFFFFF):
            for enabled in (0, 1, 2, 0xFFFFFFFF):
                initial = 0xA5A55A5A
                machine.w32(target, initial)
                machine.call(0x6287B0, target, mask, enabled)
                expected = initial | mask if enabled else initial & (~mask & 0xFFFFFFFF)
                expect(machine.r32(target), expected, "item bitmask update")

    def normal_code_get():
        select_record()
        for code, normcode in ((0x20786F62, 0), (0x2064736C, 0x2070636D),
                               (0, 0xFFFFFFFF), (0x89ABCDEF, 0x12345678)):
            record_view = machine.view("item_record", record)
            record_view.code = code
            record_view.normcode = normcode
            expected = normcode if normcode else code
            expect(machine.call(0x6287D0, unit), expected, "item normal code read")

    def affix_level_clamp():
        affix = machine.alloc(0x90)
        initials = (0, 1, 0x7F, 0x80, 0xFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF)
        for initial in initials:
            actual = machine.call(0x628830, ecx=0, edx=unit, regs={"eax": initial})
            expect(actual, initial, "null affix level clamp")
        scenarios = ((0xFF, 3, 50, 90), (3, None, 50, 90),
                     (3, 4, 50, 90), (3, 3, 50, 90))
        for affix_class, player_class, level, class_level in scenarios:
            affix_view = machine.view("affix", affix)
            affix_view.level = level
            affix_view["class"] = affix_class
            affix_view.class_level = class_level
            selected = class_level if player_class == affix_class != 0xFF else level
            if player_class is not None:
                machine.view("unit", unit)["class"] = player_class
            for initial in initials:
                actual = machine.call(0x628830, ecx=affix, edx=unit if player_class is not None else 0,
                                      regs={"eax": initial})
                signed_initial = initial - 0x100000000 if initial & 0x80000000 else initial
                expected = initial if signed_initial > selected else selected
                expect(actual, expected, "affix required level clamp")

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
    ear_name_set()
    write8(0x6283D0, 0x48)
    ear_class_get()
    write8(0x628420, 0x49)
    record_c0_get()
    item_code_get()
    record_pair_get()
    base_level_get()
    bit_and()
    bitmask_set()
    normal_code_get()
    affix_level_clamp()
    print("item accessors: all game.exe checks passed")


if __name__ == "__main__":
    main()
