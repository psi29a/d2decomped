# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify three UnitSnd.cpp sound-table lookups in stock game.exe 1.14d.

    uv run python unit_snd_tables.py [--mutation-check]

  FUN_004ca3e0 UNITSND_GetBaseSoundId(EAX classId): fatal past 6, else
    gaUnitSoundTableByClassId[classId] (a pointer into the per-class row at
    0x72a008; d2d's `kPlayerVoice`, components/rules/sound_ids.hpp).
  FUN_004cb380 UNITSND_GetModeChangeSound(stack prevMode, ESI unit, EDI
    newMode): fatal past class 0x23c or mode 7; else
    gaUnitSoundTableByClassIdExtra[GetSndUnitClassId(unit)] (a pointer into
    0x7295f8's rows), null -> 0, else row[newMode] unless the row is the
    shared record at 0x729328 and prevMode > newMode (0). d2d's
    `object_sound`, components/rules/object_sounds.hpp.
  FUN_004cb3e0 UNITSND_GetSpecialSoundByMode(ESI unit, EDI mode): same
    class lookup; class 0x3d -> gaUnitSoundTableSpecialMode[mode] (0x728338,
    8 entries); else row[8]/[9]/[10]/[11] as (sound1, mode1, sound2, mode2):
    0 when both sounds are 0; sound1 when mode1 == mode or mode1 == 8
    (any); else sound2 when mode2 == mode or mode2 == 8; else 0. d2d's
    `object_loop`.

GetSndUnitClassId (FUN_004ca2c0) is hooked to hand back a scripted class id
so every row in game.exe's own tables is driven directly, not a synthetic
one; the row data comes from game.exe's memory, read once, so the check is
real table content against a model of the branches alone. The fatal paths
(class > 0x23c, mode > 7) aren't driven: GetBaseSoundId's domain is 0..6,
and the other two only ever see 0..7 in play (d2d's callers never pass more).
No MPQs are read.
"""
import argparse
import hashlib
import random
import sys
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
GET_BASE = 0x4CA3E0
GET_MODE_CHANGE = 0x4CB380
GET_SPECIAL = 0x4CB3E0
GET_CLASS_ID = 0x4CA2C0
TABLE_PLAYER = 0x72A008    # 7 pointers, one per player class
TABLE_OBJ = 0x7295F8       # 0x23d pointers, by GetSndUnitClassId's object id
SHARED_PTR = 0x729328      # the row shared by every "later mode only" object
SPECIAL_TABLE = 0x728338   # class 0x3d's 8-entry mode table
MAX_CLASS = 0x23C
# d2d's kPlayerVoice (components/rules/sound_ids.hpp): hit, death, step.
PLAYER_VOICE = ((2878, 2883, 2768), (2910, 2915, 2720), (2894, 2899, 2720), (2886, 2891, 2768),
                (2902, 2907, 2816), (2926, 2931, 2720), (2918, 2923, 2720))
MUTATIONS = ("swap-special-slots", "ignore-shared", "mode-sentinel-7", "class-3d-generic")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def require(condition, message):
    """Raise RuntimeError(message) unless condition holds."""
    if not condition:
        raise RuntimeError(message)


def model_mode_change(row, row_ptr, prev_mode, new_mode):
    """FUN_004cb380's branches alone, given the row already read (or None)."""
    if row_ptr == 0:
        return 0
    blocked = row_ptr == SHARED_PTR and prev_mode > new_mode
    if mut("ignore-shared"):
        blocked = False
    return 0 if blocked else row[new_mode]


def model_special(row, row_ptr, class_id, mode, special_row):
    """FUN_004cb3e0's branches alone, given the row and the 0x3d table already read."""
    if class_id == 0x3D and not mut("class-3d-generic"):
        return special_row[mode]
    if row_ptr == 0:
        return 0
    s1, m1, s2, m2 = row[8], row[9], row[10], row[11]
    if mut("swap-special-slots"):
        s1, s2 = s2, s1
    sentinel = 7 if mut("mode-sentinel-7") else 8
    if s1 == 0 and s2 == 0:
        return 0
    if m1 == mode or m1 == sentinel:
        return s1
    if m2 == mode or m2 == sentinel:
        return s2
    return 0


def run_all():
    """Drive the three functions over game.exe's own tables; returns the case count."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    scripted = {"class": 0}
    machine.hook(GET_CLASS_ID, lambda _e: scripted["class"], 0)
    cases = 0

    # FUN_004ca3e0 against d2d's kPlayerVoice literal values.
    for class_id, voice in enumerate(PLAYER_VOICE):
        pointer = machine.call(GET_BASE, regs={"eax": class_id})
        require(pointer == machine.r32(TABLE_PLAYER + 4 * class_id),
                f"GetBaseSoundId({class_id}): pointer {pointer:#x} not the table's own row")
        hit, death, step = machine.r32(pointer), machine.r32(pointer + 4), machine.r32(pointer + 0x28)
        require((hit, death, step) == voice, f"class {class_id}: ({hit}, {death}, {step}) != kPlayerVoice {voice}")
        cases += 1

    special_row = [machine.r32(SPECIAL_TABLE + 4 * i) for i in range(8)]
    rng = random.Random(0x4CB380)
    modes = (0, 3, 7)
    for class_id in range(MAX_CLASS + 1):
        row_ptr = machine.r32(TABLE_OBJ + 4 * class_id)
        scripted["class"] = class_id
        row = [machine.r32(row_ptr + 4 * i) for i in range(12)] if row_ptr else None

        for new_mode in range(8):
            for prev_mode in (modes if row_ptr == SHARED_PTR else (rng.choice(modes),)):
                got = machine.call(GET_MODE_CHANGE, prev_mode, regs={"esi": 0, "edi": new_mode})
                want = model_mode_change(row, row_ptr, prev_mode, new_mode)
                require(got == want, f"GetModeChangeSound(class {class_id:#x}, prev {prev_mode}, new {new_mode}): "
                                      f"{got} != {want}")
                cases += 1

        for mode in range(8):
            got = machine.call(GET_SPECIAL, regs={"esi": 0, "edi": mode})
            want = model_special(row, row_ptr, class_id, mode, special_row)
            require(got == want, f"GetSpecialSoundByMode(class {class_id:#x}, mode {mode}): {got} != {want}")
            cases += 1
    return cases


def main():
    """Run the cases, or --mutation-check each deliberate model defect."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    global mutation
    if options.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all()
            except RuntimeError:
                print(f"caught: {name}")
                continue
            sys.exit(f"NOT caught: {name}")
        print(f"ok: {len(MUTATIONS)} deliberate model defects all caught")
        return
    print(f"ok: {run_all()} UnitSnd.cpp table-lookup cases (GetBaseSoundId, GetModeChangeSound, "
          "GetSpecialSoundByMode)")


if __name__ == "__main__":
    main()
