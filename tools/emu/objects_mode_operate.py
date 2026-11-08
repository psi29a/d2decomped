# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the OBJECTS\\ObjMode.cpp operate and event handlers in stock game.exe 1.14d.

    uv run python objects_mode_operate.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random records in a
random-filled arena (_objects_world.py). Compared after every call: the return
value (where the function has one), the ordered log of the hooked callees
(registers and stack arguments, and the bytes of the packets and requests they
are handed), the fatal-error line, the `ret n` pop count, EBX / ESI / EDI /
EBP and every byte of the arena.

Claimed (ECX = game unless a register is named):
  0x582610  ECX ctx {game, object, player}: OperateFn 17 (locked object needing an item)
  0x584420  ECX game, EDX player, stack (type word, id, out): the operate dispatch
            through the OperateFn table at 0x732d18; *out = 1; the fatal error is line 0xcf8
  0x5845d0  ECX game, one stack arg object id: a walk-blocked mode toggle
  0x584d00  ECX ctx {game, unit, player}: moves the player to a level's arrival spot
  0x5852e0  ECX game, EDX player id, stack (object id, item id, op word): op 2 / 3 packet 0x58
  0x585580  ECX game, EDX d, stack (object id, byte): the class 0x3b object's re-open
  0x585a80  ECX unit A, EDX unit B, stack (a, b): builds a 0x84 byte request for 0x55_8D90
  0x585ce0  ECX game, EDX unit: object event 9 (mode 1 / 2 / 3 steps)

