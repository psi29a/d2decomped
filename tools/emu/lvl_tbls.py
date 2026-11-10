# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 19 small LvlTbls.cpp helpers (.\\DATATBLS\\LvlTbls.cpp) in stock game.exe 1.14d.

    uv run python lvl_tbls.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random tables and
compares the return value, the memory written, the ordered log of the hooked
callees (the table free 0x61_25c0, the pool release 0x40_b480, the client
release 0x40_b3c0, the DS1 release 0x66_8300) and the bytes `ret n` popped.
The data-tables globals are written directly (no MPQ is read); the rig is
monster_tbls.py's, wsprintfA is replaced by a model of its "%s\\%s" format.

Table names: leveldefs = 0x9c-byte rows at 0x96c890 (count at tables `+0xc5c`);
lvlprest = 0x1b0-byte rows at 0x96c894 (6 DS1 per row, count `+0xc68`);
lvltypes = 0x788-byte rows at 0x96c9f0 (32 file names of 0x3c, act at `+0x780`).

* FUN_0061ddf0 (ECX out): stores `0x96c9f8` through ECX, returns `0x96c9f4`.
* FUN_0061e440: frees leveldefs (`0x96c890`) with the table free when present,
  and `0x96c9f4` with the pool release (line 0x1d9).
* FUN_0061e470 (ECX n): the leveldefs row n (0x9c bytes) when 0 <= n < the
  count at tables `+0xc5c`, else 0.
* FUN_0061e4a0 (ECX path, EDI prefix): turns `/` into `\\` in the path, then
  for a path of two or more characters rewrites it as `prefix\\path`.
* FUN_0061ea90 / 0061f300 / 0061f480: free lvlsub's neighbours: the table free
  of `0x96c9f0` (lvltypes), `0x96ca00` (lvlwarp) and `0x96ca08` (lvlmaze).
* FUN_0061eaa0 (n): lvltypes row n (0x788 bytes) = n * 0x788 + table, no bound
  on n either way; no table halts line 0x251.
  FUN_0061eae0 (n, c): is the byte at row `+0x780` equal to c.
  FUN_0061eb40 (n, k, dst): stores a 0 byte at dst, then copies string k (0x3c
  bytes each) of the row when k < 0x20 (signed).
* FUN_0061f030: frees lvlsub's DS1 list (`0x96ca20`, 6 per lvlprest row, the
  DS1 release per non-null entry, line 0x2d2) and lvlprest (`0x96c894`).
* FUN_0061f0b0 (n): lvlprest row n (0x1b0 bytes) when 0 <= n < count (tables
  `+0xc68`). FUN_0061f0e0 (ECX key): the first row with `+4` (LevelId) == key,
  else 0.
* FUN_0061f310 (ECX id, DL c): lvlwarp must hold a row (0x30 bytes) with the id
  whose `+0x2c` byte is 'b', c, or c is 'b'; none halts 0x33a, no table 0x32b.
  Only the halt or the return is checked; the returned value is not.
* FUN_0061f490 (ECX id): the lvlmaze row (0x1c bytes) with the id; 0x36c / 0x363.
* FUN_0061fa90 (ECX n): lvlsub row by type through the index `0x96ca18`:
  index[n] * 0x15c + `0x96ca10`; halts 0x3f1 (n < 0), 0x3f2, 0x3f3. There is no
  count: n past the index is read from the array (drawn to 15 of 16 entries).
* FUN_0061fc10 / 0061fc80 (EDI name): a name's index in the 36-entry level-type
  table at 0x6e7d50 (16 bytes each, "None", "1 Town" ... "5 Lava") or the
  20-entry DT1 type table at 0x6e7f90 (8 bytes each, "fl", "wl" ... "fi");
  a name starting with '0' gives 0; no match halts 0x4a5 / 0x4d0.
* FUN_006200d0: releases `0x96ca28` (line 0x551) and clears it.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require
from monster_tbls import MASK, Rig, s32

