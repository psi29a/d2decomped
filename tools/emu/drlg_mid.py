# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify six Drlg.cpp functions (tile cache, neighbour-room free, knockback end point, LvlSub loader and sweep, A* box wrapper) in stock game.exe 1.14d.

    uv run python drlg_mid.py [--mutation-check]

Runs game.exe's own code for FUN_00642a30, FUN_00643060, FUN_0067a000,
FUN_006704e0, FUN_0066ff50 and FUN_0067ad00 against
hand-written models on random records (_dungeon_rig.py): return value, callee
log, arena, allocator blocks, fatal-error line and the bytes `ret n` pops.
Hooked, logged, not claimed: FUN_00604ae0, FUN_0066dde0, FUN_0066c040,
FUN_00642ae0, FUN_0066b4f0, FUN_00619730, FUN_0067aaa0, FUN_00679720,
FUN_00648ad0, FUN_006488c0, FUN_00648900, FUN_00665f40, FUN_0067ccc0,
FUN_0067c590, FUN_0066fcf0, FUN_0066fad0, FUN_0066fe00 and the allocator. `--mutation-check` breaks the
model eight ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, Halt, require, s32

MUT = set()
F_DRLG = 0x6E_B2E0


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        from unicorn.x86_const import UC_X86_REG_EDX
        self.rig = rig
        self.base = rig.arena
        self.count = 0
        self.tile = 0
        self.vis = {}
        self.spare = 0
        rig.callee("c604ae0", 0x60_4AE0, 6, (), nlog=4)
        rig.callee("c66dde0", 0x66_DDE0, 4, ("ecx", "edx"))
        rig.callee("c66c040", 0x66_C040, 0, ("ecx", "edx"))
        rig.callee("c642ae0", 0x64_2AE0, 0, ("ecx", "edx"))
        rig.callee("c66b4f0", 0x66_B4F0, 0, ("ecx", "edx"))

        def lookup(e):
            """FUN_00604ae0 stand-in: write the first tile at the buffer and answer the count (0 when the size is not 0x28)."""
            if e.arg(5) != 0x28:
                return 0
            e.mu.mem_write(e.arg(4), self.tile.to_bytes(4, "little"))
            return self.count
        rig.custom["c604ae0"] = lookup
        rig.custom["c66c040"] = lambda e: self.vis.get(e.mu.reg_read(UC_X86_REG_EDX), 0)
        rig.custom["c642ae0"] = lambda e: self.spare
        self.gx = self.gy = self.ray = 0
        rig.callee("c6488c0", 0x64_88C0, 1, ())
        rig.callee("c648900", 0x64_8900, 1, ())
        rig.callee("c679720", 0x67_9720, 2, ("eax",), nlog=1)
        rig.callee("c648ad0", 0x64_8AD0, 3, ())
        rig.custom["c6488c0"] = lambda e: self.gx
        rig.custom["c648900"] = lambda e: self.gy

        def ray(e):
            """FUN_00679720 stand-in: store the answer point at the pointer in its second argument."""
            e.mu.mem_write(e.arg(1), self.ray.to_bytes(4, "little"))
            return 0
        rig.custom["c679720"] = ray

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def cache(self):
        """FUN_00642a30: per-act tile cache entry."""
        rig, at = self.rig, self.at

        def build(img, rng):
            """Random act, a null pDrlg or out record now and then, a random hit count."""
            self.count = rng.choice((0, 1, 1, 5))
            self.tile = rng.getrandbits(32)
            img[0x480] = rng.choice((0, 1, 2, 3, 9, 0xFF))
            ecx = 0 if rng.random() < 0.1 else at(0)
            edx = 0 if rng.random() < 0.1 else at(0x600)
            return {"ecx": ecx, "edx": edx}

        def model(m, a):
            """Zero 0x30 bytes, look the act's tile up, fill the entry from the first hit."""
            if a["ecx"] == 0:
                raise Halt(0x437)
            if a["edx"] == 0:
                raise Halt(0x438)
            for k in range(0x30):
                m.w(a["edx"] + k, 0, 1)
            act = m.r(a["ecx"] + 0x480, 1)
            if act == 0:
                typ, sub = 0, 0
            elif act == 1:
                typ, sub = 0, 1 if not mut("act1") else 2
            elif act == 2:
                typ, sub = 0x1D, 0xC
            else:
                return None
            m.call("c604ae0", a["ecx"] + 0xC, 0, typ, sub)
            if self.count == 0:
                raise Halt(0x44C)
            m.call("c66dde0", 0, a["edx"], 0, 0, 0, self.tile)
            return None
        rig.run("FUN_00642a30", 0x642A30, build, model, 0, ret=False)

    def free(self):
        """FUN_00643060: free the room tiles and near arrays of the levels a level links to."""
        rig, at = self.rig, self.at
        # arena map: levels 0x000/0x200/0x400/0x600, act 0x800, vis arrays 0xd00, rooms 0x1000, near arrays 0x3000
        levels = [0x000, 0x200, 0x400, 0x600]
        act = 0x800

        def build(img, rng):
            """A level, an act with a level list, vis arrays and rooms with random status and flags."""
            put(img, act + 0x478, rng.getrandbits(32))
            ids = rng.sample(range(1, 9), 4)
            self.vis = {}
            nxt = 0x1000
            near = 0x3000

            def rooms(owner, quiet):
                """Chain a few rooms on a level; `quiet` keeps them all freeable."""
                nonlocal nxt, near
                prev = 0
                for _ in range(rng.randrange(0, 4)):
                    room = nxt
                    nxt += 0x60
                    img[room + 0x44] = rng.choice((4, 5, 9, 0xFF)) if quiet else rng.choice((0, 3, 4, 7, 0xFF))
                    flags = rng.getrandbits(32) & ~0x100000
                    if rng.random() < 0.2 and not quiet:
                        flags |= 0x100000
                    put(img, room + 0x28, flags)
                    put(img, room + 0x24, 0)
                    if rng.random() < 0.7:
                        put(img, room + 8, at(near))
                        near += 0x20
                    else:
                        put(img, room + 8, 0)
                    put(img, room + 0x2C, rng.randrange(1, 9))
                    if prev:
                        put(img, prev + 0x24, at(room))
                    else:
                        put(img, owner + 0x10, at(room))
                    prev = room
                if prev == 0:
                    put(img, owner + 0x10, 0)
            for n, lv in enumerate(levels):
                put(img, lv + 0x1B4, at(act))
                put(img, lv + 0x1D0, ids[n])
                put(img, lv + 0x1AC, at(levels[n + 1]) if n < 2 else 0)
                self.vis[ids[n]] = at(0xD00 + 0x20 * n)
                varr = []
                for _ in range(8):
                    r = rng.random()
                    varr.append(0 if r < 0.35 else ids[0] if r < 0.6 else rng.choice(ids[1:]) if r < 0.85 else rng.randrange(1, 9))
                for i, v in enumerate(varr):
                    put(img, 0xD00 + 0x20 * n + 4 * i, v)
            put(img, act + 0x47C, at(levels[0]) if rng.random() < 0.9 else 0)
            # level 0 holds rooms that are all freeable most of the time
            quiet = rng.random() < 0.7
            for n, lv in enumerate(levels):
                rooms(lv, quiet or n > 0 and rng.random() < 0.6)
            # the spare level 3 is what the hooked allocator-of-levels returns; its list head may be missing
            self.spare = at(levels[3])
            # the target is level 0; the level list has levels 0..2 (the vis ids can name level 3's id too)
            self.vis[ids[3]] = at(0xD00 + 0x20 * 3)
            return {"stack": [at(levels[0])]}

        def model(m, a):
            """Return 0 at the first room that is not ready; else free each linked level's flagged rooms."""
            lvl = a["stack"][0]
            actp = m.r(lvl + 0x1B4)
            pool = m.r(actp + 0x478)
            room = m.r(lvl + 0x10)
            while room:
                if m.r(room + 0x44, 1) < 4 or m.r(room + 0x28) & 0x100000:
                    return 0
                room = m.r(room + 0x24)
            vis = m.call("c66c040", actp, m.r(lvl + 0x1D0))
            for i in range(8):
                vid = m.r(vis + 4 * i)
                if vid == 0:
                    continue
                cur = m.r(m.r(lvl + 0x1B4) + 0x47C)
                while cur and m.r(cur + 0x1D0) != vid:
                    cur = m.r(cur + 0x1AC)
                if cur == 0:
                    cur = m.call("c642ae0", m.r(lvl + 0x1B4), vid)
                room = m.r(cur + 0x10)
                if room == 0:
                    continue
                arr = m.call("c66c040", m.r(cur + 0x1B4), m.r(cur + 0x1D0))
                me = m.r(lvl + 0x1D0)
                flags = 0
                for k in range(8):
                    if m.r(arr + 4 * k) == me:
                        flags |= 0x10 << k
                if mut("flag-bit"):
                    flags &= ~0x80
                while room:
                    rf = m.r(room + 0x28)
                    if flags & rf and (m.r(room + 0x44, 1) < (4 if not mut("status") else 3) or rf & 0x100000):
                        return 0
                    room = m.r(room + 0x24)
                room = m.r(cur + 0x10)
                while room:
                    if m.r(room + 0x28) & flags:
                        m.call("c66b4f0", pool, room)
                        near = m.r(room + 8)
                        if near:
                            m.free(pool, near, 0x16F, F_DRLG)
                            if not mut("keep-near"):
                                m.w(room + 8, 0)
                                m.w(room + 0x2C, 0)
                    room = m.r(room + 0x24)
            return 1
        rig.run("FUN_00643060", 0x643060, build, model, 1)


