# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 8 DATATBLS table loaders and the experience ratio getter in stock game.exe 1.14d.

    uv run python txt_schemas.py [--mutation-check]

Loaders covered (DataTbls.cpp, LvlTbls.cpp, ItemTbls.cpp, MonsterTbls.cpp), each
built from a `D2BinFieldInputStrc` list and handed to CompileTxt 0x61_22f0:

* FUN_00617d90 (TXT_AllocTxt_difficultylevels, stdcall pool): 22 INT columns,
  record 0x58, table 0x96c8ac, count 0x96c8b0; any count other than 3 is fatal (line 0x61f).
* FUN_00637620 (itemratio, fastcall pool): Version SHORT at 0x40, Uber and
  Class Specific CHAR at 0x42 / 0x43, 16 INT columns, record 0x44, table 0x96cc4c,
  count 0x96cc48.
* FUN_0061f130 (lvlwarp, fastcall): 11 INT columns and Direction ASCII 1 at 0x2c,
  record 0x30, table 0x96ca00, count 0x96ca04 (copied from a stack slot).
* FUN_0061f380 (lvlmaze, fastcall): 7 INT columns, record 0x1c, table 0x96ca08,
  count 0x96ca0c.
* FUN_00659500 (monlvl, fastcall): 30 INT columns (AC, TH, HP, DM, XP and their
  L- per-level variants), record 0x78, table 0x96c7a0, count 0x96c7a4.
* FUN_00637500 (lowqualityitems, fastcall): one ASCII 0x1f column, record 0x22,
  table 0x96cc5c, count 0x96cc58; after the compile each row's name goes through
  STRTABLE_LookupString 0x524d30 and the id is stored as a short at +0x20.
* FUN_00636770 (qualityitems, fastcall): 22 columns (nummods, two NAME_INT mod
  codes linked to 0x96bcd8, params, effect strings, the ten equipment flags),
  record 0x70, table 0x96cc54, count 0x96cc50. Each non-empty effect string is
  looked up; the ids go to +0x6c (effect1) and +0x6e (effect2) as shorts.
* FUN_00613e60 (TXT_Experience_GetExpRatio, stdcall level): reads the table at
  0x96c8a8. Level <= 0 gives row 0's ExpRatio (+0x1c), a level above the
  first dword (signed) gives 0, else row level + 1 (+0x3c with rows of 0x20 bytes).
  A null table gives 0.

The compile hook is `monster_loaders.Rig`: it records the call and the column
list, writes the scripted row count and returns the scripted table. Each loader
case scrambles the data page 0x96b000..0x96e000 and requires the changed dwords
to be exactly the ones the model writes. The lowqualityitems and qualityitems
cases also compare the bytes of their (allocated) row table after the post-process
and the STRTABLE lookups, in order.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail. Verified for the inputs drawn here; the hooked callees
(CompileTxt, the string lookup, the fatal path) are checked for call order and
arguments only.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require
from monster_loaders import (ASCII, CHAR, INT, MASK, NAME_INT, REGION, REGION_END, SHORT, Rig, check_compile_call,
                             rows_value, s32, schema)

