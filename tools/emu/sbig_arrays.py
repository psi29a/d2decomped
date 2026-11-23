# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's SBig array helpers on local game.exe 1.14d, against a model.

Run: uv run python sbig_arrays.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_00420f80 FUN_00421160

Each case calls one helper on game.exe with a random array record (entry
count +0, used count +4, buffer +8, grow step +0xc, first digit +0x10) in
the register or stack slot it takes, and compares the return value, the
record, a 0x400-byte buffer the allocators hand out, ESP after the return
(the stack arguments popped) and the calls to FUN_004133d0 (SMemReAlloc),
FUN_00413020 (SMemAlloc) and FUN_00412650 (SMemFree), which are hooked
and logged, with the model. `--mutation-check` plants defects in the model
and requires each to be caught. Checks game.exe only: d2d has no SRP or
Warden big numbers (source-sbig.md).
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
REALLOC, ALLOC, FREE = 0x4133D0, 0x413020, 0x412650
TAG_I, TAG_E, TAG_D = 0x708874, 0x708880, 0x70888C
FAKE = 0x5A5A0000
SPAN = 0x400
MUTATIONS = {
    0x420F80: ("count_base_256", "count_no_clamp", "count_ff_counted"),
    0x421160: ("step_cap_0x40", "step_zero_stays", "step_not_kept"),
}
STACK_ARGS = {0x420F80: 0, 0x421160: 1, 0x4211A0: 2, 0x4211C0: 1, 0x421240: 1, 0x421280: 2,
              0x4212D0: 1, 0x421440: 0, 0x421510: 0, 0x421540: 1, 0x421550: 2, 0x4215C0: 2,
              0x421630: 2, 0x421680: 1, 0x4216B0: 1, 0x421870: 1, 0x421BA0: 1}


class Model:
    """What the helpers should do, read from their disassembly (source-sbig.md).

    run() takes the case and returns (EAX or None, {record offset: dword},
    the buffer's expected bytes or None, the hooked calls)."""

    def __init__(self, mutation=None):
        self.m = mutation

    def grow_step(self, rec, n, changes, cap=0x100):
        """FUN_00421160 / FUN_00421240: the highest power of two up to n (0 gives 1); from the cap, the cap kept at +0xc."""
        cap = 0x40 if self.m == "step_cap_0x40" else cap
        if n >= cap:
            if self.m != "step_not_kept":
                changes[0xC] = cap
            return cap
        while n & (n - 1):
            n &= n - 1
        return n if n or self.m == "step_zero_stays" else 1

    def round_up(self, value, step):
        if value % step or self.m == "round_always_adds":
            value = (value + step - value % step) & M32
        return value

    def resize(self, rec, n, case, changes, calls):
        """FUN_004211c0: +0 = n; SMemReAlloc(buffer, n, ".E", -2, 0x10), else a new block, the old bytes copied, the old freed."""
        old = rec[8]
        changes[0] = n
        size = n * 4 if self.m == "resize_dwords" else n
        calls.append(("realloc", old, size, TAG_E, 0xFFFFFFFE, 0 if self.m == "resize_flag_0" else 0x10))
        if case["realloc"]:
            changes[8] = case["realloc"]
            return None
        calls.append(("alloc", size, TAG_E, 0xFFFFFFFE, 0))
        changes[8] = case["new"]
        if not old:
            return None
        count = rec[4] if self.m == "resize_copies_count" else min(n, rec[4])
        buffer = bytearray(SPAN)
        buffer[:count] = case["old_bytes"][:count]
        if self.m != "resize_no_free":
            calls.append(("free", old, TAG_E, 0xFFFFFFFE, 0))
        return bytes(buffer)

    def set_digits(self, rec, n, source, changes, calls):
        """FUN_004215c0: +0 = n; the buffer reallocated to n dwords (".I") unless it and n are 0; n dwords copied; +4 = n."""
        changes[0] = n
        buffer = rec[8]
        if rec[8] or n:
            calls.append(("realloc", rec[8], (n if self.m == "digits_bytes" else n * 4) & M32, TAG_I, 0xFFFFFFFE, 0))
            buffer = changes[8] = DST
        out = bytearray(SPAN)
        count = n - 1 if self.m == "digits_one_short" and n else n
        out[:count * 4] = source[:count * 4]
        if self.m != "digits_used_unchanged":
            changes[4] = n
        return bytes(out) if buffer == DST else None

    def run(self, address, case):
        m, rec, a = self.m, case["rec"], case["args"]
        changes, calls, buffer, eax = {}, [], None, None
        if address == 0x420F80:   # base-255 count up to a 0xff byte, clamped to what is left
            data, size, pos, value = case["data"], case["size"], 0, 0
            while pos < size:
                byte = data[pos]
                pos += 1
                if byte == 0xFF and m != "count_ff_counted":
                    break
                value = (value * (256 if m == "count_base_256" else 255) + byte) & M32
            if m != "count_no_clamp":
                value = min(value, size - pos)
            changes[0x30], changes[0x34] = pos, value
        if address == 0x421160:
            eax = self.grow_step(rec, a[0], changes)
        return eax, changes, buffer, calls


