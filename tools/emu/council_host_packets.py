# SPDX-License-Identifier: GPL-3.0-or-later
"""Run 1.14d's monster mode sender and packet builders with synthetic aims."""
import argparse
import struct
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

from emu import RET_MAGIC, Emu


def check(exe):
    oracle = Emu(exe=str(exe))
    game, monster, path, client, target = (oracle.alloc(0x200) for _ in range(5))
    oracle.w32(monster, 1)
    oracle.w32(monster + 4, 345)
    oracle.w32(monster + 0xc, 123)
    oracle.w32(monster + 0x2c, path)
    oracle.w32(target + 0xc, 654)
    for address, value, words in (
        (0x5387f0, 1, 0), (0x620250, 0, 1),
        (0x648a40, 111, 1), (0x648a60, 222, 1),
        (0x45adf0, 333, 0), (0x45ae20, 444, 0),
    ):
        oracle.hook(address, lambda _machine, result=value: result, words)

    packets = []

    def capture(machine, _address, _size, _data):
        stack = machine.reg_read(UC_X86_REG_ESP)
        packets.append(bytes(machine.mem_read(oracle.r32(stack + 4), oracle.r32(stack + 8))))

    oracle.mu.hook_add(UC_HOOK_CODE, capture, begin=0x53b280, end=0x53b280)
    oracle.hook(0x53b280, lambda _machine: 0, 2)
    for mode, name, point_command, unit_command in ((4, "A1", 11, 10), (8, "S1", 12, 13)):
        oracle.w32(monster + 0x10, mode)
        for aim in (0, target):
            oracle.hook(0x553540, lambda _machine, result=aim: result, 2)
            packets.clear()
            oracle.call(0x597e20, client, ecx=game, edx=monster, limit=1000)
            assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
            (packet,) = packets
            expected = (0x6c, unit_command, 16) if aim else (0x6b, point_command, 16)
            assert (packet[0], packet[5], len(packet)) == expected
            assert struct.unpack_from("<I", packet, 1) == (123,)
            correction = struct.unpack_from("<HH", packet, 12)
            assert correction == (333, 444)
            if aim:
                assert struct.unpack_from("<BI", packet, 6) == (0, 654)
            else:
                assert struct.unpack_from("<HH", packet, 6) == (111, 222)
            print(f"{name}: packet {packet[0]:#04x}, command {packet[5]}, correction {correction}")


def check_skill_commands(exe):
    """Run the two original skill packet handlers up to unit dispatch."""
    oracle = Emu(exe=str(exe))
    monster = oracle.alloc(0x200)
    commands = []

    def dispatch(machine):
        args = oracle.mu.mem_read(machine.arg(0), 28)
        commands.append((oracle.mu.reg_read(UC_X86_REG_ECX), oracle.mu.reg_read(UC_X86_REG_EDX),
                         struct.unpack("<7i", args)))
        return 0

    oracle.hook(0x480c10, dispatch, 2)
    targeted = bytes.fromhex("4c 01 3f 00 00 00 3e 00 03 00 01 00 00 00 00 00")
    point = bytes.fromhex("4d 01 3f 00 00 00 3e 00 00 00 03 4b 16 83 13 00 00")
    for handler, packet, command, aim in ((0x45df00, targeted, 0x16, (0, 1)),
                                          (0x45df60, point, 0x15, (5707, 4995))):
        oracle.call(handler, ecx=monster, edx=oracle.put(packet), limit=1000)
        assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
        assert commands.pop() == (command, monster, (62, -1, *aim, 3, 0, 0))
        print(f"{packet[0]:#04x}: skill 62, command {command:#04x}, aim {aim}")


def check_client_missile_rows(exe):
    """Run the original skill action up to the ordinary client missile constructor."""
    oracle = Emu(exe=str(exe))
    tables, rows, caster = (oracle.alloc(size) for size in (0x1000, 338 * 0x23c, 0x200))
    oracle.w32(0x744304, tables)
    oracle.w32(tables + 0xb98, rows)
    oracle.w32(tables + 0xba0, 338)
    oracle.w32(0x727db0, 1)
    oracle.w32(0x727ba8, 0)
    launches = []

    def launch(machine):
        launches.append((oracle.mu.reg_read(UC_X86_REG_ECX), oracle.mu.reg_read(UC_X86_REG_EDX),
                         machine.arg(0), machine.arg(1)))
        return 1

    oracle.hook(0x4c57b0, launch, 6)
    oracle.hook(0x643a00, lambda _machine: 0, 2)
    oracle.hook(0x646ca0, lambda _machine: 0, 4)
    for skill, missile in ((45, 91), (55, 96), (62, -1), (337, 247)):
        oracle.mu.mem_write(rows + skill * 0x23c + 0xe8, struct.pack("<h", missile))
        oracle.call(0x4c6680, 4, 0, ecx=caster, edx=skill, limit=1000)
        assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
        assert launches == ([(missile, caster, skill, 4)] if missile >= 0 else []), launches
        launches.clear()
        print(f"skill {skill}: client missile row {missile}")


