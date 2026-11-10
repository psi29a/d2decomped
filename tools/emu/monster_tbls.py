# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 21 small MonsterTbls.cpp helpers (.\\DATATBLS\\MonsterTbls.cpp) in stock game.exe 1.14d.

    uv run python monster_tbls.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random tables and
compares the return value, the registers' contract, the bytes of the tables
after the call, the ordered log of callee calls and the bytes `ret n` popped.
The data-tables globals are written directly (no MPQ is read). Callees are
hooked and logged: the growing allocator 0x40_b4b0 (modelled as a realloc that
copies the old block), the excel name link 0x6b_d500 / 0x6b_d3c0, the table
free 0x61_25c0, the link free 0x6b_d0b0, the seeded random 0x45_c3e0 and the
string lookup 0x52_4a30. The C runtime's `memset` (0x68_1ef0), `strchr`
(0x68_7740) and `atol` (0x68_1ebb) run for real.

* FUN_00653d60 (index, word): MonStats `+0x32` of row index (0x1a8 bytes);
  out of range is ignored, no table halts with line 0x2ef.
* FUN_00653ed0 / 00653f10 / 00653f50 (ECX flag): a random unique title /
  prefix / suffix string: with the flag set and the count above zero, a random
  index (ECX = flag + 0x20, EDX = count), the 16-bit entry, then its string;
  a zero entry, a missing string or no flag gives the empty string `0x96d4b4`.
* FUN_00653f90 (ECX name): adds a treasure class: the name link `0x6b_d500`,
  the array grown by 16 records of 0x2c bytes (line 0x3a2) when the count is a
  multiple of 16, the record zeroed, the count bumped; returns the record.
* FUN_00654010 (ECX class, EDX n): reallocates the item list to n * 0x1c bytes
  (line 0x3ba), zeroes the new tail, sets the count (`+4`) and array (`+0x28`).
* FUN_00654080 (ECX class): one more item, filled with the class's running
  totals `+8` and `+0xc`; returns it.
* FUN_006540c0 (ECX class, EDX item; stack chance, flag): changes an item's
  chance (the span to the next item's `+4`, or to the class total `+0xc` for
  the last item), moving every later item's `+4` and the total; with the flag
  zero the same for the other running value (`+0` of items, total `+8`).
* FUN_00654410 (AL separator, ESI cursor): splits the next number off `*ESI`
  at AL (`strchr`), writes 0 there, advances the cursor, returns `atol`; a
  missing separator sets the cursor to null.
* FUN_00654dd0 (name): a treasure class by name through `0x6b_d3c0`, else 0.
* FUN_00654fc0 / 00654fe0 (n): free / row n of monitempercent (4 bytes a row);
  n >= count or n == -1 gives 0, no table halts 0x6dc, a wrapped address 0x6dd.
* FUN_00655710: the superunique count. FUN_00656390 / 00656440: the next
  hireling row (0x118 bytes) after a given one by name string / by id.
* FUN_00656900 (id): the npc row (0x4c bytes) whose first dword is id.
* FUN_00656f90 (n): the monsounds row (0x94 bytes) n, else 0.
* FUN_00658460 (ECX a, EDX b): is MonType a one of b (rows of 12 bytes, equiv
  words at `+2 / +4 / +6`); returns 0 when more than 0x7c entries remain after a pop.
* FUN_00659b40 (act, &count): monpreset's first row (returned) and count (stored through the pointer) for an act.
* FUN_00659b80 (kind, n): n plus the superunique count (kind 0) and the
  MonStats count (kind 0 or 2).
* FUN_00659c30: frees the sounds table and its link, clears the flag.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail. Verified for the values tested; see the page for what
is assumed (the realloc's copy; checks of the copied bytes test this hook's model, not game.exe).
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import Mismatch, require

FILE = "\\DATATBLS\\MonsterTbls.cpp"
(SET_STAT_WORD, UNIQUE_TITLE, UNIQUE_PREFIX, UNIQUE_SUFFIX, TC_ADD, TC_GROW, TC_ITEM, TC_CHANCE, SPLIT_NUM,
 TC_FIND, MIP_FREE, MIP_ROW, SU_COUNT, HIRE_NAME, HIRE_ID, NPC_ROW, SOUND_ROW, MONTYPE_ISA, PRESET_ACT, KIND_ID,
 SOUND_FREE) = (0x653D60, 0x653ED0, 0x653F10, 0x653F50, 0x653F90, 0x654010, 0x654080, 0x6540C0, 0x654410,
                0x654DD0, 0x654FC0, 0x654FE0, 0x655710, 0x656390, 0x656440, 0x656900, 0x656F90, 0x658460,
                0x659B40, 0x659B80, 0x659C30)