DIFF_LOAD, RATIO_LOAD, WARP_LOAD, MAZE_LOAD, MONLVL_LOAD = 0x61_7D90, 0x63_7620, 0x61_F130, 0x61_F380, 0x65_9500
LOWQ_LOAD, QUAL_LOAD, EXP_RATIO = 0x63_7500, 0x63_6770, 0x61_3E60
DIFF_TABLE, DIFF_COUNT = 0x96_C8AC, 0x96_C8B0
RATIO_TABLE, RATIO_COUNT = 0x96_CC4C, 0x96_CC48
WARP_TABLE, WARP_COUNT = 0x96_CA00, 0x96_CA04
MAZE_TABLE, MAZE_COUNT = 0x96_CA08, 0x96_CA0C
MONLVL_TABLE, MONLVL_COUNT = 0x96_C7A0, 0x96_C7A4
LOWQ_TABLE, LOWQ_COUNT = 0x96_CC5C, 0x96_CC58
QUAL_TABLE, QUAL_COUNT, QUAL_LINK = 0x96_CC54, 0x96_CC50, 0x96_BCD8
EXP_TABLE = 0x96_C8A8
DIFF_HALT_LINE = 0x61F
MUTATIONS = ("diff-no-halt", "diff-halt-line", "diff-aicurse-offset", "ratio-version-offset", "ratio-uber-int",
             "warp-direction-int", "warp-tiles-offset", "maze-merge-offset", "monlvl-xpn-offset", "monlvl-table-slot",
             "lowq-id-offset", "qual-first-id-offset", "qual-no-empty-check", "qual-mod2-linker", "exp-row0-zero",
             "exp-stride", "exp-count-off")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def cols(*rows):
    """Column rows (name, type, size, offset, linker) to the list the rig compares."""
    return [(name, t, size, off, link) for name, t, size, off, link in rows]


def ints(*pairs):
    """Plain INT columns from (name, offset) pairs."""
    return [(name, INT, 0, off, 0) for name, off in pairs]


def difficulty_columns():
    """difficultylevels: 22 INT columns in the order the loader builds them."""
    aicurse = 0x1C if not mut("diff-aicurse-offset") else 0x18
    return ints(("ResistPenalty", 0), ("DeathExpPenalty", 4), ("UberCodeOddsNormal", 8), ("UberCodeOddsGood", 0xC),
                ("UltraCodeOddsNormal", 0x20), ("UltraCodeOddsGood", 0x24), ("LifeStealDivisor", 0x28),
                ("ManaStealDivisor", 0x2C), ("MonsterSkillBonus", 0x10), ("MonsterFreezeDivisor", 0x14),
                ("MonsterColdDivisor", 0x18), ("AiCurseDivisor", aicurse), ("UniqueDamageBonus", 0x30),
                ("ChampionDamageBonus", 0x34), ("HireableBossDamagePercent", 0x38), ("MonsterCEDamagePercent", 0x3C),
                ("StaticFieldMin", 0x40), ("GambleRare", 0x44), ("GambleSet", 0x48), ("GambleUnique", 0x4C),
                ("GambleUber", 0x50), ("GambleUltra", 0x54))


def itemratio_columns():
    """itemratio: Version and the two class flags, then the drop columns and their divisors and minimums."""
    version_off = 0x40 if not mut("ratio-version-offset") else 0x42
    uber_type = INT if mut("ratio-uber-int") else CHAR
    rows = [("Version", SHORT, 0, version_off, 0), ("Uber", uber_type, 0, 0x42, 0), ("Class Specific", CHAR, 0, 0x43, 0)]
    rows += ints(("Unique", 0), ("UniqueDivisor", 4), ("UniqueMin", 8), ("Rare", 0xC), ("RareDivisor", 0x10),
                 ("RareMin", 0x14), ("Set", 0x18), ("SetDivisor", 0x1C), ("SetMin", 0x20), ("Magic", 0x24),
                 ("MagicDivisor", 0x28), ("MagicMin", 0x2C), ("HiQuality", 0x30), ("HiQualityDivisor", 0x34),
                 ("Normal", 0x38), ("NormalDivisor", 0x3C))
    return rows


def lvlwarp_columns():
    """lvlwarp: eleven INT columns, then Direction as a one-byte ASCII column."""
    direction_type = INT if mut("warp-direction-int") else ASCII
    tiles = 0x28 if not mut("warp-tiles-offset") else 0x2C
    rows = ints(("Id", 0), ("SelectX", 4), ("SelectY", 8), ("SelectDX", 0xC), ("SelectDY", 0x10), ("ExitWalkX", 0x14),
                ("ExitWalkY", 0x18), ("OffsetX", 0x1C), ("OffsetY", 0x20), ("LitVersion", 0x24))
    rows += [("Tiles", INT, 0, tiles, 0), ("Direction", direction_type, 1 if direction_type == ASCII else 0, 0x2C, 0)]
    return rows