(GET_PAIR, FREE_DEFS, DEF_ROW, FIX_PATH, FREE_TYPES, TYPES_ROW, TYPES_BYTE, TYPES_NAME, FREE_PREST, PREST_ROW,
 PREST_BY_KEY, FREE_WARP, WARP_CHECK, FREE_MAZE, MAZE_ROW, SUB_ROW, TYPE_NAME, DT1_NAME, FREE_28) = (
    0x61DDF0, 0x61E440, 0x61E470, 0x61E4A0, 0x61EA90, 0x61EAA0, 0x61EAE0, 0x61EB40, 0x61F030, 0x61F0B0,
    0x61F0E0, 0x61F300, 0x61F310, 0x61F480, 0x61F490, 0x61FA90, 0x61FC10, 0x61FC80, 0x6200D0)
TABLES_PTR = 0x744304
FILE = ".\\DATATBLS\\LvlTbls.cpp"
MUTATIONS = ("pair-swapped", "free-defs-keeps", "def-row-le", "def-stride", "path-keeps-slash", "path-short-prefix",
             "types-wrong-stride", "types-byte-dword", "types-name-keeps", "ds1-all-rows", "prest-row-negative",
             "prest-key-last", "warp-b-either", "warp-b-id", "maze-first-only", "sub-row-negative", "type-name-zero",
             "dt1-name-offset", "free28-keeps")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class LvlRig(Rig):
    """The rig with the DS1 release, the client release and wsprintfA added."""

    def __init__(self):
        """Hook 0x66_8300 (ECX DS1 slot), 0x40_b3c0 (ECX block, EDX file; line, flag) and the "%s\\%s" wsprintfA."""
        super().__init__()
        self.logger(0x66_8300, 0, "ds1-free", regs=("ecx",))
        self.logger(0x40_B3C0, 2, "client-free", regs=("ecx", "edx"))
        slot = self.m.r32(0x6CC468)
        self.m.slots[slot] = ("wsprintfA", self.wsprintf)

    def wsprintf(self, current):
        """wsprintfA(buffer, "%s\\%s", prefix, path): only that format is used."""
        fmt = current.cstr(current.arg(1))
        require(fmt == "%s\\%s", f"unexpected format {fmt!r}")
        text = current.cstr(current.arg(2)) + "\\" + current.cstr(current.arg(3))
        current.mu.mem_write(current.arg(0), text.encode("latin-1") + b"\0")
        return len(text), 0


def tables(rig, **fields):
    """A data-tables block with the given dword fields at their offsets; returns nothing."""
    block = rig.m.alloc(0x2000)
    rig.m.w32(TABLES_PTR, block)
    for offset, value in fields.items():
        rig.m.w32(block + int(offset[1:], 16), value & MASK)


def check_pair(rig, rng):
    """FUN_0061ddf0: two globals through a pointer."""
    machine = rig.m
    machine.w32(0x96C9F8, a := rng.randrange(1 << 32))
    machine.w32(0x96C9F4, b := rng.randrange(1 << 32))
    out = machine.alloc(8)
    got = rig.go(GET_PAIR, ecx=out)
    wrote = machine.r32(out + (4 if mut("pair-swapped") else 0))
    require(got == ("ret", b) and wrote == a and not rig.log, f"{got} {wrote:#x}")


def check_frees(rig, rng):
    """The one- and two-call releases: FUN_0061e440, 0061ea90, 0061f300, 0061f480, 006200d0."""
    machine = rig.m
    values = {a: rng.choice((0, rng.randrange(1, 1 << 30))) for a in (0x96C890, 0x96C9F4, 0x96C9F0, 0x96CA00, 0x96CA08, 0x96CA28)}
    for address, value in values.items():
        machine.w32(address, value)
    for address, global_at in ((FREE_TYPES, 0x96C9F0), (FREE_WARP, 0x96CA00), (FREE_MAZE, 0x96CA08)):
        require(rig.go(address)[0] == "ret" and rig.log == [("table-free", values[global_at])], f"{address:#x} {rig.log}")
    want = []
    if values[0x96C890] and not mut("free-defs-keeps"):
        want.append(("table-free", values[0x96C890]))
    if values[0x96C9F4]:
        want.append(("free", 0, values[0x96C9F4], FILE, 0x1D9, 0))
    require(rig.go(FREE_DEFS)[0] == "ret" and rig.log == want, f"leveldefs free {rig.log} != {want}")
    want = [("free", 0, values[0x96CA28], FILE, 0x551, 0)] if values[0x96CA28] else []
    require(rig.go(FREE_28)[0] == "ret" and rig.log == want, f"free 0x96ca28: {rig.log}")
    require(machine.r32(0x96CA28) == (values[0x96CA28] if mut("free28-keeps") else 0), "0x96ca28 not cleared")


