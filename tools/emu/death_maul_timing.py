# SPDX-License-Identifier: GPL-3.0-or-later
"""Schedule the stock DeathMaul sequence and standalone A2 in game.exe.

Uses local 1.14d tables through mode_rates.Rig, plus local AnimData.d2.
Captures queued events, with the starting frame and sequence event lookup
scripted from the stock rows. The real rate refresh and scheduler run.
"""
import argparse
import csv
import io
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

from council_s1 import animation_record
from emu import RET_MAGIC, mpq_cache
from mode_rates import Rig


def check(animdata):
    table = mpq_cache().read(r"data\global\excel\MonSeq.txt")
    rows = csv.DictReader(io.StringIO(table.decode("latin1")), delimiter="\t")
    markers = [int(row["event"] or 0) for row in rows if row["sequence"] == "seq_deathmaulerdig"]
    assert len(markers) == 35 and [(index, event) for index, event in enumerate(markers) if event] == [(13, 1)]

    rig = Rig()
    oracle, unit = rig.e, rig.unit
    game, sequence = oracle.alloc(0x200), oracle.alloc(0x40)
    events = []

    def sequence_event(machine):
        machine.w32(machine.arg(2), markers[machine.arg(1) >> 8])
        return 0

    def capture(machine):
        events.append(tuple(machine.arg(index) for index in range(4)))
        return 0

    oracle.hook(0x623b10, lambda machine: 0, 1)
    oracle.hook(0x6634c0, sequence_event, 3)
    oracle.hook(0x5417d0, capture, 4)

    for percent in (50, 60, 67, 100, 125):
        rig.run(529, 14, 308, 224, 75, 0, 0, 0, 0, 0, percent, percent)
        oracle.w32(unit + 0x30, sequence)
        oracle.w32(unit + 0x34, len(markers) << 8)
        oracle.w32(unit + 0x3c, 256)
        oracle.call(0x623f50, unit, 0, 0)
        assert oracle.r32(unit + 0x3c) == 256
        events.clear()
        oracle.call(0x5539b0, ecx=game, edx=unit, limit=100000)
        assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
        assert events == [(0, 13, 1, 0), (1, 35, 0, 0)], events
        print(f"DeathMaul at {percent}% other/attack rate: action 13, end 35")

    record = animation_record(animdata, b"M5A2HTH")
    frames, speed = struct.unpack_from("<II", record, 8)
    assert (frames, speed) == (15, 224)
    oracle.w32(unit + 0x10, 5)
    oracle.w32(unit + 0x30, 0)
    oracle.w32(unit + 0x48, frames << 8)
    oracle.w32(unit + 0x50, oracle.put(record))
    oracle.mu.mem_write(unit + 0x4c, struct.pack("<h", speed))
    events.clear()
    oracle.call(0x5539b0, ecx=game, edx=unit, limit=100000)
    assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC
    assert events == [(0, 10, 2, 0), (1, 18, 0, 0)], events
    print("Standalone Death Mauler A2: action 10, end 18")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--animdata", required=True, type=Path)
    check(parser.parse_args().animdata)
