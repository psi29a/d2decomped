# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 23 small ItemTbls.cpp helpers (.\\DATATBLS\\ItemTbls.cpp) in stock game.exe 1.14d.

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
* FUN_00633540: frees the items link, then non-null rows and index at lines
  0x1c8 and 0x1cb; clears only the rows pointer.
* FUN_00633ea0: frees a non-null magic affix array at line 0x2b7 and clears
  its pointer; an absent array skips the free.
* FUN_00638aa0: frees properties rows and link in that order, including null
  arguments; neither global is cleared.
* FUN_006385d0: frees present itemstatcost rows, link and sorted index in that
  order; clears rows, link, count and sorted count, leaving the sorted pointer.
* FUN_006393e0: unconditionally frees the ItemTypes matrix at line 0x8a7,
  followed by the table and its link, leaving all three globals unchanged.
* FUN_00634260: rare affix row n is 1-based and has stride 0x48; returns
  null outside 1..count and halts at line 0x31f without the table.
* FUN_006372c0: gem row n has stride 0xc0; -1 is excluded explicitly,
  while other negative n below count are accepted. A missing table halts
  at line 0x563 and a wrapped null address at 0x564.
* FUN_006336a0: a numeric column uses signed decimal conversion; an empty
  value writes zero, while a null destination writes nothing. A name searches
  the skills, montype and states links in that order, accepting the first
  nonnegative result.
* FUN_00639cc0: grows the runes array for row zero to 0x120 bytes, or 0x124
  with a leading count dword in bin mode; copies the entire row and sets count 1.
  Appending a later row preserves row zero, zeroes skipped rows, copies the
  requested row and sets count to n + 1 (the realloc copy is a hook model).
* Repeating FUN_00633540 / 006385d0 / 006393e0 calls the release helpers
  again for globals those functions left nonzero.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import re
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
ITEMS_LINK, ITEMS_ROWS = 0x96BCC4, 0x96CA5C
MAGIC_ARRAY, PROPERTIES_ROWS, PROPERTIES_LINK = 0x96CA80, 0x96BCD4, 0x96BCD8
ITEMS_FREE, MAGIC_FREE, PROPERTIES_FREE = 0x633540, 0x633EA0, 0x638AA0
ITEMSTAT_FREE, ITEMTYPE_FREE = 0x6385D0, 0x6393E0
ITEMSTAT_ROWS, ITEMSTAT_LINK, ITEMSTAT_COUNT = 0x96C7FC, 0x96C800, 0x96C804
ITEMSTAT_SORTED, ITEMSTAT_SORTED_COUNT = 0x96C808, 0x96C80C
ITEMTYPE_LINK, ITEMTYPE_MATRIX = 0x96C824, 0x96C834
RARE_ROW, GEM_ROW = 0x634260, 0x6372C0
RARE_COUNT, RARE_TABLE = 0x96CAA0, 0x96CAA4
GEM_COUNT, GEM_TABLE = 0x96CA90, 0x96CA94
COLUMN_VALUE = 0x6336A0
COLUMN_LINKS = (0x96C7CC, 0x96C868, 0x96BCF0)
RUNE_APPEND, RUNE_COUNT, RUNE_TABLE, BIN_FLAG = 0x639CC0, 0x96CA98, 0x96CA9C, 0x744308
FILE = ".\\DATATBLS\\ItemTbls.cpp"
MUTATIONS = ("getter-wrong-table", "index-range-le", "index-negative", "quality-minus-one", "quality-stride",
             "lowq-wrap", "compare-unsigned", "compare-sign", "isa-low-zero", "isa-equiv2", "isa-depth",
             "runes-link-first", "gamble-keeps-count", "isa-neg-skipped",
             "items-free-order", "items-keeps-rows", "magic-free-null", "properties-clears-link",
             "itemstat-keeps-count", "itemtype-skip-matrix", "rare-zero-based", "gem-reject-all-negatives",
             "column-empty-keeps", "column-negative-positive", "column-link-order", "rune-bin-header",
             "rune-gap-count")
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


