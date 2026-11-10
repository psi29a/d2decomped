# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the small ENGINE\\Gfx.cpp / GfxUtil.cpp helpers in stock game.exe 1.14d.

    uv run python engine_gfx_small.py [--mutation-check]

Runs game.exe's own code for the unit graphics record's COF list, layer flags,
overlay test and cache getters (FUN_0046e100 ... FUN_0046f9d0) and the
GfxUtil.cpp token / layer / table helpers (FUN_004da720 ... FUN_004dad50,
FUN_004db040 ... FUN_004db1c0)
against a hand-written model on random arena bytes (see _dungeon_rig.py):
return value, ordered callee log, the whole arena, the fatal-error line and
the bytes `ret n` pops.

Claimed: the COF list walks, unlink-and-free, "is the mode's node current"
replacement logic, the layer-flag byte array (20 / 16 / 8 by unit type), the
cache-global getters, the COF header layer lookups, the token copy and fold,
the Overlay.txt / MonStats2 row lookups, and the three unused list helpers.
Hooked, not claimed: the pool allocator and free (0x40_b380, 0x40_b3c0).
The unit mode getter FUN_0046da60 and type getter FUN_0046d9f0 run for real
with unit +0xc8 bit 3 clear (the transform-state branch is not exercised; it
is not claimed). `--mutation-check` breaks the model eleven ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, s32

