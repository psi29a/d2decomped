# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 19 MonsterTbls.cpp loaders, freers and getters (.\\DATATBLS\\MonsterTbls.cpp) in stock game.exe 1.14d.

    uv run python monster_loaders.py [--mutation-check]

Each loader builds a `D2BinFieldInputStrc` list (5 dwords an entry: name, type,
size, offset, linker) and hands it to CompileTxt 0x61_22f0. CompileTxt is hooked
here: it records the list, the table name, the record size and the row-count
address, writes a scripted row count and returns a scripted table. The link
creator 0x6b_d040, the table / link frees 0x61_25c0 / 0x6b_d0b0, the pool free
0x40_b480, the fatal path, `D2CheckExpansionGame` 0x40_8f20 and the string
table lookup 0x52_4d30 are hooked and logged; the C runtime's `strchr` runs for
real. Each loader and freer case snapshots the data page 0x96b000..0x96e000
(filled with random dwords) before and after the call and requires the changed dwords to be exactly
the ones the model writes, so a stray write or a missed one fails.

* FUN_00653db0 (ECX pool, EDX name; stack n): n < 4 compiles the table `name`
  (one `Name` KEY_WORD column, linker 0x61_17b0, record 2) into `0x96d488 + 4n`,
  its row count into `0x96d498 + 4n`; n >= 4 (unsigned, so -1 too) does nothing.
* FUN_00653e20: frees the four unique-name tables, clearing pointer and count.
* FUN_00654d50: frees the treasure class link, each class's items (`+0x28`,
  lines 0x643) and the class array (0x647), zeroes the array and count.
* FUN_00654e80 (difficulty, act, chest): the parsed treasure class at index
  `15 d + 3 a + c`, each clamped (d 0..2, a 0..4, c 0..2; <= 0 is 0).
* FUN_00654f20: `monitempercent` (4 CHAR columns, record 4) into `0x96d484`.
* FUN_00655030: `monumod` (16 columns, record 0x20, a link at line 0x6ff) and
  the row count capped at 0x100 (signed compare).
* FUN_006556e0 (n): the superunique row (0x34 bytes) or 0 for n >= count.
* FUN_00655720: `hireling` (70 columns, record 0x118); all 512 id-line dwords
  `0x96bdd8..0x96c5d8` set to -1; per row the NameFirst / NameLast string ids
  to `+0x114 / +0x116`; with the expansion flag three fatal checks (lines
  0x816 first id zero, 0x817 last id not above first, 0x818 row id >= 0x100);
  the first row of each (version >= 100, id) goes into the id lines.
* FUN_006562b0: unloads hireling (the pointer stays), count 0, id lines -1.
* FUN_006564d0 (expansion, seller, difficulty, previous row): the next
  hireling row after the previous one whose `+0x14` is seller, `+0x10` is
  difficulty + 1 and version word is 0 / 100.
* FUN_00656620: `npc` (20 columns, record 0x4c).
* FUN_00656fc0 (n): the monsounds row of MonStats row n's `+0x14` word.
* FUN_006586d0: frees MonType (table, link, extra block at line 0xb16).
* FUN_00657010 (ECX text, EDX record; stack slot, 3 link values): the
  MonStats2 component list, see `check_components`.
* FUN_00659a10: `monpreset` (Act CHAR, Place LINKER_BIT, record 4) and the
  per-act first-row / count tables `0x96c6dc..` / `0x96c6f0..`.
* FUN_00659af0: unloads monpreset and clears the per-act tables.
* FUN_00659bb0: `sounds` (one NAME_I_WORD column, record 2, link at line
  0xc99), only with the compile flag `0x96c8b4`.
* FUN_00659de0: frees MonSeq (table, link, count) and its extra block (0xcea).
* FUN_0065a390 (pool): link at line 0x60e, three loader calls, fatal 0x616 when
  the parsed treasure class count is 0xffff or more (signed).

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail. Verified for the values tested; the hooked callees
(0x65_41c0, 0x65_47d0, 0x65_a2c0, GetClassIdFromName, the string lookup) are
checked for call order and arguments only.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import Mismatch, require

FILE = ".\\DATATBLS\\MonsterTbls.cpp"
COMPILE, CREATE_LINK, CLASS_ID, UNLOAD, LINK_FREE = 0x61_22F0, 0x6B_D040, 0x6B_D130, 0x61_25C0, 0x6B_D0B0
EXPANSION, LOOKUP = 0x40_8F20, 0x52_4D30
(UNIQUE_LOAD, UNIQUE_FREE, PARSED_FREE, ROLL_TC, BODY_LOCS, MONUMOD, SUPER_ROW, HIRELING, HIRE_FREE, HIRE_NEXT, NPC,
 SOUND_OF, MONTYPE_FREE, COMPONENTS, PRESET, PRESET_FREE, SOUNDS, MONSEQ_FREE, DROP_DATA) = (
    0x653DB0, 0x653E20, 0x654D50, 0x654E80, 0x654F20, 0x655030, 0x6556E0, 0x655720, 0x6562B0, 0x6564D0, 0x656620,
    0x656FC0, 0x6586D0, 0x657010, 0x659A10, 0x659AF0, 0x659BB0, 0x659DE0, 0x65A390)
REGION, REGION_END = 0x96B000, 0x96E000
# column types (eD2BinFieldInputType)
END, ASCII, INT, SHORT, CHAR, CODE_CHAR, NAME_I_WORD, NAME_INT, NAME_SHORT, KEY_WORD, LINKER_BIT = (
    0, 1, 2, 3, 4, 13, 17, 19, 20, 22, 23)