def knock_methods():
    """The knockback group, added to Suite below."""
    def knock(self):
        """FUN_0067a000: knockback end point of a path (dominant-axis scaled displacement, then a raycast)."""
        rig, at = self.rig, self.at
        # arena: args 0x000, path 0x100, target unit 0x400, its static path 0x500, its dynamic path 0x600

        def build(img, rng):
            """A path with a random distance and, mostly, a target unit of a random type."""
            src = rng.randrange(0, 40) | rng.randrange(0, 40) << 16
            put(img, 0, src)
            put(img, 4, rng.randrange(0, 40) | rng.randrange(0, 40) << 16)
            put(img, 0x30, at(0x100))
            img[0x100 + 0x90] = rng.choice((0, 1, 2, 7, 20, 255))
            self.gx, self.gy = rng.randrange(-5, 60) & M32, rng.randrange(-5, 60) & M32
            self.ray = rng.choice((src, rng.getrandbits(32), rng.randrange(0, 40) | rng.randrange(0, 40) << 16))
            if rng.random() < 0.15:
                put(img, 0x100 + 0x58, 0)
            else:
                put(img, 0x100 + 0x58, at(0x400))
                typ = rng.choice((0, 1, 2, 3, 4, 5, 6))
                put(img, 0x400, typ)
                dyn = rng.random() < 0.8 or typ == 2 or 3 < typ <= 5
                put(img, 0x400 + 0x2C, at(0x500) if dyn else 0)
                put(img, 0x500 + 0xC, rng.randrange(0, 40))
                put(img, 0x500 + 0x10, rng.randrange(0, 40))
                if rng.random() < 0.3:      # target on the source tile: the dominant axis is zero
                    self.gx, self.gy = src & 0xFFFF, src >> 16
                    put(img, 0x500 + 0xC, self.gx)
                    put(img, 0x500 + 0x10, self.gy)
            return {"ecx": at(0)}

        def model(m, a):
            """Displacement toward the target scaled to dist/2 + 1 on the dominant axis; raycast; moved or not."""
            arg = a["ecx"]
            path = m.r(arg + 0x30)
            knock = (m.r(path + 0x90, 1) >> 1) + 1
            unit = m.r(path + 0x58)
            src = m.r(arg)
            if unit == 0:
                tgt = m.r(arg + 4)
            else:
                typ = m.r(unit)
                ptr = m.r(unit + 0x2C)
                static = typ == 2 or 3 < typ <= 5
                if static:
                    tx, ty = m.r(ptr + 0xC), m.r(ptr + 0x10)
                else:
                    tx = ty = 0
                    if ptr:
                        tx = m.call("c6488c0", ptr)
                        ty = m.call("c648900", ptr)     # the unit is read again; same pointer
                dx = (src & 0xFFFF) - s32(tx)
                dy = (src >> 16) - s32(ty)
                dom = abs(dx) if abs(dx) > abs(dy) else abs(dy)
                if mut("knock-axis"):
                    dom = abs(dx)
                if dom:
                    dx = _tdiv(knock * dx, dom)
                    dy = _tdiv(knock * dy, dom)
                tgt = ((src & 0xFFFF) + dx) & 0xFFFF | ((((src >> 16) + dy) & 0xFFFF) << 16)
            m.w(path + 0x9C, tgt)
            m.call("c679720", path, src)
            tgt = self.ray
            m.call("c648ad0", path, tgt & 0xFFFF, tgt >> 16)
            if tgt == src:
                return 0
            m.w(path + 0x9C, tgt)
            m.w(path + 0x28, 1)
            return 1
        rig.run("FUN_0067a000", 0x67A000, build, model, 0)
    return knock


