# SPDX-License-Identifier: GPL-3.0-or-later
"""Run 1.14d's monster mode sender and packet builders with synthetic aims."""
import argparse
import struct
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    check(parser.parse_args().exe)