MASK = 0xFFFFFFFF
STEP_LIMIT = 200000
MUTATIONS = ("unique-n-le-4", "unique-linker", "unique-free-keeps-count", "parsed-free-skip-items", "parsed-free-keeps-array",
             "roll-act-3", "roll-neg-abs", "roll-chest-3", "body-offset", "monumod-cap-unsigned", "monumod-no-cap",
             "monumod-offset", "super-le", "hire-schema-offset", "hire-expansion-ignored", "hire-order-0x817",
             "hire-version-99", "hire-keeps-first", "hire-id-unsigned", "hire-free-clears-pointer", "hire-next-prev",
             "hire-next-version", "npc-offset", "sound-neg-unchecked", "montype-free-keeps-link", "comp-limit-13",
             "comp-pad-wrong", "comp-sum-no-minus", "comp-clamp-0xff", "comp-quote-first", "preset-act-le",
             "preset-free-clears-pointer", "sounds-flag-ignored", "monseq-free-clears-pointer", "drop-limit-0x10000",
             "drop-order")
mutation = None


class Halt(Exception):
    """game.exe's unrecoverable-error call, with the source line it passed."""


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def s32(value):
    """A dword as a signed number."""
    return value - (1 << 32) if value & 0x80000000 else value


def cstr_len(data, start):
    """Length of the NUL-terminated string at `start` (the buffer always ends in a NUL)."""
    end = start
    while data[end]:
        end += 1
    return end - start