def lvlmaze_columns():
    """lvlmaze: seven INT columns."""
    merge = 0x18 if not mut("maze-merge-offset") else 0x1C
    return ints(("Level", 0), ("Rooms", 4), ("Rooms(N)", 8), ("Rooms(H)", 0xC), ("SizeX", 0x10), ("SizeY", 0x14)) + \
        [("Merge", INT, 0, merge, 0)]


def monlvl_columns():
    """monlvl: the normal, nightmare and hell columns and their L- per-level variants (30 INT columns)."""
    xp_n = 100 if not mut("monlvl-xpn-offset") else 104
    return ints(("AC", 0), ("AC(N)", 4), ("AC(H)", 8), ("TH", 0x18), ("TH(N)", 0x1C), ("TH(H)", 0x20), ("HP", 0x30),
                ("HP(N)", 0x34), ("HP(H)", 0x38), ("DM", 0x48), ("DM(N)", 0x4C), ("DM(H)", 0x50), ("XP", 0x60),
                ("XP(N)", xp_n), ("XP(H)", 0x68), ("L-AC", 0xC), ("L-AC(N)", 0x10), ("L-AC(H)", 0x14), ("L-TH", 0x24),
                ("L-TH(N)", 0x28), ("L-TH(H)", 0x2C), ("L-HP", 0x3C), ("L-HP(N)", 0x40), ("L-HP(H)", 0x44),
                ("L-DM", 0x54), ("L-DM(N)", 0x58), ("L-DM(H)", 0x5C), ("L-XP", 0x6C), ("L-XP(N)", 0x70),
                ("L-XP(H)", 0x74))


def qualityitems_columns():
    """qualityitems: nummods, two mod codes (linked to 0x96bcd8), mod params, the effect strings and the ten flags."""
    rows = [("nummods", CHAR, 0, 0xA, 0), ("mod1code", NAME_INT, 0, 0xC, QUAL_LINK),
            ("mod2code", NAME_INT, 0, 0x1C, QUAL_LINK if not mut("qual-mod2-linker") else 0)]
    rows += ints(("mod1param", 0x10), ("mod1min", 0x14), ("mod1max", 0x18), ("mod2param", 0x20), ("mod2min", 0x24),
                 ("mod2max", 0x28))
    rows += [("effect1", ASCII, 0x1F, 0x2C, 0), ("effect2", ASCII, 0x1F, 0x4C, 0)]
    rows += [(name, CHAR, 0, off, 0) for name, off in (("armor", 0), ("weapon", 1), ("shield", 2), ("scepter", 3),
                                                       ("wand", 4), ("staff", 5), ("bow", 6), ("boots", 7),
                                                       ("gloves", 8), ("belt", 9))]
    return rows


def lowq_columns():
    """lowqualityitems: one 0x1f-byte ASCII name."""
    return [("Name", ASCII, 0x1F, 0, 0)]


def compile_log(pool, name, count_at, record):
    """The CompileTxt record the rig logs for one loader call."""
    return [("compile", pool, name, count_at, record)]


def make_table(rig, rng, rows, record):
    """A row table of `rows` records with random bytes; returns (address, bytes)."""
    size = max(rows, 1) * record + 0x40
    content = bytearray(rng.randbytes(size))
    address = rig.m.alloc(size)
    rig.write(address, content)
    return address, content


