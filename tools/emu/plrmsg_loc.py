# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's location-skill and walk packet handlers of PlrMsg.cpp (server side).

Handlers are called as in plrmsg_click.py: game in ECX, unit in EDX, then packet and
size on the stack (`ret 8`); statuses 0 ok, 2 bad data, 3 bad length / no skill.

Covers FUN_005496f0 (the shared location check: packet in EAX, unit in ESI, stack game,
size, &x out, &y out; zeroes both word outs, size must be 5 (3), player data from
0x62_21A0 must exist (2), range check 0x54_8EF0 with range 0x32 in EBX on the packet's
x / y words; on a failure that is more than 0x19 frames (signed) after the player
data's stamp +0x168 the client gets a 0x15 reassign packet (position from 0x45_AE20 /
0x45_ADF0, the unit id or -1 for no unit) and the range status passes through; on 0 the
stamp is set to the game frame and the outs get x and y), the walk / run handlers
0x01 FUN_005497e0 and 0x03 FUN_005498d0 (move to the location with mode 2 / 3 and no
skill, answer 0), the location skill handlers 0x05 FUN_00549d00 and 0x0c FUN_00549fc0
(left / right skill, none gives 3, pierce stat 0x148 raised by one, then the cast with a
second skill fetch and answer 0 whatever the cast says), the held handlers 0x08
FUN_00549e80 and 0x0f FUN_0054a140 (location check, skill test, then the plain handler
on the same packet and size; answer 0), FUN_00549ad0 (the cast: no unit, death mode 0 or
dead mode 0x11 give 2; a dual-wield swing replaces the skill; a usability result of 1, 2
or 4 with the skills.txt flag bit set at +7 retries with the basic attack; no skill mode
gives 2; usability 0 moves, 1 sends the inventory-full message; all other 2), FUN_00549a60
(the 0x5a message packet, type 0x0e, colour 1) and the keepalive 0x0b FUN_00549fa0 (size 1
else 3).

Hooked (their work is not claimed): 0x62_21A0, 0x54_8EF0, 0x55_31C0, 0x45_AE20, 0x45_ADF0,
0x53_BC10, 0x58_09D0, 0x62_0190, 0x62_01D0, 0x62_53B0, 0x62_7260, 0x54_9AD0 (where a
caller is tested), 0x62_35A0, 0x54_9960, 0x64_7960, 0x64_4140, 0x64_39B0, 0x64_3860,
0x53_C850, 0x68_1A48.

Usage: D2_MPQ_DIR=<MPQ folder> uv run python plrmsg_loc.py [--mutation-check]
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import _monster_world as mw
from _monster_world import require

OK, BAD_DATA, BAD_PARAMS = 0, 2, 3
MASK_BYTE = 0x6CE278
COOKIE = 0x749DE0
FILE_NAME = 0x6E1050
PIERCE = 0x148
MUTATIONS = (
    "loc-size-6-ok", "loc-no-player-status-3", "loc-range-0x31", "loc-swaps-xy-args", "loc-reassign-threshold-0x1a",
    "loc-reassign-always", "loc-reassign-null-client", "loc-null-unit-guid-0", "loc-stamp-on-fail", "loc-no-stamp",
    "loc-fail-status-1", "loc-outs-swapped", "loc-reassign-packet-0x14", "loc-reassign-flag-0", "loc-unsigned-diff",
    "loc-reassign-swaps-xy", "walk-mode-3", "run-mode-2", "move-skill-nonzero", "left-uses-right-skill",
    "right-uses-left-skill", "no-skill-status-2", "pierce-not-raised", "pierce-raised-by-2", "cast-first-skill",
    "cast-swaps-xy", "cast-status-passes", "ex-left-delegates-0c", "ex-right-delegates-05", "ex-no-skill-check",
    "ex-status-passes", "ex-uses-right-getter", "keepalive-size-0-ok", "keepalive-status-2", "cast-null-unit-ok",
    "cast-dead-0x12", "cast-death-ok", "cast-no-dual", "cast-dual-always", "cast-retry-3", "cast-retry-0",
    "cast-retry-negative", "cast-mask-0x10", "cast-retry-skill-1", "cast-no-recheck", "cast-moves-without-mode",
    "cast-moves-on-1", "cast-no-invfull", "cast-invfull-on-2", "cast-move-swaps-xy", "cast-move-flag-1",
    "invmsg-type-0x0d", "invmsg-colour-0", "invmsg-line-0x73b", "invmsg-no-client-skips")
