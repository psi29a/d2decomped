# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's small log helpers on local game.exe 1.14d, against a model.

Run: uv run python slog_helpers.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041fb80 FUN_0041fce0

FUN_0041fce0 (slog_unlock) takes a lock index in EAX and leaves the critical
section 0x779238 + index * 0x18. Each case runs the record lookup
FUN_0041fa80 (EDI the log id, create on) for real, then FUN_0041fce0 with
the index the lookup wrote out, as its callers do, and checks that the
section left is the one the lookup entered (id & 3). EnterCriticalSection,
LeaveCriticalSection and VirtualAlloc are emu.py's stubs, logged here.
FUN_0041fb80 (slog_newline) takes the log record in ESI and copies the three
bytes at 0x6cff08 (CR, LF, 0) to the end of its text (+0x1c + the length
+0xc), then adds 2 to the length: each case writes a record with a random
length over a filled buffer and compares the record with the model.
`--mutation-check` plants defects in the model and requires each to be
caught. Checks game.exe only: d2d has no counterpart (source-frontier.md).
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
NEWLINE, RECORD_FIND, UNLOCK = 0x41FB80, 0x41FA80, 0x41FCE0
TEXT, SPAN = 0x1C, 0x400   # the record's text buffer, how much of it a case compares
LOCKS, LOCK_SIZE = 0x779238, 0x18
MUTATIONS = ("newline_lf_only", "newline_counts_terminator", "newline_no_terminator",
             "unlock_stride_0x10", "unlock_masked", "unlock_base_0x779298")


class Model:
    """What the helpers should do, read from their disassembly (source-frontier.md)."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def newline(self, buffer, length):
        """FUN_0041fb80: CR LF and a 0 at the end of the text; the length grows by 2."""
        buffer = bytearray(buffer)
        ending = b"\r\n\0"
        if self.mutation == "newline_lf_only":
            ending = b"\n\0"
        if self.mutation == "newline_no_terminator":
            ending = b"\r\n"
        buffer[length:length + len(ending)] = ending
        return bytes(buffer), length + (3 if self.mutation == "newline_counts_terminator" else 2)

    def lock_of(self, index):
        """FUN_0041fce0: the critical section an index names."""
        stride = 0x10 if self.mutation == "unlock_stride_0x10" else LOCK_SIZE
        base = 0x779298 if self.mutation == "unlock_base_0x779298" else LOCKS
        if self.mutation == "unlock_masked":
            index &= 3
        return (base + index * stride) & 0xFFFFFFFF


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.log = []
        imports = {"EnterCriticalSection": self.enter, "LeaveCriticalSection": self.leave}
        for address, (slot, _) in list(self.machine.slots.items()):
            if slot in imports:
                self.machine.slots[address] = (slot, imports[slot])
        self.out = self.machine.alloc(4)
        self.record = self.machine.alloc(TEXT + SPAN + 0x10)

    def enter(self, machine):
        self.log.append(("enter", machine.arg(0)))
        return 0, 1

    def leave(self, machine):
        self.log.append(("leave", machine.arg(0)))
        return 0, 1

    def find_then_unlock(self, log_id):
        """FUN_0041fa80(&index, 1) with EDI = id, then FUN_0041fce0 with EAX = index."""
        self.log.clear()
        record = self.machine.call(RECORD_FIND, self.out, 1, regs={"edi": log_id})
        index = self.machine.r32(self.out)
        self.machine.call(UNLOCK, regs={"eax": index})
        return record, index, list(self.log)

    def newline(self, buffer, length):
        """FUN_0041fb80 with ESI = a record of that length and text: its new text and length."""
        machine = self.machine
        record = self.record
        machine.mu.mem_write(record, struct.pack("<7i", 0, 0, 0, length, 0, 0, 0) + buffer)
        machine.call(NEWLINE, regs={"esi": record})
        return machine.read(record + TEXT, SPAN), machine.s32(record + 0xC)

    def unlock(self, index):
        """FUN_0041fce0 alone with any EAX: the section it leaves."""
        self.log.clear()
        self.machine.call(UNLOCK, regs={"eax": index})
        return list(self.log)


def compare(rig, model, seed, count):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    for number in range(count):
        log_id = rng.choice((1, 2, 3, 4, 5, 0x7FFFFFFF, 0xFFFFFFFF, rng.randint(1, 0xFFFFFFFF)))
        length = rng.randint(0, SPAN - 3)
        filler = bytes([rng.randint(0x21, 0x7E)]) * SPAN
        (text, new_length), (expected, expected_length) = rig.newline(filler, length), model.newline(filler, length)
        if (text, new_length) != (expected[:SPAN], expected_length):
            where = next((i for i in range(SPAN) if text[i] != expected[i]), None)
            what = f"length {new_length} != {expected_length}" if where is None else f"text differs at +{where:#x}"
            return f"case {number}: FUN_0041fb80 at length {length:#x}: {what}"
        record, index, log = rig.find_then_unlock(log_id)
        lock = LOCKS + (log_id & 3) * LOCK_SIZE
        want = [("enter", lock), ("leave", model.lock_of(index))]
        if not record or index != log_id & 3 or log != want:
            return f"case {number} (id {log_id:#x}): record {record:#x}, index {index}, {log} != {want}"
        index = rng.randint(0, 0xFFFFFFFF) if rng.random() < 0.5 else rng.randint(0, 3)
        want = [("leave", model.lock_of(index))]
        if (log := rig.unlock(index)) != want:
            return f"case {number} (EAX {index:#x}): {log} != {want}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d (default: bin/game.exe in the data folder emu.py finds)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=2000)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    difference = compare(Rig(str(exe)), Model(), args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} FUN_0041fb80 lines, {args.count} FUN_0041fa80 + FUN_0041fce0 pairs, {args.count} FUN_0041fce0 alone")
    if args.mutation_check:
        for mutation in MUTATIONS:
            caught = compare(Rig(str(exe)), Model(mutation), args.seed, args.count)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught}")


if __name__ == "__main__":
    main()
