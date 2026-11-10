# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify two LvlTbls.cpp functions in stock game.exe 1.14d: the expansion level-table free and the portal array.

    uv run python lvl_small.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

* TXT_FreeExpansionLvlTbls (FUN_0061fbf0, no arguments): if the expansion table
  pointer 0x96ca1c is set, pool-free it (LvlTbls.cpp line 0x43a). It is not cleared.
* LVLTBLS_BuildPortalLevelArray (FUN_0061dd00, cdecl, the LevelDefs row table in
  EAX): counts the level rows whose Portal flag (dword at +0x8c, rows of 0x9c
  bytes) is set, for the level count at +0xc5c of the Items table (pointer at
  0x744304); the count goes to 0x96c9f8. With a non-zero count it allocates
  count * 4 bytes (line 0x16c) into 0x96c9f4 and fills it, in row order, with the
  row index of each flagged level. A zero count leaves 0x96c9f4 alone.

The pool allocator and free are hooked by the world; memset runs for real. The
halt inside the fill (line 0x177) cannot be reached with one table and is not
run. `--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import Mismatch, require

EXPANSION_FREE, PORTAL = 0x61_FBF0, 0x61_DD00
FILE, FREE_LINE, ALLOC_LINE = 0x6E_84DC, 0x43A, 0x16C
EXPANSION_PTR, PORTAL_COUNT, PORTAL_ARRAY = 0x96_CA1C, 0x96_C9F8, 0x96_C9F4
ITEMS_TABLE, LEVELS_OFFSET = 0x74_4304, 0xC5C
ROW, PORTAL_OFFSET = 0x9C, 0x8C
MUTATIONS = ("expansion-no-free", "expansion-line", "expansion-clears", "portal-offset", "portal-skip-last",
             "portal-alloc-line", "portal-index-plus-one", "portal-zero-clears")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Rig(mw.World):
    """The emulated game; the Items table is a block the oracle fills with level rows."""

    def __init__(self):
        """Build the world and note the logs."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 200000

    def go(self, address, *args, ecx=0, regs=None, nstack=None):
        """One call; the returned value or ("loop", 0) if the instruction limit ran out."""
        try:
            return ("ret", self.run(address, *args, ecx=ecx, regs=regs, nstack=nstack))
        except Mismatch as err:
            if "did not return" in str(err):
                return ("loop", 0)
            raise


def check_expansion(rig, rng):
    """FUN_0061fbf0: pool-free the expansion table when set (line 0x43a); the pointer is left set."""
    value = rng.choice((0, rng.randrange(1, 1 << 32)))
    rig.m.w32(EXPANSION_PTR, value)
    rig.log = []
    got = rig.go(EXPANSION_FREE)
    require(got[0] == "ret", f"expansion free {got}")
    line = FREE_LINE if not mut("expansion-line") else FREE_LINE + 1
    want = [] if (not value or mut("expansion-no-free")) else [("free", 0, value, ".\\DATATBLS\\LvlTbls.cpp", line, 0)]
    require(rig.log == want, f"expansion free log {rig.log} != {want}")
    cleared = 0 if mut("expansion-clears") else value
    require(rig.read_words(EXPANSION_PTR, 1)[0] == cleared, "expansion pointer after the free")


def check_portal(rig, rng):
    """LVLTBLS_BuildPortalLevelArray: count the Portal rows, allocate, fill with the flagged row indices."""
    levels = rng.choice((0, 1, 4, 12, 40))
    rows = rig.m.alloc(max(levels, 1) * ROW + 0x10)
    content = bytearray(rng.randbytes(max(levels, 1) * ROW + 0x10))
    flags = [rng.random() < 0.4 for _ in range(levels)]
    for i in range(levels):
        struct.pack_into("<I", content, i * ROW + PORTAL_OFFSET if not mut("portal-offset") else i * ROW + PORTAL_OFFSET + 4,
                         1 if flags[i] else 0)
    rig.write(rows, bytes(content))
    items = rig.m.alloc(LEVELS_OFFSET + 0x10)
    rig.m.w32(items + LEVELS_OFFSET, levels)
    rig.m.w32(ITEMS_TABLE, items)
    rig.m.w32(PORTAL_ARRAY, 0x12345678)
    rig.log = []
    got = rig.go(PORTAL, regs={"eax": rows}, nstack=0)
    require(got[0] == "ret", f"portal array {got}")
    scan = range(levels - 1) if mut("portal-skip-last") else range(levels)
    indices = [i for i in scan if flags[i]]
    count = len(indices)
    stored = rig.read_words(PORTAL_COUNT, 1)[0]
    require(stored == count, f"portal count {stored} != {count}")
    if count:
        line = ALLOC_LINE if not mut("portal-alloc-line") else ALLOC_LINE + 1
        block = rig.read_words(PORTAL_ARRAY, 1)[0]
        require(rig.log == [("alloc", 0, count * 4, ".\\DATATBLS\\LvlTbls.cpp", line, 0)], f"portal alloc log {rig.log}")
        values = indices if not mut("portal-index-plus-one") else [i + 1 for i in indices]
        require(rig.read_words(block, count) == values, f"portal array {rig.read_words(block, count)} != {values}")
    else:
        expect_pointer = 0 if mut("portal-zero-clears") else 0x12345678
        require(rig.read_words(PORTAL_ARRAY, 1)[0] == expect_pointer, "portal array changed with no flagged level")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x61FBF0)
    rng = random.Random(0x61DD00)
    rig = Rig()
    plan = [(check_expansion, 40), (check_portal, 200)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "LvlTbls expansion free and portal array cases")


if __name__ == "__main__":
    main()
