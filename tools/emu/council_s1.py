# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace the stock Council S1 event with original game.exe code.

uv run python council_s1.py --exe /private/game.exe --animdata /private/animdata.d2

Read both files from your own installation. The AnimData record stays local.
This is an isolated scheduler/dispatch check at the fixture's rate (200).

The built-d2d differential, with speed modifiers:

    set -o pipefail; ../../build/tests/test_action_tick --council | grep '^D ' > d2d.tsv
    uv run python council_s1.py compare d2d.tsv

`d2d.tsv` is `D class walk anim rate tick`: the Council's S1 at walk % (stat
0x43) and anim % (other_animrate - 100), d2d's rate and the tick its shot
comes on. For each row game.exe's FUN_00623f50 stores the rate (tools/emu
mode_rates.py's rig, the same hooks) and FUN_005539b0 files the action on the
stock HPS1HTH record from the MPQs; `compare` prints the rows where the rate
or the tick differ, then `N rows, M differ`, and exits 1 on a difference.
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
    print(f"PASS: {cases} original scheduler/dispatch fixtures; action at start + 4 ticks at rate 200")
    print("Not tested here: the rate FUN_00623f50 stores (see `compare`), real event queue, missile creation, full AI")


def compare(d2d_path):
    """Each d2d row against FUN_00623f50's rate and FUN_005539b0's action tick."""
    import mode_rates
    from emu import mpq_cache
    blob = mpq_cache().read("data\\global\\animdata.d2")
    record = None
    offset = 0
    while offset + 4 <= len(blob):
        count = struct.unpack_from("<I", blob, offset)[0]
        offset += 4
        if offset + count * 160 > len(blob):
            raise SystemExit("animdata.d2 is cut short")
        for _ in range(count):
            if blob[offset:offset + 8].split(b"\0", 1)[0].upper() == b"HPS1HTH":
                record = blob[offset:offset + 160]
            offset += 160
    if record is None:
        raise SystemExit("no HPS1HTH record in animdata.d2")
    frames, speed = struct.unpack_from("<II", record, 8)
    rig = mode_rates.Rig()
    machine = rig.e
    game = machine.alloc(0x200)
    record_at = machine.put(record)
    events = []

    def capture_event(emulated):
        events.append(tuple(emulated.arg(index) for index in range(4)))
        return 0

    machine.hook(0x623b10, lambda emulated: 0, 1)   # starting animation frame: 0 for a monster
    machine.hook(0x5417d0, capture_event, 4)
    rows = differ = 0
    with open(d2d_path, encoding="utf-8") as handle:
        for line in handle:
            field = line.split()
            if not field:
                continue
            if len(field) != 6 or field[0] != "D":
                raise SystemExit(f"malformed line: {line.strip()!r}")
            cls, walk, anim, d2d_rate, d2d_tick = (int(value) for value in field[1:])
            rate = int(rig.run(cls, 8, 0, speed, walk, 0, 0, 0, 0, 0, 100, 100 + anim))
            unit = machine.alloc(0x200)
            machine.w32(unit, 1)
            machine.w32(unit + 4, cls)
            machine.w32(unit + 0x10, 8)
            machine.w32(unit + 0x48, frames << 8)
            machine.mu.mem_write(unit + 0x4c, struct.pack("<h", rate))
            machine.w32(unit + 0x50, record_at)
            machine.w32(game + 0xa8, 1000)
            events.clear()
            machine.call(0x5539b0, ecx=game, edx=unit, limit=100000)
            assert machine.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
            actions = [event[1] - 1000 for event in events if event[0] == 0]
            tick = actions[0] if actions else -1
            rows += 1
            if (rate, tick) != (d2d_rate, d2d_tick):
                differ += 1
                if differ <= 20:
                    print(f"DIFF class {cls} walk {walk} anim {anim}: game rate {rate} tick {tick}, d2d rate {d2d_rate} tick {d2d_tick}")
    if not rows:
        raise SystemExit("no rows (an empty file is not a pass)")
    print(f"{rows} rows, {differ} differ")
    raise SystemExit(1 if differ else 0)


def main():
    import sys
    if len(sys.argv) == 3 and sys.argv[1] == "compare":
        compare(sys.argv[2])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--animdata", required=True, type=Path)
    args = parser.parse_args()
    check(args.exe, animation_record(args.animdata))


if __name__ == "__main__":
    main()