Callees are hooked and logged, not claimed (written 0x12_3456 here): the unit
lookup 0x55_2F60, the mode setter 0x62_4690, the packet send 0x53_D8D0, the flag
helpers 0x62_22C0 / 0x62_2300, the room and position getters, and the others in
`Rig.__init__`. `--mutation-check` breaks the model on purpose, one defect at a
time, and requires each to fail. No MPQs are read.
"""
import argparse
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import _objects_world as ow
from _objects_world import M32, Mismatch, mix

LOCKED, DISPATCH, WALK, ARRIVE, ITEM_OP, REOPEN, REQUEST, EVENT9 = (
    0x582610, 0x584420, 0x5845D0, 0x584D00, 0x5852E0, 0x585580, 0x585A80, 0x585CE0)
OPFN_TABLE = 0x732D18
FILE_MODE = 0x6E1970
MUTATIONS = ("busy-ignored", "class-skip", "walk-mode", "packet-byte", "halt-line", "request-field", "event-delay", "level-class")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def dwords(current, address, count):
    """`count` dwords read from emulated memory."""
    data = current.read(address, 4 * count)
    ow.require(len(data) == 4 * count, "short read")
    return struct.unpack(f"<{count}I", data)


class Rig:
    """The world, the hooks and the case state for this oracle."""

    def __init__(self):
        """Hook the callees that are not claimed."""
        self.w = w = ow.World()
        self.cfg = {}
        c = self.cfg
        c.update(cur=0, busy=0, has_key=0, lookup={}, reach=0, pdata=0, cursor=0, opret=0, act=0, room=0, spot=0,
                 warp=0, isf=0, low_only=False, spot_room=0, irec=0, allowed=0, chance=0, orec=0, flag=0, found=0, made=0, walk=0, find=0, fire=0, drop=0)
        w.rec(0x55_4D00, "cur", 0, ("ecx", "edx"), ret=lambda entry: c["cur"])
        w.rec(0x55_31C0, "client", 1, ("ecx", "edx"))
        w.rec(0x55_4190, "stop", 1, ret=0)
        w.rec(0x62_4690, "mode", 2, ret=0)
        w.rec(0x53_5060, "busy", 0, ("ecx",), ret=lambda entry: c["busy"])
        w.rec(0x58_25B0, "has_key", 0, ("eax", "edi"), ret=lambda entry: c["has_key"])
        w.rec(0x55_3380, "x3380", 1, ("edx",), ret=0)
        w.rec(0x55_4120, "interact", 3, ret=0)
        w.m.hook(0x53_D8D0, self.send, 0)
        w.rec(0x55_2F60, "lookup", 1, ("ecx", "edx"), ret=lambda entry: c["lookup"].get(entry[2], 0))
        w.rec(0x62_1B30, "rec", 1, ret=lambda entry: c["irec"])
        w.m.hook(0x55_4100, self.reach_stub, 3)
        w.rec(0x62_21A0, "pdata", 1, ret=lambda entry: c["pdata"])
        w.rec(0x63_C1E0, "cursor", 1, ret=lambda entry: c["cursor"])
        w.rec(0x62_0BB0, "room", 1, ret=lambda entry: c["room"] if c["room"] else mix("room", entry[1]))
        w.rec(0x54_6FA0, "rng", 0, ("ecx", "edx"))
        c["opfn"] = w.m.alloc(16)
        w.m.hook(c["opfn"], self.opfn_stub, 0)
        w.m.hook(0x62_0870, self.pos_stub, 2)
        w.rec(0x62_0510, "p510", 1)
        w.rec(0x62_05A0, "p5a0", 1)
        w.rec(0x64_D800, "walk", 6, ret=lambda entry: c["walk"])
        w.rec(0x62_22C0, "fl", 1, ret=lambda entry: c["flag"])
        w.m.hook(0x62_2300, self.setfl_stub, 2)
        w.rec(0x64_27F0, "act", 1, ret=lambda entry: c["act"])
        w.m.hook(0x61_B060, self.dest_stub, 6)
        w.m.hook(0x64_E7B0, self.near_stub, 5)
        w.rec(0x55_4EA0, "warp", 5, ("ecx", "edx"), ret=lambda entry: c["warp"])
        w.rec(0x63_35F0, "irec2", 1, ret=lambda entry: c["irec"])
        w.rec(0x55_EEA0, "allowed", 1, ("ecx", "edx"), ret=lambda entry: c["allowed"])
        w.rec(0x58_5240, "chance", 2, ("edi",), ret=lambda entry: c["chance"])
        w.rec(0x64_0E90, "orec", 1, ret=lambda entry: c["orec"])
        w.rec(0x59_DD80, "special", 1, ("ecx", "edx"), ret=0)
        w.rec(0x54_17D0, "event", 4, ("ecx", "edx"), ret=0)
        w.rec(0x62_2350, "isflag", 2, ret=lambda entry: c["isf"])
        w.rec(0x55_3720, "find720", 0, ("ecx", "edx"), ret=lambda entry: c["found"])
        w.rec(0x61_A270, "x270", 3, ret=0)
        w.rec(0x55_5600, "x5600", 0, ("ecx", "edx"), ret=0)
        w.rec(0x61_A1B0, "x1b0", 1)
        w.rec(0x56_CF40, "make", 3, ("ecx", "edx"), ret=lambda entry: c["made"])
        w.m.hook(0x55_5DA0, self.find_stub, 3)
        w.rec(0x55_8200, "t8200", 0, ("ecx", "edx"))
        w.m.hook(0x55_8D90, self.fire_stub, 1)
        w.rec(0x58_5B90, "drop", 0, ("edx",), ret=lambda entry: c["drop"], peek=lambda e, a, v: dwords(e, e.mu.reg_read(UC_X86_REG_ECX), 5))
        w.rec(0x58_1680, "x1680", 1, ret=0)
        w.m.hook(0x68_1EF0, self.memset_stub, 0)

    def send(self, current):
        """0x53_d8d0 stand-in (ECX client, EDX packet 0x58): log the defined bytes (byte 6 only when byte 5 is 5)."""
        pkt = current.read(current.mu.reg_read(UC_X86_REG_EDX), 7)
        body = pkt if pkt[5] == 5 else pkt[:6]
        self.w.log.append(("send", current.mu.reg_read(UC_X86_REG_ECX), body))
        return 0

    def reach_stub(self, current):
        """0x55_4100 stand-in (player, two out pointers on the stack): log the player only."""
        self.w.log.append(("reach", current.arg(0)))
        return self.cfg["reach"]

    def opfn_stub(self, current):
        """A function in the OperateFn table: ECX = the 5-dword context."""
        self.w.log.append(("opfn",) + dwords(current, current.mu.reg_read(UC_X86_REG_ECX), 5))
        return self.cfg["opret"]

    def pos_stub(self, current):
        """0x62_0870 stand-in (unit, out pointer): stores two mixed dwords through the pointer."""
        unit, out = current.arg(0), current.arg(1)
        self.w.log.append(("pos", unit))
        current.w32(out, mix("posx", unit))
        current.w32(out + 4, mix("posy", unit))
        return 0

    def setfl_stub(self, current):
        """0x62_2300 stand-in (unit, flags dword): log both (the low byte only where the upper bytes are stack garbage)."""
        value = current.arg(1) & (0xFF if self.cfg["low_only"] else M32)
        self.w.log.append(("setfl", current.arg(0), value))
        return 0

    def dest_stub(self, current):
        """0x61_b060 stand-in (drlg, level, 0, &x, &y, size): log the by-value ones, store mixed x and y."""
        a = [current.arg(i) for i in range(6)]
        self.w.log.append(("dest", a[0], a[1], a[2], a[5]))
        current.w32(a[3], mix("destx", a[0], a[1]))
        current.w32(a[4], mix("desty", a[0], a[1]))
        return self.cfg["spot_room"]

    def near_stub(self, current):
        """0x64_e7b0 stand-in (room, &pt, size, mask, 0): log, then move the point to a mixed one."""
        a = [current.arg(i) for i in range(5)]
        x, y = dwords(current, a[1], 2)
        self.w.log.append(("near", a[0], x, y, a[2], a[3], a[4]))
        current.w32(a[1], mix("nx", x, y))
        current.w32(a[1] + 4, mix("ny", x, y))
        return self.cfg["spot"]

    def find_stub(self, current):
        """0x55_5da0 stand-in (ECX room, EDX &pos, stack (&out, 1, 1)): log the position, store two dwords."""
        room, pos = current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)
        x, y = dwords(current, pos, 2)
        self.w.log.append(("find", room, x, y, current.arg(1), current.arg(2)))
        current.w32(current.arg(0), mix("o0", room, x, y))
        current.w32(current.arg(0) + 4, mix("o1", room, x, y))
        return self.cfg["find"]

    def fire_stub(self, current):
        """0x55_8d90 stand-in (ECX unit, EDX request, stack 0): log the 0x84 request bytes."""
        edx = current.mu.reg_read(UC_X86_REG_EDX)
        self.w.log.append(("fire", current.mu.reg_read(UC_X86_REG_ECX), current.read(edx, 0x84), current.arg(0)))
        return self.cfg["fire"]

    def memset_stub(self, current):
        """The CRT memset (cdecl): fill and return the destination."""
        dst, value, count = current.arg(0), current.arg(1), current.arg(2)
        current.mu.mem_write(dst, bytes([value & 0xFF]) * count)
        return dst


def check_locked(rig, rng):
    """0x582610: open mode needs the item, a closed object takes it, a busy player is left alone."""
    a, c = rig.w.arena, rig.cfg
    for _ in range(300):
        a.reset(rng)
        game, player, obj, ctx = a.new(0x40), a.new(0x40), a.new(0x40), a.new(0x10)
        has_obj = rng.random() < 0.9
        a.w32(ctx, game)
        a.w32(ctx + 4, obj if has_obj else 0)
        a.w32(ctx + 8, player)
        a.w32(obj + 0x10, rng.choice((0, 0, 1, 2, 3, 3)))
        c["cur"] = rng.choice((obj, 0, rng.randrange(1 << 32)))
        c["busy"] = rng.choice((0, 1, 2))
        c["has_key"] = rng.choice((0, 5))

        def build(m, game=game, player=player, obj=obj, has_obj=has_obj):
            """Model: three arms: held-open, busy, key check."""
            client = mix("client", player, FILE_MODE, 0x6EC)
            log = [("cur", game, player), ("client", player, FILE_MODE, 0x6EC)]
            mode = m.r32(obj + 0x10) if has_obj else 0
            if has_obj and mode:
                if mode == 3 and c["cur"] == obj:
                    log += [("stop", player), ("mode", obj, 1)]
                return 1, log, None
            log.append(("busy", player))
            if c["busy"] == 1 and not mut("busy-ignored"):
                return 1, log, None
            log.append(("has_key", player, 0x14))
            if not c["has_key"]:
                log.append(("x3380", 0x13, player))
                return 1, log, None
            ident = m.r32(obj + 0xC) if has_obj else M32
            log += [("interact", player, 2, ident), ("mode", obj if has_obj else 0, 3),
                    ("send", client, b"\x58" + struct.pack("<I", ident) + b"\0")]
            return 1, log, None
        rig.w.go("locked", LOCKED, ecx=ctx, pops=0, build=build)


def check_dispatch(rig, rng):
    """0x584420: look the object up, gate on reach, then call OperateFn[rec +0x1b3] with a 5-dword context."""
    a, c, m_ = rig.w.arena, rig.cfg, rig.w.m
    for _ in range(600):
        a.reset(rng)
        game, player, unit, out, rec, pdata = a.new(0x40), a.new(0x80), a.new(0x80), a.new(8), a.new(0x1C0), a.new(0x80)
        has_player = rng.random() < 0.85
        a.w32(player, rng.choice((0, 0, 0, 1)))
        a.w32(unit + 4, rng.choice((0x79, 0x7A, 0x16, 0x10B, 5, 0x98)))
        has_unit = rng.random() < 0.9
        c["lookup"] = {0: 0, 2: 0}
        c["irec"] = rec
        index = rng.choice((0, 3, 0x64, 0x65, 0xFF, 7))
        a.w8(rec + 0x1B3, index)
        c["reach"] = rng.choice((0, 0, 0, 1))
        c["pdata"] = pdata
        a.w32(pdata + 0x4C, rng.choice((0, 0, 9)))
        c["cursor"] = rng.choice((0, 0, 4))
        c["opret"] = rng.randrange(1 << 32)
        has_fn = rng.random() < 0.8
        if index < 0x65:
            m_.w32(OPFN_TABLE + 4 * index, c["opfn"] if has_fn else 0)
        kind = rng.randrange(1 << 32)
        pl = player if has_player else 0
        u = unit if has_unit else 0
        c["lookup"] = {0: u, 2: u, 4: u}
        type_arg = (kind & 0xFFFF0000) | rng.choice((0, 2, 4))
        ident = rng.randrange(1 << 32)

        def build(m, game=game, u=u, pl=pl, out=out, rec=rec, index=index, ident=ident, type_arg=type_arg, has_fn=has_fn):
            """Model: reach gates for a player unit (type 0); then the table entry."""
            m.w32(out, 1)
            log = [("lookup", game, type_arg & 0xFFFF, ident)]
            cls = m.r32(u + 4) if u else M32
            log.append(("rec", u))
            if pl and m.r32(pl) == 0:
                log.append(("reach", pl))
                if c["reach"]:
                    return 0, log, None
                log.append(("pdata", pl))
                if m.r32(c["pdata"] + 0x4C):
                    return 0, log, None
                log.append(("cursor", m.r32(pl + 0x60)))
                if c["cursor"] and cls != 0x10B:
                    return 0, log, None
            al = m.r8(rec + 0x1B3)
            if al >= 0x65:
                return None, log, 0xCF9 if mut("halt-line") else 0xCF8
            skip = (0x79, 0x7A) if mut("class-skip") else (0x79, 0x7A, 0x16)
            if cls in skip or not has_fn:
                return 0, log, None
            room = mix("room", u)
            log += [("room", u), ("rng", game, room), ("opfn", game, u, pl, mix("rng", game, room), cls)]
            return c["opret"], log, None
        rig.w.go("dispatch", DISPATCH, type_arg, ident, out, ecx=game, edx=pl, pops=3, build=build)


def check_walk(rig, rng):
    """0x5845d0: look the object up (fatal when missing), test its spot, toggle between modes 2 and 5."""
    a, c = rig.w.arena, rig.cfg
    for _ in range(300):
        a.reset(rng)
        game, obj = a.new(0x40), a.new(0x80)
        a.w32(obj + 0x10, rng.choice((0, 1, 2, 3, 5, 6)))
        c["walk"] = rng.choice((0, 1, 0x10000, 0xFFFF0000, 0x8000))
        has = rng.random() < 0.95
        c["lookup"] = {2: obj if has else 0}
        ident = rng.randrange(1 << 32)

        def build(m, game=game, obj=obj, has=has, ident=ident):
            """Model: modes 0 / 2 / 5 test the spot; only 2 and 5 switch."""
            log = [("lookup", game, 2, ident)]
            if not has:
                return None, log, 0xD37
            room = mix("room", obj)
            B, C = mix("p510", obj), mix("p5a0", obj)
            log += [("room", obj), ("pos", obj), ("p510", obj), ("p5a0", obj)]
            mode = m.r32(obj + 0x10)
            if mode in (0, 2, 5):
                log.append(("walk", room, mix("posx", obj), mix("posy", obj), B, C, 0x8180))
            if mode in (2, 5):
                free = (c["walk"] & 0xFFFF) != 0
                target = 5 if free else 2
                if mode != target and not (mut("walk-mode") and mode == 5):
                    log.append(("mode", obj, target))
            return None, log, None
        rig.w.go("walk", WALK, ident, ecx=game, pops=1, build=build)


def check_arrive(rig, rng):
    """0x584d00: arrival spot of the object's level for the player; three fatal errors."""
    a, c = rig.w.arena, rig.cfg
    for _ in range(500):
        a.reset(rng)
        game, unit, player, ctx, data = a.new(0x100), a.new(0x80), a.new(0x80), a.new(0x10), a.new(0x20)
        a.w32(ctx, game)
        a.w32(ctx + 4, unit if rng.random() < 0.9 else 0)
        pl = rng.choice((player, player, player, 0))
        a.w32(ctx + 8, pl)
        a.w32(player, rng.choice((0, 0, 0, 2)))
        a.w32(unit + 4, rng.choice((0x64, 0x179, 0x179, 5)))
        a.w32(unit + 0x14, data)
        a.w8(data + 4, rng.randrange(256))
        c["flag"] = rng.randrange(1 << 32)
        c["low_only"] = True
        c["act"] = rng.choice((0, 1, 4, 5, 6, 0x100))
        c["spot_room"] = rng.choice((0, 0x77777))
        c["spot"] = rng.choice((0, 0x55555))
        c["warp"] = rng.choice((0, 1))
        for i in range(5):
            a.w32(game + 0xBC + 4 * i, rng.randrange(1 << 32))
        u = a.r32(ctx + 4)

        def build(m, ctx=ctx, game=game, u=u):
            """Model: level from the class, the act's drlg, nearest free spot, move."""
            p = m.r32(ctx + 8)
            if p == 0 or m.r32(p) != 0:
                return None, [], 0xFB3
            log = [("fl", u), ("setfl", u, (c["flag"] | 5) & 0xFF)]
            level = 0
            if u:
                cls = m.r32(u + 4)
                if cls == 0x64:
                    level = 0x49
                elif cls == 0x179:
                    level = m.r8(m.r32(u + 0x14) + 4)
                if mut("level-class") and cls == 0x179:
                    level = 0x49
            log.append(("act", level))
            bl = c["act"] & 0xFF
            if bl >= 5:
                return None, log, 0xFCB
            drlg = m.r32(game + bl * 4 + 0xBC)
            size = mix("p510", p)
            log += [("p510", p), ("dest", drlg, level, 0, size)]
            if not c["spot_room"]:
                return None, log, 0xFCD
            x, y = mix("destx", drlg, level), mix("desty", drlg, level)
            log += [("p510", p), ("near", c["spot_room"], x, y, size, 0x1C09, 0)]
            if c["spot"]:
                log.append(("warp", game, p, c["spot"], mix("nx", x, y), mix("ny", x, y), 0, 0))
                if not c["warp"]:
                    return None, log, 0xFD9
            return 0, log, None
        rig.w.go("arrive", ARRIVE, ecx=ctx, pops=0, build=build)


