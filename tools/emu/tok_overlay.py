# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 6 token and overlay table functions in stock game.exe 1.14d: TokTbls.cpp, MissileTbls.cpp, OverlayTbls.cpp.

    uv run python tok_overlay.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

* FUN_0065aef0 (TXT_AllocTxt_monmode, fastcall pool): monmode (name ASCII 0x1f,
  token RAW4, fourteen BYTE direction counts DT..KB at +0x24..+0x31, SQ and RN at
  +0x32 / +0x33), record 0x34. The table goes to 0x96d4bc, the count to 0x96d4b8
  (copied from a stack slot), and the table to 0x96d4c0 and 0x96d4c4 too.
* FUN_0065b2e0 (composit) and FUN_0065b350 (armtype): name ASCII 0x1f and token
  RAW4 at +0x20, record 0x34; no count (null), tables to 0x96d4ec and 0x96d4e8.
* FUN_0065b3c0 (free the token tables): pool free of the merged plrtype/plrmode
  block at 0x96d4dc (TokTbls.cpp line 0xd6), unload of monmode 0x96d4bc, pool
  free of the merged objtype/objmode block at 0x96d4cc (line 0xe2), unload of
  composit 0x96d4ec and armtype 0x96d4e8; the five pointers are cleared.
* FUN_00663210 (free the missiles, MissileTbls.cpp line 0x19e): pool free of the
  table at 0x96bc90 (cleared, with the side counts at 0x96bc94 / 0x96bc98), unload of
  0x96c794 and link free of 0x96c798 (both left set), then 0x96c79c cleared.
* FUN_00664150 (TXT_AllocTxt_overlay, fastcall pool): the overlay link at 0x96c7e8
  (OverlayTbls.cpp line 0x38), 26 columns (overlay NAME_I_WORD linked to 0x96c7e8,
  Filename ASCII 0x40, the frame, offset, height, colour and flag fields), record
  0x84, table 0x96c7ec, count 0x96c7f0.

The compile, unload and link-free callees come from `monster_loaders.Rig`; the
pool allocator / free log their file string, line and flag. `--mutation-check`
breaks the model on purpose, one defect at a time, and requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from monster_loaders import ASCII, CHAR, INT, MASK, NAME_I_WORD, REGION, REGION_END, SHORT, Rig, check_compile_call, \
    rows_value, schema

BYTE, RAW4 = 6, 9
MONMODE_LOAD, COMPOSIT_LOAD, ARMTYPE_LOAD, TOK_FREE, MISSILE_FREE, OVERLAY_LOAD = (
    0x65_AEF0, 0x65_B2E0, 0x65_B350, 0x65_B3C0, 0x66_3210, 0x66_4150)
TOK_FILE, MISSILE_FILE, OVERLAY_FILE = ".\\DATATBLS\\TokTbls.cpp", ".\\DATATBLS\\MissileTbls.cpp", ".\\DATATBLS\\OverlayTbls.cpp"
MONMODE1, MONMODE_SIZE, MONMODE2, MONMODE3 = 0x96_D4BC, 0x96_D4B8, 0x96_D4C0, 0x96_D4C4
PLRTYPE_MODE, OBJ_MODE, COMPOSIT, ARMTYPE = 0x96_D4DC, 0x96_D4CC, 0x96_D4EC, 0x96_D4E8
MISSILE_TABLE, MISSILE_SIDE1, MISSILE_SIDE2, MISSILE_LIST, MISSILE_LINK, MISSILE_COUNT = (
    0x96_BC90, 0x96_BC94, 0x96_BC98, 0x96_C794, 0x96_C798, 0x96_C79C)
OVERLAY_LINK, OVERLAY_TABLE, OVERLAY_COUNT = 0x96_C7E8, 0x96_C7EC, 0x96_C7F0
MUTATIONS = ("monmode-mode2-missing", "monmode-count-slot", "composit-offset", "armtype-table-slot", "free-tok-line",
             "free-tok-skip-unload", "free-tok-keeps-composit", "free-tok-order", "missile-free-line",
             "missile-clears-list", "missile-clears-link", "missile-skips-link", "overlay-linker", "overlay-height3",
             "overlay-record",
             "overlay-count-slot", "overlay-line")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def monmode_columns():
    """monmode: name, token, the fourteen direction BYTE columns, SQ and RN."""
    names = ("DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN")
    rows = [("name", ASCII, 0x1F, 0, 0), ("token", RAW4, 0, 0x20, 0)]
    rows += [(f"{n}_Dir", BYTE, 0, 0x24 + i, 0) for i, n in enumerate(names)]
    return rows


