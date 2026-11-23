# SPDX-License-Identifier: GPL-3.0-or-later
"""Check 1.14d's real timed-event insertion, dispatch and cancellation."""
import argparse
import struct
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

from emu import RET_MAGIC, Emu


def check(exe, player_action, interrupt_first, mode_end=False):
    oracle = Emu(exe=str(exe))
    game, council, player = (oracle.alloc(0x200) for _ in range(3))
    oracle.w32(council, 1)  # monster unit type
    oracle.hook(0x40b430, lambda machine: machine.alloc(machine.mu.reg_read(UC_X86_REG_EDX)), 3)
    calls = []

    def run(address, *args, **registers):
        oracle.call(address, *args, limit=100000, **registers)
        assert oracle.mu.reg_read(UC_X86_REG_EIP) == RET_MAGIC

    run(0x541470, ecx=game)
    oracle.hook(0x639df0, lambda machine: 0, 2)  # no frozen/uninterruptible state
    oracle.hook(0x5a7ba0, lambda machine: calls.append("Council action") or 0, 2)
    if mode_end:
        # Supply the stock S1 callback record without loading MonStats.
        oracle.w32(council + 0x10, 8)
        oracle.hook(0x5a78a0, lambda machine: 0x6e22e0, 0)
        # The mode end enters NU. Run its real think scheduler with a
        # minimal mode-change record, bypassing the unrelated unit setup.
        neutral, monster_data, monstats = (oracle.alloc(0x80) for _ in range(3))
        oracle.w32(neutral + 4, council)
        oracle.w32(council + 0x14, monster_data)
        oracle.w32(monster_data, monstats)
        oracle.hook(0x553570, lambda machine: 0, 1)
        trampoline = b"\xba" + struct.pack("<I", neutral) + b"\xe9" + struct.pack("<i", 0x5a73e0 - (0x5a8030 + 10))
        oracle.mu.mem_write(0x5a8030, trampoline)
        oracle.mu.hook_add(UC_HOOK_CODE, lambda *_: calls.append("Council mode end"), begin=0x5a8030, end=0x5a8030)
    oracle.hook(0x581220, lambda machine: calls.append("player action") or 0, 3)

    # Replace the AI decision with a mode change's real action/end clear.
    # The trampoline calls FUN_00553990 with the dispatcher registers intact.
    callback = oracle.alloc(8)
    oracle.mu.mem_write(callback, b"\xe8" + struct.pack("<i", 0x553990 - (callback + 5)) + b"\xc2\x08\x00")
    oracle.w32(0x6e2498, callback)  # monster event table's think callback
    oracle.mu.hook_add(UC_HOOK_CODE, lambda *_: calls.append("think"), begin=callback, end=callback)

    event_name = "Council mode end" if mode_end else "Council action"
    action = (council, 1, 0) if mode_end else (council, 0, 2)
    interrupt = (player, 0, 1) if player_action else (council, 2, 0)
    ordered = (interrupt, action) if interrupt_first else (action, interrupt)
    for unit, kind, marker in ordered:
        run(0x5417d0, kind, 4, marker, 0, ecx=game, edx=unit)
    oracle.w32(game + 0xa8, 4)
    run(0x5414d0, ecx=game)
    expected = ["player action", "Council action"] if player_action else (
        ["think"] if interrupt_first else [event_name, "think"]
    )
    assert calls == expected, (player_action, interrupt_first, calls)
    print(f"{event_name}; player event: {player_action}; interrupt queued first: {interrupt_first}; dispatched: {calls}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    for competing_player in (False, True):
        for enqueued_first in (False, True):
            check(args.exe, competing_player, enqueued_first)
    for think_first in (False, True):
        check(args.exe, False, think_first, mode_end=True)
