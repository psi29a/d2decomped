# SPDX-License-Identifier: GPL-3.0-or-later
"""Clearing a player's queued interact, as the oracle: FUN_0045c470.

    uv run python interact_queue.py 500 1 > g.txt
    sort -u g.txt   # every case: cleared when a player with player data, untouched otherwise

docs/research/re/collision.md cites FUN_0045c470 (no address in the name:
`SCMD_ClearPlayerTradeFields` in the decompile, despite the name) as what
clears a player's queue before anything else is tried (LAB_00481672 /
FUN_0045c470, "The per-frame queue step") and on command 8, the player
dying (the command table in "FUN_00461250"). Read from the decompile
(fastcall, ECX the unit): a null unit, a non-player (`D2UnitStrc.eUnitType`,
structs: D2UnitStrc), or a player with no player data (`pUnitData`, the
same struct) is untouched; otherwise the four fields `D2UnitDataPlayerStrc`
(structs: D2UnitDataPlayerStrc) names `bHasPendingInteract` (+0x150),
`eQueuedSkillId` (+0x154), `eTargetUnitType` (+0x158) and `nTargetUnitGUID`
(+0x15c) are zeroed: exactly the fields "The pending interact's fields"
below names as the walk queue.

Each case is a fresh unit and player-data block filled with non-zero
"before" values (the pattern below, so a truncated clear shows which field
survived), fed through the real function, and the four fields read back.
"""
import sys

import emu

UNIT_SIZE = 244
PLAYER_DATA_SIZE = 364
FIELDS = (0x150, 0x154, 0x158, 0x15C)   # bHasPendingInteract, eQueuedSkillId, eTargetUnitType, nTargetUnitGUID


def make_unit(e, unit_type, with_player_data):
    unit = e.alloc(UNIT_SIZE)
    e.w32(unit + 0, unit_type)   # D2UnitStrc.eUnitType
    if with_player_data:
        data = e.alloc(PLAYER_DATA_SIZE)
        for i, offset in enumerate(FIELDS):
            e.w32(data + offset, 0x11111111 * (i + 1) & 0xFFFFFFFF)
        e.w32(unit + 20, data)   # D2UnitStrc.pUnitData
        return unit, data
    e.w32(unit + 20, 0)
    return unit, 0


def run_case(e, unit_type, with_player_data):
    unit, data = make_unit(e, unit_type, with_player_data)
    e.call(0x45C470, ecx=unit)
    is_player = unit_type == 0 and with_player_data
    if not is_player:
        print(unit_type, with_player_data, "untouched (no player data to clear)")
        return
    cleared = all(e.r32(data + offset) == 0 for offset in FIELDS)
    print(unit_type, with_player_data, "cleared" if cleared else "DISAGREE: a field survived")


def main(count, seed):
    e = emu.Emu()
    e.call(0x45C470, ecx=0)   # a null unit must not fault
    print("null unit OK")
    for unit_type in (0, 1, 2, 4):   # PLAYER, MONSTER, OBJECT, ITEM
        for with_player_data in (True, False):
            run_case(e, unit_type, with_player_data)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