class Rig(mw.World):
    """The emulated game with CompileTxt, the link / free calls and the fatal path hooked."""

    def __init__(self):
        """Hook CompileTxt, the link creator, the frees, the class lookup, the fatal path and the string lookup."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = STEP_LIMIT
        self.rows = 0
        self.table = 0
        self.links = []
        self.fields = []
        self.expansion = 0
        self.ids = []
        self.link_for = 0
        machine = self.m
        machine.hook(COMPILE, self.compile, 5)
        machine.hook(CREATE_LINK, self.create_link, 2)
        machine.hook(CLASS_ID, self.class_id, 3)
        machine.hook(LOOKUP, self.lookup, 2)
        self.logger(UNLOAD, 1, "unload", regs=())
        self.logger(LINK_FREE, 1, "link-free", regs=())
        self.logger(EXPANSION, 0, "expansion", ret=lambda _c: self.expansion, regs=())
        self.logger(0x65_41C0, 0, "tc-auto", regs=())
        self.logger(0x65_47D0, 1, "tc-load", regs=())
        self.logger(0x65_A2C0, 0, "tc-parsed", regs=())
        machine.hook(0x40_8090, lambda current: current.arg(0), 1)
        machine.hook(0x40_8A60, self.fatal, 0)

    def fatal(self, current):
        """The fatal-error call: arg 1 is the line the caller passed through 0x408090."""
        raise Halt(current.arg(1))

    def compile(self, current):
        """CompileTxt (stdcall: pool, name, fields, &rows, record size): record the call and its column list."""
        fields, entries = current.arg(2), []
        for index in range(100):
            entry = tuple(current.r32(fields + 20 * index + 4 * j) for j in range(5))
            entries.append((current.cstr(entry[0]) if entry[0] else "",) + entry[1:])
            if entry[1] == END:
                break
        self.fields = entries
        self.log.append(("compile", current.arg(0), current.cstr(current.arg(1)), current.arg(3), current.arg(4)))
        if current.arg(3):
            current.w32(current.arg(3), self.rows)
        return self.table

    def create_link(self, current):
        """STRUCT_CreateTxtLinkStrc (file, line): log it, return the next scripted link."""
        self.log.append(("link", current.cstr(current.arg(0)), current.arg(1)))
        return self.links.pop(0)

    def class_id(self, current):
        """GetClassIdFromName (stdcall: link, 4-char code, flag): log it, return a code-dependent number."""
        code = current.arg(1)
        self.log.append(("class", current.arg(0), code, current.arg(2)))
        return ((code * 2654435761) >> 7) & 0x3FF

    def lookup(self, current):
        """STRTABLE_LookupString (name, &out): log the name pointer, return the next scripted id."""
        self.log.append(("lookup", current.arg(0)))
        return self.ids.pop(0)

    def go(self, address, *args, ecx=0, edx=0, regs=None):
        """One call; ("halt", line) when the fatal path ran, ("loop", 0) when it never finished, else ("ret", eax)."""
        try:
            return ("ret", self.run(address, *args, ecx=ecx, edx=edx, regs=regs))
        except Halt as halt:
            return ("halt", halt.args[0])
        except mw.Fault as err:
            return ("fault", str(err))
        except Mismatch as err:
            if "did not return" in str(err):
                return ("loop", 0)
            raise

    def scramble(self):
        """Fill the data page with random dwords; returns a copy of it."""
        data = random.randbytes(REGION_END - REGION)
        self.write(REGION, data)
        return data

    def changed(self, before):
        """The dwords of the data page that differ from `before`, as {address: value}."""
        after = self.m.read(REGION, REGION_END - REGION)
        return {REGION + off: struct.unpack_from("<I", after, off)[0] for off in range(0, len(after), 4)
                if after[off:off + 4] != before[off:off + 4]}


def schema(*columns):
    """Column rows (name, type, size, offset, linker) plus the closing `end` row."""
    return list(columns) + [("end", END, 0, 0, 0)]


def check_compile_call(rig, got, want_log, want_fields, want_writes, before, what):
    """Compare return, log, column list and the exact set of data-page stores with the model."""
    require(got[0] == "ret", f"{what}: {got}")
    require(rig.log == want_log, f"{what}: log {rig.log} != {want_log}")
    require(rig.fields == want_fields, f"{what}: columns differ: {[f for f in rig.fields if f not in want_fields][:3]} vs "
            f"{[f for f in want_fields if f not in rig.fields][:3]}")
    stores = rig.changed(before)
    expect = {a: v & MASK for a, v in want_writes.items()}
    # a dword the model writes with the value already there is not a change
    for address in list(expect):
        if struct.unpack_from("<I", before, address - REGION)[0] == expect[address]:
            del expect[address]
    require(stores == expect, f"{what}: stores {sorted((hex(a), hex(v)) for a, v in stores.items())} != "
            f"{sorted((hex(a), hex(v)) for a, v in expect.items())}")


def rows_value(rng):
    """A scripted row count: small, around the monumod cap, zero and negative."""
    return rng.choice((0, 1, 7, 0xFF, 0x100, 0x101, 0x1000, MASK, 0xFFFFFFF0, rng.randrange(300)))


def check_unique(rig, rng):
    """FUN_00653db0: compile unique name table n (n < 4)."""
    before = rig.scramble()
    n = rng.choice((0, 1, 2, 3, 4, 5, MASK, rng.randrange(1 << 32)))
    pool, name = rng.randrange(1 << 32), rig.m.put(rng.choice((b"uniquetitle", b"x", b"")) + b"\0")
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(UNIQUE_LOAD, n, ecx=pool, edx=name)
    limit = 4 if not mut("unique-n-le-4") else 5
    if n >= limit:
        require(got[0] == "ret" and not rig.log and not rig.changed(before), f"n {n}: {got} {rig.log}")
        return
    linker = 0x61_17B0 if not mut("unique-linker") else 0x61_17B4
    want_fields = schema(("Name", KEY_WORD, 0, 0, linker))
    count_at = 0x96D498 + 4 * n
    check_compile_call(rig, got, [("compile", pool, rig.m.cstr(name), count_at, 2)], want_fields,
                       {0x96D488 + 4 * n: rig.table, count_at: rig.rows}, before, f"unique {n}")


def check_unique_free(rig, rng):
    """FUN_00653e20: free every non-null unique-name table and clear its pointer and count."""
    before = rig.scramble()
    pointers = [rng.choice((0, rng.randrange(1, 1 << 32))) for _ in range(4)]
    for index, value in enumerate(pointers):
        rig.m.w32(0x96D488 + 4 * index, value)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(UNIQUE_FREE)
    want_log, want = [], {}
    for index, value in enumerate(pointers):
        if value:
            want_log.append(("unload", value))
            want[0x96D488 + 4 * index] = 0
            if not mut("unique-free-keeps-count"):
                want[0x96D498 + 4 * index] = 0
    require(got[0] == "ret" and rig.log == want_log, f"unique free {got} {rig.log} != {want_log}")
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, "unique free: stores differ")


def check_parsed_free(rig, rng):
    """FUN_00654d50: free the treasure class link, each class's item array, then the class array."""
    rig.scramble()
    count = rng.choice((0, 1, 3, 8, -1, -5))
    items = [rng.choice((0, rng.randrange(1, 1 << 32))) for _ in range(max(count, 0))]
    image = b"".join(rng.randbytes(0x28) + struct.pack("<I", ptr) for ptr in items)
    array, link = rig.m.alloc(len(image) + 0x40), rng.randrange(1, 1 << 32)
    rig.write(array, image)
    rig.m.w32(0x96C5E8, link)
    rig.m.w32(0x96C5EC, array)
    rig.m.w32(0x96C5F0, count & MASK)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(PARSED_FREE)
    want_log = [("link-free", link)]
    if not mut("parsed-free-skip-items"):
        want_log += [("free", 0, ptr, FILE, 0x643, 0) for ptr in items if ptr]
    want_log.append(("free", 0, array, FILE, 0x647, 0))
    require(got[0] == "ret" and rig.log == want_log, f"parsed free {got} {rig.log} != {want_log}")
    want = {0x96C5F0: 0} if mut("parsed-free-keeps-array") else {0x96C5EC: 0, 0x96C5F0: 0}
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, "parsed free: stores differ")


def clamp(value, top):
    """The roll's clamp: <= 0 is 0, otherwise at most `top`."""
    value = s32(value)
    return 0 if value <= 0 else min(value, top)


def check_roll(rig, rng):
    """FUN_00654e80: pick the parsed treasure class at 15 d + 3 a + c."""
    pointers = [rng.randrange(1 << 32) for _ in range(45)]
    rig.put_words(0x96C5F4, pointers)
    d, a, c = (rng.choice((0, 1, 2, 3, 4, 5, 9, MASK, 0x80000000, rng.randrange(1 << 32), rng.randrange(8))) for _ in range(3))
    got = rig.go(ROLL_TC, d, a, c)
    dd, aa, cc = clamp(d, 2), clamp(a, 4 if not mut("roll-act-3") else 3), clamp(c, 2 if not mut("roll-chest-3") else 3)
    if mut("roll-neg-abs"):
        dd = min(abs(s32(d)), 2)
    index = 15 * dd + 3 * aa + cc
    require(0 <= index < 45, f"roll index {index} outside the table")
    want = pointers[index]
    require(got == ("ret", want) and not rig.log, f"roll({s32(d)}, {s32(a)}, {s32(c)}): {got} != {want:#x}")