MUT = set()
UNIT, GFX, OUT = 0, 1, 14        # arena slots: the unit, its graphics record, an 8-byte output pair
NODES = (2, 3, 4, 5)             # COF list nodes
GRAPHICS = (6, 7, 8, 9)          # the loaded graphics the nodes point at (data at +0x14)
OVERLAYS = (10, 11, 12, 13)      # overlay nodes (0xa8 bytes: next at +0xa4)
COF_FILE, LIST_FILE = 0x6D6C1C, 0x6DB974
TABLE_PTR = 0x744304
HDR = 0x1000                     # arena offset of the 0xbc4-byte table header
ROWS2, OVROWS = 0x1D00, 0x2300   # MonStats2 and Overlay.txt rows


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value, size=4):
    """Write `value` at arena byte offset `off`."""
    img[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def slot_off(slot, off=0):
    """Arena byte offset of `off` inside `slot`."""
    return slot * 0x100 + off


def special(rng):
    """A dword that is often a boundary value."""
    return rng.choice((0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x10, 0xF, 0x11, 0x14, rng.randrange(1 << 32)))


def alloc_block(m, name, size, *args):
    """Log the hooked allocator, register the block it returned; null when the stock run failed."""
    block = m.call(name, *args)
    if block:
        buf = bytearray(size)
        m.blocks.append((block, buf))
        m.regions.append((block, buf))
    return block


class Suite:
    """Builds and runs every case; the rig counts them."""

    def __init__(self, rig):
        """Keep the rig, hook the pool calls and point the table global into the arena."""
        from unicorn.x86_const import UC_X86_REG_ECX
        self.rig = rig
        self.fail_alloc = False
        self.ecx = UC_X86_REG_ECX
        rig.callee("pool_free", 0x40_B3C0, 2, ("ecx", "edx"))
        rig._callee("pool_alloc", 0x40_B380, 2, ("ecx", "edx"), self._alloc)
        rig.m.w32(TABLE_PTR, rig.arena + HDR)

    def _alloc(self, e):
        """Allocator stand-in: a zeroed block of ECX bytes, or null while fail_alloc is set."""
        return 0 if self.fail_alloc else e.alloc(max(e.mu.reg_read(self.ecx), 1))

    def run(self, name, addr, build, model, nstack, mask=M32, ret=True):
        """Run one function through the rig."""
        self.rig.run(name, addr, build, model, nstack, mask, ret)

    # ---- builders ------------------------------------------------------------
    def unit(self, img, rng, gfx_null=0.15, nodes=True, overlays=False):
        """A unit with a graphics record (null with probability gfx_null), a COF list and an overlay list."""
        rig = self.rig
        put(img, slot_off(UNIT, 0xC8), 0)
        put(img, slot_off(UNIT, 0), rng.choice((0, 1, 2, 3, 4)))
        put(img, slot_off(UNIT, 0x10), rng.choice((0, 1, 2, 0xC, 0x11)))
        put(img, slot_off(UNIT, 0x30), rng.choice((0, 0, 1)))
        put(img, slot_off(UNIT, 0x40), rng.choice((0, 1, 2, 0xC, 0x11)))
        put(img, slot_off(UNIT, 0x54), 0 if rng.random() < gfx_null else rig.slot(GFX))
        if nodes:
            count = rng.choice((0, 1, 2, 3, 4))
            order = list(NODES[:count])
            rng.shuffle(order)
            prev_off = slot_off(GFX, 0x28)
            put(img, prev_off, 0)
            for i, node in enumerate(order):
                put(img, prev_off, rig.slot(node))
                put(img, slot_off(node, 0), rng.choice((0, 1, 2, 0xC, 0x11, 0x11)))
                put(img, slot_off(node, 4), rng.choice((0, 1, 2, 3, 0x20202020)))
                put(img, slot_off(node, 8), rig.slot(GRAPHICS[i]))
                put(img, slot_off(GRAPHICS[i], 0x14), rng.randrange(1 << 32))
                put(img, slot_off(node, 0x10), 0)
                prev_off = slot_off(node, 0x10)
        if overlays:
            count = rng.choice((0, 1, 2, 3, 4))
            prev_off = slot_off(GFX, 0x2C)
            put(img, prev_off, 0)
            for node in OVERLAYS[:count]:
                put(img, prev_off, rig.slot(node))
                put(img, slot_off(node, 4), rng.choice((0, 1, 2, 3, 7)))
                put(img, slot_off(node, 0xA4), 0)
                prev_off = slot_off(node, 0xA4)
        return rig.slot(UNIT)

    def maybe_null(self, rng, unit, p=0.0):
        """`unit` or null."""
        return 0 if rng.random() < p else unit

    # ---- model helpers -------------------------------------------------------
    @staticmethod
    def gfx_of(m, unit):
        """Unit +0x54 (FUN_00463e10), 0 for a null unit."""
        return m.r(unit + 0x54) if unit else 0

    @staticmethod
    def nodes(m, gfx):
        """The COF list as node addresses."""
        out, n = [], m.r(gfx + 0x28)
        while n:
            out.append(n)
            n = m.r(n + 0x10)
        return out

    @staticmethod
    def unit_mode(m, unit):
        """FUN_0046da60 with the transform-state bit clear: +0x40 for a type 0 / 1 unit with +0x30, else +0x10."""
        if m.r(unit) in (0, 1) and m.r(unit + 0x30):
            return m.r(unit + 0x40)
        return m.r(unit + 0x10)

    # ---- the COF list --------------------------------------------------------
    def cof_list(self):
        """FUN_0046e570, 0046e5a0, 0046e610, 0046e690, 0046ec80, 0046ece0."""
        gfx_of, nodes = self.gfx_of, self.nodes
        build = lambda img, rng: {"ecx": self.unit(img, rng), "edx": rng.choice((0, 1, 2, 0xC, 0x11, 5)), "stack": [rng.choice((0, 1, 2, 3, 0x20202020, 0xFFFFFFFF, 0xC))]}  # noqa: E731

        def data(m, a):
            """The data pointer (graphic +0x14) of the first node with the mode, else 0 (also without a record)."""
            g = gfx_of(m, a["ecx"])
            if not g:
                return 0
            for n in nodes(m, g):
                if m.r(n) == a["edx"]:
                    return m.r(m.r(n + 8) + 0x14)
            return 0
        self.run("FUN_0046e570", 0x46E570, build, data, 0)

        def find(m, a):
            """The first node matching (mode, token); mode -1 is the unit's own; fatal 0x3ac without a record."""
            g = gfx_of(m, a["ecx"])
            if not g:
                raise Halt(0x3AC)
            mode = self.unit_mode(m, a["ecx"]) if a["stack"][0] == M32 else a["stack"][0]
            for n in nodes(m, g):
                if m.r(n) == mode and m.r(n + 4) == a["edx"]:
                    return n
            return 0
        self.run("FUN_0046e5a0", 0x46E5A0, build, find, 1)

        def build_free(img, rng):
            """A unit and a graphic address: one of the list's, or a stranger."""
            u = self.unit(img, rng)
            pick = rng.choice(GRAPHICS + (30,))
            return {"ecx": u, "regs": {"eax": self.rig.slot(pick)}}

        def free_node(m, a):
            """Unlink and free the node whose graphic is EAX: 1 when found, else 0; fatal 0x3c8 without a record."""
            g = gfx_of(m, a["ecx"])
            if not g:
                raise Halt(0x3C8)
            prev = 0
            for n in nodes(m, g):
                if m.r(n + 8) == a["regs"]["eax"]:
                    nxt = m.r(n + 0x10)
                    m.call("pool_free", n, COF_FILE, 0x3D0, 0)
                    m.w((prev + 0x10) if prev else (g + 0x28), nxt)
                    return 1
                prev = n
            return 0
        self.run("FUN_0046e610", 0x46E610, build_free, free_node, 0)

        def build_cur(img, rng):
            """Unit (EDI), mode (EAX), token and two output pointers."""
            u = self.unit(img, rng)
            return {"regs": {"edi": u, "eax": rng.choice((0, 1, 2, 0xC, 0x11, 0xFFFFFFFF, 5))},
                    "stack": [rng.choice((0, 1, 2, 3, 0x20202020)), self.rig.slot(OUT), self.rig.slot(OUT) + 4]}

        def current(m, a):
            """No node: 1. Same token: *out1 = node, 0. Another token: free it, *out1 = 0, *out2 = old data, 1."""
            u, mode = a["regs"]["edi"], a["regs"]["eax"]
            g = gfx_of(m, u)
            if not g:
                raise Halt(0x3F0)
            if mode == M32:
                mode = self.unit_mode(m, u)
            hit = next((n for n in nodes(m, g) if m.r(n) == mode), 0)
            if not hit:
                return 1
            token, out1, out2 = a["stack"]
            if m.r(hit + 4) == token:
                m.w(out1, hit)
                return 0
            m.w(out1, 0)
            m.w(out2, m.r(m.r(hit + 8) + 0x14))
            # FUN_0046e610(graphic) from this unit: it frees the first node holding that graphic
            if not free_node(m, {"ecx": u, "regs": {"eax": m.r(hit + 8)}}):
                raise Halt(0x406)
            return 1
        self.run("FUN_0046e690", 0x46E690, build_cur, current, 3)

        def build_u(img, rng):
            """A unit in ECX."""
            return {"ecx": self.unit(img, rng)}

        def free_all(m, a):
            """Free every node (line 0x830), clear the head; fatal 0x82a without a record."""
            g = gfx_of(m, a["ecx"])
            if not g:
                raise Halt(0x82A)
            for n in nodes(m, g):
                m.call("pool_free", n, COF_FILE, 0x830, 0)
            m.w(g + 0x28, 0)
            return None
        self.run("FUN_0046ec80", 0x46EC80, build_u, free_all, 0, ret=False)

        def keep_death(m, a):
            """Free every node except modes 0 and 0x11, relinking the rest; fatal 0x83d without a record."""
            g = gfx_of(m, a["ecx"])
            if not g:
                raise Halt(0x83D)
            kept = 0
            for n in nodes(m, g):
                nxt = m.r(n + 0x10)
                if m.r(n) == 0 or m.r(n) == (0x12 if mut("keep-12") else 0x11):
                    kept = n
                else:
                    m.call("pool_free", n, COF_FILE, 0x847, 0)
                    m.w((kept + 0x10) if kept else (g + 0x28), nxt)
            return None
        self.run("FUN_0046ece0", 0x46ECE0, build_u, keep_death, 0, ret=False)

    # ---- layer flags ---------------------------------------------------------
    def layer_flags(self):
        """FUN_004db1a0, 004db1c0, 0046f020, 0046f280, 0046eac0, 0046f000, 0046e100."""
        gfx_of, nodes = self.gfx_of, self.nodes

        def build_i(img, rng):
            """Unit and a flag index."""
            return {"ecx": self.unit(img, rng), "edx": rng.choice((0, 1, 7, 15, 19, 20, 0xFFFFFFFF, rng.randrange(20))),
                    "stack": [rng.choice((0, 1, 0xFF, 0x1FF, 0x80000100))]}

        def get(m, a):
            """Flag byte i (two bytes apart); 0 without a record."""
            g = gfx_of(m, a["ecx"])
            if not g:
                return 0
            return m.r(g + 2 * s32(a["edx"]), 1)
        # the index is signed in the address arithmetic; keep it inside the arena image
        def build_get(img, rng):
            """As build_i, with an index that stays inside the arena."""
            a = build_i(img, rng)
            a["edx"] = rng.randrange(0, 20)
            return a
        self.run("FUN_004db1a0", 0x4DB1A0, build_get, get, 0, mask=0xFF)

        def set_(m, a):
            """Store the low byte of the stack argument at byte 2*i."""
            g = gfx_of(m, a["ecx"])
            if g:
                m.w(g + 2 * a["edx"], a["stack"][0] & (0 if mut("flag-zero") else 0xFF), 1)
            return None
        self.run("FUN_004db1c0", 0x4DB1C0, build_get, set_, 1, ret=False)

        def build_all(img, rng):
            """Unit and a byte in EDX."""
            return {"ecx": self.unit(img, rng), "edx": rng.choice((0, 1, 0x7F, 0xFF, 0x1234))}

        def set_all(m, a):
            """Twenty flag bytes = DL; fatal 0xbba without a record."""
            g = gfx_of(m, a["ecx"])
            if not g:
                raise Halt(0xBBA)
            for i in range(20):
                m.w(g + 2 * i, a["edx"] & 0xFF, 1)
            return None
        self.run("FUN_0046f020", 0x46F020, build_all, set_all, 0, ret=False)

        def build_clear(img, rng):
            """Unit of type 0 / 1 / 2 / 3.. and an index n."""
            u = self.unit(img, rng)
            return {"ecx": u, "edx": rng.choice((0, 5, 15, 16, 17, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF))}

        def clear_flags(m, a):
            """n >= 16 (signed): nothing. Else 20 / 16 / 8 flag bytes zeroed for a type 0 / 1 / 2 unit; other types fatal 0xd3e."""
            if s32(a["edx"]) >= 0x10:
                return None
            g = gfx_of(m, a["ecx"])
            ty = m.r(a["ecx"])
            if ty > 2:
                raise Halt(0xD3E)
            count = {0: 20, 1: 16, 2: 8}[ty]
            if mut("count-20"):
                count = 20
            for i in range(count):
                if g:
                    m.w(g + 2 * i, 0, 1)
            return None
        self.run("FUN_0046f280", 0x46F280, build_clear, clear_flags, 0, ret=False)

        def build_eac(img, rng):
            """Unit in EAX and n on the stack."""
            return {"regs": {"eax": self.unit(img, rng)}, "stack": [rng.choice((0, 3, 15, 16, 17, 0x80000000, 0xFFFFFFFF))]}

        def clear_list(m, a):
            """n >= 16 (signed): fatal 0x65f. Else zero 16 flag bytes, free the whole COF list (line 0x66d); fatal 0x668 without a record."""
            if s32(a["stack"][0]) >= 16:
                raise Halt(0x65F)
            g = gfx_of(m, a["regs"]["eax"])
            if not g:
                raise Halt(0x668)
            for i in range(16):
                m.w(g + 2 * i, 0, 1)
            for n in nodes(m, g):
                m.call("pool_free", n, COF_FILE, 0x66D, 0)
            m.w(g + 0x28, 0)
            return None
        self.run("FUN_0046eac0", 0x46EAC0, build_eac, clear_list, 1, ret=False)

        def clear_sixteen(m, a):
            """FUN_0046eac0 for n = 0..15; the first call frees the list, the rest find it empty."""
            g = gfx_of(m, a["ecx"])
            if not g:
                raise Halt(0x668)
            for i in range(16):
                m.w(g + 2 * i, 0, 1)
            for n in nodes(m, g):
                m.call("pool_free", n, COF_FILE, 0x66D, 0)
            m.w(g + 0x28, 0)
            return None
        self.run("FUN_0046f000", 0x46F000, build_u_only(self), clear_sixteen, 0, ret=False)

        def build_ov(img, rng):
            """Unit with an overlay list and a row in EDX."""
            return {"ecx": self.unit(img, rng, overlays=True), "edx": rng.choice((0, 1, 2, 3, 7, 9))}

        def has_overlay(m, a):
            """1 when an overlay node has +4 == row; fatal 0x1ea without a record."""
            g = gfx_of(m, a["ecx"])
            if not g:
                raise Halt(0x1EA)
            n = m.r(g + 0x2C)
            while n:
                if m.r(n + 4) == a["edx"]:
                    return 1
                n = m.r(n + 0xA4)
            return 0
        self.run("FUN_0046e100", 0x46E100, build_ov, has_overlay, 0)

    # ---- cache getters and the record's two values ----------------------------
    def cache_getters(self):
        """FUN_0046f980 / 990 / 9a0 / 9d0, FUN_0046f1c0 ... 0046f250."""
        rig = self.rig
        from random import Random
        gen = Random(7)
        state = {}

        def with_globals(rng):
            """Set the four cache globals to fresh values."""
            for name, addr in (("a", 0x7A8914), ("b", 0x7A8918), ("c", 0x7A891C), ("t", 0x7A80FC)):
                state[name] = gen.randrange(1 << 32)
                rig.m.w32(addr, state[name])
        for name, addr, key in (("FUN_0046f980", 0x46F980, "a"), ("FUN_0046f990", 0x46F990, "b"), ("FUN_0046f9a0", 0x46F9A0, "c")):
            def build(img, rng):
                """New global values."""
                with_globals(rng)
                return {}
            self.run(name, addr, build, lambda m, a, key=key: state[key], 0)

        def build_row(img, rng):
            """New globals and a row number."""
            with_globals(rng)
            return {"ecx": rng.choice((0, 1, 5, 0xFFFFFFFF, 0x12345678, rng.randrange(1 << 32)))}
        self.run("FUN_0046f9d0", 0x46F9D0, build_row,
                 lambda m, a: (state["t"] + (a["ecx"] * 0xA0 if not mut("row-size") else a["ecx"] * 0x80)) & M32, 0)

        for name, addr, off, setter, line in (("FUN_0046f1c0", 0x46F1C0, 0x34, True, 0xCE4), ("FUN_0046f1f0", 0x46F1F0, 0x34, False, 0xCF1),
                                              ("FUN_0046f220", 0x46F220, 0x38, True, 0xCFD), ("FUN_0046f250", 0x46F250, 0x38, False, 0xD0A)):
            def build_rec(img, rng):
                """The graphics record in ECX (sometimes null) and a value."""
                return {"ecx": 0 if rng.random() < 0.2 else rig.slot(GFX), "edx": special(rng)}

            def model(m, a, off=off, setter=setter, line=line):
                """Fatal on a null record; else set / get the dword."""
                if a["ecx"] == 0:
                    raise Halt(line)
                if setter:
                    m.w(a["ecx"] + off, a["edx"])
                    return None
                return m.r(a["ecx"] + off)
            self.run(name, addr, build_rec, model, 0, ret=not setter)

    # ---- GfxUtil.cpp ---------------------------------------------------------
    def gfxutil(self):
        """FUN_004db040, 004db050 / 090 / 0d0, 004db180, 004da720, 004da770, 004daa70, 004da8e0, 004dad50."""
        rig = self.rig

        def build_cof(img, rng):
            """A COF header: layer count and 9-byte layer records from +0x1c, ids drawn from a few values."""
            base = slot_off(8)
            count = rng.choice((0, 1, 3, 8, 16, 20))
            img[base] = count
            for i in range(20):
                for j in range(9):
                    img[base + 0x1C + 9 * i + j] = rng.randrange(256) if j else rng.choice((0, 1, 2, 5, 7, 15, 16, 0x80, 0xFF))
            return {"ecx": rng.choice((0, rig.slot(8), rig.slot(8))), "edx": rng.choice((0, 1, 2, 5, 7, 15, 16, 0x80, 0xFF, 0x103, 0xFFFFFF05, 0x7FFFFFFF, 0xFFFFFFFF))}

        def layer_byte(which):
            """Model of the three layer lookups: byte `which` of the first record with id == n; n >= 16 (signed) gives 0."""
            def model(m, a):
                """First matching record's byte, 0 when none, the header is null or n >= 16."""
                if a["ecx"] == 0 or s32(a["edx"]) >= 0x10:
                    return 0
                base = a["ecx"]
                for i in range(m.r(base, 1)):
                    rec = base + 0x1C + 9 * i
                    if m.r(rec, 1) == (a["edx"] & 0xFF if mut("byte-compare") else a["edx"]):
                        return m.r(rec + (2 if mut("byte-2") and which == 1 else which), 1)
                return 0
            return model
        for name, addr, which in (("FUN_004db090", 0x4DB090, 1), ("FUN_004db0d0", 0x4DB0D0, 2), ("FUN_004db050", 0x4DB050, 3)):
            self.run(name, addr, build_cof, layer_byte(which), 0)
        self.run("FUN_004db040", 0x4DB040, lambda img, rng: {"ecx": special(rng)}, lambda m, a: a["ecx"], 0)

        def build_type(img, rng):
            """A unit pointer whose type is any value."""
            put(img, slot_off(UNIT), rng.choice((0, 1, 2, 3, 4, 0xFFFFFFFF, 0x80000000, 5)))
            return {"ecx": rng.choice((0, rig.slot(UNIT)))}
        self.run("FUN_004db180", 0x4DB180, build_type,
                 lambda m, a: int(a["ecx"] != 0 and 0 <= s32(m.r(a["ecx"])) <= 2), 0)

        def build_token(img, rng):
            """A NUL-terminated string of 0..5 characters, bytes after it random."""
            text = rng.choice((b"", b"a", b"ab", b"abc", b"abcd", b"abcdef", b"\x80z", b"a\x00bcd"))
            put(img, slot_off(20), 0, 0)
            img[slot_off(20):slot_off(20) + len(text) + 1] = text + b"\0"
            return {"edx": rig.slot(20), "regs": {"eax": rig.slot(21)}}

        def token(m, a):
            """Up to three characters copied, a NUL ends the copy; space-pad to four bytes."""
            src, dst = a["edx"], a["regs"]["eax"]
            out = bytearray()
            for i in range(3):
                c = m.r(src + i, 1)
                if c == 0:
                    break
                out.append(c)
            out += b" " * (4 - len(out))
            if mut("pad-nul"):
                out = out[:3] + b"\0"
            for i, c in enumerate(out):
                m.w(dst + i, c, 1)
            return None
        self.run("FUN_004da720", 0x4DA720, build_token, token, 0, ret=False)

        def build_modebyte(img, rng):
            """A monster-ish record (type 1 or other) with a data pointer, and an index in DL."""
            put(img, slot_off(UNIT), rng.choice((0, 1, 1, 2)))
            put(img, slot_off(UNIT, 0x14), rng.choice((0, rig.slot(22))))
            return {"ecx": rng.choice((0, rig.slot(UNIT), rig.slot(UNIT))), "edx": rng.choice((0, 3, 15, 16, 17, 255, 0x1FF))}

        def modebyte(m, a):
            """Byte at data+4+n when the record is type 1, has data and n < 16; else 0."""
            u, n = a["ecx"], a["edx"] & 0xFF
            if u == 0 or m.r(u) != 1 or n >= 16 or m.r(u + 0x14) == 0:
                return 0
            return m.r(m.r(u + 0x14) + 4 + n, 1)
        self.run("FUN_004da770", 0x4DA770, build_modebyte, modebyte, 0, mask=0xFF)

        counter = {"n": 0}

        def build_fold(img, rng):
            """A dword draw type in the arena."""
            values = (0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF, 0x80000000)
            counter["n"] += 1
            put(img, slot_off(23), values[counter["n"] % len(values)])      # every value, in turn
            return {"edx": rig.slot(23)}

        def fold(m, a):
            """0, 1, 2, 4, 6 become 4; 3, 5 become 3; anything else is left."""
            v = m.r(a["edx"])
            table = {0: 4, 1: 4, 2: 4, 4: 4, 6: 4, 3: 3, 5: 3}
            if mut("fold-5") and v == 5:
                table[5] = 4
            if v in table:
                m.w(a["edx"], table[v])
            return None
        self.run("FUN_004daa70", 0x4DAA70, build_fold, fold, 0, ret=False)

        def fill_tables(img, rng):
            """The shared table header: MonStats2 (0x1a4-byte rows) and Overlay.txt (0x84-byte rows) pointers and counts."""
            put(img, HDR + 0xB64, rig.arena + ROWS2)
            put(img, HDR + 0xB6C, rng.choice((0, 1, 3, 5)))
            put(img, HDR + 0xBBC, rig.arena + OVROWS)
            put(img, HDR + 0xBC0, rng.choice((0, 1, 3, 5)))

        def build_row2(img, rng):
            """A unit with a class id and a request record."""
            fill_tables(img, rng)
            put(img, slot_off(UNIT, 4), rng.choice((0xFFFFFFFF, 0, 1, 2, 4, 5, 6, 0x80000000)))
            return {"regs": {"eax": rng.choice((0, rig.slot(UNIT), rig.slot(UNIT)))}, "stack": [rig.slot(24)]}

        def row2(m, a):
            """Request +0x2c = MonStats2 row + 0x138 when the unit's class id is in range; else untouched."""
            u = a["regs"]["eax"]
            if u == 0:
                return None
            cls = s32(m.r(u + 4))
            hdr = TABLE_PTR_VALUE
            if cls < 0 or cls >= s32(m.r(hdr + 0xB6C)):
                return None
            row = (cls * 0x1A4 + m.r(hdr + 0xB64)) & M32
            m.w(a["stack"][0] + 0x2C, row + (0x138 if not mut("row-0x13c") else 0x13C))
            return None
        global TABLE_PTR_VALUE
        TABLE_PTR_VALUE = rig.arena + HDR
        # the table pointer itself lives outside the arena, so the model reads the header through a fixed address
        self.run("FUN_004da8e0", 0x4DA8E0, build_row2, row2, 1, ret=False)

        def build_ov(img, rng):
            """A row number for Overlay.txt."""
            fill_tables(img, rng)
            put(img, OVROWS + 0x80, rng.randrange(256), 1)
            put(img, OVROWS + 0x84 + 0x80, rng.randrange(256), 1)
            return {"regs": {"eax": rng.choice((0, 1, 2, 3, 4, 5, 0xFFFFFFFF, 0x80000000))}}

        def ov80(m, a):
            """Overlay.txt +0x80 of the row; 1 when the row is out of range."""
            n = s32(a["regs"]["eax"])
            hdr = TABLE_PTR_VALUE
            if n < 0 or n >= s32(m.r(hdr + 0xBC0)):
                return 1
            return m.r(m.r(hdr + 0xBBC) + n * 0x84 + 0x80, 1)
        self.run("FUN_004dad50", 0x4DAD50, build_ov, ov80, 0, mask=0xFF)

    # ---- the unused list helpers --------------------------------------------
    def list_helpers(self):
        """FUN_004da920 (push), FUN_004da990 (find by +4 / +8), FUN_004da9c0 (free all)."""
        rig = self.rig
        head, count, total = slot_off(26), slot_off(26, 4), slot_off(26, 8)

        def build_push(img, rng):
            """An (empty or short) list head, a counter and a byte total."""
            self.fail_alloc = rng.random() < 0.2
            put(img, head, 0)
            put(img, count, rng.randrange(8))
            put(img, total, rng.randrange(0x100))
            if rng.random() < 0.5:
                put(img, slot_off(26, 0), rig.slot(27))
                put(img, slot_off(27, 0), 1)
            return {"ecx": rig.slot(26), "edx": special(rng), "stack": [special(rng), special(rng), rig.slot(26) + 4, rig.slot(26) + 8]}

        def push(m, a):
            """Allocate 0x10 (fatal 0x81 on null), fill (+0 EDX, +4 arg1, +8 arg2), link at the head, count += 1, bytes += 0x10."""
            blk = alloc_block(m, "pool_alloc", 0x10, 0x10, LIST_FILE, 0x80, 0)
            if not blk:
                raise Halt(0x81)
            m.w(a["stack"][3], m.r(a["stack"][3]) + 0x10)
            m.w(blk + 8, a["stack"][1])
            m.w(blk + 0, a["edx"])
            m.w(blk + 4, a["stack"][0])
            m.w(blk + 0xC, m.r(a["ecx"]))
            m.w(a["ecx"], blk)
            m.w(a["stack"][2], m.r(a["stack"][2]) + 1)
            return None
        self.run("FUN_004da920", 0x4DA920, build_push, push, 4, ret=False)
        self.fail_alloc = False

        def build_find(img, rng):
            """A three-node list with (+4, +8) from a small set."""
            for i, node in enumerate((27, 28, 29)):
                put(img, slot_off(node, 4), rng.choice((1, 2, 3)))
                put(img, slot_off(node, 8), rng.choice((1, 2, 3)))
                put(img, slot_off(node, 0xC), rig.slot(node + 1) if i < 2 else 0)
            return {"ecx": rng.choice((0, rig.slot(27))), "edx": rng.choice((1, 2, 3)), "stack": [rng.choice((1, 2, 3))]}

        def find(m, a):
            """First node with +4 == EDX and +8 == arg."""
            n = a["ecx"]
            while n:
                if m.r(n + 8) == a["stack"][0] and m.r(n + 4) == (a["edx"] ^ (1 if mut("find-swap") else 0)):
                    return n
                n = m.r(n + 0xC)
            return 0
        self.run("FUN_004da990", 0x4DA990, build_find, find, 1)

        def build_free(img, rng):
            """A list of 0..3 nodes and a byte total."""
            put(img, head, 0)
            nodes = [27, 28, 29][:rng.randrange(4)]
            if nodes:
                put(img, head, rig.slot(nodes[0]))
            for i, node in enumerate(nodes):
                put(img, slot_off(node, 0xC), rig.slot(nodes[i + 1]) if i + 1 < len(nodes) else 0)
            put(img, total, rng.randrange(0x400))
            return {"ecx": rig.slot(26), "edx": rig.slot(26) + 8}

        def free_all(m, a):
            """Free every node (line 0xa8), 0x10 less on the byte total each time; the head is zeroed, the count is not."""
            n = m.r(a["ecx"])
            while n:
                nxt = m.r(n + 0xC)
                m.call("pool_free", n, LIST_FILE, 0xA8, 0)
                m.w(a["edx"], m.r(a["edx"]) - (0x10 if not mut("free-bytes") else 0x8))
                n = nxt
            m.w(a["ecx"], 0)
            return None
        self.run("FUN_004da9c0", 0x4DA9C0, build_free, free_all, 0, ret=False)


def build_u_only(suite):
    """A builder for functions that take only a unit in ECX."""
    return lambda img, rng: {"ecx": suite.unit(img, rng)}


TABLE_PTR_VALUE = 0


def suite(rig):
    """Run every group; returns the number of cases."""
    s = Suite(rig)
    s.cof_list()
    s.layer_flags()
    s.cache_getters()
    s.gfxutil()
    s.list_helpers()
    return rig.cases


MUTATIONS = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
             for name in ("keep-12", "flag-zero", "count-20", "row-size", "byte-2", "pad-nul", "fold-5", "row-0x13c", "find-swap", "free-bytes", "byte-compare")}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "ENGINE Gfx.cpp / GfxUtil.cpp helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
