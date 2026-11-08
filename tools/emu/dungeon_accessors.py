# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the DUNGEON\\Dungeon.cpp field accessors and forwarding wrappers in stock game.exe 1.14d.

    uv run python dungeon_accessors.py [--mutation-check]

Runs game.exe's own code for 52 small functions between FUN_00619550 and
FUN_0061afa0 against a hand-written model on random room1 / dungeon records
(see _dungeon_rig.py): return value, ordered callee log (registers, then stack
arguments), the whole arena afterwards, the fatal-error line (null-record
asserts, hook of 0x40_8a60) and the bytes `ret n` pops.

Claimed: getters and setters of room1 / dungeon fields, the null-record
asserts, and which record field each wrapper passes on to which callee, in
which register or stack slot. The forwarded callees (0x66_xxxx room2 helpers,
0x61_b6xx / 0x64_xxxx act helpers) are hooked and logged, not claimed.
`--mutation-check` breaks the model five ways and requires each to fail.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32

MUT = set()


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def cn(address):
    """The log name of the hooked callee at `address`."""
    return f"c{address:x}"


# (address, stack argument count, register arguments) of each hooked callee.
CALLEES = (
    (0x66_AB70, 0, ("ecx", "edx")), (0x66_AF80, 0, ("ecx", "edx")), (0x66_B2B0, 3, ("ecx", "edx")),
    (0x64_C0C0, 1, ()), (0x66_BFA0, 0, ("ecx",)), (0x61_B640, 3, ("ecx", "edx")),
    (0x61_B690, 3, ("ecx", "edx")), (0x61_B6F0, 0, ("ecx", "edx")), (0x61_B7B0, 1, ("ecx", "edx")),
    (0x66_BAC0, 0, ("ecx", "edx")), (0x66_BB20, 0, ("ecx",)), (0x66_BB60, 0, ("ecx",)),
    (0x66_BBA0, 0, ("ecx",)), (0x61_B840, 0, ("ecx",)), (0x61_B870, 0, ("ecx", "edx")),
    (0x61_BA30, 0, ("ecx",)), (0x61_BAE0, 0, ("ecx",)), (0x64_3020, 0, ("ecx", "edx")),
    (0x64_3200, 0, ("ecx",)), (0x64_26A0, 0, ("ecx",)), (0x64_2740, 0, ("ecx",)),
    (0x66_BAB0, 0, ("ecx",)), (0x64_2BE0, 0, ("ecx", "edx")), (0x64_2380, 0, ("ecx",)),
    (0x66_AB90, 0, ("ecx", "edx")), (0x66_ABE0, 0, ("ecx", "edx")), (0x66_71E0, 2, ("ecx", "edx")),
    (0x66_6A80, 1, ("ecx", "edx")), (0x61_BAC0, 0, ("ecx", "edx")), (0x66_D410, 0, ("ecx",)),
    (0x66_D750, 0, ("ecx", "edx")),
)


def install(rig):
    """Hook every callee this oracle leaves unclaimed."""
    for address, nstack, regs in CALLEES:
        rig.callee(cn(address), address, nstack, regs)


def rec(rig, i):
    """Address of the i-th 0x100-byte record slot."""
    return rig.slot(i)


def pick(rng, address, null=0.25):
    """`address` or, sometimes, a null pointer."""
    return 0 if rng.random() < null else address


