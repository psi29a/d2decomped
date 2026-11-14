# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify six client UI and input helpers in stock game.exe 1.14d.

Run: uv run python ui_client_helpers.py [--exe /path/to/Game.exe] [--mutation-check]

Each function runs for real in the emulator and is compared with a small
Python model written from the decompile and the disassembly:

- FUN_00469aa0 (key of an action and slot): all 114 ten-byte binding
  records at 0x7a6f90 (+0 action, +4 u16 key, +6 slot), first match wins, a
  miss gives 0xffff in the low word. No callee.
- FUN_0046a530 (key name): the same lookup, then a switch over the key
  (jump table 0x46a6bc, index table 0x46a73c) that tail-jumps to the string
  getter FUN_00524a30 with a string id in ECX for 32 keys, and to
  FUN_00469de0 with ECX and EDX unchanged for any other key and for no
  binding. HOOKED: FUN_00524a30 and FUN_00469de0 (they log their registers).
- FUN_00477730 (driver option): returns DAT_00712ccc. No callee.
- FUN_004f6190 (frame pacing): runs for real including the CRT's float
  to int (FUN_00682fd0) and the x87 arithmetic. HOOKED: the clock FUN_004f6130
  (replaced by `fld qword [cell]; ret`, the cell fed from a script), the Sleep
  import (logs its argument) and the driver's two slots at +0x1c and +0x20
  (log the call order).
- FUN_0049e3a0 (chat line add): HOOKED: the Storm allocator FUN_0040b380 and
  free FUN_0040b3c0, the line wrapper FUN_00502970 (returns a scripted line
  list), the font setter FUN_00502ef0, the draw and refresh callees
  (FUN_0049dbc0, FUN_0049d490, FUN_004b9a00, FUN_004538d0, FUN_0049e340,
  FUN_005263e0, FUN_00611560 and its vtable slot), GetTickCount, and the fatal
  report FUN_00408090 / FUN_00408a60 / FUN_00681e09 (which stops the run).
  Checked: the node bytes, the queue head and counter, the order and
  arguments of every hooked call, the 18-line eviction and the fatal path.
- FUN_00466730 (ambient critter make): HOOKED: FUN_00466360 (type 1 path),
  FUN_00620290 (unit allocator) and FUN_00465fd0 (unit init); they log their
  registers and stack arguments. The counter DAT_00711f30 and the OR of
  0x600000 into the allocated record's +0xc4 are game.exe's own.

Not covered: d2d's ports, the hooked callees' own behaviour, real timing and
real Sleep, the wrapper's line breaking, drawing, the ignore list.

`--mutation-check` plants one deliberate defect at a time in the model and
requires the check to fail on it.
"""
import argparse
import hashlib
import random
import struct
import sys
from pathlib import Path

import emu
import unicorn
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX

if not __debug__:
    sys.exit("ui_client_helpers.py requires assertions; do not use python -O")

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
# Instruction cap for every emulated call (the longest, the 114-record key scan, needs about 2,000).
CALL_LIMIT = 100000

KEY_TABLE = 0x7A6F90
KEY_RECORDS = 114
KEY_RECORD_SIZE = 10

# Key (the 16-bit code in a binding) to the string id FUN_0046a530 hands to
# FUN_00524a30; read from jump table 0x46a6bc / index table 0x46a73c and the
# `mov ecx, id` at each target. Every other key goes to FUN_00469de0.
SPECIAL_KEYS = {
    0x08: 0xF2C, 0x10: 0xF2F, 0x11: 0xF30, 0x1B: 0xF1E, 0x21: 0xF34, 0x22: 0xF35,
    0x2C: 0xF37, 0x2D: 0xF38, 0x2E: 0xF39,
    0x60: 0xF3B, 0x61: 0xF3C, 0x62: 0xF3D, 0x63: 0xF3E, 0x64: 0xF3F, 0x65: 0xF40,
    0x66: 0xF41, 0x67: 0xF42, 0x68: 0xF43, 0x69: 0xF44, 0x6A: 0xF45, 0x6B: 0xF46,
    0x6C: 0xF48, 0x6D: 0xF47, 0x6F: 0xF49,
    0x90: 0xF2B, 0x91: 0xF4A,
    0x100: 0xF15, 0x101: 0xF16, 0x102: 0xF17, 0x103: 0xF18, 0x104: 0xF19,
}

mutation = None


class Mismatch(Exception):
    """A check disagreed with the model."""


class ModelGuard(Exception):
    """The model or its script broke an internal assumption (not a game-vs-model difference)."""


def require(condition, message):
    """Raise Mismatch with message unless condition holds."""
    if not condition:
        raise Mismatch(message)


def broken(name):
    """True while the named model defect is planted."""
    return mutation == name


def new_engine(exe):
    """A fresh emulator on the stock game.exe."""
    return emu.Emu(exe=exe)


def reg(engine, name):
    """A register of the emulator by unicorn constant."""
    return engine.mu.reg_read(name)


# ---- FUN_00469aa0 -----------------------------------------------------

def write_bindings(engine, records):
    """Write 114 (action, key, slot) records at the key table; short lists are zero filled."""
    padded = list(records) + [(0, 0, 0)] * (KEY_RECORDS - len(records))
    blob = b"".join(struct.pack("<IHI", action & M32, key, slot & M32) for action, key, slot in padded)
    engine.mu.mem_write(KEY_TABLE, blob)


def model_key_of(records, action, slot):
    """Low word FUN_00469aa0 returns: the first matching record's key, else 0xffff."""
    count = KEY_RECORDS - 1 if broken("key_113_records") else KEY_RECORDS + 1 if broken("key_115_records") else KEY_RECORDS
    order = range(count - 1, -1, -1) if broken("key_last_match") else range(count)
    for index in order:
        if index >= len(records):
            break
        record_action, key, record_slot = records[index]
        if broken("key_ignore_slot"):
            record_slot = slot
        if broken("key_ignore_action"):
            record_action = action
        if record_action == action and record_slot == slot:
            return key
    return 0xFFFF