def composit_columns():
    """composit and armtype: a name and a token, at the same offsets."""
    offset = 0x20 if not mut("composit-offset") else 0x24
    return [("name", ASCII, 0x1F, 0, 0), ("token", RAW4, 0, offset, 0)]


def overlay_columns():
    """overlay: the NAME_I_WORD key on the link, then the frame, offset, height, colour and flag columns."""
    height3 = 100 if not mut("overlay-height3") else 0x64 + 4
    link = OVERLAY_LINK if not mut("overlay-linker") else OVERLAY_LINK + 4
    return [("overlay", NAME_I_WORD, 0, 0, link), ("Filename", ASCII, 0x40, 2, 0), ("version", SHORT, 0, 0x42, 0),
            ("Frames", INT, 0, 0x44, 0), ("PreDraw", CHAR, 0, 0x48, 0), ("1ofN", INT, 0, 0x4C, 0), ("Dir", CHAR, 0, 0x50, 0),
            ("Open", CHAR, 0, 0x51, 0), ("Beta", CHAR, 0, 0x52, 0), ("Xoffset", INT, 0, 0x54, 0),
            ("Yoffset", INT, 0, 0x58, 0), ("Height1", INT, 0, 0x5C, 0), ("Height2", INT, 0, 0x60, 0),
            ("Height3", INT, 0, height3, 0), ("Height4", INT, 0, 0x68, 0), ("AnimRate", INT, 0, 0x6C, 0),
            ("LoopWaitTime", INT, 0, 0x78, 0), ("Trans", CHAR, 0, 0x7C, 0), ("InitRadius", INT, 0, 0x70, 0),
            ("Radius", INT, 0, 0x74, 0), ("Red", CHAR, 0, 0x7D, 0), ("Green", CHAR, 0, 0x7E, 0), ("Blue", CHAR, 0, 0x7F, 0),
            ("NumDirections", CHAR, 0, 0x80, 0), ("LocalBlood", CHAR, 0, 0x81, 0)]


def normalize(rig):
    """Compile calls whose count goes to a stack slot: its address is not checked, only the rest of the log."""
    rig.log = [(e[0], e[1], e[2], None, e[4]) if e[0] == "compile" else e for e in rig.log]


def check_monmode(rig, rng):
    """FUN_0065aef0: the monmode table, its count copied from a stack slot, and the two mirrored pointers."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(MONMODE_LOAD, ecx=pool)
    normalize(rig)
    writes = {MONMODE1: rig.table, MONMODE_SIZE: rig.rows, MONMODE3: rig.table}
    if not mut("monmode-mode2-missing"):
        writes[MONMODE2] = rig.table
    if mut("monmode-count-slot"):
        writes[MONMODE_SIZE] = rig.rows ^ 1
    check_compile_call(rig, got, [("compile", pool, "monmode", None, 0x34)], schema(*monmode_columns()), writes, before,
                       "monmode")


def check_plain(rig, rng, load, name, what):
    """FUN_0065b2e0 / FUN_0065b350: one CompileTxt with no count; the table pointer is the only store."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = 0, rng.randrange(1 << 32)
    got = rig.go(load, ecx=pool)
    target = COMPOSIT if what == "composit" else ARMTYPE
    if mut("armtype-table-slot") and what == "armtype":
        target += 4
    check_compile_call(rig, got, [("compile", pool, name, 0, 0x34)], schema(*composit_columns()),
                       {target: rig.table}, before, what)


def check_overlay(rig, rng):
    """FUN_00664150: the overlay link (line 0x38), then the 26 columns, record 0x84, the table and count."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    link = rng.randrange(1, 1 << 32)
    rig.links = [link]
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(OVERLAY_LOAD, ecx=pool)
    line = 0x38 if not mut("overlay-line") else 0x39
    record = 0x84 if not mut("overlay-record") else 0x88
    count_at = OVERLAY_COUNT if not mut("overlay-count-slot") else OVERLAY_COUNT + 4
    want_log = [("link", OVERLAY_FILE, line), ("compile", pool, "overlay", count_at, record)]
    check_compile_call(rig, got, want_log, schema(*overlay_columns()),
                       {OVERLAY_LINK: link, OVERLAY_TABLE: rig.table, count_at: rig.rows}, before, "overlay")


def pointer_set(rng):
    """Random pointers, some null, for a freer's globals."""
    return [rng.choice((0, rng.randrange(1, 1 << 32))) for _ in range(5)]