def check_item_op(rig, rng):
    """0x5852e0: object + player + item lookups, then op 2 (take back) or op 3 (put the item in)."""
    a, c = rig.w.arena, rig.cfg
    for _ in range(800):
        a.reset(rng)
        game, player, obj, item, irec, orec = a.new(0x100), a.new(0x40), a.new(0x80), a.new(0x40), a.new(0x200), a.new(0x100)
        have = (rng.random() < 0.9, rng.random() < 0.9, rng.random() < 0.9)
        o, p, i = obj if have[0] else 0, player if have[1] else 0, item if have[2] else 0
        c["lookup"] = {2: o, 0: p, 4: i}
        a.w32(obj + 4, rng.choice((0x98, 0x98, 0x50, 0x50, 0x5A)))
        a.w32(item + 4, rng.randrange(1 << 32))
        a.w32(obj + 0xC, rng.randrange(1 << 32))
        c["irec"] = irec
        a.w32(irec + 0x80, rng.choice((0x20747368, 0x20747368, 0x20747369)))
        a.w8(irec + 0x122, rng.randrange(256))
        c["allowed"] = rng.choice((0, 3))
        c["chance"] = rng.choice((0, 1, 0x101, 2))
        c["orec"] = orec
        a.w32(orec + 0xDC, rng.randrange(1 << 32))
        op = rng.choice((2, 3, 3, 3, 4, 0)) | rng.choice((0, 0, 0x10000))
        obj_id, item_id, who = rng.randrange(1 << 32), rng.randrange(1 << 32), rng.randrange(1 << 32)

        def build(m, game=game, o=o, p=p, i=i, obj_id=obj_id, item_id=item_id, who=who, op=op, irec=irec, orec=orec):
            """Model of the packet 0x58 arms."""
            log = [("lookup", game, 2, obj_id), ("lookup", game, 0, who), ("lookup", game, 4, item_id)]
            if p == 0:
                return (1, log, None) if o else (None, log, 0x1109)
            if o == 0:
                return 1, log, None
            client = mix("client", p, FILE_MODE, 0x1135)
            log.append(("client", p, FILE_MODE, 0x1135))
            head = b"\x58" + struct.pack("<I", obj_id)
            code = op & 0xFFFF
            if code == 2:
                log += [("send", client, head + b"\x01"), ("mode", o, 0), ("stop", p)]
                return 1, log, None
            if code != 3:
                return 1, log, None
            ident = m.r32(i + 4) if i else M32
            log.append(("irec2", ident))
            cls = m.r32(o + 4)
            if cls == 0x98:
                ok = m.r32(irec + 0x80) == 0x20747368
            else:
                log.append(("allowed", game, p, i))
                ok = c["allowed"] != 0
            if not ok:
                log.append(("send", client, head + b"\x04"))
                return 1, log, None
            log.append(("stop", p))
            if cls == 0x98:
                flag = 1
            else:
                log.append(("chance", p, game, m.r8(irec + 0x122)))
                flag = c["chance"] & 0xFF
            log.append(("send", client, head + (b"\x05" if not mut("packet-byte") else b"\x06") + bytes([flag])))
            if flag != 1:
                log.append(("mode", o, 0))
                return 1, log, None
            log += [("mode", o, 1), ("orec", cls)]
            if cls == 0x98:
                log += [("mode", o, 2), ("special", game, p, o)]
            else:
                when = ((m.r32(orec + 0xDC) >> 8) + m.r32(game + 0xA8) + (0 if mut("event-delay") else 1)) & M32
                log.append(("event", game, o, 1, when, 0, 0))
            return 1, log, None
        rig.w.go("item-op", ITEM_OP, obj_id, item_id, op, ecx=game, edx=who, pops=3, build=build)


