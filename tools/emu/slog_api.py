# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's SLog calls on local game.exe 1.14d, against a model.

Run: uv run python slog_api.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041fdf0 FUN_0041ffc0

Each case gives a pool of eight log records (made by game.exe's own
FUN_0041fa80, create on) fresh fields: a file handle `+8`, a length `+0xc`,
a committed length `+0x10` (at most the length), an indent `+0x14`, a stamp
flag `+0x18` and printable text ending in a 0 at the length; then runs one
SLog call on game.exe and the same call on the model, and compares the
records, the return value, and what reached WriteFile, FlushFileBuffers,
OutputDebugStringA, CloseHandle, VirtualFree, DeleteCriticalSection and the
handle check FUN_0041d1e0 (hooked). Every call must leave each of the four
sections 0x779238 + i * 0x18 as often as it enters it. A tenth of the cases
name a log id with no record (0, 7, 0x10, 0xffffffff): nothing may happen.
GetTickCount and GetLocalTime are fixed, so the stamp FUN_0041fba0 writes is
"11/23 22:05:09.042  " (20 characters; slog_stamp.py checks it).
wsprintfA is emu.py's stub, so the dump's formats come from game.exe's
strings but are expanded by Python. `--mutation-check` plants defects in the
model and requires each to be caught. Checks game.exe only: d2d has no
counterpart (source-frontier.md).
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
RECORD_FIND, UNLOCK, HANDLE_CHECK = 0x41FA80, 0x41FCE0, 0x41D1E0
DUMP = 0x41FDF0
FLUSH = 0x41FFC0
DEBUG_FLAG, LOCKS, LOCK_SIZE = 0x7790AC, 0x779238, 0x18
TEXT, BUFFER, FLUSH_AT = 0x1C, 0x10000, 0xC000
STAMP = b"11/23 22:05:09.042  "
POOL_IDS = (1, 2, 3, 4, 5, 6, 0x7FFFFFFF, 0xFFFFFFFE)
NO_RECORD = (0, 7, 0x10, 0xFFFFFFFF)
MUTATIONS = {
    "dump": ("dump_hex_gap_one_space", "dump_ascii_gap_every_4", "dump_zero_as_dot",
             "dump_no_stamp_pad", "dump_no_commit", "dump_flush_above_0xc000"),
    "flush": ("flush_keeps_length", "flush_writes_committed"),
}


class Rec:
    """One log record's fields and text (the first `size` bytes of its buffer)."""

    def __init__(self, handle, length, committed, indent, flag, text):
        self.handle, self.length, self.committed = handle, length, committed
        self.indent, self.flag, self.text = indent, flag, bytearray(text)

    def key(self, size):
        return (self.handle, self.length, self.committed, self.indent, self.flag, bytes(self.text[:size]))