mutation = None


def mut(name):
    """True while the model carries the deliberate defect `name` (--mutation-check)."""
    return mutation == name


def s32(value):
    """The signed value of a 32-bit pattern."""
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


class Rig:
    """One emulated game.exe with the callees hooked and the fake game / unit / player data / packet blocks."""

    def __init__(self, rng, hooks):
        """Build the machine, the fake blocks and the hooks named in `hooks` (range, skills, move, delegate, cast, msg)."""
        self.rng = rng
        self.w = mw.World()
        self.m = self.w.m
        self.game, self.unit, self.pd = self.m.alloc(0x200), self.m.alloc(0x100), self.m.alloc(0x200)
        self.packet, self.txt = self.m.alloc(0x40), self.m.alloc(0x40)
        self.pd_ptr, self.range_status, self.client = self.pd, 0, 0
        self.xpos, self.ypos, self.skills, self.stat, self.cast_ret = 0, 0, [], 0, 0
        self.dual, self.canuse, self.attack, self.mode, self.txt_ptr, self.dual_skill = 0, [], 0, 0, 0, 0
        self.sent = None
        w = self.w
        if "range" in hooks:
            w.logger(0x62_21A0, 1, "pd", ret=lambda _c: self.pd_ptr, regs=())
            w.logger(0x54_8EF0, 2, "range", ret=lambda _c: self.range_status, regs=("eax", "ebx"))
            w.logger(0x55_31C0, 1, "client", ret=lambda _c: self.client, regs=("ecx", "edx"))
            w.logger(0x45_AE20, 0, "ypos", ret=lambda _c: self.ypos, regs=("ecx",))
            w.logger(0x45_ADF0, 0, "xpos", ret=lambda _c: self.xpos, regs=("ecx",))
            w.logger(0x53_BC10, 5, "reassign", regs=("ecx", "dl"))
        if "skills" in hooks:
            w.logger(0x62_0190, 1, "left", ret=lambda _c: self.skills.pop(0), regs=())
            w.logger(0x62_01D0, 1, "right", ret=lambda _c: self.skills.pop(0), regs=())
            w.logger(0x62_53B0, 3, "get_stat", ret=lambda _c: self.stat, regs=())
            w.logger(0x62_7260, 4, "set_stat", regs=())
            w.logger(0x54_9AD0, 3, "cast", ret=lambda _c: self.cast_ret, regs=("ecx", "eax"))
        if "move" in hooks:
            w.logger(0x58_09D0, 5, "move", regs=("ecx", "edx"))
        if "delegate" in hooks:
            w.logger(0x54_9D00, 2, "to_05", ret=lambda _c: self.cast_ret, regs=("ecx", "edx"))
            w.logger(0x54_9FC0, 2, "to_0c", ret=lambda _c: self.cast_ret, regs=("ecx", "edx"))
        if "cast" in hooks:
            w.logger(0x62_35A0, 1, "dual", ret=lambda _c: self.dual, regs=())
            w.logger(0x54_9960, 1, "swing", ret=lambda _c: self.dual_skill, regs=("ebx",))
            w.logger(0x64_7960, 2, "canuse", ret=lambda _c: self.canuse.pop(0), regs=())
            w.logger(0x64_4140, 0, "txt", ret=lambda _c: self.txt_ptr, regs=("ecx",))
            w.logger(0x64_39B0, 1, "attack", ret=lambda _c: self.attack, regs=("ecx", "edx"))
            w.logger(0x64_3860, 2, "mode", ret=lambda _c: self.mode, regs=())
            w.logger(0x54_9A60, 0, "invfull", regs=("ecx",))
        if "msg" in hooks:
            w.logger(0x55_31C0, 1, "client", ret=lambda _c: self.client, regs=("ecx", "edx"))
            w.logger(0x68_1A48, 0, "cookie", regs=("ecx",))
            self.m.hook(0x53_C850, self.send, 0)

    def send(self, current):
        """Hook for the system-message sender: log the client (ECX) and the 0x28 packet bytes (EDX)."""
        self.w.log.append(("send", current.mu.reg_read(UC_X86_REG_ECX), current.read(current.mu.reg_read(UC_X86_REG_EDX), 0x28)))
        return 0

    def put_packet(self, data):
        """Write the packet bytes (padded to 0x40)."""
        self.m.mu.mem_write(self.packet, bytes(data) + bytes(0x40 - len(data)))

    def randomize(self, unit_guid=None):
        """Draw a fresh frame / stamp / range / client / position state; the frame gaps straddle 0x19 and wrap."""
        rng = self.rng
        frame = rng.choice((rng.randrange(1 << 32), rng.randrange(1000, 100000)))
        gap = rng.choice((0x18, 0x19, 0x1A, 0x1B, 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFE7, rng.randrange(1 << 32)))
        self.frame, self.last = frame, (frame - gap) & 0xFFFFFFFF
        self.m.w32(self.game + 0xA8, self.frame)
        self.m.w32(self.pd + 0x168, self.last)
        self.m.w32(self.unit + 0xC, rng.randrange(1, 1 << 32) if unit_guid is None else unit_guid)
        self.range_status = rng.choice((0, 0, 0, 1, 2, 3, rng.randrange(1, 1 << 32)))
        self.client = rng.choice((0, rng.randrange(1, 1 << 32)))
        self.xpos, self.ypos = rng.randrange(1 << 32), rng.randrange(1 << 32)
        self.pd_ptr = self.pd if rng.randrange(8) else 0
        x, y = rng.choice((rng.randrange(1 << 16), 0, 0xFFFF)), rng.choice((rng.randrange(1 << 16), 0, 0xFFFF))
        self.put_packet(bytes([rng.randrange(256)]) + struct.pack("<HH", x, y))
        self.x, self.y = x, y

    def call(self, address, size, unit=None):
        """Run one handler with game / unit in ECX / EDX; return (status, log)."""
        status = self.w.run(address, self.packet, size, ecx=self.game, edx=self.unit if unit is None else unit, nstack=2)
        return status, list(self.w.log)