def check_def_row(rig, rng):
    """FUN_0061e470: leveldefs row n."""
    count = rng.choice((0, 1, 5, 40))
    base = rng.randrange(0x1000, 0x7000_0000)
    tables(rig, **{"o0xc5c": count})
    rig.m.w32(0x96C890, base)
    n = rng.choice((-1, 0, count - 1, count, count + 1, rng.randrange(-4, 45), MASK))
    value = s32(n & MASK)
    inside = (-1 < value < count) or (mut("def-row-le") and value == count)
    stride = 0x9B if mut("def-stride") else 0x9C
    want = (value * stride + base) & MASK if inside else 0
    require(rig.go(DEF_ROW, ecx=n & MASK) == ("ret", want), f"row {value} of {count}")


def model_path(path, prefix):
    """FUN_0061e4a0: the bytes the path buffer holds after the call."""
    text = path if mut("path-keeps-slash") else path.replace(b"/", b"\\")
    limit = 1 if mut("path-short-prefix") else 2
    return prefix + b"\\" + text if len(text) >= limit else text


def check_path(rig, rng):
    """FUN_0061e4a0: slashes to backslashes, then the prefix."""
    machine = rig.m
    alphabet = b"abc/\\.-_0"
    path = bytes(rng.choice(alphabet) for _ in range(rng.choice((0, 1, 2, 3, 9, 20))))
    prefix = bytes(rng.choice(b"dataGLOBAL\\/") for _ in range(rng.randrange(1, 12)))
    buffer = machine.alloc(128)
    rig.write(buffer, path + b"\0" + bytes([mw.GARBAGE]) * 100)
    text = machine.put(prefix + b"\0")
    got = rig.go(FIX_PATH, ecx=buffer, regs={"edi": text})
    want = model_path(path, prefix)
    require(got[0] == "ret" and machine.read(buffer, len(want) + 1) == want + b"\0", f"{path!r} + {prefix!r}: {machine.read(buffer, 40)!r} != {want!r}")


def types_table(rig, rng, rows):
    """lvltypes rows (0x788 bytes, random bytes with 32 short names per row); returns (base, image)."""
    image = bytearray(rng.randbytes(0x788 * rows))
    for row in range(rows):
        for k in range(0x20):
            text = bytes(rng.choice(b"abcdef\\.") for _ in range(rng.randrange(0, 8))) + b"\0"
            image[0x788 * row + 0x3C * k:0x788 * row + 0x3C * k + len(text)] = text
    base = rig.array(bytes(image))
    rig.m.w32(0x96C9F0, base)
    return base, image


def check_types(rig, rng):
    """FUN_0061eaa0, 0061eae0 and 0061eb40 on lvltypes."""
    machine = rig.m
    rows = rng.randrange(2, 5)
    absent = rng.random() < 0.15
    base, image = types_table(rig, rng, rows)
    if absent:
        machine.w32(0x96C9F0, 0)
    n = rng.randrange(0, rows)
    which = rng.choice(("row", "byte", "name"))
    if which == "row":
        n = rng.choice((n, -1, -5, rows, rows + 7, rng.randrange(-8, rows + 9)))       # no bound either way
        got = rig.go(TYPES_ROW, n & MASK)
        want = ("halt", 0x251) if absent else ("ret", (base + n * (0x787 if mut("types-wrong-stride") else 0x788)) & MASK)
        require(got == want, f"row {n}: {got}")
    elif which == "byte":
        c = rng.choice((image[0x788 * n + 0x780], rng.randrange(256), 0, 0xFF))
        arg = c | rng.choice((0, 0x100, 0xFFFFFF00))
        got = rig.go(TYPES_BYTE, n, arg)
        a = image[0x788 * n + 0x780]
        equal = a == (arg if mut("types-byte-dword") else c)
        want = ("halt", 0x251) if absent else ("ret", int(equal))
        require(got == want, f"byte {n} {arg:#x}: {got}")
    else:
        k = rng.choice((0, 1, 31, 32, 33, rng.randrange(0, 40)))
        dst = machine.alloc(0x80)
        rig.write(dst, bytes([mw.GARBAGE]) * 0x80)
        got = rig.go(TYPES_NAME, n, k, dst)
        if absent:
            require(got == ("halt", 0x251), f"name: {got}")
            return
        require(got[0] == "ret", f"name: {got}")
        if k < 0x20:
            start = 0x788 * n + 0x3C * k
            text = image[start:image.index(0, start) + 1]
            want = text
        else:
            want = b"\0" if not mut("types-name-keeps") else bytes([mw.GARBAGE])
        require(machine.read(dst, len(want)) == want, f"string {k} of row {n}: {machine.read(dst, 12)!r}")


