# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the two token-table merges in TokTbls.cpp (.\\DATATBLS\\TokTbls.cpp) in stock game.exe 1.14d.

    uv run python tok_merge.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

* TXT_AllocTxt_plrmode_plrtype (FUN_0065adc0, fastcall pool): compiles plrtype
  then plrmode (CompileTxt 0x6122f0, record 0x34), allocates (t + m) * 0x34 bytes
  (line 0x37), copies plrtype and then plrmode behind it, unloads both source
  tables (0x6125c0), and stores the merged block as Mode1 0x96d4dc and Mode2
  0x96d4e0, Mode3 0x96d4e4 = block + t * 0x34, and the total count 0x96d4d8.
* TXT_AllocTxt_objtype_objmode (FUN_0065b1b0, fastcall pool): the same with
  objtype then objmode (line 0x94): ObjType 0x96d4cc, ObjTypeMode 0x96d4d0,
  ObjMode 0x96d4d4 = block + t * 0x34, count 0x96d4c8.

The compile hook returns one scripted (table, rows) pair per call, so the rig's
compile hook is replaced by a queue. Each source table is a real allocated block:
the merge copies from it. `--mutation-check` breaks the model on purpose, one
defect at a time, and requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from monster_loaders import Rig

PLR_LOAD, OBJ_LOAD = 0x65_ADC0, 0x65_B1B0
COMPILE = 0x61_22F0
TOK_FILE = ".\\DATATBLS\\TokTbls.cpp"
PLR_STORES = {"ptr": 0x96_D4DC, "mode2": 0x96_D4E0, "mode3": 0x96_D4E4, "size": 0x96_D4D8}
OBJ_STORES = {"ptr": 0x96_D4CC, "mode2": 0x96_D4D0, "mode3": 0x96_D4D4, "size": 0x96_D4C8}
MUTATIONS = ("plr-merge-line", "obj-merge-line", "copy-order", "plr-skip-unload", "size-type-only",
             "plr-mode2-missing", "mode3-offset", "obj-mode-slot")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class QueueRig(Rig):
    """monster_loaders.Rig with CompileTxt returning the next scripted (table, rows) pair on each call."""

    def __init__(self):
        """Replace the rig's compile hook with the queue version."""
        super().__init__()
        self.queue = []
        self.m.hook(COMPILE, self.compile_queued, 5)

    def compile_queued(self, current):
        """CompileTxt (stdcall pool, name, fields, &rows, record): log the call, write the rows, return the next table."""
        self.log.append(("compile", current.arg(0), current.cstr(current.arg(1)), current.arg(3), current.arg(4)))
        table, rows = self.queue.pop(0)
        if current.arg(3):
            current.w32(current.arg(3), rows)
        return table


def table_block(rig, rng, rows):
    """A real table block of `rows` records (0x34 bytes each) with random bytes; returns its address."""
    size = max(rows, 1) * 0x34
    address = rig.m.alloc(size)
    rig.write(address, rng.randbytes(size))
    return address


def check_stores(rig, before, expect, what):
    """The globals written must be exactly the model's, dropping writes that do not change a value."""
    expect = {a: v & 0xFFFFFFFF for a, v in expect.items()}
    expect = {a: v for a, v in expect.items() if struct.unpack_from("<I", before, a - REGION_BASE)[0] != v}
    got = rig.changed(before)
    mw.require(got == expect, f"{what}: stores {sorted((hex(a), hex(v)) for a, v in got.items())} != "
               f"{sorted((hex(a), hex(v)) for a, v in expect.items())}")


REGION_BASE = 0x96_B000


def check_merge(rig, rng, load, tag, names, stores, line, what):
    """Two compiles, the merged block of (a + b) * 0x34 bytes, two unloads, and the stores."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rows_a, rows_b = rng.randrange(0, 4), rng.randrange(0, 4)
    if rows_a + rows_b == 0:
        rows_a = 1
    table_a, table_b = table_block(rig, rng, rows_a), table_block(rig, rng, rows_b)
    rig.queue = [(table_a, rows_a), (table_b, rows_b)]
    rig.log = []
    got = rig.go(load, ecx=pool)
    mw.require(got[0] == "ret", f"{what}: {got}")
    total = rows_a + rows_b
    if mut("size-type-only"):
        size_value = rows_a
    else:
        size_value = total
    merged = rig.read_words(stores["ptr"], 1)[0]
    first, second = (table_b, table_a) if mut("copy-order") else (table_a, table_b)
    first_n, second_n = (rows_b, rows_a) if mut("copy-order") else (rows_a, rows_b)
    expect_bytes = bytes(rig.m.read(first, first_n * 0x34)) + bytes(rig.m.read(second, second_n * 0x34))
    mw.require(merged != 0 and bytes(rig.m.read(merged, total * 0x34)) == expect_bytes, f"{what}: merged bytes differ")
    alloc_line = line + (1 if mut(f"{tag}-merge-line") else 0)
    want = [("compile", pool, names[0], 0, 0x34), ("compile", pool, names[1], 0, 0x34)]
    # the count arguments are stack slots, so only the rest of each compile entry is compared
    got_log = [(e[0], e[1], e[2], 0, e[4]) if e[0] == "compile" else e for e in rig.log]
    want += [("alloc", 0, total * 0x34, TOK_FILE, alloc_line, 0)]
    unloads = [("unload", table_a), ("unload", table_b)]
    if tag == "plr" and mut("plr-skip-unload"):
        unloads = [("unload", table_a)]
    want += unloads
    mw.require(got_log == want, f"{what}: log {got_log} != {want}")
    expect = {stores["ptr"]: merged, stores["size"]: size_value}
    if not (tag == "plr" and mut("plr-mode2-missing")):
        expect[stores["mode2"]] = merged
    mode3_rows = rows_b if mut("mode3-offset") else rows_a
    mode3_at = stores["mode3"] + (4 if (tag == "obj" and mut("obj-mode-slot")) else 0)
    expect[mode3_at] = merged + mode3_rows * 0x34
    check_stores(rig, before, expect, what)


def check_plr(rig, rng):
    """FUN_0065adc0: plrtype then plrmode, merged into one block (line 0x37)."""
    check_merge(rig, rng, PLR_LOAD, "plr", ("plrtype", "plrmode"), PLR_STORES, 0x37, "plrmode_plrtype")


def check_obj(rig, rng):
    """FUN_0065b1b0: objtype then objmode, merged into one block (line 0x94)."""
    check_merge(rig, rng, OBJ_LOAD, "obj", ("objtype", "objmode"), OBJ_STORES, 0x94, "objtype_objmode")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x65ADC0)
    rng = random.Random(0x65B1B0)
    rig = QueueRig()
    plan = [(check_plr, 80), (check_obj, 80)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "token merge cases")


if __name__ == "__main__":
    main()