def check_key_of(engine):
    """FUN_00469aa0: unique table at every index, misses, duplicates, a sentinel past the end, random tables."""
    cases = 0
    unique = [(1000 + index, index + 1, 10 + index) for index in range(KEY_RECORDS)]
    sentinel = unique + [(7777, 0x1234, 77)]
    write_bindings(engine, unique)
    # The record just past the 114th must not be scanned.
    engine.mu.mem_write(KEY_TABLE + KEY_RECORDS * KEY_RECORD_SIZE, struct.pack("<IHI", 7777, 0x1234, 77))
    for index, (action, _, slot) in enumerate(unique):
        for query_action, query_slot in ((action, slot), (action, slot + 1), (action + 1, slot)):
            got = engine.call(0x469AA0, ecx=query_action, edx=query_slot, limit=CALL_LIMIT) & 0xFFFF
            want = model_key_of(unique, query_action, query_slot)
            require(got == want, f"unique {index} ({query_action}, {query_slot}): game {got:#x} model {want:#x}")
            cases += 1
    got = engine.call(0x469AA0, ecx=7777, edx=77, limit=CALL_LIMIT) & 0xFFFF
    require(got == model_key_of(sentinel, 7777, 77) == 0xFFFF, f"sentinel past the table: {got:#x}")
    cases += 1
    rng = random.Random(0x469AA0)
    for case in range(400):
        records = [(rng.randrange(5), rng.choice((0xFFFF, rng.randrange(0x10000))), rng.randrange(3))
                   for _ in range(rng.choice((1, 2, 10, 60, KEY_RECORDS)))]
        write_bindings(engine, records)
        if len(records) < KEY_RECORDS:
            engine.mu.mem_write(KEY_TABLE + len(records) * KEY_RECORD_SIZE, struct.pack("<IHI", 99, 0x5555, 9) * (KEY_RECORDS - len(records)))
            records = records + [(99, 0x5555, 9)] * (KEY_RECORDS - len(records))
        for _ in range(6):
            action, slot = rng.randrange(6), rng.randrange(4)
            got = engine.call(0x469AA0, ecx=action, edx=slot, limit=CALL_LIMIT) & 0xFFFF
            want = model_key_of(records, action, slot)
            require(got == want, f"random {case} ({action}, {slot}): game {got:#x} model {want:#x}")
            cases += 1
    return cases


# ---- FUN_0046a530 -----------------------------------------------------

def model_key_label(key, action, slot):
    """The one hooked call FUN_0046a530 makes for a key (None means FUN_00469de0)."""
    table = dict(SPECIAL_KEYS)
    if broken("label_swap_6c_6d"):
        table[0x6C], table[0x6D] = table[0x6D], table[0x6C]
    if broken("label_drop_0x104"):
        del table[0x104]
    if broken("label_special_0x105"):
        table[0x105] = 0xF19
    if key in table:
        return [("string", table[key])]
    if broken("label_default_args_swapped"):
        return [("name", slot, action)]
    return [("name", action, slot)]


def check_key_label(engine):
    """FUN_0046a530: every key 0..0x200 plus the 16-bit extremes, with the key as a binding and as a miss."""
    log = []
    engine.hook(0x524A30, lambda machine: log.append(("string", reg(machine, UC_X86_REG_ECX))) or 0, 0)
    engine.hook(0x469DE0, lambda machine: log.append(("name", reg(machine, UC_X86_REG_ECX), reg(machine, UC_X86_REG_EDX))) or 0, 0)
    cases = 0
    keys = list(range(0x201)) + [0x7FFF, 0x8000, 0xFFFE, 0xFFFF]
    for key in keys:
        action, slot = 40 + key % 7, key % 3
        records = [(action + 1, 0x41, slot), (action, key, slot), (action, 0x1B, slot)]
        write_bindings(engine, records)
        del log[:]
        engine.call(0x46A530, ecx=action, edx=slot, limit=CALL_LIMIT)
        want = model_key_label(key, action, slot)
        require(log == want, f"key {key:#x}: game {log} model {want}")
        cases += 1
    write_bindings(engine, [(1, 0x1B, 0)])
    for action, slot in ((2, 0), (1, 1), (0, 0)):
        del log[:]
        engine.call(0x46A530, ecx=action, edx=slot, limit=CALL_LIMIT)
        want = model_key_label(0xFFFF, action, slot)
        require(log == want, f"no binding ({action}, {slot}): game {log} model {want}")
        cases += 1
    return cases


