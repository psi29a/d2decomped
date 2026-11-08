# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the PATH\\Path.cpp dynamic-path accessors in stock game.exe 1.14d.

    uv run python path_dynamic.py

Runs game.exe's own code for 70 small functions between FUN_00648460 and
FUN_00649ca0 (docs/research/re/path.md lists them) against a hand-written
model of each, on random path/unit records: the whole 0x240-byte path and
0x100-byte unit are compared afterwards, not just the field a function is
meant to touch, and so is its return value (to the width it returns).
A fatal-error branch (ERROR_UnrecoverableInternalError_Halt) is caught by
hooking FUN_00408a60 and must give the line number the model expects.
Callees that are not the claim are hooked and their arguments recorded:
the allocator free (0x40b480), the collision add/remove entries, the
recalc / reached-target / update-flag helpers, the missile builders.
Everything else (path-type tables, direction table, the world-to-iso
helper 0x643540) is game.exe's own. No MPQs are read.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LIMIT = 20000
PSIZE, USIZE = 0x240, 0x100
ROUNDS = 24
HALT = "halt"

# Addresses of game.exe data and helpers used below.
GET_IP, ERR_HALT = 0x408090, 0x408A60
FREE_ENTRY = 0x40B480
DIFF_DIRECTIONS = 0x6EB548          # 64 entries, 4 bytes apart
PATH_TYPE_POINTS = 0x6EB648         # 18 dwords
PATH_TYPE_FLAGS = 0x6EB690          # 18 dwords
MON_TABLE, MON_MASKS = 0x744304, 0x6CE268   # sgptDataTable, gadwBitMasks
PATH_FILE = 0x6EB534                # ".\\PATH\\Path.cpp"

M32 = 0xFFFFFFFF


def require(condition, message):
    """Stop the probe when a required stock-code observation is absent."""
    if not condition:
        raise RuntimeError(message)


def put(buf, off, size, value):
    """Store value little-endian into buf at off."""
    buf[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def get(buf, off, size, signed=False):
    """Read a little-endian value from buf."""
    return int.from_bytes(buf[off:off + size], "little", signed=signed)


class Rig:
    """The emulator, a path record P, a unit record U, an out buffer O, and the hooks."""

    def __init__(self):
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
                "requires stock game.exe 1.14d (1.14.3.71)")
        self.m = emu.Emu(str(exe))
        self.P = self.m.alloc(PSIZE)
        self.U = self.m.alloc(USIZE)
        self.O = self.m.alloc(16)
        self.rng = random.Random(114)
        self.halts = []
        self.log = []                  # (name, args) of recorded callee calls
        self.rets = {}                 # callee name -> value it returns
        self.cases = 0
        self.diff = self.m.read(DIFF_DIRECTIONS, 256)
        self.flags = struct.unpack("<18I", self.m.read(PATH_TYPE_FLAGS, 72))
        self.points = struct.unpack("<18I", self.m.read(PATH_TYPE_POINTS, 72))
        self._hooks()

    def _hooks(self):
        """Record fatal errors and the callees that are not what this probe claims."""
        m = self.m

        def halt(e):
            """FUN_00408a60 (error halt): note the line and stop the emulation."""
            self.halts.append(e.arg(2))
            e.mu.emu_stop()
            return 0

        m.hook(GET_IP, lambda e: 0, 0)
        m.hook(ERR_HALT, halt, 0)

        def rec(name, n, regs=()):
            """Hook a callee: log its stack args and named registers, return self.rets[name]."""
            def fn(e):
                regvals = tuple(e.mu.reg_read(r) for r in regs)
                self.log.append((name, tuple(e.arg(i) for i in range(n)) + regvals))
                return self.rets.get(name, 0)
            return fn

        for name, addr, n, regs in (
                ("free", FREE_ENTRY, 3, (UC_X86_REG_ECX, UC_X86_REG_EDX)),
                ("rm_width", 0x64EBA0, 5, ()), ("rm_type", 0x64EC10, 5, ()),
                ("add_width", 0x64EA00, 5, ()), ("add_type", 0x64EA90, 5, ()),
                ("reached", 0x650540, 1, ()), ("recalc", 0x650350, 2, ()),
                ("update_flag", 0x624690, 2, ()),
                ("get_room", 0x620BB0, 1, ()), ("remove_unit", 0x649560, 2, ()),
                ("interact", 0x648580, 2, ()), ("add_unit", 0x649400, 1, ()),
                ("direct", 0x6492F0, 0, (UC_X86_REG_ESI,)),
                ("random_path", 0x67A240, 0, (UC_X86_REG_ECX, UC_X86_REG_EDX)),
                ("circle_path", 0x67A140, 0, (UC_X86_REG_ECX, UC_X86_REG_EDX))):
            m.hook(addr, rec(name, n, regs), n if name not in
                   ("direct", "random_path", "circle_path") else 0)

    def rand_path(self):
        """A random path record and unit record."""
        return (bytearray(self.rng.randbytes(PSIZE)), bytearray(self.rng.randbytes(USIZE)))

    def run(self, name, addr, args, model, setup=None, conv="std", mask=M32, regs=None):
        """Run stock code on one random state and compare to model(p, u, a) -> eax or HALT line."""
        for _ in range(1 if setup == "once" else ROUNDS):
            p, u = self.rand_path()
            if callable(setup):
                setup(p, u, self.rng)
            self.m.mu.mem_write(self.P, bytes(p))
            self.m.mu.mem_write(self.U, bytes(u))
            self.m.mu.mem_write(self.O, b"\0" * 16)
            self.halts.clear()
            self.log.clear()
            a = [{"P": self.P, "U": self.U, "O": self.O}.get(x, x) if isinstance(x, str) else x
                 for x in args]
            if conv == "std":
                eax = self.m.call(addr, *a, limit=LIMIT)
            elif conv == "fast":
                eax = self.m.call(addr, ecx=a[0], edx=a[1], limit=LIMIT)
            else:
                eax = self.m.call(addr, ecx=a[1], regs={"eax": a[0]}, limit=LIMIT)
            want = model(p, u, a)
            gotp, gotu = self.m.read(self.P, PSIZE), self.m.read(self.U, USIZE)
            where = f"{name} {addr:08x} args {[hex(x) for x in a]}"
            if isinstance(want, tuple) and want[0] == HALT:
                require(self.halts == [want[1]], f"{where}: halt {self.halts}, wanted line {want[1]:#x}")
            else:
                require(not self.halts, f"{where}: unexpected fatal error at line {self.halts}")
                if want is not None:
                    require(eax & mask == want & mask, f"{where}: eax {eax & mask:#x}, model {want & mask:#x}")
            require(gotp == bytes(p), f"{where}: path record differs at "
                    f"{[hex(i) for i in range(PSIZE) if gotp[i] != p[i]][:6]}")
            require(gotu == bytes(u), f"{where}: unit record differs at "
                    f"{[hex(i) for i in range(USIZE) if gotu[i] != u[i]][:6]}")
            self.cases += 1
        return self.cases

    def run_log(self, name, addr, args, model, setup, conv="std", mask=M32):
        """Like run, with the hooked callees' recorded calls compared too: model returns (eax, log)."""
        def wrapped(p, u, a):
            """Model result is (eax, expected_log); check the log here, where it's recorded."""
            eax, want = model(p, u, a)
            require(self.log == want, f"{name}: callee calls {self.log}, model {want}")
            return eax
        return self.run(name, addr, args, wrapped, setup, conv, mask)