MONSTATS_PTR, MONSTATS_COUNT = 0x96C6A8, 0x96C6B0
TC_LINK, TC_ARRAY, TC_COUNT = 0x96C5E8, 0x96C5EC, 0x96C5F0
UNIQUE = {UNIQUE_TITLE: (0x96D49C, 0x96D48C), UNIQUE_PREFIX: (0x96D4A0, 0x96D490), UNIQUE_SUFFIX: (0x96D4A4, 0x96D494)}
EMPTY = 0x96D4B4
MIP_COUNT, MIP_PTR, SU_COUNT_ADDR = 0x96D480, 0x96D484, 0x96C70C
HIRE_BASE, HIRE_COUNT = 0x96BDD0, 0x96BDD4
NPC_BASE, NPC_COUNT = 0x96C5D8, 0x96C5DC
SOUND_BASE, SOUND_COUNT = 0x96C6B4, 0x96C6BC
MONTYPE_BASE, MONTYPE_COUNT = 0x96C86C, 0x96C870
PRESET_FLAG, PRESET_FIRST, PRESET_COUNT = 0x96C6D8, 0x96C6DC, 0x96C6F0
SOUNDS_PTR, SOUNDS_LINK, SOUNDS_FLAG = 0x96BDC4, 0x96BDC8, 0x96BDCC
MASK = 0xFFFFFFFF
STEP_LIMIT = 4000       # model steps after which the walk is declared endless (the emulator's limit is 500000 instructions)
MUTATIONS = ("stat-word-dword", "stat-range-le", "unique-no-flag", "unique-zero-entry", "tc-add-grow-15",
             "tc-add-no-zero", "tc-grow-no-zero", "tc-item-swap", "chance-no-total", "chance-flag-ignored",
             "chance-moves-self", "split-no-null", "find-negative", "mip-minus-one",
             "mip-wrap", "hire-name-open", "hire-start-same", "hire-id-version", "npc-last-wins",
             "sound-range", "isa-depth", "isa-equiv2", "preset-act-5", "kind-all", "sound-free-keeps", "isa-neg-skipped")
mutation = None


class Halt(Exception):
    """game.exe's unrecoverable-error call, with the source line it passed."""


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def s32(value):
    """A dword as a signed number."""
    return value - (1 << 32) if value & 0x80000000 else value


class Rig(mw.World):
    """The emulated game with the callees this file's functions use hooked."""

    def __init__(self):
        """Hook the allocator, the excel links, the frees, the random, the string lookup and the fatal path."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 500000      # atol runs the C runtime's strtol with its locale lookups
        self.blocks = {}
        self.index_value = 0
        self.string_value = 0
        self.link_value = 0
        machine = self.m
        machine.hook(0x40_B4B0, self.grow, 4)
        self.logger(0x6B_D500, 2, "link-add", regs=())
        self.logger(0x6B_D3C0, 3, "link-find", ret=lambda _c: self.link_value & MASK, regs=())
        self.logger(0x61_25C0, 1, "table-free", regs=())
        self.logger(0x6B_D0B0, 1, "link-free", regs=())
        self.logger(0x45_C3E0, 0, "random", ret=lambda _c: self.index_value, regs=("ecx", "edx"))
        self.logger(0x52_4A30, 0, "string", ret=lambda _c: self.string_value, regs=("ecx",))
        machine.hook(0x40_8090, lambda current: current.arg(0), 1)
        machine.hook(0x40_8A60, self.fatal, 0)

    def fatal(self, current):
        """The fatal-error call: arg 1 is the line the caller passed through 0x408090."""
        raise Halt(current.arg(1))

    def grow(self, current):
        """0x40_b4b0 (ECX pool, EDX old block; size, file, line, flag): log it, return a realloc'd block."""
        old, size = current.mu.reg_read(mw.REGISTERS["edx"]), current.arg(0)
        self.log.append(("grow", current.mu.reg_read(mw.REGISTERS["ecx"]), old, size,
                         current.cstr(current.arg(1)), current.arg(2), current.arg(3)))
        block = current.alloc(size, zero=False)
        current.mu.mem_write(block, bytes([mw.GARBAGE]) * size)
        if old in self.blocks:
            keep = min(self.blocks[old], size)
            current.mu.mem_write(block, current.read(old, keep))
        self.blocks[block] = size
        return block

    def array(self, data, spare=0):
        """Put `data` in memory as a block the allocator knows the size of, then `spare` bytes of garbage."""
        block = self.m.alloc(len(data) + 4 + spare)
        self.write(block, data + bytes([mw.GARBAGE]) * spare)
        self.blocks[block] = len(data)
        return block

    def go(self, address, *args, ecx=0, edx=0, regs=None):
        """One call; returns ("halt", line) when the fatal path ran, else ("ret", eax)."""
        try:
            return ("ret", self.run(address, *args, ecx=ecx, edx=edx, regs=regs))
        except Halt as halt:
            return ("halt", halt.args[0])
        except Mismatch as err:
            if "did not return" in str(err):
                return ("loop", 0)      # the instruction limit ran out: the function never finished
            raise


