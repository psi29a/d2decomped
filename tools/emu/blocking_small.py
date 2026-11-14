# SPDX-License-Identifier: GPL-3.0-or-later
"""Check eight small game.exe 1.14d functions that listed gaps name.

    uv run python blocking_small.py --exe /path/to/game.exe [--mutation-check]

FUN_006492a0 (squared distance of two points), FUN_00483340 and FUN_00487d30
(each stores one constant in a global), FUN_00551ff0 (returns 1),
FUN_004f68e0 (forwards seven of its nine stack arguments to the renderer
vtable slot +0x7c), FUN_0064a6f0 (a missile's stream word: unit +0x14 -> +0x30),
FUN_004cdb40 (fills a 0x5c-byte request and calls FUN_004cd540) and
FUN_005b2490 (unit lookup, then FUN_005b23c0 with a corpse-state unit).
Callees outside the claim are hooked; no MPQs are read.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MASK = 0xFFFFFFFF
MUTATIONS = ("dist-swapped-axes", "dist-saturates", "forward-wrong-arg", "stream-any-type", "stream-wrong-offset",
             "request-no-null-check", "request-wrong-slot", "corpse-skips-check", "corpse-wrong-order", "byte-store-as-dword")
mutation = None


def signed(value):
    """A 32-bit word as a signed int."""
    return value - (1 << 32) if value & 0x80000000 else value


def model_dist(a0, a1, a2, a3):
    """FUN_006492a0: (a2 - a0)^2 + (a3 - a1)^2 in 32-bit wrap-around, args pushed x1, y1, x2, y2."""
    if mutation == "dist-swapped-axes":
        a1, a2 = a2, a1
    dx, dy = signed((a2 - a0) & MASK), signed((a3 - a1) & MASK)
    value = dx * dx + dy * dy
    return min(value, MASK) if mutation == "dist-saturates" else value & MASK


def check_dist(machine, rng):
    """FUN_006492a0 on edge values and random points, stdcall with four stack words."""
    edges = (0, 1, MASK, 0x7FFFFFFF, 0x80000000, 46341, 0x10000)
    cases = [tuple(rng.choice(edges) for _ in range(4)) for _ in range(200)]
    cases += [tuple(rng.randrange(-4000, 4000) & MASK for _ in range(4)) for _ in range(800)]
    cases += [tuple(rng.getrandbits(32) for _ in range(4)) for _ in range(200)]
    for case in cases:
        got = machine.call(0x6492A0, *case, limit=100)
        if got != model_dist(*case):
            raise AssertionError(f"FUN_006492a0{case}: game.exe {got:#x}, model {model_dist(*case):#x}")
    return len(cases)


def popped(machine, address, *args, **regs):
    """Call address; return (eax, argument words the callee popped with `ret n`)."""
    value = machine.call(address, *args, limit=500, **regs)
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    return value, (machine.mu.reg_read(UC_X86_REG_ESP) - (top - 4 * (len(args) + 1))) // 4 - 1


def check_constants(machine):
    """FUN_00483340 stores the byte 1 in 0x7bc9d8, FUN_00487d30 the dword 7 in 0x7bcbf0; FUN_00551ff0 returns 1 and pops one word."""
    machine.w32(0x7BC9DC, 0x11223344)
    for before in (0, 0xDEADBEEF):
        machine.w32(0x7BC9D8, before)
        machine.call(0x483340, limit=100)
        expect = (before & ~0xFF | 1) if mutation != "byte-store-as-dword" else 1
        if machine.r32(0x7BC9D8) != expect or machine.r32(0x7BC9DC) != 0x11223344:
            raise AssertionError(f"FUN_00483340 left {machine.r32(0x7BC9D8):#x}")
        machine.w32(0x7BCBF0, before)
        machine.call(0x487D30, limit=100)
        if machine.r32(0x7BCBF0) != 7:
            raise AssertionError(f"FUN_00487d30 left {machine.r32(0x7BCBF0):#x}")
    for arg in (0, 5, MASK):
        value, words = popped(machine, 0x551FF0, arg)
        if value != 1 or words != 1:
            raise AssertionError(f"FUN_00551ff0: eax {value:#x}, popped {words} words")


def check_forward(machine, rng):
    """FUN_004f68e0 passes stack args 3..9 (not 1, 2) to renderer vtable +0x7c, in order, and pops 0x24."""
    table, seen = machine.alloc(0x100), []
    machine.w32(0x7C8CC0, table)
    target = machine.alloc(0x10)
    machine.w32(table + 0x7C, target)

    def sink(current):
        """Record the seven forwarded args."""
        seen.append(tuple(current.arg(index) for index in range(7)))
        return 0

    machine.hook(target, sink, 7)
    for _ in range(50):
        args = tuple(rng.getrandbits(32) for _ in range(9))
        seen.clear()
        machine.call(0x4F68E0, *args, limit=200)
        expect = args[2:] if mutation != "forward-wrong-arg" else args[1:8]
        if seen != [expect]:
            raise AssertionError(f"FUN_004f68e0 forwarded {seen}, model {[expect]}")


def check_stream(machine, rng):
    """FUN_0064a6f0: 0 unless the unit is non-null, type 3 and has data (+0x14); else data +0x30."""
    for kind in (0, 1, 2, 3, 4, 5, 0x80000003, MASK):
        for has_data in (False, True):
            unit, data = machine.alloc(0x40), machine.alloc(0x40)
            word = rng.getrandbits(32) | 1
            machine.w32(unit, kind)
            machine.w32(unit + 0x14, data if has_data else 0)
            machine.w32(data + 0x30, word)
            machine.w32(data + 0x34, word ^ 0x55)
            expect = word if has_data and (kind == 3 or mutation == "stream-any-type") else 0
            if mutation == "stream-wrong-offset" and has_data and kind == 3:
                expect = word ^ 0x55
            got = machine.call(0x64A6F0, ecx=unit, limit=100)
            if got != expect:
                raise AssertionError(f"FUN_0064a6f0 type {kind} data {has_data}: {got:#x}, model {expect:#x}")
    if machine.call(0x64A6F0, ecx=0, limit=100) != 0:
        raise AssertionError("FUN_0064a6f0 of a null unit isn't 0")


def check_request(machine, rng):
    """FUN_004cdb40: null ECX returns 0; else a zeroed 0x5c block (+0 = 1, +4 ECX, +0x10 EDX, +0x14 / +0x18 / +0x2c / +0x30 the four stack words) goes to FUN_004cd540."""
    seen = []

    def sink(current):
        """Capture the block FUN_004cd540 receives in ECX."""
        block = current.mu.reg_read(UC_X86_REG_ECX)
        seen.append(current.read(block, 0x5C))
        return 0xABCD

    machine.hook(0x4CD540, sink, 0)
    for null in (True, False, False, False, False):
        seen.clear()
        ecx = 0 if null else rng.getrandbits(32) | 0x10000
        edx, a3, a4, a5, a6 = (rng.getrandbits(32) for _ in range(5))
        got = machine.call(0x4CDB40, a3, a4, a5, a6, ecx=ecx, edx=edx, limit=500)
        if null and mutation != "request-no-null-check":
            if got != 0 or seen:
                raise AssertionError("FUN_004cdb40 of a null unit called out")
            continue
        block = {0: 1, 4: ecx, 0x10: edx, 0x14: a3, 0x18: a4 if mutation != "request-wrong-slot" else a5, 0x2C: a5, 0x30: a6}
        blob = bytearray(0x5C)
        for offset, value in block.items():
            blob[offset:offset + 4] = struct.pack("<I", value)
        if seen != [bytes(blob)] or got != 0xABCD:
            raise AssertionError(f"FUN_004cdb40 block differs (ret {got:#x}):\n game {seen[0].hex() if seen else None}\n model {bytes(blob).hex()}")


def check_corpse(machine, rng):
    """FUN_005b2490: unit = FUN_00620bb0(EDX); 0 from FUN_0061a1f0 ends it; else FUN_005b23c0(ECX, EDX, FUN_0063ec70(unit, a1), a2, a3, a4)."""
    log = []
    machine.hook(0x620BB0, lambda c: (log.append(("find", c.arg(0))), 0x5000 + (c.arg(0) & 0xFF))[1], 1)
    flag = {"value": 1}
    machine.hook(0x61A1F0, lambda c: (log.append(("check", c.arg(0))), flag["value"])[1], 1)
    machine.hook(0x63EC70, lambda c: (log.append(("state", c.arg(0), c.arg(1))), 0x9000 + c.arg(1))[1], 2)

    def final(current):
        """Record FUN_005b23c0's registers and four stack words."""
        log.append(("apply", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                    tuple(current.arg(index) for index in range(4))))
        return 0

    machine.hook(0x5B23C0, final, 4)
    for value in (0, 1, 1, 1, 0, 1):
        flag["value"] = value
        log.clear()
        ecx, edx = rng.getrandbits(32), rng.getrandbits(32)
        a1, a2, a3, a4 = (rng.getrandbits(32) for _ in range(4))
        machine.call(0x5B2490, a1, a2, a3, a4, ecx=ecx, edx=edx, limit=500)
        unit = 0x5000 + (edx & 0xFF)
        model = [("find", edx), ("check", unit)]
        if value or mutation == "corpse-skips-check":
            model += [("state", unit, a1), ("apply", ecx, edx, (0x9000 + a1 & MASK, a2, a3, a4))]
        if mutation == "corpse-wrong-order" and value:
            model[-1] = ("apply", ecx, edx, (0x9000 + a1 & MASK, a3, a2, a4))
        if log != model:
            raise AssertionError(f"FUN_005b2490 flag {value}: {log}, model {model}")


def run(exe):
    """Run every check once with the active mutation; returns a one-line summary."""
    machine = emu.Emu(str(exe))
    rng = random.Random(0x6492A0)
    count = check_dist(machine, rng)
    check_constants(machine)
    check_forward(machine, rng)
    check_stream(machine, rng)
    check_request(machine, rng)
    check_corpse(machine, rng)
    return f"ok: {count} distance cases, constants, forwarder, stream word, request block, corpse state"


def main():
    """Entry point: run the checks, or every mutation and require each to fail."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()