def subfile_methods():
    """The LvlSub file loader group, added to Suite below."""
    def subfile(self):
        """FUN_006704e0: load a LvlSub .ds1 once and build its layer grids."""
        rig, at = self.rig, self.at
        rig.callee("c665f40", 0x66_5F40, 1, ("ecx", "edx"))
        rig.callee("c67ccc0", 0x67_CCC0, 3, ("ecx", "edx"), nlog=1)
        rig.callee("c67c590", 0x67_C590, 1, ("ecx", "edx"))
        self.ctxs = []

        def load(e):
            """FUN_00665f40 stand-in: store the file record pointer at the out pointer in ECX."""
            from unicorn.x86_const import UC_X86_REG_ECX
            e.mu.mem_write(e.mu.reg_read(UC_X86_REG_ECX), (self.file).to_bytes(4, "little"))
            return 0

        def grid(e):
            """FUN_0067ccc0 stand-in: note the context rectangle and the width."""
            self.ctxs.append((e.r32(e.arg(1)), e.r32(e.arg(1) + 4), e.r32(e.arg(1) + 8), e.r32(e.arg(1) + 12), e.arg(2)))
            return 0
        rig.custom["c665f40"] = load
        rig.custom["c67ccc0"] = grid

        def build(img, rng):
            """A record that is loaded or not, over a file record of random size and layer counts."""
            self.file = at(0x400)
            self.ctxs.clear()
            put(img, 0x100 + 0x50, 0 if rng.random() < 0.7 else rng.getrandbits(32) | 1)
            put(img, 0x400 + 0xC, rng.randrange(0, 50))
            put(img, 0x400 + 0x10, rng.randrange(0, 50))
            put(img, 0x400 + 0x14, rng.choice((-1, 0, 1, 2, 3, 4)))
            put(img, 0x400 + 0x4C, 0 if rng.random() < 0.15 else rng.getrandbits(32))
            put(img, 0x400 + 0x18, rng.choice((0, 0, 1, 3)))
            put(img, 0x400 + 0x44, rng.choice((0, 0, at(0x900))))
            return {"ecx": rng.getrandbits(32), "edx": at(0x100)}

        def model(m, a):
            """Load the file; grid each wall layer twice, clear flags on layers 1.., then the floor and shadow grids."""
            rec = a["edx"]
            if m.r(rec + 0x50):
                return None
            m.call("c665f40", rec + 0x50, a["ecx"], rec + 4)
            m.w(rec + 0x50, self.file)          # what the hooked loader stores
            f = m.r(rec + 0x50)
            wd, ht = m.r(f + 0xC) + 1, m.r(f + 0x10) + 1
            ctx = (0, 0, wd, ht)
            if m.r(f + 0x4C) == 0:
                raise Halt(0x2B6)
            n = s32(m.r(f + 0x14))
            exp = []
            for k in range(max(n, 0)):
                m.call("c67ccc0", 0, rec + 0xA4 + 0x14 * k, m.r(f + 0x2C + 4 * k))
                exp.append(ctx + (wd,))
                m.call("c67ccc0", 0, rec + 0x54 + 0x14 * k, m.r(f + 0x1C + 4 * k))
                exp.append(ctx + (wd,))
            for k in range(1, n):
                m.call("c67c590", rec + 0xA4 + 0x14 * k - (0 if not mut("sub-grid") else 4), k << 0x12, 0)
            if m.r(f + 0x18):
                m.call("c67ccc0", 0, rec + 0xF4, m.r(f + 0x3C))
                exp.append(ctx + (wd,))
            if m.r(f + 0x44):
                m.call("c67ccc0", 0, rec + 0x108, m.r(f + 0x44))
                exp.append(ctx + (wd,))
            self.expect_ctx = exp
            return None

        def run_checked():
            """Run the cases, then check the contexts the grid hook saw against the model's."""
            rig.run("FUN_006704e0", 0x6704E0, build, wrapped, 0, ret=False)

        def wrapped(m, a):
            """Model plus the context check (the grid hook's saved rectangles are on the stack in the stock run)."""
            self.expect_ctx = []
            out = model(m, a)
            require(self.ctxs == self.expect_ctx, f"grid contexts {self.ctxs}, model {self.expect_ctx}")
            return out
        run_checked()
    return subfile


