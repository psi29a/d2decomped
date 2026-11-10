# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 8 FieldTbls.cpp functions (.\\DATATBLS\\FieldTbls.cpp) in stock game.exe 1.14d.

    uv run python fieldtbls.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

* FIELDTBLS_InitFieldData 0x66a2b0 (stdcall archive, unused): the expfield archive's
  width (dword at +2) and height (dword at +6) and its bytes from +10. Allocates
  w*h bytes (pool, line 0x3e), copies them, sets the table pointer 0x96d654, the
  size 0x96d660, width 0x96d65c, height 0x96d658, half height 0x96d664 (= h >> 1)
  and half width 0x96d668 (= w >> 1), and nine row offsets 0x96d630 + 4k =
  Y[k] * h + X[k] from the step tables (X at 0x749780, Y at 0x7497a4). Frees the
  archive as client memory (line 0x50). The multiplier is the HEIGHT register in
  the stock code: the Ghidra output names it nWidth.
* FIELDTBLS_FreeFieldData 0x66a420 (no arguments): frees the table if set (pool,
  line 0x5b), clears the table pointer and the five dimensions, returns 1.
* FIELDTBLS_GetFieldCenterOffsets 0x66a470 (stdcall two pointers): stores the
  half height (0x96d664) through the first pointer and the half width (0x96d668)
  through the second.
* FIELDTBLS_AllocPoint 0x66a4b0 (no arguments): an 8-byte pool block (line 0x87)
  with x = y = 0; a null block is fatal (line 0x88, not reached here).
* FIELDTBLS_FreePoint 0x66a500 (stdcall point): null is fatal (line 0x93),
  otherwise the block is freed (line 0x94).
* FIELDTBLS_SetXY 0x66a540 (stdcall point, x, y): null is fatal (line 0xa1),
  otherwise x at +0 and y at +4.
* FIELDTBLS_GetFieldValueAtCoords 0x66a580 (stdcall origin, x, y): null origin
  is fatal (line 0xb0); else the byte at table + x + ((y - origin[1]) << 8) - origin[0].
* FIELDTBLS_LoadExpFieldFile 0x66a700 (fastcall pool): wsprintfA("%s\\expfield.d2",
  "DATA\\GLOBAL") gives the path, D2FILE 0x517079 loads it (line 0x28), then
  FIELDTBLS_InitFieldData runs on the result.

The fatal path (0x408090 / 0x408a60), the pool and client release, D2FILE and the
memmove 0x6829c0 are hooked or run as in the other oracles. The pool allocator
hook returns fresh memory, so the null allocation path of InitFieldData and
AllocPoint is not reached; those halts are not claimed.

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import _monster_world as mw
from _monster_world import require

INIT, FREE_FIELD, CENTER, ALLOC_POINT, FREE_POINT, SET_XY, BYTE_AT, LOAD = (
    0x66_A2B0, 0x66_A420, 0x66_A470, 0x66_A4B0, 0x66_A500, 0x66_A540, 0x66_A580, 0x66_A700)
FILE_PTR, FILE = 0x6E_ED30, ".\\DATATBLS\\FieldTbls.cpp"
FATAL_ADDR, FATAL_RAISE, CLIENT_FREE, D2FILE = 0x40_8090, 0x40_8A60, 0x40_B3C0, 0x51_7079
MEMMOVE = 0x68_29C0
TABLE, SIZE, WIDTH, HEIGHT, HALF_H, HALF_W = 0x96_D654, 0x96_D660, 0x96_D65C, 0x96_D658, 0x96_D664, 0x96_D668
ROW_OFFSETS = 0x96_D630
X_STEPS, Y_STEPS = 0x74_9780, 0x74_97A4
REGION, REGION_END = 0x96_D630, 0x96_D66C
MASK = 0xFFFFFFFF
MUTATIONS = ("init-stride-width", "init-half-swap", "init-step-swap-4-5", "init-size-kept", "freefield-keeps-width",
             "freefield-no-free", "freefield-return-0", "center-swap", "alloc-point-y-kept", "freepoint-line",
             "setxy-swap", "byte-stride-width", "byte-origin-y-ignored", "byte-null-no-halt", "load-path-typo", "load-line-29",
             "init-client-free-skipped")