class Suite:
    """Builds and runs every case; `count` is the number of cases run."""

    def __init__(self, rig):
        """Keep the rig and hook the callees."""
        self.rig = rig
        install(rig)
        rig.custom[cn(0x61_BAE0)] = lambda e: rig.rng.choice((0, 1, 2, 3, 4, 0x103, 0x1FF, rig.rng.randrange(1 << 32)))

    def run(self, name, addr, build, model, nstack, mask=M32, ret=True):
        """Run one function through the rig."""
        self.rig.run(name, addr, build, model, nstack, mask, ret)

    # ---- generic forwarders -------------------------------------------------
    def forwarder(self, name, addr, nstack, callee, null="halt", line=0, argfn=None, nullfn=None):
        """A wrapper whose first argument is a room1 / dungeon and whose callee call is argfn(m, a).

        null: "halt" (assert `line` on a null first argument), "zero" (return 0), "call" (no check).
        """
        rig = self.rig

        def build(img, rng):
            """Arguments: a record (sometimes null), then random values."""
            first = pick(rng, rig.slot(0), 0.3 if null != "call" else 0)
            rest = [rng.choice((0, 1, rng.randrange(1 << 32))) for _ in range(nstack - 1)]
            return {"stack": [first] + rest}

        def model(m, a):
            """Null check, then the forwarded call; the callee's result is the return value."""
            first = a["stack"][0]
            if first == 0 and null == "halt":
                raise Halt(line)
            if first == 0 and null == "zero":
                return 0
            return m.call(cn(callee), *argfn(m, a))
        self.run(name, addr, build, model, nstack)

    def all_forwarders(self):
        """The wrappers that pass room1 / dungeon fields to a callee."""
        def r2(m, a):
            """The room2 pointer of the first argument's room1."""
            return m.r(a["stack"][0] + 0x10)

        def d48(m, a):
            """The act pointer at +0x48 of the first argument."""
            return m.r(a["stack"][0] + 0x48)
        swap = (lambda x, y: (y, x)) if mut("swap-args") else (lambda x, y: (x, y))
        self.forwarder("FUN_00619560", 0x619560, 2, 0x66_AB70, "halt", 0x62,
                       lambda m, a: swap(r2(m, a), a["stack"][1]))
        self.forwarder("FUN_00619620", 0x619620, 2, 0x66_AF80, "halt", 0x8A,
                       lambda m, a: (r2(m, a), a["stack"][1]))
        self.forwarder("FUN_00619e50", 0x619E50, 5, 0x66_B2B0, "halt", 0x221,
                       lambda m, a: (d48(m, a), a["stack"][1], a["stack"][2], a["stack"][3], a["stack"][4]))
        self.forwarder("FUN_00619fd0", 0x619FD0, 1, 0x66_BFA0, "halt", 0x2BB, lambda m, a: (r2(m, a),))
        for name, addr, callee, line in (("FUN_0061a070", 0x61A070, 0x61_B640, 0x2D7),
                                         ("FUN_0061a0c0", 0x61A0C0, 0x61_B690, 0x2E9)):
            self.five_arg(name, addr, callee, line)
        self.forwarder("FUN_0061a140", 0x61A140, 3, 0x61_B7B0, "halt", 0x313,
                       lambda m, a: (d48(m, a), a["stack"][1], a["stack"][2]))
        self.forwarder("FUN_0061a1d0", 0x61A1D0, 2, 0x66_BAC0, "zero", 0,
                       lambda m, a: (r2(m, a), a["stack"][1]))
        self.forwarder("FUN_0061a1f0", 0x61A1F0, 1, 0x66_BB20, "zero", 0, lambda m, a: (r2(m, a),))
        self.forwarder("FUN_0061a210", 0x61A210, 1, 0x66_BB60, "halt", 0x363, lambda m, a: (r2(m, a),))
        self.forwarder("FUN_0061a250", 0x61A250, 1, 0x66_BBA0, "zero", 0, lambda m, a: (r2(m, a),))
        self.forwarder("FUN_0061a360", 0x61A360, 1, 0x61_B840, "halt", 0x3C5, lambda m, a: (d48(m, a),))
        self.forwarder("FUN_0061aa20", 0x61AA20, 1, 0x64_3200, "call", 0, lambda m, a: (d48(m, a),))
        self.forwarder("FUN_0061aaf0", 0x61AAF0, 1, 0x64_26A0, "call", 0, lambda m, a: (a["stack"][0],))
        self.forwarder("FUN_0061abf0", 0x61ABF0, 2, 0x64_2BE0, "call", 0,
                       lambda m, a: (d48(m, a), a["stack"][1]))
        self.forwarder("FUN_0061ac10", 0x61AC10, 1, 0x64_2380, "halt", 0x5B9, lambda m, a: (r2(m, a),))
        self.forwarder("FUN_0061ac50", 0x61AC50, 2, 0x66_AB90, "halt", 0x5C4,
                       lambda m, a: (r2(m, a), a["stack"][1]))
        self.forwarder("FUN_0061ac90", 0x61AC90, 2, 0x66_ABE0, "halt", 0x5CF,
                       lambda m, a: (r2(m, a), a["stack"][1]))
        self.forwarder("FUN_0061acd0", 0x61ACD0, 4, 0x66_71E0, "halt", 0x5DA,
                       lambda m, a: (r2(m, a), a["stack"][1], a["stack"][2], a["stack"][3]))
        self.forwarder("FUN_0061ad10", 0x61AD10, 3, 0x66_6A80, "call", 0,
                       lambda m, a: (r2(m, a), a["stack"][1], a["stack"][2]))
        self.forwarder("FUN_0061aed0", 0x61AED0, 2, 0x61_BAC0, "zero", 0,
                       lambda m, a: (r2(m, a), a["stack"][1]))
        self.forwarder("FUN_0061aef0", 0x61AEF0, 1, 0x66_D410, "zero", 0, lambda m, a: (r2(m, a),))

    def five_arg(self, name, addr, callee, line):
        """FUN_0061a070 / FUN_0061a0c0: act +0x48 and four more; the last is a room1 (or null) whose +0x10 goes on."""
        rig = self.rig

        def build(img, rng):
            """A dungeon (sometimes null), three values and a room1 or null."""
            return {"stack": [pick(rng, rig.slot(0), 0.3), rng.randrange(1 << 32), rng.randrange(1 << 32),
                              rng.randrange(1 << 32), pick(rng, rig.slot(1), 0.4)]}

        def model(m, a):
            """Assert, then pass (act, arg1) in registers and (arg2, arg3, room2-of-arg4 or 0) on the stack."""
            s = a["stack"]
            if s[0] == 0:
                raise Halt(line)
            last = m.r(s[4] + 0x10) if s[4] else 0
            return m.call(cn(callee), m.r(s[0] + 0x48), s[1], s[2], s[3], last)
        self.run(name, addr, build, model, 5)

    # ---- odd forwarders --------------------------------------------------------
    def odd_forwarders(self):
        """The wrappers with a null test on a second argument or two callees."""
        rig = self.rig

        def two_rooms(name, addr, callee):
            """FUN_0061a110 / FUN_0061a9f0: both arguments are room1 or null; room2 of each in ecx / edx."""
            def build(img, rng):
                """Two room1 pointers, either may be null."""
                return {"stack": [pick(rng, rig.slot(0), 0.3), pick(rng, rig.slot(1), 0.3)]}

            def model(m, a):
                """Room2 of each (0 for null) goes on in ecx and edx."""
                x = m.r(a["stack"][0] + 0x10) if a["stack"][0] else 0
                y = m.r(a["stack"][1] + 0x10) if a["stack"][1] else 0
                return m.call(cn(callee), x, y)
            self.run(name, addr, build, model, 2)
        two_rooms("FUN_0061a110", 0x61A110, 0x61_B6F0)
        two_rooms("FUN_0061a9f0", 0x61A9F0, 0x64_3020)

        def build_a3a0(img, rng):
            """Dungeon and room1, each sometimes null."""
            return {"stack": [pick(rng, rig.slot(0), 0.25), pick(rng, rig.slot(1), 0.25)]}

        def model_a3a0(m, a):
            """Two asserts (dungeon 0x3ce, room1 0x3d1), then (act, room2) in ecx / edx."""
            d, r = a["stack"]
            if d == 0:
                raise Halt(0x3CE)
            if r == 0:
                raise Halt(0x3D1)
            return m.call(cn(0x61_B870), m.r(d + 0x48), m.r(r + 0x10))
        self.run("FUN_0061a3a0", 0x61A3A0, build_a3a0, model_a3a0, 2)

        def build_a3f0(img, rng):
            """Dungeon (field +0x50 sometimes zero) and room1."""
            if rng.random() < 0.5:
                rig_mod_w(img, rig, 0, 0x50, 0)
            return {"stack": [pick(rng, rig.slot(0), 0.2), pick(rng, rig.slot(1), 0.25)]}

        def model_a3f0(m, a):
            """Asserts 0x3dd (null), 0x3de (+0x50 set), 0x3df (null room1), then room2 in ecx."""
            d, r = a["stack"]
            if d == 0:
                raise Halt(0x3DD)
            if m.r(d + 0x50):
                raise Halt(0x3DE)
            if r == 0:
                raise Halt(0x3DF)
            return m.call(cn(0x61_BA30), m.r(r + 0x10))
        self.run("FUN_0061a3f0", 0x61A3F0, build_a3f0, model_a3f0, 2)

        def build_ab30(img, rng):
            """A room1 or null."""
            return {"stack": [pick(rng, rig.slot(0), 0.3)]}

        def model_ab30(m, a):
            """Null: 0x64_2740 on 0. Else it takes 0x66_bab0 of room2 (two calls)."""
            r = a["stack"][0]
            inner = m.call(cn(0x66_BAB0), m.r(r + 0x10)) if r else 0
            return m.call(cn(0x64_2740), inner)
        self.run("FUN_0061ab30", 0x61AB30, build_ab30, model_ab30, 1)

        def build_a440(img, rng):
            """Any room1 (no null check)."""
            return {"stack": [rig.slot(0)]}

        def model_a440(m, a):
            """0x61_bae0 on room2; the result is 1 when its low byte is 3 or more."""
            v = m.call(cn(0x61_BAE0), m.r(a["stack"][0] + 0x10))
            return int((v & 0xFF) >= (4 if mut("byte-compare") else 3))
        self.run("FUN_0061a440", 0x61A440, build_a440, model_a440, 1)

        def build_af10(img, rng):
            """Dungeon with a room1 chain (+0x10, next +0x7c), a room1 and a second one, all maybe null."""
            chain = [rig.slot(4), rig.slot(5), rig.slot(6)]
            rig_mod_w(img, rig, 0, 0x10, chain[0] if rng.random() < 0.9 else 0)
            for i, _ in enumerate(chain):
                rig_mod_w(img, rig, 4 + i, 0x7C, chain[i + 1] if i + 1 < len(chain) else 0)
            second = rng.choice((chain[0], chain[1], chain[2], rig.slot(7), 0))
            return {"stack": [rig.slot(0), second, pick(rng, rig.slot(8), 0.4)]}

        def model_af10(m, a):
            """ecx = room2 of arg1 when arg1 is on the dungeon's chain, else 0; edx = room2 of arg2 or 0."""
            d, b, c = a["stack"]
            ecx = 0
            if b:
                node = m.r(d + 0x10)
                while node:
                    if node == b:
                        ecx = m.r(b + 0x10)
                        break
                    node = m.r(node + 0x7C)
            edx = m.r(c + 0x10) if c else 0
            return m.call(cn(0x66_D750), ecx, edx)
        self.run("FUN_0061af10", 0x61AF10, build_af10, model_af10, 3)

    # ---- plain accessors -------------------------------------------------------
    def accessors(self):
        """Getters, setters and flag helpers."""
        rig = self.rig
        run = self.run

        def one(img, rng):
            """A record in slot 0, with the pointers some getters follow in slots 1..3."""
            rig_mod_w(img, rig, 0, 8, rig.slot(1))
            rig_mod_w(img, rig, 0, 0x2C, rig.slot(2))
            return rig.slot(0)

        def b_first(img, rng):
            """One stack argument: the record."""
            return {"stack": [one(img, rng)]}

        run("FUN_00619720", 0x619720, lambda i, r: {"stack": [r.randrange(1 << 32)]},
            lambda m, a: (a["stack"][0] + 0x18) & M32, 1)
        run("FUN_0061a330", 0x61A330, lambda i, r: {"stack": [pick(r, one(i, r))]},
            lambda m, a: (m.r(a["stack"][0] + 0x18) if a["stack"][0] else (_ for _ in ()).throw(Halt(0x3A6))), 1)
        run("FUN_0061aa90", 0x61AA90, lambda i, r: {"stack": [pick(r, one(i, r))]},
            lambda m, a: (m.r(a["stack"][0] + 0x48) if a["stack"][0] else (_ for _ in ()).throw(Halt(0x550))), 1)
        run("FUN_0061ae80", 0x61AE80, lambda i, r: {"stack": [pick(r, one(i, r))]},
            lambda m, a: (m.r(a["stack"][0] + 8) if a["stack"][0] else (_ for _ in ()).throw(Halt(0x634))), 1)
        run("FUN_0061aac0", 0x61AAC0, lambda i, r: {"stack": [pick(r, one(i, r))]},
            lambda m, a: (m.r(a["stack"][0] + 0xC) if a["stack"][0] else 0), 1)
        run("FUN_0061ab60", 0x61AB60, b_first, lambda m, a: m.r(a["stack"][0] + 0x34) & 4, 1)
        run("FUN_0061aae0", 0x61AAE0, lambda i, r: {"ecx": one(i, r)},
            lambda m, a: m.r(a["ecx"] + 0x10), 0)
        run("FUN_00619550", 0x619550, lambda i, r: {"ecx": one(i, r)},
            lambda m, a: m.r(a["ecx"] + 0x5C), 0)

        def ecx_assert(line, field=None):
            """Model for a register-record getter with a null assert."""
            def model(m, a):
                """Null ecx is fatal; else the field (or address) result."""
                if a["ecx"] == 0:
                    raise Halt(line)
                return m.r(a["ecx"] + 4)
            return model
        run("FUN_0061aa60", 0x61AA60, lambda i, r: {"ecx": pick(r, one(i, r))}, ecx_assert(0x547), 0)

        def a_61a4d0(m, a):
            """Bit 1 of room1 +0x34 (value 2), fatal 0x42f on null."""
            if a["stack"][0] == 0:
                raise Halt(0x42F)
            return m.r(a["stack"][0] + 0x34) & (1 if mut("flag-bit") else 2)
        run("FUN_0061a4d0", 0x61A4D0, lambda i, r: {"stack": [pick(r, one(i, r))]}, a_61a4d0, 1)

        def a_61a510(m, a):
            """Set (nonzero) or clear (zero) bit 1 of room1 +0x34, keeping the other bits; fatal 0x438 on null."""
            r, on = a["stack"]
            if r == 0:
                raise Halt(0x438)
            flags = m.r(r + 0x34)
            m.w(r + 0x34, (flags | 2) if on else (flags & ~2 & M32))
        run("FUN_0061a510", 0x61A510,
            lambda i, r: {"stack": [pick(r, one(i, r)), r.choice((0, 1, 2, 0x80000000, r.randrange(1 << 32)))]},
            a_61a510, 2, ret=False)

        def a_61af60(m, a):
            """Store the callback at +0x4c; fatal 0x67f on null."""
            if a["stack"][0] == 0:
                raise Halt(0x67F)
            m.w(a["stack"][0] + 0x4C, a["stack"][1])
        run("FUN_0061af60", 0x61AF60, lambda i, r: {"stack": [pick(r, one(i, r)), r.randrange(1 << 32)]},
            a_61af60, 2, ret=False)

        def a_61afa0(m, a):
            """Ring of four dwords from +0x38 with a byte index at +0x14; null is a no-op."""
            d, v = a["stack"]
            if d == 0:
                return None
            index = m.r(d + 0x14, 1)
            m.w(d + 0x38 + index * 4, v)
            m.w(d + 0x14, (index + 1) & (7 if mut("ring-mask") else 3), 1)
        run("FUN_0061afa0", 0x61AFA0, lambda i, r: {"stack": [pick(r, one(i, r)), r.randrange(1 << 32)]},
            a_61afa0, 2, ret=False)

        def a_619660(m, a):
            """Out parameter = [[r+8]+0xc], result [[r+8]+8]; no record or no +8: out 0, result 0."""
            r, out = a["stack"]
            if r == 0 or m.r(r + 8) == 0:
                m.w(out, 0)
                return 0
            t = m.r(r + 8)
            m.w(out, m.r(t + 0xC))
            return m.r(t + 8)

        def b_tile(img, rng):
            """Record sometimes null, its +8 sometimes null, out parameter in slot 3."""
            if rng.random() < 0.3:
                rig_mod_w(img, rig, 0, 8, 0)
            else:
                rig_mod_w(img, rig, 0, 8, rig.slot(1))
            return {"stack": [pick(rng, rig.slot(0), 0.2), rig.slot(3)]}
        run("FUN_00619660", 0x619660, b_tile, a_619660, 2)

        def tile_pair(line, count_off, array_off):
            """FUN_006196a0 / FUN_006196e0: out = [[r+8]+count_off], result [[r+8]+array_off]."""
            def model(m, a):
                """Fatal on null; else the two fields of the record at +8."""
                r, out = a["stack"]
                if r == 0:
                    raise Halt(line)
                t = m.r(r + 8)
                m.w(out, m.r(t + count_off))
                return m.r(t + array_off)
            return model
        for name, addr, line, co, ao in (("FUN_006196a0", 0x6196A0, 0xB1, 4, 0),
                                         ("FUN_006196e0", 0x6196E0, 0xBE, 0x14, 0x10)):
            def b_pair(img, rng):
                """Record (sometimes null) whose +8 is slot 1, out parameter in slot 3."""
                rig_mod_w(img, rig, 0, 8, rig.slot(1))
                return {"stack": [pick(rng, rig.slot(0), 0.2), rig.slot(3)]}
            run(name, addr, b_pair, tile_pair(line, co, ao), 2)

    def counters(self):
        """The unit counter at room1 +0x28 and the list heads."""
        rig = self.rig
        run = self.run

        def counter_values(img, rng):
            """Counter -1, 0, 1 or random."""
            rig_mod_w(img, rig, 0, 0x28, rng.choice((0, 1, 2, M32, rng.randrange(1 << 32))))
            return {"stack": [pick(rng, rig.slot(0), 0.25)]}

        def inc(m, a):
            """Increment +0x28 unless null."""
            if a["stack"][0]:
                m.w(a["stack"][0] + 0x28, m.r(a["stack"][0] + 0x28) + 1)
        run("FUN_00619ee0", 0x619EE0, counter_values, inc, 1, ret=False)
        run("FUN_00619f00", 0x619F00, counter_values,
            lambda m, a: m.r(a["stack"][0] + 0x28) if a["stack"][0] else 0, 1)

        def dec(m, a):
            """Null is a no-op; a counter of 0 or less is fatal 0x27c; else decrement."""
            r = a["stack"][0]
            if r == 0:
                return None
            value = m.r(r + 0x28)
            if s32(value) <= (-1 if mut("counter-le") else 0):
                raise Halt(0x27C)
            m.w(r + 0x28, value - 1)
        run("FUN_00619f20", 0x619F20, counter_values, dec, 1, ret=False)

        def ecx_head(img, rng):
            """The room1 in ecx (sometimes null)."""
            rig_mod_w(img, rig, 0, 0x2C, rig.slot(2))
            return {"ecx": pick(rng, rig.slot(0), 0.25), "edx": rng.choice((0, 1, 7, rng.randrange(1 << 32)))}

        def primary(m, a):
            """Address of the primary unit-list head (+0x74); fatal 0x286 on null."""
            if a["ecx"] == 0:
                raise Halt(0x286)
            return a["ecx"] + 0x74
        run("FUN_00619f60", 0x619F60, ecx_head, primary, 0)

        def secondary(m, a):
            """Address of the secondary head (+0x1c); a nonzero flag stores 1 through [+0x2c]. Fatal 0x2ad on null."""
            r = a["ecx"]
            if r == 0:
                raise Halt(0x2AD)
            if a["edx"]:
                m.w(m.r(r + 0x2C), 1)
            return r + 0x1C
        run("FUN_00619f90", 0x619F90, ecx_head, secondary, 0)

        def set_collision(m, a):
            """Store edx at +0x20; fatal 0x2ce on null."""
            if a["ecx"] == 0:
                raise Halt(0x2CE)
            m.w(a["ecx"] + 0x20, a["edx"])
        run("FUN_0061a040", 0x61A040, ecx_head, set_collision, 0, ret=False)

        def sort_wrapper(m, a):
            """Fatal 0x249 on null; calls 0x64_c0c0(room1), returns the head at +0x74."""
            r = a["stack"][0]
            if r == 0:
                raise Halt(0x249)
            m.call(cn(0x64_C0C0), r)
            return m.r(r + 0x74)
        run("FUN_00619ea0", 0x619EA0, lambda i, r: {"stack": [pick(r, rig.slot(0), 0.25)]}, sort_wrapper, 1)


def rig_mod_w(img, rig, slot, off, value):
    """Write a dword into slot `slot` at `off` of the arena image."""
    index = slot * 0x100 + off
    img[index:index + 4] = (value & M32).to_bytes(4, "little")


def suite(rig):
    """Run every group; returns the number of cases."""
    s = Suite(rig)
    s.accessors()
    s.counters()
    s.all_forwarders()
    s.odd_forwarders()
    return rig.cases


MUTATIONS = {
    "swap-args": lambda on: MUT.add("swap-args") if on else MUT.discard("swap-args"),
    "flag-bit": lambda on: MUT.add("flag-bit") if on else MUT.discard("flag-bit"),
    "ring-mask": lambda on: MUT.add("ring-mask") if on else MUT.discard("ring-mask"),
    "counter-le": lambda on: MUT.add("counter-le") if on else MUT.discard("counter-le"),
    "byte-compare": lambda on: MUT.add("byte-compare") if on else MUT.discard("byte-compare"),
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Dungeon.cpp accessor and wrapper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