def check_body_locs(rig, rng):
    """FUN_00654f20: the monitempercent table."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(BODY_LOCS, ecx=pool)
    third = 3 if not mut("body-offset") else 2
    want_fields = schema(("HeartPercent", CHAR, 0, 0, 0), ("BodyPartPercent", CHAR, 0, 1, 0),
                         ("TreasureClassPercent", CHAR, 0, 2, 0), ("ComponentPercent", CHAR, 0, third, 0))
    check_compile_call(rig, got, [("compile", pool, "monitempercent", 0x96D480, 4)], want_fields,
                       {0x96D480: rig.rows, 0x96D484: rig.table}, before, "monitempercent")


def check_monumod(rig, rng):
    """FUN_00655030: the monumod table, its link and the row-count cap."""
    before = rig.scramble()
    pool, link = rng.randrange(1 << 32), rng.randrange(1 << 32)
    rig.rows, rig.table, rig.links = rows_value(rng), rng.randrange(1 << 32), [link]
    got = rig.go(MONUMOD, ecx=pool)
    names = ("version", "enabled", "xfer", "champion", "fpick")
    offset = 4
    cols = [("uniquemod", NAME_I_WORD, 0, 0, 0x96C87C), ("version", SHORT, 0, offset, 0)]
    cols += [(n, CHAR, 0, 6 + i, 0) for i, n in enumerate(names[1:])]
    cols += [("exclude1", NAME_SHORT, 0, 10, 0x96C868), ("exclude2", NAME_SHORT, 0, 12 + (2 if mut("monumod-offset") else 0), 0x96C868)]
    for i, n in enumerate(("cpick", "cpick (N)", "cpick (H)", "upick", "upick (N)", "upick (H)")):
        cols.append((n, SHORT, 0, 14 + 2 * i, 0))
    cols.append(("constants", INT, 0, 0x1C, 0))
    count = s32(rig.rows)
    if mut("monumod-cap-unsigned"):
        capped = 0x100 if rig.rows > 0x100 else rig.rows
    elif mut("monumod-no-cap"):
        capped = rig.rows
    else:
        capped = 0x100 if count > 0x100 else rig.rows
    check_compile_call(rig, got, [("link", FILE, 0x6FF), ("compile", pool, "monumod", 0x96C884, 0x20)], schema(*cols),
                       {0x96C87C: link, 0x96C880: rig.table, 0x96C884: capped}, before, "monumod")


def check_super_row(rig, rng):
    """FUN_006556e0: superunique row n."""
    count, base = rng.choice((0, 1, 5, 66, MASK)), rng.randrange(1 << 32)
    rig.m.w32(0x96C70C, count)
    rig.m.w32(0x96C704, base)
    n = rng.choice((0, 1, 4, 5, 65, 66, 67, MASK, 0x80000000, rng.randrange(1 << 32), count & MASK))
    got = rig.go(SUPER_ROW, n)
    inside = s32(n) < s32(count) if not mut("super-le") else s32(n) <= s32(count)
    want = (base + 0x34 * s32(n)) & MASK if inside else 0
    require(got == ("ret", want) and not rig.log, f"super row {s32(n)} of {s32(count)}: {got} != {want:#x}")


def hire_columns():
    """The 70 hireling columns with the six skill groups spelled out."""
    cols = [("version", SHORT, 0, 0, 0)]
    cols += [(n, INT, 0, 4 * (i + 1), 0) for i, n in enumerate(("id", "class", "act", "difficulty", "seller", "gold", "level"))]
    cols += [("namefirst", ASCII, 32, 0xD3, 0), ("namelast", ASCII, 32, 0xF3, 0)]
    cols += [(n, INT, 0, 0x20 + 4 * i, 0) for i, n in enumerate((
        "exp/lvl", "hp", "hp/lvl", "defense", "def/lvl", "str", "str/lvl", "dex", "dex/lvl", "ar", "ar/lvl", "share",
        "dmg-min", "dmg-max", "dmg/lvl", "resist", "resist/lvl"))]
    cols += [("hiredesc", CODE_CHAR, 0, 0xD2, 0x96BCE8), ("defaultchance", INT, 0, 0x64, 0)]
    for k in range(6):
        cols += [(f"skill{k + 1}", NAME_INT, 0, 0x78 + 4 * k, 0x96C7CC), (f"mode{k + 1}", CHAR, 0, 0xC0 + k, 0),
                 (f"chance{k + 1}", INT, 0, 0x90 + 4 * k, 0), (f"chanceperlvl{k + 1}", INT, 0, 0xA8 + 4 * k, 0),
                 (f"level{k + 1}", CHAR, 0, 0xC6 + k, 0), (f"lvlperlvl{k + 1}", CHAR, 0, 0xCC + k, 0)]
    cols += [(n, INT, 0, 0x68 + 4 * i, 0) for i, n in enumerate(("head", "torso", "weapon", "shield"))]
    if mut("hire-schema-offset"):
        cols[9] = ("namelast", ASCII, 32, 0xF4, 0)
    return cols


def check_hireling(rig, rng):
    """FUN_00655720: schema, id lines and the NameFirst / NameLast ids per row."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    count = rng.choice((0, 1, 2, 4, 7, -2))
    rig.rows = count & MASK
    table = rig.m.alloc(0x118 * max(count, 1) + 0x20)
    rows = []
    for _ in range(max(count, 0)):
        row = bytearray(rng.randbytes(0x118))
        struct.pack_into("<H", row, 0, rng.choice((0, 1, 99, 100, 101, 0xFFFF, 0x8064)))
        struct.pack_into("<i", row, 4, rng.choice((0, 1, 5, 255, 256, 300, -1, -3, 0x7FFFFFFF, 12, 12)))
        rows.append(row)
    rig.write(table, b"".join(bytes(r) for r in rows))
    rig.table = table
    rig.expansion = rng.choice((0, 1, 5))
    rig.ids = [rng.choice((0, 1, 7, 0xABCD0000, rng.randrange(1 << 32), 0x10003, 5)) for _ in range(2 * max(count, 0))]
    ids = list(rig.ids)
    got = rig.go(HIRELING, ecx=pool)
    want_log = [("compile", pool, "hireling", 0x96BDD4, 0x118)]
    want = {0x96BDD0: table, 0x96BDD4: rig.rows}
    memory = {0x96BDD8 + 4 * i: MASK for i in range(512)}
    want_log.append(("expansion",))
    expansion = rig.expansion != 0 and not mut("hire-expansion-ignored")
    halt = None
    for index, row in enumerate(rows):
        want_log.append(("lookup", table + 0x118 * index + 0xD3))
        first = ids.pop(0) & 0xFFFF
        struct.pack_into("<H", row, 0x114, first)
        want_log.append(("lookup", table + 0x118 * index + 0xF3))
        last = ids.pop(0) & 0xFFFF
        struct.pack_into("<H", row, 0x116, last)
        row_id = struct.unpack_from("<i", row, 4)[0]
        if expansion:
            if mut("hire-order-0x817") and last <= first:
                halt = 0x817
            elif first == 0:
                halt = 0x816
            elif last <= first:
                halt = 0x817
            elif row_id >= 0x100:
                halt = 0x818
            if halt:
                break
        below = row_id < 0x100 if not (mut("hire-id-unsigned") and row_id < 0) else False
        if below:
            version = struct.unpack_from("<H", row, 0)[0]
            slot = (0x100 if version >= (99 if mut("hire-version-99") else 100) else 0) + row_id
            address = (0x96BDD8 + 4 * slot) & MASK
            current = memory.get(address, want.get(address))
            if current is None:
                current = struct.unpack_from("<I", before, address - REGION)[0] if REGION <= address < REGION_END else 0
            if s32(current) < 0 or mut("hire-keeps-first"):
                memory[address] = index
    want_state = ("halt", halt) if halt else ("ret", None)
    require(got[0] == want_state[0] and (not halt or got[1] == halt), f"hireling: {got} != {want_state}")
    require(rig.log == want_log, f"hireling: log {rig.log} != {want_log}")
    require(rig.fields == schema(*hire_columns()), "hireling: columns differ")
    require(rig.m.read(table, len(rows) * 0x118) == b"".join(bytes(r) for r in rows), "hireling: table rows differ")
    want.update(memory)
    expect = {a: v & MASK for a, v in want.items() if REGION <= a < REGION_END and
              struct.unpack_from("<I", before, a - REGION)[0] != v & MASK}
    actual = rig.changed(before)
    require(actual == expect, "hireling: data page stores differ: only stock "
            f"{sorted((hex(a), hex(v)) for a, v in actual.items() if expect.get(a) != v)[:4]}, only model "
            f"{sorted((hex(a), hex(v)) for a, v in expect.items() if actual.get(a) != v)[:4]}")


