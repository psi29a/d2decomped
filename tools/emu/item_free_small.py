# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 10 small DATATBLS helpers (DataTbls.cpp and ItemTbls.cpp) in stock game.exe 1.14d.

    uv run python item_free_small.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

* FUN_00611d20 (no arguments): returns the dword at 0x96c8a4.
* FUN_006125a0 (stdcall one argument): stores 1 at the compile flag 0x96c8b4 when the argument is 0, else 0.
* FUN_00636b00, FUN_006372a0, FUN_006375a0, FUN_006378f0, FUN_00637490: unload
  (0x6125c0) the table in 0x96cc54, 0x96ca94, 0x96cc5c, 0x96cc4c and 0x96cc64
  respectively. There is no null test: the unload is called even when the pointer is 0.
* FUN_006374a0 (no arguments): returns the address 0x96cc60 (the books count).
* FUN_00634220 (no arguments): pool-frees the rare-affix block at 0x96caa4 (line
  0x30b) when set, and clears it.
* FUN_00634d90 (ITEMTBLS_FreeUniqueItems, no arguments; the sets free is FUN_00636730): link-frees 0x96c850 (no null test), unloads
  0x96c854 (no null test), and clears the count at 0x96c858.

The unload and the link free are logged by the world rig; the pool free is
logged by the pool hook. `--mutation-check` breaks the model on purpose, one defect
at a time, and requires each to fail.
"""
import random
import sys

import _monster_world as mw
from _monster_world import require

GET_FLAG_VALUE, SET_COMPILE_FLAG, RARE_FREE = 0x61_1D20, 0x61_25A0, 0x63_4220
UNLOAD, LINK_FREE, POOL_FILE = 0x61_25C0, 0x6B_D0B0, 0x6E_90E4
ITEM_FILE = ".\\DATATBLS\\ItemTbls.cpp"
FLAG_VALUE, COMPILE_FLAG = 0x96_C8A4, 0x96_C8B4
UNLOAD_SLOTS = {0x63_6B00: 0x96_CC54, 0x63_72A0: 0x96_CA94, 0x63_75A0: 0x96_CC5C, 0x63_78F0: 0x96_CC4C,
                0x63_7490: 0x96_CC64}
BOOKS_COUNT_ADDR, GET_BOOKS_COUNT = 0x96_CC60, 0x63_74A0
RARE_SLOT, UNIQUE_LINK, UNIQUE_TABLE, UNIQUE_COUNT, FREE_UNIQUE = 0x96_CAA4, 0x96_C850, 0x96_C854, 0x96_C858, 0x63_4D90
MUTATIONS = ("flag-value-plus-1", "compile-flag-inverted", "unload-null-skipped", "unload-slot-wrong", "books-count-address",
             "rare-line", "rare-keeps-pointer", "unique-link-skipped", "unique-count-kept")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Rig(mw.World):
    """The emulated game with the unload and link free logged (not run) and the flag and slots set by the oracle."""

    def __init__(self):
        """Log the unload and the link free."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 200000
        self.logger(UNLOAD, 1, "unload", regs=())
        self.logger(LINK_FREE, 1, "link-free", regs=())

    def go(self, address, *args, ecx=0):
        """One call; the returned value (eax)."""
        return self.run(address, *args, ecx=ecx)


def check_flag_value(rig, rng):
    """FUN_00611d20: the dword at 0x96c8a4 (returned unchanged)."""
    value = rng.randrange(1 << 32)
    rig.m.w32(FLAG_VALUE, value)
    rig.log = []
    got = rig.go(GET_FLAG_VALUE)
    want = (value + (1 if mut("flag-value-plus-1") else 0)) & 0xFFFFFFFF
    require(got == want, f"FUN_00611d20: {got} != {want}")
    require(rig.log == [], "FUN_00611d20 logged a call")


