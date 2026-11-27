# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a2q2's pickup handler FUN_00599a30 (record 9's event 4) in game.exe 1.14d.

    .venv/bin/python staff_pickup_news.py [--exe /path/to/game.exe] [--mutation-check]

FUN_00599a30 (ECX the record, EDX the event block: +0 game, +8 item, +0xc player) runs for real, with
FUN_00599510 and FUN_00599570; hooked: the item's row (FUN_006335f0, its code at +0x80), the player's
quest bits (FUN_006221a0, FUN_0065c310), the act lookups (FUN_00620bb0, FUN_0061a1b0, FUN_006427f0) and
the log send FUN_00544190 (logged: ECX game, EDX player, the record id). Over the codes tr1, vip, box,
msf, hst and one unbound, with quest 10's bits 0, 3, 13, 14 and 15 in every combination: the send goes
to the picker for record 9 when the code is vip, box, msf, or tr1 with bit 3 clear, and
FUN_00599510 passes (bits 0 and 15 clear, or 13 or 14: rules::quest_log_goes_to); the record's +0xb
is 1 for an unread scroll, else 2 with bit 3 or 6. d2d: World::item_event's ev::QuestNews{10},
tests/game/test_staff_pickup_news.cpp. Without --exe, $D2_MPQ_DIR/bin/game.exe.
`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import itertools
import os
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CODES = ("tr1 ", "vip ", "box ", "msf ", "hst ", "qey ")
BITS = (0, 3, 13, 14, 15)
MUTATIONS = ("read-scroll-sends", "staff-sends", "ignores-closed", "wrong-state")
mutation = None


def goes_to(bits):
    """rules::quest_log_goes_to for quest 10 (FUN_00599510)."""
    if mutation == "ignores-closed":
        return True
    return (not bits[0] and not bits[15]) or bits[13] or bits[14]


def model(code, bits):
    """FUN_00599a30's (send, record +0xb) for one pickup; +0xb None when untouched."""
    code = code.strip()
    if code == "tr1" and not bits[3]:
        state = 1
    elif code == "tr1" and mutation == "read-scroll-sends":
        state = None
        return goes_to(bits), state
    elif code in ("vip", "box", "msf"):
        state = 2 if bits[3] else 6
        if mutation == "wrong-state":
            state = 6 if bits[3] else 2
    elif code == "hst" and mutation == "staff-sends":
        return goes_to(bits), None
    else:
        return False, None
    return goes_to(bits), state


def run(exe):
    """FUN_00599a30 over codes and bit patterns; returns the case count."""
    machine = emu.Emu(exe)
    game, record, block = machine.alloc(0x200), machine.alloc(0x100), machine.alloc(0x40)
    item, player, pdata, row = machine.alloc(0x40), machine.alloc(0x100), machine.alloc(0x40), machine.alloc(0x100)
    bits_ptr = 0x5EED
    machine.mu.mem_write(game + 0x6D, b"\x00")
    machine.w32(pdata + 0x10, bits_ptr)
    machine.w32(block, game)
    machine.w32(block + 8, item)
    machine.w32(block + 0xC, player)
    machine.w32(item + 4, 77)
    machine.mu.mem_write(record + 8, b"\x01")       # the record's act: act 2
    machine.w32(record + 4, game)                   # the record's game (FUN_00599510's difficulty, +0x6d)
    state = {"bits": {}}
    sent = []

    def bit(current):
        """Hook FUN_0065c310(bits, quest, bit): quest 10's from the case, others 0."""
        if current.arg(0) != bits_ptr:
            raise AssertionError("FUN_0065c310 not on the player's bits")
        return int(current.arg(1) == 10 and state["bits"].get(current.arg(2), False))

    machine.hook(0x6335F0, lambda current: row if current.arg(0) == 77 else 0, 1)
    machine.hook(0x6221A0, lambda current: pdata if current.arg(0) == player else 0, 1)
    machine.hook(0x65C310, bit, 3)
    machine.hook(0x620BB0, lambda _e: 0x1000, 1)
    machine.hook(0x61A1B0, lambda _e: 0x2000, 1)
    machine.hook(0x6427F0, lambda _e: 1, 1)
    machine.hook(0x544190, lambda current: sent.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0))) or 0, 1)
    cases = 0
    for code in CODES:
        for values in itertools.product((False, True), repeat=len(BITS)):
            bits = dict(zip(BITS, values))
            state["bits"] = bits
            sent.clear()
            machine.mu.mem_write(row + 0x80, code.encode())
            machine.mu.mem_write(record + 0xB, b"\xee")
            machine.call(0x599A30, ecx=record, edx=block, limit=20000)
            seen = (bool(sent), None if machine.read(record + 0xB, 1)[0] == 0xEE else machine.read(record + 0xB, 1)[0])
            want = model(code, bits)
            if seen != want or (sent and sent != [(game, player, 9)]):
                raise AssertionError(f"FUN_00599a30 {code!r} bits {bits}: game.exe {seen} {sent}, model {want}")
            cases += 1
    return cases


def main():
    """Parse the arguments, verify game.exe, run and optionally run the mutations."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=Path(os.environ.get("D2_MPQ_DIR", ".")) / "bin" / "game.exe")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(f"ok: {run(str(args.exe))} a2q2 pickup cases (FUN_00599a30)")
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(str(args.exe))
            except AssertionError:
                print(f"caught: {name}")
            else:
                print(f"NOT CAUGHT: {name}")
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