# ---- FUN_00477730 -----------------------------------------------------

def check_driver_option(engine):
    """FUN_00477730 returns DAT_00712ccc whole."""
    cases = 0
    for value in (0, 1, 3, 4, 5, 7, 0x12345678, 0x80000000, M32):
        engine.w32(0x712CCC, value)
        want = value & 0xFF if broken("driver_low_byte") else value
        got = engine.call(0x477730, limit=CALL_LIMIT)
        require(got == want, f"driver option {value:#x}: game {got:#x} model {want:#x}")
        cases += 1
    return cases


# ---- FUN_004f6190 -----------------------------------------------------

PACE_FLAG, PACE_LAST, PACE_CARRY = 0x7C9370, 0x7C9368, 0x7C9360
PACE_THRESHOLD = 0x6DC670
DRIVER_END, DRIVER_PRESENT = 0x5117F0, 0x511870


def model_pace(flag, last, carry, clocks, threshold):
    """FUN_004f6190 on doubles: (flag, last, carry, sleeps, clocks used) after one call."""
    clocks = list(clocks)
    used = 0

    def clock():
        """Next scripted clock value, standing in for FUN_004f6130."""
        nonlocal used
        used += 1
        if used > len(clocks):
            raise ModelGuard("model reads more clocks than the script holds")
        return clocks[used - 1]

    if not flag & 1:
        flag |= 1
        last = clock()
    now = clock()
    elapsed = now - last
    skip = elapsed >= threshold if broken("pace_threshold_ge") else threshold < elapsed
    sleeps = []
    if broken("pace_threshold_4"):
        skip = 4.0 < elapsed
    if not skip:
        budget = threshold - elapsed + carry
        millis = round(budget) if broken("pace_round") else int(budget)
        carry = budget - millis
        zero_ok = millis < 0 if broken("pace_zero_sleeps") else millis <= 0
        too_long = millis >= 5 if broken("pace_limit_5_open") else millis > 5
        if zero_ok or too_long:
            if not broken("pace_carry_kept"):
                carry = 0.0
        else:
            sleeps.append(millis)
            after = clock()
            if not broken("pace_no_correction"):
                carry = carry - ((after - now) - millis)
    last = now if broken("pace_last_not_updated") else clock()
    return flag, last, carry, sleeps, used


def run_pace(engine, flag, last, carry, clocks):
    """One FUN_004f6190 call in the emulator; returns the same tuple as model_pace plus the event log."""
    events = []
    cell = engine.alloc(8)
    queue = list(clocks)
    used = [0]
    extra = [0]

    def feed(uc, address, size, user):
        """Code hook at FUN_004f6130: load the next scripted clock into the cell it reads."""
        if used[0] < len(queue):
            uc.mem_write(cell, struct.pack("<d", queue[used[0]]))
        else:
            extra[0] += 1
        used[0] += 1

    engine.mu.mem_write(0x4F6130, b"\xdd\x05" + struct.pack("<I", cell) + b"\xc3")
    engine.mu.ctl_remove_cache(0x4F6130, 0x4F6140)
    engine.mu.hook_add(unicorn.UC_HOOK_CODE, feed, begin=0x4F6130, end=0x4F6130)
    for address, slot in engine.slots.items():
        if slot[0] == "Sleep":
            engine.slots[address] = ("Sleep", lambda machine: (events.append(("sleep", machine.arg(0))) or 0, 1))
    driver = engine.alloc(0x24)
    engine.w32(0x7C8CC0, driver)
    engine.w32(driver + 0x1C, DRIVER_END)
    engine.w32(driver + 0x20, DRIVER_PRESENT)
    engine.hook(DRIVER_END, lambda machine: events.append(("end",)) or 0, 0)
    engine.hook(DRIVER_PRESENT, lambda machine: events.append(("present",)) or 0, 0)
    engine.w32(PACE_FLAG, flag)
    engine.mu.mem_write(PACE_LAST, struct.pack("<d", last))
    engine.mu.mem_write(PACE_CARRY, struct.pack("<d", carry))
    result = engine.call(0x4F6190, limit=CALL_LIMIT)
    require(result == 1, f"FUN_004f6190 returned {result}")
    if extra[0]:
        raise ModelGuard(f"{extra[0]} clock reads past the script")
    return (engine.r32(PACE_FLAG), struct.unpack("<d", engine.read(PACE_LAST, 8))[0],
            struct.unpack("<d", engine.read(PACE_CARRY, 8))[0], events, used[0])