def rand_signed(rng):
    """A dword drawn to hit zero, small, negative and large values."""
    return rng.choice((0, 1, 2, 5, 0xFFFF, MASK, 0x7FFFFFFF, 0x80000000, rng.randrange(1 << 32), rng.randrange(40)))


def check_set_stat_word(rig, rng):
    """FUN_00653d60 on a random MonStats array."""
    machine = rig.m
    count = rng.randrange(0, 6)
    image = bytearray(rng.randbytes(0x1A8 * (count + 2)))
    absent = rng.random() < 0.2
    machine.w32(MONSTATS_PTR, 0 if absent else rig.array(bytes(image)))
    machine.w32(MONSTATS_COUNT, count)
    index = rng.choice((-1, 0, count - 1, count, count + 1, rng.randrange(-3, 8), rng.randrange(1 << 32)))
    word = rng.randrange(1 << 32)
    got = rig.go(SET_STAT_WORD, s32(index) & MASK, word)
    if absent:
        require(got == ("halt", 0x2EF), f"absent table: {got}")
        return
    pointer = machine.r32(MONSTATS_PTR)
    inside = (-1 < s32(index) < s32(count)) if not mut("stat-range-le") else (-1 < s32(index) <= s32(count))
    if inside:
        struct.pack_into("<I" if mut("stat-word-dword") else "<H", image, 0x1A8 * s32(index) + 0x32,
                         word if mut("stat-word-dword") else word & 0xFFFF)
    require(got[0] == "ret" and not rig.log, f"set stat word: {got} {rig.log}")
    require(machine.read(pointer, len(image)) == bytes(image), f"index {s32(index)} of {count}: array differs")


def check_unique(rig, rng, address):
    """FUN_00653ed0 / 00653f10 / 00653f50 with a scripted random and string."""
    machine = rig.m
    count_at, array_at = UNIQUE[address]
    count = rng.choice((0, 1, 3, 7, -2))
    entries = [rng.choice((0, 0, rng.randrange(1, 1 << 16))) for _ in range(max(count, 1))]
    machine.w32(count_at, count & MASK)
    machine.w32(array_at, rig.array(struct.pack(f"<{len(entries)}H", *entries)))
    rig.index_value = rng.randrange(max(count, 1))
    rig.string_value = rng.choice((0, 0x1234, rng.randrange(1 << 32)))
    flag = rng.choice((0, 1, 0xFFFFFFFF, rng.randrange(1 << 32)))
    got = rig.go(address, ecx=flag)
    want_log, want = [], EMPTY
    if (flag != 0 or mut("unique-no-flag")) and count > 0:
        want_log.append(("random", (flag + 0x20) & MASK, count))
        entry = entries[rig.index_value]
        if entry != 0 or mut("unique-zero-entry"):
            want_log.append(("string", entry))
            if rig.string_value != 0:
                want = rig.string_value
    require(got == ("ret", want) and rig.log == want_log, f"{address:#x}: {got} {rig.log} != {want:#x} {want_log}")


def check_tc_add(rig, rng):
    """FUN_00653f90: add a treasure class record."""
    machine = rig.m
    count = rng.choice((0, 1, 15, 16, 17, 31, 32, rng.randrange(0, 70)))
    size = max(count, 1) * 0x2C
    image = rng.randbytes(size)
    array = rig.array(image, spare=0x2C * 2)
    machine.w32(TC_LINK, 0x7000_0001)
    machine.w32(TC_ARRAY, array)
    machine.w32(TC_COUNT, count)
    name = rng.randrange(1 << 32)
    got = rig.go(TC_ADD, ecx=name)
    grow = count % (15 if mut("tc-add-grow-15") else 16) == 0
    require(rig.log[:1] == [("link-add", 0x7000_0001, name)], f"link: {rig.log}")
    if grow:
        want = ("grow", 0, array, (count + 16) * 0x2C, "." + FILE, 0x3A2, 0)
        require(rig.log[1:] == [want], f"grow: {rig.log}")
    else:
        require(len(rig.log) == 1, f"unexpected allocation {rig.log}")
    new_array = machine.r32(TC_ARRAY)
    require((new_array != array) == grow, "array pointer")
    record = new_array + count * 0x2C
    require(got == ("ret", record), f"record {got} != {record:#x}")
    require(machine.r32(TC_COUNT) == count + 1, "count")
    fill = bytes([mw.GARBAGE]) if mut("tc-add-no-zero") else b"\0"
    require(machine.read(record, 0x2C) == fill * 0x2C, "the record is not zeroed")
    # the copy is done by the hooked allocator (assumed realloc), so this checks the hook, not game.exe
    require(machine.read(new_array, count * 0x2C) == image[:count * 0x2C], "earlier records changed")


