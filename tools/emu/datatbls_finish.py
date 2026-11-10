# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 14 more DATATBLS functions in stock game.exe 1.14d against models and hooked callees.

    uv run python datatbls_finish.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

DataTbls.cpp (all against ".\\DATATBLS\\DataTbls.cpp"):

* FUN_006118b0 (ecx &buffer, edx &size, stack &capacity, source, n; ret 0xc): while size + n >=
  capacity (signed) the capacity grows by 0x400 and, once it reaches 0x7fffffff, the call gives
  0 (the capacity stays grown); each step reallocates the buffer (FUN_0040b4b0, pool 0, line
  0x292, flag 0). Then the source is copied to buffer + size, size += n, and the old size is
  returned.
* FUN_00611bd0 and FUN_00611c70 (ecx text, edx record, stack offset, size, row, column; ret 0x10):
  compile a skills / skill description calculation. No record: nothing. No text, or
  FUN_006c1ae0 (text, a 0x400 byte buffer, 0x400, callbacks 0x611930, 0x6119e0, 0x6119f0) giving
  0 or less: -1 stored at record + offset. Else the bytes are appended with FUN_006118b0 to the
  buffer at 0x96bc70 / 0x96bc74 / 0x96bc78 (descriptions: 0x96bc7c / 0x96bc80 / 0x96bc84) and
  the old size is stored at record + offset.
* FUN_00611ee0, 00611f40, 00611fa0, 00611fe0, 00612030 (class id, buffer, size; id in ecx, edx,
  ebx, ebx, eax; buffer edi, size esi): the class name into the buffer, last byte forced to
  0: a player's CharStats name (table +0xbc4, count +0xbc8 of the Items table pointer at
  0x744304, rows of 0xc4, name at +0x20) else `player %d`; a monster's MonStats name string
  (+0xa78, +0xa80, rows of 0x1a8, id word at +6, not 0x1506) through FUN_00524a30 and
  FUN_00526f70, else `monster %d`; an object's name (FUN_00640e90, a row's first byte
  non-zero) else `object %d`; an item's name string (FUN_006335f0, id word at +0xf4, not
  0x1506) else `item %d`, the item's name string path having no size test; `missile %d`. A
  size of 0 or less writes nothing, except in the item name string path.
* FUN_00617c30 (skills) and FUN_00618c90 (states) free their derived arrays (pool 0, the
  lines in the code), clearing the pointers and the counts; the tables themselves are freed
  at line 0x926, or four bytes before at line 0x924 when 0x744308 is set; the link free
  FUN_006bd0b0 follows each table.
* FUN_00619140 frees every table in 49 steps: 43 unloaders (FUN_00612ea0 first) in the order the
  code calls them, the CharStats, pet type, experience and difficulty tables, the pet link and
  the animation data between, checked by the log.

LvlTbls.cpp and the experience table:

* FUN_00613d30 (stdcall pool): `experience`, 8 INT columns Amazon .. Assassin and ExpRatio at
  0, 4 .. 0x1c, record 0x20, no row count, table to 0x96c8a8.
* FUN_0061e520 (fastcall pool): `lvltypes`, 32 ASCII columns `File 1` .. `File 32` (size 0x3b,
  0x3c apart), `Act` CHAR at 0x780, `Expansion` INT at 0x784, record 0x788, count 0x96c9fc,
  table 0x96c9f0; then FUN_0061e4a0 on each of the 32 names of each row with the string at
  0x6e867c ("DATA\\\\GLOBAL\\\\TILES").
