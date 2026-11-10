# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 14 small ItemTbls.cpp helpers (.\\DATATBLS\\ItemTbls.cpp) in stock game.exe 1.14d.

    uv run python item_tbls.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random tables and
compares the return value, the tables after the call, the ordered log of the
hooked callees (the table free 0x61_25c0, the link free 0x6b_d0b0, the pool
release 0x40_b480) and the bytes `ret n` popped. The data-tables globals are
written directly (no MPQ is read); the rig is monster_tbls.py's.

* FUN_00633590 / 00634250 / 00636b10 / 006375b0 / 006374a0 / 00639cb0: the
  address of a table's row count (items `0x96ca58`, rare affixes `0x96caa0`,
  qualityitems `0x96cc50`, lowqualityitems `0x96cc58`, books `0x96cc60`, runes
  `0x96ca98`).
* FUN_006335a0 (n): the items index word n (2 bytes a row); -1 for n < 0 or
  n >= count; no index halts with line 0x1e2.
* FUN_00636b20 (n): qualityitems row n (0x70 bytes); 0 for n >= count or
  n == -1; no table halts 0x4f2, an address that wrapped to zero halts 0x4f3.
* FUN_006375c0 (n): lowqualityitems row n (0x22 bytes); the same, 0x5d0 / 0x5d1.
* FUN_006379d0 (a, b): the qsort compare of two rows on the signed word at
  `+2`: -1, 0 or 1.
* FUN_00638cd0 (ECX a, EDX b): is ItemTypes row a one of b (rows of 0xe4
  bytes, equiv words `+4` / `+6`; the walk returns 0 when more than 0x7c entries remain after a pop). Unlike
  the MonType walk (`FUN_00658460`) it answers 1 for b < 1.
* FUN_00639c90: frees the runes table (`0x96ca9c`) and its link (`0x96bce0`).
* FUN_00637490: frees the books table (`0x96cc64`).
* FUN_00638c80: releases gamble's sorted array (line 0x80d) and clears both
  `0x96cab4` and `0x96cab0`; nothing to do when the array is absent.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require
from monster_tbls import MASK, STEP_LIMIT, Rig, s32

(ITEMS_COUNT_ADDR, ITEM_INDEX_WORD, RARE_COUNT_ADDR, QUALITY_COUNT_ADDR, QUALITY_ROW, LOWQ_COUNT_ADDR, LOWQ_ROW,
 BOOKS_COUNT_ADDR, RUNES_COUNT_ADDR, ROW_COMPARE, ITEMTYPE_ISA, RUNES_FREE, BOOKS_FREE, GAMBLE_FREE) = (
    0x633590, 0x6335A0, 0x634250, 0x636B10, 0x636B20, 0x6375B0, 0x6375C0, 0x6374A0, 0x639CB0, 0x6379D0,
    0x638CD0, 0x639C90, 0x637490, 0x638C80)
GETTERS = {ITEMS_COUNT_ADDR: 0x96CA58, RARE_COUNT_ADDR: 0x96CAA0, QUALITY_COUNT_ADDR: 0x96CC50,
           LOWQ_COUNT_ADDR: 0x96CC58, BOOKS_COUNT_ADDR: 0x96CC60, RUNES_COUNT_ADDR: 0x96CA98}
ITEMS_COUNT, ITEMS_INDEX = 0x96CA58, 0x96CA78
QUALITY = (0x96CC50, 0x96CC54, 0x70, 0x4F2, 0x4F3)
LOWQ = (0x96CC58, 0x96CC5C, 0x22, 0x5D0, 0x5D1)
ITEMTYPES_BASE, ITEMTYPES_COUNT = 0x96C828, 0x96C82C
RUNES_TABLE, RUNES_LINK, BOOKS_TABLE = 0x96CA9C, 0x96BCE0, 0x96CC64
GAMBLE_ARRAY, GAMBLE_COUNT = 0x96CAB4, 0x96CAB0
FILE = ".\\DATATBLS\\ItemTbls.cpp"
MUTATIONS = ("getter-wrong-table", "index-range-le", "index-negative", "quality-minus-one", "quality-stride",
             "lowq-wrap", "compare-unsigned", "compare-sign", "isa-low-zero", "isa-equiv2", "isa-depth",
             "runes-link-first", "gamble-keeps-count", "isa-neg-skipped")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def check_getter(rig, rng, address):
    """A row-count address getter returns the global's address, 0 arguments."""
    want = GETTERS[address] + (4 if mut("getter-wrong-table") else 0)
    require(rig.go(address) == ("ret", want) and not rig.log, f"{address:#x}")