def make_class(rig, rng, count):
    """Build a class (0x2c bytes) and its item array (count * 0x1c); returns (class, array, items)."""
    machine = rig.m
    items = []
    start, chance = 0, 0
    for _ in range(count):
        step = rng.randrange(0, 50)
        items.append([start, chance] + [rng.randrange(1 << 32) for _ in range(5)])
        start += rng.randrange(0, 50)
        chance += step
    item_image = b"".join(struct.pack("<7I", *[v & MASK for v in item]) for item in items)
    array = rig.array(item_image or bytes(0x1C))
    cls = machine.alloc(0x2C)
    rig.write(cls, rng.randbytes(0x2C))
    machine.w32(cls + 4, count)
    machine.w32(cls + 8, start)
    machine.w32(cls + 0xC, chance)
    machine.w32(cls + 0x28, array)
    return cls, array, item_image


def check_tc_grow(rig, rng):
    """FUN_00654010 (reallocate the item list) and FUN_00654080 (append an item with the totals)."""
    machine = rig.m
    count = rng.randrange(0, 9)
    cls, array, image = make_class(rig, rng, count)
    before = machine.read(cls, 0x2C)
    if rng.random() < 0.5:
        new = count + rng.randrange(0, 6)
        rig.go(TC_GROW, ecx=cls, edx=new)
        require(len(rig.log) == 1 and rig.log[0][:3] == ("grow", 0, array) and rig.log[0][3] == new * 0x1C and
                rig.log[0][4].endswith(FILE) and rig.log[0][5:] == (0x3BA, 0), f"grow log {rig.log}")
        grown = machine.r32(cls + 0x28)
        require(machine.r32(cls + 4) == new, "count not set")
        # the copy is the hook's (assumed realloc): this checks the hook
        require(machine.read(grown, count * 0x1C) == image[:count * 0x1C], "old items lost")
        fill = bytes([mw.GARBAGE]) if mut("tc-grow-no-zero") else b"\0"
        require(machine.read(grown + count * 0x1C, (new - count) * 0x1C) == fill * ((new - count) * 0x1C), "tail not zeroed")
        after = machine.read(cls, 0x2C)
        require(after[:4] == before[:4] and after[8:0x28] == before[8:0x28], "class fields other than +4 / +0x28 changed")
        return
    total8, totalc = machine.r32(cls + 8), machine.r32(cls + 0xC)
    got = rig.go(TC_ITEM, ecx=cls)
    grown = machine.r32(cls + 0x28)
    item = grown + count * 0x1C
    require(len(rig.log) == 1 and rig.log[0][3] == (count + 1) * 0x1C and rig.log[0][5] == 0x3BA, f"log {rig.log}")
    require(got == ("ret", item), f"item {got} != {item:#x}")
    want = struct.pack("<II", totalc, total8) if mut("tc-item-swap") else struct.pack("<II", total8, totalc)
    require(machine.read(item, 8) == want, "item totals")
    require(machine.read(item + 8, 0x14) == bytes(0x14), "the rest of the item is not zero")
    # the copy is the hook's (assumed realloc): this checks the hook
    require(machine.read(grown, count * 0x1C) == image[:count * 0x1C], "old items lost")
    require(machine.r32(cls + 4) == count + 1, "count")


def model_chance(cls, items, index, chance, flag):
    """FUN_006540c0: returns the new (class +8, class +0xc, items)."""
    n = len(items)
    total_start, total_chance = cls[0], cls[1]
    last = index == n - 1
    end = total_chance if last else items[index + 1][1]
    span = (end - items[index][1]) & MASK
    if span != chance:
        delta = (chance - span) & MASK
        total_chance = (total_chance + delta) & MASK
        for j in range(index + (0 if mut("chance-moves-self") else 1), n):
            items[j][1] = (items[j][1] + delta) & MASK
    if flag == 0 or mut("chance-flag-ignored"):
        index_end = total_start if last else items[index + 1][0]
        span = (index_end - items[index][0]) & MASK
        if span != chance:
            delta = (chance - span) & MASK
            total_start = (total_start + delta) & MASK
            for j in range(index + 1, n):
                items[j][0] = (items[j][0] + delta) & MASK
    return total_start, total_chance, items


