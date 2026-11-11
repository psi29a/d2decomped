# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Cain's scroll-loss rewind against stock game.exe 1.14d.

Run: python quest_cain_rewind.py --exe /path/to/game.exe
Synthetic records exercise the trigger and transition. Item lookup, quest
notifications, state changes, and player iteration are hooked; their own
implementations and end-to-end game behavior are outside this check.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    record, data, event, unit = (machine.alloc(size) for size in (0x100, 0x200, 0x20, 0x100))
    game = machine.alloc(0x20)
    calls = []
    lookup = {"unit": 0, "code": 0}

    def capture(name, registers, stack, result=0):
        def hook(current):
            values = tuple(current.mu.reg_read(reg) for reg in registers)
            calls.append((name, *values, *(current.arg(i) for i in range(stack))))
            return lookup[result] if isinstance(result, str) else result
        return hook

    machine.hook(0x552f60, capture("find", (UC_X86_REG_ECX, UC_X86_REG_EDX), 1, "unit"), 1)
    machine.hook(0x624690, capture("mode", (), 2), 2)
    machine.hook(0x544300, capture("notify", (UC_X86_REG_ECX, UC_X86_REG_EDX), 3), 3)
    machine.hook(0x544350, capture("state", (UC_X86_REG_ECX, UC_X86_REG_EDX), 2), 2)
    machine.hook(0x5537d0, capture("players", (UC_X86_REG_ECX, UC_X86_REG_EDX), 2), 2)
    machine.hook(0x628590, capture("code", (), 1, "code"), 1)

    def reset(known, stone, count, state=4, active=1):
        for address, size in ((record, 0x100), (data, 0x200), (event, 0x20), (unit, 0x100)):
            machine.mu.mem_write(address, bytes(size))
        machine.w32(record + 4, game)
        machine.w32(record + 0x18, data)
        machine.mu.mem_write(record + 9, bytes([active]))
        machine.mu.mem_write(record + 0xc, bytes([state]))
        machine.mu.mem_write(record + 0x14, b'\x77')
        machine.w32(data + 0x30, 0x1234)
        machine.mu.mem_write(data + 0x47, bytes([known]))
        machine.mu.mem_write(data + 0x4f, bytes([stone]))
        machine.w32(data + 0x58, 7)
        machine.w32(data + 0x7c, count)
        machine.w32(event + 8, 0x7654)
        machine.w32(unit + 4, 30)
        calls.clear()

    def run(address, **kwargs):
        machine.call(address, limit=1000, **kwargs)
        if (machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
            raise AssertionError("execution did not return with the expected stack")

    # Transition: known tree is looked up; only class 30 is put in mode 0.
    for known, found, class_id in ((0, False, 30), (1, False, 30), (1, True, 31), (1, True, 30), (2, True, 30)):
        reset(known, 0, 0)
        lookup["unit"] = unit if found else 0
        machine.w32(unit + 4, class_id)
        run(0x592bd0, regs={"esi": record})
        expected = []
        if known == 1:
            expected.append(("find", game, 2, 0x1234))
            if found and class_id == 30:
                expected.append(("mode", unit, 0))
        expected += [("notify", record, 1, 0, 0x5920d0, 1),
                     ("state", record, 3, 0x6e1c48, 0x397 if known == 1 else 0x39f)]
        if known == 1:
            expected.append(("players", game, 0, 0, 0x592130))
        if calls != expected or machine.read(record + 0x14, 1) != b'\0' or machine.r32(data + 0x58) != 0:
            raise AssertionError(f"transition mismatch for {(known, found, class_id)}: {calls!r}")

    # Trigger: bkd/bks decrement, including wraparound; non-scrolls can still trigger at zero.
    for code, count, state, active, stone, known in (
        (0x20646b62, 1, 4, 1, 0, 1), (0x20736b62, 1, 4, 1, 0, 1),
        (0, 0, 4, 1, 0, 1), (0, 1, 4, 1, 0, 1),
        (0x20646b62, 0, 4, 1, 0, 1), (0x20646b62, 1, 6, 1, 0, 1),
        (0x20646b62, 1, 7, 1, 0, 1), (0x20646b62, 1, 4, 0, 0, 1),
        (0x20646b62, 1, 4, 1, 1, 1), (0x20646b62, 1, 4, 1, 0, 0)):
        reset(known, stone, count, state, active)
        lookup.update(code=code, unit=0)
        run(0x592c80, ecx=record, edx=event)
        enabled = active != 0 and state != 6
        new_count = (count - int(code in (0x20646b62, 0x20736b62))) & 0xffffffff if enabled else count
        triggered = enabled and new_count == 0 and state < 6 and stone != 1 and known == 1
        expected_names = (["code"] if enabled else []) + (["state", "find", "notify", "state", "players"] if triggered else [])
        if machine.r32(data + 0x7c) != new_count or [entry[0] for entry in calls] != expected_names:
            raise AssertionError(f"trigger mismatch: {(code, count, state, active, stone, known)} {calls!r}")
        if triggered and (calls[1][2] != 3 or calls[1][-1] != 0x3f7 or machine.r32(data + 0x58) != 0):
            raise AssertionError(f"trigger transition mismatch: {calls!r}")
    print("ok: 5 Cain transitions and 10 scroll-loss trigger cases (callees hooked)")


if __name__ == "__main__":
    main()