def check_difficulty(rig, rng):
    """FUN_00617d90 (stdcall pool): the difficultylevels table, fatal unless it has 3 rows."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows = rng.choice((3, 3, 2, 4, 0, 1, MASK, 0x100))
    rig.table = rng.randrange(1 << 32)
    got = rig.go(DIFF_LOAD, pool)
    halt_line = DIFF_HALT_LINE if not mut("diff-halt-line") else DIFF_HALT_LINE + 1
    if rig.rows != 3 and not mut("diff-no-halt"):
        require(got == ("halt", halt_line), f"difficultylevels with {rig.rows} rows: {got}")
    else:
        require(got[0] == "ret", f"difficultylevels with {rig.rows} rows: {got}")
    want_fields = schema(*difficulty_columns())
    check_compile_call(rig, ("ret", 0) if got[0] == "halt" else got, compile_log(pool, "difficultylevels", DIFF_COUNT, 0x58),
                       want_fields, {DIFF_TABLE: rig.table, DIFF_COUNT: rig.rows}, before, "difficultylevels")


def check_ratio(rig, rng):
    """FUN_00637620 (fastcall pool): the itemratio table."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(RATIO_LOAD, ecx=pool)
    check_compile_call(rig, got, compile_log(pool, "itemratio", RATIO_COUNT, 0x44), schema(*itemratio_columns()),
                       {RATIO_TABLE: rig.table, RATIO_COUNT: rig.rows}, before, "itemratio")


def check_lvlwarp(rig, rng):
    """FUN_0061f130 (fastcall pool): the lvlwarp table, its row count copied from a stack slot."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(WARP_LOAD, ecx=pool)
    # the row count goes through a stack slot, whose address the rig logs; only the other arguments are the model's
    rig.log = [(e[0], e[1], e[2], None, e[4]) if e[0] == "compile" else e for e in rig.log]
    check_compile_call(rig, got, [("compile", pool, "lvlwarp", None, 0x30)], schema(*lvlwarp_columns()),
                       {WARP_TABLE: rig.table, WARP_COUNT: rig.rows}, before, "lvlwarp")


def check_lvlmaze(rig, rng):
    """FUN_0061f380 (fastcall pool): the lvlmaze table."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(MAZE_LOAD, ecx=pool)
    check_compile_call(rig, got, compile_log(pool, "lvlmaze", MAZE_COUNT, 0x1C), schema(*lvlmaze_columns()),
                       {MAZE_TABLE: rig.table, MAZE_COUNT: rig.rows}, before, "lvlmaze")


def check_monlvl(rig, rng):
    """FUN_00659500 (fastcall pool): the monlvl table."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(MONLVL_LOAD, ecx=pool)
    table_at = MONLVL_TABLE if not mut("monlvl-table-slot") else MONLVL_TABLE + 8
    check_compile_call(rig, got, compile_log(pool, "monlvl", MONLVL_COUNT, 0x78), schema(*monlvl_columns()),
                       {table_at: rig.table, MONLVL_COUNT: rig.rows}, before, "monlvl")


def check_lowqualityitems(rig, rng):
    """FUN_00637500 (fastcall pool): the compile, then STRTABLE per row for the id at +0x20."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rows = rng.choice((0, 1, 2, 4))
    table, content = make_table(rig, rng, rows, 0x22)
    rig.rows, rig.table = rows, table
    ids = [rng.randrange(1 << 32) for _ in range(rows)]
    rig.ids = list(ids)
    got = rig.go(LOWQ_LOAD, ecx=pool)
    want_log = compile_log(pool, "lowqualityitems", LOWQ_COUNT, 0x22) + [("lookup", table + 0x22 * i) for i in range(rows)]
    check_compile_call(rig, got, want_log, schema(*lowq_columns()), {LOWQ_TABLE: table, LOWQ_COUNT: rows}, before,
                       "lowqualityitems")
    expect = bytearray(content)
    off_id = 0x20 if not mut("lowq-id-offset") else 0x22
    for i in range(rows):
        struct.pack_into("<H", expect, 0x22 * i + off_id, ids[i] & 0xFFFF)
    require(bytes(rig.m.read(table, len(content))) == bytes(expect), "lowqualityitems: row table after the lookups differs")