* FUN_0061fae0: no lvltypes table is fatal (line 0x410); else 0x1000 bytes a row are allocated
  (LvlTbls.cpp line 0x413) into 0x96ca1c and cleared; per row, its names up to the first one
  that is exactly "0" get FUN_00604a40 (the row's 0x1000 block, the name) when the game is
  the expansion or the row's flag (+0x784) is 0.

The callees are hooked and logged in order; the CRT strncpy, _snprintf and memset run for
real. Not run: a count of 0x7ffffc00 or more in FUN_006118b0 (the signed wrap loops about
two million times) and negative row counts in FUN_0061fae0 (a 4 GB allocation).
`--mutation-check` breaks the models on purpose, one defect at a time, and requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require
from monster_loaders import ASCII, CHAR, INT, REGION, REGION_END, Rig, check_compile_call, schema

APPEND, SKILL_CODE, DESC_CODE = 0x61_18B0, 0x61_1BD0, 0x61_1C70
PLAYER, MONSTER, OBJECT, ITEM, MISSILE = 0x61_1EE0, 0x61_1F40, 0x61_1FA0, 0x61_1FE0, 0x61_2030
SKILLS_FREE, STATES_FREE, FREE_TABLES = 0x61_7C30, 0x61_8C90, 0x61_9140
EXPERIENCE, LVLTYPES, EXPANSION_SETS = 0x61_3D30, 0x61_E520, 0x61_FAE0
REALLOC, PARSE, GET_LOCALE, WIDE_TO_MULTI = 0x40_B4B0, 0x6C_1AE0, 0x52_4A30, 0x52_6F70
OBJECT_LINE, ITEM_LINE, FIX_SLASHES, ALLOC_SLOT = 0x64_0E90, 0x63_35F0, 0x61_E4A0, 0x60_4A40
TABLES, MISC_FLAG = 0x74_4304, 0x74_4308
DATATBLS_FILE, LVLTBLS_FILE = ".\\DATATBLS\\DataTbls.cpp", ".\\DATATBLS\\LvlTbls.cpp"
MASK = 0xFFFFFFFF
PATTERN = 0xA5
MUTATIONS = ("append-grow-step", "append-lt-boundary", "append-limit-eq", "append-returns-new", "append-size-kept",
             "append-realloc-line", "skill-text-null-kept", "skill-nonpositive-kept", "skill-desc-uses-skills",
             "skill-stores-new-size", "skill-null-record-parses", "skill-callbacks", "player-bound-le",
             "player-empty-name", "player-no-terminator", "player-format", "monster-sentinel", "monster-no-size-test",
             "monster-wide-args", "object-empty-name", "object-row-offset", "item-size-test", "item-sentinel",
             "item-name-offset", "missile-format", "name-truncate-off-by-one", "skills-free-line",
             "skills-keeps-pointer", "skills-misc-offset", "skills-no-link-free", "skills-clears-table",
             "states-memset-short", "states-line", "states-keeps-pointer", "states-misc-line", "free-tables-order",
             "free-tables-skip-last", "free-tables-experience-offset", "free-tables-difficulty-line",
             "free-tables-animation-arg", "experience-offset", "experience-record", "experience-store",
             "lvltypes-act-type", "lvltypes-stride", "lvltypes-skip-last-file", "lvltypes-count-slot",
             "expansion-no-fatal", "expansion-alloc-size", "expansion-stops-at-zero", "expansion-skips-empty",
             "expansion-flag-ignored", "expansion-block-stride", "expansion-classic-allows")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def reg(current, name):
    """A register of the stopped call."""
    return current.mu.reg_read(mw.REGISTERS[name])


class Plain(mw.World):
    """The emulated game with the pool entries hooked and a realloc that keeps the old bytes."""

    def __init__(self):
        """Build the world and hook the realloc."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 400000
        self.sizes = {}
        self.returned = []
        self.parse_script = (0, b"")
        self.parse_calls = []
        self.m.hook(REALLOC, self.realloc, 4)
        self.m.hook(PARSE, self.parse, 6)

    def block(self, size, fill=PATTERN):
        """A tracked block of `size` bytes filled with `fill`."""
        address = self.m.alloc(max(size, 1) + 8, zero=False)
        self.write(address, bytes([fill]) * (max(size, 1) + 8))
        self.sizes[address] = size
        return address

    def realloc(self, current):
        """ReAllocServerMemory (ecx pool, edx block, stack size, file, line, flag): log; the old bytes carry over."""
        size, old = current.arg(0), reg(current, "edx")
        self.log.append(("realloc", reg(current, "ecx"), old, size, self.file_of(current.arg(1)), current.arg(2), current.arg(3)))
        new = self.m.alloc(min(size, 0x10000) + 8, zero=False)
        self.write(new, bytes([PATTERN]) * (min(size, 0x10000) + 8))
        keep = min(self.sizes.get(old, 0), size, 0x10000)
        if keep:
            self.write(new, self.m.read(old, keep))
        self.sizes[new] = size
        self.returned.append(new)
        return new

    def parse(self, current):
        """FUN_006c1ae0 (text, buffer, 0x400, three callbacks): log, write the scripted bytes, return the scripted length."""
        self.parse_calls.append((current.arg(0), current.arg(2), current.arg(3), current.arg(4), current.arg(5),
                                 current.arg(1) != 0))
        length, data = self.parse_script
        if length > 0:
            self.write(current.arg(1), data[:length])
        return length & MASK


def append_model(size, count, capacity):
    """FUN_006118b0's growth loop: (result, capacity, new size, the sizes reallocated to)."""
    step = 0x200 if mut("append-grow-step") else 0x400
    grow = size + count > capacity if mut("append-lt-boundary") else size + count >= capacity
    reallocs = []
    while grow:
        capacity += step
        if capacity > 0x7FFFFFFF if mut("append-limit-eq") else capacity >= 0x7FFFFFFF:
            return 0, capacity, size, reallocs
        reallocs.append(capacity)
        grow = size + count > capacity if mut("append-lt-boundary") else size + count >= capacity
    return (size + count if mut("append-returns-new") else size), capacity, (size if mut("append-size-kept") else size + count), reallocs


def check_append(rig, rng):
    """FUN_006118b0: grow by 0x400 until the data fits (signed), copy it, return the old size."""
    edge = rng.random() < 0.1
    if edge:
        size, capacity, count = rng.choice(((0x7FFFFBFF, 0x7FFFFBFF, 1), (0x7FFFFBFF, 0x7FFFFBFE, 0)))
    else:
        capacity = rng.choice((0, 0x400, 0x800, 0x1000, rng.randrange(0, 5000)))
        size = rng.randrange(0, capacity + 1) if capacity else 0
        count = rng.choice((0, 1, 5, 0x3FF, 0x400, 0x401, rng.randrange(0, 3000)))
        if rng.random() < 0.25:
            count = max(capacity - size, 0)
        if rng.random() < 0.15:
            count = max(capacity - size - 1, 0)
    held = 0 if edge else size
    buffer = rig.block(min(max(capacity, held), 0x10000))
    content = rng.randbytes(held) if held else b""
    rig.write(buffer, content)
    source = rig.m.put(rng.randbytes(count or 1))
    cell = rig.m.alloc(16)
    rig.put_words(cell, [buffer, size, capacity])
    rig.returned.clear()
    value = rig.run(APPEND, cell + 8, source, count, ecx=cell, edx=cell + 4, nstack=3)
    result, want_cap, want_size, reallocs = append_model(size, count, capacity)
    line = 0x293 if mut("append-realloc-line") else 0x292
    olds = [buffer] + rig.returned[:-1]
    want_log = [("realloc", 0, olds[i] if i < len(olds) else 0, new, DATATBLS_FILE, line, 0) for i, new in enumerate(reallocs)]
    require(rig.log == want_log, f"append reallocs {rig.log} != {want_log}")
    require(rig.read_words(cell + 8, 1)[0] == want_cap, f"append capacity {rig.read_words(cell + 8, 1)[0]:#x} != {want_cap:#x}")
    require(value == result, f"append returned {value:#x} != {result:#x}")
    require(rig.read_words(cell + 4, 1)[0] == want_size, "append size")
    if not edge and result != 0 or (not edge and count == 0):
        data = rig.m.read(rig.read_words(cell, 1)[0], size + count)
        require(data == content[:size] + rig.m.read(source, count), "append buffer content")


def check_skill_code(rig, rng, desc):
    """FUN_00611bd0 / FUN_00611c70: compile a calculation text into the skills / description code buffer."""
    base = 0x96_BC70 if mut("skill-desc-uses-skills") else 0x96_BC7C
    if not desc:
        base = 0x96_BC70
    capacity = rng.choice((0x400, 0x800))
    size = rng.randrange(0, 0x3F0)
    buffer = rig.block(capacity)
    content = rng.randbytes(size)
    rig.write(buffer, content)
    rig.put_words(base, [buffer, size, capacity])
    other = 0x96_BC70 if desc else 0x96_BC7C
    sibling = rig.read_words(other, 3)
    offset = rng.choice((0, 4, 0x10, 0x3C))
    record = rig.m.alloc(0x80)
    rig.write(record, rng.randbytes(0x80))
    before = rig.m.read(record, 0x80)
    has_record, has_text = rng.random() < 0.9, rng.random() < 0.85
    length = rng.choice((-3, -1, 0, 1, 7, 33, 300, 0x400))
    data = rng.randbytes(0x400)
    rig.parse_script, rig.parse_calls = (length, data), []
    text = rig.m.put(b"1+level*2\0")
    rig.run(DESC_CODE if desc else SKILL_CODE, offset, 0x40, rng.randrange(50), rng.randrange(50),
            ecx=text if has_text else 0, edx=record if has_record else 0, nstack=4)
    callbacks = (0x61_1930, 0x61_19E0, 0x61_19F0) if not mut("skill-callbacks") else (0x61_1930, 0x61_19F0, 0x61_19E0)
    parses = has_text and (has_record or mut("skill-null-record-parses"))
    want_calls = [(text, 0x400) + callbacks + (True,)] if parses else []
    require(rig.parse_calls == want_calls, f"parse calls {rig.parse_calls} != {want_calls}")
    compiled = has_record and has_text and length > 0
    want = bytearray(before)
    if has_record:
        stored = None
        if compiled:
            stored = size + length if mut("skill-stores-new-size") else size
        elif not (mut("skill-text-null-kept") and not has_text) and not (mut("skill-nonpositive-kept") and has_text):
            stored = MASK
        if stored is not None:
            struct.pack_into("<I", want, offset, stored)
        require(rig.m.read(record, 0x80) == bytes(want), "skill code record bytes")
    held = rig.read_words(base, 3)
    new_size = size + length if compiled else size
    require(held[1] == new_size, f"skill code size {held[1]} != {new_size}")
    want_data = content + (data[:length] if compiled else b"")
    require(rig.m.read(held[0], new_size) == want_data, "skill code buffer")
    require(rig.read_words(other, 3) == sibling, "skill code touched the other code buffer")


def format_expect(text, size, prefix_fill=PATTERN):
    """The bytes a size-limited _snprintf then `buffer[size - 1] = 0` leave in a size + 8 byte guard buffer."""
    out = bytearray([prefix_fill]) * (size + 8)
    raw = text.encode()[:size]
    out[:len(raw)] = raw
    if len(text.encode()) < size:
        out[len(raw)] = 0
    if not mut("name-truncate-off-by-one"):
        out[size - 1] = 0
    else:
        out[size - 2] = 0
    return bytes(out)


def copy_expect(name, size):
    """The bytes a strncpy of the name into a size + 8 byte guard buffer plus the forced terminator leave."""
    out = bytearray([PATTERN]) * (size + 8)
    raw = name[:size]
    out[:size] = raw + b"\0" * (size - len(raw))
    out[size - 1] = 0 if not mut("player-no-terminator") else out[size - 1]
    return bytes(out)


def name_rig():
    """A world for the class-name getters, with the lookups hooked."""
    rig = Plain()
    rig.m.crt_init()
    rig.state = {"locale": 0, "object": 0, "item": 0}
    rig.m.hook(GET_LOCALE, lambda c: (rig.log.append(("locale", reg(c, "ecx"))), rig.state["locale"])[1], 0)
    rig.m.hook(WIDE_TO_MULTI, lambda c: (rig.log.append(("wide", reg(c, "ecx"), reg(c, "edx"), c.arg(0))), 0)[1], 1)
    rig.m.hook(OBJECT_LINE, lambda c: (rig.log.append(("object", c.arg(0))), rig.state["object"])[1], 1)
    rig.m.hook(ITEM_LINE, lambda c: (rig.log.append(("item", c.arg(0))), rig.state["item"])[1], 1)
    return rig


def sizes(rng):
    """A buffer size: unusable, tiny, the length of the longer names, large."""
    return rng.choice((-5, 0, 1, 2, 3, 7, 8, 9, 12, 13, 16, 40))


def check_player(rig, rng):
    """FUN_00611ee0: CharStats name or `player %d`."""
    count = rng.choice((0, 1, 3, 7))
    table = rig.m.alloc(0xC4 * 8 + 0x20)
    names = []
    for i in range(8):
        name = rng.choice((b"", b"Amazon", b"Sorceress", b"Necromancer", b"Barbarian_extra", b"Za"))
        names.append(name)
        rig.write(table + 0xC4 * i + 0x20, name + b"\0")
    items = rig.m.alloc(0xC00 + 0x20)
    rig.put_words(items + 0xBC4, [table, count])
    rig.m.w32(TABLES, items)
    ident = rng.choice((-3, -1, 0, 1, 2, 3, 6, 7, 8, 100))
    size = sizes(rng)
    buf = rig.m.alloc(max(size, 0) + 16, zero=False)
    rig.write(buf, bytes([PATTERN]) * (max(size, 0) + 16))
    rig.run(PLAYER, ecx=ident & MASK, regs={"edi": buf, "esi": size & MASK}, nstack=0)
    got = rig.m.read(buf, max(size, 0) + 8) if size > 0 else rig.m.read(buf, 16)
    if size <= 0:
        require(got == bytes([PATTERN]) * 16, "player wrote with a size of 0 or less")
        return
    bound = ident <= count if mut("player-bound-le") else ident < count
    valid = ident >= 0 and bound and ident < 8 and (names[ident] != b"" or mut("player-empty-name"))
    want = copy_expect(names[ident], size) if valid else format_expect(("player %d" if not mut("player-format") else "player %x") % ident, size)
    require(got == want, f"player {ident} size {size}: {got!r} != {want!r}")


def check_monster(rig, rng):
    """FUN_00611f40: MonStats name string through the locale table, else `monster %d`."""
    count = rng.choice((0, 1, 3, 6))
    table = rig.m.alloc(0x1A8 * 8 + 0x20)
    ids = []
    for i in range(8):
        value = rng.choice((0x1506, 0x1507, 0x2345, 0, 0xFFFF, rng.randrange(1, 60000)))
        ids.append(value)
        rig.write(table + 0x1A8 * i + 6, struct.pack("<H", value))
    items = rig.m.alloc(0xC00 + 0x20)
    rig.m.w32(items + 0xA78, table)
    rig.m.w32(items + 0xA80, count)
    rig.m.w32(TABLES, items)
    rig.state["locale"] = rng.randrange(1 << 20, 1 << 28)
    ident = rng.choice((-2, -1, 0, 1, 2, 5, 6, 7, 9))
    size = sizes(rng)
    buf = rig.m.alloc(max(size, 0) + 16, zero=False)
    rig.write(buf, bytes([PATTERN]) * (max(size, 0) + 16))
    rig.run(MONSTER, ecx=0, edx=ident & MASK, regs={"edi": buf, "esi": size & MASK}, nstack=0)
    got = rig.m.read(buf, max(size, 0) + 8) if size > 0 else rig.m.read(buf, 16)
    if size <= 0:
        quiet = got == bytes([PATTERN]) * 16 and rig.log == []
        require(quiet != mut("monster-no-size-test"), f"monster with size {size}: {rig.log}")
        return
    sentinel = 0x1506 if not mut("monster-sentinel") else 0x1507
    if 0 <= ident < count and ident < 8 and ids[ident] != sentinel:
        want_log = [("locale", ids[ident]), ("wide", buf, rig.state["locale"], size & MASK)]
        if mut("monster-wide-args"):
            want_log[1] = ("wide", rig.state["locale"], buf, size & MASK)
        require(rig.log == want_log, f"monster {ident} log {rig.log} != {want_log}")
        return
    require(rig.log == [], f"monster fallback made calls {rig.log}")
    require(got == format_expect("monster %d" % ident, size), f"monster {ident} size {size}: {got!r}")


def check_object(rig, rng):
    """FUN_00611fa0: an Objects.txt row's name, else `object %d`."""
    size = sizes(rng)
    ident = rng.choice((-1, 0, 1, 5, 130, 399))
    row = rng.choice((0, rig.m.alloc(0x40)))
    name = rng.choice((b"", b"Barrel", b"Waypoint_portal_x"))
    if row:
        rig.write(row, name + b"\0")
    rig.state["object"] = row
    buf = rig.m.alloc(max(size, 0) + 16, zero=False)
    rig.write(buf, bytes([PATTERN]) * (max(size, 0) + 16))
    rig.run(OBJECT, regs={"ebx": ident & MASK, "edi": buf, "esi": size & MASK}, nstack=0)
    if size <= 0:
        require(rig.log == [] and rig.m.read(buf, 16) == bytes([PATTERN]) * 16, "object with a size of 0 or less")
        return
    require(rig.log == [("object", ident & MASK)], f"object log {rig.log}")
    got = rig.m.read(buf, size + 8)
    present = bool(row) and (name != b"" or mut("object-empty-name")) and not mut("object-row-offset")
    if present:
        require(got == copy_expect(name, size), f"object name {got!r}")
    else:
        require(got == format_expect("object %d" % ident, size), f"object fallback {got!r}")


def check_item(rig, rng):
    """FUN_00611fe0: an item's name string, else `item %d`; the name string path ignores the size."""
    size = sizes(rng)
    ident = rng.choice((-1, 0, 1, 77, 500))
    row = rng.choice((0, rig.m.alloc(0x100)))
    value = rng.choice((0x1506, 0x1507, 0x777, 0x4000))
    if row:
        rig.write(row + 0xF4, struct.pack("<H", value))
        rig.write(row + 0xF6, struct.pack("<H", 0x1506))
    rig.state["item"] = row
    rig.state["locale"] = rng.randrange(1 << 20, 1 << 28)
    buf = rig.m.alloc(max(size, 0) + 16, zero=False)
    rig.write(buf, bytes([PATTERN]) * (max(size, 0) + 16))
    rig.run(ITEM, regs={"ebx": ident & MASK, "edi": buf, "esi": size & MASK}, nstack=0)
    seen = 0x1506 if mut("item-name-offset") else value
    sentinel = 0x1507 if mut("item-sentinel") else 0x1506
    want_log = [("item", ident & MASK)]
    if row and seen != sentinel:
        want_log += [("locale", seen), ("wide", buf, rig.state["locale"], size & MASK)]
        require(rig.log == want_log, f"item name string log {rig.log} != {want_log}")
        return
    require(rig.log == want_log, f"item fallback log {rig.log}")
    if size <= 0:
        untouched = rig.m.read(buf, 16) == bytes([PATTERN]) * 16
        require(untouched != mut("item-size-test"), "item fallback with a size of 0 or less")
        return
    require(rig.m.read(buf, size + 8) == format_expect("item %d" % ident, size), "item fallback bytes")


def check_missile(rig, rng):
    """FUN_00612030: `missile %d`."""
    size = sizes(rng)
    ident = rng.choice((-1, 0, 5, 300, 0x7FFFFFFF))
    buf = rig.m.alloc(max(size, 0) + 16, zero=False)
    rig.write(buf, bytes([PATTERN]) * (max(size, 0) + 16))
    rig.run(MISSILE, regs={"eax": ident & MASK, "edi": buf, "esi": size & MASK}, nstack=0)
    if size <= 0:
        require(rig.m.read(buf, 16) == bytes([PATTERN]) * 16, "missile wrote with a size of 0 or less")
        return
    shown = ident & MASK if mut("missile-format") else ident
    require(rig.m.read(buf, size + 8) == format_expect("missile %d" % shown, size), "missile bytes")


def with_misc(rig, rng):
    """Set 0x744308 (the offset-by-four table switch) and return whether it is on."""
    on = rng.random() < 0.5
    rig.m.w32(MISC_FLAG, rng.choice((1, 7, 0x100)) if on else 0)
    return on


def table_free(pointer, on, mutation_line=None):
    """The (block, line) a table free passes: the pointer or four before it, line 0x924 or 0x926."""
    if on:
        return pointer - (4 if not mut("skills-misc-offset") else 8), 0x924 if not mut("states-misc-line") else 0x925
    return pointer, 0x926


def check_skills_free(rig, rng):
    """FUN_00617c30: free the derived arrays and the tables, clear what the code clears."""
    on = with_misc(rig, rng)
    arrays = [(0x96_C7D4, 0x5B8), (0x96_C7DC, 0x5BE), (0x96_C7E4, 0x5C5), (0x96_BC70, 0x5CC), (0x96_BC7C, 0x5D4)]
    rig.scramble()
    values = {a: rng.choice((0, rng.randrange(1, 1 << 32))) for a, _ in arrays}
    skills, desc = rng.choice((0, rng.randrange(8, 1 << 32))), rng.choice((0, rng.randrange(8, 1 << 32)))
    links = rng.randrange(1, 1 << 32), rng.randrange(1, 1 << 32)
    for address, value in values.items():
        rig.m.w32(address, value)
    rig.put_words(0x96_C7C8, [skills])
    rig.m.w32(0x96_C7CC, links[0])
    rig.m.w32(0x96_C7BC, desc)
    rig.m.w32(0x96_C7C0, links[1])
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(SKILLS_FREE)
    require(got[0] == "ret", f"skills free {got}")
    want_log, want = [], {}
    for address, line in arrays:
        if values[address]:
            want_log.append(("free", 0, values[address], DATATBLS_FILE, line + (1 if mut("skills-free-line") and line == 0x5B8 else 0), 0))
            if not mut("skills-keeps-pointer") or line != 0x5BE:
                want[address] = 0
    for address in (0x96_C7D8, 0x96_C7E0, 0x96_BC74, 0x96_BC78, 0x96_BC80, 0x96_BC84, 0x96_C7D0, 0x96_C7C4):
        want[address] = 0
    if skills:
        block, line = table_free(skills, on)
        want_log.append(("free", 0, block, DATATBLS_FILE, line, 0))
    if not mut("skills-no-link-free"):
        want_log.append(("link-free", links[0]))
    if desc:
        block, line = table_free(desc, on)
        want_log.append(("free", 0, block, DATATBLS_FILE, line, 0))
    want_log.append(("link-free", links[1]))
    if mut("skills-clears-table"):
        want[0x96_C7C8] = 0
    require(rig.log == want_log, f"skills free log {rig.log} != {want_log}")
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, f"skills free stores {sorted(map(hex, rig.changed(before)))} != {sorted(map(hex, expect))}")


