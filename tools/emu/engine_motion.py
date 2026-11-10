# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the unit motion record (ENGINE\\Particle.cpp and the run after it) in stock game.exe 1.14d.

    uv run python engine_motion.py [--mutation-check]

Runs game.exe's own code for FUN_004da000 ... FUN_004da6e0 and the record
pointer pair FUN_0046f060 / FUN_0046f070 against a hand-written model on
random unit / graphics-record / motion-record bytes (see _dungeon_rig.py):
return value, ordered callee log, the whole arena, the fatal-error line and
the bytes `ret n` pops.

Claimed: the allocate / free / reuse behaviour of the record (0x4c bytes,
unit +0x54 -> +0x30), every getter and setter's field, shift and null
behaviour, the flag setters, the throw set-up (FUN_004da5b0) and the per-tick
step (FUN_004da350) with all four of its branches.
Hooked, not claimed: the pool allocator and free (0x40_b380, 0x40_b3c0), the
owner lookup FUN_004639d0, the frame size FUN_004706e0 and the two screen
conversions FUN_00643510 / FUN_00643290; each returns a hash of its arguments
and (the last three) writes hash-derived values through its output pointers.
`--mutation-check` breaks the model seven ways and requires each to fail.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, mix, s32

MUT = set()
MOTION_FILE = 0x6DB95C          # the source-file string the allocator calls pass
COVER = {}                      # step branch -> times the model took it (all must be non-zero)
MAGIC = -0x51EB851F - 1         # 0xae147ae1 as a signed dword (the compiler's divide by -100)


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def hit(branch):
    """Count a FUN_004da350 branch the model took."""
    COVER[branch] = COVER.get(branch, 0) + 1


def sar11(v):
    """Arithmetic shift right by 11 of a 32-bit value, as a 32-bit value."""
    return (s32(v) >> 11) & M32


def shl11(v):
    """Shift left by 11, 32 bits."""
    return (v << 11) & M32


# Arena slots: unit A, its graphics record, its motion record; then the same for the owner B.
UA, GA, MA, UB, GB, MB = range(6)


def put(img, slot, off, value):
    """Write a dword at `off` of arena slot `slot`."""
    i = slot * 0x100 + off
    img[i:i + 4] = (value & M32).to_bytes(4, "little")


def chain(rig, img, rng, unit, gfx, motion, p_broken=0.3):
    """Link unit -> +0x54 graphics record -> +0x30 motion record; sometimes break one link.

    Returns (unit address or 0, motion address or 0) as the stock accessors see them.
    """
    u, g, mo = rig.slot(unit), rig.slot(gfx), rig.slot(motion)
    roll = rng.random()
    put(img, unit, 0x54, 0 if roll < p_broken / 2 else g)
    put(img, gfx, 0x30, 0 if p_broken / 2 <= roll < p_broken else mo)
    return u, (mo if roll >= p_broken else 0)


def rec_of(m, unit):
    """The motion record of `unit` the way FUN_0046f060 reads it (0 for no unit, no record or no motion)."""
    if unit == 0:
        return 0
    g = m.r(unit + 0x54)
    return m.r(g + 0x30) if g else 0


def pick_unit(rng, unit, p_null=0.15):
    """`unit` or a null unit."""
    return 0 if rng.random() < p_null else unit


def special(rng):
    """A dword that is often a boundary value."""
    return rng.choice((0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x800, 0xFFFFF000, 0x1FFFFF, rng.randrange(1 << 32), rng.randrange(1 << 32)))


