# SPDX-License-Identifier: GPL-3.0-or-later
"""Check 1.14d's real timed-event insertion, dispatch and cancellation."""
import argparse
import struct
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

from emu import RET_MAGIC, Emu


def check(exe, player_action, interrupt_first):
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
    oracle.hook(0x581220, lambda machine: calls.append("player action") or 0, 3)

    # Replace the AI decision with a mode change's real action/end clear.
    # The trampoline calls FUN_00553990 with the dispatcher registers intact.
    callback = oracle.alloc(8)
    oracle.mu.mem_write(callback, b"\xe8" + struct.pack("<i", 0x553990 - (callback + 5)) + b"\xc2\x08\x00")
    oracle.w32(0x6e2498, callback)  # monster event table's think callback
    oracle.mu.hook_add(UC_HOOK_CODE, lambda *_: calls.append("think"), begin=callback, end=callback)

    action = (council, 0, 2)
    interrupt = (player, 0, 1) if player_action else (council, 2, 0)
    ordered = (interrupt, action) if interrupt_first else (action, interrupt)
    for unit, kind, marker in ordered:
        run(0x5417d0, kind, 4, marker, 0, ecx=game, edx=unit)
    oracle.w32(game + 0xa8, 4)
    run(0x5414d0, ecx=game)
    expected = ["player action", "Council action"] if player_action else (
        ["think"] if interrupt_first else ["Council action", "think"]
    )
    assert calls == expected, (player_action, interrupt_first, calls)
    print(f"Player event: {player_action}; interrupt queued first: {interrupt_first}; dispatched: {calls}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    for competing_player in (False, True):
        for enqueued_first in (False, True):
            check(args.exe, competing_player, enqueued_first)