def check(got, want, label):
    """Compare the emulated answer with the model; mismatch stops the oracle."""
    require(got == want, f"{label}: game.exe {got!r}, model {want!r}")


def range_model(rig, size, unit):
    """FUN_005496f0 as modelled: (status, log, (x out, y out), stamp after) for the scripted state."""
    if size not in ((5, 6) if mut("loc-size-6-ok") else (5,)):
        return BAD_PARAMS, [], (0, 0), rig.last
    log = [("pd", unit)]
    if rig.pd_ptr == 0:
        return (3 if mut("loc-no-player-status-3") else BAD_DATA), log, (0, 0), rig.last
    x, y = rig.x, rig.y
    ax, ay = (y, x) if mut("loc-swaps-xy-args") else (x, y)
    log.append(("range", unit, 0x31 if mut("loc-range-0x31") else 0x32, ax, ay))
    status = rig.range_status
    if status == 0:
        outs = (y, x) if mut("loc-outs-swapped") else (x, y)
        return OK, log, outs, (rig.last if mut("loc-no-stamp") else rig.frame)
    diff = (rig.frame - rig.last) & 0xFFFFFFFF
    diff = diff if mut("loc-unsigned-diff") else s32(diff)
    if mut("loc-reassign-always") or diff > (0x1A if mut("loc-reassign-threshold-0x1a") else 0x19):
        log.append(("client", unit, FILE_NAME, 0x667))
        if rig.client != 0 or mut("loc-reassign-null-client"):
            guid = (0 if mut("loc-null-unit-guid-0") else 0xFFFFFFFF) if unit == 0 else rig.m.r32(unit + 0xC)
            log.append(("ypos", unit))
            log.append(("xpos", unit))
            px, py = (rig.ypos, rig.xpos) if mut("loc-reassign-swaps-xy") else (rig.xpos, rig.ypos)
            log.append(("reassign", rig.client, 0x14 if mut("loc-reassign-packet-0x14") else 0x15, 0, guid, px, py,
                        0 if mut("loc-reassign-flag-0") else 1))
    return (1 if mut("loc-fail-status-1") else status), log, (0, 0), (rig.frame if mut("loc-stamp-on-fail") else rig.last)