def check_chance(rig, rng):
    """FUN_006540c0 on a random class and item."""
    machine = rig.m
    count = rng.randrange(1, 8)
    cls, array, image = make_class(rig, rng, count)
    items = [list(struct.unpack_from("<7I", image, 0x1C * i)) for i in range(count)]
    index = rng.randrange(count)
    chance = rng.choice((0, 1, 7, 100, MASK, rng.randrange(1 << 32), (items[index][1] + 3) & MASK))
    flag = rng.choice((0, 0, 1, MASK))
    start0, chance0 = machine.r32(cls + 8), machine.r32(cls + 0xC)
    want_items = [list(item) for item in items]
    start, total, want_items = model_chance((start0, chance0), want_items, index, chance, flag) if not mut("chance-no-total") else (
        *model_chance((start0, chance0), want_items, index, chance, flag)[:1], chance0, want_items)
    got = rig.go(TC_CHANCE, chance, flag, ecx=cls, edx=array + 0x1C * index)
    require(got[0] == "ret" and not rig.log, f"chance call {got} {rig.log}")
    want_image = b"".join(struct.pack("<7I", *item) for item in want_items)
    require(machine.read(array, len(image)) == want_image, f"items differ (index {index} of {count}, chance {chance:#x}, flag {flag})")
    require((machine.r32(cls + 8), machine.r32(cls + 0xC)) == (start, total), "class totals differ")
    require(machine.r32(cls + 4) == count and machine.r32(cls + 0x28) == array, "class header changed")


def atol_model(text):
    """MSVC's atol on a byte string: leading white space, a sign, digits, wrapped to 32 bits."""
    index = 0
    while index < len(text) and text[index] in b" \t\n\v\f\r":
        index += 1
    sign = 1
    if index < len(text) and text[index] in b"+-":
        sign = -1 if text[index] == 0x2D else 1
        index += 1
    value = 0
    while index < len(text) and 0x30 <= text[index] <= 0x39:
        value = value * 10 + text[index] - 0x30
        index += 1
    return (sign * value) & MASK


def check_split(rig, rng):
    """FUN_00654410: split the next number off a cursor at a separator."""
    machine = rig.m
    sep = rng.choice((b",", b";", b" ", b"%", b"\x00"))
    pieces = [rng.choice((b"12", b"-7", b" 40", b"+3", b"0", b"x9", b"", b"2147483647", b"-2147483648"))
              for _ in range(rng.randrange(1, 4))]
    text = sep.join(pieces) + (b"" if rng.random() < 0.5 else sep)
    cursor_text = machine.alloc(len(text) + 8)
    rig.write(cursor_text, text + b"\0\0\0\0")
    cell = machine.alloc(8)
    machine.w32(cell, cursor_text)
    got = rig.go(SPLIT_NUM, ecx=0, edx=0, regs={"esi": cell, "eax": sep[0] | 0xABCD00})
    found = text.find(sep)
    if found < 0 and sep == b"\x00":
        found = len(text)       # strchr finds the terminator
    first = text[:found] if found >= 0 else text
    require(got == ("ret", atol_model(first)), f"atol {got} for {first!r}")
    after = machine.r32(cell)
    if found < 0:
        require(after == 0, f"cursor {after:#x} without separator")
    else:
        require(after == cursor_text + found + 1, f"cursor {after - cursor_text:#x} != {found + 1:#x}")
        want = bytearray(text + b"\0\0\0\0")
        if not mut("split-no-null"):
            want[found] = 0
        require(machine.read(cursor_text, len(want)) == bytes(want), "text differs after the split")
    if found >= 0 and mut("split-no-null"):
        require(machine.read(cursor_text + found, 1) == sep or sep == b"\x00", "the separator was not overwritten")


def check_find(rig, rng):
    """FUN_00654dd0: a treasure class by name."""
    machine = rig.m
    array = machine.alloc(0x2C * 8)
    machine.w32(TC_LINK, 0x7000_0002)
    machine.w32(TC_ARRAY, array)
    rig.link_value = rng.choice((-5, -1, 0, 1, 7, MASK))
    name = rng.randrange(1 << 32)
    got = rig.go(TC_FIND, name)
    index = s32(rig.link_value & MASK)
    want = (index * 0x2C + array) & MASK if (index >= 0 or mut("find-negative")) else 0
    require(got == ("ret", want) and rig.log == [("link-find", 0x7000_0002, name, 0)], f"find {got} {rig.log}")


