# SPDX-License-Identifier: GPL-3.0-or-later
"""The interact send's packet id by target type, as the oracle: FUN_00480930
(INTERACT_AnyObject).

    uv run python interact_any_object.py 1 1 > g.txt
    sort -u g.txt   # every target type's final send, and the monster's 200ms throttle

docs/research/re/collision.md ("The server's pending interact"): "FUN_00481600
sends those with no range test ..., once, as FUN_00480930's 0x16 or 0x13
... The other 0x16 immediates in these functions aren't sends ...". The
decompile is a switch on the target's type (`D2UnitStrc.eUnitType`): PLAYER
and MONSTER face the target (UpdateDirectionAndSpeed) and send 0x13
(NET_D2GS_CLIENT_Send_INT_INT, 0x4786a0) — a monster only once each 200ms
real time (GetTickCount, always 0 under the default import stub, against
`pTargetUnit->dwTickCount`); ITEM sends 0x16 (NET_D2GS_CLIENT_Send_INT_INT_INT,
0x4786d0) with the inventory-open flag; OBJECT, when its interact flag
(`eUnitFlags` bit 2) is clear, falls through to the same 0x13 send as
everything else. **Not covered here**: an interactable object (the flag
set) first tries a melee/kick skill cast, with class-specific cases for
0x194 and 0x178 — real weapon/inventory state this oracle doesn't fake, so
that sub-path stays cited, not verified; whichever way it goes, the
function still ends in the same 0x13 send this oracle does check.

A target not found (UNITS_FindClientSideUnit, hooked) sends nothing at all.
"""
import sys

import emu

UNIT_SIZE = 244


def setup(e):
    sends = []
    e.hook(0x4786A0, lambda ev: (sends.append((0x13, ev.mu.reg_read(emu.UC_X86_REG_EDX), ev.arg(0))), 0)[1], 1)
    e.hook(0x4786D0, lambda ev: (sends.append((0x16, ev.mu.reg_read(emu.UC_X86_REG_EDX), ev.arg(0), ev.arg(1))), 0)[1], 2)
    e.hook(0x621C00, lambda ev: 0, 3)   # UpdateDirectionAndSpeed: no-op
    e.hook(0x45ADF0, lambda ev: 500, 0)   # GetXPosFromDynamicPathUnit
    e.hook(0x45AE20, lambda ev: 600, 0)   # GetYPosFromDynamicPathUnit
    e.hook(0x4538D0, lambda ev: 0, 0)     # GetUIFlag(UI_INVENTORY): closed
    e.hook(0x648B90, lambda ev: 0, 2)     # SetTarget: no-op
    return sends


def run_case(e, unit_type, guid, found, label, object_interact_flag=False, monster_tick=0, expect=None):
    sends = setup(e)
    player = e.alloc(UNIT_SIZE)
    e.w32(player + 44, e.alloc(16))   # D2UnitStrc.pPath (D2PathUnion): a fake dynamic path
    e.hook(0x463DD0, lambda ev: player, 0)   # GetPlayerUnit_UI
    target = e.alloc(UNIT_SIZE)
    e.w32(target + 0, unit_type)
    if unit_type == 2:     # UNIT_OBJECT: eUnitFlags at +196, bit 2
        e.w32(target + 196, 4 if object_interact_flag else 0)
    if unit_type == 1:     # UNIT_MONSTER: dwTickCount at +212
        e.w32(target + 212, monster_tick & 0xFFFFFFFF)
    e.hook(0x463990, lambda ev: (target if found else 0), 0)
    e.call(0x480930, ecx=unit_type, edx=guid)
    ok = sends == expect
    print(unit_type, label, "OK" if ok else f"DISAGREE {sends} expect={expect}")


def main(count, seed):
    e = emu.Emu()
    run_case(e, 0, 777, False, "not found", expect=[])
    run_case(e, 0, 777, True, "player", expect=[(0x13, 0, 777)])
    run_case(e, 4, 777, True, "item", expect=[(0x16, 4, 777, 0)])   # eD2UnitType: 4 = UNIT_ITEM
    run_case(e, 2, 777, True, "object not interactable", object_interact_flag=False, expect=[(0x13, 2, 777)])
    # a monster just interacted with (dwTickCount close to "now" = 0): throttled, nothing sent
    run_case(e, 1, 777, True, "monster throttled", monster_tick=0, expect=[])
    # a monster whose dwTickCount is 300 ticks in the "past" (now - dwTickCount wraps to 300 >= 200): sends
    run_case(e, 1, 777, True, "monster", monster_tick=(0 - 300) & 0xFFFFFFFF, expect=[(0x13, 1, 777)])


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