def check_ds1(rig, rng):
    """FUN_0061f030: free the DS1 list and lvlprest."""
    machine = rig.m
    types = rng.randrange(0, 4)
    entries = [rng.choice((0, 0, rng.randrange(1, 1 << 20))) for _ in range(types * 6)]
    list_present = rng.random() < 0.8
    block = rig.array(struct.pack(f"<{len(entries)}I", *entries) or bytes(4)) if list_present else 0
    prest_ptr = rng.choice((0, rng.randrange(1, 1 << 30)))
    tables(rig, **{"o0xc68": types})
    machine.w32(0x96CA20, block)
    machine.w32(0x96C894, prest_ptr)
    got = rig.go(FREE_PREST)
    want = []
    if block:
        for i, entry in enumerate(entries):
            if entry != 0 or mut("ds1-all-rows"):
                want.append(("ds1-free", block + 4 * i))
        want.append(("client-free", block, 0x6E84DC, 0x2D2, 0))
    if prest_ptr:
        want.append(("table-free", prest_ptr))
    require(got[0] == "ret" and rig.log == want, f"{rig.log} != {want}")
    require(machine.r32(0x96CA20) == 0, "0x96ca20 not cleared")


def check_prest(rig, rng):
    """FUN_0061f0b0 (row n) and FUN_0061f0e0 (row by LevelId key) on lvlprest."""
    machine = rig.m
    count = rng.choice((0, 1, 4, 9))
    keys = [rng.randrange(0, 6) for _ in range(count)]
    rows = b"".join(rng.randbytes(4) + struct.pack("<I", key) + rng.randbytes(0x1B0 - 8) for key in keys)
    base = rig.array(rows or bytes(0x1B0))
    tables(rig, **{"o0xc68": count})
    machine.w32(0x96C894, base)
    if rng.random() < 0.5:
        n = rng.choice((-1, 0, count - 1, count, rng.randrange(-3, 12), MASK))
        value = s32(n & MASK)
        inside = (-1 < value < count) or (mut("prest-row-negative") and value == -1)
        require(rig.go(PREST_ROW, n & MASK) == ("ret", (base + 0x1B0 * value) & MASK if inside else 0), f"lvlprest row {value} of {count}")
    else:
        key = rng.randrange(0, 7)
        hits = [i for i, k in enumerate(keys) if k == key]
        want = base + 0x1B0 * (hits[-1] if mut("prest-key-last") else hits[0]) if hits else 0
        require(rig.go(PREST_BY_KEY, ecx=key) == ("ret", want), f"lvlprest key {key} in {keys}")


def check_warp(rig, rng):
    """FUN_0061f310: lvlwarp must hold a row for the id and a matching letter."""
    machine = rig.m
    count = rng.choice((0, 1, 4, 7))
    absent = rng.random() < 0.15
    rows = []
    for _ in range(count):
        row = bytearray(rng.randbytes(0x30))
        row[0:4] = struct.pack("<I", rng.randrange(0, 4))
        row[0x2C] = rng.choice(b"abcb")
        rows.append(bytes(row))
    base = 0 if absent else rig.array(b"".join(rows) or bytes(0x30))
    machine.w32(0x96CA00, base)
    machine.w32(0x96CA04, count)
    ident = rng.randrange(0, 5)
    letter = rng.choice(b"abcd")
    got = rig.go(WARP_CHECK, ecx=ident, edx=letter | rng.choice((0, 0x100, 0xAB00)))
    found = any(struct.unpack_from("<I", r)[0] == ident and (letter == 0x62 or r[0x2C] == 0x62 or r[0x2C] == letter)
                for r in rows)
    if mut("warp-b-either"):
        found = any(struct.unpack_from("<I", r)[0] == ident and (letter == 0x62 or r[0x2C] == letter) for r in rows)
    if mut("warp-b-id"):
        found = any(struct.unpack_from("<I", r)[0] == ident or (letter == r[0x2C]) for r in rows)
    want = ("halt", 0x32B) if absent else (("ret", got[1]) if found else ("halt", 0x33A))
    require(got == want, f"warp id {ident} letter {chr(letter)}: {got} != {want}")