def check_mip(rig, rng):
    """FUN_00654fc0 (free) and FUN_00654fe0 (row n) of monitempercent."""
    machine = rig.m
    count = rng.choice((0, 1, 4, 9, -1))
    absent = rng.random() < 0.2
    wrap = rng.random() < 0.15
    n = rng.choice((-1, 0, 1, 3, count - 1, count, count + 3, -7, rng.randrange(-9, 12)))
    pointer = 0 if absent else (((1 << 32) - 4 * s32(n & MASK)) & MASK if wrap else rng.randrange(0x1000, 0x7000_0000))
    machine.w32(MIP_COUNT, count & MASK)
    machine.w32(MIP_PTR, pointer)
    if rng.random() < 0.25:
        got = rig.go(MIP_FREE)
        require(got[0] == "ret" and rig.log == [("table-free", pointer)], f"free {got} {rig.log}")
        return
    got = rig.go(MIP_ROW, n & MASK)
    inside = s32(n & MASK) < count and (n & MASK) != MASK
    if mut("mip-minus-one"):
        inside = s32(n & MASK) < count
    if not inside:
        want = ("ret", 0)
    elif pointer == 0:
        want = ("halt", 0x6DC)
    else:
        total = (pointer + 4 * s32(n & MASK)) & MASK
        want = ("ret", total) if total != 0 or mut("mip-wrap") else ("halt", 0x6DD)
    require(got == want and not rig.log, f"row {s32(n & MASK)} of {count} at {pointer:#x}: {got} != {want}")


def check_su_count(rig, rng):
    """FUN_00655710: the superunique count."""
    value = rng.randrange(1 << 32)
    rig.m.w32(SU_COUNT_ADDR, value)
    require(rig.go(SU_COUNT) == ("ret", value) and not rig.log, "superunique count")


def make_hires(rig, rng, count):
    """Hireling rows (0x118): +0 version word, +8 id, +0x114 / +0x116 the name string range."""
    rows = bytearray()
    for _ in range(count):
        row = bytearray(rng.randbytes(0x118))
        struct.pack_into("<H", row, 0, rng.choice((0, 100, 100, 0, 7)))
        struct.pack_into("<I", row, 8, rng.randrange(0, 5))
        low = rng.randrange(0, 6)
        struct.pack_into("<HH", row, 0x114, low, rng.choice((low + rng.randrange(0, 4), low - 1 if low else 0)) & 0xFFFF)
        rows += row
    base = rig.array(bytes(rows) or bytes(0x118))
    rig.m.w32(HIRE_BASE, base)
    rig.m.w32(HIRE_COUNT, count)
    return base, bytes(rows)


def check_hire(rig, rng, address):
    """FUN_00656390 (by name string) and FUN_00656440 (by id): the next matching hireling row."""
    count = rng.randrange(0, 9)
    base, rows = make_hires(rig, rng, count)
    after_index = rng.choice((None, None, rng.randrange(max(count, 1)), count - 1))
    after = 0 if after_index is None else base + 0x118 * after_index
    expansion = rng.choice((0, 1, 5, MASK))
    key = rng.randrange(0, 7)
    start = 0 if after == 0 else after_index + 1
    if mut("hire-start-same") and after:
        start = after_index
    version_wanted = 100 if expansion else 0
    want = 0
    for i in range(start, count):
        row = rows[0x118 * i:0x118 * (i + 1)]
        version = struct.unpack_from("<H", row, 0)[0]
        if address == HIRE_NAME:
            low, high = struct.unpack_from("<HH", row, 0x114)
            hit = not (key < low or (high < key if not mut("hire-name-open") else False)) and version == version_wanted
        else:
            hit = struct.unpack_from("<I", row, 8)[0] == key and (version == version_wanted or mut("hire-id-version"))
        if hit:
            want = base + 0x118 * i
            break
    if start >= count:
        want = 0
    got = rig.go(address, expansion, key, after)
    require(got == ("ret", want) and not rig.log, f"{address:#x}: {got} != {want:#x} (after {after_index}, key {key}, exp {expansion})")