class Model:
    """What the SLog calls should do, read from their disassembly (source-frontier.md)."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def prefix(self, rec, stamp):
        """FUN_0041fba0: nothing unless the flag; the stamp (or as many spaces) and the indent."""
        if not rec.flag:
            return b""
        head = STAMP if stamp else b" " * len(STAMP)
        if self.mutation == "dump_no_stamp_pad" and not stamp:
            head = b""
        return head + b" " * min(max(rec.indent, 0), 0x80)

    def write(self, rec, data):
        rec.text[rec.length:rec.length + len(data) + 1] = data + b"\0"
        rec.length += len(data)

    def flush_record(self, rec, events, committed_only=False):
        """FUN_0041fa40: the text so far to the file, then both lengths 0."""
        size = rec.committed if committed_only else rec.length
        if rec.length:
            events += [("write", rec.handle, bytes(rec.text[:size])), ("flush", rec.handle)]
            rec.length = rec.committed = 0

    def dump(self, rec, data, debug, events):
        """FUN_0041fdf0: eight bytes a line, hex then text, the line committed and maybe flushed."""
        hex_gap = b" " if self.mutation == "dump_hex_gap_one_space" else b"  "
        text_gap = 3 if self.mutation == "dump_ascii_gap_every_4" else 7
        for base in range(0, len(data), 8):
            line = self.prefix(rec, False) + b"%04x: " % base
            for i in range(base, base + 8):
                line += b"%02x " % data[i] if i < len(data) else b"   "
                line += hex_gap if i & 3 == 3 else b""
            for i in range(base, base + 8):
                c = data[i] if i < len(data) else 0
                if 0x20 <= c <= 0x7E:
                    line += bytes([c])
                else:
                    line += b"." if c or self.mutation == "dump_zero_as_dot" and i < len(data) else b" "
                line += b" " if i & text_gap == 3 else b""
            self.write(rec, line + b"\r\n")
        if debug:
            events.append(("debug", bytes(rec.text[rec.committed:rec.text.index(0, rec.committed)])))
        if self.mutation != "dump_no_commit":
            rec.committed = rec.length
        if rec.length >= FLUSH_AT + (1 if self.mutation == "dump_flush_above_0xc000" else 0):
            self.flush_record(rec, events)

    def flush(self, rec, events):
        """FUN_0041ffc0: FUN_0041fa40 on the record, pending text included."""
        if self.mutation == "flush_keeps_length":
            if rec.length:
                events += [("write", rec.handle, bytes(rec.text[:rec.length])), ("flush", rec.handle)]
            return
        self.flush_record(rec, events, self.mutation == "flush_writes_committed")


def wrap(value):
    return struct.unpack("<i", struct.pack("<I", value & 0xFFFFFFFF))[0]


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.events = []
        self.locks = {}
        imports = {"EnterCriticalSection": self.enter, "LeaveCriticalSection": self.leave,
                   "WriteFile": self.write_file, "FlushFileBuffers": self.flush_file_buffers,
                   "OutputDebugStringA": self.debug_out, "CloseHandle": self.close_handle,
                   "VirtualFree": self.virtual_free, "DeleteCriticalSection": self.delete,
                   "GetTickCount": lambda machine: (1234, 0), "GetLocalTime": self.local_time}
        for address, (slot, _) in list(self.machine.slots.items()):
            if slot in imports:
                self.machine.slots[address] = (slot, imports[slot])
        self.machine.hook(HANDLE_CHECK, self.handle_check, 1)
        self.out = self.machine.alloc(4)
        self.strings = self.machine.alloc(0x200)
        self.make_pool()

    def enter(self, machine):
        self.locks[machine.arg(0)] = self.locks.get(machine.arg(0), 0) + 1
        return 0, 1

    def leave(self, machine):
        self.locks[machine.arg(0)] = self.locks.get(machine.arg(0), 0) - 1
        return 0, 1

    def write_file(self, machine):
        self.events.append(("write", machine.arg(0), machine.read(machine.arg(1), machine.arg(2))))
        machine.w32(machine.arg(3), machine.arg(2))
        return 1, 5

    def flush_file_buffers(self, machine):
        self.events.append(("flush", machine.arg(0)))
        return 1, 1

    def debug_out(self, machine):
        self.events.append(("debug", machine.cstr(machine.arg(0)).encode("latin-1")))
        return 0, 1

    def close_handle(self, machine):
        self.events.append(("close", machine.arg(0)))
        return 1, 1

    def virtual_free(self, machine):
        self.events.append(("free", machine.arg(0)))
        return 1, 3

    def delete(self, machine):
        self.events.append(("delete", machine.arg(0)))
        return 0, 1

    def handle_check(self, machine):
        self.events.append(("check", machine.cstr(machine.arg(0)).encode("latin-1")))
        return 0

    def local_time(self, machine):
        machine.mu.mem_write(machine.arg(0), struct.pack("<8H", 2026, 11, 1, 23, 22, 5, 9, 42))
        return 0, 1

    def make_pool(self):
        """The eight records, made by FUN_0041fa80 (create on) and unlocked again."""
        self.pool = []
        for log_id in POOL_IDS:
            record = self.machine.call(RECORD_FIND, self.out, 1, regs={"edi": log_id})
            self.machine.call(UNLOCK, regs={"eax": self.machine.r32(self.out)})
            self.pool.append((log_id, record))

    def load(self, record, rec):
        machine = self.machine
        machine.mu.mem_write(record + 8, struct.pack("<I4i", rec.handle, rec.length, rec.committed,
                                                    rec.indent, rec.flag))
        machine.mu.mem_write(record + TEXT, bytes(rec.text))

    def state(self, record, size):
        handle, length, committed, indent, flag = struct.unpack("<I4i", self.machine.read(record + 8, 20))
        return (handle, length, committed, indent, flag, self.machine.read(record + TEXT, size))

    def run(self, address, *args, cdecl=False):
        self.events.clear()
        self.locks.clear()
        result = self.machine.call(address, *args, cdecl=cdecl)
        held = {lock: count for lock, count in self.locks.items() if count}
        return result, list(self.events), held


def fresh(rng, handle):
    """A record's fields: printable text up to the length, a 0 there, filler after."""
    length = rng.choice((0, rng.randint(1, 0x200), rng.randint(0x200, 0xB000), rng.randint(0xBF00, 0xBFFF)))
    committed = rng.choice((length, rng.randint(0, length)))
    indent = rng.choice((0, 0, 1, 4, 0x80, 0x81, -1, rng.randint(-0x100, 0x100)))
    filler = bytes([rng.randint(0x21, 0x7E)])
    text = bytearray(filler * BUFFER)
    text[length] = 0
    return Rec(handle, length, committed, indent, rng.choice((0, 1)), text)


