# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs 47 of the functions in docs/research/re/frontier-ring.md in stock
game.exe and checks each against the reading written there.

    uv run python frontier_ring.py

Each check sets up only what the function reads (a global, a fake unit,
a fake game's unit tables, a fake data-table block at PTR 0x744304) and
hooks the callees the reading names, so a run shows the function's own
body. A failing check prints the function, what game.exe did and what
the reading says, and the script exits 1.

Two findings the checks pin down:
- FUN_004c5bd0 (the room reach test of the area search FUN_004c5c40)
  never answers 0 for a radius >= 0: each axis rejects only a point left
  of the room's left edge and right of its right edge at once.
- FUN_004c9d70 tests the same byte (+5 of the first record) on every pass
  of its loop, so it answers -1 or 7, never 0..6.
"""
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

# gaps-runs: 004f65e0 00407360 00449e50 0044bf30 0045c460 00457440 00473cf0 00475ff0 00477030
# gaps-runs: 00498d50 00498d60 0049c760 0049da30 0049da50 0049e8b0 004b2640 004b2a00 004b2a20
# gaps-runs: 004b2a40 004b2a60 004b3c00 004b2e20 004b8a10 004b9be0 004c5bd0 004c76d0 004c7bd0
# gaps-runs: 004c9d70 00501690 00534a50 0053ca90 0053dfb0 00552a40 00552db0 005530f0 00557f10
# gaps-runs: 00573640 0059f840 0063e8d0 0064a4e0 0064a510 0064a540 0064a570 0064c260 0064fe10
# gaps-runs: 006617b0 006c8a50

TABLES_PTR = 0x744304           # PTR_DAT_00744304: the data tables block
MONSTATS_ROWS, MONSTATS_COUNT, MONSTATS_SIZE = 0xA78, 0xA80, 0x1A8
UNIT_NEXT_IN_HASH = 0xE4
HASH_BY_TYPE = {0: 0x1120, 1: 0x1320, 2: 0x1520, 3: 0x1920, 4: 0x1720}   # 0x6e10e0, type 5 at +0x1b20

failures = []
checked = set()


def check(address, what, got, want):
    checked.add(address)
    if got != want:
        failures.append(f"FUN_{address:08x} {what}: game.exe {got!r}, reading {want!r}")


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


def w16(machine, address, value):
    machine.mu.mem_write(address, struct.pack("<H", value & 0xFFFF))


def w8(machine, address, value):
    machine.mu.mem_write(address, bytes([value & 0xFF]))


def r8(machine, address):
    return machine.read(address, 1)[0]


def fake_tables(machine, monstats_count):
    block = machine.alloc(0x1000)
    rows = machine.alloc(MONSTATS_SIZE * monstats_count)
    machine.w32(block + MONSTATS_ROWS, rows)
    machine.w32(block + MONSTATS_COUNT, monstats_count)
    machine.w32(TABLES_PTR, block)
    return rows


def unit(machine, unit_type, class_id=0, unit_id=0, mode=0):
    address = machine.alloc(0x100)
    machine.w32(address + 0, unit_type)
    machine.w32(address + 4, class_id)
    machine.w32(address + 0xC, unit_id)
    machine.w32(address + 0x10, mode)
    return address


def globals_getters(machine):
    machine.w32(0x72DB44, 0x1234)
    check(0x4F65E0, "returns DAT_0072db44", machine.call(0x4F65E0), 0x1234)
    for value, want in ((0, 0), (5, 0x74D88C)):
        machine.w32(0x74D88C, value)
        check(0x407360, f"DAT_0074d88c={value}", machine.call(0x407360), want)
    machine.call(0x449E50, 0x55)
    check(0x449E50, "stores its stack arg", machine.r32(0x798F7C), 0x55)
    machine.w32(0x7A0650, 0x77)
    check(0x44BF30, "returns DAT_007a0650", machine.call(0x44BF30), 0x77)
    fake_tables(machine, 0x123)
    check(0x45C460, "MonStats row count", machine.call(0x45C460), 0x123)
    machine.w32(0x7C9148, machine.alloc(0x20))
    machine.w32(machine.r32(0x7C9148) + 0x14, 0xBEEF)
    check(0x6C8A50, "*(DAT_007c9148 + 0x14)", machine.call(0x6C8A50), 0xBEEF)
    for address in (0x457440, 0x473CF0):
        before = machine.read(0x700000, 0x100000)
        machine.call(address)
        check(address, "empty", machine.read(0x700000, 0x100000) == before, True)


def ui_flags(machine):
    flags = machine.alloc(4)
    machine.w32(flags, 0x10)
    machine.call(0x475FF0, ecx=flags)
    check(0x475FF0, "ECX dword |= 1", machine.r32(flags), 0x11)
    for index in range(6):
        machine.mu.mem_write(0x7BEFB0, b"\0" * 8)
        machine.call(0x498D50, ecx=index, edx=0xA0 + index)
        want = [0] * 8
        if index < 4:
            want[index] = 0xA0 + index
        check(0x498D50, f"index {index}", list(machine.read(0x7BEFB0, 8)), want)
    for address in (0x72235C, 0x722360, 0x722364, 0x7BEF94, 0x7BEFA8, 0x7BEFAC):
        machine.w32(address, 0x5A5A)
    machine.call(0x498D60)
    check(0x498D60, "belt hover reset", [machine.r32(a) for a in (0x72235C, 0x722360, 0x722364)], [0xFFFFFFFF] * 3)
    check(0x498D60, "flags cleared", [machine.r32(a) for a in (0x7BEF94, 0x7BEFA8, 0x7BEFAC)], [0, 0, 0])
    hovered = unit(machine, 1, unit_id=0x42)
    machine.call(0x49DA30, ecx=hovered)
    check(0x49DA30, "unit id and flag", (r8(machine, 0x7BF23A), machine.r32(0x7BF23B)), (1, 0x42))
    machine.call(0x49DA30, ecx=0)
    check(0x49DA30, "null clears the flag", r8(machine, 0x7BF23A), 0)
    w8(machine, 0x7BF23A, 1)
    machine.call(0x49DA50)
    check(0x49DA50, "clears the flag", r8(machine, 0x7BF23A), 0)
    machine.w32(0x7BCC0C, 0)
    machine.call(0x4B2640)
    check(0x4B2640, "DAT_007bcc0c = 1", machine.r32(0x7BCC0C), 1)
    for value in (0, 1, 3):
        w8(machine, 0x7C0C6B, value)
        check(0x4B2E20, f"DAT_007c0c6b={value}", machine.call(0x4B2E20), int(value != 0))
    for value in range(4):
        machine.w32(0x7C0E7C, value)
        check(0x4B8A10, f"DAT_007c0e7c={value}", machine.call(0x4B8A10), int(value in (1, 2)))


def hover_unit_find(machine):
    seen = []

    def find(emulator):
        seen.append((emulator.mu.reg_read(UC_X86_REG_ECX), emulator.mu.reg_read(UC_X86_REG_EDX)))
        return 0x5150
    machine.hook(0x463990, find, 0)
    machine.w32(0x7BF202, 11)
    machine.w32(0x7BF206, 22)
    for first, second, want_seen, want in ((1, 0, (11, 1), 0x5150), (0, 1, (22, 2), 0x5150), (0, 0, None, 0)):
        seen.clear()
        machine.w32(0x7BF20A, first)
        machine.w32(0x7BF1C0, second)
        got = machine.call(0x49E8B0)
        check(0x49E8B0, f"flags {first},{second}", (got, seen[0] if seen else None), (want, want_seen))


def npc_item_watchers(machine):
    calls = []
    machine.hook(0x4B2940, lambda emulator: calls.append(1) or 0, 0)
    for address, wanted_kind in ((0x4B2A00, 1), (0x4B2A20, 1), (0x4B2A40, 0), (0x4B2A60, 0), (0x4B3C00, 0)):
        for active in (0, 1):
            for kind in (0, 1):
                for matches in (0, 1):
                    calls.clear()
                    machine.w32(0x7C0D96, active)
                    w8(machine, 0x7C0D89, kind)
                    machine.w32(0x7C0D8E, 0x99)
                    machine.call(address, ecx=0x99 if matches else 0x98)
                    want = int(active and kind == wanted_kind and matches)
                    check(address, f"active {active} kind {kind} id match {matches}", len(calls), want)


def palshift_find(machine):
    nodes = []
    for key in (5, 7, 9):
        node = machine.alloc(0x1008)
        machine.w32(node, key)
        nodes.append(node)
    for node, after in zip(nodes, nodes[1:] + [0]):
        machine.w32(node + 0x1004, after)
    machine.w32(0x7B957C, nodes[0])
    for key, want in ((5, nodes[0]), (9, nodes[2]), (8, 0)):
        check(0x477030, f"key {key}", machine.call(0x477030, ecx=key), want)


def waypoint_tab(machine):
    count = signed(machine.r32(0x7224E4))
    done = {}
    machine.hook(0x4B32D0, lambda emulator: emulator.arg(0), 0)
    machine.hook(0x65C310, lambda emulator: done.get(emulator.arg(0), 0), 3)

    def model(tab):
        if tab >= count:
            tab = 0
        if not 1 <= tab <= 4:
            return tab
        for record, fallback in ((0x1C, 3), (0x17, 2), (0xF, 1), (7, 0)):
            if tab >= fallback + 1 and not done.get(record):
                tab = fallback
        return tab
    for done_records in ((), (7,), (7, 0xF), (7, 0xF, 0x17), (7, 0xF, 0x17, 0x1C), (0xF, 0x17, 0x1C), (7, 0x17)):
        done.clear()
        done.update(dict.fromkeys(done_records, 1))
        for tab in range(-1, 7):
            machine.w32(0x7BF086, 0xEEEE)
            machine.call(0x49C760, regs={"eax": tab & 0xFFFFFFFF})
            check(0x49C760, f"tab {tab} done {done_records} (count {count})", signed(machine.r32(0x7BF086)), model(tab))


def sound_handles(machine):
    handles = []
    previous = 0
    for handle, voice in ((3, 0), (4, 1), (5, 2)):
        data = machine.alloc(0x30)
        machine.w32(data + 8, handle)
        if voice:
            body = machine.alloc(8)
            machine.w32(body + 4, voice - 1)
            machine.w32(data + 0x28, body)
        node = machine.alloc(8)
        machine.w32(node, data)
        machine.w32(node + 4, previous)
        previous = node
        handles.append(handle)
    machine.w32(0x7C5458, previous)
    for handle, want in ((0, 0), (3, 0), (4, 0), (5, 1), (6, 0)):
        check(0x4B9BE0, f"handle {handle}", machine.call(0x4B9BE0, ecx=handle), want)


def room_reach(machine):
    room_rect = (100, 200, 40, 30)

    def rect(emulator):
        out = emulator.arg(1)
        for index, value in enumerate(room_rect):
            emulator.w32(out + 4 * index, value)
        return out
    machine.hook(0x619730, rect, 2)
    far = 0
    for x_pos in (-500, 0, 95, 100, 120, 140, 145, 1000):
        for y_pos in (-500, 0, 195, 200, 215, 230, 235, 1000):
            for radius in (0, 3, 10):
                got = machine.call(0x4C5BD0, ecx=0x1000, edx=x_pos & 0xFFFFFFFF, regs={"eax": y_pos & 0xFFFFFFFF, "edi": radius})
                check(0x4C5BD0, f"point ({x_pos},{y_pos}) radius {radius}", got, 1)
                left, top, width, height = room_rect
                far += x_pos + radius < left or x_pos - radius > left + width or y_pos + radius < top or y_pos - radius > top + height
    print(f"FUN_004c5bd0: {far} of the points are out of the room's reach and still answer 1")


def percent_and_size(machine):
    record = machine.alloc(0x48)
    for value, percent in ((0x6400, 25), (0x1234, 50), (0xFF, 10), (0x10000, 0), (0x10000, 100)):
        machine.w32(record + 0x44, value)
        machine.call(0x4C76D0, ecx=record, edx=percent)
        check(0x4C76D0, f"{value:#x} less {percent}%", machine.r32(record + 0x44), ((100 - percent) * (value >> 8) // 100) << 8)
    machine.hook(0x622870, lambda emulator: emulator.arg(0) & 0xFF, 1)
    check(0x4C7BD0, "size + 4", machine.call(0x4C7BD0, ecx=0x1007), 0x0B)


def sequence_byte(machine):
    state = {}
    machine.hook(0x6633B0, lambda emulator: state["sequence"], 1)
    machine.hook(0x6633F0, lambda emulator: state["count"], 1)
    records = machine.alloc(0x40)
    for sequence, count, first_byte, want in ((0, 5, 0, 7), (records, 0, 0, 7), (records, 4, 0, -1), (records, 4, 1, 7)):
        state.update(sequence=sequence, count=count)
        w8(machine, records + 5, first_byte)
        w8(machine, records + 5 + 0xE, 0)    # a second record's byte 5: never read
        check(0x4C9D70, f"sequence {bool(sequence)} count {count} byte {first_byte}", signed(machine.call(0x4C9D70, ecx=0x2000)), want)


def font_glyph(machine):
    codes = [code for code in range(0x20, 0x80) if code % 7]
    glyphs = machine.alloc(0xE * len(codes))
    for index, code in enumerate(codes):
        w16(machine, glyphs + 0xE * index, code)
    header = machine.alloc(0x10)
    w16(machine, header + 8, len(codes))
    font = 1
    machine.w32(0x72DFFC, font)
    machine.w32(0x841DA8 + 0x14 * font + 8, header)
    machine.w32(0x841DA8 + 0x14 * font + 0xC, glyphs)
    for code in (0x20, 0x41, 0x7F, 0x23, 0x46, 0x10, 0x100):
        want = glyphs + 0xE * codes.index(code) if code in codes else glyphs + 0x1B2
        check(0x501690, f"code {code:#x}", machine.call(0x501690, ecx=code), want)


def unit_tables(machine):
    game = machine.alloc(0x1C00)
    by_type = {}
    for unit_type in range(5):
        units = [unit(machine, unit_type, unit_id=unit_id, mode=6 if unit_type == 4 and unit_id == 0x81 else 0)
                 for unit_id in (0x01, 0x81, 0x02)]
        by_type[unit_type] = units
        for item in units:
            bucket = game + HASH_BY_TYPE[unit_type] + (machine.r32(item + 0xC) & 0x7F) * 4
            machine.w32(item + UNIT_NEXT_IN_HASH, machine.r32(bucket))
            machine.w32(bucket, item)
    return game, by_type


def unlink(machine, game, item):
    bucket = game + HASH_BY_TYPE[machine.r32(item)] + (machine.r32(item + 0xC) & 0x7F) * 4
    previous, current = 0, machine.r32(bucket)
    while current and current != item:
        previous, current = current, machine.r32(current + UNIT_NEXT_IN_HASH)
    if current:
        machine.w32(previous + UNIT_NEXT_IN_HASH if previous else bucket, machine.r32(item + UNIT_NEXT_IN_HASH))


def free_all(machine):
    freed = []

    def free(emulator):
        game, item = emulator.mu.reg_read(UC_X86_REG_ECX), emulator.mu.reg_read(UC_X86_REG_EDX)
        freed.append(item)
        unlink(emulator, game, item)
        return 0
    machine.hook(0x555600, free, 0)
    for address, unit_type in ((0x534A50, 0), (0x573640, 1), (0x552A40, 2), (0x59F840, 3), (0x557F10, 4)):
        freed.clear()
        game, by_type = unit_tables(machine)
        machine.call(address, ecx=game)
        want = [item for item in by_type[unit_type] if not (unit_type == 4 and machine.r32(item + 0x10) == 6)]
        check(address, f"frees the type {unit_type} table (+{HASH_BY_TYPE[unit_type]:#x})", sorted(freed), sorted(want))

    game, by_type = unit_tables(machine)
    first = by_type[1][2]
    check(0x552DB0, "unit +0xe4", machine.call(0x552DB0, ecx=first), machine.r32(first + UNIT_NEXT_IN_HASH))
    for unit_type in range(5):
        for victim in by_type[unit_type]:
            machine.call(0x5530F0, edx=game, regs={"esi": victim})
            bucket = game + HASH_BY_TYPE[unit_type] + (machine.r32(victim + 0xC) & 0x7F) * 4
            chain, current = [], machine.r32(bucket)
            while current:
                chain.append(current)
                current = machine.r32(current + UNIT_NEXT_IN_HASH)
            check(0x5530F0, f"type {unit_type} id {machine.r32(victim + 0xC):#x} unlinked", victim in chain, False)


def packets(machine):
    sent = []

    def queue(emulator):
        sent.append(emulator.read(emulator.arg(0), emulator.arg(1)))
        return 0
    machine.hook(0x53B280, queue, 2)
    machine.call(0x53CA90, ecx=0x3000, edx=0x11223344)
    check(0x53CA90, "S->C 0x5c", sent[-1], bytes([0x5C]) + struct.pack("<I", 0x11223344))
    machine.call(0x53DFB0, 0x55667788, 0x9A, ecx=0x3000, edx=0x01020304)
    check(0x53DFB0, "S->C 0x8e", sent[-1], bytes([0x8E, 0x9A]) + struct.pack("<II", 0x01020304, 0x55667788))


def monster_base(machine):
    rows = fake_tables(machine, 0x200)
    w16(machine, rows + MONSTATS_SIZE * 5 + 2, 0x4E)
    w16(machine, rows + MONSTATS_SIZE * 6 + 2, 0x4F)
    for kind, class_id, flag, want in ((1, 5, 0, 1), (1, 5, 1, 0), (1, 6, 0, 0), (0, 5, 0, 0)):
        check(0x63E8D0, f"type {kind} class {class_id} flag {flag}", machine.call(0x63E8D0, unit(machine, kind, class_id), flag), want)
    fake_tables(machine, 0x40)
    check(0x63E8D0, "a player, MonStats of 0x40 rows: -1 == -1", machine.call(0x63E8D0, unit(machine, 0), 0), 1)


def missile_words(machine):
    missile = unit(machine, 3)
    data = machine.alloc(0x10)
    machine.w32(missile + 0x14, data)
    machine.call(0x64A4E0, missile, 0x1FF)
    machine.call(0x64A540, missile, 0x8002)
    check(0x64A4E0, "word +4", machine.r16(data + 4), 0x1FF)
    check(0x64A540, "word +6", machine.r16(data + 6), 0x8002)
    check(0x64A510, "word +4", machine.call(0x64A510, missile), 0x1FF)
    check(0x64A570, "word +6, sign-extended", signed(machine.call(0x64A570, missile)), -0x7FFE)
    other = unit(machine, 1)
    machine.w32(other + 0x14, data)
    check(0x64A510, "not a missile", signed(machine.call(0x64A510, other)), -1)
    check(0x64A570, "not a missile", machine.call(0x64A570, other), 0)
    machine.call(0x64A4E0, other, 0x1234)
    check(0x64A4E0, "not a missile: unchanged", machine.r16(data + 4), 0x1FF)


def room_units(machine):
    head = machine.alloc(4)
    chain = [unit(machine, 1) for _ in range(3)]
    for item, after in zip(chain, chain[1:] + [0]):
        machine.w32(item + 0xE8, after)
    machine.w32(head, chain[0])
    machine.hook(0x619F60, lambda emulator: head, 0)
    stranger = unit(machine, 1)
    for room, item, want in ((0x4000, chain[0], 1), (0x4000, chain[2], 1), (0x4000, stranger, 0), (0, chain[0], 0)):
        check(0x64C260, f"room {room:#x}", machine.call(0x64C260, room, item), want)


def aim_keeps(machine):
    seen = []

    def aim(emulator):
        seen.append(tuple(emulator.arg(index) for index in range(4)))
        emulator.w32(emulator.mu.reg_read(UC_X86_REG_EDX), 0x66)
        return 0
    machine.hook(0x64FC60, aim, 4)
    got = machine.call(0x64FE10, 1, 2, 3, 4)
    check(0x64FE10, "returns the 4th arg as the aim left it", (got, seen[-1]), (0x66, (1, 2, 3, 4)))


def code_pad(machine):
    for text, want in ((b"", b"    "), (b"a", b"a   "), (b"ab", b"ab  "), (b"abc", b"abc "), (b"abcd", b"abcd"), (b"abcde", b"abcd")):
        source = machine.put(text + b"\0")
        out = machine.alloc(8)
        machine.call(0x6617B0, ecx=source, regs={"eax": out})
        check(0x6617B0, repr(text), machine.read(out, 4), want)


def main():
    machine = emu.Emu()
    for case in (globals_getters, ui_flags, hover_unit_find, npc_item_watchers, palshift_find, waypoint_tab,
                 sound_handles, room_reach, percent_and_size, sequence_byte, font_glyph, free_all, packets,
                 monster_base, missile_words, room_units, aim_keeps, code_pad):
        case(machine)
    for line in failures:
        print(line)
    print(f"{'FAIL' if failures else 'ok'}: {len(checked)} functions, {len(failures)} differences")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