def check_states_free(rig, rng):
    """FUN_00618c90: free the five state arrays, clear 0xa0 bytes, free the table and its link."""
    on = with_misc(rig, rng)
    arrays = [(0x96_BCF8, 0x6E8), (0x96_BD9C, 0x6ED), (0x96_BDA4, 0x6F2), (0x96_BDAC, 0x6F7), (0x96_BDB4, 0x6FC)]
    rig.scramble()
    values = {a: rng.choice((0, rng.randrange(1, 1 << 32))) for a, _ in arrays}
    table, link = rng.choice((0, rng.randrange(8, 1 << 32))), rng.randrange(1, 1 << 32)
    for address, value in values.items():
        rig.m.w32(address, value)
    rig.m.w32(0x96_BCEC, table)
    rig.m.w32(0x96_BCF0, link)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(STATES_FREE)
    require(got[0] == "ret", f"states free {got}")
    want_log, want = [], {}
    for address, line in arrays:
        if values[address]:
            want_log.append(("free", 0, values[address], DATATBLS_FILE, line + (1 if mut("states-line") and line == 0x6F7 else 0), 0))
            if not mut("states-keeps-pointer") or line != 0x6F7:
                want[address] = 0
    span = 0xA0 if not mut("states-memset-short") else 0x9C
    for address in range(0x96_BCFC, 0x96_BCFC + span, 4):
        want[address] = 0
    if table:
        block, line = table_free(table, on)
        want_log.append(("free", 0, block, DATATBLS_FILE, line, 0))
    want_log.append(("link-free", link))
    require(rig.log == want_log, f"states free log {rig.log} != {want_log}")
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, f"states free stores {sorted(map(hex, rig.changed(before)))} != {sorted(map(hex, expect))}")


