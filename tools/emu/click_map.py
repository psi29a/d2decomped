# SPDX-License-Identifier: GPL-3.0-or-later
"""A map click's dispatch and its flags, as the oracle: FUN_00462d00 (ClickMap).

    uv run python click_map.py 1 1 > g.txt
    sort -u g.txt   # every (clickType, run key, stamina, selection) combination

docs/research/re/collision.md cites FUN_00462d00 twice: "A walk to a spot
just ends" ("That is FUN_00462d00 with flags 9", from FUN_0044c000 /
queued_click.py) and "The mouse-button state words" (the state-word table).
The decompile shows six cases (clickType 0 left-down .. 5 right-up) and a
default that does nothing; each ORs its own flag bits into a
`D2UnderMouseStrc` (structs: D2UnderMouseStrc) local before calling one of
six handlers, all taking that struct by pointer in a custom register (not
d2d's claim: hooked here to record what they got, instead of running).

Base flags per clickType, confirmed below: 0 -> 5 (INPUT_HandleLeftClickRepeat,
0x462d00's own LAB_00462de1 case), 1 -> 9 (WalkToCoordOrUnit, the doc's
"flags 9"), 2 -> 0x11 (INPUT_HandleLeftClickWithPet), 3 -> 6
(ClickMapRightSkill), 4 -> 0xa (INPUT_HandleRightClickRepeat), 5 -> 0x12
(INPUT_HandleRightClickWithPet); 6+ calls nothing and returns 1. On top of
that: bit 0x20 when mouseEventFlags & 4 (the "run" bit position the doc's
state-word table reads at 0x44bec0/0x4621d0), and 0x40 when mouseEventFlags
& 8 *and* the player's current stamina (STATSEX_GetCurrentStat, hooked to a
fixed value here) is non-zero. Types 0 and 3 pick ClearSelectedUnit or
SELECT_UpdateUnitSelection(target) first, by whether GetSelectedUnit (hooked)
returns a unit; both are recorded here to show which ran.

Gated on GetUIFlag(UI_ESCMENU) (hooked, 0) and the re-entrancy flag
`bRecursionClick` (0x7a5264), reset before each case: real game.exe code,
same struct layout, same flag arithmetic, every case diffed against what
the decompile says it must be.
"""
import sys

import emu

RECURSION_CLICK = 0x7A5264
HANDLERS = {
    "WalkToCoordOrUnit": (0x462A20, "EDI"),
    "INPUT_HandleLeftClickWithPet": (0x462B40, "EAX"),
    "ClickMapRightSkill": (0x462BA0, "ESI"),
    "INPUT_HandleRightClickRepeat": (0x462C20, "ESI"),
    "INPUT_HandleRightClickWithPet": (0x462CA0, "EBX"),
    "INPUT_HandleLeftClickRepeat": (0x4629A0, "ESI"),
}
EXPECTED_HANDLER = {0: "INPUT_HandleLeftClickRepeat", 1: "WalkToCoordOrUnit", 2: "INPUT_HandleLeftClickWithPet",
                     3: "ClickMapRightSkill", 4: "INPUT_HandleRightClickRepeat", 5: "INPUT_HandleRightClickWithPet"}
BASE_FLAGS = {0: 5, 1: 9, 2: 0x11, 3: 6, 4: 0xA, 5: 0x12}


def setup(e, stamina, target):
    seen = []

    def handler_hook(name, reg):
        def fn(ev):
            mouse = ev.mu.reg_read(getattr(emu, "UC_X86_REG_" + reg))
            seen.append((name, ev.r32(mouse + 0), ev.r32(mouse + 8)))   # flags, pTarget
            return 1
        return fn
    for name, (addr, reg) in HANDLERS.items():
        e.hook(addr, handler_hook(name, reg), 0)
    e.hook(0x4538D0, lambda ev: 0, 0)                 # GetUIFlag(UI_ESCMENU): closed
    e.hook(0x463DD0, lambda ev: 0x1000, 0)             # GetPlayerUnit_UI: a fixed non-null player
    e.hook(0x467A10, lambda ev: target, 0)             # GetSelectedUnit
    e.hook(0x45AFF0, lambda ev: 0, 0)                  # ConvertCoordToPerspectiveView: identity (no-op)
    e.hook(0x625480, lambda ev: stamina, 3)            # STATSEX_GetCurrentStat: pUnit, eUnitStat, nLayer on the stack
    e.hook(0x466FE0, lambda ev: (seen.append(("ClearSelectedUnit",)), 0)[1], 0)
    e.hook(0x467A70, lambda ev: (seen.append(("SELECT_UpdateUnitSelection", ev.mu.reg_read(emu.UC_X86_REG_ECX))), 0)[1], 0)
    return seen


def run_case(e, click_type, mouse_event_flags, stamina, target):
    seen = setup(e, stamina, target)
    e.w32(RECURSION_CLICK, 0)
    e.call(0x462D00, 0, mouse_event_flags, ecx=click_type, edx=0)
    calls = [s for s in seen if s[0] in HANDLERS]
    expect_handler = EXPECTED_HANDLER.get(click_type)
    if expect_handler is None:
        ok = not calls
        print(click_type, mouse_event_flags, stamina, bool(target), "nothing" if ok else f"DISAGREE {seen}")
        return
    expect_flags = BASE_FLAGS[click_type]
    if mouse_event_flags & 4:
        expect_flags |= 0x20
    if (mouse_event_flags & 8) and stamina != 0:
        expect_flags |= 0x40
    expect_select = ("SELECT_UpdateUnitSelection", target) if (click_type in (0, 3) and target) else \
                     ("ClearSelectedUnit",) if click_type in (0, 2, 3, 5) else None
    ok = len(calls) == 1 and calls[0][0] == expect_handler and calls[0][1] == expect_flags
    select_calls = [s for s in seen if s[0] in ("ClearSelectedUnit", "SELECT_UpdateUnitSelection")]
    select_ok = (expect_select is None and not select_calls) or (select_calls[:1] == [expect_select] if expect_select else True)
    print(click_type, mouse_event_flags, stamina, bool(target), "OK" if ok and select_ok else f"DISAGREE calls={calls} select={select_calls} expect={expect_handler},{expect_flags:#x},{expect_select}")


def main(count, seed):
    e = emu.Emu()
    fake_target = e.alloc(32)
    e.w32(fake_target + 0, 1)   # D2UnitStrc.eUnitType = UNIT_MONSTER: not OBJECT/ITEM, skips the GetXPosFromDynamicPathUnit branch
    for click_type in range(7):
        for mouse_event_flags in (0, 4, 8, 12):
            for stamina in (0, 1):
                for target in (0, fake_target):
                    run_case(e, click_type, mouse_event_flags, stamina, target)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
