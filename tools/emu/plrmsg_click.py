# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's click-on-unit packet handlers of PlrMsg.cpp (server side).

Handlers are called as in plrmsg_small.py: game in ECX, unit in EDX, then packet and
size on the stack (`ret 8`); statuses 0 ok, 2 bad data, 3 bad length / no skill.

Covers FUN_00549830 (the shared click check: packet in EAX, unit in EDI, stack game,
size, &type out, &id out; zeroes both outs, size must be 9 (3), type under 6 (2),
then the range check with range 0x32 and its status passes through; on 0 it stores
type and id), the walk/run handlers 0x02 FUN_00549890 and 0x04 FUN_00549920 (then
start mode 2 / 3 on the target), the skill-on-unit handlers 0x06 FUN_00549d80,
0x07 FUN_00549e00, 0x0d FUN_0054a040, 0x0e FUN_0054a0c0 (left / right skill, pierce
index stat 0x148 raised by one, then the skill processor with approach 1 for
0x06 / 0x0d and 0 for 0x07 / 0x0e), the held handlers 0x09 FUN_00549ee0,
0x0a FUN_00549f40, 0x10 FUN_0054a1a0, 0x11 FUN_0054a200 (click check, skill test,
then the plain handler on the same packet; the click status and the no-skill 3 pass,
the plain handler's answer is dropped and they answer 0) and the interact handler 0x13
FUN_0054aa90.

Hooked (their work is not claimed): 0x54_8F80, 0x54_9BA0, 0x62_0190, 0x62_01D0,
0x62_53B0, 0x62_7260, 0x58_0A70, 0x54_8B00.

Usage: D2_MPQ_DIR=<MPQ folder> uv run python plrmsg_click.py [--mutation-check]
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDI

import _monster_world as mw
from _monster_world import require

OK, BAD_DATA, BAD_PARAMS = 0, 2, 3
CLICK, RANGE_CHECK, LEFT_SKILL, RIGHT_SKILL = 0x549830, 0x54_8F80, 0x62_0190, 0x62_01D0
GET_STAT, SET_STAT, PROCESS, START_MODE, INTERACT = 0x62_53B0, 0x62_7260, 0x54_9BA0, 0x58_0A70, 0x54_8B00
PIERCE = 0x148
MUTATIONS = ("click-size-8", "click-type-6-ok", "click-range-0x31", "click-keeps-outs-on-fail", "click-swaps-outs",
             "walk-mode-3", "run-mode-2", "left-uses-right-skill", "right-uses-left-skill", "pierce-not-raised",
             "pierce-raised-by-2", "left-approach-0", "no-approach-uses-1", "held-checks-no-skill", "held-drops-click-status", "held-wrong-target",
             "interact-type-7-ok", "interact-range-arg", "no-skill-status-2")
mutation = None


def mut(name):
    """True while the model carries the deliberate defect `name` (--mutation-check)."""
    return mutation == name


class Rig:
    """One emulated game.exe with the callees hooked and the fake game / unit / packet blocks."""

    def __init__(self, rng):
        self.rng = rng
        self.w = mw.World()
        self.m = self.w.m
        self.game, self.unit, self.packet = self.m.alloc(0x100), self.m.alloc(0x100), self.m.alloc(0x40)
        self.click_status, self.click_type, self.click_id = 0, 0, 0
        self.skills = []                       # values the next skill getter calls return
        self.stat = 0
        self.range_status = 0
        self.w.logger(RANGE_CHECK, 1, "range", ret=lambda _c: self.range_status, regs=("ecx", "edx", "eax", "edi"))
        self.w.logger(LEFT_SKILL, 1, "left", ret=lambda _c: self.skills.pop(0), regs=())
        self.w.logger(RIGHT_SKILL, 1, "right", ret=lambda _c: self.skills.pop(0), regs=())
        self.w.logger(GET_STAT, 3, "get_stat", ret=lambda _c: self.stat, regs=())
        self.w.logger(SET_STAT, 4, "set_stat", regs=())
        self.w.logger(PROCESS, 4, "process", regs=("ecx", "eax"))
        self.w.logger(START_MODE, 5, "start_mode", regs=("ecx", "edx"))
        self.w.logger(INTERACT, 3, "interact", ret=lambda _c: self.interact_status, regs=("ecx", "edx"))
        self.interact_status = 0

    def hook_click(self):
        """Replace FUN_00549830 with a scripted shared check (for the wrapper tests)."""
        def click(current):
            """Log the call, zero both outs, store type and id only when the scripted status is 0."""
            game, size, type_ptr, id_ptr = (current.arg(i) for i in range(4))
            self.w.log.append(("click", game, size, current.mu.reg_read(UC_X86_REG_EAX), current.mu.reg_read(UC_X86_REG_EDI)))
            current.w32(type_ptr, 0)
            current.w32(id_ptr, 0)
            if self.click_status == 0:
                current.w32(type_ptr, self.click_type)
                current.w32(id_ptr, self.click_id)
            return self.click_status
        self.m.hook(CLICK, click, 4)

    def put_packet(self, data):
        """Write the packet bytes (padded to 0x40)."""
        self.m.mu.mem_write(self.packet, bytes(data) + bytes(0x40 - len(data)))

    def call(self, address, size, packet=None):
        """Run one handler with game / unit in ECX / EDX; return (status, log)."""
        status = self.w.run(address, self.packet if packet is None else packet, size, ecx=self.game, edx=self.unit, nstack=2)
        return status, list(self.w.log)


def check(got, want, label):
    """Compare the emulated answer with the model; mismatch stops the oracle."""
    require(got == want, f"{label}: game.exe {got!r}, model {want!r}")


def check_click(rig, rng):
    """FUN_00549830 for real: the outs are zeroed first, size 9, type under 6, range 0x32, status passes through."""
    cases = 0
    for size in (9, 8, 10, 0, 0xFFFFFFFF, rng.randrange(0, 40)):
        for type_value in (0, 5, 6, 7, 0xFFFFFFFF, rng.randrange(1 << 32)):
            for status in (0, 1, 2, rng.randrange(1, 1 << 32)):
                ident = rng.randrange(1 << 32)
                rig.put_packet(b"\x02" + struct.pack("<II", type_value, ident))
                rig.range_status = status
                out_type, out_id = rig.m.alloc(4), rig.m.alloc(4)
                rig.m.w32(out_type, 0xDEAD0001)
                rig.m.w32(out_id, 0xDEAD0002)
                got = rig.w.run(CLICK, rig.game, size, out_type, out_id, ecx=0, edx=0,
                                regs={"eax": rig.packet, "edi": rig.unit}, nstack=4)
                log = list(rig.w.log)
                need_size = 8 if mut("click-size-8") else 9
                limit = 7 if mut("click-type-6-ok") else 6
                rng_arg = 0x31 if mut("click-range-0x31") else 0x32
                if size != need_size:
                    want, want_log, outs = BAD_PARAMS, [], (0, 0)
                elif type_value >= limit:
                    want, want_log, outs = BAD_DATA, [], (0, 0)
                else:
                    want_log = [("range", rig.game, type_value, ident, rig.unit, rng_arg)]
                    if status == 0:
                        want, outs = OK, ((ident, type_value) if mut("click-swaps-outs") else (type_value, ident))
                    else:
                        want, outs = status, ((type_value, ident) if mut("click-keeps-outs-on-fail") else (0, 0))
                check((got, log), (want, want_log), f"click size {size:#x} type {type_value:#x} status {status:#x}")
                check((rig.m.r32(out_type), rig.m.r32(out_id)), outs, "click outs")
                cases += 1
    return cases


def check_walk(rig, rng):
    """FUN_00549890 (walk, mode 2) and FUN_00549920 (run, mode 3): the click check, then StartModeID on the target."""
    cases = 0
    for address, mode in ((0x549890, 2), (0x549920, 3)):
        for status in (0, 3, 2, rng.randrange(1, 1 << 32)):
            rig.click_status, rig.click_type, rig.click_id = status, rng.randrange(6), rng.randrange(1 << 32)
            size = rng.choice((9, rng.randrange(40)))
            got, log = rig.call(address, size)
            if mut("walk-mode-3") and mode == 2:
                mode_used = 3
            elif mut("run-mode-2") and mode == 3:
                mode_used = 2
            else:
                mode_used = mode
            want = [("click", rig.game, size, rig.packet, rig.unit)]
            if status == 0:
                want.append(("start_mode", rig.game, rig.unit, 0, mode_used, rig.click_type, rig.click_id, 0))
            check((got, log), (status, want), f"walk/run {address:#x} status {status:#x}")
            cases += 1
    return cases


def skill_model(rig, side, approach, status, size):
    """The expected (status, log) of a skill-on-unit handler (0x06 / 0x07 / 0x0d / 0x0e) for the scripted state."""
    game, unit = rig.game, rig.unit
    getter = "left" if side == "left" else "right"
    if mut("left-uses-right-skill") and side == "left":
        getter = "right"
    if mut("right-uses-left-skill") and side == "right":
        getter = "left"
    log = [("click", game, size, rig.packet, unit)]
    if status != 0:
        return status, log
    first = rig.skills[0]
    log.append((getter, unit))
    if first == 0:
        return (2 if mut("no-skill-status-2") else BAD_PARAMS), log
    raised = (rig.stat + (2 if mut("pierce-raised-by-2") else 1)) & 0xFFFFFFFF
    log.append(("get_stat", unit, PIERCE, 0))
    if not mut("pierce-not-raised"):
        log.append(("set_stat", unit, PIERCE, raised, 0))
    log.append((getter, unit))
    used = approach
    if mut("left-approach-0") and side == "left" and approach == 1:
        used = 0
    if mut("no-approach-uses-1") and approach == 0:
        used = 1
    log.append(("process", unit, rig.skills[1], game, rig.click_type, rig.click_id, used))
    return OK, log


def check_skills(rig, rng):
    """The four skill-on-unit handlers: pierce index raised, skill processor called with the second skill fetch."""
    cases = 0
    for address, side, approach in ((0x549D80, "left", 1), (0x549E00, "left", 0), (0x54A040, "right", 1), (0x54A0C0, "right", 0)):
        for status in (0, 0, 3, rng.randrange(1, 1 << 32)):
            for has_skill in (True, True, False):
                rig.click_status, rig.click_type, rig.click_id = status, rng.randrange(6), rng.randrange(1 << 32)
                rig.stat = rng.choice((0, 1, 0xFFFFFFFF, rng.randrange(1 << 32)))
                rig.skills = [rng.randrange(1, 1 << 32) if has_skill else 0, rng.randrange(1, 1 << 32)]
                size = rng.choice((9, 0, rng.randrange(40)))
                want = skill_model(rig, side, approach, status, size)
                got = rig.call(address, size)
                check(got, want, f"{address:#x} status {status:#x} skill {has_skill}")
                cases += 1
    return cases


def check_held(rig, rng):
    """The held handlers 0x09 / 0x0a / 0x10 / 0x11: click check, skill test, then the plain handler on the same packet and size; answer 0."""
    cases = 0
    plain = {0x549EE0: (0x549D80, "left", 1), 0x549F40: (0x549E00, "left", 0), 0x54A1A0: (0x54A040, "right", 1),
             0x54A200: (0x54A0C0, "right", 0)}
    for address, (target, side, approach) in plain.items():
        if mut("held-wrong-target"):
            target, side, approach = {0x549EE0: (0x549E00, "left", 0), 0x549F40: (0x549D80, "left", 1),
                                      0x54A1A0: (0x54A0C0, "right", 0), 0x54A200: (0x54A040, "right", 1)}[address]
        for status in (0, 0, 3, rng.randrange(1, 1 << 32)):
            for held_skill, plain_skill in ((True, True), (True, False), (False, True)):
                rig.click_status, rig.click_type, rig.click_id = status, rng.randrange(6), rng.randrange(1 << 32)
                rig.stat = rng.randrange(1 << 32)
                size = rng.choice((9, rng.randrange(40)))
                # the held handler fetches the skill once; the plain handler then fetches it twice more
                rig.skills = [rng.randrange(1, 1 << 32) if held_skill else 0,
                              rng.randrange(1, 1 << 32) if plain_skill else 0, rng.randrange(1, 1 << 32)]
                held_first = rig.skills[0]
                plain_skills = rig.skills[1:]
                log = [("click", rig.game, size, rig.packet, rig.unit)]
                if status != 0:
                    want = (0 if mut("held-drops-click-status") else status, log)
                else:
                    log.append((side, rig.unit))
                    if held_first == 0 and not mut("held-checks-no-skill"):
                        want = (BAD_PARAMS, log)
                    else:
                        saved = rig.skills
                        rig.skills = plain_skills
                        # the plain handler runs on the original packet and size and its answer is dropped
                        _, tail = skill_model(rig, side, approach, status, size)
                        rig.skills = saved
                        want = (OK, log + tail)
                got = rig.call(address, size)
                check(got, want, f"held {address:#x} (to {target:#x}) status {status:#x} skills {held_skill}/{plain_skill}")
                cases += 1
    return cases


def check_interact(rig, rng):
    """FUN_0054aa90: size 9, type under 6, then 0x54_8B00 (ECX unit, EDX type, stack id, 0, game); its answer passes through."""
    cases = 0
    for size in (9, 8, 10, rng.randrange(40)):
        for type_value in (0, 5, 6, 7, 0xFFFFFFFF, rng.randrange(1 << 32)):
            ident = rng.randrange(1 << 32)
            rig.interact_status = rng.choice((0, 1, 2, rng.randrange(1 << 32)))
            rig.put_packet(b"\x13" + struct.pack("<II", type_value, ident))
            got = rig.call(0x54AA90, size)
            limit = 7 if mut("interact-type-7-ok") else 6
            if size != 9:
                want = (BAD_PARAMS, [])
            elif type_value >= limit:
                want = (BAD_DATA, [])
            else:
                want = (rig.interact_status, [("interact", rig.unit, type_value, ident, 1 if mut("interact-range-arg") else 0, rig.game)])
            check(got, want, f"interact size {size} type {type_value:#x}")
            cases += 1
    return cases


def run_all():
    """Run every check; return the case count."""
    rng = random.Random(20261110)
    rig = Rig(rng)
    cases = check_click(rig, rng)
    rig.hook_click()
    for _ in range(12):
        cases += check_walk(rig, rng) + check_skills(rig, rng) + check_held(rig, rng) + check_interact(rig, rng)
    return cases


if __name__ == "__main__":
    mw.drive(__doc__, sys.modules[__name__], run_all, "PlrMsg click-on-unit handler cases")
