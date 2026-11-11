# SPDX-License-Identifier: GPL-3.0-or-later
"""FUN_004b8cb0 (TRADE_CloseTrade), as the oracle: net-packets.md "The trade
close wait" cites the GetTickCount call inside it (0x4b8cdb) as one of three
places that send C->S 0x4f (2) and arm the close wait, but doesn't say the
whole function is guarded, or that it also sets a byte flag the doc never
named. Run natively except the network send (0x478600, hooked to record its
args: no socket in this harness) and GetTickCount (emu.py's own w_GetTickCount
stub, already 0 here).

    uv run python trade_close.py

Globals (disassembly, not decompiler enum names): DAT_007c0e7c (the trade
state), DAT_007c0e70 (the throttle tick), DAT_007c0e74 (a byte; decompiler
name `gbTradeResetFlag`, not documented before this).
"""
from unicorn.x86_const import UC_X86_REG_CL, UC_X86_REG_EDX

import emu

STATE, TICK, RESET_FLAG = 0x7C0E7C, 0x7C0E70, 0x7C0E74


def main():
    """Run FUN_004b8cb0 at every trade state and check the send and the guard."""
    e = emu.Emu()
    sent = []

    def send(cur):
        """NET_D2GS_CLIENT_Send_SHORT_SHORT_SHORT(CL opcode, EDX val1, val2, val3) at 0x478600: record the args."""
        sent.append((cur.mu.reg_read(UC_X86_REG_CL), cur.mu.reg_read(UC_X86_REG_EDX), cur.arg(0), cur.arg(1)))
        return 0

    e.hook(0x478600, send, 2)
    cases = 0
    for state in (0, 1, 7):
        e.w32(STATE, state)
        e.w32(TICK, 0x1234)
        # state 7 starts with the flag already set, so a close that wrongly
        # clears it (instead of leaving it alone) doesn't pass by accident.
        e.mu.mem_write(RESET_FLAG, b"\x01" if state == 7 else b"\x00")
        sent.clear()
        e.call(0x4B8CB0)
        cases += 1
        if sent != [(0x4F, 2, 0, 0)]:
            raise AssertionError(f"state {state}: must always send C->S 0x4f 2 0 0, got {sent}")
        if state == 7:
            # the guard: already waiting, so the state/tick/flag are left alone
            if (e.r32(STATE), e.r32(TICK), e.mu.mem_read(RESET_FLAG, 1)[0]) != (7, 0x1234, 1):
                raise AssertionError("state already 7: must not touch state, tick or the reset flag")
        else:
            if (e.r32(STATE), e.mu.mem_read(RESET_FLAG, 1)[0]) != (7, 1):
                raise AssertionError(f"state {state}: must set state 7 and the reset flag")
            if e.r32(TICK) != 0:
                raise AssertionError(f"state {state}: must stamp the tick with GetTickCount's stub (0), got {e.r32(TICK):#x}")
    print(f"ok: {cases} FUN_004b8cb0 close/guard cases")


if __name__ == "__main__":
    main()
