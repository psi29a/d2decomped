# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace the stock Council S1 event with original game.exe code.

uv run python council_s1.py --exe /private/game.exe --animdata /private/animdata.d2

Read both files from your own installation. The AnimData record stays local.
This is an isolated scheduler/dispatch check, not a built-d2d differential.
"""
import argparse
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

from emu import RET_MAGIC, Emu


def animation_record(path):
    data = Path(path).read_bytes()
    offset = 0
    found = []
    while offset < len(data):
        if offset + 4 > len(data):
            raise ValueError("truncated AnimData bucket")
        count = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        if offset + count * 160 > len(data):
            raise ValueError("truncated AnimData records")
        for _ in range(count):
            record = data[offset:offset + 160]
            offset += 160
            if record[:8].split(b"\0", 1)[0].upper() == b"HPS1HTH":
                found.append(record)
    if len(found) != 1:
        raise ValueError(f"expected one HPS1HTH record, found {len(found)}")
    return found[0]


def check(exe, record):
    frames, speed = struct.unpack_from("<II", record, 8)
    markers = [(index, value) for index, value in enumerate(record[16:]) if value]
    if (frames, speed, markers) != (14, 200, [(3, 2)]):
        raise ValueError("this check expects the stock 1.14d Council animation")

    oracle = Emu(exe=str(exe))
    game, unit = oracle.alloc(0x200), oracle.alloc(0x200)
    oracle.w32(unit, 1)
    oracle.w32(unit + 0x10, 8)
    oracle.w32(unit + 0x48, frames << 8)
    oracle.mu.mem_write(unit + 0x4c, struct.pack("<h", speed))
    oracle.w32(unit + 0x50, oracle.put(record))

    # Controlled tables let the original mode and missile lookups run.
    # Missile identifiers are synthetic sentinels, not game data dumps.
    tables = oracle.alloc(0xc00)
    monstats = oracle.alloc(348 * 0x1a8)
    monstats2 = oracle.alloc(0x134)  # stationary S1: movement flags zero
    difficulty = oracle.alloc(0x100)
    oracle.w32(0x744304, tables)
    oracle.w32(tables + 0xa78, monstats)
    oracle.w32(tables + 0xa80, 348)
    oracle.w32(tables + 0xa90, monstats2)
    oracle.w32(tables + 0xa98, 1)
    missile_offset = oracle.r32(0x6ea938 + 8 * 4)
    assert missile_offset == 0x3e
    for character_class in (345, 346, 347):
        row = monstats + character_class * 0x1a8
        oracle.mu.mem_write(row + missile_offset, struct.pack("<h", character_class))

    events, missile_requests = [], []
    oracle.hook(0x623b10, lambda machine: 0, 1)  # starting animation frame
    oracle.hook(0x611d30, lambda machine: difficulty, 1)

    def capture_event(machine):
        events.append(tuple(machine.arg(index) for index in range(4)))
        return 0

    def capture_missile(machine):
        missile_requests.append((machine.mu.reg_read(UC_X86_REG_EDX), machine.arg(2)))
        return 0  # stop before creation and its post-spawn hooks

    oracle.hook(0x5417d0, capture_event, 4)
    oracle.hook(0x56ecb0, capture_missile, 8)
    cases = 0
    for character_class in (345, 346, 347):
        oracle.w32(unit + 4, character_class)
        for now in (0, 1, 100, 10000):
            oracle.w32(game + 0xa8, now)
            events.clear()
            missile_requests.clear()
            oracle.call(0x5539b0, ecx=game, edx=unit, limit=100000)
            assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
            assert not missile_requests, "scheduler must not create a missile immediately"
            actions = [event for event in events if event[0] == 0]
            assert actions == [(0, now + 4, 2, 0)], (character_class, now, events)
            # Deliver only the captured action to the original dispatcher.
            # This does not exercise the real event queue or its tick ordering.
            oracle.w32(game + 0xa8, actions[0][1])
            oracle.call(0x5a7ba0, 2, 0, ecx=game, edx=unit, limit=100000)
            assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
            assert missile_requests == [(character_class, 1)], missile_requests
            cases += 1
    # Arithmetic cross-check of the current C++ source expression only.
    # It is deliberately not described as an executed C++ comparison.
    source_delay_ms = (40 * 256 // speed) * markers[0][0]
    source_first_tick = (source_delay_ms + 39) // 40
    assert source_first_tick == 4
    print(f"PASS: {cases} original scheduler/dispatch fixtures; action at start + 4 ticks")
    print("Current source arithmetic: deadline 153 ms, first 40-ms tick 160 ms")
    print("Not tested: real event queue, missile creation, full AI, speed modifiers, built d2d")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--animdata", required=True, type=Path)
    args = parser.parse_args()
    check(args.exe, animation_record(args.animdata))


if __name__ == "__main__":
    main()