def check_client_s1_missile(exe):
    """The original client selects MissS1 only when its action frame is crossed."""
    oracle = Emu(exe=str(exe))
    tables, rows, caster, target = (oracle.alloc(size) for size in (0x1000, 348 * 0x1a8, 0x200, 0x200))
    oracle.w32(0x744304, tables)
    oracle.w32(tables + 0xa78, rows)
    oracle.w32(tables + 0xa80, 348)
    oracle.w32(caster, 1)
    oracle.w32(caster + 0x10, 8)
    oracle.w32(caster + 0x48, 14 << 8)
    oracle.w32(caster + 0x4c, 168 | (2 << 16))
    animation = bytearray(160)
    animation[16 + 3] = 2
    oracle.w32(caster + 0x50, oracle.put(bytes(animation)))
    oracle.hook(0x4648f0, lambda _machine: target, 0)
    oracle.hook(0x45adf0, lambda _machine: 5707, 0)
    oracle.hook(0x45ae20, lambda _machine: 4995, 0)
    oracle.hook(0x4af1b0, lambda _machine: 0, 4)
    launches = []

    def launch(machine):
        request = oracle.mu.reg_read(UC_X86_REG_ECX)
        launches.append(tuple(oracle.r32(request + offset) for offset in (0, 0x10, 0x1c, 0x20, 0x30)))
        return 0

    oracle.hook(0x4cd540, launch, 0)
    for monster_class in (345, 346, 347):
        oracle.w32(caster + 4, monster_class)
        oracle.mu.mem_write(rows + monster_class * 0x1a8 + 0x3e, struct.pack("<h", 343))
        for position, fires in ((672, False), (840, True), (1008, False)):
            oracle.w32(caster + 0x44, position)
            oracle.call(0x4af2e0, regs={"eax": caster}, limit=1000)
            assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
            assert launches == ([(0x20, 343, 5707, 4995, 1)] if fires else []), launches
            launches.clear()
        print(f"class {monster_class}: client S1 selects missile 343 on the crossing tick")


def check_state_stats(exe):
    """Run the original 0xa8 decoder through the signed send fields."""
    oracle = Emu(exe=str(exe))
    tables, rows, unit = (oracle.alloc(size) for size in (0x1000, 0x144 * 97, 0x200))
    oracle.w32(0x744304, tables)
    oracle.w32(tables + 0xbcc, rows)
    oracle.w32(tables + 0xbd4, 97)
    signed_flag = oracle.mu.mem_read(0x6ce26c, 1)[0]
    expected = [(67, -30), (68, -50), (93, 20), (96, -10)]
    for (stat, _), width in zip(expected, (10, 10, 9, 9), strict=True):
        row = rows + stat * 0x144
        oracle.w32(row + 4, signed_flag)
        oracle.mu.mem_write(row + 8, bytes([width]))

    decoded = []

    def add_stat(machine):
        value = struct.unpack("<i", struct.pack("<I", machine.arg(2)))[0]
        decoded.append((machine.arg(1), value))
        return 0

    oracle.hook(0x463990, lambda _machine: unit, 0)
    oracle.hook(0x4d9b20, lambda _machine: 0, 0)
    oracle.hook(0x4d9d70, add_stat, 4)
    oracle.hook(0x4d9e60, lambda _machine: 0, 0)
    body = bytes.fromhex("43c427e27c170a60ecff07")
    packet = struct.pack("<BBIBB", 0xa8, 1, 42, 8 + len(body), 11) + body
    oracle.call(0x45ee20, ecx=oracle.put(packet), limit=10000)
    assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
    assert decoded == expected, decoded
    print(f"0xa8 signed animation stats: {decoded}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    check(args.exe)
    check_skill_commands(args.exe)
    check_client_missile_rows(args.exe)
    check_client_s1_missile(args.exe)
    check_state_stats(args.exe)