def check_cited_frees(rig, rng):
    """Check five cited ItemTbls unloaders against ordered calls and globals."""
    machine = rig.m
    link, rows, index = (rng.choice((0, rng.randrange(1, 1 << 30))) for _ in range(3))
    machine.w32(ITEMS_LINK, link)
    machine.w32(ITEMS_ROWS, rows)
    machine.w32(ITEMS_INDEX, index)
    got = rig.go(ITEMS_FREE)
    want = [("link-free", link)]
    if rows:
        want.append(("free", 0, rows, FILE, 0x1C8, 0))
    if index:
        want.append(("free", 0, index, FILE, 0x1CB, 0))
    if mut("items-free-order"):
        want.reverse()
    require(got[0] == "ret" and rig.log == want, f"items free: {rig.log} != {want}")
    require(machine.r32(ITEMS_ROWS) == (rows if mut("items-keeps-rows") else 0)
            and machine.r32(ITEMS_INDEX) == index, "items globals after free")

    magic = rng.choice((0, rng.randrange(1, 1 << 30)))
    machine.w32(MAGIC_ARRAY, magic)
    got = rig.go(MAGIC_FREE)
    want = [("free", 0, magic, FILE, 0x2B7, 0)] if magic or mut("magic-free-null") else []
    require(got[0] == "ret" and rig.log == want and machine.r32(MAGIC_ARRAY) == 0,
            f"magic free: {rig.log} != {want}")

    properties, prop_link = (rng.choice((0, rng.randrange(1, 1 << 30))) for _ in range(2))
    machine.w32(PROPERTIES_ROWS, properties)
    machine.w32(PROPERTIES_LINK, prop_link)
    got = rig.go(PROPERTIES_FREE)
    require(got[0] == "ret" and rig.log == [("table-free", properties), ("link-free", prop_link)],
            f"properties free: {rig.log}")
    require(machine.r32(PROPERTIES_ROWS) == properties and
            machine.r32(PROPERTIES_LINK) == (0 if mut("properties-clears-link") else prop_link),
            "properties globals after free")

    rows, link, sorted_rows = (rng.choice((0, rng.randrange(1, 1 << 30))) for _ in range(3))
    machine.w32(ITEMSTAT_ROWS, rows)
    machine.w32(ITEMSTAT_LINK, link)
    machine.w32(ITEMSTAT_COUNT, rng.randrange(1 << 20))
    machine.w32(ITEMSTAT_SORTED, sorted_rows)
    machine.w32(ITEMSTAT_SORTED_COUNT, rng.randrange(1 << 20))
    got = rig.go(ITEMSTAT_FREE)
    want = []
    if rows:
        want.append(("table-free", rows))
    if link:
        want.append(("link-free", link))
    if sorted_rows:
        want.append(("free", 0, sorted_rows, FILE, 0x763, 0))
    require(got[0] == "ret" and rig.log == want, f"itemstat free: {rig.log} != {want}")
    require((machine.r32(ITEMSTAT_ROWS), machine.r32(ITEMSTAT_LINK), machine.r32(ITEMSTAT_COUNT),
             machine.r32(ITEMSTAT_SORTED), machine.r32(ITEMSTAT_SORTED_COUNT)) ==
            (0, 0, 1 if mut("itemstat-keeps-count") else 0, sorted_rows, 0),
            "itemstat globals after free")

    matrix, types, type_link = (rng.choice((0, rng.randrange(1, 1 << 30))) for _ in range(3))
    machine.w32(ITEMTYPE_MATRIX, matrix)
    machine.w32(ITEMTYPES_BASE, types)
    machine.w32(ITEMTYPE_LINK, type_link)
    got = rig.go(ITEMTYPE_FREE)
    want = [("free", 0, matrix, FILE, 0x8A7, 0), ("table-free", types), ("link-free", type_link)]
    if mut("itemtype-skip-matrix"):
        want.pop(0)
    require(got[0] == "ret" and rig.log == want, f"itemtype free: {rig.log} != {want}")
    require((machine.r32(ITEMTYPE_MATRIX), machine.r32(ITEMTYPES_BASE), machine.r32(ITEMTYPE_LINK)) ==
            (matrix, types, type_link), "itemtype globals after free")