def check_hire_free(rig, rng):
    """FUN_006562b0: unload hireling, count 0, id lines -1; the pointer is left."""
    before = rig.scramble()
    pointer = rng.choice((0, rng.randrange(1, 1 << 32)))
    rig.m.w32(0x96BDD0, pointer)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(HIRE_FREE)
    want = {0x96BDD4: 0}
    want.update({0x96BDD8 + 4 * i: MASK for i in range(512)})
    if mut("hire-free-clears-pointer"):
        want[0x96BDD0] = 0
    require(got[0] == "ret" and rig.log == [("unload", pointer)], f"hire free {got} {rig.log}")
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, "hire free: stores differ")


def check_hire_next(rig, rng):
    """FUN_006564d0: the next hireling row after a previous one, by seller, difficulty and version."""
    count = rng.choice((0, 1, 4, 9, 20))
    pad = 4
    base_buf = rig.m.alloc(0x118 * (count + 2 * pad) + 0x40)
    table = base_buf + 0x118 * pad
    rows = bytearray(rng.randbytes(0x118 * (count + 2 * pad)))
    for index in range(-pad, count + pad):
        offset = 0x118 * (index + pad)
        struct.pack_into("<H", rows, offset, rng.choice((0, 100, 100, 0, 99, 101, 0xFFFF, 0x8064)))
        struct.pack_into("<I", rows, offset + 0x10, rng.choice((1, 2, 3, 1, 2)))
        struct.pack_into("<I", rows, offset + 0x14, rng.choice((0, 1, 2, 3, 1)))
    rig.write(base_buf, bytes(rows))
    rig.m.w32(0x96BDD0, table)
    rig.m.w32(0x96BDD4, count)
    expansion = rng.choice((0, 1, 7))
    seller, difficulty = rng.choice((0, 1, 2, 3)), rng.choice((0, 1, 2))
    prev = rng.choice((0, table, table + 0x118 * rng.randrange(max(count, 1)), table + 0x118 * 2 + 5,
                       table - 0x118 * 2 - 3, table + 0x118 * count, table - 0x118 + 1))
    got = rig.go(HIRE_NEXT, expansion, seller, difficulty, prev)
    index = 0
    if prev:
        diff = s32((prev - table) & MASK)
        index = int(diff / 0x118) + (0 if mut("hire-next-prev") else 1)    # trunc toward zero, like the compiler's idiv
    version = (100 if expansion else 0) if not mut("hire-next-version") else (100 if expansion else 1)
    want = 0
    while index < count:
        offset = 0x118 * (index + pad)
        if (struct.unpack_from("<I", rows, offset + 0x14)[0] == seller and
                struct.unpack_from("<I", rows, offset + 0x10)[0] == (difficulty + 1) and
                struct.unpack_from("<H", rows, offset)[0] == version):
            want = table + 0x118 * index
            break
        index += 1
    require(got == ("ret", want), f"hire next(exp {expansion}, seller {seller}, diff {difficulty}, prev {prev - table:+#x}): "
            f"{got} != {want:#x}")