def check_range(rig, rng):
    """FUN_005496f0 for real: outs zeroed (words only), size 5, player data, range 0x32, reassign, stamp."""
    cases = 0
    for _ in range(260):
        rig.randomize(rng.choice((None, None, 0)))
        unit = rig.unit if rng.randrange(8) else 0
        size = rng.choice((5, 5, 5, 4, 6, 0, 0xFFFFFFFF, rng.randrange(1 << 32)))
        out_x, out_y = rig.m.alloc(4), rig.m.alloc(4)
        rig.m.w32(out_x, 0xDEADBEEF)
        rig.m.w32(out_y, 0xCAFEF00D)
        got = rig.w.run(0x54_96F0, rig.game, size, out_x, out_y, ecx=0, edx=0,
                        regs={"eax": rig.packet, "esi": unit}, nstack=4)
        log = list(rig.w.log)
        want, want_log, (ox, oy), stamp = range_model(rig, size, unit)
        check((got, log), (want, want_log), f"range size {size:#x} status {rig.range_status:#x}")
        check((rig.m.r32(out_x), rig.m.r32(out_y), rig.m.r32(rig.pd + 0x168)),
              (0xDEAD0000 | ox, 0xCAFE0000 | oy, stamp), "range outs / stamp")
        cases += 1
    return cases


def check_walk(rig, rng):
    """FUN_005497e0 (walk, mode 2) and FUN_005498d0 (run, mode 3): the location check, then the move with no skill."""
    cases = 0
    for address, mode in ((0x5497E0, 2), (0x5498D0, 3)):
        for _ in range(40):
            rig.randomize()
            size = rng.choice((5, 5, 5, rng.randrange(40)))
            want_status, log, _outs, stamp = range_model(rig, size, rig.unit)
            if want_status == 0:
                used = 3 if mut("walk-mode-3") and mode == 2 else 2 if mut("run-mode-2") and mode == 3 else mode
                log.append(("move", rig.game, rig.unit, 1 if mut("move-skill-nonzero") else 0, used, rig.x, rig.y, 0))
            got = rig.call(address, size)
            check(got, (want_status, log), f"walk/run {address:#x} size {size}")
            check(rig.m.r32(rig.pd + 0x168), stamp, "walk/run stamp")
            cases += 1
    return cases


def check_skill(rig, rng):
    """FUN_00549d00 (0x05) and FUN_00549fc0 (0x0c): the location check, skill, pierce stat, then the cast; answer 0."""
    cases = 0
    for address, side in ((0x549D00, "left"), (0x549FC0, "right")):
        getter = side
        if mut("left-uses-right-skill") and side == "left":
            getter = "right"
        if mut("right-uses-left-skill") and side == "right":
            getter = "left"
        for _ in range(60):
            rig.randomize()
            if rng.randrange(3):
                rig.range_status = 0
                rig.pd_ptr = rig.pd
            size = rng.choice((5, 5, 5, rng.randrange(40)))
            rig.stat = rng.choice((0, 1, 0xFFFFFFFF, rng.randrange(1 << 32)))
            rig.skills = [rng.choice((0, rng.randrange(1, 1 << 32))), rng.randrange(1, 1 << 32)]
            rig.cast_ret = rng.choice((0, 1, 2, 3))
            skills = list(rig.skills)
            want_status, log, _outs, stamp = range_model(rig, size, rig.unit)
            if want_status == 0:
                log.append((getter, rig.unit))
                if skills[0] == 0:
                    want_status = 2 if mut("no-skill-status-2") else BAD_PARAMS
                else:
                    raised = (rig.stat + (2 if mut("pierce-raised-by-2") else 1)) & 0xFFFFFFFF
                    log.append(("get_stat", rig.unit, PIERCE, 0))
                    if not mut("pierce-not-raised"):
                        log.append(("set_stat", rig.unit, PIERCE, raised, 0))
                    log.append((getter, rig.unit))
                    cx, cy = (rig.y, rig.x) if mut("cast-swaps-xy") else (rig.x, rig.y)
                    log.append(("cast", rig.unit, skills[0] if mut("cast-first-skill") else skills[1], rig.game, cx, cy))
                    want_status = rig.cast_ret if mut("cast-status-passes") else OK
            got = rig.call(address, size)
            check(got, (want_status, log), f"{address:#x} size {size} status {rig.range_status:#x} skill {skills[0]:#x}")
            check(rig.m.r32(rig.pd + 0x168), stamp, "skill stamp")
            cases += 1
    return cases