mutation = None


class Halt(Exception):
    """game.exe's unrecoverable-error call, with the line it passed."""


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def s32(value):
    """A dword as a signed number."""
    return value - (1 << 32) if value & 0x80000000 else value


class Rig(mw.World):
    """The emulated game with the fatal path, client release and the expfield archive reader hooked."""

    def __init__(self):
        """Hook the fatal path, the client release, D2FILE and log the callees the loaders make."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 200000
        self.archive = 0
        self.m.hook(FATAL_ADDR, lambda current: current.arg(0), 1)
        self.m.hook(FATAL_RAISE, self.fatal, 0)
        self.logger(CLIENT_FREE, 2, "client-free", regs=("ecx", "edx"))
        self.m.hook(D2FILE, self.d2file, 3)

    def fatal(self, current):
        """The fatal-error call: arg 1 is the line passed through 0x408090."""
        raise Halt(current.arg(1))

    def d2file(self, current):
        """D2FILE_AllocAndLoadD2File (fastcall pool, path; stack: &size, file, line): log and return the archive."""
        self.log.append(("d2file", current.mu.reg_read(UC_X86_REG_ECX), current.cstr(current.mu.reg_read(UC_X86_REG_EDX)),
                         current.arg(1), current.arg(2), current.arg(0)))
        if current.arg(0):
            current.w32(current.arg(0), 0x7F)
        return self.archive

    def go(self, address, *args, ecx=0, edx=0):
        """One call; ("halt", line) when the fatal path ran, else ("ret", eax)."""
        try:
            return ("ret", self.run(address, *args, ecx=ecx, edx=edx))
        except Halt as halt:
            return ("halt", halt.args[0])

    def scramble(self):
        """Random dwords in the globals and the step tables; returns the globals' region before the call."""
        self.write(REGION, random.randbytes(REGION_END - REGION))
        self.write(X_STEPS, random.randbytes(0x24))
        self.write(Y_STEPS, random.randbytes(0x24))
        return bytes(self.m.read(REGION, REGION_END - REGION))

    def changed(self, before):
        """The dwords of the globals region that differ from `before`, as {address: value}."""
        after = self.m.read(REGION, REGION_END - REGION)
        return {REGION + off: struct.unpack_from("<I", after, off)[0] for off in range(0, len(after), 4)
                if after[off:off + 4] != before[off:off + 4]}

    def steps(self):
        """The current step tables: X[k] and Y[k] for k in 0..8."""
        return (self.read_words(X_STEPS, 9), self.read_words(Y_STEPS, 9))


def check_stores(rig, before, expect, what):
    """The globals written must be exactly the model's, dropping writes that do not change a value."""
    expect = {a: v & MASK for a, v in expect.items()}
    expect = {a: v for a, v in expect.items() if struct.unpack_from("<I", before, a - REGION)[0] != v}
    got = rig.changed(before)
    require(got == expect, f"{what}: stores {sorted((hex(a), hex(v)) for a, v in got.items())} != "
            f"{sorted((hex(a), hex(v)) for a, v in expect.items())}")


def make_archive(rig, rng, width, height):
    """An expfield archive block: width at +2, height at +6, the bytes from +10; returns (address, data)."""
    data = rng.randbytes(width * height)
    block = rig.m.alloc(10 + len(data) + 0x10)
    head = bytearray(rng.randbytes(10))
    struct.pack_into("<I", head, 2, width)
    struct.pack_into("<I", head, 6, height)
    rig.write(block, bytes(head) + data)
    return block, data