def check_npc(rig, rng):
    """FUN_00656620: the npc table."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    rig.rows, rig.table = rows_value(rng), rng.randrange(1 << 32)
    got = rig.go(NPC, ecx=pool)
    cols = [("npc", NAME_INT, 0, 0, 0x96C6AC)]
    names = ("sell mult", "buy mult", "rep mult", "questflag A", "questflag B", "questflag C", "questsellmult A",
             "questsellmult B", "questsellmult C", "questbuymult A", "questbuymult B", "questbuymult C",
             "questrepmult A", "questrepmult B", "questrepmult C", "max buy", "max buy (N)", "max buy (H)")
    cols += [(n, INT, 0, 4 * (i + 1) + (4 if mut("npc-offset") and n == "max buy (H)" else 0), 0) for i, n in enumerate(names)]
    check_compile_call(rig, got, [("compile", pool, "npc", 0x96C5DC, 0x4C)], schema(*cols),
                       {0x96C5DC: rig.rows, 0x96C5D8: rig.table}, before, "npc")


def check_sound_of(rig, rng):
    """FUN_00656fc0: the monsounds row of MonStats row n."""
    stats_count, sounds_count = rng.choice((0, 1, 4, 9)), rng.choice((0, 1, 3, 8, MASK))
    pad = 3
    buf = rig.m.alloc(0x1A8 * (stats_count + 2 * pad) + 0x40)
    stats = buf + 0x1A8 * pad
    image = bytearray(rng.randbytes(0x1A8 * (stats_count + 2 * pad)))
    for index in range(-pad, stats_count + pad):
        struct.pack_into("<h", image, 0x1A8 * (index + pad) + 0x14, rng.choice((-1, 0, 1, 2, 7, -300, 4)))
    rig.write(buf, bytes(image))
    stats_ptr = rng.choice((stats, stats, 0))
    sounds_ptr = rng.choice((rng.randrange(0x1000, 1 << 30), rng.randrange(0x1000, 1 << 30), 0))
    for address, value in ((0x96C6A8, stats_ptr), (0x96C6B0, stats_count), (0x96C6B4, sounds_ptr), (0x96C6BC, sounds_count)):
        rig.m.w32(address, value & MASK)
    n = rng.choice((0, 1, stats_count - 1, stats_count, stats_count + 1, -1, -2, rng.randrange(-3, 12)))
    got = rig.go(SOUND_OF, n & MASK)
    want = 0
    if sounds_ptr and stats_ptr and n < stats_count:
        sound = struct.unpack_from("<h", image, 0x1A8 * (n + pad) + 0x14)[0]
        ok = 0 <= sound < s32(sounds_count)
        if mut("sound-neg-unchecked"):
            ok = sound < s32(sounds_count)
        if ok:
            want = (sounds_ptr + sound * 0x94) & MASK
    require(got == ("ret", want) and not rig.log, f"sound of({n}): {got} != {want:#x}")


def check_montype_free(rig, rng):
    """FUN_006586d0: free MonType's table, link and extra block, and clear the pointer and link."""
    rig.scramble()
    table = rng.choice((0, rng.randrange(1, 1 << 32)))
    link, extra = rng.randrange(1, 1 << 32), rng.randrange(1, 1 << 32)
    for address, value in ((0x96C86C, table), (0x96C868, link), (0x96C874, extra)):
        rig.m.w32(address, value)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(MONTYPE_FREE)
    want_log, want = [], {}
    if table:
        want_log = [("unload", table), ("link-free", link), ("free", 0, extra, FILE, 0xB16, 0)]
        want = {0x96C86C: 0}
        if not mut("montype-free-keeps-link"):
            want[0x96C868] = 0
    require(got[0] == "ret" and rig.log == want_log, f"montype free {got} {rig.log} != {want_log}")
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, "montype free: stores differ")