def check_held(rig, rng):
    """FUN_00549e80 (0x08) and FUN_0054a140 (0x0f): location check, skill test, then the plain handler; answer 0."""
    cases = 0
    for address, side, target in ((0x549E80, "left", 0x549D00), (0x54A140, "right", 0x549FC0)):
        getter = side
        if mut("ex-uses-right-getter") and side == "left":
            getter = "right"
        if mut("ex-left-delegates-0c") and side == "left":
            target = 0x549FC0
        if mut("ex-right-delegates-05") and side == "right":
            target = 0x549D00
        for _ in range(70):
            rig.randomize()
            if rng.randrange(3):
                rig.range_status = 0
                rig.pd_ptr = rig.pd
            size = rng.choice((5, 5, 5, rng.randrange(40)))
            rig.skills = [rng.choice((0, rng.randrange(1, 1 << 32)))]
            rig.cast_ret = rng.choice((0, 1, 2, 3))
            skill = rig.skills[0]
            want_status, log, _outs, stamp = range_model(rig, size, rig.unit)
            if want_status == 0:
                log.append((getter, rig.unit))
                if skill == 0 and not mut("ex-no-skill-check"):
                    want_status = BAD_PARAMS
                else:
                    log.append(("to_05" if target == 0x549D00 else "to_0c", rig.game, rig.unit, rig.packet, size))
                    want_status = rig.cast_ret if mut("ex-status-passes") else OK
                    if skill == 0:
                        rig.skills = [0]
            got = rig.call(address, size)
            check(got, (want_status, log), f"held {address:#x} size {size} status {rig.range_status:#x} skill {skill:#x}")
            check(rig.m.r32(rig.pd + 0x168), stamp, "held stamp")
            cases += 1
    return cases


def check_keepalive(rig, rng):
    """FUN_00549fa0 (0x0b): size 1 answers 0, any other size 3; it touches nothing."""
    cases = 0
    for size in (1, 0, 2, 5, 0xFFFFFFFF, 0x80000001) + tuple(rng.randrange(1 << 32) for _ in range(6)):
        got = rig.w.run(0x549FA0, rig.packet, size, ecx=rig.game, edx=rig.unit, nstack=2)
        ok = size in (0, 1) if mut("keepalive-size-0-ok") else size == 1
        want = OK if ok else (2 if mut("keepalive-status-2") else BAD_PARAMS)
        check((got, list(rig.w.log)), (want, []), f"keepalive size {size:#x}")
        cases += 1
    return cases


def cast_model(rig, unit_mode, args):
    """FUN_00549ad0 as modelled: (status, log) for the scripted state; `args` = (unit or 0, skill, game, x, y)."""
    unit, skill, game, tx, ty = args
    if unit == 0:
        if mut("cast-null-unit-ok"):
            return OK, []
        return BAD_DATA, []
    dead = 0x12 if mut("cast-dead-0x12") else 0x11
    if (unit_mode == 0 and not mut("cast-death-ok")) or unit_mode == dead:
        return BAD_DATA, []
    log = [("dual", unit)]
    canuse = list(rig.canuse)
    if rig.dual != 0 and not mut("cast-no-dual") or mut("cast-dual-always"):
        log.append(("swing", unit, skill))
        skill = rig.dual_skill
    value = canuse.pop(0)
    log.append(("canuse", unit, skill))
    log.append(("txt", skill))
    if rig.txt_ptr == 0:
        return BAD_DATA, log
    mask = 0x10 if mut("cast-mask-0x10") else rig.m.mu.mem_read(MASK_BYTE, 1)[0]
    flag = (rig.m.mu.mem_read(rig.txt_ptr + 7, 1)[0] & mask) != 0
    limit = 3 if mut("cast-retry-3") else 2
    low = 0 if mut("cast-retry-0") else 1
    retry = (s32(value) >= low or (mut("cast-retry-negative") and s32(value) < 0)) and (
        s32(value) <= limit or value == 4)
    if retry and flag:
        log.append(("attack", unit, 0, 0xFFFFFFFF))
        skill = rig.attack
        if mut("cast-retry-skill-1"):
            log[-1] = ("attack", unit, 1, 0xFFFFFFFF)
        if not mut("cast-no-recheck"):
            value = canuse.pop(0)
            log.append(("canuse", unit, skill))
    log.append(("mode", unit, skill))
    if rig.mode == 0 and not mut("cast-moves-without-mode"):
        return BAD_DATA, log
    if value == 0 or (mut("cast-moves-on-1") and value == 1):
        mx, my = (ty, tx) if mut("cast-move-swaps-xy") else (tx, ty)
        log.append(("move", game, unit, skill, rig.mode, mx, my, 1 if mut("cast-move-flag-1") else 0))
        return OK, log
    if (value == 1 and not mut("cast-no-invfull")) or (value == 2 and mut("cast-invfull-on-2")):
        log.append(("invfull", unit))
    return BAD_DATA, log