DST = None   # set by the rig: the buffer the allocators hand out


class Rig:
    def __init__(self, exe):
        global DST
        self.machine = emu.Emu(exe)
        self.calls = []
        self.realloc_result = FAKE
        self.machine.hook(REALLOC, lambda e: self.log("realloc", 5, self.realloc_result), 5)
        self.machine.hook(ALLOC, lambda e: self.log("alloc", 4, DST), 4)
        self.machine.hook(FREE, lambda e: self.log("free", 4, 1), 4)
        self.record = self.machine.alloc(0x40)
        self.other = self.machine.alloc(0x40)
        self.data = self.machine.alloc(SPAN)
        self.old = self.machine.alloc(SPAN)
        DST = self.dst = self.machine.alloc(SPAN)

    def log(self, name, count, result):
        self.calls.append((name,) + tuple(self.machine.arg(i) for i in range(count)))
        return result

    def run(self, address, case, regs, args):
        machine = self.machine
        machine.mu.mem_write(self.record, struct.pack("<16I", *[case["rec"].get(o, 0) for o in range(0, 0x40, 4)]))
        machine.mu.mem_write(self.dst, bytes(SPAN))
        self.realloc_result = case.get("realloc", FAKE)
        self.calls.clear()
        eax = machine.call(address, *args, ecx=regs.pop("ecx", 0), regs=regs)
        popped = machine.mu.reg_read(UC_X86_REG_ESP) - (emu.STACK + emu.STACK_SIZE - 0x1000) + 4 * (1 + len(args))
        record = struct.unpack("<16I", machine.read(self.record, 0x40))
        changes = {o: v for o, v in zip(range(0, 0x40, 4), record) if v != case["rec"].get(o, 0)}
        dst = machine.read(self.dst, SPAN)
        return eax, changes, dst, list(self.calls), popped


def word(rng, small=0x40):
    return rng.choice((0, 1, 2, 3, 0x7F, 0xFF, 0x100, 0x101, 0x80000000, M32, rng.randint(0, small), rng.randint(0, M32)))