def check_npc(rig, rng):
    """FUN_00656900: the npc row whose first dword is the id."""
    count = rng.randrange(0, 7)
    ids = [rng.randrange(0, 5) for _ in range(count)]
    rows = b"".join(struct.pack("<I", i) + rng.randbytes(0x48) for i in ids)
    base = rig.array(rows or bytes(0x4C))
    rig.m.w32(NPC_BASE, base)
    rig.m.w32(NPC_COUNT, count)
    key = rng.randrange(0, 6)
    hits = [i for i, v in enumerate(ids) if v == key]
    want = base + 0x4C * (hits[-1] if mut("npc-last-wins") else hits[0]) if hits else 0
    require(rig.go(NPC_ROW, key) == ("ret", want), f"npc {key} in {ids}")


def check_sound_row(rig, rng):
    """FUN_00656f90: monsounds row n."""
    count = rng.choice((0, 1, 5, 9))
    absent = rng.random() < 0.2
    base = 0 if absent else rig.array(bytes(0x94 * max(count, 1)))
    rig.m.w32(SOUND_BASE, base)
    rig.m.w32(SOUND_COUNT, count)
    n = rng.choice((-1, 0, count - 1, count, count + 1, rng.randrange(-4, 12), MASK))
    inside = -1 < s32(n & MASK) < count or (mut("sound-range") and s32(n & MASK) == count)
    want = (base + 0x94 * s32(n & MASK)) & MASK if base and inside else 0
    require(rig.go(SOUND_ROW, n & MASK) == ("ret", want), f"row {s32(n & MASK)} of {count}")


def check_isa(rig, rng):
    """FUN_00658460: is MonType a one of b."""
    count = rng.choice((1, 2, 5, 12, 40))
    pad = 64
    rows = [[rng.randrange(1 << 16), rng.choice((0, 0, rng.randrange(0, count + 3), -1)),
             rng.choice((0, 0, rng.randrange(0, count + 3), -2)), rng.choice((0, 0, rng.randrange(0, count + 3)))]
            for _ in range(count)]
    deep = rng.random() < 0.1
    cycle = False
    if deep:        # every row names the next one three times: the walk's stack grows two a level
        count = 130
        rows = [[0, i + 1 if i + 1 < count else 0, i + 1 if i + 1 < count else 0, i + 1 if i + 1 < count else 0]
                for i in range(count)]
    elif rng.random() < 0.06:
        count = 6       # rows 1 and 2 name each other and nothing else: row 5 is never reached
        rows = [[0, 0, 0, 0] for _ in range(count)]
        rows[1][1], rows[2][1] = 2, 1
        cycle = True
    elif rng.random() < 0.25:
        for i in range(1, count):
            rows[i][1] = i + 1 if i + 1 < count else 0
        if count > 1 and rng.random() < 0.5:
            rows[count - 1][1] = rng.randrange(1, count)
    padding = [[0, 0, 0, 0] for _ in range(pad)]
    negpath = None
    if not (deep or cycle):     # rows before row 0, reached by a negative equivalent: they name real rows (no negative words)
        padding = [[rng.randrange(1 << 16)] + [rng.choice((0, rng.randrange(1, count + 3))) for _ in range(3)] for _ in range(pad)]
        if rng.random() < 0.3 and count > 2:    # a -> (negative equivalent) -> padding row -> target
            a_row, back = rng.randrange(1, count), rng.randrange(1, 4)
            target = rng.choice([i for i in range(1, count) if i != a_row])
            rows[a_row][1] = rng.randrange(1, count + 3)
            rows[a_row][2], rows[a_row][3] = -back, 0
            padding[pad - back][1:] = [target, 0, 0]
            negpath = (a_row, target)
    image = b"".join(struct.pack("<Hhhh", *r) + rng.randbytes(4) for r in padding + rows)
    table = rig.array(image)
    rig.m.w32(MONTYPE_BASE, table + 12 * pad)
    rig.m.w32(MONTYPE_COUNT, count)
    a = rng.choice((0, -1, count, count + 1, rng.randrange(0, count + 2)))
    b = rng.choice((0, -1, rng.randrange(-1, count + 2), a))
    if negpath:
        a, b = negpath
    elif deep:
        a, b = 1, rng.choice((60, 63, 64, 65, 70))
    elif cycle:
        a, b = 1, 5

    def word(row, which):
        """Equiv word `which` (1..3) of a row, from the table memory (a negative row reads the padding before it)."""
        if row < -pad:
            return 0
        return struct.unpack_from("<h", image, 12 * (pad + row) + 2 * which)[0]

    def model(a, b):
        """The stack walk of FUN_00658460: pop, compare, push the (up to three) equivalents."""
        if b < 1 or a < 1 or a >= count:
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
            first = word(cur, 1)
            if first > 0:
                stack.append(first)
                if word(cur, 2) != 0 and not (mut("isa-neg-skipped") and word(cur, 2) < 0):
                    stack.append(word(cur, 2))
                    if word(cur, 3) != 0 and not mut("isa-equiv2") and not (mut("isa-neg-skipped") and word(cur, 3) < 0):
                        stack.append(word(cur, 3))
        return "loop"       # a cycle of single links: the original has no visited set and never returns
    want = model(a, b)
    got = rig.go(MONTYPE_ISA, ecx=a & MASK, edx=b & MASK)
    if want == "loop":
        require(got == ("loop", 0), f"isa({a}, {b}) should not return: {got}")
        return
    require(got == ("ret", want) and not rig.log, f"isa({a}, {b}) over {count} rows: {got} != {want}")