def check_cast(rig, rng):
    """FUN_00549ad0 for real: the guards, dual swing, retry with the basic attack, skill mode, move or message."""
    cases = 0
    for _ in range(420):
        unit = rig.unit if rng.randrange(12) else 0
        unit_mode = rng.choice((0, 0x11, 0x12, 1, 2, 0x10, rng.randrange(1 << 32)))
        rig.m.w32(rig.unit + 0x10, unit_mode)
        rig.m.mu.mem_write(MASK_BYTE, bytes([rng.choice((0x10, 0x10, 0x08, 0xFF, 0, rng.randrange(256)))]))
        rig.m.mu.mem_write(rig.txt + 7, bytes([rng.randrange(256) if rng.randrange(2) else 0x10]))
        rig.txt_ptr = rig.txt if rng.randrange(8) else 0
        rig.dual, rig.dual_skill = rng.choice((0, 1, rng.randrange(1 << 32))), rng.randrange(1, 1 << 32)
        rig.attack = rng.choice((0, rng.randrange(1, 1 << 32)))
        rig.mode = rng.choice((0, 0, rng.randrange(1, 40)))
        rig.canuse = [rng.choice((0, 1, 2, 3, 4, 5, 0xFFFFFFFF, 0x80000000, rng.randrange(1 << 32))) for _ in range(2)]
        if rng.randrange(3) == 0:
            rig.canuse[0] = rng.choice((1, 2, 4))
        skill, game, tx, ty = (rng.randrange(1, 1 << 32), rig.game, rng.randrange(1 << 32), rng.randrange(1 << 32))
        want = cast_model(rig, unit_mode, (unit, skill, game, tx, ty))
        got = rig.w.run(0x54_9AD0, game, tx, ty, ecx=unit, regs={"eax": skill}, nstack=3)
        check((got, list(rig.w.log)), want, f"cast mode {unit_mode:#x} canuse {rig.canuse}")
        cases += 1
    return cases


def check_message(rig, rng):
    """FUN_00549a60: look up the unit's client (line 0x73c), send the 40-byte 0x5a packet, check the stack cookie."""
    cases = 0
    for _ in range(20):
        rig.client = rng.choice((0, rng.randrange(1, 1 << 32)))
        data = bytearray(0x28)
        data[0], data[1], data[2] = 0x5A, 0x0D if mut("invmsg-type-0x0d") else 0x0E, 0 if mut("invmsg-colour-0") else 1
        cookie = rig.m.r32(COOKIE)
        got = rig.w.run(0x54_9A60, ecx=rig.unit, edx=0, nstack=0)     # no result: eax is not checked
        line = 0x73B if mut("invmsg-line-0x73b") else 0x73C
        want = [("client", rig.unit, FILE_NAME, line)]
        if not (mut("invmsg-no-client-skips") and rig.client == 0):
            want.append(("send", rig.client, bytes(data)))
        want.append(("cookie", cookie))
        check(list(rig.w.log), want, f"message client {rig.client:#x} (returned {got:#x})")
        cases += 1
    return cases


def run_all():
    """Run every check; return the case count."""
    rng = random.Random(20261111)
    rig_a = Rig(rng, ("range", "skills", "move"))
    rig_b = Rig(rng, ("range", "skills", "move", "delegate"))
    rig_c = Rig(rng, ("cast", "move"))
    rig_d = Rig(rng, ("msg",))
    cases = check_range(rig_a, rng) + check_keepalive(rig_d, rng) + check_message(rig_d, rng) + check_cast(rig_c, rng)
    for _ in range(3):
        cases += check_walk(rig_a, rng) + check_skill(rig_a, rng) + check_held(rig_b, rng)
    return cases


if __name__ == "__main__":
    mw.drive(__doc__, sys.modules[__name__], run_all, "PlrMsg location handler cases")