def check_cited_rows(rig, rng):
    """Check rare affix and gem row addresses, including invalid indices."""
    machine = rig.m
    count = rng.choice((0, 1, 3, 8))
    index = rng.choice((-3, -2, -1, 0, 1, count, count + 1, rng.randrange(-3, count + 3)))
    table = rng.choice((0, rng.randrange(0x100000, 0x70000000)))
    machine.w32(RARE_COUNT, count)
    machine.w32(RARE_TABLE, table)
    got = rig.go(RARE_ROW, index & MASK)
    inside = (0 <= index <= count) if mut("rare-zero-based") else (0 < index <= count)
    want = ("ret", (table + 0x48 * (index - 1)) & MASK) if inside and table else (
        ("halt", 0x31F) if inside else ("ret", 0))
    require(got == want and not rig.log, f"rare row {index}/{count}: {got} != {want}")

    machine.w32(GEM_COUNT, count)
    machine.w32(GEM_TABLE, table)
    got = rig.go(GEM_ROW, index & MASK)
    inside = index < count and index != -1 and (index >= 0 or not mut("gem-reject-all-negatives"))
    want = ("ret", (table + 0xC0 * index) & MASK) if inside and table else (
        ("halt", 0x563) if inside else ("ret", 0))
    require(got == want and not rig.log, f"gem row {index}/{count}: {got} != {want}")


def check_column_value(rig, rng, raw=None):
    """Check the numeric and empty branches of the affix column callback."""
    machine = rig.m
    for address in COLUMN_LINKS:
        machine.w32(address, 0)
    if raw is None:
        raw = rng.choice((b"", b"0", b"25", b"-18", b"-1", b"9876", b"?",
                          b"25tail", b"-18x", b"-foo", b"+7", b" 8"))
    text_ptr = rig.array(raw + b"\0")
    record = machine.alloc(32)
    offset = rng.choice((0, 4, 12, 28))
    machine.w32(record + offset, 0xA5A5A5A5)
    destination = rng.choice((0, record))
    got = rig.go(COLUMN_VALUE, offset, 0, 0, 0, ecx=text_ptr, edx=destination)
    numeric_prefix = re.match(rb"-?[0-9]+", raw)
    number = int(numeric_prefix.group(0)) if numeric_prefix else 0
    if mut("column-negative-positive") and number < 0:
        number = -number
    want = 0xA5A5A5A5 if not destination or (mut("column-empty-keeps") and not raw) else number & MASK
    require(got[0] == "ret" and machine.r32(record + offset) == want and not rig.log,
            f"column {raw!r}: {machine.r32(record + offset):#x} != {want:#x}")

    text_ptr = rig.array(b"known_name\0")
    links = [rng.choice((0, rng.randrange(1, 1 << 30))) for _ in COLUMN_LINKS]
    for address, link in zip(COLUMN_LINKS, links):
        machine.w32(address, link)
    rig.link_value = rng.choice((-1, 0, 7, 99))
    machine.w32(record + offset, 0xA5A5A5A5)
    got = rig.go(COLUMN_VALUE, offset, 0, 0, 0, ecx=text_ptr, edx=record)
    searched = [link for link in links if link]
    if rig.link_value >= 0:
        searched = searched[:1]
    if mut("column-link-order"):
        searched.reverse()
    want_log = [("link-find", link, text_ptr, 0) for link in searched]
    want = rig.link_value & MASK if searched and rig.link_value >= 0 else 0
    require(got[0] == "ret" and rig.log == want_log and machine.r32(record + offset) == want,
            f"column link search: {rig.log} != {want_log}; value {machine.r32(record + offset):#x} != {want:#x}")


