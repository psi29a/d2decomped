# SPDX-License-Identifier: GPL-3.0-or-later
"""Run bounded 1.14d item lifecycle claims on synthetic inputs.

Usage: D2_MPQ_DIR=/path/to/game-data python item_lifecycle.py
Requires the existing emulator dependencies and bin/game.exe; no MPQs or
game process are opened. Stat/table/event/list callees are hooks: this checks
the original control flow and accessor writes, not live scheduling or a d2d
comparison. See docs/research/re/items-lifecycle.md for coverage and limits.
"""

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP


def invoke(machine, address, **kwargs):
    """Fail if the instruction budget ends before the original routine returns."""
    result = machine.call(address, limit=10000, **kwargs)
    if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
        raise AssertionError(f"{address:08x}: did not return within instruction budget")
    return result


def equal(actual, expected, label):
    """Raise on a mismatching observable, even when Python assertions are disabled."""
    if actual != expected:
        raise AssertionError(f"{label}: got {actual!r}, expected {expected!r}")


def scheduling():
    """Check FUN_00558530/00558580 signed delays, event gating and register arguments."""
    machine = emu.Emu()
    game, item = machine.alloc(0x100), machine.alloc(0x100)
    state, calls = {}, []

    def stat(current):
        """Record the stat query and supply the synthetic replenishment rate."""
        calls.append(("stat", current.arg(0), current.arg(1), current.arg(2)))
        return state["rate"]

    def event(current):
        """Supply event presence while preserving its real fastcall argument checks."""
        calls.append(("find", current.mu.reg_read(UC_X86_REG_ECX),
                      current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return state["present"]

    def queue(current):
        """Record the complete event-creation call instead of queueing a real event."""
        calls.append(("queue", current.mu.reg_read(UC_X86_REG_ECX),
                      current.mu.reg_read(UC_X86_REG_EDX),
                      *(current.arg(index) for index in range(4))))
        return 0

    machine.hook(0x625480, stat, 3)
    machine.hook(0x5415a0, event, 1)
    machine.hook(0x5417d0, queue, 4)
    count = 0
    for address, stat_id in ((0x558530, 0xfc), (0x558580, 0xfd)):
        for frame in (100, 0xfffffff0):
            machine.view("game", game).frame = frame
            for rate in (0, 1, 3, 2500, 2501, -3):
                for present in (0, 1):
                    state.update(rate=rate, present=present)
                    calls.clear()
                    invoke(machine, address, ecx=game, edx=item)
                    expected = [("stat", item, stat_id, 0)]
                    if rate:
                        expected.append(("find", game, item, 3))
                        if not present:
                            quotient = (2500 // abs(rate)) * (1 if rate > 0 else -1)
                            deadline = (frame + quotient + 1) & 0xffffffff
                            expected.append(("queue", game, item, 3, deadline, 0, 0))
                    equal(calls, expected, f"schedule {address:08x}/{frame}/{rate}/{present}")
                    count += 1
    return count


def expiry():
    """Check FUN_00558a10's quality/type precedence and strict gold threshold."""
    machine = emu.Emu()
    game, item = machine.alloc(0x100), machine.alloc(0x100)
    state = {}
    for address, key in ((0x628cd0, "quest"), (0x627e70, "quality"),
                         (0x62b400, "type")):
        machine.hook(address, lambda current, key=key: state[key], 1)
    machine.hook(0x625480, lambda current: state["gold"], 3)

    def type_match(current):
        """Check FUN_0062beb0's real forwarding of item and type 0x35."""
        equal((current.arg(0), current.arg(1)), (item, 0x35), "type wrapper arguments")
        return state["type53"]

    machine.hook(0x629bb0, type_match, 2)
    count = 0
    for frame in (100, 0xfffffff0):
        machine.view("game", game).frame = frame
        for quest in (0, 1):
            for quality in (0, 2, 4, 5, 9, 10):
                for item_type in (0, 4):
                    for gold in (9999, 10000, 10001):
                        for type53 in (0, 1):
                            state.update(quest=quest, quality=quality, type=item_type,
                                         gold=gold, type53=type53)
                            if quest:
                                expected = 0
                            else:
                                delay = 15000
                                if quality == 4:
                                    delay = 30000
                                elif 5 <= quality <= 9:
                                    delay = 45000
                                elif item_type == 4:
                                    delay = 45000 if gold > 10000 else 15000
                                elif type53:
                                    delay = 30000
                                expected = (frame + delay) & 0xffffffff
                            equal(invoke(machine, 0x558a10, ecx=game, edx=item), expected,
                                  f"expiry {frame}/{state}")
                            count += 1
    return count


def runeword_cleanup():
    """Check FUN_00558c50's flag/prefix writes and stat-list unlink/free order."""
    machine = emu.Emu()
    item, data, stat_list = machine.alloc(0x100), machine.alloc(0x80), machine.alloc(0x40)
    unit = machine.view("unit", item)
    unit.data = data
    calls, state = [], {}

    def find(current):
        """Record the state-list lookup; its contents remain outside this claim."""
        calls.append(("find", *(current.arg(index) for index in range(3))))
        return state["list"]

    def unlink(current):
        """Record detachment without executing the real stat-list machinery."""
        calls.append(("unlink", current.arg(0), current.arg(1)))
        return 0

    def free(current):
        """Record release without invoking the allocator."""
        calls.append(("free", current.arg(0)))
        return 0

    machine.hook(0x625790, find, 3)
    machine.hook(0x6277e0, unlink, 2)
    machine.hook(0x626cd0, free, 1)
    count = 0
    for pointer, kind, flags in ((0, 4, 0x4000001), (item, 3, 0x4000001),
                                  (item, 4, 1), (item, 4, 0x4000001)):
        for linked in (0, stat_list):
            unit.type = kind
            machine.w32(data + 0x18, flags)
            machine.w32(data + 0x38, 0x12345678)
            state["list"] = linked
            calls.clear()
            invoke(machine, 0x558c50, edx=pointer)
            active = pointer == item and kind == 4 and bool(flags & 0x4000000)
            equal(machine.r32(data + 0x18), flags & ~0x4000000 if active else flags, "flags")
            equal(machine.r32(data + 0x38), 0x12340000 if active else 0x12345678, "prefix")
            expected = [("find", item, 0xab, 0x40)] if active else []
            if active and linked:
                expected.extend((("unlink", item, stat_list), ("free", stat_list)))
            equal(calls, expected, "runeword cleanup order")
            count += 1
    return count


def main():
    """Run only the stated bounded original-code observations and report counts."""
    for name, check in (("replenishment scheduling", scheduling),
                        ("expiry deadlines", expiry), ("runeword cleanup", runeword_cleanup)):
        print(f"PASS {name}: {check()} cases")


if __name__ == "__main__":
    main()
