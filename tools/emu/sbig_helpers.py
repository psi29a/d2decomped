# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's small SBig helpers on local game.exe 1.14d, against a model.

Run: uv run python sbig_helpers.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_00420db0 FUN_00420dd0 FUN_00420e00 FUN_00420e10 FUN_00420eb0 FUN_00420f30 FUN_00420f60 FUN_00420fd0 FUN_00420fe0

Each case calls one helper on game.exe with random registers and arguments
(the number or pool in ECX, the 64-bit accumulator at ESI, the high dword in
EAX, as each takes them) and compares the return value (EDX:EAX for the
64-bit ones), the memory it may write and ESP after the return (the stack
arguments popped) with the model. FUN_004133d0 (SMemReAlloc) is hooked and
its five arguments logged. `--mutation-check` plants defects in the model
and requires each to be caught. Checks game.exe only: d2d has no SRP or
Warden big numbers (source-sbig.md).
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32, M64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
REALLOC, INT_TAG = 0x4133D0, 0x708874
MUTATIONS = {
    0x420DB0: ("below_signed", "below_or_equal", "below_no_offset"),
    0x420DD0: ("take_stride_0x10", "take_no_count", "take_returns_next"),
    0x420E00: ("release_adds",),
    0x420E10: ("take_if_inverted", "take_if_no_bump"),
    0x420EB0: ("carry_dropped", "add_returns_high", "add_keeps_low"),
    0x420F30: ("shift_keeps_high", "shift_by_16"),
    0x420F60: ("make_swapped", "make_low_only"),
    0x420FD0: ("entry_stride_1",),
    0x420FE0: ("last_one_past", "last_empty_not_null"),
}
STACK_ARGS = {0x420DB0: 1, 0x420DD0: 1, 0x420E00: 1, 0x420E10: 2, 0x420EB0: 2, 0x420F30: 1,
              0x420F60: 1, 0x420FD0: 1, 0x420FE0: 0, 0x421000: 1, 0x4210E0: 2, 0x421110: 1}


class Model:
    """What the helpers should do, read from their disassembly (source-sbig.md)."""

    def __init__(self, mutation=None):
        self.m = mutation

    def run(self, address, regs, args, mem):
        """Returns (EAX, EDX or None, the memory dwords it changes as {offset: value}, realloc calls)."""
        m, ecx = self.m, regs.get("ecx", 0)
        num = lambda off: mem.get(off, 0)
        if address == 0x420DB0:   # +0x10 + n below the count +4
            left = (num(0x10) + args[0]) & M32 if m != "below_no_offset" else args[0]
            if m == "below_signed":
                return int(s32(left) < s32(num(4))), None, {}, []
            return int(left <= num(4) if m == "below_or_equal" else left < num(4)), None, {}, []
        if address == 0x420DD0:   # the pool's next temporary, an optional counter bumped
            top, changes = num(0x140), {}
            if args[0] and m != "take_no_count":
                changes["counter"] = (mem["counter"] + 1) & M32
            changes[0x140] = (top + 1) & M32
            stride = 0x10 if m == "take_stride_0x10" else 0x14
            return (ecx + (top + (m == "take_returns_next")) * stride) & M32, None, changes, []
        if address == 0x420E00:   # the pool's top back by n
            return None, None, {0x140: (num(0x140) + (args[0] if m == "release_adds" else -args[0])) & M32}, []
        if address == 0x420E10:   # the given number, or a temporary when the flag is set
            if (args[1] == 0) != (m == "take_if_inverted"):
                return args[0], None, {}, []
            top = num(0x140)
            return (ecx + top * 0x14) & M32, None, ({} if m == "take_if_no_bump" else {0x140: (top + 1) & M32}), []
        if address == 0x420EB0:   # 64-bit accumulator + (hi:lo): low dword back, accumulator >>= 32
            acc = num(0) | num(4) << 32
            total = acc + (args[0] | args[1] << 32)
            if m == "carry_dropped":
                total &= M64
            low, rest = total & M32, total >> 32
            if m == "add_keeps_low":
                rest = total & M64
            return (rest & M32 if m == "add_returns_high" else low), None, {0: rest & M32, 4: rest >> 32 & M32}, []
        if address == 0x420F30:   # accumulator = its low dword : the digit
            high = num(0 if m != "shift_keeps_high" else 4)
            acc = ((high << (16 if m == "shift_by_16" else 32)) + args[0]) & M64
            return None, None, {0: acc & M32, 4: acc >> 32}, []
        if address == 0x420F60:   # EDX:EAX = EAX : the digit
            eax = regs["eax"]
            if m == "make_swapped":
                return eax, args[0], {}, []
            return args[0], (0 if m == "make_low_only" else eax), {}, []
        if address == 0x420FD0:   # &buffer[i]
            return (num(8) + args[0] * (1 if m == "entry_stride_1" else 4)) & M32, None, {}, []
        if address == 0x420FE0:   # &buffer[count - 1], 0 when empty
            if num(4) == 0 and m != "last_empty_not_null":
                return 0, None, {}, []
            return (num(8) + (num(4) - (0 if m == "last_one_past" else 1)) * 4) & M32, None, {}, []
        raise ValueError(hex(address))