def checkall_methods():
    """The LvlSub substitution sweep group, added to Suite below."""
    def checkall(self):
        """FUN_0066ff50: sweep each substitution group over the room (act 1: overlap checks; act 2: seeded variants)."""
        rig, at = self.rig, self.at
        rig.callee("c66fcf0", 0x66_FCF0, 5, ())
        rig.callee("c66fad0", 0x66_FAD0, 7, ())
        rig.callee("c66fe00", 0x66_FE00, 4, ("esi",))
        rig.custom["c66fcf0"] = lambda e: rig.rng.getrandbits(1)
        rig.custom["c66fe00"] = lambda e: rig.rng.getrandbits(1)
        # arena: wall grids 0x000, room 0x100, level 0x200, act 0x400, sub record 0x600, file 0x900, groups 0xa00

        def build(img, rng):
            """A room of random size, a file with a few groups and an act; random seed and probabilities."""
            put(img, 0, at(0x100))
            put(img, 0x34, rng.randrange(0, 8))
            put(img, 0x100 + 0x58, at(0x200))
            put(img, 0x200 + 0x1B4, at(0x400))
            put(img, 0x400 + 0x478, rng.getrandbits(32))
            put(img, 0x100 + 0x3C, rng.randrange(1, 7))
            put(img, 0x100 + 0x40, rng.randrange(1, 7))
            put(img, 0x600 + 0x50, at(0x900))
            for k in range(8):
                put(img, 0x600 + 0x11C + 4 * k, rng.randrange(0, 101))
            put(img, 0x900, rng.choice((1, 2, 2, 3)))
            put(img, 0x900 + 0x4C, rng.choice((-1, 0, 1, 2, 3)))
            put(img, 0x900 + 0x50, at(0xA00))
            for g in range(3):
                put(img, 0xA00 + 0x18 * g + 8, rng.randrange(0, 5))
                put(img, 0xA00 + 0x18 * g + 0xC, rng.randrange(0, 5))
                put(img, 0xA00 + 0x18 * g + 0x14, rng.choice((-1, 0, 1, 2, 3, 4, 5, 8)))
            return {"stack": [at(0), at(0x600)]}

        def model(m, a):
            """Act 1: apply where the overlap check passes; act 2: apply a seeded variant where the tile types match."""
            wall, sub = a["stack"]
            room = m.r(wall)
            pool = m.r(m.r(m.r(room + 0x58) + 0x1B4) + 0x478)
            f = m.r(sub + 0x50)
            for g in range(max(s32(m.r(f + 0x4C)), 0)):
                ent = m.r(f + 0x50) + 0x18 * g
                mx = s32(m.r(room + 0x3C)) - s32(m.r(ent + 8)) + 1
                my = s32(m.r(room + 0x40)) - s32(m.r(ent + 0xC)) + 1
                if mx <= 0 or my <= 0:
                    continue
                act = m.r(f)
                if act == 1:
                    for row in range(1, my):
                        for col in range(1, mx):
                            if m.call("c66fcf0", col, row, wall, ent, sub):
                                m.call("c66fad0", pool, col, row, wall, ent, sub, 0)
                elif act == 2:
                    for row in range(my):
                        for col in range(mx):
                            if not m.call("c66fe00", ent, col, row, wall, sub):
                                continue
                            roll = self_step(m, room)
                            prob = s32(m.r(sub + 0x11C + 4 * m.r(wall + 0x34)))
                            if (prob >= roll % 100) if not mut("prob-dir") else (prob > roll % 100):
                                continue
                            n = s32(m.r(ent + 0x14))
                            if n <= 0:
                                var = 0
                            elif n & (n - 1) == 0:
                                var = (n - 1) & self_step(m, room)
                            else:
                                var = self_step(m, room) % n
                            m.call("c66fad0", pool, col, row, wall, ent, sub, ((m.r(ent + 8) + 1) * (var + 1)) & M32)
            return None
        rig.run("FUN_0066ff50", 0x66FF50, build, model, 2, ret=False)
    return checkall