def check_index_word(rig, rng):
    """FUN_006335a0: the items index word n."""
    machine = rig.m
    count = rng.choice((0, 1, 5, 12))
    absent = rng.random() < 0.2
    words = [rng.randrange(1 << 16) for _ in range(count + 10)]      # eight words of padding before row 0
    machine.w32(ITEMS_COUNT, count)
    machine.w32(ITEMS_INDEX, 0 if absent else rig.array(struct.pack(f"<{len(words)}H", *words)) + 16)
    n = rng.choice((-1, 0, count - 1, count, count + 1, rng.randrange(-4, 3 + count), MASK))
    value = s32(n & MASK)
    inside = (value < count or (mut("index-range-le") and value == count)) and (-1 < value or mut("index-negative"))
    if not inside:
        want = ("ret", MASK)
    elif absent:
        want = ("halt", 0x1E2)
    else:
        want = ("ret", words[value + 8])
    got = rig.go(ITEM_INDEX_WORD, n & MASK)
    require(got == want and not rig.log, f"index {value} of {count}: {got} != {want}")


def check_row(rig, rng, address, spec):
    """FUN_00636b20 / FUN_006375c0: row n of a table whose count and pointer are globals."""
    machine = rig.m
    count_at, pointer_at, stride, line_absent, line_wrapped = spec
    if mut("quality-stride") and address == QUALITY_ROW:
        stride += 1
    count = rng.choice((0, 1, 6, -1))
    absent = rng.random() < 0.2
    wrap = rng.random() < 0.15
    n = rng.choice((-1, 0, 1, count - 1, count, count + 3, -5, rng.randrange(-6, 9)))
    value = s32(n & MASK)
    pointer = 0 if absent else (((1 << 32) - spec[2] * value) & MASK if wrap else rng.randrange(0x1000, 0x7000_0000))
    machine.w32(count_at, count & MASK)
    machine.w32(pointer_at, pointer)
    got = rig.go(address, n & MASK)
    inside = value < count and (n & MASK) != MASK
    if mut("quality-minus-one") and address == QUALITY_ROW:
        inside = value < count
    if not inside:
        want = ("ret", 0)
    elif pointer == 0:
        want = ("halt", line_absent)
    else:
        total = (pointer + stride * value) & MASK
        want = ("ret", total) if total != 0 or (mut("lowq-wrap") and address == LOWQ_ROW) else ("halt", line_wrapped)
    require(got == want and not rig.log, f"{address:#x} row {value} of {count} at {pointer:#x}: {got} != {want}")


def check_compare(rig, rng):
    """FUN_006379d0: compare the signed words at +2 of two rows."""
    machine = rig.m
    first, second = machine.alloc(16), machine.alloc(16)
    a = rng.choice((0, 1, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1 << 16)))
    b = rng.choice((a, 0, 1, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1 << 16)))
    rig.write(first, rng.randbytes(2) + struct.pack("<H", a) + rng.randbytes(12))
    rig.write(second, rng.randbytes(2) + struct.pack("<H", b) + rng.randbytes(12))
    sa, sb = (a, b) if mut("compare-unsigned") else (struct.unpack("<h", struct.pack("<H", a))[0],
                                                      struct.unpack("<h", struct.pack("<H", b))[0])
    want = MASK if sa < sb else int(sa != sb)
    if mut("compare-sign") and sa > sb:
        want = MASK
    got = rig.run(ROW_COMPARE, first, second, nstack=0)
    require(got == want and not rig.log, f"compare {a:#x} {b:#x}: {got:#x} != {want:#x}")