def main():
    """Run every PATH accessor case against stock game.exe 1.14d."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    r = Rig()
    P, U = "P", "U"

    def ptr(p, u, rr, nonzero=True):
        """Link the path to its owner unit (and the unit back to the path)."""
        put(p, 0x30, 4, r.U)
        put(u, 0x2C, 4, r.P)

    # ---- plain getters: eax = field -------------------------------------
    getters = (
        ("GetVelocity", 0x6486C0, 0x7C, 4, M32), ("GetMaxVelocity", 0x6486F0, 0x84, 4, M32),
        ("GetAcceleration", 0x648720, 0x88, 4, M32), ("GetPathPointIndex", 0x6487C0, 0x24, 4, M32),
        ("UpdateDirection", 0x648810, 0x65, 1, 0xFF), ("GetPathType", 0x648E30, 0x3C, 4, M32),
        ("GetDistance", 0x648E60, 0x90, 1, 0xFF), ("GetMaxDistance", 0x648EA0, 0x91, 1, 0xFF),
        ("GetCollisionFlag", 0x648EF0, 0x54, 2, 0xFFFF), ("GetTargetX", 0x6489C0, 0x08, 4, M32),
        ("GetTargetY", 0x6489D0, 0x0C, 4, M32), ("GetXSP1", 0x648A00, 0x10, 2, M32),
        ("GetYSP1", 0x648A10, 0x12, 2, M32), ("GetXSP3", 0x648A20, 0x18, 2, M32),
        ("GetYSP3", 0x648A30, 0x1A, 2, M32), ("GetRoom", 0x648A80, 0x1C, 4, M32),
        ("GetNextRoom", 0x648AA0, 0x20, 4, M32), ("GetCollisionPattern", 0x648CD0, 0x50, 4, M32))
    for name, addr, off, size, mask in getters:
        r.run(name, addr, [P], lambda p, u, a, o=off, s=size: get(p, o, s), mask=mask)
    r.run("GetPrecisionX", 0x648940, [P], lambda p, u, a: get(p, 0, 4))
    r.run("GetPrecisionY", 0x648970, [P], lambda p, u, a: get(p, 4, 4))
    for name, addr, line in (("GetPrecisionX", 0x648940, 0x6A8), ("GetPrecisionY", 0x648970, 0x6B3)):
        r.run(name + " null", addr, [0], lambda p, u, a, ln=line: (HALT, ln), setup="once")
    r.run("GetStepNum", 0x6490A0, [P], lambda p, u, a: get(p, 0x93, 1) + 1)
    r.run("GetUnitWidth", 0x649180, [U], lambda p, u, a: get(p, 0x48, 4), ptr)
    for name, addr, bit in (("GetFlags0x20", 0x648770, 0x20), ("Get0x02Flag", 0x648B30, 2),
                            ("Get0x04Flag", 0x648B80, 4)):
        r.run(name, addr, [P], lambda p, u, a, b=bit: get(p, 0x34, 4) & b)
    r.run("GetTargetUnit", 0x648BF0, [P], lambda p, u, a: get(p, 0x58, 4))
    r.run("GetTargetUnit null", 0x648BF0, [0], lambda p, u, a: 0, setup="once")
    r.run("GetCollisionType", 0x648C10, [P], lambda p, u, a: get(p, 0x4C, 4))
    r.run("GetCollisionType null", 0x648C10, [0], lambda p, u, a: 0xFFFF, setup="once")

    def few_points(base, top):
        """Keep the point count inside the record, and make zero common."""
        def setup(p, u, rr):
            put(p, base, 4, rr.choice((0, 1, 2, top, rr.randrange(top + 1))))
        return setup

    def path_last(coord):
        """GetLastPathPointX/Y: count at +0x28, points from +0x9c, so the last is +0x98 + 4n."""
        def model(p, u, a):
            n = get(p, 0x28, 4)
            return get(p, 0x98 + 4 * n + 2 * coord, 2) if n else 0
        return model

    r.run("GetLastPathPointX", 0x648A40, [P], path_last(0), few_points(0x28, 77))
    r.run("GetLastPathPointY", 0x648A60, [P], path_last(1), few_points(0x28, 77))

    def init_flag(on):
        """Set or clear the 'has saved init coordinates' flag 0x20000 and keep the count small."""
        def setup(p, u, rr):
            put(p, 0x34, 4, (get(p, 0x34, 4) | 0x20000) if on else (get(p, 0x34, 4) & ~0x20000))
            put(p, 0x1D4, 4, rr.choice((0, 1, 2, 10, rr.randrange(11))))
        return setup

    def init_last(coord):
        """GetLastInitCoordX/Y: count at +0x1d4, entries from +0x1d8, so the last is +0x1d4 + 4n."""
        def model(p, u, a):
            if not get(p, 0x34, 4) & 0x20000:
                return (HALT, 0x902 if coord == 0 else 0x911)
            n = get(p, 0x1D4, 4)
            return get(p, 0x1D4 + 4 * n + 2 * coord, 2) if n else 0
        return model

    for on in (True, False):
        r.run(f"GetLastInitCoordX flag={on}", 0x648F90, [P], init_last(0), init_flag(on))
        r.run(f"GetLastInitCoordY flag={on}", 0x648FE0, [P], init_last(1), init_flag(on))

    def init_coords(p, u, a):
        """GetInitCoords: out = &path + 0x1d8, returns the count, or fatal line 0x8f4 without the flag."""
        if not get(p, 0x34, 4) & 0x20000:
            return (HALT, 0x8F4)
        return get(p, 0x1D4, 4)

    def init_coords_out(p, u, a):
        """Model wrapper that also expects the out pointer written (checked after the call)."""
        res = init_coords(p, u, a)
        want = r.P + 0x1D8
        if not (isinstance(res, tuple)):
            require(get(bytearray(r.m.read(r.O, 4)), 0, 4) == want, "GetInitCoords out pointer")
        return res

    for on in (True, False):
        r.run(f"GetInitCoords flag={on}", 0x648F40, [P, "O"], init_coords_out, init_flag(on))

    def point_set(p, u, a):
        """GetPathPoint: *out = &path + 0x9c, returns the count at +0x28."""
        require(get(bytearray(r.m.read(r.O, 4)), 0, 4) == r.P + 0x9C, "GetPathPoint out pointer")
        return get(p, 0x28, 4)

    r.run("GetPathPoint", 0x6487D0, [P, "O"], point_set)

    # ---- plain setters ---------------------------------------------------
    setters = (("SetMaxVelocity", 0x6486D0, 0x84, 4), ("SetAcceleration", 0x648700, 0x88, 4),
               ("SetPrecisionX", 0x6489A0, 0x00, 4), ("SetPrecisionY", 0x6489B0, 0x04, 4),
               ("SetCollisionPattern", 0x648CE0, 0x50, 4), ("SetRoom", 0x648A90, 0x1C, 4),
               ("SetDistance", 0x648E40, 0x90, 1))
    for name, addr, off, size in setters:
        for value in (0, 1, 0x7FFFFFFF, 0x80000000, M32, 0x12345678):
            r.run(f"{name} {value:#x}", addr, [P, value],
                  lambda p, u, a, o=off, s=size: put(p, o, s, a[1]) or 0, mask=0)
    for name, addr, off in (("SetTargetX", 0x6489E0, 8), ("SetTargetY", 0x6489F0, 0xC)):
        for value in (0, 5, M32, 0x80000000):
            r.run(f"{name} {value:#x}", addr, [P, value],
                  lambda p, u, a, o=off: put(p, o, 4, a[1]) or 0, conv="fast", mask=0)
    r.run("SetUnitWidth", 0x649190, [U, 0x1234], lambda p, u, a: put(p, 0x48, 4, a[1]) or 0, ptr, mask=0)
    r.run("SetMissileOffsets", 0x6490C0, [P, 0x11, 0x22],
          lambda p, u, a: (put(p, 0x67, 1, a[1]), put(p, 0x68, 1, a[2])) and 0, mask=0)
    r.run("SetMissileOffsets wide", 0x6490C0, [P, 0x1FF, 0x2AA],
          lambda p, u, a: (put(p, 0x67, 1, a[1]), put(p, 0x68, 1, a[2])) and 0, mask=0)
    r.run("ResetNextRoom", 0x648AB0, [P], lambda p, u, a: put(p, 0x20, 4, 0) or 0, mask=0)
    r.run("ResetSP2", 0x649050, [P],
          lambda p, u, a: (put(p, 0x14, 2, 0), put(p, 0x16, 2, 0)) and 0, mask=0)
    r.run("ZeroPathPointsSize", 0x648730, [P],
          lambda p, u, a: (put(p, 0x34, 4, get(p, 0x34, 4) & ~0x20), put(p, 0x28, 4, 0)) and 0, mask=0)

    def flag_setter(bit, invert=False):
        """Model of a flag toggle: sets bit when arg is non-zero (zero, if invert), else clears it."""
        def model(p, u, a):
            on = (a[1] == 0) if invert else (a[1] != 0)
            put(p, 0x34, 4, (get(p, 0x34, 4) | bit) if on else (get(p, 0x34, 4) & ~bit))
            return 0
        return model

    for name, addr, bit in (("SetUnsetFlags0x20", 0x648750, 0x20), ("SetUnset0x02Flag", 0x648B40, 2),
                            ("SetUnset0x04Flag", 0x648B60, 4)):
        for value in (0, 1, 2, M32):
            r.run(f"{name} {value:#x}", addr, [P, value], flag_setter(bit), mask=0)
    for value in (0, 1, 7, M32):                 # inverted: zero sets 0x40
        r.run(f"SetUnsetFlags0x40 {value:#x}", 0x649030, [P, value], flag_setter(0x40, True), mask=0)
    r.run("SetUnsetFlags0x40 null", 0x649030, [0, 0], lambda p, u, a: 0, setup="once", mask=0)

    for value in (-5, 0, 1, 76, 77, 78, 0x7FFFFFFF, -0x80000000):
        def clamp(p, u, a):
            """Count is clamped to 77 on the high side only (signed compare)."""
            put(p, 0x28, 4, 77 if (a[1] - (1 << 32) if a[1] >> 31 else a[1]) > 77 else a[1])
            return 0
        r.run(f"SetPathPointsSize {value}", 0x648790, [P, value & M32], clamp, mask=0)

    r.run("SetDirectionAndNewDirection", 0x6488A0, [P, 0xC5],
          lambda p, u, a: (put(p, 0x64, 1, a[1] & 0x3F), put(p, 0x65, 1, a[1] & 0x3F)) and 0, mask=0)
    r.run("SetDirectionAndNewDirection null", 0x6488A0, [0, 0xC5], lambda p, u, a: 0, setup="once", mask=0)

    def set_sp1(p, u, a):
        """SetSP1: target tile words, and the target unit cleared; a null path is a no-op."""
        put(p, 0x10, 2, a[1])
        put(p, 0x12, 2, a[2])
        put(p, 0x58, 4, 0)
        return 0
    r.run("SetSP1", 0x648AD0, [P, 0xFFF1, 0x1234], set_sp1, mask=0)
    r.run("SetSP1 null", 0x648AD0, [0, 1, 2], lambda p, u, a: 0, setup="once", mask=0)

    def sp1_shifted(p, u, a):
        """SetSP1Shifted: (x + 2y, 2y - x) >> 11 (arithmetic) into the two target words."""
        def s32(v):
            """Signed view of a 32-bit value."""
            return v - (1 << 32) if v >> 31 else v
        x, y = s32(a[1]), s32(a[2])
        nx, ny = (x + 2 * y) & M32, (2 * y - x) & M32
        put(p, 0x10, 2, s32(nx) >> 11)
        put(p, 0x12, 2, s32(ny) >> 11)
        return 0
    for x, y in ((0, 0), (1000, 2000), (M32 - 999, 5000), (0x7FFFFF, 0x7FFFF), (-300000 & M32, -70000 & M32)):
        r.run(f"SetSP1Shifted {x:#x},{y:#x}", 0x648B00, [P, x, y], sp1_shifted, mask=0)

    def set_target(p, u, a):
        """SetTarget: pointer stored; type and GUID copied from the unit only when it's non-null."""
        put(p, 0x58, 4, a[1])
        if a[1]:
            put(p, 0x5C, 4, get(u, 0, 4))
            put(p, 0x60, 4, get(u, 0xC, 4))
        return 0
    r.run("SetTarget unit", 0x648B90, [P, U], set_target, mask=0)
    r.run("SetTarget null", 0x648B90, [P, 0], set_target, mask=0)

    def target_guid(p, u, a):
        """GetTargetTypeAndGUID: needs a target unit (fatal 0x7c0 otherwise); writes type, GUID."""
        if not get(p, 0x58, 4):
            return (HALT, 0x7C0)
        require(r.m.read(r.O, 8) == bytes(p[0x5C:0x60]) + bytes(p[0x60:0x64]), "target type/GUID out values")
        return 0
    def with_target(on):
        """Give the path a target unit (or none)."""
        def setup(p, u, rr):
            put(p, 0x58, 4, r.U if on else 0)
        return setup
    for on in (True, False):
        r.run(f"GetTargetTypeAndGUID target={on}", 0x648BB0, [P, "O", r.O + 4], target_guid, with_target(on), mask=0)

    def ida_star(p, u, a):
        """SetIdaStarVariable: only path type 0 may set it; any other is fatal line 0x8e8."""
        if get(p, 0x3C, 4):
            return (HALT, 0x8E8)
        put(p, 0x92, 1, a[1])
        return 0
    def type_zero(on):
        """Make the path type zero (or not)."""
        def setup(p, u, rr):
            put(p, 0x3C, 4, 0 if on else rr.randrange(1, 18))
        return setup
    for on in (True, False):
        r.run(f"SetIdaStarVariable type0={on}", 0x648F00, [P, 0x5A], ida_star, type_zero(on), mask=0)

    # ---- direction -------------------------------------------------------
    def set_direction(p, u, a):
        """SetDirection (ECX path, DL dir): objects/items set the facing, missiles unless
        flag 0x40, everything else turns toward it with the diff table."""
        d = a[1] & 0x3F
        owner = get(p, 0x30, 4)
        kind = get(u, 0, 4) if owner else 6
        if kind in (2, 4) or (kind == 3 and not get(p, 0x34, 4) & 0x40):
            put(p, 0x64, 1, d)
        elif kind not in (2, 3, 4) and d != get(p, 0x65, 1):
            put(p, 0x65, 1, d)
            put(p, 0x66, 1, r.diff[((d - get(p, 0x64, 1)) & 0x3F) * 4])
        return 0

    def owner_kind(kind):
        """Point the path at an owner unit of this type (None: no owner)."""
        def setup(p, u, rr):
            put(p, 0x30, 4, 0 if kind is None else r.U)
            if kind is not None:
                put(u, 0, 4, kind)
            if rr.random() < 0.3:
                put(p, 0x65, 1, rr.randrange(256))
        return setup
    for kind in (None, 0, 1, 2, 3, 4, 5, 6):
        for value in (0, 0x3F, 0x40, 0xFF, 0x25):
            r.run(f"SetDirection owner={kind} dir={value:#x}", 0x6485F0, [P, value], set_direction,
                  owner_kind(kind), conv="fast", mask=0)

    def interpolate(p, u, a):
        """InterpolateDirection: step the facing by the diff; snap to the new one when the
        step would overshoot (distance left beyond half a turn on the wrong side)."""
        cur, new, diff = get(p, 0x64, 1), get(p, 0x65, 1), get(p, 0x66, 1)
        if cur == new or get(p, 0x34, 4) & 0x40:
            return 0
        cur = (diff + cur) & 0x3F
        put(p, 0x64, 1, cur)
        left = (cur - new) & 0x3F
        if (left > 0x1F and diff > 0x3F) or (left < 0x1F and diff < 0x3F):
            put(p, 0x64, 1, new)
        return 0

    def turn_state(p, u, rr):
        """Directions and diffs from the real range, with equal directions and 0x40 common."""
        put(p, 0x64, 1, rr.randrange(64))
        put(p, 0x65, 1, get(p, 0x64, 1) if rr.random() < 0.2 else rr.randrange(64))
        put(p, 0x66, 1, rr.choice((1, 0xFF, 0x3F, 0x40, 2, rr.randrange(256))))
        if rr.random() < 0.2:
            put(p, 0x34, 4, get(p, 0x34, 4) | 0x40)
    r.run("InterpolateDirection", 0x648640, [P], interpolate, turn_state, mask=0)

    # ---- monster collision pattern (EAX = unit, class id at +4) ---------
    count, rec = 6, 0x1A8
    table = r.m.alloc(count * rec)
    datatable = r.m.alloc(0x1000)
    r.m.w32(datatable + 0xA78, table)
    r.m.w32(datatable + 0xA80, count)
    r.m.w32(MON_TABLE, datatable)
    for i in range(32):
        r.m.w32(MON_MASKS + 4 * i, 1 << i)       # gadwBitMasks, filled at runtime by the Fog bit code
    for idx in range(count):
        for b0, b1 in ((0, 0), (8, 0), (0, 0x40), (8, 0x40), (0xFF, 0xFF), (0xF7, 0xBF)):
            rr = r.rng.randbytes(rec)
            r.m.mu.mem_write(table + idx * rec, rr[:0xC] + bytes((b0, b1)) + rr[0xE:])
            want = 0x1804 if (b1 & 0x40) else (0x3401 if (b0 & 8) else 0x3C01)
            put_u = bytearray(r.rng.randbytes(USIZE))
            put(put_u, 4, 4, idx)
            r.m.mu.mem_write(r.U, bytes(put_u))
            eax = r.m.call(0x648480, regs={"eax": r.U}, limit=LIMIT)
            require(eax == want, f"CollisionPatternFromMonStats class {idx} bytes {b0:#x},{b1:#x}: "
                    f"{eax:#x}, model {want:#x}")
            require(r.m.read(r.U, USIZE) == bytes(put_u), "monster pattern read changed the unit")
            r.cases += 1
    # An out-of-range class has no guard of its own: the null record is read at +0xd and faults.
    for bad in (M32, count):
        u = bytearray(r.rng.randbytes(USIZE))
        put(u, 4, 4, bad)
        r.m.mu.mem_write(r.U, bytes(u))
        try:
            r.m.call(0x648480, regs={"eax": r.U}, limit=LIMIT)
        except RuntimeError:
            r.cases += 1
        else:
            raise RuntimeError(f"class {bad:#x} did not fault")
    # A null unit faults the same way.
    try:
        r.m.call(0x648480, regs={"eax": 0}, limit=LIMIT)
    except RuntimeError:
        r.cases += 1
    else:
        raise RuntimeError("null unit did not fault")

    # ---- path type -------------------------------------------------------
    def set_path_type(p, u, a):
        """SetPathType: players may not take type 2 (fatal 0x861); the type's flag bits come
        from PATHTYPEFLAGS, 0x2000 saves the old type, 0x8000 the velocity; then fatal if the
        saved type is 0xb (0x871) or 8 (0x872), or a type-4 path has distance cap >= 0x4e (0x873)."""
        t = a[1]
        owner = get(p, 0x30, 4)
        if owner and get(u, 0, 4) == 0 and t == 2:
            return (HALT, 0x861)
        fl = r.flags[t]
        if fl & 0x2000 and not get(p, 0x34, 4) & 0x4000:
            put(p, 0x40, 4, get(p, 0x3C, 4))
        if fl & 0x8000 and not get(p, 0x34, 4) & 0x10000:
            put(p, 0x80, 4, get(p, 0x7C, 4))
        put(p, 0x34, 4, (get(p, 0x34, 4) & 0xFFF800FF) | fl)
        put(p, 0x3C, 4, t)
        put(p, 0x98, 4, r.points[t])
        prev = get(p, 0x40, 4)
        if prev == 0xB:
            return (HALT, 0x871)
        if prev == 8:
            return (HALT, 0x872)
        if t == 4 and get(p, 0x91, 1) >= 0x4E:
            return (HALT, 0x873)
        return 0

    def type_state(p, u, rr):
        """Owner kind, previous type and distance cap that reach every branch."""
        owner_kind(rr.choice((None, 0, 1, 3)))(p, u, rr)
        put(p, 0x40, 4, rr.choice((0, 1, 8, 0xB, 3, rr.randrange(18))))
        put(p, 0x91, 1, rr.choice((0, 0x4D, 0x4E, 0xFF, rr.randrange(256))))
    for t in range(18):
        r.run(f"SetPathType {t}", 0x648CF0, [P, t], set_path_type, type_state, mask=0)

    def restore(p, u, a):
        """RestoreVelocityAndPathType: restores the velocity when 0x8000 is set; a player
        goes to type 7, anything else back to the saved type if 0x2000 is set; no owner is
        fatal 0x87e."""
        if not get(p, 0x30, 4):
            return (HALT, 0x87E)
        if get(p, 0x34, 4) & 0x8000:
            put(p, 0x7C, 4, get(p, 0x80, 4))
        if get(u, 0, 4) == 0:
            return set_path_type(p, u, [None, 7])
        if get(p, 0x34, 4) & 0x2000:
            return set_path_type(p, u, [None, get(p, 0x40, 4)])
        return 0

    def restore_state(p, u, rr):
        """Owners of each kind (and none), saved types small enough to index the tables."""
        owner_kind(rr.choice((None, 0, 1, 1)))(p, u, rr)
        put(p, 0x40, 4, rr.choice((0, 1, 3, 4, 5, 8, 0xB, rr.randrange(18))))
        put(p, 0x91, 1, rr.choice((0, 0x4D, 0x4E, rr.randrange(256))))
    r.run("RestoreVelocityAndPathType", 0x648DC0, [P], restore, restore_state, mask=0)

    # ---- allocator and collision wrappers -------------------------------
    r.run_log("FreePath", 0x648460, [0x1111, 0x2222],
              lambda p, u, a: (0, [("free", (PATH_FILE, 0x3D9, 0, 0x1111, 0x2222))]),
              setup="once", mask=0)

    def change_collision(p, u, a):
        """ChangeCollisionType: with a room, remove the old flag then add the new one at the
        subtile (words +2, +6); a missile owner uses the size at +0x44, others the width at
        +0x48; the new flag is stored in between. Without a room it only stores the flag."""
        old = get(p, 0x4C, 4)
        room, x, y = get(p, 0x1C, 4), get(p, 2, 2), get(p, 6, 2)
        missile = bool(get(p, 0x30, 4)) and get(u, 0, 4) == 3
        put(p, 0x4C, 4, a[1])
        if not room:
            return 0, []
        size = get(p, 0x44 if missile else 0x48, 4)
        return 0, [("rm_width" if missile else "rm_type", (room, x, y, size, old)),
                   ("add_width" if missile else "add_type", (room, x, y, size, a[1]))]

    def collision_state(p, u, rr):
        """Rooms present or not, owners of kind missile / other / none."""
        owner_kind(rr.choice((None, 3, 1, 3, 0)))(p, u, rr)
        put(p, 0x1C, 4, rr.choice((0, 0x70000000 + rr.randrange(0x1000))))
    r.run_log("ChangeCollisionType", 0x648C30, [P, 0x5A5A], change_collision, collision_state, mask=0)

    # ---- recalc, flag and width on the unit -------------------------------
    def flag2000(p, u, a):
        """SetOrClearFlag0x2000 takes a UNIT: sets or clears bit 13 of its +0xc8 flags and
        zeroes the recalc counter (+0x38) of the unit's path; no path is fatal 0x9a2."""
        if not get(u, 0x2C, 4):
            return (HALT, 0x9A2)
        put(u, 0xC8, 4, (get(u, 0xC8, 4) | 0x2000) if a[1] else (get(u, 0xC8, 4) & ~0x2000))
        put(p, 0x38, 4, 0)
        return 0

    def unit_path(on):
        """Give the unit a path (or none)."""
        def setup(p, u, rr):
            put(u, 0x2C, 4, r.P if on else 0)
        return setup
    for on in (True, False):
        for flag in (0, 1, 5):
            r.run(f"SetOrClearFlag0x2000 path={on} set={flag}", 0x6491B0, [U, flag], flag2000,
                  unit_path(on), mask=0)

    def periodic(p, u, a):
        """PeriodicPathRecalc (unit): only for a unit with bit 13 of +0xc8 set and bit 21 of
        +0xc4 clear; bumps the path's counter (+0x38); every second call resets it and, if
        the target is not reached, recalculates (target given: flag 1) and on success sets
        update flag 2."""
        log = []
        if not a[0]:
            return 0, log
        if get(u, 0xC4, 4) >> 21 & 1 or not get(u, 0xC8, 4) >> 13 & 1:
            return 0, log
        if not get(u, 0x2C, 4):
            return (HALT, 0x9B5), log
        put(p, 0x38, 4, (get(p, 0x38, 4) + 1) & M32)
        if (get(p, 0x38, 4) - (1 << 32) if get(p, 0x38, 4) >> 31 else get(p, 0x38, 4)) <= 1:
            return 0, log
        put(p, 0x38, 4, 0)
        log.append(("reached", (r.U,)))
        if not r.rets.get("reached", 0):
            log.append(("recalc", (r.U, 1 if get(p, 0x58, 4) else 0)))
            if r.rets.get("recalc", 0):
                log.append(("update_flag", (r.U, 2)))
        return 0, log

    def periodic_state(p, u, rr):
        """Flags either side of the gates, a counter at 0 / 1 / negative / large, a target or none."""
        put(u, 0xC4, 4, rr.choice((0, 1 << 21, rr.randrange(1 << 32))))
        put(u, 0xC8, 4, rr.choice((0, 0x2000, rr.randrange(1 << 32))))
        put(u, 0x2C, 4, rr.choice((r.P, r.P, r.P, 0)))
        put(p, 0x38, 4, rr.choice((0, 1, M32, 0x7FFFFFFF, rr.randrange(1 << 32))))
        put(p, 0x58, 4, rr.choice((0, r.U)))
    for reached in (0, 1):
        for recalc in (0, 1, 5):
            r.rets.update(reached=reached, recalc=recalc)
            r.run_log(f"PeriodicPathRecalc reached={reached} recalc={recalc}", 0x649210, [U],
                      periodic, periodic_state, mask=0)
    r.rets.clear()
    r.run_log("PeriodicPathRecalc null", 0x649210, [0], lambda p, u, a: (0, []), setup="once", mask=0)

    def update_width(p, u, a):
        """UpdateCollisionWidth (unit): with a path, ask for the interact size of the size at
        +0x44, store it at +0x48; if the unit has a room, drop its collision first and add it
        back after. No path is a no-op."""
        log = []
        if not get(u, 0x2C, 4):
            return 0, log
        room = r.rets.get("get_room", 0)
        log.append(("get_room", (r.U,)))
        if room:
            log.append(("remove_unit", (r.U, 1)))
        log.append(("interact", (r.U, get(p, 0x44, 4))))
        put(p, 0x48, 4, r.rets["interact"])
        if room:
            log.append(("add_unit", (r.U,)))
        return 0, log
    for room in (0, 0x70001000):
        r.rets.update(get_room=room, interact=0x4D2)
        for on in (True, False):
            r.run_log(f"UpdateCollisionWidth room={room:#x} path={on}", 0x649CA0, [U], update_width,
                      unit_path(on), mask=0)
    r.rets.clear()

    # ---- missile path builders ------------------------------------------
    def missile_path(p, u, a):
        """CalculateMissilePath (EAX path, ECX unit): type 4 builds a direct path (nothing
        built clears flag 0x20 and returns 0), 10 a randomized one, 0xe a circular one, and
        the rest is fatal 0x207; a built path sets flag 0x20 and returns the point count."""
        t = get(p, 0x3C, 4)
        log = []
        if t == 4:
            log.append(("direct", (r.P,)))
            if not r.rets.get("direct", 0):
                put(p, 0x34, 4, get(p, 0x34, 4) & ~0x20)
                return 0, log
        elif t == 10:
            log.append(("random_path", (r.P, r.U + 0x20)))
        elif t == 14:
            log.append(("circle_path", (r.P, r.U + 0x20)))
        else:
            return (HALT, 0x207), log
        put(p, 0x34, 4, get(p, 0x34, 4) | 0x20)
        return get(p, 0x28, 4), log

    def missile_state(p, u, rr):
        """Path types around the three handled ones."""
        put(p, 0x3C, 4, rr.choice((4, 10, 14, 0, 3, 5, 11, 15)))
        put(p, 0x28, 4, rr.randrange(78))
    for built in (0, 1):
        r.rets["direct"] = built
        r.run_log(f"CalculateMissilePath direct={built}", 0x649760, [P, U], missile_path,
                  missile_state, conv="reg")
    r.rets.clear()

    def remaining(p, u, a):
        """HasRemainingDistance: the server path's index is below its point count (signed)."""
        if not a[0]:
            return 0
        sp = get(p, 0x2C, 4)
        if not sp:
            return 0
        s = lambda v: v - (1 << 32) if v >> 31 else v
        return 1 if s(get(p, 0x24, 4)) < s(get(p, 0x28, 4)) else 0

    def remaining_state(p, u, rr):
        """A server path or none, indices around the point count."""
        put(p, 0x2C, 4, rr.choice((r.P, r.P, 0)))
        put(p, 0x24, 4, rr.choice((0, 5, M32, rr.randrange(80))))
        put(p, 0x28, 4, rr.choice((0, 5, 6, rr.randrange(80))))
    r.run("HasRemainingDistance", 0x6492C0, [P], remaining, remaining_state)
    r.run("HasRemainingDistance null", 0x6492C0, [0], lambda p, u, a: 0, setup="once")

    print(f"ok: {r.cases} PATH accessor, setter, collision and recalc cases")


if __name__ == "__main__":
    main()
