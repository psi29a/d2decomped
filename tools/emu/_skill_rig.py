# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the SKILLS\\Skills.cpp oracles (skill_*.py): not run by itself.

Builds on _dungeon_rig.Rig (stock game.exe 1.14d in Unicorn, one random-filled
arena, a hand-written model run on the same bytes). Adds the D2Common data
table the skill helpers read (sgptDataTable, the pointer at 0x744304) inside
the arena: the Skills.txt rows (0x23c bytes each) and the MonStats rows
(0x1a8 bytes each), with random counts, null pointers and out-of-range ids.
No MPQs are read.
"""
from _dungeon_rig import M32

DATA_TABLE = 0x744304       # sgptDataTable
BIT_MASKS = 0x6CE26C        # Fog gadwBitMasks[1]
TAB, SKILLS, MONSTATS = 0x000, 0xC00, 0x1800   # arena offsets of the table, its Skills rows, its MonStats rows
SKILL_ROW, MON_ROW = 0x23C, 0x1A8
FILE_SKILLS = 0x6D_AB88     # ".\SKILLS\Skills.cpp"


def put(img, off, value, size=4):
    """Write a little-endian `size`-byte value at arena offset `off`."""
    img[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def point_table(rig):
    """Point sgptDataTable (0x744304) at the arena's table; the layout is fixed, the contents per case."""
    rig.m.w32(DATA_TABLE, rig.arena + TAB)


def fill_tables(img, rng, rig, skill_count=None, mon_count=None, null_skills=False):
    """Random table counts and base pointers: Skills +0xb98 / +0xba0, MonStats +0xa78 / +0xa80.

    Returns (skill_count, mon_count); the Skills count is signed and may be 0 or negative (every reader
    compares it signed, so no id is valid). A null Skills base keeps the count at 1 so row 0 is the only
    index that is read (the code tests the row address for null after adding the offset).
    """
    n = rng.choice((1, 2, 3, 4, 4, 0, -1, -5, -0x80000000)) if skill_count is None else skill_count
    base = rig.arena + SKILLS
    if null_skills:
        base, n = 0, 1
    put(img, TAB + 0xB98, base)
    put(img, TAB + 0xBA0, n & M32)
    k = rng.randrange(1, 4) if mon_count is None else mon_count
    put(img, TAB + 0xA78, rig.arena + MONSTATS)
    put(img, TAB + 0xA80, k)
    return n, k


def skill_id(rng, n):
    """A skill id: mostly in range (when the count is positive), sometimes -1, -2, n, n + 1 or a huge value."""
    return rng.choice([rng.randrange(0, n)] * 4 + [-1, -2, n, n + 1, 0x7FFFFFFF, -0x80000000]) & M32 if n > 0 else rng.choice((0, 1, -1, n, 0x7FFFFFFF)) & M32