def check_column_cascade(rig, rng):
    """Check that a failed skills/monster link falls through to the next link."""
    text_ptr = rig.array(b"known_name\0")
    record = rig.m.alloc(20)
    links = (0x1234, 0x2345, 0x3456)
    for slot, link in zip(COLUMN_LINKS, links):
        rig.m.w32(slot, link)
    scenarios = (((-1, -1, 7), 7, links),
                 ((-1, 2, 99), 2, links[:2]),
                 ((0, 2, 99), 0, links[:1]),
                 ((-1, -1, -1), 0, links))
    for answers, expected_value, visited in scenarios:
        remaining = list(answers)

        def find(current, remaining=remaining):
            """Return a different signed result from each successive link."""
            rig.log.append(("link-find", current.arg(0), current.arg(1), current.arg(2)))
            return remaining.pop(0) & MASK

        rig.m.hook(0x6BD3C0, find, 3)
        rig.m.w32(record + 12, 0xA5A5A5A5)
        got = rig.go(COLUMN_VALUE, 12, 0, 0, 0, ecx=text_ptr, edx=record)
        wanted_log = [("link-find", link, text_ptr, 0) for link in visited]
        require(got[0] == "ret" and rig.log == wanted_log and
                rig.m.r32(record + 12) == expected_value and len(remaining) == len(links) - len(visited),
                f"column link cascade {answers}: {got}, {rig.log}, {rig.m.r32(record + 12):#x}")
    rig.logger(0x6BD3C0, 3, "link-find", ret=lambda _current: rig.link_value & MASK, regs=())


def check_rune_append(rig, rng):
    """Check the first runes row and the optional binary header."""
    machine = rig.m
    record = rng.randbytes(0x120)
    source = rig.array(record)
    binary = rng.choice((0, 1))
    machine.w32(RUNE_COUNT, 0)
    machine.w32(RUNE_TABLE, 0)
    machine.w32(BIN_FLAG, binary)
    got = rig.go(RUNE_APPEND, 0, source)
    require(got[0] == "ret" and machine.r32(RUNE_COUNT) == 1, "rune append count")
    table = machine.r32(RUNE_TABLE)
    allocation = table - (4 if binary else 0)
    require(bytes(machine.read(table, 0x120)) == record, "rune append row bytes")
    if binary and not mut("rune-bin-header"):
        require(machine.r32(allocation) == 1, "rune binary row-count header")
    elif binary:
        require(machine.r32(allocation) == 2, "rune binary row-count header")
    require(rig.log == [("grow", 0, MASK - 3 if binary else 0, 0x124 if binary else 0x120, FILE, 0x95B, 0)],
            f"rune grow call: {rig.log}")


def check_rune_gap(rig, rng):
    """Check growth to a later row, preserving the old row and clearing gaps."""
    machine = rig.m
    binary = rng.choice((0, 1))
    row_index = rng.randrange(2, 6)
    old_row, new_row = rng.randbytes(0x120), rng.randbytes(0x120)
    initial = (struct.pack("<I", 1) if binary else b"") + old_row
    old_base = rig.array(initial)
    source = rig.array(new_row)
    machine.w32(RUNE_COUNT, 1)
    machine.w32(RUNE_TABLE, old_base + (4 if binary else 0))
    machine.w32(BIN_FLAG, binary)
    got = rig.go(RUNE_APPEND, row_index, source)
    new_table = machine.r32(RUNE_TABLE)
    require(got[0] == "ret" and machine.r32(RUNE_COUNT) ==
            (row_index if mut("rune-gap-count") else row_index + 1), "rune gap count")
    require(bytes(machine.read(new_table, 0x120)) == old_row, "rune previous row")
    require(bytes(machine.read(new_table + 0x120, (row_index - 1) * 0x120)) ==
            bytes((row_index - 1) * 0x120), "rune skipped rows")
    require(bytes(machine.read(new_table + row_index * 0x120, 0x120)) == new_row,
            "rune appended row")
    if binary:
        require(machine.r32(new_table - 4) == row_index + 1, "rune binary count")
    size = (row_index * 9 + 9) * 0x20 + (4 if binary else 0)
    require(rig.log == [("grow", 0, old_base, size, FILE, 0x95B, 0)],
            f"rune gap grow: {rig.log}")