def check_reopen(rig, rng):
    """0x585580: re-open a class 0x3b object: store the byte, flag it, make the replacement."""
    a, c = rig.w.arena, rig.cfg
    for _ in range(500):
        a.reset(rng)
        game, esi, edi, data = a.new(0x100), a.new(0x80), a.new(0x80), a.new(0x20)
        g = game if rng.random() < 0.95 else 0
        a.w32(esi + 4, rng.choice((0x3B, 0x3B, 0x3B, 0x3C)))
        a.w32(esi + 0x14, data)
        a.w32(edi + 4, rng.choice((0x3B, 0x3B, 0x3B, 0x3C)))
        a.w32(edi + 0xC, rng.randrange(1 << 32))
        has = rng.random() < 0.9
        c["lookup"] = {2: esi if has else 0}
        c["isf"] = rng.choice((0, 0, 1))
        c["low_only"] = False
        c["flag"] = rng.randrange(1 << 32)
        c["found"] = rng.choice((edi, edi, 0))
        c["made"] = rng.choice((0x1234, 0x1234, 0))
        ident, val, d = rng.randrange(1 << 32), rng.randrange(1 << 32), rng.randrange(1 << 32)

        def build(m, g=g, esi=esi, edi=edi, has=has, ident=ident, val=val, d=d, data=data):
            """Model: guards, byte store, flag |1, replacement, flag |3 on it."""
            if g == 0:
                return None, [], 0x11DD
            log = [("lookup", g, 2, ident)]
            if not has or m.r32(esi + 4) != 0x3B:
                return None, log, None
            log.append(("isflag", esi, 1))
            if c["isf"]:
                return None, log, None
            log.append(("find720", g, esi))
            if c["found"] == 0:
                return None, log, None
            if m.r32(edi + 4) != 0x3B:
                return None, log, 0x11EA
            m.w8(data + 4, val & 0xFF)
            log.append(("fl", esi))
            arg = (ident & 0xFFFFFF00) | ((c["flag"] | 1) & 0xFF)
            log += [("setfl", esi, arg), ("mode", esi, 3), ("room", edi)]
            room = mix("room", edi)
            log += [("x270", room, 2, m.r32(edi + 0xC)), ("x5600", g, edi), ("room", esi)]
            r1b0 = mix("x1b0", mix("room", esi))
            log += [("x1b0", mix("room", esi)), ("make", g, d, esi, val, r1b0)]
            if c["made"] == 0:
                return None, log, 0x11FB
            log += [("fl", c["made"]), ("setfl", c["made"], (arg & 0xFFFFFF00) | ((c["flag"] | 3) & 0xFF))]
            return None, log, None
        rig.w.go("reopen", REOPEN, ident, val, ecx=g, edx=d, pops=2, build=build)


