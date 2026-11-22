# SPDX-License-Identifier: GPL-3.0-or-later
"""Inferno's channel (srvdofunc 19, skills.md "Inferno's channel"): the two pieces of game.exe that make a held cast a loop.

    uv run python inferno_channel.py

1. The server's player mode switch FUN_0057edd0 (EAX unit, ECX game, EBX new mode; FUN_005809d0 asks it for each
   skill packet): from SQ (18) or SC (10), a new SQ / SC / A1 goes only while a mode-end event is queued
   (FUN_005415a0 hooked: the case's frame, or 0 for none). A channelling unit always has one, so a held cast is
   taken mid-channel. Expected: queued 0 -> 0, queued 1 -> 1, and from NU always 1.
2. FUN_00553c70(game, unit, 1), which Inferno's do (FUN_005c8ca0's player branch, 0x5c8da7) and its start's
   burning path (0x5c8f4c) call: on a plain animation record (unit +0x30 0: AnimData flags at +0x50, rate +0x4c,
   frames +0x48) whose event-4 marker is on frame 6, with the unit 6 ticks into its mode (+0x44 = start << 8),
   it moves the start one tick later, takes the queued type-0 and type-1 events off (FUN_00540e60(0), (1)) and
   files the marker again a tick on and the end event with the start's push. Hooked: FUN_005417d0 (the events,
   recorded) and FUN_00540e60 (recorded). Expected: start now - 5, events (0, +1, 4) and (1, +9).

Prints each result and `ok`, or exits 1 on a difference. game.exe: the data path emu.py finds.
"""
import struct
import sys

import emu

SQ, SC, A1, NU = 18, 10, 7, 1


def mode_switch(machine):
    state = {"end": 0}
    machine.hook(0x5415a0, lambda _: state["end"], 1)
    unit, game = machine.alloc(0x200), machine.alloc(0x2000)
    machine.w32(game + 0xa8, 5000)
    bad = 0
    for queued in (0, 1):
        state["end"] = 5040 if queued else 0
        for current in (NU, SC, SQ):
            for new in (SC, SQ, A1):
                machine.mu.mem_write(unit, bytes(0x200))
                machine.w32(unit + 4, 1)                 # a Sorceress
                machine.w32(unit + 0x10, current)
                allowed = int(machine.call(0x57edd0, ecx=game, regs={"eax": unit, "ebx": new}) & 0xff != 0)
                want = 1 if current == NU or queued else 0
                print(f"switch queued {queued} from {current} to {new}: {allowed}" + ("" if allowed == want else f"  (expected {want})"))
                bad += allowed != want
    return bad


def refile(machine):
    events, removed = [], []
    machine.hook(0x5417d0, lambda m: events.append(tuple(m.arg(index) for index in range(3))) or 0, 4)
    machine.hook(0x540e60, lambda m: removed.append(m.arg(0)) or 0, 2)
    game, unit = machine.alloc(0x200), machine.alloc(0x200)
    record = bytearray(160)
    record[16 + 6] = 4                                   # the do's marker on frame 6
    now = 1000
    machine.w32(unit, 0)
    machine.w32(unit + 0x48, 14 << 8)
    machine.mu.mem_write(unit + 0x4c, struct.pack("<h", 256))
    machine.w32(unit + 0x50, machine.put(bytes(record)))
    machine.w32(unit + 0x44, (now - 6) << 8)
    machine.w32(game + 0xa8, now)
    machine.call(0x553c70, 1, ecx=game, edx=unit, limit=100000)
    start = machine.r32(unit + 0x44) >> 8
    filed = [(kind, tick - now, marker) for kind, tick, marker in events]
    print(f"refile: start now - {now - start}, removed {removed}, filed {filed}")
    return int(now - start != 5 or removed != [0, 1] or filed != [(0, 1, 4), (1, 9, 0)])


def main():
    machine = emu.Emu()
    bad = mode_switch(machine) + refile(emu.Emu())
    print("ok" if not bad else f"{bad} differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
