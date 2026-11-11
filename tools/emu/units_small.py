# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the small UNITS\\Units.cpp accessors (position, path, animation, type data, player data) in stock game.exe 1.14d.

    uv run python units_small.py [--mutation-check]

Runs game.exe's own code on random fake units (a 0x100-byte block of random
bytes with the type, path, data and Objects.txt row pointers set) and checks
each call against a model written from the decompile and the disassembly:
the return value, every byte of the unit, path, data and row blocks after
each call that returns (so a write the model does not expect fails; a call
that ends in the fatal path is compared on its line, return and log), the
ordered log of the hooked callees (registers and stack arguments) and the
bytes `ret n` popped. A fatal error
(the .\\UNITS\\Units.cpp line passed to 0x40_8090 / 0x40_8a60) is a
"halt" with that line; the shared assert caller is 0x40_8a60 (hooked),
0x40_8090 itself runs for real (it returns its return address).

Unit: +0 type (0 player, 1 monster, 2 object, 3 missile, 4 item, 5 tile),
+0x10 mode, +0x14 data (an object's first dword is its Objects.txt row),
+0x2c path (static for types 2, 4, 5: +0xc x, +0x10 y, +0x1d flag).

Covered: FUN_00620360, 006203b0, 00620410, 00620470, 006204c0, 00620710,
00620780, 006207f0, 00620830, 00620980, 00620ae0, 00620b50, 00620c70,
00620cd0, 00620d30, 00620f50, 00620fa0, 00621150, 006211c0, 006212c0,
00621310, 00621330, 00621420, 00621870, 006218d0, 00621920, 006219c0,
00621a70, 00621ac0, 00621b00, 00621b70, 00621c00, 00621f60, 00622150, 006220f0, 006221e0, 00622230, 00622270,
006222c0, 00622300, 00622350, 00622dc0, 00622e90, 00622ea0, 00622f10,
00622f80, 00622ff0, 00623060, 006230c0, 00623120, 006231a0, 00623220,
00623290, 00623300, 00623350, 006233a0, 00623410, 006234c0, 006234f0,
00623520, 00623570, 006205a0 (GetUnitSizeY).

`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require

MASK = 0xFFFFFFFF
FILE = ".\\UNITS\\Units.cpp"
FILE_ADDRESS = 0x6E8E10
ASSERT = 0x40_8A60
COVERED = (0x62_0360, 0x62_03b0, 0x62_0410, 0x62_0470, 0x62_04c0, 0x62_0710, 0x62_0780, 0x62_07f0, 0x62_0830, 0x62_0980, 0x62_0ae0, 0x62_0b50, 0x62_0c70, 0x62_0cd0, 0x62_0d30, 0x62_0f50, 0x62_0fa0, 0x62_1150, 0x62_11c0, 0x62_12c0, 0x62_1310, 0x62_1330, 0x62_1420, 0x62_1870, 0x62_18d0, 0x62_1920, 0x62_19c0, 0x62_1a70, 0x62_1ac0, 0x62_1b00, 0x62_1b70, 0x62_1c00, 0x62_1f60, 0x62_2150, 0x62_20f0, 0x62_21e0, 0x62_2230, 0x62_2270, 0x62_22c0, 0x62_2300, 0x62_2350, 0x62_2dc0, 0x62_2e90, 0x62_2ea0, 0x62_2f10, 0x62_2f80, 0x62_2ff0, 0x62_3060, 0x62_30c0, 0x62_3120, 0x62_31a0, 0x62_3220, 0x62_3290, 0x62_3300, 0x62_3350, 0x62_33a0, 0x62_3410, 0x62_34c0, 0x62_34f0, 0x62_3520, 0x62_3570,
           0x62_05a0)
CALLED = set()
MUTATIONS = ("tile-path-swap", "precise-x-no-shift", "precise-x-static-wrong-type", "set-x-allows-missile", "dx-signed",
             "dx-dynamic-null-path", "screen-x-swapped", "init-path-flag-kept", "clear-room-dynamic-always",
             "target-type-allows-item", "flag2-get-type5-static", "flag2-set-nonbool", "take-keeps", "mode-field-always",
             "frame-keeps-low", "advance-unsigned", "advance-range-sign", "row-boundary", "length-wrong-mode",
             "event-copy-5", "event-seq-monster-only", "obj-mode-byte", "txt-13c-nonzero", "path-649ef0-null",
             "set-data5-word", "test-data5-all", "skill-keep-swapped", "switch-read-no-clear", "item-90-other-type",
             "static-path-free-clears", "static-path-alloc-always", "kept-getter-default", "monster-flag-edx")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Halt(Exception):
    """game.exe took the fatal-error path; args[0] is the source line passed to it."""


class Rig(mw.World):
    """The emulated game with the fatal path and the callees of the small accessors hooked."""

    def __init__(self):
        """Hook the assert (0x40_8a60) and every callee the covered functions reach; 0x40_8090 stays real."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 50000
        self.m.hook(ASSERT, self.fatal, 0)
        self.script = {}
        lg = self.logger
        lg(0x64_8940, 1, "precise-x", ret=lambda c: self.script.get("ret", 0), regs=())
        lg(0x64_8970, 1, "precise-y", ret=lambda c: self.script.get("ret", 0), regs=())
        lg(0x64_89E0, 0, "screen-x", regs=("ecx", "edx"))
        lg(0x64_89F0, 0, "screen-y", regs=("ecx", "edx"))
        lg(0x64_8A90, 2, "path-room", regs=())
        lg(0x64_8B90, 2, "path-target-set", regs=())
        lg(0x64_8B30, 1, "path-flag-get", ret=lambda c: self.script.get("ret", 0), regs=())
        lg(0x64_8B40, 2, "path-flag-set", regs=())
        lg(0x64_88C0, 1, "path-x", ret=lambda c: self.script.get("by_path", {}).get(c.arg(0), 0), regs=())
        lg(0x64_8900, 1, "path-y", ret=lambda c: self.script.get("by_path_y", self.script.get("by_path", {})).get(c.arg(0), 0), regs=())
        lg(0x64_9EF0, 4, "path-649ef0", regs=())
        lg(0x41_35D0, 3, "name-copy", regs=())
        lg(0x62_2D00, 1, "unit-622d00", ret=lambda c: self.script.get("ret", 0), regs=())
        lg(0x46_38A0, 0, "monster-4638a0", ret=lambda c: self.script.get("ret", 0), regs=("ecx", "edx"))
        lg(0x62_01D0, 1, "right-skill", ret=lambda c: self.script.get("node", 0), regs=())
        lg(0x62_0190, 1, "left-skill", ret=lambda c: self.script.get("node", 0), regs=())
        lg(0x64_3CE0, 3, "skill-node-id", ret=lambda c: self.script.get("id", 0), regs=())
        lg(0x64_3AD0, 1, "skill-node-4", ret=lambda c: self.script.get("word", 0), regs=())
        lg(0x45_1FE0, 0, "monstats2", ret=lambda c: self.script.get("monstats2_row", 0), regs=("ecx",))
        lg(0x46_ACE0, 0, "missile-tbl", ret=lambda c: self.script.get("missile_row", 0), regs=("ecx",))
        self.m.hook(0x64_8BB0, self.target, 3)
        self.m.hook(0x64_3260, self.pixel, 2)
        self.m.hook(0x66_3410, self.seq, 7)
        lg(0x66_33B0, 1, "seq-handle", ret=lambda c: self.script.get("handle", 0), regs=())

    def fatal(self, current):
        """0x40_8a60 (message, caller address, line): the line is the third argument."""
        raise Halt(current.arg(2))

    def target(self, current):
        """0x64_8bb0 (path, &type, &id): log the path, store the scripted pair through the two out pointers."""
        self.log.append(("path-target", current.arg(0)))
        current.w32(current.arg(1), self.script.get("a", 0))
        current.w32(current.arg(2), self.script.get("b", 0))
        return 0

    def pixel(self, current):
        """0x64_3260 (&x, &y): log the inputs, replace them with the scripted world pixel spot."""
        self.log.append(("pixel-in", current.r32(current.arg(0)), current.r32(current.arg(1))))
        current.w32(current.arg(0), self.script.get("px", 0))
        current.w32(current.arg(1), self.script.get("py", 0))
        return 0

    def seq(self, current):
        """0x66_3410 (handle, a, b, out x4): log (handle, a, b), the seventh argument gets the scripted event."""
        self.log.append(("seq-663410", current.arg(0), current.arg(1), current.arg(2)))
        current.w32(current.arg(6), self.script.get("event", 0))
        return 0

    def go(self, address, *args, ecx=0, edx=0, regs=None, nstack=None):
        """One call; ("halt", line) when the fatal path ran, else ("ret", eax)."""
        CALLED.add(address)
        try:
            return ("ret", self.run(address, *args, ecx=ecx, edx=edx, regs=regs, nstack=nstack))
        except Halt as halt:
            return ("halt", halt.args[0])


class Scene:
    """Random blocks in emulator memory and a model copy of each, for whole-block comparison."""

    def __init__(self, rig, rng):
        """Remember the rig and the random source."""
        self.rig, self.rng, self.blocks = rig, rng, {}

    def block(self, size):
        """A block of random bytes; its model copy is the same bytes."""
        address = self.rig.m.alloc(size + 64)
        data = bytearray(self.rng.randbytes(size))
        self.rig.write(address, data)
        self.blocks[address] = data
        return address

    def r(self, address):
        """Model dword at address (inside one of the blocks)."""
        base = max(a for a in self.blocks if a <= address)
        return struct.unpack_from("<I", self.blocks[base], address - base)[0]

    def w(self, address, value, size=4):
        """Model write of `size` bytes."""
        base = max(a for a in self.blocks if a <= address)
        fmt = {1: "<B", 2: "<H", 4: "<I"}[size]
        struct.pack_into(fmt, self.blocks[base], address - base, value & ((1 << 8 * size) - 1))

    def b(self, address):
        """Model byte at address."""
        base = max(a for a in self.blocks if a <= address)
        return self.blocks[base][address - base]

    def word(self, address):
        """Model signed 16-bit word at address."""
        base = max(a for a in self.blocks if a <= address)
        return struct.unpack_from("<h", self.blocks[base], address - base)[0]

    def put(self, address, value, size=4):
        """Set both the emulator and the model (set-up, before the call)."""
        self.w(address, value, size)
        self.rig.write(address, struct.pack({1: "<B", 2: "<H", 4: "<I"}[size], value & ((1 << 8 * size) - 1)))

    def check(self, label):
        """Every block in emulator memory equals the model's."""
        for address, data in self.blocks.items():
            got = self.rig.m.read(address, len(data))
            if got != bytes(data):
                where = next(i for i in range(len(data)) if got[i] != data[i])
                raise mw.Mismatch(f"{label}: block {address:#x} differs at byte {where:#x}")


def signed(value):
    """A 32-bit value as a signed int."""
    value &= MASK
    return value - (1 << 32) if value & 0x80000000 else value


def sext16(value):
    """A 16-bit value as a signed int."""
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def make_unit(rig, rng, kind, rows=True):
    """A scene with a unit of `kind`, its path, its data and (for objects) a row the data points at."""
    sc = Scene(rig, rng)
    unit, path, data, row = sc.block(0x100), sc.block(0x60), sc.block(0x200), sc.block(0x200)
    sc.unit, sc.path, sc.data, sc.row = unit, path, data, row
    sc.put(unit, kind)
    sc.put(unit + 0x14, data)
    sc.put(unit + 0x2C, path)
    sc.put(data, row)
    return sc


def kinds(rng):
    """A unit type: the six real ones most of the time, an out-of-range one now and then."""
    return rng.choice((0, 1, 2, 3, 4, 5, 0, 1, 2, 4, 5, 6, 7))


def is_static(kind):
    """Types 2, 4 and 5 keep a static path (the range tests of the decompile: == 2, or 4 <= t <= 5)."""
    return kind == 2 or 4 <= kind <= 5


def expect(rig, label, got, want):
    """Compare ("ret", v) / ("halt", line) pairs."""
    require(got == want, f"{label}: game.exe {got}, model {want}")


def run_case(rig, sc, label, address, want, want_log, *args, ecx=0, edx=0, nstack=None):
    """Call, compare the result, the log and every block."""
    got = rig.go(address, *args, ecx=ecx, edx=edx, nstack=nstack)
    expect(rig, label, got, want)
    require(rig.log == want_log, f"{label}: callee log {rig.log} != {want_log}")
    sc.check(label)


def null_halt(rig, address, line, *args, ecx=0, edx=0, nstack=None):
    """A null unit: the function halts on `line`."""
    got = rig.go(address, *args, ecx=ecx, edx=edx, nstack=nstack)
    expect(rig, f"{address:#x} null unit", got, ("halt", line))


def faults(rig, label, kind, address, *args, ecx=0):
    """The call has no null check and reads (kind "READ") or writes ("WRITE") unmapped memory: game.exe's own fault."""
    try:
        got = rig.go(address, *args, ecx=ecx)
    except mw.Fault as fault:
        require(f"UC_ERR_{kind}_UNMAPPED" in str(fault), f"{label}: wrong fault {fault}")
        return
    raise mw.Mismatch(f"{label}: expected an unmapped {kind.lower()}, got {got}")


def rand32(rng):
    """A dword drawn to hit zero, small, negative and large values."""
    return rng.choice((0, 1, 2, 7, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, MASK, 0x7FFFFFFF, 0x80000000, rng.randrange(1 << 32)))


def check_tile_set(rig, rng):
    """FUN_00620360 (ECX tile, EDX a; b, c, d): a type 5 path's +0xc..+0x18; 0x6af null, 0x6b1 other types."""
    a, b, c, d = (rng.randrange(1 << 32) for _ in range(4))
    for kind in (5, 5, rng.choice((0, 1, 2, 3, 4, 6))):
        sc = make_unit(rig, rng, kind)
        if kind == 5:
            order = (b, c, d) if not mut("tile-path-swap") else (c, b, d)
            sc.w(sc.path + 0xC, a)
            sc.w(sc.path + 0x10, order[0])
            sc.w(sc.path + 0x14, order[1])
            sc.w(sc.path + 0x18, order[2])
        got = rig.go(0x62_0360, b, c, d, ecx=sc.unit, edx=a)
        if kind == 5:
            require(got[0] == "ret" and rig.log == [], f"620360 type 5: {got} {rig.log}")
        else:
            expect(rig, "620360 other type", got, ("halt", 0x6B1))
            require(rig.log == [], f"620360 other type: unexpected callee {rig.log}")
        sc.check(f"620360 kind {kind}")
    null_halt(rig, 0x62_0360, 0x6AF, 1, 2, 3, ecx=0, edx=0)


def check_tile_set48(rig, rng):
    """FUN_00620980 (ECX tile, EDX a; b): a type 5 path's +4 / +8; 0x7ff null, 0x801 other types."""
    a, b = rng.randrange(1 << 32), rng.randrange(1 << 32)
    for kind in (5, 5, rng.choice((0, 1, 2, 3, 4, 6))):
        sc = make_unit(rig, rng, kind)
        if kind == 5:
            sc.w(sc.path + 4, a)
            sc.w(sc.path + 8, b)
        got = rig.go(0x62_0980, b, ecx=sc.unit, edx=a)
        if kind == 5:
            require(got[0] == "ret" and rig.log == [], f"620980 type 5: {got} {rig.log}")
        else:
            expect(rig, "620980 other type", got, ("halt", 0x801))
            require(rig.log == [], f"620980 other type: unexpected callee {rig.log}")
        sc.check(f"620980 kind {kind}")
    null_halt(rig, 0x62_0980, 0x7FF, 1, ecx=0, edx=0)


def check_precise(rig, rng):
    """FUN_006203b0 / 00620410: static path x / y << 16, else 0x64_8940 / 0x64_8970 (path); 0x6c4 / 0x6da null."""
    for address, offset, line, name in ((0x62_03B0, 0xC, 0x6C4, "precise-x"), (0x62_0410, 0x10, 0x6DA, "precise-y")):
        for _ in range(25):
            kind = kinds(rng)
            sc = make_unit(rig, rng, kind)
            rig.script["ret"] = rng.randrange(1 << 32)
            if is_static(kind) or (mut("precise-x-static-wrong-type") and kind == 3):
                shift = 0 if mut("precise-x-no-shift") else 16
                want, log = ("ret", (sc.r(sc.path + offset) << shift) & MASK), []
            else:
                want, log = ("ret", rig.script["ret"]), [(name, sc.path)]
            run_case(rig, sc, f"{address:08x} type {kind}", address, want, log, sc.unit)
        null_halt(rig, address, line, 0)


def check_set_xy(rig, rng):
    """FUN_00620470 / 006204c0 (unit, v): a static path's x / y; 0x6ef / 0x706 null, 0x6fa / 0x711 other types."""
    for address, offset, null_line, kind_line in ((0x62_0470, 0xC, 0x6EF, 0x6FA), (0x62_04C0, 0x10, 0x706, 0x711)):
        for _ in range(25):
            kind = kinds(rng)
            sc = make_unit(rig, rng, kind)
            value = rand32(rng)
            ok = is_static(kind) or (mut("set-x-allows-missile") and kind == 3)
            if ok:
                sc.w(sc.path + offset, value)
            got = rig.go(address, sc.unit, value)
            require(got[0] == ("ret" if ok else "halt") and (ok or got[1] == kind_line), f"{address:08x} type {kind}: {got}")
            require(rig.log == [], f"{address:08x} type {kind}: unexpected callee {rig.log}")
            sc.check(f"{address:08x} type {kind}")
        null_halt(rig, address, null_line, 0, 5)


def check_distance(rig, rng):
    """FUN_00620710 / 00620780 (a, b): |x1 - x2| / |y1 - y2|; a static path's field, else 0x64_88c0 / 0x64_8900 (0 for no path)."""
    for address, offset, name in ((0x62_0710, 0xC, "path-x"), (0x62_0780, 0x10, "path-y")):
        for _ in range(60):
            first, second = kinds(rng), kinds(rng)
            s1, s2 = make_unit(rig, rng, first), make_unit(rig, rng, second)
            for sc in (s1, s2):
                if rng.random() < 0.2 and not is_static(sc.r(sc.unit)):
                    sc.put(sc.unit + 0x2C, 0)
            parts, log = [], []
            rig.script["by_path"] = {}
            for sc in (s1, s2):
                path = sc.r(sc.unit + 0x2C)
                if is_static(sc.r(sc.unit)):
                    parts.append(sc.r(path + offset))
                elif path == 0 and not mut("dx-dynamic-null-path"):
                    parts.append(0)
                else:
                    rig.script["by_path"][path] = value = rand32(rng)
                    parts.append(value)
                    log.append((name, path))
            diff = signed(parts[0]) - signed(parts[1])
            diff = ((diff + (1 << 31)) % (1 << 32)) - (1 << 31)
            result = diff & MASK if mut("dx-signed") else (-diff if diff < 0 else diff) & MASK
            got = rig.go(address, s1.unit, s2.unit)
            expect(rig, f"{address:08x} {first} {second}", got, ("ret", result))
            require(rig.log == log, f"{address:08x}: callee log {rig.log} != {log}")
            s1.check("distance a")
            s2.check("distance b")
            rig.script["by_path"] = {}


def check_screen(rig, rng):
    """FUN_006207f0 / 00620830 (unit, v): 0x64_89e0 / 0x64_89f0 (ECX the path, EDX v); 0x7b2 / 0x7be null."""
    for address, name, line in ((0x62_07F0, "screen-x", 0x7B2), (0x62_0830, "screen-y", 0x7BE)):
        for _ in range(10):
            sc = make_unit(rig, rng, kinds(rng))
            value = rand32(rng)
            hook_name = {"screen-x": "screen-y", "screen-y": "screen-x"}[name] if mut("screen-x-swapped") else name
            got = rig.go(address, sc.unit, value)
            require(got[0] == "ret" and rig.log == [(hook_name, sc.path, value)], f"{address:08x}: {got} {rig.log}")
            sc.check(f"{address:08x} screen")
        null_halt(rig, address, line, 0, 3)


def check_path_init(rig, rng):
    """FUN_00620ae0 (unit, room, x, y): static path +0 room, +0xc x, +0x10 y, byte +0x1c 0, +4 / +8 0x64_3260's (x, y); 0x866 null."""
    for _ in range(40):
        sc = make_unit(rig, rng, kinds(rng))
        room, x, y = rand32(rng), rand32(rng), rand32(rng)
        rig.script.update(px=rng.randrange(1 << 32), py=rng.randrange(1 << 32))
        sc.w(sc.path + 0xC, x)
        sc.w(sc.path + 0x10, y)
        if not mut("init-path-flag-kept"):
            sc.w(sc.path + 0x1C, 0, 1)
        sc.w(sc.path, room)
        sc.w(sc.path + 4, rig.script["px"])
        sc.w(sc.path + 8, rig.script["py"])
        got = rig.go(0x62_0AE0, sc.unit, room, x, y)
        require(got[0] == "ret" and rig.log == [("pixel-in", x, y)], f"620ae0: {got} {rig.log}")
        sc.check("620ae0")
    null_halt(rig, 0x62_0AE0, 0x866, 0, 1, 2, 3)


def check_clear_room(rig, rng):
    """FUN_00620b50 (unit): a static path's +0 = 0, else 0x64_8a90(path, 0) when there is a path; 0x87d null."""
    for _ in range(40):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        if rng.random() < 0.25 and not is_static(kind):
            sc.put(sc.unit + 0x2C, 0)
        path = sc.r(sc.unit + 0x2C)
        log = []
        if is_static(kind):
            sc.w(path, 0)
        elif path or mut("clear-room-dynamic-always"):
            log = [("path-room", path, 0)]
        got = rig.go(0x62_0B50, sc.unit)
        require(got[0] == "ret" and rig.log == log, f"620b50 type {kind}: {got} {rig.log} {log}")
        sc.check("620b50")
    null_halt(rig, 0x62_0B50, 0x87D, 0)


def check_target(rig, rng):
    """FUN_00620c70 / 00620cd0 (unit): 0x64_8bb0's first / second output for a player, monster or missile; 0x8b9 / 0x8ca null, 0x8bb / 0x8cc others."""
    for address, which, null_line, kind_line in ((0x62_0C70, "a", 0x8B9, 0x8BB), (0x62_0CD0, "b", 0x8CA, 0x8CC)):
        for _ in range(30):
            kind = kinds(rng)
            sc = make_unit(rig, rng, kind)
            rig.script.update(a=rand32(rng), b=rand32(rng))
            ok = kind in (0, 1, 3) or (mut("target-type-allows-item") and kind == 4)
            want = ("ret", rig.script[which]) if ok else ("halt", kind_line)
            got = rig.go(address, sc.unit)
            expect(rig, f"{address:08x} type {kind}", got, want)
            require(rig.log == ([("path-target", sc.path)] if ok else []), f"{address:08x} type {kind}: log {rig.log}")
            sc.check("target")
        null_halt(rig, address, null_line, 0)


def check_set_target(rig, rng):
    """FUN_00620d30 (unit, v): 0x64_8b90(path, v) for a player or monster; 0x8dc null, 0x8de others."""
    for _ in range(40):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        value = rand32(rng)
        if kind in (0, 1):
            want, log = ("ret", None), [("path-target-set", sc.path, value)]
        else:
            want, log = ("halt", 0x8DE), []
        got = rig.go(0x62_0D30, sc.unit, value)
        require(got == want or (got[0] == "ret" and want[0] == "ret"), f"620d30 type {kind}: {got} != {want}")
        require(rig.log == log, f"620d30 type {kind}: log {rig.log} != {log}")
        sc.check("620d30")
    null_halt(rig, 0x62_0D30, 0x8DC, 0, 1)


def check_path_flag(rig, rng):
    """FUN_00620f50 / 00620fa0: path flag 2. Get: static (types 2, 4) the byte at +0x1d, else 0x64_8b30(path); set: static (2, 4, 5) +0x1d = (v != 0), else 0x64_8b40(path, v). A tile (type 5) is dynamic for the get and static for the set."""
    for _ in range(40):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        rig.script["ret"] = rng.choice((0, 2, rng.randrange(1 << 32)))
        if kind in (2, 4) or (mut("flag2-get-type5-static") and kind == 5):
            want, log = ("ret", sc.b(sc.path + 0x1D)), []
        else:
            want, log = ("ret", rig.script["ret"]), [("path-flag-get", sc.path)]
        got = rig.go(0x62_0F50, sc.unit)
        expect(rig, f"620f50 type {kind}", got, want)
        require(rig.log == log, f"620f50 type {kind}: log {rig.log} != {log}")
        sc.check("620f50")
    for _ in range(60):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        value = rand32(rng)
        if is_static(kind):
            sc.w(sc.path + 0x1D, (value & 0xFF) if mut("flag2-set-nonbool") else int(value != 0), 1)
            log = []
        else:
            log = [("path-flag-set", sc.path, value)]
        got = rig.go(0x62_0FA0, sc.unit, value)
        require(got[0] == "ret" and rig.log == log, f"620fa0 type {kind}: {got} {rig.log}")
        sc.check("620fa0")
    null_halt(rig, 0x62_0F50, 0x9EC, 0)
    null_halt(rig, 0x62_0FA0, 0x9FE, 0, 1)


def check_take(rig, rng):
    """FUN_00621150 (unit): returns +0x64 and clears it; 0xa6d null."""
    for _ in range(10):
        sc = make_unit(rig, rng, kinds(rng))
        value = sc.r(sc.unit + 0x64)
        if not mut("take-keeps"):
            sc.w(sc.unit + 0x64, 0)
        run_case(rig, sc, "621150", 0x62_1150, ("ret", value), [], sc.unit)
    null_halt(rig, 0x62_1150, 0xA6D, 0)


def check_mode_field(rig, rng):
    """FUN_006211c0 (unit, v): +0x40 for a player or monster with +0x30 set, else +0x10; 0xa92 null."""
    for _ in range(60):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        if rng.random() < 0.4:
            sc.put(sc.unit + 0x30, 0)
        value = rand32(rng)
        if (kind in (0, 1) and sc.r(sc.unit + 0x30)) or mut("mode-field-always"):
            sc.w(sc.unit + 0x40, value)
        else:
            sc.w(sc.unit + 0x10, value)
        got = rig.go(0x62_11C0, sc.unit, value)
        require(got[0] == "ret", f"6211c0: {got}")
        sc.check("6211c0")
    null_halt(rig, 0x62_11C0, 0xA92, 0, 1)


def check_set_frame(rig, rng):
    """FUN_006212c0 (unit, frame): +0x38 = (+0x38 & 0xff) + frame * 256 (32-bit); 0xace null."""
    for _ in range(40):
        sc = make_unit(rig, rng, kinds(rng))
        frame = rand32(rng)
        old = sc.r(sc.unit + 0x38)
        sc.w(sc.unit + 0x38, (old if mut("frame-keeps-low") else old & 0xFF) + frame * 256)
        got = rig.go(0x62_12C0, sc.unit, frame)
        require(got[0] == "ret", "6212c0")
        sc.check("6212c0")
    null_halt(rig, 0x62_12C0, 0xACE, 0, 1)


def check_advance(rig, rng):
    """FUN_00621310 (unit) / 00621330 (unit, start, end): +0x44 += signed word +0x4c, then wrap by +0x48 / by (end - start) * 256 when at or past the limit (signed)."""
    for _ in range(80):
        sc = make_unit(rig, rng, kinds(rng))
        pos = rng.choice((0, 5, 0x1FF, 0x200, rng.randrange(1 << 16), 0x7FFFFFF0, 0x80000010, MASK))
        sc.put(sc.unit + 0x44, pos)
        length = rng.choice((0, 0x100, 0x200, 0x1000, rng.randrange(1 << 16), 0x80000000, MASK))
        sc.put(sc.unit + 0x48, length)
        sc.put(sc.unit + 0x4C, rng.choice((0, 1, 0x100, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1 << 16))), 2)
        new = (pos + sext16(sc.r(sc.unit + 0x4C) & 0xFFFF)) & MASK
        limit = signed(length)
        if (new if mut("advance-unsigned") else signed(new)) >= (length if mut("advance-unsigned") else limit):
            new = (new - length) & MASK
        sc.w(sc.unit + 0x44, new)
        got = rig.go(0x62_1310, sc.unit)
        require(got[0] == "ret", "621310")
        sc.check("621310")
    for _ in range(80):
        sc = make_unit(rig, rng, kinds(rng))
        pos = rng.choice((0, 5, 0x1FF, 0x200, rng.randrange(1 << 16), 0x7FFFFFF0, MASK))
        sc.put(sc.unit + 0x44, pos)
        sc.put(sc.unit + 0x4C, rng.choice((0, 1, 0x100, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1 << 16))), 2)
        start, end = rng.choice((0, 1, 3, 8, 0x7F, 0xFFFFFFFF)), rng.choice((0, 1, 4, 9, 0x80, 0x7FFFFF, 0xFFFFFFFE))
        new = (pos + sext16(sc.r(sc.unit + 0x4C) & 0xFFFF)) & MASK
        if signed(end * 256) <= signed(new):
            new = (new + (end if mut("advance-range-sign") else start) * 256 - end * 256) & MASK
        sc.w(sc.unit + 0x44, new)
        got = rig.go(0x62_1330, sc.unit, start, end)
        require(got[0] == "ret", "621330")
        sc.check("621330")


def check_row(rig, rng):
    """FUN_00621420 (EAX n; limit): 0x6e8b90 + n * 0x14 when limit < 0x19a (signed) else 0x6e8cd0 + n * 0x14."""
    for _ in range(80):
        n = rng.choice((0, 1, 2, 0x19, 0x19A, rng.randrange(1 << 32), rng.randrange(0x30)))
        limit = rng.choice((0, 0x199, 0x19A, 0x19B, MASK, 0x80000000, 0x7FFFFFFF, rng.randrange(1 << 32)))
        base = 0x6E8B90 if (signed(limit) < 0x19A if not mut("row-boundary") else signed(limit) <= 0x19A) else 0x6E8CD0
        got = rig.go(0x62_1420, limit, regs={"eax": n})
        expect(rig, "621420", got, ("ret", (base + n * 0x14) & MASK))


def check_frame_length(rig, rng):
    """FUN_00621870 (unit, &frame, &length): (0, 0) for null; +0x44 and an object's Objects.txt +0xd8 + mode * 4, else +0x48."""
    for _ in range(60):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        mode = rng.choice((0, 1, 5, 7, rng.randrange(0x20)))
        sc.put(sc.unit + 0x10, mode)
        out = sc.block(8)
        frame = sc.r(sc.unit + 0x44)
        length = sc.r(sc.row + 0xD8 + 4 * (mode if not mut("length-wrong-mode") else 0)) if kind == 2 else sc.r(sc.unit + 0x48)
        sc.w(out, frame)
        sc.w(out + 4, length)
        got = rig.go(0x62_1870, sc.unit, out, out + 4)
        require(got[0] == "ret", "621870")
        sc.check(f"621870 type {kind}")
    out = Scene(rig, rng)
    block = out.block(8)
    out.w(block, 0)
    out.w(block + 4, 0)
    rig.go(0x62_1870, 0, block, block + 4)
    out.check("621870 null unit")


def check_event_copy(rig, rng):
    """FUN_006218d0 (unit, frame): +0x4e = the event byte 1..4 at +0x50's data +0x10 + frame when frame < 0x90 (signed); nothing for none."""
    for _ in range(80):
        sc = make_unit(rig, rng, kinds(rng))
        anim = sc.block(0xC0)
        sc.put(sc.unit + 0x50, anim if rng.random() > 0.15 else 0)
        frame = rng.choice((0, 1, 5, 0x8F, 0x90, 0x91, 0x7FFFFFFF, MASK - 1, MASK, rng.randrange(0x90)))
        if sc.r(sc.unit + 0x50) and signed(frame) < 0x90:
            if signed(frame) < -0x10:
                continue
            index = anim + 0x10 + signed(frame)
            if rng.random() < 0.7:
                sc.put(index, rng.choice((0, 1, 2, 3, 4, 5, 6, 0xFF)), 1)
            value = sc.b(index)
            if value in (1, 2, 3, 4) or (mut("event-copy-5") and value == 5):
                sc.w(sc.unit + 0x4E, value, 1)
        got = rig.go(0x62_18D0, sc.unit, frame)
        require(got[0] == "ret", "6218d0")
        sc.check("6218d0")
    got = rig.go(0x62_18D0, 0, 3)
    require(got[0] == "ret", "6218d0 null")


def check_event(rig, rng):
    """FUN_00621920 (unit, frame): 0 from frame 0x90 (signed); a player in mode 0x12 or a player or monster in mode 0xe reads its sequence (0x66_33b0, 0x66_3410), else the byte at +0x50's... data +0x10 + frame; 0xdcc null."""
    for _ in range(100):
        kind = rng.choice((0, 1, 2, 3, 0, 1))
        sc = make_unit(rig, rng, kind)
        anim = sc.block(0x120)
        sc.put(sc.unit + 0x50, anim + 0x20)
        mode = rng.choice((0, 0xC, 0xE, 0x12, 0x12, 0xE, rng.randrange(0x20)))
        sc.put(sc.unit + 0x10, mode)
        frame = rng.choice((0, 1, 0x8F, 0x90, 0x91, MASK, rng.randrange(0x90)))
        rig.script.update(handle=rng.randrange(1 << 32), event=rng.randrange(1 << 32))
        if signed(frame) > 0x8F:
            want, log = ("ret", 0), []
        elif (kind == 0 and mode == 0x12) or (kind in (0, 1) and mode == 0xE and not mut("event-seq-monster-only")) or \
                (kind == 1 and mode == 0xE):
            want = ("ret", rig.script["event"])
            log = [("seq-handle", sc.unit), ("seq-663410", rig.script["handle"], (frame << 8) & MASK, (frame << 8) & MASK)]
        else:
            index = sc.r(sc.unit + 0x50) + 0x10 + signed(frame)
            if signed(frame) < -0x10:
                continue
            want, log = ("ret", sc.b(index)), []
        got = rig.go(0x62_1920, sc.unit, frame)
        expect(rig, f"621920 type {kind} mode {mode:#x} frame {frame:#x}", got, want)
        require(rig.log == log, f"621920 log {rig.log} != {log}")
        sc.check("621920")
    null_halt(rig, 0x62_1920, 0xDCC, 0, 1)


def check_object_data(rig, rng):
    """The Objects.txt row readers: FUN_006219c0 (+0x120 + mode, 1 for others), 00621a70 (+0x13a, 0; 0xe10 null), 00621ac0 (ECX; +0x13c == 1; 0xe1e not an object), 00621b00 (+0x167 bit 0), 00622270 (+0x168; 0xfd3 null), 00621b70 (data +8; 0xe4e null, 0xe55 not an object). EAX is compared whole (byte loads are zero-extended, the tests return 0 or 1). 006219c0 and 00621ac0 have no null check (read from the disassembly); a null unit faults in game.exe's own read, which the case below requires."""
    for _ in range(40):
        kind = rng.choice((0, 1, 2, 2, 2, 3, 4, 5))
        sc = make_unit(rig, rng, kind)
        mode = rng.choice((0, 1, 7, rng.randrange(8)))
        sc.put(sc.unit + 0x10, mode)
        obj = kind == 2
        sc.put(sc.row + 0x13C, rng.choice((0, 1, 1, 2, 0xFF)), 1)
        want = sc.b(sc.row + 0x120 + (mode if not mut("obj-mode-byte") else 0)) if obj else 1
        got = rig.go(0x62_19C0, sc.unit)
        require(got == ("ret", want), f"6219c0 type {kind}: {got} != {want}")
        got = rig.go(0x62_1A70, sc.unit)
        require(got == ("ret", sc.b(sc.row + 0x13A) if obj else 0), f"621a70 type {kind}: {got}")
        expect(rig, "622270", rig.go(0x62_2270, sc.unit), ("ret", sc.r(sc.row + 0x168) if obj else 0))
        expect(rig, "621b00", rig.go(0x62_1B00, sc.unit), ("ret", int(obj and bool(sc.b(sc.row + 0x167) & 1))))
        want_13c = sc.b(sc.row + 0x13C) == (0 if mut("txt-13c-nonzero") else 1)
        got = rig.go(0x62_1AC0, ecx=sc.unit)
        expect(rig, "621ac0", got, ("ret", int(want_13c)) if obj else ("halt", 0xE1E))
        expect(rig, "621b70", rig.go(0x62_1B70, sc.unit), ("ret", sc.r(sc.data + 8)) if obj else ("halt", 0xE55))
        require(rig.log == [], f"object readers: unexpected callee {rig.log}")
        sc.check("object readers")
    for address, line in ((0x62_1A70, 0xE10), (0x62_1B70, 0xE4E), (0x62_2270, 0xFD3)):
        null_halt(rig, address, line, 0)
    faults(rig, "6219c0 null unit", "READ", 0x62_19C0, 0)       # no null check
    faults(rig, "621ac0 null unit", "READ", 0x62_1AC0, ecx=0)   # no null check


def check_object_flags(rig, rng):
    """FUN_006222c0 (get), 00622300 (set), 00622350 (test mask): an object's data byte +5; 0xfe4 / 0xfef / 0xffa null, 0xfe5 / 0xff0 / 0xffb for other types. The getter's EAX keeps the data pointer's upper bytes; the test is 0 or 1."""
    for _ in range(60):
        kind = rng.choice((2, 2, 2, 0, 1, 4))
        sc = make_unit(rig, rng, kind)
        value, mask = rng.randrange(256), rng.choice((1, 2, 0x80, 0xFF, rng.randrange(256)))
        got = rig.go(0x62_22C0, sc.unit)
        if kind == 2:
            require(got == ("ret", (sc.data & 0xFFFFFF00) | sc.b(sc.data + 5)), f"6222c0: {got}")
        else:
            require(got == ("halt", 0xFE5), f"6222c0 type {kind}: {got}")
        got = rig.go(0x62_2350, sc.unit, mask)
        if kind == 2:
            hit = (sc.b(sc.data + 5) & (0xFF if mut("test-data5-all") else mask)) != 0
            require(got == ("ret", int(hit)), f"622350: {got}")
        else:
            require(got == ("halt", 0xFFB), f"622350 type {kind}: {got}")
        got = rig.go(0x62_2300, sc.unit, value)
        if kind == 2:
            sc.w(sc.data + 5, value, 2 if mut("set-data5-word") else 1)
            require(got[0] == "ret", "622300")
        else:
            require(got == ("halt", 0xFF0), f"622300 type {kind}: {got}")
        sc.check("object flags")
    for address, line, args in ((0x62_22C0, 0xFE4, (0,)), (0x62_2300, 0xFEF, (0, 3)), (0x62_2350, 0xFFA, (0, 3))):
        null_halt(rig, address, line, *args)


def check_misc_unit(rig, rng):
    """FUN_00621c00 (unit, a, b): 0x64_9ef0(path, a, b, 0) when the unit has a path. FUN_00621f60: a monster and 0x46_38a0(ECX = the unit's class +4, EDX = 0xc) != 0, as 0 or 1. FUN_00622dc0: 0x62_2d00(unit) != 0. FUN_00622e90: touches nothing. FUN_006234c0 / 006234f0 (ECX; EDX): unit +0x7c set / get, 0x150b / 0x1513 null."""
    for _ in range(40):
        sc = make_unit(rig, rng, kinds(rng))
        if rng.random() < 0.3:
            sc.put(sc.unit + 0x2C, 0)
        a, b = rand32(rng), rand32(rng)
        log = [("path-649ef0", sc.r(sc.unit + 0x2C), a, b, 0)] if sc.r(sc.unit + 0x2C) or mut("path-649ef0-null") else []
        got = rig.go(0x62_1C00, sc.unit, a, b)
        require(got[0] == "ret" and rig.log == log, f"621c00: {got} {rig.log} {log}")
        sc.check("621c00")
        rig.script["ret"] = rng.choice((0, 1, 0x80000000))
        kind = sc.r(sc.unit)
        got = rig.go(0x62_1F60, sc.unit)
        require(got == ("ret", int(kind == 1 and rig.script["ret"] != 0)), f"621f60 type {kind}: {got}")
        want_edx = 0xD if mut("monster-flag-edx") else 0xC
        require(rig.log == ([("monster-4638a0", sc.r(sc.unit + 4), want_edx)] if kind == 1 else []), f"621f60 log {rig.log}")
        sc.check("621f60")
        got = rig.go(0x62_2DC0, sc.unit)
        require(got == ("ret", int(rig.script["ret"] != 0)) and rig.log == [("unit-622d00", sc.unit)], f"622dc0: {got} {rig.log}")
        sc.check("622dc0")
        value = rand32(rng)
        sc.w(sc.unit + 0x7C, value)
        require(rig.go(0x62_34C0, ecx=sc.unit, edx=value)[0] == "ret", "6234c0")
        sc.check("6234c0")
        expect(rig, "6234f0", rig.go(0x62_34F0, ecx=sc.unit), ("ret", sc.r(sc.unit + 0x7C)))
        require(rig.log == [], f"6234f0: unexpected callee {rig.log}")
        sc.check("6234f0")
    got = rig.go(0x62_1F60, 0)
    require(got == ("ret", 0), "621f60 null")
    null_halt(rig, 0x62_34C0, 0x150B, ecx=0, edx=3)
    null_halt(rig, 0x62_34F0, 0x1513, ecx=0)
    sc = make_unit(rig, rng, 0)
    got = rig.go(0x62_2E90, 0xDEAD, 0xBEEF, ecx=sc.unit, edx=sc.data)
    require(got[0] == "ret" and rig.log == [], f"622e90: {got} {rig.log}")
    sc.check("622e90 touches nothing")


def check_player_checks(rig, rng):
    """FUN_00622150 (checks only: 0xfa5 null, 0xfa7 no data, 0xfa8 not a player), FUN_006220f0 (unit, name): the same checks (0xf97 / 0xf99 / 0xf9a), then 0x41_35d0(data, name, 0x7fffffff); FUN_006221e0 / 00622230 (unit, v): player data +0x2c set / get, 0xfbf / 0xfc9 null, 0xfc0 / 0xfca not a player (no data check)."""
    for _ in range(40):
        kind = rng.choice((0, 0, 1, 2, 4))
        sc = make_unit(rig, rng, kind)
        if rng.random() < 0.25:
            sc.put(sc.unit + 0x14, 0)
        data = sc.r(sc.unit + 0x14)
        name = rng.randrange(1 << 32)
        for address, data_line, kind_line, args in ((0x62_2150, 0xFA7, 0xFA8, ()), (0x62_20F0, 0xF99, 0xF9A, (name,))):
            got = rig.go(address, sc.unit, *args)
            if data == 0:
                want = ("halt", data_line)
            elif kind != 0:
                want = ("halt", kind_line)
            else:
                want = ("ret", None)
            require(got == want or (got[0] == "ret" and want[0] == "ret"), f"{address:08x} type {kind} data {data:#x}: {got} != {want}")
            if address == 0x62_20F0:
                require(rig.log == ([("name-copy", data, name, 0x7FFFFFFF)] if want[0] == "ret" else []), f"6220f0 log {rig.log}")
        value = rng.randrange(1 << 32)
        if kind == 0 and not data:
            # no data check: the write / read goes to address 0x2c
            faults(rig, "6221e0 no data", "WRITE", 0x62_21E0, sc.unit, value)
            faults(rig, "622230 no data", "READ", 0x62_2230, sc.unit)
            sc.check("player checks, no data")
            continue
        got = rig.go(0x62_21E0, sc.unit, value)
        if kind == 0 and data:
            sc.w(data + 0x2C, value)
            require(got[0] == "ret", "6221e0")
            expect(rig, "622230", rig.go(0x62_2230, sc.unit), ("ret", value))
        elif kind != 0:
            require(got == ("halt", 0xFC0), f"6221e0 type {kind}: {got}")
            expect(rig, "622230 type", rig.go(0x62_2230, sc.unit), ("halt", 0xFCA))
        sc.check("player checks")
    for address, line, args in ((0x62_2150, 0xFA5, ()), (0x62_20F0, 0xF97, (1,)), (0x62_21E0, 0xFBF, (1,)), (0x62_2230, 0xFC9, ())):
        null_halt(rig, address, line, 0, *args)


def check_kept_skills(rig, rng):
    """The kept-skill pairs in a player's data: FUN_00622ea0 / 00622f10 keep the right / left skill at +0x70/+0x78 / +0x74/+0x7c (id from 0x64_3ce0, +4 word from 0x64_3ad0); 00622f80 / 00622ff0 keep them at +0x80/+0x88 and +0x84/+0x8c. Not a player: nothing; a player without data: 0x13f6 / 0x1405 / 0x1414 / 0x1424."""
    keepers = ((0x62_2EA0, "right-skill", 0x70, 0x78, 0x13F6, 0x13FA), (0x62_2F10, "left-skill", 0x74, 0x7C, 0x1405, 0x1409),
               (0x62_2F80, "right-skill", 0x80, 0x88, 0x1414, 0x1419), (0x62_2FF0, "left-skill", 0x84, 0x8C, 0x1424, 0x1429))
    for address, hand, off_id, off_word, null_line, id_line in keepers:
        for _ in range(30):
            kind = rng.choice((0, 0, 0, 1, 2))
            sc = make_unit(rig, rng, kind)
            if rng.random() < 0.15:
                sc.put(sc.unit + 0x14, 0)
            data = sc.r(sc.unit + 0x14)
            rig.script.update(node=rng.randrange(1, 1 << 32), id=rng.randrange(1 << 32), word=rng.randrange(1 << 32))
            if mut("skill-keep-swapped"):
                off_id, off_word = off_word, off_id
            if kind != 0:
                want, log = ("ret", None), []
            elif data == 0:
                want, log = ("halt", null_line), []
            else:
                sc.w(data + off_id, rig.script["id"])
                sc.w(data + off_word, rig.script["word"])
                want = ("ret", None)
                log = [(hand, sc.unit), ("skill-node-id", rig.script["node"], FILE_ADDRESS, id_line), ("skill-node-4", rig.script["node"])]
            got = rig.go(address, sc.unit)
            require(got == want or (got[0] == "ret" and want[0] == "ret"), f"{address:08x} type {kind}: {got} != {want}")
            require(rig.log == log, f"{address:08x}: log {rig.log} != {log}")
            sc.check(f"{address:08x}")
            if mut("skill-keep-swapped"):
                off_id, off_word = off_word, off_id
        got = rig.go(address, 0)
        require(got[0] == "ret", f"{address:08x} null unit is a no-op: {got}")


def check_kept_readers(rig, rng):
    """FUN_00623060 / 006230c0 (unit, &id, &word) read +0x70/+0x78 / +0x74/+0x7c, 00623220 / 00623290 read +0x84/+0x8c / +0x80/+0x88; 00623120 / 006231a0 read +0x80/+0x88 / +0x84/+0x8c and also reset +0x70/+0x78 (or +0x74/+0x7c) to (0, -1) and +0x90/+0x94 to (-1, 0). A non-player or null unit gives (0, -1); a player without data halts (0x1438, 0x1449, 0x145a, 0x1470, 0x1487, 0x1499)."""
    readers = ((0x62_3060, 0x70, 0x78, None, 0x1438), (0x62_30C0, 0x74, 0x7C, None, 0x1449), (0x62_3120, 0x80, 0x88, (0x70, 0x78), 0x145A),
               (0x62_31A0, 0x84, 0x8C, (0x74, 0x7C), 0x1470), (0x62_3220, 0x84, 0x8C, None, 0x1487), (0x62_3290, 0x80, 0x88, None, 0x1499))
    for address, off_id, off_word, reset, line in readers:
        for _ in range(30):
            kind = rng.choice((0, 0, 0, 1, 2, 3))
            sc = make_unit(rig, rng, kind)
            if rng.random() < 0.15:
                sc.put(sc.unit + 0x14, 0)
            data = sc.r(sc.unit + 0x14)
            out = sc.block(8)
            if kind != 0:
                sc.w(out, 0)
                sc.w(out + 4, 0 if mut("kept-getter-default") else MASK)
                want = ("ret", None)
            elif data == 0:
                want = ("halt", line)
            else:
                sc.w(out, sc.r(data + off_id))
                sc.w(out + 4, sc.r(data + off_word))
                if reset and not mut("switch-read-no-clear"):
                    sc.w(data + reset[0], 0)
                    sc.w(data + reset[1], MASK)
                    sc.w(data + 0x90, MASK)
                    sc.w(data + 0x94, 0)
                want = ("ret", None)
            got = rig.go(address, sc.unit, out, out + 4)
            require(got == want or (got[0] == "ret" and want[0] == "ret"), f"{address:08x} type {kind}: {got} != {want}")
            if want[0] == "halt":
                continue
            sc.check(f"{address:08x} type {kind}")
        out = Scene(rig, rng)
        block = out.block(8)
        out.w(block, 0)
        out.w(block + 4, MASK)
        rig.go(address, 0, block, block + 4)
        out.check(f"{address:08x} null unit")


def check_kept_setters(rig, rng):
    """FUN_00623300 / 00623350 (unit, id, word): player data +0x84/+0x8c / +0x80/+0x88; not a player: nothing; no data: 0x14a8 / 0x14b7. FUN_006233a0 (unit, item): +0x90 = item id (+0xc) or 0 for none; 0x14c5 no data, 0x14c9 not an item; a non-player is ignored. FUN_00623410: the +0x90 read, 0xfb4 null, 0xfb6 not a player, 0x14d7 no data."""
    for address, off_id, off_word, line in ((0x62_3300, 0x84, 0x8C, 0x14A8), (0x62_3350, 0x80, 0x88, 0x14B7)):
        for _ in range(30):
            kind = rng.choice((0, 0, 1, 4))
            sc = make_unit(rig, rng, kind)
            if rng.random() < 0.15:
                sc.put(sc.unit + 0x14, 0)
            data = sc.r(sc.unit + 0x14)
            a, b = rand32(rng), rand32(rng)
            if kind == 0 and data:
                sc.w(data + off_id, a)
                sc.w(data + off_word, b)
                want = ("ret", None)
            elif kind == 0:
                want = ("halt", line)
            else:
                want = ("ret", None)
            got = rig.go(address, sc.unit, a, b)
            require(got == want or (got[0] == "ret" and want[0] == "ret"), f"{address:08x} type {kind}: {got} != {want}")
            sc.check(f"{address:08x}")
        require(rig.go(address, 0, 1, 2)[0] == "ret", "null no-op")
    for _ in range(60):
        kind = rng.choice((0, 0, 0, 1, 4))
        sc = make_unit(rig, rng, kind)
        if rng.random() < 0.15:
            sc.put(sc.unit + 0x14, 0)
        data = sc.r(sc.unit + 0x14)
        item_kind = rng.choice((4, 4, 3, 0, 2))
        item_scene = make_unit(rig, rng, item_kind)
        pick = rng.choice((item_scene.unit, item_scene.unit, 0))
        if kind != 0:
            want = ("ret", None)
        elif data == 0:
            want = ("halt", 0x14C5)
        elif pick == 0:
            sc.w(data + 0x90, 0)
            want = ("ret", None)
        elif item_kind == 4 or (mut("item-90-other-type") and item_kind == 3):
            sc.w(data + 0x90, item_scene.r(pick + 0xC))
            want = ("ret", None)
        else:
            want = ("halt", 0x14C9)
        got = rig.go(0x62_33A0, sc.unit, pick)
        require(got == want or (got[0] == "ret" and want[0] == "ret"), f"6233a0 type {kind}/{item_kind}: {got} != {want}")
        sc.check("6233a0")
        got = rig.go(0x62_3410, sc.unit)
        if kind != 0:
            require(got == ("halt", 0xFB6), f"623410 type {kind}: {got}")
        elif data == 0:
            require(got == ("halt", 0x14D7), f"623410 no data: {got}")
        else:
            require(got == ("ret", sc.r(data + 0x90)), f"623410: {got}")
        require(rig.log == [], f"623410: unexpected callee {rig.log}")
        sc.check("623410")
    require(rig.go(0x62_33A0, 0, 0)[0] == "ret", "6233a0 null unit ignored")
    null_halt(rig, 0x62_3410, 0xFB4, 0)


def check_static_path_alloc(rig, rng):
    """FUN_00623520 (unit): with no path, allocate 0x20 bytes from the unit's pool (+8), file Units.cpp line 0x151e, zero them, store at +0x2c; with a path nothing. FUN_00623570 (unit): with a path release it (pool +8, block = the path, line 0x152b) but leave +0x2c as it was."""
    for _ in range(30):
        sc = make_unit(rig, rng, kinds(rng))
        has = rng.random() < 0.5
        if not has:
            sc.put(sc.unit + 0x2C, 0)
        pool = sc.r(sc.unit + 8)
        before = rig.m.brk
        got = rig.go(0x62_3520, sc.unit)
        require(got[0] == "ret", "623520")
        if has and not mut("static-path-alloc-always"):
            require(rig.log == [], f"623520 with a path: {rig.log}")
            sc.check("623520 kept")
        else:
            require(len(rig.log) == 1 and rig.log[0][:3] == ("alloc", pool, 0x20) and rig.log[0][3:] == (FILE, 0x151E, 0),
                    f"623520 alloc {rig.log}")
            new = rig.m.r32(sc.unit + 0x2C)
            require(new >= before and rig.m.read(new, 0x20) == bytes(0x20), "623520 block not zeroed")
            sc.w(sc.unit + 0x2C, new)
            sc.blocks[new] = bytearray(0x20)
            sc.check("623520 new path")
        path = rig.m.r32(sc.unit + 0x2C)
        got = rig.go(0x62_3570, sc.unit)
        require(got[0] == "ret", "623570")
        require(rig.log == [("free", pool, path, FILE, 0x152B, 0)], f"623570 release {rig.log}")
        if mut("static-path-free-clears"):
            sc.w(sc.unit + 0x2C, 0)
        sc.check("623570 keeps the dangling pointer")
        if not mut("static-path-free-clears"):
            # the stale pointer stays: another release frees the same block, FUN_00623520 sees a path and allocates nothing
            rig.go(0x62_3570, sc.unit)
            require(rig.log == [("free", pool, path, FILE, 0x152B, 0)], f"623570 twice {rig.log}")
            rig.go(0x62_3520, sc.unit)
            require(rig.log == [], f"623520 after a release allocated {rig.log}")
            sc.check("623570 twice")
        sc.put(sc.unit + 0x2C, 0)
        rig.go(0x62_3570, sc.unit)
        require(rig.log == [], "623570 with no path")


def check_unit_size_y(rig, rng):
    """FUN_006205a0 (unit): player 2; monster MonStats2Txt +9 SizeY (0x45_1fe0 by class, null row 0); object
    data's row +0xd4 SizeY; missile MissilesTxt +0x18a Size (0x46_ace0 by class, null row 0); item 1; any
    other type (or a not-found row) 0. No null-unit check in the decompile (no fatal line, unlike its
    neighbours)."""
    for _ in range(40):
        kind = kinds(rng)
        sc = make_unit(rig, rng, kind)
        log = []
        if kind == 0:
            want = 2
        elif kind == 1:
            found = rng.random() < 0.8
            class_id = sc.r(sc.unit + 4)
            if found:
                row = sc.block(0x20)
                byte = rng.randrange(256)
                sc.put(row + 9, byte, 1)
                want = byte - 0x100 if byte & 0x80 else byte  # SizeY is a signed char, sign-extended to int
                rig.script["monstats2_row"] = row
            else:
                rig.script["monstats2_row"] = 0
                want = 0
            log = [("monstats2", class_id)]
        elif kind == 2:
            want = sc.r(sc.row + 0xD4)
        elif kind == 3:
            found = rng.random() < 0.8
            class_id = sc.r(sc.unit + 4)
            if found:
                row = sc.block(0x200)
                want = rng.randrange(256)
                sc.put(row + 0x18A, want, 1)
                rig.script["missile_row"] = row
            else:
                rig.script["missile_row"] = 0
                want = 0
            log = [("missile-tbl", class_id)]
        elif kind == 4:
            want = 1
        else:
            want = 0
        got = rig.go(0x62_05A0, sc.unit)
        expect(rig, f"6205a0 kind {kind}", got, ("ret", want & MASK))
        require(rig.log == log, f"6205a0 kind {kind}: callee log {rig.log} != {log}")
        sc.check(f"6205a0 kind {kind}")
        rig.script["monstats2_row"] = rig.script["missile_row"] = 0


def run_all():
    """Run every check; returns the case count."""
    rng = random.Random(0x620300)
    rig = Rig()
    rig.m.w32(0x744304, 0)
    plan = [(check_tile_set, 8), (check_tile_set48, 8), (check_precise, 4), (check_set_xy, 4), (check_distance, 3),
            (check_screen, 3), (check_path_init, 2), (check_clear_room, 4), (check_target, 4), (check_set_target, 3),
            (check_path_flag, 4), (check_take, 3), (check_mode_field, 3), (check_set_frame, 3), (check_advance, 3),
            (check_row, 3), (check_frame_length, 3), (check_event_copy, 3), (check_event, 3), (check_object_data, 4),
            (check_object_flags, 4), (check_misc_unit, 4), (check_player_checks, 4), (check_kept_skills, 4),
            (check_kept_readers, 4), (check_kept_setters, 4), (check_static_path_alloc, 4),
            (check_unit_size_y, 1)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    missing = [f"{a:#x}" for a in COVERED if a not in CALLED]
    require(not missing, f"functions never called: {missing}")
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "UNITS small-accessor check rounds")


if __name__ == "__main__":
    main()