def case_dump(rig, model, rng, rec, log_id, record):
    size = rng.choice((0, rng.randint(1, 0x40), rng.randint(0x40, 0x180)))
    data = bytes(rng.choice((0, rng.randint(1, 0x1F), rng.randint(0x20, 0x7E), rng.randint(0x7F, 0xFF)))
                 for _ in range(size))
    debug = rng.choice((0, 1))
    if record and size and rng.random() < 0.2:   # end the dump right at the flush mark, or one short
        probe = Rec(rec.handle, 0, 0, rec.indent, rec.flag, bytearray(BUFFER))
        Model().dump(probe, data, 0, [])
        rec.length = rec.committed = FLUSH_AT - probe.length - rng.choice((0, 1))
        rec.text[rec.length] = 0
        rig.load(record, rec)
    source = rig.strings
    rig.machine.mu.mem_write(source, data)
    rig.machine.w32(DEBUG_FLAG, debug)
    result = rig.run(DUMP, log_id, source, size)
    events = []
    if record:
        model.dump(rec, data, debug, events)
    return f"FUN_0041fdf0 {size:#x} bytes (debug {debug})", None, events, result


def case_flush(rig, model, rng, rec, log_id, record):
    result = rig.run(FLUSH, log_id)
    events = []
    if record:
        model.flush(rec, events)
    return "FUN_0041ffc0", None, events, result

CASES = {"dump": case_dump}
CASES["flush"] = case_flush


def compare(rig, model, seed, count):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    kinds = sorted(CASES)
    for number in range(count):
        slot = rng.randrange(len(rig.pool))
        log_id, record = rig.pool[slot]
        rec = fresh(rng, 0x100 + slot)
        rig.load(record, rec)
        if rng.random() < 0.1:
            log_id, record = rng.choice(NO_RECORD), 0
        before = rec.key(BUFFER)
        what, want, events, (result, got, held) = CASES[rng.choice(kinds)](rig, model, rng, rec, log_id, record)
        expected = rec.key(BUFFER) if record else before
        state = rig.state(rig.pool[slot][1], BUFFER)
        if state != expected:
            field = next(i for i in range(6) if state[i] != expected[i])
            if field < 5:
                return f"case {number} (id {log_id:#x}): {what}: field {field}: {state[field]} != {expected[field]}"
            where = next(i for i in range(BUFFER) if state[5][i] != expected[5][i])
            return (f"case {number} (id {log_id:#x}): {what}: text at +{where:#x}: "
                    f"{state[5][where:where + 24]!r} != {expected[5][where:where + 24]!r}")
        if got != events:
            return f"case {number} (id {log_id:#x}): {what}: events {str(got)[:300]} != {str(events)[:300]}"
        if want is not None and wrap(result) != want:
            return f"case {number} (id {log_id:#x}): {what}: returned {wrap(result)} != {want}"
        if held:
            return f"case {number} (id {log_id:#x}): {what}: sections left held {held}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d (default: bin/game.exe in the data folder emu.py finds)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    difference = compare(Rig(str(exe)), Model(), args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} cases ({', '.join(sorted(MUTATIONS))})")
    if args.mutation_check:
        for mutation in (m for group in MUTATIONS.values() for m in group):
            caught = compare(Rig(str(exe)), Model(mutation), args.seed, args.count)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:160]}")


if __name__ == "__main__":
    main()