def check_compile_flag(rig, rng):
    """FUN_006125a0 (stdcall argument): the compile flag 0x96c8b4 = (argument == 0)."""
    arg = rng.choice((0, 1, 5, 0xFFFFFFFF, rng.randrange(1 << 32)))
    rig.m.w32(COMPILE_FLAG, 0x55)
    rig.log = []
    rig.go(SET_COMPILE_FLAG, arg)
    want = 1 if arg == 0 else 0
    if mut("compile-flag-inverted"):
        want = 1 - want
    require(rig.read_words(COMPILE_FLAG, 1)[0] == want, f"compile flag for {arg:#x}")
    require(rig.log == [], "compile flag logged a call")


def check_unload(rig, rng, entry, slot):
    """One unload of a table slot, not null-tested: the unload is called with the pointer, even when it is 0."""
    value = rng.choice((0, rng.randrange(1, 1 << 32)))
    neighbour = rng.randrange(1, 1 << 32) | 1
    rig.m.w32(slot, value)
    rig.m.w32(slot + 4, neighbour)
    rig.log = []
    rig.go(entry)
    want = [("unload", value)]
    if value == 0 and mut("unload-null-skipped"):
        want = []
    if mut("unload-slot-wrong"):
        want = [("unload", neighbour)]
    require(rig.log == want, f"unload at {entry:#x}: {rig.log} != {want}")


def check_books_count(rig, rng):
    """FUN_006374a0: returns the address 0x96cc60 (no memory read)."""
    rig.log = []
    got = rig.go(GET_BOOKS_COUNT)
    want = BOOKS_COUNT_ADDR if not mut("books-count-address") else BOOKS_COUNT_ADDR + 4
    require(got == want and rig.log == [], f"books count address {got:#x}")


def check_rare_free(rig, rng):
    """FUN_00634220: pool-free the rare-affix block (line 0x30b) when set, then clear it."""
    value = rng.choice((0, rng.randrange(1, 1 << 32)))
    rig.m.w32(RARE_SLOT, value)
    rig.log = []
    rig.go(RARE_FREE)
    line = 0x30B if not mut("rare-line") else 0x30C
    want = [("free", 0, value, ITEM_FILE, line, 0)] if value else []
    require(rig.log == want, f"rare free log {rig.log} != {want}")
    cleared = 0 if not mut("rare-keeps-pointer") else value
    require(rig.read_words(RARE_SLOT, 1)[0] == cleared, "rare free pointer")


def check_unique_free(rig, rng):
    """FUN_00634d90 (ITEMTBLS_FreeUniqueItems): link-free 0x96c850, unload 0x96c854 (both not null-tested), clear 0x96c858."""
    link, table = rng.choice((0, rng.randrange(1, 1 << 32))), rng.choice((0, rng.randrange(1, 1 << 32)))
    rig.m.w32(UNIQUE_LINK, link)
    rig.m.w32(UNIQUE_TABLE, table)
    count = rng.randrange(1, 1 << 32)
    rig.m.w32(UNIQUE_COUNT, count)
    rig.log = []
    rig.go(FREE_UNIQUE)
    want = [] if mut("unique-link-skipped") else [("link-free", link)]
    want += [("unload", table)]
    require(rig.log == want, f"unique items free log {rig.log} != {want}")
    want_count = count if mut("unique-count-kept") else 0
    require(rig.read_words(UNIQUE_COUNT, 1)[0] == want_count, "unique items count after the free")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x611D20)
    rng = random.Random(0x6125A0)
    rig = Rig()
    cases = 0
    for _ in range(40):
        check_flag_value(rig, rng)
        check_compile_flag(rig, rng)
        check_books_count(rig, rng)
        check_rare_free(rig, rng)
        check_unique_free(rig, rng)
        cases += 5
    for entry, slot in UNLOAD_SLOTS.items():
        for _ in range(40):
            check_unload(rig, rng, entry, slot)
            cases += 1
    return cases


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "small DATATBLS helper cases")


if __name__ == "__main__":
    main()