def check_qualityitems(rig, rng):
    """FUN_00636770 (fastcall pool): compile, then each non-empty effect string's id at +0x6c / +0x6e."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rows = rng.choice((0, 1, 2, 3))
    table, content = make_table(rig, rng, rows, 0x70)
    for i in range(rows):
        for field in (0x2C, 0x4C):
            if rng.random() < 0.5:
                content[0x70 * i + field] = 0
            elif content[0x70 * i + field] == 0:
                content[0x70 * i + field] = rng.randrange(1, 256)
    rig.write(table, content)
    rig.rows, rig.table = rows, table
    plan = []
    for i in range(rows):
        for field, slot in ((0x2C, 0x6C), (0x4C, 0x6E)):
            if mut("qual-no-empty-check") or content[0x70 * i + field]:
                plan.append((i, field, slot))
    if mut("qual-first-id-offset") and plan:
        plan = [(i, f, 0x6E if s == 0x6C else s) for i, f, s in plan]
    ids = [rng.randrange(1 << 32) for _ in plan]
    rig.ids = list(ids)
    got = rig.go(QUAL_LOAD, ecx=pool)
    want_log = compile_log(pool, "qualityitems", QUAL_COUNT, 0x70)
    want_log += [("lookup", table + 0x70 * i + field) for i, field, _slot in plan]
    check_compile_call(rig, got, want_log, schema(*qualityitems_columns()), {QUAL_TABLE: table, QUAL_COUNT: rows}, before,
                       "qualityitems")
    expect = bytearray(content)
    for (i, _field, slot), value in zip(plan, ids):
        struct.pack_into("<H", expect, 0x70 * i + slot, value & 0xFFFF)
    require(bytes(rig.m.read(table, len(content))) == bytes(expect), "qualityitems: row table after the lookups differs")


def check_exp_ratio(rig, rng):
    """FUN_00613e60 (stdcall level): the experience ratio of a level, read only."""
    rig.scramble()
    count = rng.choice((0, 1, 2, 5, 40, -1, MASK)) & MASK
    rows = max(s32(count), 0) + 3
    content = bytearray(rng.randbytes(0x20 * rows))
    struct.pack_into("<I", content, 0, count & MASK)
    table = 0 if rng.random() < 0.1 else rig.m.alloc(len(content))
    if table:
        rig.write(table, content)
    rig.m.w32(EXP_TABLE, table)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    n = rng.choice((-3, -1, 0, 1, 2, 3, s32(count), s32(count) + 1, s32(count) - 1, rng.randrange(-4, 45))) & MASK
    got = rig.go(EXP_RATIO, n)
    if not table:
        want = 0
    elif s32(n) <= 0 and not mut("exp-row0-zero"):
        want = dword_at(content, 0x1C)
    elif s32(n) <= 0:
        want = 0
    elif s32(n) > s32(count) - (1 if mut("exp-count-off") else 0):
        want = 0
    else:
        want = dword_at(content, 0x20 * s32(n) + (0x3C if not mut("exp-stride") else 0x40))
    require(got == ("ret", want) and not rig.changed(before), f"ExpRatio({s32(n)}) with count {s32(count)}: {got} != {want:#x}")


def dword_at(content, offset):
    """The dword at `offset` of a bytes-like table; the length is checked before the read."""
    require(offset + 4 <= len(content), f"model reads past the table at {offset:#x}")
    return struct.unpack_from("<I", content, offset)[0]


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x613E60)
    rng = random.Random(0x617D90)
    rig = Rig()
    plan = [(check_difficulty, 150), (check_ratio, 60), (check_lvlwarp, 60), (check_lvlmaze, 60), (check_monlvl, 60),
            (check_lowqualityitems, 150), (check_qualityitems, 150), (check_exp_ratio, 300)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            rig.fields, rig.links, rig.ids = [], [], []
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "DATATBLS table loader and getter cases")


if __name__ == "__main__":
    main()