def check_isa(rig, rng):
    """FUN_00638cd0: is ItemTypes row a one of b."""
    count = rng.choice((1, 2, 6, 14, 50))
    rows = [[rng.randrange(1 << 8), rng.choice((0, 0, rng.randrange(1, count + 3), -1)),
             rng.choice((0, rng.randrange(1, count + 3), rng.randrange(1, count + 3), -3))] for _ in range(count)]
    deep = rng.random() < 0.1
    cycle = False
    if deep:
        count = 130
        rows = [[0, i + 1 if i + 1 < count else 0, i + 1 if i + 1 < count else 0] for i in range(count)]
    elif rng.random() < 0.06:
        count = 6       # rows 1 and 2 name each other and nothing else: row 5 is never reached
        rows = [[0, 0, 0] for _ in range(count)]
        rows[1][1], rows[2][1] = 2, 1
        cycle = True
    elif rng.random() < 0.25:
        for i in range(1, count):
            rows[i][1] = i + 1 if i + 1 < count else 0
    pad = 8         # rows before row 0, reached by a negative equivalent; they name real rows (no negative words)
    padding = [[0, 0, 0] for _ in range(pad)]
    negpath = None
    if not (deep or cycle):
        padding = [[rng.randrange(1 << 8), rng.choice((0, rng.randrange(1, count + 3))), rng.choice((0, rng.randrange(1, count + 3)))]
                   for _ in range(pad)]
        if rng.random() < 0.3 and count > 2:    # a -> (negative equivalent) -> padding row -> target
            a_row, back = rng.randrange(1, count), rng.randrange(1, 4)
            target = rng.choice([i for i in range(1, count) if i != a_row])
            rows[a_row][1], rows[a_row][2] = rng.randrange(1, count), -back
            padding[pad - back][1:] = [target, 0]
            negpath = (a_row, target)
    image = b"".join(struct.pack("<HHhh", r[0], 0, r[1], r[2]) + rng.randbytes(0xE4 - 8) for r in padding + rows)
    table = rig.array(image)
    rig.m.w32(ITEMTYPES_BASE, table + 0xE4 * pad)
    rig.m.w32(ITEMTYPES_COUNT, count)
    a = rng.choice((0, -1, count, count + 1, rng.randrange(0, count + 2)))
    b = rng.choice((0, -1, rng.randrange(-1, count + 2), a))
    if negpath:
        a, b = negpath
    elif cycle:
        a, b = 1, 5
    elif deep:
        a, b = 1, rng.choice((60, 124, 125, 126, 127))      # one stack entry a level here: depth 0x7c is level 125
    elif 0 < a < count and rng.random() < 0.5:
        b = rng.choice(rows[a][1:])        # an equivalent of a: the walk must find it

    def model(a, b):
        """The stack walk of FUN_00638cd0: pop, compare, push up to two equivalents."""
        if b < 1:
            return 0 if mut("isa-low-zero") else 1
        if a < 1 or a >= count:
            return 0
        stack = [a]
        for _ in range(STEP_LIMIT):
            if not stack:
                return 0
            cur = stack.pop()
            if b == cur:
                return 1
            if cur >= count or len(stack) > (0x7B if mut("isa-depth") else 0x7C):
                return 0
            first, second = struct.unpack_from("<hh", image, 0xE4 * (cur + pad) + 4)
            if first > 0:
                stack.append(first)
                if second != 0 and not mut("isa-equiv2") and not (mut("isa-neg-skipped") and second < 0):
                    stack.append(second)
        return "loop"       # a cycle of single links: the original has no visited set and never returns
    want = model(a, b)
    if want == "loop":
        require(rig.go(ITEMTYPE_ISA, ecx=a & MASK, edx=b & MASK) == ("loop", 0), f"isa({a}, {b}) should not return")
        return
    require(rig.go(ITEMTYPE_ISA, ecx=a & MASK, edx=b & MASK) == ("ret", want) and not rig.log, f"isa({a}, {b}) over {count} rows")


def check_frees(rig, rng):
    """FUN_00639c90, FUN_00637490 and FUN_00638c80: the releases."""
    machine = rig.m
    runes, link, books = (rng.randrange(1 << 32) for _ in range(3))
    machine.w32(RUNES_TABLE, runes)
    machine.w32(RUNES_LINK, link)
    machine.w32(BOOKS_TABLE, books)
    first, second = (("link-free", link), ("table-free", runes)) if mut("runes-link-first") else (
        ("table-free", runes), ("link-free", link))
    require(rig.go(RUNES_FREE)[0] == "ret" and rig.log == [first, second], f"runes free {rig.log}")
    require(rig.go(BOOKS_FREE)[0] == "ret" and rig.log == [("table-free", books)], f"books free {rig.log}")
    array, count = rng.choice((0, rng.randrange(1, 1 << 30))), rng.randrange(1 << 32)
    machine.w32(GAMBLE_ARRAY, array)
    machine.w32(GAMBLE_COUNT, count)
    got = rig.go(GAMBLE_FREE)
    want_log = [("free", 0, array, FILE, 0x80D, 0)] if array else []
    require(got[0] == "ret" and rig.log == want_log, f"gamble free {rig.log} != {want_log}")
    want_count = count if (not array or mut("gamble-keeps-count")) else 0
    require((machine.r32(GAMBLE_ARRAY), machine.r32(GAMBLE_COUNT)) == (0 if array else array, want_count),
            "gamble globals after the release")


def run_all():
    """Run every check on random tables; returns the case count."""
    rng = random.Random(0x6335A0)
    rig = Rig()
    plan = [(lambda r, g, a=a: check_getter(r, g, a), 3) for a in GETTERS]
    plan += [(check_index_word, 300), (lambda r, g: check_row(r, g, QUALITY_ROW, QUALITY), 300),
             (lambda r, g: check_row(r, g, LOWQ_ROW, LOWQ), 300), (check_compare, 300), (check_isa, 800),
             (check_frees, 60)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "ItemTbls helper cases")


if __name__ == "__main__":
    main()