def check_pace(exe):
    """FUN_004f6190: scripted clocks over the first-call init, the 5.0 skip, the sleep window and the carry."""
    probe = new_engine(exe)
    threshold = struct.unpack("<d", probe.read(PACE_THRESHOLD, 8))[0]
    require(threshold == 5.0, f"threshold constant {threshold}")
    rng = random.Random(0x4F6190)
    fixed = [
        # flag, last, carry, clocks
        (0, 0.0, 0.0, [100.0, 101.0, 105.0, 105.5]),        # first call: init, elapsed 1, sleep 4
        (1, 100.0, 0.0, [112.0, 112.0]),                     # elapsed 12 > 5: no pacing
        (1, 100.0, 0.0, [105.0, 105.0]),                     # elapsed exactly 5: v 0, no sleep
        (1, 100.0, 0.0, [100.0, 105.0, 105.0]),              # elapsed 0: v 5, sleeps 5
        (1, 100.0, 2.5, [100.0, 100.0]),                     # v 7.5: more than 5, carry cleared
        (1, 100.0, -0.75, [104.5, 104.5]),                   # v -0.25 truncates to 0, carry cleared
        (1, 100.0, -1.5, [104.5, 104.5]),                    # v -1.5
        (1, 100.0, 0.5, [102.25, 105.25, 105.5]),            # v 3.25: sleeps 3, carry .25 corrected
        (1, 100.0, 0.0, [99.0, 104.75]),                     # clock went backwards: elapsed -1, v 6
        (0x80, 3.0, 0.0, [10.0, 11.0, 13.0, 13.0]),          # flag bits above bit 0 kept
    ]
    cases = list(fixed)
    for _ in range(120):
        flag = rng.choice((0, 1, 0x80, 0x81))
        last = rng.randrange(0, 400) / 4
        carry = rng.randrange(-8, 13) / 4
        clocks = []
        now = last + rng.randrange(-4, 40) / 4
        if not flag & 1:
            clocks.append(last)
        clocks += [now, now + rng.randrange(0, 40) / 4, now + rng.randrange(0, 80) / 4, now + rng.randrange(0, 80) / 4]
        cases.append((flag, last, carry, clocks))
    count = 0
    for flag, last, carry, clocks in cases:
        # Padding lets a defective model read extra clocks; the clock-read count then differs from the game.
        want = model_pace(flag, last, carry, clocks + [clocks[-1]] * 4, threshold)
        engine = new_engine(exe)
        got = run_pace(engine, flag, last, carry, clocks)
        # The model's list of clocks it needs is what the game consumed.
        want_events = [("sleep", ms) for ms in want[3]] + [("end",), ("present",)]
        if broken("pace_callback_order"):
            want_events = [("sleep", ms) for ms in want[3]] + [("present",), ("end",)]
        require(got[3] == want_events, f"{flag:#x} {last} {carry} {clocks}: events {got[3]} model {want_events}")
        require(got[0] == want[0], f"flag {got[0]:#x} model {want[0]:#x}")
        require(got[1] == want[1], f"last {got[1]} model {want[1]} ({clocks})")
        require(got[2] == want[2], f"carry {got[2]} model {want[2]} ({flag:#x} {last} {carry} {clocks})")
        require(got[4] == want[4], f"clock reads {got[4]} model {want[4]}")
        count += 1
    return count


# ---- FUN_0049e3a0 -----------------------------------------------------

QUEUE_HEAD, QUEUE_COUNT, TICK_WIDTH = 0x7BF1E4, 0x7BF1E8, 0x71146C
NODE_SIZE = 0x26
FONT_HOOK_OBJECT = 0x7BF1B4


def line_cap():
    """The most lines a node keeps."""
    return 7 if broken("chat_cap_7") else 6


