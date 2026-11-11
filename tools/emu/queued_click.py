# SPDX-License-Identifier: GPL-3.0-or-later
"""A held left button's queued click, as the oracle: FUN_0044c000 (ProcessQueuedPrimaryClick).

    uv run python queued_click.py 1 1 > g.txt
    sort -u g.txt   # every gate combination: ClickMap(1, x, y, flags) sent, or nothing

docs/research/re/collision.md ("A walk to a spot just ends", "The mouse-button
state words"): "A held button: FUN_0044efa0 calls FUN_0044c000 each frame
(LAB_0044f039) while the press was taken and 0x7a061c is set. That is
FUN_00462d00 with flags 9 (the press has 5)". The decompile confirms the
chain and the three globals it gates on, named in game.exe's own symbols
(not the doc's raw addresses, but the same cells): `ServerLoadingComplete`
(0x7a061c), `gQueuedPrimaryClick` (0x7a0650, "the press was taken" in the
doc's table) and `gbMouseButtonEventPending` (0x7a0658). All three true,
and a player unit present, it calls ClickMap(1, GetMouseX(), GetMouseY(),
GetMouseClickFlags()) — the doc's "flags 9" is ClickMap's own clickType==1
case (0x462d00), set inside ClickMap, not here; this oracle only confirms
FUN_0044c000 reaches it with clickType 1 exactly when the three gates and
the player all hold, and nothing otherwise (all 16 combinations below).

GetPlayerUnit_UI, GetMouseClickFlags, GetMouseX and GetMouseY are hooked to
fixed values (not the claim: a real player/cursor would need a drlg boot);
ClickMap is hooked to record what it is called with, not run (it is the
subject of "The mouse-button state words" oracle, not this one).
"""
import sys

import emu

SERVER_LOADING_COMPLETE = 0x7A061C
QUEUED_PRIMARY_CLICK = 0x7A0650
MOUSE_BUTTON_EVENT_PENDING = 0x7A0658
MOUSE_X, MOUSE_Y, CLICK_FLAGS = 42, 99, 0x4


def run_case(e, player, loading_complete, queued, pending):
    calls = []

    def record_click(ev):
        clicktype = ev.mu.reg_read(emu.UC_X86_REG_ECX)
        x = ev.mu.reg_read(emu.UC_X86_REG_EDX)
        calls.append((clicktype, x, ev.arg(0), ev.arg(1) & 0xFFFF))
        return 0
    e.hook(0x462D00, record_click, 2)   # ECX, EDX, 2 stack args (nY, mouseEventFlags): RET 8
    e.hook(0x463DD0, lambda ev: player, 0)
    e.hook(0x44BEC0, lambda ev: CLICK_FLAGS, 0)
    e.hook(0x468730, lambda ev: MOUSE_X, 0)
    e.hook(0x468740, lambda ev: MOUSE_Y, 0)
    e.w32(SERVER_LOADING_COMPLETE, loading_complete)
    e.w32(QUEUED_PRIMARY_CLICK, queued)
    e.w32(MOUSE_BUTTON_EVENT_PENDING, pending)
    e.call(0x44C000)
    gate = loading_complete != 0 and queued != 0 and pending == 0 and player != 0
    if gate:
        ok = calls == [(1, MOUSE_X, MOUSE_Y, CLICK_FLAGS)]
    else:
        ok = calls == []
    print(bool(loading_complete), bool(queued), bool(pending), bool(player), "OK" if ok else f"DISAGREE {calls}")


def main(count, seed):
    e = emu.Emu()
    player = e.alloc(16)
    e.w32(player + 0, 0)   # D2UnitStrc.eUnitType = UNIT_PLAYER
    for loading_complete in (0, 1):
        for queued in (0, 1):
            for pending in (0, 1):
                for has_player in (0, 1):
                    run_case(e, player if has_player else 0, loading_complete, queued, pending)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