def check_preset(rig, rng):
    """FUN_00659b40 (act, &count): returns monpreset's first row for an act and stores its count through the pointer."""
    machine = rig.m
    machine.w32(PRESET_FLAG, rng.choice((0, 1, 7)))
    rows = [rng.randrange(1 << 32) for _ in range(5)]
    counts = [rng.randrange(1 << 32) for _ in range(5)]
    extra = rng.randrange(1 << 32)
    rig.put_words(PRESET_FIRST, rows + counts + [extra])      # the two tables are adjacent: index 5 reads into the next
    act = rng.choice((0, 1, 4, 5, 6, MASK, rng.randrange(1 << 32)))
    use_out = rng.random() < 0.85
    out = machine.alloc(8) if use_out else 0
    if use_out:
        machine.w32(out, 0xDEADBEEF)
    got = rig.go(PRESET_ACT, act, out)
    ok = use_out and act < (6 if mut("preset-act-5") else 5) and machine.r32(PRESET_FLAG) != 0
    if not use_out:
        require(got == ("ret", 0), f"no out pointer: {got}")
        return
    first, count = ((rows + counts)[act], (counts + [extra])[act]) if ok else (0, 0)
    require(got == ("ret", first) and machine.r32(out) == count, f"act {act}: {got} count {machine.r32(out):#x}")


def check_kind(rig, rng):
    """FUN_00659b80 (kind, n): n plus the superunique and MonStats counts."""
    machine = rig.m
    table = machine.alloc(0x1000)
    machine.w32(0x744304, table)
    machine.w32(table + 0xA80, v := rng.randrange(1 << 32))
    machine.w32(SU_COUNT_ADDR, su := rng.randrange(1 << 32))
    kind = rng.choice((0, 1, 2, 3, MASK))
    n = rng.randrange(1 << 32)
    want = n
    if kind == 0:
        want = (want + su) & MASK
    if kind in (0, 2) or mut("kind-all"):
        want = (want + v) & MASK
    require(rig.go(KIND_ID, kind, n) == ("ret", want), f"kind {kind}")


def check_sounds_free(rig, rng):
    """FUN_00659c30: free the sounds table, its link, clear the flag."""
    machine = rig.m
    pointer = rng.choice((0, rng.randrange(1, 1 << 30)))
    link = rng.randrange(1 << 32)
    machine.w32(SOUNDS_PTR, pointer)
    machine.w32(SOUNDS_LINK, link)
    machine.w32(SOUNDS_FLAG, 0x55)
    got = rig.go(SOUND_FREE)
    want_log = [("table-free", pointer), ("link-free", link)] if pointer else []
    require(got[0] == "ret" and rig.log == want_log, f"log {rig.log} != {want_log}")
    want_flag = 0 if pointer and not mut("sound-free-keeps") else 0x55
    require(machine.r32(SOUNDS_FLAG) == want_flag, "sounds flag")


def run_all():
    """Run every check on random tables; returns the case count."""
    rng = random.Random(0x653D60)
    rig = Rig()
    plan = [(check_set_stat_word, 200), (lambda r, g: check_unique(r, g, UNIQUE_TITLE), 150),
            (lambda r, g: check_unique(r, g, UNIQUE_PREFIX), 150), (lambda r, g: check_unique(r, g, UNIQUE_SUFFIX), 150),
            (check_tc_add, 200), (check_tc_grow, 300), (check_chance, 600), (check_split, 400), (check_find, 100),
            (check_mip, 400), (check_su_count, 20), (lambda r, g: check_hire(r, g, HIRE_NAME), 400),
            (lambda r, g: check_hire(r, g, HIRE_ID), 400), (check_npc, 150), (check_sound_row, 150), (check_isa, 800),
            (check_preset, 200), (check_kind, 100), (check_sounds_free, 60)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "MonsterTbls helper cases")


if __name__ == "__main__":
    main()