def run_chat(exe, spec):
    """Run FUN_0049e3a0 for each message in spec on a fresh emulator; returns the case count.

    spec: dict with `messages` (list of (kind, line count)), initial queue lines
    (list of line counts), `tick`, `width`, `alloc_flag` (FUN_004538d0's result),
    `object` (whether DAT_007bf1b4 is set), `corrupt` (counter off by one).
    """
    engine = new_engine(exe)
    log = []
    pool = []
    for _ in range(len(spec["initial"]) + len(spec["messages"])):
        address = engine.alloc(NODE_SIZE)
        engine.mu.mem_write(address, bytes([0xAA]) * NODE_SIZE)
        pool.append(address)
    next_node = [0]
    pending_lines = []
    stopped = []

    def allocate(machine):
        """Hook for the Storm allocator FUN_0040b380: log the call, hand out the next pooled node."""
        log.append(("alloc", reg(machine, UC_X86_REG_ECX), machine.arg(0), machine.arg(1), reg(machine, UC_X86_REG_EDX)))
        node = pool[next_node[0]]
        next_node[0] += 1
        return node

    def free(machine):
        """Hook for the Storm free FUN_0040b3c0: log the call."""
        log.append(("free", reg(machine, UC_X86_REG_ECX), machine.arg(0), machine.arg(1)))
        return 0

    def wrap(machine):
        """Hook for the line wrapper FUN_00502970: log, report the scripted line count, return the list head."""
        log.append(("wrap", reg(machine, UC_X86_REG_ECX), machine.arg(0)))
        machine.w32(reg(machine, UC_X86_REG_EDX), len(pending_lines))
        return pending_lines[0][0] if pending_lines else machine.alloc(8)

    def font(machine):
        """Hook for the font setter FUN_00502ef0: log, return a font handle when asked for font 0xD."""
        log.append(("font", reg(machine, UC_X86_REG_ECX)))
        return 0x5150 if reg(machine, UC_X86_REG_ECX) == 0xD else 0

    def simple(name):
        """Make a hook that logs its registers under name (FUN_0049dbc0, FUN_0049d490, FUN_004b9a00, FUN_0049e340)."""
        def run(machine):
            """Log the call with ECX, EDX and EDI."""
            log.append((name, reg(machine, UC_X86_REG_ECX), reg(machine, UC_X86_REG_EDX), reg(machine, UC_X86_REG_EDI)))
            return 0
        return run

    def draw_state(machine):
        """Hook for FUN_004538d0: log, return the scripted draw-state flag."""
        log.append(("state", reg(machine, UC_X86_REG_ECX)))
        return spec["alloc_flag"]

    def ignore_object(machine):
        """Hook for FUN_00611560: log, return the fake ignore-list object."""
        log.append(("ignore_list",))
        return ignore_obj

    def ignore_call(machine):
        """Hook for the ignore object's vtable slot (0x4F6250): log ECX and both stack arguments."""
        log.append(("ignore_call", reg(machine, UC_X86_REG_ECX), machine.arg(0), machine.arg(1)))
        return 0

    def copy_text(machine):
        """Hook for FUN_005263e0 (text copy): log ECX, EDX and the stack size argument."""
        log.append(("copy", reg(machine, UC_X86_REG_ECX), reg(machine, UC_X86_REG_EDX), machine.arg(0)))
        return 0

    def vcall(machine):
        """Hook for the view object's vtable slot (0x4F6270) that FUN_0049e3a0 calls: log ECX."""
        log.append(("view_call", reg(machine, UC_X86_REG_ECX)))
        return 0

    def fatal(machine):
        """Hook for the fatal report FUN_00681e09: log, then stop the emulator."""
        log.append(("fatal", machine.arg(0)))
        stopped.append(True)
        machine.mu.emu_stop()
        return 0

    ignore_obj = engine.alloc(0x40)
    ignore_vtable = engine.alloc(0x40)
    engine.w32(ignore_obj, ignore_vtable)
    engine.w32(ignore_vtable + 0x18, 0x4F6250)
    view_obj = engine.alloc(0x40)
    view_vtable = engine.alloc(0x80)
    engine.w32(view_obj, view_vtable)
    engine.w32(view_vtable + 0x3C, 0x4F6270)
    engine.hook(0x40B380, allocate, 2)
    engine.hook(0x40B3C0, free, 2)
    engine.hook(0x502970, wrap, 1)
    engine.hook(0x502EF0, font, 0)
    for name, address, nstack in (("show", 0x49DBC0, 0), ("evict", 0x49D490, 0), ("refresh", 0x49E340, 0)):
        engine.hook(address, simple(name), nstack)
    engine.hook(0x4B9A00, simple("flush"), 3)
    engine.hook(0x4538D0, draw_state, 0)
    engine.hook(0x5263E0, copy_text, 1)
    engine.hook(0x611560, ignore_object, 0)
    engine.hook(0x4F6250, ignore_call, 2)
    engine.hook(0x4F6270, vcall, 0)
    engine.hook(0x408090, lambda machine: 0x1234, 0)
    engine.hook(0x408A60, lambda machine: 0, 0)
    engine.hook(0x681E09, fatal, 0)
    tick = [spec["tick"]]
    for address, slot in engine.slots.items():
        if slot[0] == "GetTickCount":
            engine.slots[address] = ("GetTickCount", lambda machine: (tick[0], 0))
    engine.w32(TICK_WIDTH, spec["width"])
    engine.w32(FONT_HOOK_OBJECT, view_obj if spec["object"] else 0)

    # model queue: list of dicts; the game's queue is rebuilt in memory from the same shape.
    queue = []
    for count in spec["initial"]:
        node = pool[next_node[0]]
        next_node[0] += 1
        shown = min(count, 6)
        engine.mu.mem_write(node, b"\0" * NODE_SIZE)
        for index in range(shown):
            engine.w32(node + 4 * index, 0x7000 + 0x10 * len(queue) + index)
        engine.mu.mem_write(node + 0x18, struct.pack("<H", shown))
        if queue:
            engine.w32(queue[-1]["node"] + 0x22, node)
        queue.append({"node": node, "lines": shown})
    if queue:
        engine.w32(QUEUE_HEAD, queue[0]["node"])
    else:
        engine.w32(QUEUE_HEAD, 0)
    counter = len(queue) + (1 if spec.get("corrupt") else 0)
    engine.mu.mem_write(QUEUE_COUNT, struct.pack("<H", counter))

    cases = 0
    for message_index, (kind, count) in enumerate(spec["messages"]):
        pending_lines.clear()
        text = 0x6000_0000 + 0x100 * message_index
        line_nodes = []
        for index in range(count):
            list_node = engine.alloc(8)
            string = 0x5000_0000 + 0x1000 * message_index + 0x10 * index
            engine.w32(list_node, string)
            line_nodes.append((list_node, string))
        for index, (list_node, _) in enumerate(line_nodes):
            engine.w32(list_node + 4, line_nodes[index + 1][0] if index + 1 < count else 0)
        pending_lines.extend(line_nodes)
        del log[:]
        want_node = pool[next_node[0]] if next_node[0] < len(pool) else None
        tick[0] = (spec["tick"] + 1000 * message_index) & M32
        engine.call(0x49E3A0, ecx=text, edx=kind, limit=CALL_LIMIT)
        require(want_node is not None, "node pool exhausted")
        shown = min(count, line_cap())
        want = [("alloc", NODE_SIZE, 0xD46, 0, 0x6DA13C), ("font", 0xD),
                ("wrap", text, spec["width"] - 0x46 & M32)]
        for index, (list_node, string) in enumerate(line_nodes):
            if index >= shown and not broken("chat_extra_not_freed"):
                want.append(("free", string, 0xD5A, 0))
            want.append(("free", list_node, 0xD5D, 0))
        if spec["corrupt"] and queue:
            want.append(("fatal", 0xFFFFFFFF))
            got_log = [entry for entry in log]
            require(got_log == want, f"fatal path: game {got_log} model {want}")
            require(stopped, "mismatched counter did not reach FUN_00681e09")
            require(engine.r32(queue[-1]["node"] + 0x22) == 0, "node linked despite the counter mismatch")
            return 1
        trigger = kind == 4 and shown == 1
        if broken("chat_ignore_kind_3"):
            trigger = kind in (3, 4) and shown == 1
        if trigger:
            want += [("copy", 0x0, 0, 0x100)]
        queue.append({"node": want_node, "lines": shown})
        total = sum(entry["lines"] for entry in queue)
        old_head = queue[0]["node"]
        over = total > (19 if broken("chat_limit_19") else 18)
        counter = counter + 1
        if over:
            queue.pop(0)
            if not broken("chat_counter_not_restored"):
                counter -= 1
        # Compare in two parts: everything up to the show call exactly, then the rest.
        index_show = next((i for i, entry in enumerate(log) if entry[0] == "show"), None)
        require(index_show is not None, "FUN_0049dbc0 not called")
        got_head = log[:index_show]
        head_want = [entry for entry in want if entry[0] != "copy"]
        require([entry for entry in got_head if entry[0] not in ("copy", "ignore_list", "ignore_call")] == head_want,
                f"message {message_index} up to show: game {got_head} model {head_want}")
        ignore_events = [entry[0] for entry in got_head if entry[0] in ("copy", "ignore_list", "ignore_call")]
        require(ignore_events == (["copy", "ignore_list", "ignore_call"] if trigger else []),
                f"ignore-list path {ignore_events}, kind {kind}, lines {shown}")
        if trigger:
            call = next(entry for entry in got_head if entry[0] == "ignore_call")
            require(call[1] == ignore_obj and call[3] == 0x100, f"ignore call {call}")
            copy = next(entry for entry in got_head if entry[0] == "copy")
            require(copy[2] == line_nodes[0][1] and copy[3] == 0x100, f"ignore copy {copy}")
        tail = log[index_show:]
        require(tail[0][:2] == ("show", want_node), f"FUN_0049dbc0 ECX {tail[0]}")
        rest = [entry[0] for entry in tail[1:]]
        want_rest = (["evict"] if over else []) + ["flush", "state"]
        if spec["alloc_flag"] and spec["object"]:
            want_rest += ["view_call", "refresh"]
        want_rest += ["font"]
        require(rest == want_rest, f"message {message_index} after show: game {rest} model {want_rest}")
        if over:
            evict = next(entry for entry in tail if entry[0] == "evict")
            require(evict[3] == old_head, f"evicted {evict[3]:#x} model {old_head:#x}")
        flush = next(entry for entry in tail if entry[0] == "flush")
        require((flush[1], flush[2]) == (6, 0), f"FUN_004b9a00 registers {flush}")
        require(tail[-1] == ("font", 0x5150), f"font restore {tail[-1]}")
        # state
        node = want_node
        raw = engine.read(node + 0x18, 0x0E)
        got_count, got_kind, got_expiry = struct.unpack("<HII", raw[:10])
        require((got_count, got_kind) == (shown, kind), f"node count/kind {got_count}/{got_kind} model {shown}/{kind}")
        require(got_expiry == (tick[0] + (5000 if broken("chat_expiry_5000") else 10000)) & M32, f"expiry {got_expiry:#x}")
        for index in range(6):
            string = engine.r32(node + 4 * index)
            want_string = line_nodes[index][1] if index < shown else 0
            require(string == want_string, f"node line {index}: {string:#x} model {want_string:#x}")
        require(engine.r32(node + 0x22) == 0, "new node's next not null")
        # queue chain from the head
        chain = []
        cursor = engine.r32(QUEUE_HEAD)
        while cursor:
            chain.append(cursor)
            cursor = engine.r32(cursor + 0x22)
            require(len(chain) < 64, "queue loops")
        want_chain = [entry["node"] for entry in queue]
        if broken("chat_prepend"):
            want_chain = want_chain[::-1]
        require(chain == want_chain, f"queue {[hex(c) for c in chain]} model {[hex(c) for c in want_chain]}")
        require(engine.r16(QUEUE_COUNT) == counter, f"counter {engine.r16(QUEUE_COUNT)} model {counter}")
        cases += 1
    return cases