def token_text(rng):
    """A random component list as bytes: codes of 0..6 letters, commas, quotes, spaces."""
    parts = []
    for _ in range(rng.choice((0, 1, 1, 2, 3, 5, 12, 13, 14))):
        parts.append(bytes(rng.choice(b"abcxyz019 ") for _ in range(rng.choice((0, 1, 2, 3, 4, 4, 5, 6)))))
    text = b",".join(parts)
    kind = rng.random()
    if kind < 0.25:
        text = b'"' + text + b'"'
    elif kind < 0.35 and len(text) > 2:
        cut = rng.randrange(1, len(text))
        text = text[:cut] + b'"' + text[cut:]
    elif kind < 0.4:
        text = b'"' + text
    return text


def model_components(text, record, slot, link):
    """FUN_00657010 on a record image; returns the calls to GetClassIdFromName."""
    calls = []
    if slot >= 16:
        return calls
    record[0x15 + slot] = 0
    base = 0x26 + 12 * slot
    for i in range(12):
        record[base + i] = 0xFF
    if text is None:
        return calls
    data = bytearray(text + b"\0\0\0\0\0")
    position = 1 if data[0] == 0x22 and not mut("comp-quote-first") else 0
    if mut("comp-quote-first") and data[0] == 0x22:
        position = 0
    scan = position
    while data[scan]:
        if data[scan] == 0x22:
            data[scan] = 0
        scan += 1
    count = 0
    limit = 12 if not mut("comp-limit-13") else 13
    while True:
        end = position + cstr_len(data, position)
        comma = data.find(b",", position, end)
        following = None
        if comma >= 0:
            data[comma] = 0
            following = comma + 1
        if data[position] == 0:
            break
        length = cstr_len(data, position)
        if length > 4:
            break
        chars = bytearray(b"    ")
        chars[0] = data[position]
        if data[position + 1]:
            chars[1] = data[position + 1]
            if data[position + 2]:
                chars[2] = data[position + 2]
                if data[position + 3]:
                    chars[3] = data[position + 3]
        if mut("comp-pad-wrong") and length == 1:
            chars[1] = 0
        code = struct.unpack("<I", bytes(chars))[0]
        calls.append(("class", link, code, 1))
        record[base + count] = (((code * 2654435761) >> 7) & 0x3FF) & 0xFF
        count += 1
        if following is None or count >= limit:
            break
        position = following
    record[0x15 + slot] = count
    if slot == 15:
        total = 0
        for index in range(0x15, 0x25):
            value = record[index]
            if value > 1:
                total += value if mut("comp-sum-no-minus") else value - 1
        record[0x25] = min(total, 0xFF if mut("comp-clamp-0xff") else 0xFE)
    return calls


def check_components(rig, rng):
    """FUN_00657010: parse one component list into a MonStats2 record's slot."""
    machine = rig.m
    link = rng.randrange(1, 1 << 32)
    machine.w32(0x96BCB0, link)
    slot = rng.choice((0, 1, 7, 14, 15, 15, 15, 16, 17, MASK, rng.randrange(16)))
    image = bytearray(rng.randbytes(0x100))
    if rng.random() < 0.5 and slot == 15:
        for index in range(0x15, 0x25):       # frame counts the final sum reads
            image[index] = rng.choice((0, 0, 1, 2, 3, 5, 10, 0xFF) if rng.random() < 0.2 else (0, 0, 1, 1, 2, 3, 4))
    text = None if rng.random() < 0.08 else token_text(rng)
    record = machine.alloc(0x100)
    rig.write(record, bytes(image))
    text_ptr = 0 if text is None else machine.put(text + b"\0\0\0\0\0")
    use_record = 0 if rng.random() < 0.06 else record
    got = rig.go(COMPONENTS, slot, *(rng.randrange(1 << 32) for _ in range(3)), ecx=text_ptr, edx=use_record)
    require(got[0] == "ret", f"components: {got}")
    if use_record == 0:
        require(not rig.log, "null record: callee reached")
        return
    want_calls = model_components(text, image, slot, link)
    require(rig.log == want_calls, f"components {text!r} slot {slot}: calls {rig.log} != {want_calls}")
    require(machine.read(record, 0x100) == bytes(image), f"components {text!r} slot {slot}: record differs")


def check_preset(rig, rng):
    """FUN_00659a10: monpreset schema and the per-act first row / count tables."""
    before = rig.scramble()
    pool = rng.randrange(1 << 32)
    count = rng.choice((0, 1, 5, 12, 30, -3))
    acts, act = [], rng.choice((1, 2))
    for _ in range(max(count, 0)):
        act = min(act + rng.choice((0, 0, 0, 1, 1, 2, 3)), rng.choice((5, 5, 5, 7))) if rng.random() < 0.9 else rng.randrange(256)
        acts.append(act)
    table = rig.m.alloc(4 * max(count, 1) + 8)
    rig.write(table, b"".join(struct.pack("<BBH", a, 0xAA, 0xBBBB) for a in acts))
    rig.rows, rig.table = count & MASK, table
    got = rig.go(PRESET, ecx=pool)
    want = {0x96C6D8: table, 0x96C6DC: table}
    ecx = esi = 0
    edx = table
    for a in acts:
        edi = ecx + 1
        if a > edi if not mut("preset-act-le") else a >= edi:
            while True:
                want[0x96C6F0 + 4 * ecx] = esi
                ecx += 1
                want[0x96C6DC + 4 * ecx] = edx
                edi += 1
                esi = 0
                if not a > edi:
                    break
        esi += 1
        edx += 4
    want[0x96C6F0 + 4 * ecx] = esi
    want_fields = schema(("Act", CHAR, 0, 0, 0), ("Place", LINKER_BIT, 0, 0, 0x65_9990))
    require(got[0] == "ret", f"preset {got}")
    require(len(rig.log) == 1 and rig.log[0][:3] == ("compile", pool, "monpreset") and rig.log[0][4] == 4, f"preset log {rig.log}")
    count_at = rig.log[0][3]
    require(count_at != 0 and not REGION <= count_at < REGION_END, f"preset row-count pointer {count_at:#x} is null or in the data page")
    require(rig.fields == want_fields, "preset columns differ")
    expect = {a: v & MASK for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v & MASK}
    require(rig.changed(before) == expect, f"preset stores {sorted(map(hex, rig.changed(before)))} != {sorted(map(hex, expect))}")