def make_case(rng, rig, address):
    """The record, the registers and stack arguments, and what the hooks hand back."""
    machine = rig.machine
    digits = [rng.randint(0, M32) for _ in range(SPAN // 4)]
    machine.mu.mem_write(rig.data, struct.pack(f"<{SPAN // 4}I", *digits))
    rec = {0: word(rng), 4: word(rng), 8: rig.data, 0xC: rng.choice((0, 0, word(rng))), 0x10: rng.randint(0, 8)}
    if rng.random() < 0.5:
        rec[4] = rng.randint(0, 0x18)
    case = {"rec": rec, "digit": lambda i: digits[i] if i < len(digits) else None, "realloc": FAKE}
    regs, args = {"ecx": rig.record}, []
    if address == 0x420F80:
        head = bytes(rng.choice((rng.randint(0, 4), rng.randint(0, 0xFE))) for _ in range(rng.randint(0, 3)))
        data = (head + bytes([0xFF]) * (rng.random() < 0.8)
                + bytes(rng.choice((0xFF, rng.randint(0, 0xFE))) for _ in range(SPAN)))[:SPAN]
        machine.mu.mem_write(rig.data, data)
        case["data"], case["size"] = data, rng.choice((0, 1, 2, rng.randint(0, 8), rng.randint(0, SPAN)))
        rec[0x30], rec[0x34] = word(rng), word(rng)
        regs = {"ebx": rig.data, "edi": case["size"], "eax": rig.record + 0x30, "esi": rig.record + 0x34}
    elif address in (0x421160, 0x421240):
        args = [word(rng, 0x200)]
    elif address in (0x4211A0, 0x421280):
        regs, args = {}, [word(rng), rng.choice((1, 2, 4, 0x10, 0x100, rng.randint(1, M32)))]
    elif address == 0x4211C0:
        old = bytes(rng.randint(0, 0xFF) for _ in range(SPAN))
        machine.mu.mem_write(rig.old, old)
        rec[8] = rng.choice((0, rig.old))
        rec[4] = rng.randint(0, SPAN)
        case.update(realloc=rng.choice((0, FAKE)), new=rig.dst, old_bytes=old)
        args = [rng.randint(0, SPAN)]
    elif address == 0x4212D0:
        args = [rng.choice((0, 1, rng.randint(0, 0x20), M32))]
    elif address in (0x421440, 0x421510):
        regs = {"edx": rig.record} if address == 0x421440 else {"eax": rig.record, "edx": rig.record + 0x38}
    elif address in (0x421540, 0x421550):
        regs = {}
        args = [rig.record] + ([rig.record + 0x38] if address == 0x421550 else [])
    elif address == 0x4215C0:
        rec[8] = rng.choice((0, rig.data))
        case["source"] = machine.read(rig.old, SPAN)
        args = [rng.choice((0, rng.randint(0, SPAN // 4))), rig.old]
        case["realloc"] = rig.dst
    elif address == 0x421630:
        args = [word(rng, 0x400), rng.choice((0, 1, word(rng)))]
        rec[0] = rng.choice((word(rng, 0x400), rng.randint(0, 0x400)))
        rec[4] = rng.randint(0, 0x400)
        if rng.random() < 0.2:   # exactly full, or one short
            rec[0] = (args[0] + rec[4] - rng.choice((0, 1))) & M32
    elif address in (0x421680, 0x4216B0):
        rec[8] = rng.choice((0, rig.data))
        args = [rng.choice((0, word(rng)))]
    elif address in (0x421870, 0x421BA0):
        case["self"] = rng.random() < 0.2
        src = {4: rng.choice((0, rng.randint(0, SPAN // 4))), 8: rig.old, 0xC: word(rng)}
        machine.mu.mem_write(rig.other, struct.pack("<16I", *[src.get(o, 0) for o in range(0, 0x40, 4)]))
        case["source"] = machine.read(rig.old, SPAN)
        if case["self"]:
            src, case["own"] = None, machine.read(rig.data, SPAN)
        case["src_rec"] = src
        case["dest"] = rig.record
        case["realloc"] = rig.dst
        args = [rig.record if case["self"] else rig.other]
    case["args"] = args
    return case, regs, args


def compare(rig, model, seed, count, only=None):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    for number in range(count):
        for address in sorted(MUTATIONS) if only is None else [only]:
            case, regs, args = make_case(rng, rig, address)
            eax, changes, dst, calls, popped = rig.run(address, case, dict(regs), args)
            want_eax, want_changes, want_dst, want_calls = model.run(address, case)
            want_changes = {k: v & M32 for k, v in want_changes.items() if v & M32 != case["rec"].get(k, 0)}
            got = (eax if want_eax is not None else None, changes, dst if want_dst is not None else None,
                   [c[1:] if c[0] != "x" else c for c in calls], popped)
            want = (want_eax, want_changes, want_dst, [c[1:] for c in want_calls], 4 * STACK_ARGS[address] + 4)
            if got[0] != want[0] or got[1] != want[1] or got[3] != want[3] or got[4] != want[4] or got[2] != want[2]:
                what = [i for i in range(5) if got[i] != want[i]]
                show = lambda v: v if not isinstance(v, bytes) else v[:16].hex()
                return (f"case {number} FUN_{address:08x} args {[hex(x) for x in args]}: part {what}: "
                        f"{[show(got[i]) for i in what]} != {[show(want[i]) for i in what]}")
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
    rig = Rig(str(exe))
    difference = compare(rig, Model(), args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} calls each of {len(MUTATIONS)} helpers")
    if args.mutation_check:
        for address, mutations in MUTATIONS.items():
            for mutation in mutations:
                caught = compare(rig, Model(mutation), args.seed, args.count, address)
                if not caught:
                    raise SystemExit(f"FAIL mutation {mutation} not caught")
                print(f"caught {mutation}: {caught[:150]}")


if __name__ == "__main__":
    main()