def check_chat(exe):
    """FUN_0049e3a0: message sequences over line counts, kinds, eviction, the cap of six lines and the fatal path."""
    rng = random.Random(0x49E3A0)
    total = 0
    scripts = [
        dict(initial=[], messages=[(2, 0), (3, 1)], tick=1000, width=0x1E0, alloc_flag=0, object=False),
        dict(initial=[], messages=[(4, 1), (4, 2), (3, 1), (4, 1)], tick=0xFFFFF000, width=0x280, alloc_flag=1, object=True),
        dict(initial=[], messages=[(1, 7), (1, 6), (2, 3), (2, 6), (2, 6), (5, 1)], tick=5, width=0x100, alloc_flag=1, object=False),
        dict(initial=[3, 4, 5], messages=[(1, 6), (0, 2)], tick=77, width=0x300, alloc_flag=0, object=True),
        dict(initial=[6, 6, 5], messages=[(1, 1), (1, 1)], tick=9, width=0x200, alloc_flag=1, object=True),
        dict(initial=[2, 2], messages=[(1, 1)], tick=9, width=0x200, alloc_flag=1, object=True, corrupt=True),
    ]
    for script in scripts:
        script.setdefault("corrupt", False)
        total += run_chat(exe, script)
    for number in range(12):
        kinds = [rng.randrange(0, 6) for _ in range(8)]
        counts = [rng.choice((0, 1, 1, 2, 3, 5, 6, 7, 9)) for _ in range(8)]
        script = dict(initial=[], messages=list(zip(kinds, counts)), tick=rng.randrange(M32 + 1),
                      width=rng.randrange(0x100, 0x400), alloc_flag=rng.randrange(2), object=bool(rng.randrange(2)), corrupt=False)
        total += run_chat(exe, script)
    return total