# FUN_00619140 in call order: ("call", address) or ("table", pointer slot) frees; the link and animation
# data arguments are named where they apply.
FREE_PLAN = ([("call", 0x61_2EA0), ("table", 0x96_C7F4), ("call", 0x66_3210), ("call", 0x66_4530), ("call", 0x61_7C30),
              ("call", 0x63_85D0), ("call", 0x61_8C90), ("table", 0x96_C818), ("link", 0x96_C81C), ("call", 0x63_3540),
              ("call", 0x63_8AA0), ("call", 0x63_3EA0), ("call", 0x63_4220), ("call", 0x63_4D90), ("call", 0x63_6730),
              ("call", 0x63_6B00), ("call", 0x63_75A0), ("call", 0x63_72A0), ("call", 0x63_7490), ("call", 0x63_78F0),
              ("call", 0x63_8C80), ("call", 0x63_93E0), ("call", 0x65_86D0), ("call", 0x65_B3C0), ("table", 0x96_C8A8),
              ("anim", 0x96_C8A4), ("call", 0x65_A190), ("call", 0x61_DC60), ("call", 0x61_E440), ("call", 0x61_EA90),
              ("call", 0x61_F030), ("call", 0x61_F300), ("call", 0x61_F480), ("call", 0x61_FBF0), ("call", 0x61_F9C0),
              ("call", 0x62_00D0), ("call", 0x64_0EE0), ("call", 0x64_0F00), ("call", 0x64_1510), ("call", 0x65_C130),
              ("call", 0x63_9C90), ("call", 0x66_A420), ("call", 0x66_0C60), ("call", 0x65_4FC0), ("call", 0x66_4AF0),
              ("call", 0x66_A1A0), ("call", 0x66_5740), ("call", 0x66_A280), ("table", 0x96_C8AC)])