def check_maze(rig, rng):
    """FUN_0061f490: the lvlmaze row with the id."""
    machine = rig.m
    count = rng.choice((0, 1, 5))
    absent = rng.random() < 0.15
    ids = [rng.randrange(0, 5) for _ in range(count)]
    rows = b"".join(struct.pack("<I", i) + rng.randbytes(0x18) for i in ids)
    base = 0 if absent else rig.array(rows or bytes(0x1C))
    machine.w32(0x96CA08, base)
    machine.w32(0x96CA0C, count)
    ident = rng.randrange(0, 6)
    got = rig.go(MAZE_ROW, ecx=ident)
    hits = [i for i, v in enumerate(ids) if v == ident]
    if absent:
        want = ("halt", 0x363)
    elif not hits:
        want = ("halt", 0x36C)
    else:
        want = ("ret", base + 0x1C * (hits[0] if not mut("maze-first-only") else hits[-1]))
    require(got == want, f"maze {ident} in {ids}: {got} != {want}")


def check_sub_row(rig, rng):
    """FUN_0061fa90: lvlsub row by type through the index."""
    machine = rig.m
    base = 0 if rng.random() < 0.15 else rng.randrange(0x1000, 0x7000_0000)
    index = (rig.array(bytes(12) + struct.pack("<16I", *[rng.randrange(0, 50) for _ in range(16)])) + 12
             if rng.random() > 0.15 else 0)   # 12 bytes of padding so a negative n still reads inside the array
    machine.w32(0x96CA10, base)
    machine.w32(0x96CA18, index)
    n = rng.choice((-1, 0, 3, 5, 6, 15, rng.randrange(-3, 16)))     # no count: the index is read past the first entries
    got = rig.go(SUB_ROW, ecx=n & MASK)
    if n < 0 and not mut("sub-row-negative"):
        want = ("halt", 0x3F1)
    elif base == 0:
        want = ("halt", 0x3F2)
    elif index == 0:
        want = ("halt", 0x3F3)
    else:
        want = ("ret", (struct.unpack_from("<I", machine.read(index - 12, 76), 12 + 4 * n)[0] * 0x15C + base) & MASK)
    require(got == want, f"sub row {n}: {got} != {want}")


def table_names(rig, start, stop, stride):
    """The name strings of a static table in game.exe."""
    return [rig.m.cstr(a).encode("latin-1") for a in range(start, stop, stride)]


def check_name(rig, rng, address, start, stop, stride, line):
    """FUN_0061fc10 / 0061fc80: a name's index in a static table."""
    names = table_names(rig, start, stop, stride)
    name = rng.choice(names + [b"nope", b"", b"0", b"0x", b"1 town", names[3] + b"x", names[3][:-1], b"None "])
    if rng.random() < 0.2:
        name = rng.choice(names).upper()
    text = rig.m.put(name + b"\0")
    got = rig.go(address, regs={"edi": text})
    if name[:1] == b"0" and not mut("type-name-zero"):
        want = ("ret", 0)
    elif name in names:
        want = ("ret", names.index(name) + (1 if mut("dt1-name-offset") else 0))
    else:
        want = ("halt", line)
    require(got == want, f"{address:#x} {name!r}: {got} != {want}")


def run_all():
    """Run every check on random tables; returns the case count."""
    rng = random.Random(0x61E470)
    rig = LvlRig()
    plan = [(check_pair, 20), (check_frees, 100), (check_def_row, 200), (check_path, 300), (check_types, 500),
            (check_ds1, 200), (check_prest, 300), (check_warp, 500), (check_maze, 300), (check_sub_row, 200),
            (lambda r, g: check_name(r, g, TYPE_NAME, 0x6E7D50, 0x6E7F90, 16, 0x4A5), 300),
            (lambda r, g: check_name(r, g, DT1_NAME, 0x6E7F90, 0x6E8030, 8, 0x4D0), 300)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "LvlTbls helper cases")


if __name__ == "__main__":
    main()