def check_rune_replace(rig, rng):
    """Replace a row below the current count without growth or count change."""
    machine = rig.m
    binary = rng.choice((0, 1))
    count = rng.randrange(2, 6)
    row_index = rng.randrange(count)
    old_rows = [rng.randbytes(0x120) for _ in range(count)]
    new_row = rng.randbytes(0x120)
    initial = (struct.pack("<I", count) if binary else b"") + b"".join(old_rows)
    old_base = rig.array(initial)
    source = rig.array(new_row)
    machine.w32(RUNE_COUNT, count)
    machine.w32(RUNE_TABLE, old_base + (4 if binary else 0))
    machine.w32(BIN_FLAG, binary)
    got = rig.go(RUNE_APPEND, row_index, source)
    expected = old_rows[:]
    expected[row_index] = new_row
    require(got[0] == "ret" and machine.r32(RUNE_COUNT) == count and not rig.log,
            f"rune replacement {row_index} of {count}: {got}, {rig.log}")
    require(machine.r32(RUNE_TABLE) == old_base + (4 if binary else 0) and
            bytes(machine.read(machine.r32(RUNE_TABLE), count * 0x120)) == b"".join(expected),
            "rune replacement changed another row or table pointer")
    if binary:
        require(machine.r32(old_base) == count, "rune replacement changed binary header")


def check_repeated_frees(rig, rng):
    """Check the release calls a second unload makes with dangling globals."""
    machine = rig.m
    link, rows, index = (rng.randrange(1, 1 << 30) for _ in range(3))
    machine.w32(ITEMS_LINK, link)
    machine.w32(ITEMS_ROWS, rows)
    machine.w32(ITEMS_INDEX, index)
    require(rig.go(ITEMS_FREE)[0] == "ret", "first items free")
    require(rig.go(ITEMS_FREE)[0] == "ret" and rig.log ==
            [("link-free", link), ("free", 0, index, FILE, 0x1CB, 0)],
            f"second items free: {rig.log}")

    sorted_rows = rng.randrange(1, 1 << 30)
    machine.w32(ITEMSTAT_ROWS, rng.randrange(1, 1 << 30))
    machine.w32(ITEMSTAT_LINK, rng.randrange(1, 1 << 30))
    machine.w32(ITEMSTAT_SORTED, sorted_rows)
    require(rig.go(ITEMSTAT_FREE)[0] == "ret", "first itemstat free")
    require(rig.go(ITEMSTAT_FREE)[0] == "ret" and rig.log ==
            [("free", 0, sorted_rows, FILE, 0x763, 0)],
            f"second itemstat free: {rig.log}")

    matrix, types, type_link = (rng.randrange(1, 1 << 30) for _ in range(3))
    machine.w32(ITEMTYPE_MATRIX, matrix)
    machine.w32(ITEMTYPES_BASE, types)
    machine.w32(ITEMTYPE_LINK, type_link)
    require(rig.go(ITEMTYPE_FREE)[0] == "ret", "first itemtype free")
    want = [("free", 0, matrix, FILE, 0x8A7, 0), ("table-free", types), ("link-free", type_link)]
    require(rig.go(ITEMTYPE_FREE)[0] == "ret" and rig.log == want,
            f"second itemtype free: {rig.log} != {want}")


def run_all():
    """Run every check on random tables; returns the case count."""
    rng = random.Random(0x6335A0)
    rig = Rig()
    plan = [(lambda r, g, a=a: check_getter(r, g, a), 3) for a in GETTERS]
    plan += [(check_index_word, 300), (lambda r, g: check_row(r, g, QUALITY_ROW, QUALITY), 300),
             (lambda r, g: check_row(r, g, LOWQ_ROW, LOWQ), 300), (check_compare, 300), (check_isa, 800),
             (check_frees, 60), (check_cited_frees, 60), (check_cited_rows, 300), (check_column_value, 100),
             (check_column_cascade, 1), (check_rune_append, 60), (check_rune_gap, 60),
             (check_rune_replace, 60), (check_repeated_frees, 40)]
    plan += [(lambda r, g, raw=raw: check_column_value(r, g, raw), 1)
             for raw in (b"25tail", b"-18x", b"-foo", b"+7", b" 8")]
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