def check_free_tables(rig, rng):
    """FUN_00619140: every unloader, in order, with the four table frees and the link and animation data between."""
    on = with_misc(rig, rng)
    slots = {slot: rng.choice((0, rng.randrange(8, 1 << 32))) for kind, slot in FREE_PLAN if kind in ("table", "link", "anim")}
    for slot, value in slots.items():
        rig.m.w32(slot, value)
    got = rig.run(FREE_TABLES, nstack=0)
    del got
    plan = list(FREE_PLAN)
    if mut("free-tables-order"):
        plan[2], plan[3] = plan[3], plan[2]
    if mut("free-tables-skip-last"):
        plan = [p for p in plan if p != ("call", 0x66_A280)]
    want = []
    for kind, slot in plan:
        if kind == "call":
            want.append(("call", slot))
        elif kind == "table" and slots[slot]:
            offset = {0x96_C8A8: 8 if mut("free-tables-experience-offset") else 4}.get(slot, 4)
            block = slots[slot] - offset if on else slots[slot]
            line = 0x924 if on else 0x926
            if mut("free-tables-difficulty-line") and slot == 0x96_C8AC:
                line += 1
            want.append(("free", 0, block, DATATBLS_FILE, line, 0))
        elif kind == "link":
            want.append(("link-free", slots[slot]))
        elif kind == "anim":
            want.append(("anim", slots[slot] if not mut("free-tables-animation-arg") else slots[slot] + 4))
    require(rig.log == want, f"free tables log differs at {next((i for i, (a, b) in enumerate(zip(rig.log, want)) if a != b), len(want))}: "
            f"{rig.log[:3]} vs {want[:3]} ({len(rig.log)} vs {len(want)})")


