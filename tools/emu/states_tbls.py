# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify three States helpers in DataTbls.cpp (.\\DATATBLS\\DataTbls.cpp) in stock game.exe 1.14d.

    uv run python states_tbls.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

The States table is the row block at the pointer 0x96bcec, with the row count
at 0x96bcf4. Rows are 0x3c bytes, and a row's flag byte for bit b is at
row + 0x10 + (b >> 3). The bit masks gadwBitMasks[k] = 1 << k are at 0x6ce268.

* DATATBLS_BuildStatesDataBitMask (FUN_00611d60, fastcall struct, bit): for each
  state whose flag bit is set, ORs 1 << i into dword (i >> 5) of the struct.
* DATATBLS_BuildStatesDataList (FUN_00611dc0, fastcall list, counter; stack
  bit; ret 4): allocates 2 * count bytes (DataTbls.cpp line 0x64b), zeroes them,
  stores the state ids (shorts) whose flag bit is set, and stores the number of
  them as a short at the counter.
* TXT_States_AllocItemTypesStateDataTable (FUN_00611e60, fastcall list, counter;
  stack offset; ret 4): the same allocation (line 0x662); a state is listed when
  the short at row + offset is positive (signed).

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require

BIT_MASK, LIST_BITS, LIST_ITEMS = 0x61_1D60, 0x61_1DC0, 0x61_1E60
STATES_PTR, STATES_COUNT = 0x96_BCEC, 0x96_BCF4
ROW, FLAG_BASE = 0x3C, 0x10
FILE = ".\\DATATBLS\\DataTbls.cpp"
MUTATIONS = ("bitmask-row-stride", "bitmask-word-first", "bitmask-bit-minus-one", "list-line", "list-counter-kept",
             "items-threshold", "list-skip-last")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Rig(mw.World):
    """The emulated game, with one call helper that reports the return value."""

    def __init__(self):
        """Build the world and raise its instruction limit."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 200000

    def go(self, address, *args, ecx=0, edx=0, nstack=None):
        """One call; ("ret", eax)."""
        return ("ret", self.run(address, *args, ecx=ecx, edx=edx, nstack=nstack))


def make_rows(rig, rng, count):
    """A States row block of `count` random rows (0x3c bytes each), with the table pointer and count globals set.

    Returns (block address, the row bytes)."""
    size = ROW * max(count, 1) + 0x100
    block = rig.m.alloc(size)
    rows = bytearray(rng.randbytes(size))
    rig.write(block, bytes(rows))
    rig.m.w32(STATES_PTR, block)
    rig.m.w32(STATES_COUNT, count)
    return block, rows


def flag_set(rows, row, bit, stride=ROW):
    """Whether bit `bit` of row `row`'s flag bytes is set (the flag byte is at row * stride + 0x10 + bit >> 3)."""
    return bool(rows[stride * row + FLAG_BASE + (bit >> 3)] & (1 << (bit & 7)))


def check_bitmask(rig, rng):
    """FUN_00611d60: the struct gets 1 << i in word i >> 5 for each state i whose flag bit is set."""
    count = rng.randrange(1, 70)
    _block, rows = make_rows(rig, rng, count)
    bit = rng.randrange(0, 256)
    struct_block = rig.m.alloc(0x40)
    prefill = rng.randbytes(0x40)
    rig.write(struct_block, prefill)
    got = rig.go(BIT_MASK, ecx=struct_block, edx=bit)
    require(got[0] == "ret", f"BitMask {got}")
    words = list(struct.unpack("<16I", prefill))
    test_bit = bit - 1 if mut("bitmask-bit-minus-one") and bit else bit
    stride = ROW + 1 if mut("bitmask-row-stride") else ROW
    for i in range(count):
        if flag_set(rows, i, test_bit, stride):
            word = 0 if mut("bitmask-word-first") else i >> 5
            words[word] |= 1 << (i & 31)
    require(list(struct.unpack("<16I", rig.m.read(struct_block, 0x40))) == [w & 0xFFFFFFFF for w in words],
            "BitMask words differ from the model")


def check_list(rig, rng, entry):
    """FUN_00611dc0 (listed by flag bit) and FUN_00611e60 (listed by a positive short at the offset).

    The counter is a short: its slot is prefilled with a dword of garbage, and all four bytes are compared."""
    count = rng.randrange(0, 40)
    block, rows = make_rows(rig, rng, count)
    pointer_slot, counter_slot = rig.m.alloc(0x10), rig.m.alloc(0x10)
    rig.m.w32(counter_slot, 0xDEADBEEF)
    if entry == LIST_BITS:
        arg = rng.randrange(0, 256)
        listed = [i for i in range(count) if flag_set(rows, i, arg)]
        line = 0x64B
    else:
        arg = rng.choice((0, 2, 0x20, 0x34))
        for i in range(count):
            value = rng.choice((0, -1, 1, 0x7FFF, rng.randrange(0x10000)))
            struct.pack_into("<H", rows, arg + ROW * i, value & 0xFFFF)
        rig.write(block, bytes(rows))
        listed = [i for i in range(count) if struct.unpack_from("<h", rows, arg + ROW * i)[0] > 0]
        line = 0x662
    if mut("items-threshold") and entry == LIST_ITEMS:
        listed = list(range(count))
    if mut("list-skip-last") and listed:
        listed = listed[:-1]
    rig.log = []
    got = rig.go(entry, arg, ecx=pointer_slot, edx=counter_slot, nstack=1)
    require(got[0] == "ret", f"list {got}")
    block = rig.read_words(pointer_slot, 1)[0]
    used_line = line if not mut("list-line") else line + 1
    require(rig.log == [("alloc", 0, count * 2, FILE, used_line, 0)], f"list alloc log {rig.log}")
    expected = bytearray(count * 2)
    for k, i in enumerate(listed):
        struct.pack_into("<h", expected, 2 * k, i)
    require(bytes(rig.m.read(block, count * 2)) == bytes(expected), "list ids differ from the model")
    want = len(listed) if not mut("list-counter-kept") else 0
    counter_dword = struct.unpack("<I", rig.m.read(counter_slot, 4))[0]
    require(counter_dword == (0xDEAD0000 | (want & 0xFFFF)), f"list counter dword {counter_dword:#x} != {want:#x}")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x611D60)
    rng = random.Random(0x611DC0)
    rig = Rig()
    plan = [(lambda r, g: check_bitmask(r, g), 200), (lambda r, g: check_list(r, g, LIST_BITS), 200),
            (lambda r, g: check_list(r, g, LIST_ITEMS), 200)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "States helper cases")


if __name__ == "__main__":
    main()
