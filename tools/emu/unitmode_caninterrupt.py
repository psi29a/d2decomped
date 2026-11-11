# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether a queued action may interrupt a unit's current skill, as the
oracle: FUN_00480ba0 (UNITMODE_CanInterruptSkill).

    uv run python unitmode_caninterrupt.py

docs/research/re/collision.md's "The action send" cites FUN_00480ba0 only
as "(not traced) must accept it first (0: return, nothing sent)". Traced
here: pUnit arrives in ESI (pushed once, as GetCurrentSkill's one stdcall
stack arg); with no current skill (FUN_00620250 null), an invalid skill id
(FUN_00643ce0 < 0 or >= Skills.txt's row count) or a null Skills.txt row,
it answers 1 (allowed) without looking at pUnit at all. With a skill row,
it reads the row's own flags byte at +7 against a fixed mask (0x80, read
from game.exe's own data at 0x6ce284 -- Skills.txt's "NoInterrupt"-shaped
bit); set, it answers 1 regardless of pUnit. Clear, it answers 0 for a
null pUnit or one whose mode (unit+0x10) is not 1 or 5 (Neutral,
TownNeutral), else 1. This oracle fakes `sgptDataTable` (the global at
0x744304) with a tiny Skills.txt, hooks GetCurrentSkill/GetSkillId to
answer given values, and runs every branch for real: no disagreement.
"""
import emu

CAN_INTERRUPT = 0x480BA0
GET_CURRENT_SKILL = 0x620250
GET_SKILL_ID = 0x643CE0
SGPT_DATA_TABLE = 0x744304
ROW_SIZE = 0x23C
FLAGS_OFFSET = 7
NO_INTERRUPT_MASK = 0x80   # game.exe's own byte at 0x6ce284, read directly below


def setup(e, skill_count):
    rows = e.alloc(ROW_SIZE * skill_count)
    table = e.alloc(0x2000)
    e.w32(table + 0xB98, rows)        # pTxtSkills
    e.w32(table + 0xBA0, skill_count)  # nTxtSkillsCount
    e.w32(SGPT_DATA_TABLE, table)
    return rows


def run_case(e, rows, pskill, skill_id, skill_count, row_flags, unit, mode):
    e.hook(GET_CURRENT_SKILL, lambda ev: pskill, 1)   # stdcall(pUnit): one stack arg
    e.hook(GET_SKILL_ID, lambda ev: skill_id, 3)       # stdcall(pSkill, file, line): three
    if unit:
        e.w32(unit + 0x10, mode)
    if 0 <= skill_id < skill_count and row_flags is not None:
        e.mu.mem_write(rows + skill_id * ROW_SIZE + FLAGS_OFFSET, bytes([row_flags]))

    result = e.call(CAN_INTERRUPT, regs={"ESI": unit})

    if pskill == 0 or not (0 <= skill_id < skill_count) or row_flags is None:
        expect = 1
    elif row_flags & NO_INTERRUPT_MASK:
        expect = 1
    elif not unit:
        expect = 0
    else:
        expect = 1 if mode in (1, 5) else 0
    print(bool(pskill), skill_id, skill_count, row_flags, bool(unit), mode,
          "OK" if result == expect else f"DISAGREE got={result} expect={expect}")


def main():
    e = emu.Emu()
    rows = setup(e, 5)
    unit = e.alloc(0x200)
    assert e.r32(0x6CE284) & 0xFF == NO_INTERRUPT_MASK, "game.exe's own byte at 0x6ce284 changed"

    run_case(e, rows, 0, -1, 5, None, 0, 0)            # no current skill
    for skill_id in (-1, 5, 10):                        # out of range / row missing
        run_case(e, rows, 0x1234, skill_id, 5, None, 0, 0)
    for flags in (0x00, 0x80, 0x81):
        for unit_arg in (0, unit):
            for mode in (0, 1, 3, 5, 7):
                run_case(e, rows, 0x1234, 2, 5, flags, unit_arg, mode)


if __name__ == "__main__":
    main()