def check_request(rig, rng):
    """0x585a80: negative a returns 0; a missing spot returns 0; else a 0x84 byte request for 0x55_8d90."""
    a, c = rig.w.arena, rig.cfg
    for _ in range(300):
        a.reset(rng)
        edi, esi = a.new(0x100), a.new(0x80)
        a.w16(edi + 0x78, rng.randrange(1 << 16))
        c["find"] = rng.choice((0, 0x4321, 0xFFFFFFFF))
        c["fire"] = rng.randrange(1 << 32)
        arg0 = rng.choice((0, 5, 0x7FFFFFFF, rng.randrange(1 << 32)))
        arg1 = rng.randrange(1 << 32)

        def build(m, edi=edi, esi=esi, arg0=arg0, arg1=arg1):
            """Model: the request fields at their offsets, the rest zero."""
            if arg0 & 0x80000000:
                return 0, [], None
            x, y = mix("posx", esi), mix("posy", esi)
            room = mix("room", esi)
            log = [("pos", esi), ("room", esi), ("find", room, x, y, 1, 1)]
            if c["find"] == 0:
                return 0, log, None
            body = bytearray(0x84)
            fields = {0: esi, 8: edi, 0xC: mix("t8200", esi, 0), 0x14: arg0, 0x18: 3, 0x1C: mix("o0", room, x, y),
                      0x20: mix("o1", room, x, y), 0x24: c["find"], 0x30: arg1}
            for offset, value in fields.items():
                struct.pack_into("<I", body, offset, value)
            struct.pack_into("<H", body, 0x28, 1)
            struct.pack_into("<H", body, 0x2A, m.r16(edi + 0x78))
            if mut("request-field"):
                struct.pack_into("<I", body, 0x30, arg0)
            log += [("t8200", esi, 0), ("fire", edi, bytes(body), 0)]
            return c["fire"], log, None
        rig.w.go("request", REQUEST, arg0, arg1, ecx=edi, edx=esi, pops=2, build=build)