def check_preset_free(rig, rng):
    """FUN_00659af0: unload monpreset (pointer left) and clear the per-act tables."""
    rig.scramble()
    pointer = rng.choice((0, rng.randrange(1, 1 << 32)))
    rig.m.w32(0x96C6D8, pointer)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(PRESET_FREE)
    want = {0x96C6DC + 4 * i: 0 for i in range(5)}
    want.update({0x96C6F0 + 4 * i: 0 for i in range(5)})
    if mut("preset-free-clears-pointer"):
        want[0x96C6D8] = 0
    require(got[0] == "ret" and rig.log == ([("unload", pointer)] if pointer else []), f"preset free {got} {rig.log}")
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, "preset free: stores differ")


def check_sounds(rig, rng):
    """FUN_00659bb0: the sounds table, only with the compile flag."""
    before = rig.scramble()
    flag = rng.choice((0, 1, 0x100, MASK, rng.randrange(1 << 32)))
    rig.m.w32(0x96C8B4, flag)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    pool, link = rng.randrange(1 << 32), rng.randrange(1 << 32)
    rig.rows, rig.table, rig.links = rows_value(rng), rng.randrange(1 << 32), [link]
    got = rig.go(SOUNDS, ecx=pool)
    if flag == 0 and not mut("sounds-flag-ignored"):
        require(got[0] == "ret" and not rig.log and not rig.changed(before), f"sounds without the flag: {got} {rig.log}")
        return
    check_compile_call(rig, got, [("link", FILE, 0xC99), ("compile", pool, "sounds", 0x96BDCC, 2)],
                       schema(("Sound", NAME_I_WORD, 0, 0, 0x96BDC8)),
                       {0x96BDC8: link, 0x96BDC4: rig.table, 0x96BDCC: rig.rows}, before, "sounds")


def check_monseq_free(rig, rng):
    """FUN_00659de0: free MonSeq's table and link (count cleared), and its extra block."""
    rig.scramble()
    table, extra = (rng.choice((0, rng.randrange(1, 1 << 32))) for _ in range(2))
    link = rng.randrange(1, 1 << 32)
    for address, value in ((0x96C7A8, table), (0x96C7AC, link), (0x96C7B4, extra)):
        rig.m.w32(address, value)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(MONSEQ_FREE)
    want_log, want = [], {}
    if table:
        want_log += [("unload", table), ("link-free", link)]
        want[0x96C7B0] = 0
        if mut("monseq-free-clears-pointer"):
            want[0x96C7A8] = 0
    if extra:
        want_log.append(("free", 0, extra, FILE, 0xCEA, 0))
        want[0x96C7B8] = 0
    require(got[0] == "ret" and rig.log == want_log, f"monseq free {got} {rig.log} != {want_log}")
    expect = {a: v for a, v in want.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    require(rig.changed(before) == expect, "monseq free: stores differ")


def check_drop_data(rig, rng):
    """FUN_0065a390: the treasure class link, three loader calls, and the 0xffff class limit."""
    rig.scramble()
    pool, link = rng.randrange(1 << 32), rng.randrange(1, 1 << 32)
    count = rng.choice((0, 5, 0xFFFE, 0xFFFF, 0x10000, MASK, 0x7FFFFFFF, 0x80000000))
    rig.m.w32(0x96C5F0, count)
    rig.links = [link]
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(DROP_DATA, pool)
    order = [("link", FILE, 0x60E), ("tc-auto",), ("tc-load", pool), ("tc-parsed",)]
    if mut("drop-order"):
        order[1], order[2] = order[2], order[1]
    require(rig.log == order, f"drop data log {rig.log}")
    limit = 0x10000 if mut("drop-limit-0x10000") else 0xFFFF
    fatal = s32(count) >= limit
    require(got == (("halt", 0x616) if fatal else ("ret", got[1])), f"drop data count {count:#x}: {got}")
    expect = {0x96C5E8: link} if struct.unpack_from("<I", before, 0x96C5E8 - REGION)[0] != link else {}
    require(rig.changed(before) == expect, "drop data: stores differ")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x655030)
    rng = random.Random(0x657010)
    rig = Rig()
    plan = [(check_unique, 120), (check_unique_free, 60), (check_parsed_free, 80), (check_roll, 400), (check_body_locs, 20),
            (check_monumod, 80), (check_super_row, 120), (check_hireling, 300), (check_hire_free, 40),
            (check_hire_next, 500), (check_npc, 20), (check_sound_of, 300), (check_montype_free, 40),
            (check_components, 1500), (check_preset, 250), (check_preset_free, 40), (check_sounds, 60),
            (check_monseq_free, 60), (check_drop_data, 60)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            rig.fields, rig.links, rig.ids = [], [], []
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "MonsterTbls loader cases")


if __name__ == "__main__":
    main()