def check_experience(rig, rng):
    """FUN_00613d30: compile `experience` (8 INT columns, record 0x20) into 0x96c8a8."""
    before = rig.scramble()
    pool = rng.randrange(1, 1 << 32)
    rig.rows, rig.table = 0, rng.randrange(1, 1 << 32)
    got = rig.go(EXPERIENCE, pool)
    names = ("Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "ExpRatio")
    columns = [(name, INT, 0, 4 * i if not (mut("experience-offset") and i == 7) else 0x20, 0) for i, name in enumerate(names)]
    record = 0x20 if not mut("experience-record") else 0x24
    slot = 0x96_C8A8 if not mut("experience-store") else 0x96_C8AC
    check_compile_call(rig, got, [("compile", pool, "experience", 0, record)], schema(*columns), {slot: rig.table}, before,
                       "experience")


def check_lvltypes(rig, rng):
    """FUN_0061e520: compile `lvltypes` (32 file names, Act, Expansion) and fix each name's slashes."""
    before = rig.scramble()
    rows = rng.choice((0, 1, 2, 3, -1, -5))
    pool = rng.randrange(1, 1 << 32)
    rig.rows, rig.table = rows & MASK, rng.randrange(1 << 20, 1 << 28)
    got = rig.go(LVLTYPES, ecx=pool)
    stride = 0x3C if not mut("lvltypes-stride") else 0x3B
    columns = [(f"File {i + 1}", ASCII, 0x3B, stride * i, 0) for i in range(32)]
    columns += [("Act", CHAR if not mut("lvltypes-act-type") else INT, 0, 0x780, 0), ("Expansion", INT, 0, 0x784, 0)]
    count_slot = 0x96_C9FC if not mut("lvltypes-count-slot") else 0x96_C9F8
    want_log = [("compile", pool, "lvltypes", count_slot, 0x788)]
    for row in range(max(rows, 0)):
        for name in range(31 if mut("lvltypes-skip-last-file") else 32):
            want_log.append(("fix", rig.table + 0x788 * row + 0x3C * name, 0x6E_867C))
    check_compile_call(rig, got, want_log, schema(*columns), {0x96_C9F0: rig.table, 0x96_C9FC: rows & MASK}, before, "lvltypes")
    require(rig.m.cstr(0x6E_867C) == "DATA\\GLOBAL\\TILES", "lvltypes slash string")


def check_expansion(rig, rng):
    """FUN_0061fae0: allocate the per-row tile blocks and open each row's files up to the first named 0."""
    rows = rng.choice((0, 1, 2, 3, 4))
    table = rig.m.alloc(max(rows, 1) * 0x788 + 0x20)
    have_table = rng.random() < 0.9
    flags, names = [], []
    pool = [b"0", b"00", b"0x", b"", b"DATA\\a.tg1", b"tiles\\b", b"1", b"\\0", b"0\\", b"zz"]
    for row in range(rows):
        flag = rng.choice((0, 0, 1, 0xFF0000))
        flags.append(flag)
        row_names = []
        for index in range(32):
            name = rng.choice(pool) if rng.random() < 0.6 else rng.choice(pool[4:])
            if rng.random() < 0.08:
                name = b"0"
            row_names.append(name)
            rig.write(table + 0x788 * row + 0x3C * index, name + b"\0" + b"\xCC" * 4)
        names.append(row_names)
        rig.m.w32(table + 0x788 * row + 0x784, flag)
    expansion = rng.choice((0, 1, 2))
    rig.expansion = expansion
    rig.m.w32(0x96_C9F0, table if have_table else 0)
    rig.m.w32(0x96_C9FC, rows)
    rig.m.w32(0x96_CA1C, 0x1234)
    slots = rig.slots
    slots.clear()
    got = rig.go(EXPANSION_SETS)
    if not have_table and not mut("expansion-no-fatal"):
        require(got == ("halt", 0x410), f"expansion without a table {got}")
        return
    require(got[0] == "ret", f"expansion {got} (log {rig.log})")
    size = rows << 12 if not mut("expansion-alloc-size") else rows << 11
    block = rig.m.r32(0x96_CA1C)
    require(rig.log[0] == ("alloc", 0, size, LVLTBLS_FILE, 0x413, 0), f"expansion alloc {rig.log[0]}")
    require(rig.log[1:] == [("expansion",)], f"expansion log {rig.log[1:]}")
    require(rig.m.read(block, size) == bytes(size), "expansion block not cleared")
    want = []
    for row in range(rows):
        for index in range(32):
            name = names[row][index]
            if name == b"0" and not mut("expansion-stops-at-zero"):
                break
            if name == b"" and mut("expansion-skips-empty"):
                continue
            use = expansion != 0 or flags[row] == 0
            if mut("expansion-flag-ignored"):
                use = True
            if mut("expansion-classic-allows"):
                use = expansion == 0 or flags[row] == 0
            if use:
                stride = 0x1000 if not mut("expansion-block-stride") else 0x800
                want.append((block + stride * row, table + 0x788 * row + 0x3C * index))
    require(slots == want, f"expansion slots {slots[:3]} != {want[:3]} ({len(slots)} vs {len(want)})")


class LevelRig(Rig):
    """The monster-loader rig plus the lvltypes name fixer hooked."""

    def __init__(self):
        """Hook the slash fixer (ecx name, edi string)."""
        super().__init__()
        self.logger(FIX_SLASHES, 0, "fix", regs=("ecx", "edi"))


def run_all():
    """Run every check on random inputs; returns the case count."""
    rng = random.Random(0x6118B0)
    cases = 0
    rig = Plain()
    for _ in range(400):
        check_append(rig, rng)
        cases += 1
    rig = Plain()
    for index in range(300):
        check_skill_code(rig, rng, index % 2 == 1)
        cases += 1
    rig = name_rig()
    for check, rounds in ((check_player, 200), (check_monster, 200), (check_object, 150), (check_item, 150), (check_missile, 100)):
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    rig = LevelRig()
    for _ in range(150):
        check_skills_free(rig, rng)
        check_states_free(rig, rng)
        cases += 2
    for _ in range(60):
        check_experience(rig, rng)
        check_lvltypes(rig, rng)
        cases += 2
    rig = FreeRig()
    for _ in range(80):
        check_free_tables(rig, rng)
        cases += 1
    rig = ExpansionRig()
    for _ in range(150):
        check_expansion(rig, rng)
        cases += 1
    return cases


class FreeRig(Plain):
    """A world with every FUN_00619140 callee hooked to log its address."""

    def __init__(self):
        """Hook each unloader, the link free and the animation free."""
        super().__init__()
        for kind, address in FREE_PLAN:
            if kind == "call":
                self.m.hook(address, lambda c, a=address: (self.log.append(("call", a)), 0)[1], 0)
        self.m.hook(0x6B_D0B0, lambda c: (self.log.append(("link-free", c.arg(0))), 0)[1], 1)
        self.m.hook(0x66_A830, lambda c: (self.log.append(("anim", reg(c, "ecx"))), 0)[1], 0)


class ExpansionRig(Rig):
    """The monster-loader rig for FUN_0061fae0 (allocation, the expansion test, the slot loader)."""

    def __init__(self):
        """The base rig already hooks D2CheckExpansionGame (logged as `expansion`); add the slot loader."""
        super().__init__()
        self.slots = []
        self.m.hook(ALLOC_SLOT, lambda c: (self.slots.append((c.arg(0), c.arg(1))), 0)[1], 2)


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "more DATATBLS function cases")


if __name__ == "__main__":
    main()