def check_event9(rig, rng):
    """0x585ce0: object event 9: mode 1 re-queues, mode 2 counts and drops, mode 3 goes to mode 4."""
    a, c = rig.w.arena, rig.cfg
    for _ in range(500):
        a.reset(rng)
        game, unit, data, orec = a.new(0x100), a.new(0x80), a.new(0x20), a.new(0x100)
        u = unit if rng.random() < 0.95 else 0
        a.w32(unit + 0x10, rng.choice((0, 1, 2, 2, 2, 3, 4)))
        a.w32(unit + 4, rng.randrange(1 << 32))
        a.w32(unit + 0x14, data)
        a.w8(data + 4, rng.choice((0, 1, 2, 255)))
        a.w32(orec + 0xE4, rng.randrange(1 << 32))
        c["orec"] = orec
        c["drop"] = rng.choice((0, 7))

        def build(m, game=game, u=u, data=data, orec=orec):
            """Model: the 3 mode arms; mode 2's counter byte decides between drop and the plain path."""
            mode = m.r32(u + 0x10) if u else 0
            cls = m.r32(u + 4) if u else M32
            log = [("orec", cls)]
            frame = m.r32(game + 0xA8)
            if mode == 1:
                log.append(("event", game, u, 9, (frame + 0xA) & M32, 0, 0))
            elif mode == 2:
                count = (m.r8(m.r32(u + 0x14) + 4) + 1) & 0xFF
                m.w8(m.r32(u + 0x14) + 4, count)
                room = mix("room", u)
                log.append(("room", u))
                log.append(("rng", game, room))
                if count > 1:
                    log.append(("drop", 0, game, u, 0, mix("rng", game, room), cls))
                    if c["drop"] == 0:
                        return None, log, None
                    log.append(("mode", u, 3))
                    when = ((m.r32(orec + 0xE4) >> 8) + frame + (2 if mut("event-delay") else 1)) & M32
                    log.append(("event", game, u, 9, when, 0, 0))
                else:
                    log += [("x1680", game), ("event", game, u, 9, (frame + 0x19) & M32, 0, 0)]
            elif mode == 3:
                log.append(("mode", u, 4))
            return None, log, None
        rig.w.go("event9", EVENT9, ecx=game, edx=u, pops=0, build=build)


def run_all(seed, rounds):
    """Build the rig and run every group; returns the case count."""
    rng = random.Random(seed)
    rig = Rig()
    for _ in range(rounds):
        check_locked(rig, rng)
        check_dispatch(rig, rng)
        check_walk(rig, rng)
        check_arrive(rig, rng)
        check_item_op(rig, rng)
        check_reopen(rig, rng)
        check_request(rig, rng)
        check_event9(rig, rng)
    return rig.w.cases


def main():
    """Run the oracle, or with --mutation-check require every model defect to be caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="break the model on purpose and require failures")
    options = parser.parse_args()
    if options.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all(1, 1)
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"mutation {name} was not caught")
        print(f"ok: {len(MUTATIONS)} model mutations caught")
        return
    print(f"ok: {run_all(1, 2)} object operate and event handler cases")


if __name__ == "__main__":
    main()
