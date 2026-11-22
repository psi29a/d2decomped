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


def animation_record(path, name=b"HPS1HTH"):
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
            if record[:8].split(b"\0", 1)[0].upper() == name:
                found.append(record)
    if len(found) != 1:
        raise ValueError(f"expected one {name.decode('ascii')} record, found {len(found)}")
    return found[0]


def check(exe, record, melee_record):
    frames, speed = struct.unpack_from("<II", record, 8)
    markers = [(index, value) for index, value in enumerate(record[16:]) if value]
    if (frames, speed, markers) != (14, 200, [(3, 2)]):
        raise ValueError("this check expects the stock 1.14d Council animation")

    oracle = Emu(exe=str(exe))
    game, unit = oracle.alloc(0x200), oracle.alloc(0x200)
    oracle.w32(unit, 1)
    oracle.w32(unit + 0x10, 8)
    oracle.w32(unit + 0x48, frames << 8)
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
    # Effective animation speeds already supplied to the scheduler. This
    # isolates event timing from the code that computes chill and other rates.
    timing = {30: (26, 120), 42: (19, 86), 50: (16, 72), 56: (14, 64),
              70: (11, 52), 73: (11, 50), 84: (10, 43), 94: (9, 39), 100: (8, 36),
              112: (7, 32), 117: (7, 31), 125: (7, 29), 126: (7, 29), 140: (6, 26),
              150: (6, 24), 168: (5, 22), 170: (5, 22), 200: (4, 18),
              210: (4, 18), 250: (4, 15), 254: (4, 15), 256: (3, 14), 300: (3, 12),
              350: (3, 11)}
    cases = 0
    for character_class in (345, 346, 347):
        oracle.w32(unit + 4, character_class)
        for rate, (action, end) in timing.items():
            oracle.mu.mem_write(unit + 0x4c, struct.pack("<h", rate))
            for now in (0, 1, 100, 10000):
                oracle.w32(game + 0xa8, now)
                events.clear()
                missile_requests.clear()
                oracle.call(0x5539b0, ecx=game, edx=unit, limit=100000)
                assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
                assert not missile_requests, "scheduler must not create a missile immediately"
                assert events == [(0, now + action, 2, 0), (1, now + end, 0, 0)], (
                    character_class, rate, now, events
                )
                # Deliver only the captured action to the original dispatcher.
                # This does not exercise the real event queue or its tick ordering.
                oracle.w32(game + 0xa8, now + action)
                oracle.call(0x5a7ba0, 2, 0, ecx=game, edx=unit, limit=100000)
                assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
                assert missile_requests == [(character_class, 1)], missile_requests
                cases += 1
    print(f"PASS: {cases} original scheduler/dispatch fixtures")
    for rate, (action, end) in timing.items():
        print(f"Animation rate {rate}: action at +{action} ticks, mode end at +{end}")

    removed = []
    oracle.hook(0x540e60, lambda machine: removed.append(machine.arg(0)) or 0, 2)
    oracle.w32(game + 0xa8, 2)
    oracle.mu.mem_write(unit + 0x4c, struct.pack("<h", speed))
    events.clear()
    oracle.call(0x553990, ecx=game, edx=unit, limit=1000)
    assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
    assert removed == [0, 1], removed
    oracle.call(0x5539b0, ecx=game, edx=unit, limit=100000)
    assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
    assert events == [(0, 6, 2, 0), (1, 20, 0, 0)], events
    print("Restart helpers at tick 2: clear action and end; queue action at 6 and end at 20")

    # Apply chill to a fresh schedule. The refresh hook supplies the new
    # effective rate so this checks the chill caller's queue operations.
    removed.clear()
    events.clear()
    oracle.w32(game + 0xa8, 0)
    oracle.call(0x5539b0, ecx=game, edx=unit, limit=100000)
    assert events == [(0, 4, 2, 0), (1, 18, 0, 0)], events
    oracle.mu.mem_write(monstats + 347 * 0x1a8 + 0x168, struct.pack("<b", -50))
    state_list = oracle.alloc(0x40)
    oracle.stub(0, {0x6256b0: 2, 0x639db0: 3, 0x6252d0: 2,
                    0x625ce0: 2, 0x626e10: 3, 0x6270b0: 4})
    oracle.hook(0x554010, lambda machine: game, 0)
    oracle.hook(0x6251f0, lambda machine: state_list, 5)
    refreshes = []

    def refresh_rate(machine):
        refreshes.append(True)
        machine.mu.mem_write(unit + 0x4c, struct.pack("<h", speed // 2))
        return 0

    oracle.hook(0x623f50, refresh_rate, 3)
    oracle.w32(game + 0xa8, 2)
    oracle.call(0x57af80, 0, 10, regs={"edi": unit}, limit=100000)
    assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
    assert refreshes == [True] and not removed, (refreshes, removed)
    assert events == [(0, 4, 2, 0), (1, 18, 0, 0), (12, 12, 0, 0)], events
    print("Chill at tick 2: rate refreshed; action and mode-end events left queued")
    # Exercise the original dispatch gate with freeze already applied
    # when the queued action is delivered.
    frozen = False
    dispatched = []
    oracle.hook(0x639df0, lambda machine: int(frozen and machine.arg(1) == 1), 2)
    oracle.hook(0x5a7ba0, lambda machine: dispatched.append(machine.arg(0)) or 0, 2)
    for mode, tick, marker in ((8, 4, 2), (4, 8, 1)):
        oracle.w32(unit + 0x10, mode)
        oracle.w32(game + 0xa8, tick)
        for frozen in (False, True):
            dispatched.clear()
            oracle.call(0x5a7f80, 0, marker, 0, ecx=game, edx=unit, limit=10000)
            assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
            assert dispatched == ([] if frozen else [marker]), dispatched
        print(f"Mode {mode} action at tick {tick}: delivered if active, discarded if already frozen")

    melee_frames, melee_speed = struct.unpack_from("<II", melee_record, 8)
    assert (melee_frames, melee_speed) == (10, 168)
    oracle.w32(unit + 0x10, 4)  # A1
    oracle.w32(unit + 0x48, melee_frames << 8)
    oracle.w32(unit + 0x50, oracle.put(melee_record))
    oracle.w32(game + 0xa8, 0)
    for rate, action, end in ((84, 16, 31), (112, 12, 23), (126, 11, 21), (168, 8, 16)):
        events.clear()
        oracle.mu.mem_write(unit + 0x4c, struct.pack("<h", rate))
        oracle.call(0x5539b0, ecx=game, edx=unit, limit=100000)
        assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
        assert events == [(0, action, 1, 0), (1, end, 0, 0)], (rate, events)
        print(f"Council A1 rate {rate}: action at +{action} ticks, mode end at +{end}")

    oracle.w32(unit + 0x44, 0)
    positions = []
    for rate in (168, 168, 84, 84, 84, 84, 84, 84):
        oracle.mu.mem_write(unit + 0x4c, struct.pack("<h", rate))
        oracle.call(0x623e00, unit, limit=10000)
        assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
        positions.append(oracle.r32(unit + 0x44))
    assert positions == [168, 336, 420, 504, 588, 672, 756, 840], positions
    print(f"Council A1 visible positions after slowdown: {positions}")
    print("Not tested: rate derivation, real event queue, missile creation, full AI, built d2d")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--animdata", required=True, type=Path)
    args = parser.parse_args()
    check(args.exe, animation_record(args.animdata), animation_record(args.animdata, b"HPA1HTH"))


if __name__ == "__main__":
    main()