# ---- FUN_00466730 -----------------------------------------------------

CRITTER_COUNTER = 0x711F30


def check_critter(exe):
    """FUN_00466730: counter wrap, the type 1 record, unit allocation and the 0x600000 flags."""
    rng = random.Random(0x466730)
    count = 0
    for counter in (0, 1, 0xFFFFFFFD, 0xFFFFFFFE, 0xFFFFFFFF, 0x80000000):
        for kind in (1, 2, 5, 14):
            for found in (True, False):
                engine = new_engine(exe)
                log = []
                row = engine.alloc(0x100)
                flags = rng.randrange(M32 + 1)
                engine.w32(row + 0xC4, flags)

                def record_type1(machine, log=log):
                    """Hook for FUN_00466360 (type 1 path): log the record words, EDX and four stack arguments."""
                    record = reg(machine, UC_X86_REG_ECX)
                    log.append(("type1", tuple(machine.r32(record + 4 * field) for field in range(7)),
                                reg(machine, UC_X86_REG_EDX), tuple(machine.arg(index) for index in range(4))))
                    return 0

                def allocate(machine, log=log, row=row, found=found):
                    """Hook for FUN_00620290 (unit allocator): log, return the row or 0."""
                    log.append(("alloc", machine.arg(0), machine.arg(1)))
                    return row if found else 0

                def init(machine, log=log):
                    """Hook for FUN_00465fd0 (unit init): log its six stack arguments."""
                    log.append(("init", tuple(machine.arg(index) for index in range(6))))
                    return 0

                engine.hook(0x466360, record_type1, 4)
                engine.hook(0x620290, allocate, 2)
                engine.hook(0x465FD0, init, 6)
                engine.w32(CRITTER_COUNTER, counter)
                ecx, edx = rng.randrange(M32 + 1), rng.randrange(M32 + 1)
                first, third = rng.randrange(M32 + 1), rng.randrange(M32 + 1)
                engine.call(0x466730, first, kind, third, ecx=ecx, edx=edx, limit=CALL_LIMIT)
                new = 0 if (counter + 1) & M32 == (0xFFFFFFFE if broken("critter_wrap_early") else M32) else (counter + 1) & M32
                if broken("critter_no_wrap") and (counter + 1) & M32 == M32:
                    new = M32
                require(engine.r32(CRITTER_COUNTER) == new, f"counter {counter:#x}: game {engine.r32(CRITTER_COUNTER):#x} model {new:#x}")
                if kind == 1:
                    want = [("type1", (new, ecx, edx, first, 0, 0, 1), 0, (0, 0, 0, 1))]
                    if broken("critter_type1_allocates"):
                        want.insert(0, ("alloc", 0, 1))
                elif found:
                    mask = 0x200000 if broken("critter_flag_mask") else 0x600000
                    require(engine.r32(row + 0xC4) == flags | mask, f"row flags {engine.r32(row + 0xC4):#x}")
                    want = [("alloc", 0, kind), ("init", (new, ecx, first, kind, third, 0))]
                else:
                    want = [("alloc", 0, kind)]
                    require(engine.r32(row + 0xC4) == flags, "flags written without a record")
                require(log == want, f"type {kind} found {found}: game {log} model {want}")
                count += 1
    return count