def check_free_tokens(rig, rng):
    """FUN_0065b3c0: free the merged plr and obj blocks (pool), unload monmode / composit / armtype, clear all five."""
    rig.scramble()
    plr, monmode, obj, composit, armtype = pointer_set(rng)
    for address, value in ((PLRTYPE_MODE, plr), (MONMODE1, monmode), (OBJ_MODE, obj), (COMPOSIT, composit),
                           (ARMTYPE, armtype)):
        rig.m.w32(address, value)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    rig.log = []
    got = rig.go(TOK_FREE)
    require_ret(got, "free tokens")
    steps = []
    if plr:
        steps.append(("free", 0, plr, TOK_FILE, 0xD6 if not mut("free-tok-line") else 0xD7, 0))
    if monmode and not mut("free-tok-skip-unload"):
        steps.append(("unload", monmode))
    if obj:
        steps.append(("free", 0, obj, TOK_FILE, 0xE2, 0))
    if composit and not mut("free-tok-keeps-composit"):
        steps.append(("unload", composit))
    if armtype:
        steps.append(("unload", armtype))
    if mut("free-tok-order") and len(steps) > 1:
        steps = steps[1:] + steps[:1]
    check_log(rig, steps, "free tokens")
    check_writes(rig, before, {PLRTYPE_MODE: 0, MONMODE1: 0, OBJ_MODE: 0, COMPOSIT: 0, ARMTYPE: 0}, "free tokens")


def check_missile_free(rig, rng):
    """FUN_00663210: free the missile table (line 0x19e), clear its side counts, unload the list, link-free the link."""
    rig.scramble()
    table, side1, side2, lst, link, count = pointer_set(rng) + [rng.randrange(1 << 32)]
    for address, value in ((MISSILE_TABLE, table), (MISSILE_SIDE1, side1), (MISSILE_SIDE2, side2), (MISSILE_LIST, lst),
                           (MISSILE_LINK, link), (MISSILE_COUNT, count)):
        rig.m.w32(address, value)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    rig.log = []
    got = rig.go(MISSILE_FREE)
    require_ret(got, "free missiles")
    steps = []
    if table:
        steps.append(("free", 0, table, MISSILE_FILE, 0x19E if not mut("missile-free-line") else 0x19F, 0))
    if lst:
        steps.append(("unload", lst))
    if link and not mut("missile-skips-link"):
        steps.append(("link-free", link))
    check_log(rig, steps, "free missiles")
    expect = {MISSILE_TABLE: 0, MISSILE_SIDE1: 0, MISSILE_SIDE2: 0, MISSILE_COUNT: 0}
    if mut("missile-clears-list"):
        expect[MISSILE_LIST] = 0
    if mut("missile-clears-link"):
        expect[MISSILE_LINK] = 0
    check_writes(rig, before, expect, "free missiles")


def require_ret(got, what):
    """The call returned (not halted or looped)."""
    mw.require(got[0] == "ret", f"{what}: {got}")


def check_log(rig, want, what):
    """The callee log must be exactly the model's, in order."""
    mw.require(rig.log == want, f"{what}: log {rig.log} != {want}")


def check_writes(rig, before, expect, what):
    """The globals written must be exactly the model's, dropping writes that do not change a value."""
    expect = {a: v & MASK for a, v in expect.items()}
    expect = {a: v for a, v in expect.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    got = rig.changed(before)
    mw.require(got == expect, f"{what}: stores {sorted((hex(a), hex(v)) for a, v in got.items())} != "
               f"{sorted((hex(a), hex(v)) for a, v in expect.items())}")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x65AEF0)
    rng = random.Random(0x65B3C0)
    rig = Rig()
    plan = [(check_monmode, 80), (check_composit_case, 40), (check_armtype_case, 40), (check_overlay, 80),
            (check_free_tokens, 150), (check_missile_free, 150)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            rig.fields, rig.links, rig.ids = [], [], []
            check(rig, rng)
            cases += 1
    return cases


def check_composit_case(rig, rng):
    """FUN_0065b2e0 (composit)."""
    check_plain(rig, rng, COMPOSIT_LOAD, "composit", "composit")


def check_armtype_case(rig, rng):
    """FUN_0065b350 (armtype)."""
    check_plain(rig, rng, ARMTYPE_LOAD, "armtype", "armtype")


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "token, missile and overlay table cases")


if __name__ == "__main__":
    main()
