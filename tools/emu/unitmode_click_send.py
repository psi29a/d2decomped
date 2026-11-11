# SPDX-License-Identifier: GPL-3.0-or-later
"""The click-command send, as the oracle: FUN_00480b40
(ClickWithSkillOnUnitOrCoord).

    uv run python unitmode_click_send.py

docs/research/re/collision.md ("FUN_00480b40(type, id, third)") and
net-packets.md ("the action as the opcode") both cite FUN_00480b40 as the
one place an action's send happens, without saying which of its two wire
shapes a given action picks. The decompile is one dispatch on eModeAction
(ECX): UNITMODEACTION_ITEM_INTERACT (19) sends nothing at all; attack (2,
4), a left skill on a unit (6, 7, 9, 10) and a right skill on a unit (13,
14, 16, 17) go out as FUN_004786a0 (NET_D2GS_CLIENT_Send_INT_INT, a 5-byte
packet: id, int, int); every other action (interact 1/3, a left/right
skill at a coordinate 5, 8, 11, 12, 15, 18) goes out as FUN_004785d0
(NET_D2GS_CLIENT_Send_SHORT_SHORT, a 5-byte packet: id, short, short).
This oracle hooks both sends to record which one ran, with what args, over
every eD2UnitModeAction value (1..20, past the last defined one), and
diffs against that split: no disagreement.
"""
import emu

SEND_INT_INT = 0x4786A0
SEND_SHORT_SHORT = 0x4785D0
CLICK_SEND = 0x480B40

INT_INT_ACTIONS = {2, 4, 6, 7, 9, 10, 13, 14, 16, 17}
ITEM_INTERACT = 19


def run_case(e, action, x, y):
    # Both sends are fastcall(nPacketId CL, nArg1 EDX, nArg2 on the stack): one stack dword.
    seen = []
    e.hook(SEND_INT_INT, lambda ev: (seen.append(("INT_INT", ev.mu.reg_read(emu.UC_X86_REG_CL),
                                                   ev.mu.reg_read(emu.UC_X86_REG_EDX), ev.arg(0))), 0)[1], 1)
    e.hook(SEND_SHORT_SHORT, lambda ev: (seen.append(("SHORT_SHORT", ev.mu.reg_read(emu.UC_X86_REG_CL),
                                                       ev.mu.reg_read(emu.UC_X86_REG_EDX) & 0xFFFF,
                                                       ev.arg(0) & 0xFFFF)), 0)[1], 1)
    e.call(CLICK_SEND, x, y, 0, ecx=action, edx=0)

    if action == ITEM_INTERACT:
        expect = None
    elif action in INT_INT_ACTIONS:
        expect = ("INT_INT", action, x & 0xFFFFFFFF, y & 0xFFFFFFFF)   # full int32 args, no truncation
    else:
        expect = ("SHORT_SHORT", action, x & 0xFFFF, y & 0xFFFF)       # truncated to int16 by the struct
    got = seen[0] if seen else None
    ok = (got == expect) and len(seen) <= 1
    print(action, x, y, "OK" if ok else f"DISAGREE got={got} expect={expect}")


def main():
    e = emu.Emu()
    for action in range(1, 21):
        for x, y in ((0, 0), (5, 1000), (0x7FFF, -1)):
            run_case(e, action, x, y)


if __name__ == "__main__":
    main()
