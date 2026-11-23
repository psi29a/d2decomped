# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's log-line prefix FUN_0041fba0 on local game.exe 1.14d, against a model.

Run: uv run python slog_stamp.py --exe /path/to/game.exe [--mutation-check]

# gaps-runs: FUN_0041fba0

FUN_0041fba0 takes the log record in EAX and one stack argument (ret 4). It
runs for real with its callees (strlen FUN_00413750, the copy FUN_0041d2a0,
the fill FUN_0041d2c0, the indent FUN_0041fb40). GetTickCount and
GetLocalTime are scripted; wsprintfA is emu.py's stand-in, so the stamp's
text checks the format string at 0x6cff0c, not user32. Each case writes a
record (length +0xc, indent +0x14, stamp flag +0x18, text from +0x1c) and
compares the record, the cached tick 0x7792f0, the stamp 0x7792b0 and its
length 0x7792a8 with the model. `--mutation-check` plants 6 defects in the
model and requires each to be caught. Checks game.exe only: d2d has no
counterpart (source-frontier.md).
"""
import argparse
import hashlib
import random
import struct

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
STAMP_LINE = 0x41FBA0
LAST_TICK, STAMP_LENGTH, STAMP_TEXT = 0x7792F0, 0x7792A8, 0x7792B0
TEXT = 0x1C          # the record's text buffer
SPAN = 0x400         # how much of it a case compares
MUTATIONS = ("no_flag_gate", "indent_uncapped", "indent_without_flag",
             "stamp_every_call", "stamp_when_zero", "no_terminator")


class Model:
    """What FUN_0041fba0 should do, read from its disassembly (source-frontier.md)."""

    def __init__(self, mutation=None):
        self.mutation = mutation
        self.last_tick = 0
        self.stamp = b""

    def run(self, buffer, length, indent, flag, with_stamp, tick, when):
        """Returns the new buffer and length; updates the cached stamp."""
        buffer = bytearray(buffer)
        if not flag and self.mutation != "no_flag_gate":
            if self.mutation == "indent_without_flag":
                return self.indent(buffer, length, indent)
            return buffer, length
        if tick != self.last_tick or self.mutation == "stamp_every_call":
            self.last_tick = tick
            month, day, hour, minute, second, millis = when
            self.stamp = b"%u/%u %02u:%02u:%02u.%03u  " % (month, day, hour, minute, second, millis)
        size = len(self.stamp)
        if with_stamp or self.mutation == "stamp_when_zero":
            terminator = b"" if self.mutation == "no_terminator" else b"\0"
            buffer[length:length + size + len(terminator)] = self.stamp + terminator
        else:
            buffer[length:length + size + 1] = b" " * size + b"\0"
        return self.indent(buffer, length + size, indent)

    def indent(self, buffer, length, indent):
        """FUN_0041fb40: up to 0x80 spaces and a 0 (a positive indent only)."""
        if indent > 0:
            count = indent if self.mutation == "indent_uncapped" else min(indent, 0x80)
            buffer[length:length + count + 1] = b" " * count + b"\0"
            length += count
        return buffer, length


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.tick = 0
        self.when = (1, 1, 0, 0, 0, 0)
        self.calls = {"GetTickCount": 0, "GetLocalTime": 0}
        imports = {"GetTickCount": self.get_tick_count, "GetLocalTime": self.get_local_time}
        for address, (slot, _) in list(self.machine.slots.items()):
            if slot in imports:
                self.machine.slots[address] = (slot, imports[slot])
        self.record = self.machine.alloc(TEXT + SPAN + 0x10)

    def get_tick_count(self, machine):
        """GetTickCount: the case's tick."""
        self.calls["GetTickCount"] += 1
        return self.tick, 0

    def get_local_time(self, machine):
        """GetLocalTime: a SYSTEMTIME (year, month, weekday, day, hour, minute, second, ms)."""
        self.calls["GetLocalTime"] += 1
        month, day, hour, minute, second, millis = self.when
        machine.mu.mem_write(machine.arg(0), struct.pack("<8H", 2026, month, 1, day, hour, minute, second, millis))
        return 0, 1

    def run(self, buffer, length, indent, flag, with_stamp):
        """One call; returns the record's text, length, indent, flag and the three globals."""
        machine = self.machine
        machine.mu.mem_write(self.record, struct.pack("<7i", 0, 0, 0, length, 0, indent, flag) + buffer)
        machine.call(STAMP_LINE, with_stamp, regs={"eax": self.record})
        fields = struct.unpack("<7i", machine.read(self.record, TEXT))
        stamp = machine.read(STAMP_TEXT, min(machine.r32(STAMP_LENGTH), LAST_TICK - STAMP_TEXT))   # the 0x40-byte buffer
        return (machine.read(self.record + TEXT, SPAN), fields[3], fields[5], fields[6],
                machine.r32(LAST_TICK), stamp)


def cases(seed, count):
    """Random calls: the flag, the argument, the indent and the start length vary; ticks repeat."""
    rng = random.Random(seed)
    tick = 1000
    for _ in range(count):
        if rng.random() < 0.6:
            tick += rng.choice((1, 15, 16, 1000))
        when = (rng.randint(1, 12), rng.randint(1, 31), rng.randint(0, 23),
                rng.randint(0, 59), rng.randint(0, 59), rng.randint(0, 999))
        flag = rng.choice((0, 1, 1, 1, 2, -1))
        with_stamp = rng.choice((0, 1, 1, 7))
        indent = rng.choice((0, 0, 1, 2, 4, 0x7F, 0x80, 0x81, 0x200, -1, -0x80, rng.randint(1, 0x100)))
        length = rng.randint(0, 0x100)
        filler = bytes([rng.randint(0x21, 0x7E)]) * SPAN
        yield tick, when, flag, with_stamp, indent, length, filler


def compare(rig, model, seed, count):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    for number, (tick, when, flag, with_stamp, indent, length, filler) in enumerate(cases(seed, count)):
        rig.tick, rig.when = tick, when
        text, new_length, new_indent, new_flag, last_tick, stamp = rig.run(filler, length, indent, flag, with_stamp)
        expected, expected_length = model.run(filler, length, indent, flag, with_stamp, tick, when)
        got = (text, new_length, new_indent, new_flag)
        want = (bytes(expected[:SPAN]), expected_length, indent, flag)
        if flag and (last_tick, stamp) != (model.last_tick, model.stamp):
            return f"case {number}: cached tick/stamp {last_tick} {stamp!r} != {model.last_tick} {model.stamp!r}"
        if got != want:
            where = next((i for i in range(SPAN) if text[i] != expected[i]), None)
            what = f"length {new_length} != {expected_length}" if where is None else f"text differs at +{where:#x}"
            return f"case {number} (flag {flag}, arg {with_stamp}, indent {indent}, length {length}): {what}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", help="game.exe 1.14d (default: as emu.py finds it)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=5000)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if args.exe:
        with open(args.exe, "rb") as handle:
            if hashlib.sha256(handle.read()).hexdigest() != SHA256:
                raise SystemExit(f"{args.exe} is not game.exe 1.14d")
    rig = Rig(args.exe)
    difference = compare(rig, Model(), args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL FUN_0041fba0 {difference}")
    print(f"ok: {args.count} FUN_0041fba0 cases ({rig.calls['GetTickCount']} GetTickCount, "
          f"{rig.calls['GetLocalTime']} GetLocalTime)")
    if args.mutation_check:
        for mutation in MUTATIONS:
            caught = compare(Rig(args.exe), Model(mutation), args.seed, args.count)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught}")


if __name__ == "__main__":
    main()