CHECKS = (
    ("FUN_00469aa0", check_key_of, False),
    ("FUN_0046a530", check_key_label, False),
    ("FUN_00477730", check_driver_option, False),
    ("FUN_004f6190", check_pace, True),
    ("FUN_0049e3a0", check_chat, True),
    ("FUN_00466730", check_critter, True),
)

MUTATIONS = {
    "key_113_records": "FUN_00469aa0",
    "key_115_records": "FUN_00469aa0",
    "key_last_match": "FUN_00469aa0",
    "key_ignore_slot": "FUN_00469aa0",
    "key_ignore_action": "FUN_00469aa0",
    "label_swap_6c_6d": "FUN_0046a530",
    "label_drop_0x104": "FUN_0046a530",
    "label_special_0x105": "FUN_0046a530",
    "label_default_args_swapped": "FUN_0046a530",
    "driver_low_byte": "FUN_00477730",
    "pace_threshold_ge": "FUN_004f6190",
    "pace_threshold_4": "FUN_004f6190",
    "pace_round": "FUN_004f6190",
    "pace_zero_sleeps": "FUN_004f6190",
    "pace_limit_5_open": "FUN_004f6190",
    "pace_carry_kept": "FUN_004f6190",
    "pace_no_correction": "FUN_004f6190",
    "pace_last_not_updated": "FUN_004f6190",
    "pace_callback_order": "FUN_004f6190",
    "chat_cap_7": "FUN_0049e3a0",
    "chat_extra_not_freed": "FUN_0049e3a0",
    "chat_ignore_kind_3": "FUN_0049e3a0",
    "chat_limit_19": "FUN_0049e3a0",
    "chat_counter_not_restored": "FUN_0049e3a0",
    "chat_expiry_5000": "FUN_0049e3a0",
    "chat_prepend": "FUN_0049e3a0",
    "critter_wrap_early": "FUN_00466730",
    "critter_no_wrap": "FUN_00466730",
    "critter_type1_allocates": "FUN_00466730",
    "critter_flag_mask": "FUN_00466730",
}


def run_check(name, exe):
    """Run the check for one function; returns its case count."""
    for function, check, wants_exe in CHECKS:
        if function == name:
            return check(exe) if wants_exe else check(new_engine(exe))
    raise SystemExit(f"no check for {name}")


def main():
    """Run every check, or with --mutation-check prove each model defect is caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", help="stock game.exe 1.14d (default: the MPQ folder's bin/game.exe)")
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    exe = Path(options.exe or Path(emu.data_path()) / "bin" / "game.exe")
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    exe = str(exe)
    if options.mutation_check:
        for name, function in MUTATIONS.items():
            mutation = name
            try:
                run_check(function, exe)
            except Mismatch:
                print(f"caught: {name} ({function})")
            except ModelGuard as error:
                raise SystemExit(f"error: mutation {name} broke the model, not the comparison: {error}")
            except SystemExit as error:
                if str(error).startswith("emu:"):
                    raise SystemExit(f"error: emulator fault under mutation {name}: {error}")
                raise
            else:
                raise SystemExit(f"mutation {name} was not caught")
            finally:
                mutation = None
        print(f"ok: {len(MUTATIONS)} model defects caught")
        return
    total = 0
    for function, _, _ in CHECKS:
        count = run_check(function, exe)
        print(f"ok: {function}: {count} cases")
        total += count
    print(f"ok: {total} client helper cases over {len(CHECKS)} functions")


if __name__ == "__main__":
    main()