def astar_methods():
    """The A* bounding-box wrapper group, added to Suite below."""
    def astar(self):
        """FUN_0067ad00: bounding box of the current and next room (padded by 10 on an unchanged axis), then the A* search."""
        rig, at = self.rig, self.at
        rig.callee("c619730", 0x61_9730, 2, (), nlog=1)
        rig.callee("c67aaa0", 0x67_AAA0, 1, ())
        self.gen = []
        self.boxes = []

        def room_coords(e):
            """FUN_00619730 stand-in: write a random room box (x, y, w, h, then four more dwords) and remember it."""
            vals = [rig.rng.randrange(0, 30), rig.rng.randrange(0, 30), rig.rng.randrange(1, 30), rig.rng.randrange(1, 30)]
            vals += [rig.rng.getrandbits(32) for _ in range(4)]
            self.gen.append(vals)
            for k, v in enumerate(vals):
                e.mu.mem_write(e.arg(1) + 4 * k, v.to_bytes(4, "little"))
            return 0

        def search(e):
            """FUN_0067aaa0 stand-in: note the box in ECX and answer a fixed number."""
            from unicorn.x86_const import UC_X86_REG_ECX
            ptr = e.mu.reg_read(UC_X86_REG_ECX)
            self.boxes.append([e.r32(ptr + 4 * k) for k in range(8)])
            return 0x1234
        rig.custom["c619730"] = room_coords
        rig.custom["c67aaa0"] = search

        def build(img, rng):
            """A path argument with a current room, a next room (same, none or different) and a doubling byte."""
            self.gen.clear()
            self.boxes.clear()
            img[0x14] = rng.choice((0, 1, 5, 127, 128, 200))
            put(img, 8, at(0x100))
            put(img, 0xC, rng.choice((0, at(0x100), at(0x200), at(0x200))))
            return {"ecx": at(0)}

        def model(m, a):
            """Double the byte; box the current room; widen to the next room if it differs; pad the axes left unchanged; search."""
            arg = a["ecx"]
            m.w(arg + 0x14, (m.r(arg + 0x14, 1) * 2) & 0xFF, 1)
            room, nxt = m.r(arg + 8), m.r(arg + 0xC)
            m.call("c619730", room)
            cur = list(self.gen[0])
            snap = list(cur)
            if nxt and room != nxt:
                m.call("c619730", nxt)
                nb = self.gen[1]
                if nb[0] < snap[0]:
                    cur[0], cur[2] = nb[0], snap[2] - nb[0] + snap[0]
                else:
                    cur[0], cur[2] = snap[0], nb[2] - snap[0] + nb[0]
                if nb[1] < snap[1]:
                    cur[1], cur[3] = nb[1], snap[3] - nb[1] + snap[1]
                else:
                    cur[1], cur[3] = snap[1], nb[3] - snap[1] + nb[1]
                pad_x = cur[2] == snap[2]
            else:
                pad_x = True
            if pad_x:
                cur[0] -= 10
                cur[2] += 20
            if cur[3] == snap[3] and not mut("pad-y"):
                cur[1] -= 10
                cur[3] += 20
            cur = [v & M32 for v in cur]
            res = m.call("c67aaa0", arg)
            require(self.boxes == [cur], f"search box {self.boxes}, model {[cur]}")
            return res
        rig.run("FUN_0067ad00", 0x67AD00, build, model, 0)
    return astar


def self_step(m, room):
    """Advance the room's seed (+0x14 low, +0x18 high) by low * 0x6ac690c5 + high and return the new low."""
    lo, hi = m.r(room + 0x14), m.r(room + 0x18)
    new = lo * 0x6AC690C5 + hi
    m.w(room + 0x14, new & M32)
    m.w(room + 0x18, (new >> 32) & M32)
    return new & M32


def _tdiv(a, b):
    """C division truncating toward zero (idiv)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


Suite.knock = knock_methods()
Suite.subfile = subfile_methods()
Suite.checkall = checkall_methods()
Suite.astar = astar_methods()


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 150
    s = Suite(rig)
    s.cache()
    s.free()
    s.knock()
    s.subfile()
    s.checkall()
    s.astar()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("act1", "flag-bit", "status", "keep-near", "knock-axis", "sub-grid", "prob-dir", "pad-y")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Drlg.cpp tile cache and neighbour free", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