def s32(value):
    return struct.unpack("<i", struct.pack("<I", value & M32))[0]


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.calls = []
        self.machine.hook(REALLOC, self.realloc, 5)
        self.block = self.machine.alloc(0x200)
        self.counter = self.machine.alloc(4)

    def realloc(self, machine):
        self.calls.append(tuple(machine.arg(i) for i in range(5)))
        return 0x5A5A0000

    def run(self, address, regs, args, mem):
        """Writes the block's dwords, calls, returns (EAX, EDX, changed dwords, realloc calls, popped)."""
        machine = self.machine
        machine.mu.mem_write(self.block, bytes(0x200))
        for offset, value in mem.items():
            machine.w32(self.counter if offset == "counter" else self.block + offset, value)
        self.calls.clear()
        call_args = [self.counter if a == "counter" else a for a in args]
        registers = {k: (self.block if v == "block" else v) for k, v in regs.items() if k != "ecx"}
        ecx = self.block if regs.get("ecx") == "block" else regs.get("ecx", 0)
        eax = machine.call(address, *call_args, ecx=ecx, regs=registers)
        popped = machine.mu.reg_read(UC_X86_REG_ESP) - (emu.STACK + emu.STACK_SIZE - 0x1000) + 4 * (1 + len(args))
        edx = machine.mu.reg_read(UC_X86_REG_EDX)
        changes = {}
        for offset in range(0, 0x200, 4):
            value = machine.r32(self.block + offset)
            if value != mem.get(offset, 0):
                changes[offset] = value
        if "counter" in mem and machine.r32(self.counter) != mem["counter"]:
            changes["counter"] = machine.r32(self.counter)
        return eax, edx, changes, list(self.calls), popped


def word(rng):
    return rng.choice((0, 1, 2, 0x7FFFFFFF, 0x80000000, M32, rng.randint(0, 0x40), rng.randint(0, M32)))


def case(rng, address, block):
    """Registers, stack arguments and the block's starting dwords for one call."""
    mem = {off: word(rng) for off in (0, 4, 8, 0xC, 0x10)}
    regs, args = {"ecx": "block"}, []
    if address in (0x420DD0, 0x420E00, 0x420E10):
        mem[0x140] = rng.randint(0, 15) if rng.random() < 0.8 else word(rng)
    if address == 0x420DD0:
        mem["counter"] = word(rng)
        args = [rng.choice(("counter", 0))]
    elif address == 0x420E10:
        args = [word(rng), rng.choice((0, 1, word(rng)))]
    elif address in (0x420EB0, 0x420F30):
        regs = {"esi": "block"}
        args = [word(rng), word(rng)][:STACK_ARGS[address]]
    elif address == 0x420F60:
        regs = {"eax": word(rng)}
        args = [word(rng)]
    elif address == 0x4210E0:
        regs, args = {}, [word(rng), rng.choice((1, 2, 4, 0x10, 0x40, rng.randint(1, M32)))]
    elif address == 0x421110:
        mem[8] = rng.choice((0, word(rng)))
        args = [rng.choice((0, word(rng)))]
    else:
        args = [word(rng) for _ in range(STACK_ARGS[address])]
    if address == 0x420FE0:
        mem[4] = rng.choice((0, word(rng)))
    return regs, args, mem


def compare(rig, model, seed, count, only=None):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    addresses = sorted(MUTATIONS) if only is None else [only]
    for number in range(count):
        for address in addresses:
            regs, args, mem = case(rng, address, rig.block)
            eax, edx, changes, calls, popped = rig.run(address, regs, args, mem)
            model_regs = {k: (rig.block if v == "block" else v) for k, v in regs.items()}
            want_eax, want_edx, want_changes, want_calls = model.run(address, model_regs, args, mem)
            want_changes = {k: v for k, v in want_changes.items() if v != mem.get(k, 0)}
            got = ((eax if want_eax is not None else None), (edx if want_edx is not None else None), changes, calls, popped)
            want = (want_eax, want_edx, want_changes, want_calls, 4 * STACK_ARGS[address] + 4)
            if got != want:
                return f"case {number} FUN_{address:08x} {regs} {[hex(a) if isinstance(a, int) else a for a in args]}: {got} != {want}"
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
    print(f"ok: {args.count} calls each of {len(MUTATIONS)} helpers")
    if args.mutation_check:
        rig = Rig(str(exe))
        for address, mutations in MUTATIONS.items():
            for mutation in mutations:
                caught = compare(rig, Model(mutation), args.seed, args.count, address)
                if not caught:
                    raise SystemExit(f"FAIL mutation {mutation} not caught")
                print(f"caught {mutation}: {caught[:150]}")


if __name__ == "__main__":
    main()