def check_init(rig, rng):
    """FIELDTBLS_InitFieldData: copy the bytes, store the dimensions and the nine row offsets, free the archive."""
    before = rig.scramble()
    width, height = rng.randrange(1, 40), rng.randrange(1, 40)
    archive, data = make_archive(rig, rng, width, height)
    rig.log = []
    got = rig.go(INIT, archive, rng.randrange(1 << 32))
    require(got == ("ret", 1), f"InitFieldData {got}")
    stride = width if mut("init-stride-width") else height
    xs, ys = rig.steps()
    if mut("init-step-swap-4-5"):
        xs[4], xs[5] = xs[5], xs[4]
    size = width * height if not mut("init-size-kept") else width * height + 1
    table = rig.read_words(TABLE, 1)[0]
    require(table != 0, "InitFieldData left the table null")
    require(bytes(rig.m.read(table, width * height)) == data, "InitFieldData copied other bytes")
    half_h, half_w = (width >> 1, height >> 1) if mut("init-half-swap") else (height >> 1, width >> 1)
    expect = {SIZE: size, WIDTH: width, HEIGHT: height, HALF_H: half_h, HALF_W: half_w, TABLE: table}
    for k in range(9):
        expect[ROW_OFFSETS + 4 * k] = ys[k] * stride + xs[k]
    check_stores(rig, before, expect, "InitFieldData")
    want_log = [("alloc", 0, width * height, FILE, 0x3E, 0)]
    if not mut("init-client-free-skipped"):
        want_log.append(("client-free", archive, FILE_PTR, 0x50, 0))
    require(rig.log == want_log, f"InitFieldData log {rig.log} != {want_log}")


def check_free_field(rig, rng):
    """FIELDTBLS_FreeFieldData: free the table (line 0x5b) when set, clear the globals, return 1."""
    before = rig.scramble()
    table = rng.choice((0, rng.randrange(1, 1 << 32)))
    rig.write(TABLE, struct.pack("<I", table))
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    rig.log = []
    got = rig.go(FREE_FIELD)
    want = 0 if mut("freefield-return-0") else 1
    require(got == ("ret", want), f"FreeFieldData {got}")
    want_log = [("free", 0, table, FILE, 0x5B, 0)] if table and not mut("freefield-no-free") else []
    require(rig.log == want_log, f"FreeFieldData log {rig.log} != {want_log}")
    expect = {TABLE: 0, WIDTH: 0, HEIGHT: 0, SIZE: 0, HALF_H: 0, HALF_W: 0}
    if mut("freefield-keeps-width"):
        del expect[WIDTH]
    check_stores(rig, before, expect, "FreeFieldData")


def check_center(rig, rng):
    """FIELDTBLS_GetFieldCenterOffsets: half height through the first pointer, half width through the second."""
    before = rig.scramble()
    half_h, half_w = rng.randrange(1 << 32), rng.randrange(1 << 32)
    rig.write(HALF_H, struct.pack("<II", half_h, half_w))
    first, second = rig.m.alloc(8), rig.m.alloc(8)
    before = bytes(rig.m.read(REGION, REGION_END - REGION))
    got = rig.go(CENTER, first, second)
    require(got[0] == "ret", f"center {got}")
    a, b = (half_w, half_h) if mut("center-swap") else (half_h, half_w)
    require(rig.read_words(first, 1)[0] == a & MASK and rig.read_words(second, 1)[0] == b & MASK, "center offsets differ")
    require(not rig.changed(before), "center offsets wrote the globals")


def check_alloc_point(rig, rng):
    """FIELDTBLS_AllocPoint: an 8-byte block (line 0x87) with x = y = 0."""
    rig.scramble()
    rig.log = []
    got = rig.go(ALLOC_POINT)
    require(got[0] == "ret", f"AllocPoint {got}")
    point = got[1]
    require(rig.log == [("alloc", 0, 8, FILE, 0x87, 0)], f"AllocPoint log {rig.log}")
    y = 0xA5A5A5A5 if mut("alloc-point-y-kept") else 0
    require(rig.read_words(point, 2) == [0, y], f"AllocPoint fields {rig.read_words(point, 2)}")