class Suite:
    """Builds and runs every case; the rig counts them."""

    def __init__(self, rig):
        """Keep the rig and hook the callees this oracle does not claim."""
        self.rig = rig
        self.fail_alloc = False
        rig.owner = 0
        rig.callee("pool_free", 0x40_B3C0, 2, ("ecx", "edx"))
        rig._callee("pool_alloc", 0x40_B380, 2, ("ecx", "edx"), self._alloc)
        rig.callee("owner", 0x46_39D0, 0, ("ecx",))
        rig.custom["owner"] = self._owner
        self._hook_outputs()

    def _alloc(self, e):
        """Allocator stand-in: a zeroed block of ECX bytes, or null while fail_alloc is set."""
        from unicorn.x86_const import UC_X86_REG_ECX
        return 0 if self.fail_alloc else e.alloc(max(e.mu.reg_read(UC_X86_REG_ECX), 1))

    def _owner(self, e):
        """FUN_004639d0 stand-in: the owner unit B (or null) the case chose, for any unit."""
        return self.rig.owner

    def _hook_outputs(self):
        """FUN_004706e0, FUN_00643510 and FUN_00643290: log their inputs, write hash-derived outputs."""
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX
        rig = self.rig
        m = rig.m

        def frame(e):
            """FUN_004706e0(ECX unit, EDX mode, &width, &height): width/height from the hash."""
            ecx, edx = e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EDX)
            r = mix("frame", ecx, edx)
            e.w32(e.arg(0), r & 0x3FF)
            e.w32(e.arg(1), (r >> 10) & 0x3FF)
            rig.log.append(("frame", ecx, edx))
            rig.outs.append(r)
            return r

        def screen(name):
            """A converter of two in-place coordinates (&x, &y): both replaced by hash-derived values."""
            def run(e):
                """Log the pointed-to values, write two new ones."""
                a, b = e.arg(0), e.arg(1)
                x, y = e.r32(a), e.r32(b)
                r = mix(name, x, y)
                e.w32(a, r)
                e.w32(b, r ^ 0xA5A5A5A5)
                rig.log.append((name, x, y))
                rig.outs.append(r)
                return r
            return run
        m.hook(0x4706E0, frame, 2)
        m.hook(0x643510, screen("world_to_screen"), 2)
        m.hook(0x643290, screen("cell_to_screen"), 2)

    def run(self, name, addr, build, model, nstack, mask=M32, ret=True):
        """Run one function through the rig."""
        self.rig.owner = 0
        self.rig.run(name, addr, build, model, nstack, mask, ret)

    # ---- the record pointer pair ---------------------------------------------
    def pointer_pair(self):
        """FUN_0046f060 reads unit +0x54 -> +0x30; FUN_0046f070 writes it (and nothing without a graphics record)."""
        rig = self.rig

        def build_get(img, rng):
            """A unit with a possibly broken chain."""
            u, mo = chain(rig, img, rng, UA, GA, MA)
            return {"ecx": pick_unit(rng, u)}

        def get(m, a):
            """The motion pointer, or 0."""
            return rec_of(m, a["ecx"])
        self.run("FUN_0046f060", 0x46F060, build_get, get, 0)

        def build_set(img, rng):
            """A unit, maybe with no graphics record, and a new pointer."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            return {"ecx": pick_unit(rng, u), "edx": special(rng)}

        def set_(m, a):
            """Store EDX at graphics +0x30 when the unit has a graphics record."""
            u = a["ecx"]
            if u and m.r(u + 0x54):
                m.w(m.r(u + 0x54) + 0x30, a["edx"])
            return None
        self.run("FUN_0046f070", 0x46F070, build_set, set_, 0, ret=False)

    # ---- allocate / free -----------------------------------------------------
    def lifecycle(self):
        """FUN_004da000 allocates (or re-zeroes) the 0x4c-byte record; FUN_004da080 frees it."""
        rig = self.rig

        def build_new(img, rng):
            """Unit with or without a record; allocation fails sometimes."""
            u, _ = chain(rig, img, rng, UA, GA, MA, 0.5)
            self.fail_alloc = rng.random() < 0.2
            return {"stack": [pick_unit(rng, u, 0.1)]}

        def new_model(m, a):
            """Reuse the existing record, else allocate 0x4c (fatal 0x4a on null); zero it; store the pointer."""
            unit = a["stack"][0]
            cur = rec_of(m, unit)
            if cur == 0:
                block = m.call("pool_alloc", 0x4C, MOTION_FILE, 0x48, 0)
                if block == 0:
                    raise Halt(0x4A)
                buf = bytearray(0x4C)
                m.blocks.append((block, buf))
                m.regions.append((block, buf))
                cur = block
            if not mut("keep-old") or cur == block_or_zero(m, cur):
                for i in range(0x4C):
                    m.w(cur + i, 0, 1)
            if unit and m.r(unit + 0x54):
                m.w(m.r(unit + 0x54) + 0x30, cur)
            return None
        self.run("FUN_004da000", 0x4DA000, build_new, new_model, 1, ret=False)
        self.fail_alloc = False

        def build_free(img, rng):
            """Unit with or without a record."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            return {"stack": [pick_unit(rng, u)]}

        def free(m, a):
            """Free the record when there is one, then clear the pointer (a no-op without a graphics record)."""
            unit = a["stack"][0]
            cur = rec_of(m, unit)
            if cur:
                m.call("pool_free", cur, MOTION_FILE, 0x64, 0)
                m.w(m.r(unit + 0x54) + 0x30, 0)
            return None
        self.run("FUN_004da080", 0x4DA080, build_free, free, 1, ret=False)

    # ---- getters -------------------------------------------------------------
    def getters(self):
        """The six position getters (whole-unit and raw), the three draw-offset getters, the target getter."""
        rig = self.rig
        # name, address, field offset, shift (None: raw)
        rows = (
            ("FUN_004da0b0", 0x4DA0B0, 0x34, False), ("FUN_004da0d0", 0x4DA0D0, 0x38, False),
            ("FUN_004da0f0", 0x4DA0F0, 0x3C, False), ("FUN_004da110", 0x4DA110, 4, True),
            ("FUN_004da130", 0x4DA130, 8, True), ("FUN_004da150", 0x4DA150, 0xC, True),
            ("FUN_004da170", 0x4DA170, 4, False), ("FUN_004da190", 0x4DA190, 8, False),
            ("FUN_004da1b0", 0x4DA1B0, 0xC, False), ("FUN_004da320", 0x4DA320, 0x40, False),
        )
        for name, addr, off, shifted in rows:
            def build(img, rng):
                """A unit with a possibly broken chain and random record bytes."""
                u, _ = chain(rig, img, rng, UA, GA, MA)
                return {"stack": [pick_unit(rng, u)]}

            def model(m, a, off=off, shifted=shifted):
                """The field (arithmetic >> 11 for the whole-unit getters), 0 without a record."""
                cur = rec_of(m, a["stack"][0])
                if not cur:
                    return 0
                v = m.r(cur + off)
                if shifted:
                    return (v >> 11) if mut("logical-shift") else sar11(v)
                return v
            self.run(name, addr, build, model, 1)

    # ---- setters -------------------------------------------------------------
    def setters(self):
        """Position (<< 11), velocity / acceleration / third vector (<< 11 with arg 5), target."""
        rig = self.rig

        def build3(img, rng):
            """Unit plus x, y, z."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            return {"stack": [pick_unit(rng, u), special(rng), special(rng), special(rng)]}

        def pos(m, a):
            """x, y, z each << 11 into +4, +8, +0xc."""
            cur = rec_of(m, a["stack"][0])
            if cur:
                for i, off in enumerate((4, 8, 0xC)):
                    m.w(cur + off, shl11(a["stack"][1 + i]))
            return None
        self.run("FUN_004da1d0", 0x4DA1D0, build3, pos, 4, ret=False)

        def build5(img, rng):
            """Unit plus x, y, z and the shift flag."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            return {"stack": [pick_unit(rng, u), special(rng), special(rng), special(rng), rng.choice((0, 0, 1, 2, 0x100))]}
        for name, addr, base in (("FUN_004da200", 0x4DA200, 0x10), ("FUN_004da250", 0x4DA250, 0x1C), ("FUN_004da2a0", 0x4DA2A0, 0x28)):
            def vec(m, a, base=base):
                """Three dwords at base, base+4, base+8; << 11 when arg 5 is non-zero."""
                cur = rec_of(m, a["stack"][0])
                if cur:
                    for i in range(3):
                        v = a["stack"][1 + i]
                        m.w(cur + base + 4 * i, shl11(v) if a["stack"][4] != 0 else v)
                return None
            self.run(name, addr, build5, vec, 5, ret=False)

        def build_target(img, rng):
            """Unit plus two dwords."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            return {"stack": [pick_unit(rng, u), special(rng), special(rng)]}

        def target(m, a):
            """Flag 4, then +0x40 and +0x44."""
            cur = rec_of(m, a["stack"][0])
            if cur:
                m.w(cur, m.r(cur) | 4)
                m.w(cur + 0x40, a["stack"][1])
                m.w(cur + 0x44, a["stack"][2])
            return None
        self.run("FUN_004da2f0", 0x4DA2F0, build_target, target, 3, ret=False)

    # ---- flags ---------------------------------------------------------------
    def flags(self):
        """FUN_004da340, 004da620/6e0, 004da640, 004da660, 004da690, 004da6b0."""
        rig = self.rig

        def build_ecx(img, rng):
            """Unit in ECX."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            return {"ecx": pick_unit(rng, u), "edx": rng.choice((0, 1, 2, 0x80000000))}

        def clear2(m, a):
            """Clear flag 2."""
            cur = rec_of(m, a["ecx"])
            if cur:
                m.w(cur, m.r(cur) & ~(0 if mut("clear-nothing") else 2))
            return None
        self.run("FUN_004da340", 0x4DA340, build_ecx, clear2, 0, ret=False)
        for name, addr, bit in (("FUN_004da620", 0x4DA620, 0x10), ("FUN_004da6e0", 0x4DA6E0, 0x20)):
            def setbit(m, a, bit=bit):
                """Set the bit when EDX is non-zero, else clear it."""
                cur = rec_of(m, a["ecx"])
                if cur:
                    m.w(cur, (m.r(cur) | bit) if a["edx"] else (m.r(cur) & ~bit))
                return None
            self.run(name, addr, build_ecx, setbit, 0, ret=False)

        def build_stack(img, rng):
            """Unit on the stack."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            return {"stack": [pick_unit(rng, u)]}

        def start(m, a):
            """Set flag 1."""
            cur = rec_of(m, a["stack"][0])
            if cur:
                m.w(cur, m.r(cur) | 1)
            return None
        self.run("FUN_004da640", 0x4DA640, build_stack, start, 1, ret=False)

        def started(m, a):
            """1 when the record has flag 1, else 0."""
            cur = rec_of(m, a["stack"][0])
            return 1 if cur and m.r(cur) & 1 else 0
        self.run("FUN_004da660", 0x4DA660, build_stack, started, 1)

        def done(m, a):
            """Clear 1, set 8."""
            cur = rec_of(m, a["stack"][0])
            if cur:
                m.w(cur, (m.r(cur) & ~1) | 8)
            return None
        self.run("FUN_004da690", 0x4DA690, build_stack, done, 1, ret=False)

        def not_running(m, a):
            """0 only for a started record that is not done; 1 for no record, not started, or done."""
            cur = rec_of(m, a["stack"][0])
            if not cur:
                return 1
            f = m.r(cur)
            return 0 if (f & 1) and not (f & 8) else 1
        self.run("FUN_004da6b0", 0x4DA6B0, build_stack, not_running, 1)

    # ---- the throw -----------------------------------------------------------
    def throw(self):
        """FUN_004da5b0: flag 2, ticks floor 1, z = h << 11, velocity z from the arc."""
        rig = self.rig

        def build(img, rng):
            """Unit, height (EDX) and ticks."""
            u, _ = chain(rig, img, rng, UA, GA, MA)
            if rng.random() < 0.3:
                put(img, MA, 0x24, 0)
            return {"stack": [rng.choice((1, 2, 5, 17, 0, 0xFFFFFFFF, 0x80000000, rng.randrange(1, 60)))],
                    "ecx": pick_unit(rng, u), "edx": rng.choice((0, 1, 20, 0xFFFFFFF0, 100, rng.randrange(-300, 300) & M32))}

        def model(m, a):
            """Mirror the instruction order, with trunc-toward-zero halving and idiv."""
            cur = rec_of(m, a["ecx"])
            if not cur:
                return None
            t = a["stack"][0]
            m.w(cur, m.r(cur) | 2)
            ticks = t if s32(t) >= 1 else 1
            if mut("no-tick-floor"):
                ticks = t
            m.w(cur + 0x48, ticks)
            h = shl11(a["edx"])
            for off in (4, 8):
                m.w(cur + off, 0)
            m.w(cur + 0xC, h)
            if m.r(cur + 0x24) == 0:
                m.w(cur + 0x24, 0xFFFFF000)
            az = m.r(cur + 0x24)
            m.w(cur + 0x10, 0)
            m.w(cur + 0x14, 0)
            n = (az * ticks * ticks) & M32
            n = s32(n)
            half = (n + (1 if n < 0 else 0)) >> 1
            if mut("floor-half"):
                half = n >> 1
            num = s32((-s32(h) - half) & M32)
            quotient = abs(num) // abs(s32(ticks)) * (1 if (num < 0) == (s32(ticks) < 0) else -1)
            m.w(cur + 0x18, quotient & M32)
            return None
        self.run("FUN_004da5b0", 0x4DA5B0, build, model, 1, ret=False)

    # ---- the step ------------------------------------------------------------
    def step(self):
        """FUN_004da350: integrate, the throw counter, bounce (flag 4), unit-frame (0x10) and arrival (0x20 / default)."""
        rig = self.rig

        def build(img, rng):
            """Unit A with a record and an owner B with its own record; flags and vectors biased to hit each branch."""
            ua, _ = chain(rig, img, rng, UA, GA, MA, 0.08)
            ub, _ = chain(rig, img, rng, UB, GB, MB, 0.2)
            rig.owner = rng.choice((ub, ub, ub, 0))
            type_a = rng.choice((0, 1, 2, 3, 3, 3, 4, 6))
            type_b = rng.choice((0, 1, 1, 1, 2, 3))
            put(img, UA, 0, type_a)
            put(img, UB, 0, type_b)
            # a missile (type 3) with a monster owner: both skip modes (0 and 0xc) need to come up
            put(img, UB, 0x10, rng.choice((0, 0xC, 0xC, 1, 2, 7)) if (type_a, type_b) == (3, 1) else rng.choice((0, 0xC, 1, 2, 7)))
            small = lambda: rng.randrange(-40, 40) & M32          # noqa: E731
            put(img, MA, 0, rng.choice((0, 1, 2, 4, 6, 0x10, 0x20, 0x24, 0x12, 0x30, 8, 5, 0x14, 0x40, rng.randrange(1 << 8))))
            for off in (4, 8, 0xC):
                put(img, MA, off, rng.choice((0, rng.randrange(-60, 60) << 11 & M32, rng.randrange(1 << 32))))
            for off in (0x10, 0x14, 0x18, 0x1C, 0x20, 0x24):
                put(img, MA, off, small() if rng.random() < 0.8 else rng.randrange(1 << 32))
            for off in (0x28, 0x2C, 0x30):
                put(img, MA, off, rng.randrange(-30, 30) & M32)
            if rng.random() < 0.5:            # a position on or next to the third vector, so equality is tested
                for off, third in ((4, 0x28), (8, 0x2C), (0xC, 0x30)):
                    at = int.from_bytes(img[MA * 0x100 + third:MA * 0x100 + third + 4], "little")
                    put(img, MA, off, shl11(at + rng.choice((0, 0, 0, 1, -1))) | rng.choice((0, 0, 0x7FF)))
            put(img, MA, 0x40, rng.choice((0, 1, 2, 3)))
            put(img, MA, 0x44, rng.choice((0, 50, 60, 100, 0xFFFFFFCE, rng.randrange(1 << 32))))
            put(img, MA, 0x48, rng.choice((0, 0, 1, 2, 9)))
            for off in (0x34, 0x38, 0x3C):
                put(img, MA, off, rng.choice((0, rng.randrange(-100, 100) & M32)))
            for off in (0x34, 0x38, 0x3C):
                put(img, MB, off, rng.choice((0, rng.randrange(-100, 100) & M32)))
            return {"stack": [pick_unit(rng, ua, 0.05)]}

        def model(m, a):
            """The per-tick update, instruction for instruction."""
            unit = a["stack"][0]
            p = rec_of(m, unit)
            if not p:
                return None
            flags = m.r(p)
            if flags & 1:
                hit("early-return")
                return None
            vx, vy, vz = m.r(p + 0x10), m.r(p + 0x14), m.r(p + 0x18)
            m.w(p + 4, m.r(p + 4) + vx)
            m.w(p + 0x10, vx + m.r(p + 0x1C))
            m.w(p + 8, m.r(p + 8) + vy)
            m.w(p + 0x14, vy + m.r(p + 0x20))
            m.w(p + 0xC, m.r(p + 0xC) + vz)
            vz2 = (vz + m.r(p + 0x24)) & M32
            m.w(p + 0x18, vz2)
            if flags & 2:
                if m.r(p + 0x48):
                    hit("throw-count")
                    m.w(p + 0x48, m.r(p + 0x48) - 1)
                else:
                    hit("throw-end")
                    flags |= 1
                    m.w(p, flags)
                    m.w(p + 4, 0)
                    m.w(p + 8, 0)
            flags = m.r(p)
            skip_tail = False
            if flags & 4:
                hit("bounce-test")
                if flags & 0x10:
                    hit("bounce-and-follow")
                if s32(m.r(p + 0xC)) >> 11 > s32(m.r(p + 0x30)):
                    hit("bounce-air")
                if s32(m.r(p + 0xC)) >> 11 <= s32(m.r(p + 0x30)):
                    n = s32((s32(m.r(p + 0x44)) * s32(vz2)) & M32)
                    q = (MAGIC * n) >> 32 >> 5
                    q += 1 if q < 0 else 0
                    m.w(p + 0x18, q & M32)
                    m.w(p + 0xC, shl11(m.r(p + 0x30)) if mut("bounce-z-shift") else m.r(p + 0x30))
                    if m.r(p + 0x40):
                        hit("bounce")
                        m.w(p + 0x40, m.r(p + 0x40) - 1)
                    else:
                        hit("bounce-last")
                        m.w(p, flags | 1)
            elif flags & 0x10:
                owner = m.call("owner", unit)
                hit("frame-owner" if owner else "frame-no-owner")
                if owner:
                    mode = 8
                    ty = m.r(unit)
                    if ty == 1:
                        hit("frame-monster")
                        mode = 0xE
                    elif ty == 3:
                        if m.r(owner) != 1:
                            hit("frame-missile-fatal")
                            raise Halt(0x1A9)
                        if m.r(owner + 0x10) in (0, 0xC):
                            hit("frame-missile-skip-c" if m.r(owner + 0x10) == 0xC else "frame-missile-skip-0")
                            return None
                        hit("frame-missile")
                        mode = 0xE
                    else:
                        hit("frame-other")
                    r = m.call("frame", owner, mode)
                    w, h = r & 0x3FF, (r >> 10) & 0x3FF
                    m.w(p + 0x34, w)
                    h2 = (h + 10) & M32 if ty == 3 else h
                    m.w(p + 0x3C, h2)
                    m.w(p + 0x34, m.r(p + 0x34) + rec_get(m, owner, 0x34))
                    m.w(p + 0x3C, m.r(p + 0x3C) + rec_get(m, owner, 0x3C))
                    x0, y0 = m.r(p + 0x34), m.r(p + 0x38)
                    m.w(p + 4, x0)
                    m.w(p + 8, y0)
                    r2 = m.call("world_to_screen", x0, y0)
                    m.w(p + 4, r2)
                    m.w(p + 8, r2 ^ 0xA5A5A5A5)
                    m.w(p + 0xC, (-shl11(m.r(p + 0x3C))) & M32)
                    skip_tail = True
            else:
                third = [s32(m.r(p + o)) for o in (0x28, 0x2C, 0x30)]
                pos = [s32(m.r(p + o)) >> 11 for o in (4, 8, 0xC)]
                if flags & 0x20:
                    arrived = all(pos[i] >= third[i] for i in range(3))
                else:
                    arrived = all(pos[i] <= third[i] for i in range(3))
                if mut("arrive-cmp"):
                    arrived = all(pos[i] < third[i] for i in range(3)) if not flags & 0x20 else arrived
                hit("arrive-ge" if flags & 0x20 else "arrive-le") if arrived else hit("moving")
                if arrived:
                    m.w(p + 4, shl11(third[0]))
                    m.w(p + 8, shl11(third[1]))
                    m.w(p + 0xC, shl11(third[2]))
                    m.w(p, flags | 1)
            if not skip_tail and not (m.r(p) & 0x10):
                x, y = sar11(m.r(p + 4)), sar11(m.r(p + 8))
                m.w(p + 0x34, x)
                m.w(p + 0x38, y)
                r3 = m.call("cell_to_screen", x, y)
                m.w(p + 0x34, r3)
                m.w(p + 0x38, r3 ^ 0xA5A5A5A5)
                m.w(p + 0x3C, (-s32(sar11(m.r(p + 0xC)))) & M32)
            return None
        # FUN_004da350 gets many rounds: its branches depend on random flag bits
        old = rig_mod.ROUNDS
        rig_mod.ROUNDS = 400
        try:
            self.run("FUN_004da350", 0x4DA350, build, model, 1, ret=False)
        finally:
            rig_mod.ROUNDS = old
        missing = [b for b in ("throw-count", "throw-end", "bounce-test", "bounce", "bounce-last", "frame-owner", "frame-no-owner",
                               "frame-monster", "frame-missile", "frame-missile-skip-0", "frame-missile-skip-c", "frame-missile-fatal",
                               "frame-other", "arrive-le", "arrive-ge", "moving", "early-return", "bounce-air", "bounce-and-follow") if not COVER.get(b)]
        rig_mod.require(not missing, f"FUN_004da350 branches never reached: {missing}")


def rec_get(m, unit, off):
    """Field `off` of `unit`'s motion record (0 when it has none): what FUN_004da0b0 / FUN_004da0f0 return."""
    cur = rec_of(m, unit)
    return m.r(cur + off) if cur else 0


def block_or_zero(m, cur):
    """Used by the `keep-old` mutation: only a freshly allocated block is cleared."""
    return cur if any(cur == b[0] for b in m.blocks) else -1


def suite(rig):
    """Run every group; returns the number of cases."""
    COVER.clear()
    s = Suite(rig)
    s.pointer_pair()
    s.lifecycle()
    s.getters()
    s.setters()
    s.flags()
    s.throw()
    s.step()
    return rig.cases


MUTATIONS = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
             for name in ("logical-shift", "keep-old", "clear-nothing", "no-tick-floor", "floor-half", "arrive-cmp", "bounce-z-shift")}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "unit motion record", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