def check_free_point(rig, rng):
    """FIELDTBLS_FreePoint: null is fatal (line 0x93); otherwise free the block (line 0x94)."""
    rig.scramble()
    point = rng.choice((0, rig.m.alloc(8)))
    rig.log = []
    got = rig.go(FREE_POINT, point)
    if point == 0:
        require(got == ("halt", 0x93), f"FreePoint(null) {got}")
        return
    line = 0x94 if not mut("freepoint-line") else 0x95
    require(got[0] == "ret", f"FreePoint {got}")
    require(rig.log == [("free", 0, point, FILE, line, 0)], f"FreePoint log {rig.log}")


def check_set_xy(rig, rng):
    """FIELDTBLS_SetXY: x at +0, y at +4; null is fatal (line 0xa1)."""
    rig.scramble()
    point = rng.choice((0, rig.m.alloc(8)))
    x, y = rng.randrange(1 << 32), rng.randrange(1 << 32)
    got = rig.go(SET_XY, point, x, y)
    if point == 0:
        require(got == ("halt", 0xA1), f"SetXY(null) {got}")
        return
    require(got[0] == "ret", f"SetXY {got}")
    if mut("setxy-swap"):
        x, y = y, x
    require(rig.read_words(point, 2) == [x & MASK, y & MASK], "SetXY stored other values")


def check_byte_at(rig, rng):
    """FIELDTBLS_GetFieldValueAtCoords: table + x + ((y - origin[1]) << 8) - origin[0]; a null origin is fatal (line 0xb0)."""
    rig.scramble()
    base = rig.m.alloc(0x200000)
    table = base + 0x100000
    rig.m.w32(TABLE, table)
    content = rng.randbytes(0x200000)
    rig.write(base, content)
    x, y = rng.randrange(-200, 200), rng.randrange(-200, 200)
    if rng.random() < 0.1:
        got = rig.go(BYTE_AT, 0, x & MASK, y & MASK)
        want = ("halt", 0xB0) if not mut("byte-null-no-halt") else ("ret", content[0x100000 + x])
        require(got == want, f"FieldByte(null origin): {got} != {want}")
        return
    origin = rig.m.alloc(8)
    o0, o1 = rng.randrange(-200, 200), rng.randrange(-200, 200)
    rig.write(origin, struct.pack("<ii", o0, o1))
    got = rig.go(BYTE_AT, origin, x & MASK, y & MASK)
    require(got[0] == "ret", f"FieldByte {got}")
    if mut("byte-origin-y-ignored"):
        index = x + ((y << 8)) - o0
    elif mut("byte-stride-width"):
        index = x + ((y - o1) * 40) - o0
    else:
        index = x + ((y - o1) << 8) - o0
    expect = content[0x100000 + index]
    require(got == ("ret", expect), f"FieldByte: {got} != {expect} at index {index}")


def check_load(rig, rng):
    """FIELDTBLS_LoadExpFieldFile: the path built from wsprintfA, D2FILE with line 0x28, then the init."""
    rig.scramble()
    width, height = rng.randrange(1, 30), rng.randrange(1, 30)
    archive, data = make_archive(rig, rng, width, height)
    rig.archive = archive
    rig.log = []
    pool = rig.m.alloc(4)
    got = rig.go(LOAD, ecx=pool)
    require(got[0] == "ret", f"LoadExpFieldFile {got}")
    require(rig.log, "LoadExpFieldFile made no D2FILE call")
    path = "DATA\\GLOBAL\\expfield.d2" if not mut("load-path-typo") else "DATA\\GLOBAL\\expfield.dt"
    line = 0x28 if not mut("load-line-29") else 0x29
    require(rig.log[0][:5] == ("d2file", pool, path, FILE_PTR, line) and rig.log[0][5] != 0,
            f"LoadExpFieldFile log {rig.log[:1]}")
    table = rig.read_words(TABLE, 1)[0]
    require(bytes(rig.m.read(table, width * height)) == data, "LoadExpFieldFile copied other bytes")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x66A2B0)
    rng = random.Random(0x66A420)
    rig = Rig()
    plan = [(check_init, 60), (check_free_field, 40), (check_center, 40), (check_alloc_point, 40),
            (check_free_point, 40), (check_set_xy, 40), (check_byte_at, 300), (check_load, 40)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "FieldTbls cases")


if __name__ == "__main__":
    main()
