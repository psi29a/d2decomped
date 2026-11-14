# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the maze room preset picker and the prescribed-neighbour helper of game.exe 1.14d.

    uv run python drlg_room_helpers.py --exe /path/to/game.exe [--mutation-check]

FUN_006709b0 (EBX room, stack flag; ret 4) runs for real on every LevelType
the function reads (0..0x25 and four larger values: the 20 table or
arithmetic cases and the fatal default), every 4-bit link mask (links in
list order, plus links whose direction is out of range), both flag values,
and the level +0x1d0 values its special cases compare. The model reads the
data tables at 0x6ef6d8..0x6ef760 from the emulator; FUN_00670810 (case 0x21)
runs for real. Checked: the written definition and file, the flag bit
(bit 1 of preset +0xc set for flag 0, cleared otherwise; other bits kept),
no write when the definition is 0, EAX = room, the popped stack, and for the
fatal default the FUN_00408090(0x160) / FUN_00408a60 call and no write.

FUN_00670de0 (ECX source, EAX side, stack def, file, flag; ret 0xc) runs for
real with its callees FUN_0066b3e0 (allocate), FUN_00670880 (place),
FUN_0066c100 (free), FUN_0066b5e0 (link) and FUN_0066b970 (list insert)
hooked and logged with the registers and stack words it passes them; they
are not claimed. FUN_006709b0 runs for real when flag is set. Checked: the
callee order and arguments, the copied dimensions seen by the placement, the
def / file / special-bit write on the new room, the source's preset (def
0x34 + link mask) and the return value.
`--mutation-check` breaks the model in 9 ways (7 model defects, 2 targets) and also overwrites each
target's first byte with `ret` in a fresh emulator.
"""
import argparse
import itertools
import sys

import emu
from unicorn.x86_const import (UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_ESP)

if not __debug__:
    sys.exit("drlg_room_helpers.py requires assertions; do not use python -O")

MUT = set()
BASE_SP = emu.STACK + emu.STACK_SIZE - 0x1000
M32 = 0xFFFFFFFF
SENTINEL = (0x1111, 0x2222, 0x3333)
ADD = {3: 0x34, 4: 0x6c, 7: 0xa7, 8: 0xcd, 0xa: 0x101, 0xd: 0x12d, 0x11: 0x19d,
       0x12: 0x1e1, 0x13: 0x1fd, 0x16: 0x2f1, 0x18: 0x298, 0x19: 0x2c0, 0x1c: 0x344,
       0x22: 0x422}
DIRECTION_BIT = {0: 1, 1: 8, 2: 2, 3: 4}      # link direction -> mask bit (left, above, right, below)
STATS = {"fatal": 0, "zero_def": 0, "special_2": 0, "special_3": 0, "special_298": 0,
         "special_297": 0, "diff_pass": 0, "diff_skip": 0}


class Fatal(Exception):
    """Raised by the FUN_00408a60 hook: game.exe's fatal-error report was reached."""


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def table_word(engine, base, index, stride):
    """A dword of a game.exe data table: base + index * stride."""
    return engine.r32(base + index * stride)


def expected_preset(engine, level_type, level_1d0, mask, difficulty_pass):
    """The model of FUN_006709b0: (def, file) written, None for no write, or 'fatal'."""
    low = mask
    if mut("bits"):
        low = ((mask & 1) << 1) | ((mask & 2) >> 1) | (mask & 12)
    file_arg = M32
    if level_type in ADD:
        base = ADD[level_type] + (1 if mut("base") and level_type == 3 else 0)
        definition = low + base
    elif level_type == 0xe:
        definition = table_word(engine, 0x6ef758, low, 12)
    elif level_type == 0xf:
        definition = table_word(engine, 0x6ef75c, low, 12)
        if level_1d0 == 0x34 and definition == 0x169:
            file_arg = 2
        if level_1d0 == 0x36 and definition in (0x169, 0x167):
            file_arg = 3
    elif level_type == 0x17:
        definition = table_word(engine, 0x6ef760, low, 12)
        if level_1d0 == 0x54 and definition == 0x296:
            definition = 0x298
        if level_1d0 == 0x55 and definition == 0x295:
            definition = 0x297
    elif level_type == 0x20:
        definition = table_word(engine, 0x6ef6d8, low, 8)
    elif level_type == 0x21:
        definition = low if difficulty_pass else low + 0x3ea
    elif level_type == 0x23:
        definition = table_word(engine, 0x6ef6dc, low, 8)
    else:
        return "fatal"
    if definition == 0:
        return None
    return definition, file_arg


class Rig:
    """One emulator with a synthetic room, level and preset record."""

    def __init__(self, executable, target=None):
        """Load game.exe; with `target`, overwrite that function's first byte with `ret`."""
        self.engine = emu.Emu(exe=executable)
        if target:
            self.engine.mu.mem_write(target, b"\xc3")
            self.engine.mu.ctl_remove_cache(target, target + 1)
        self.log = []
        self.cases = 0

    def hook(self, address, name, nstack, result=0):
        """Replace a callee: log (name, ecx, edx, esi, ebx, stack words) and return result."""
        engine = self.engine

        def handler(machine):
            """Record the registers and stack words, then answer."""
            regs = tuple(machine.mu.reg_read(r) for r in (UC_X86_REG_ECX, UC_X86_REG_EDX,
                                                           UC_X86_REG_ESI, UC_X86_REG_EBX))
            eax = machine.mu.reg_read(emu.UC_X86_REG_EAX)
            words = tuple(machine.arg(i) for i in range(nstack))
            self.log.append((name, eax) + regs + words)
            return result(machine) if callable(result) else result

        engine.hook(address, handler, nstack)

    def make_links(self, mask, extras=()):
        """A room link list whose directions give `mask`, newest first, plus out-of-range extras."""
        engine = self.engine
        head = 0
        directions = [d for d, bit in DIRECTION_BIT.items() if mask & bit] + list(extras)
        for direction in directions:
            link = engine.alloc(0x18)
            engine.w32(link + 4, direction)
            engine.w32(link + 0x14, head)
            head = link
        return head


def run_preset(rig):
    """Check FUN_006709b0 over level types, link masks, flags and +0x1d0 values."""
    engine = rig.engine
    engine.hook(0x408090, lambda machine: (rig.log.append(("string", machine.arg(0))) or 0x7000), 1)

    def fatal(machine):
        """Record FUN_00408a60's arguments and stop the run, as the real fatal path would."""
        rig.log.append(("fatal", machine.arg(0), machine.arg(1)))
        raise Fatal

    engine.hook(0x408a60, fatal, 0)
    room = engine.alloc(0x80)
    level = engine.alloc(0x200)
    preset = engine.alloc(0x20)
    dims = engine.alloc(0x40)
    objects = engine.alloc(0x500)
    engine.w32(room + 0x58, level)
    engine.w32(room + 0x20, preset)
    engine.w32(level + 0x14, dims)
    engine.w32(level + 0x1b4, objects)
    types = list(range(0, 0x26)) + [0x100, 0x7fffffff, 0xfffffffd, 0xffffffff]
    for level_type in types:
        for level_1d0 in (0, 0x34, 0x36, 0x37, 0x54, 0x55, 0x56):
            if level_1d0 and level_type not in (0xf, 0x17):
                continue
            for mask in range(16):
                for extras in ((), (4,), (M32, 7)):
                    if extras and mask not in (0, 5, 15):
                        continue
                    variants = (0,)
                    if level_type == 0x21:
                        variants = (0, 1, 2)
                    for variant in variants:
                        for flag in (0, 1, 7):
                            for flags_before in (0, M32, 0x10):
                                preset_case(rig, room, level, preset, dims, objects, level_type,
                                            level_1d0, mask, extras, variant, flag, flags_before)


def preset_case(rig, room, level, preset, dims, objects, level_type, level_1d0, mask, extras,
                variant, flag, flags_before):
    """One FUN_006709b0 call and its checks."""
    engine = rig.engine
    engine.w32(room, rig.make_links(mask, extras))
    engine.w32(level + 0x1c0, level_type)
    engine.w32(level + 0x1d0, level_1d0)
    # FUN_00670810 (case 0x21): level +0x14 row dword [1 + difficulty] == 1 and byte +0x450.
    engine.w32(dims + 4 + 4 * variant, 1 if variant != 2 else 5)
    engine.mu.mem_write(objects + 0x450, bytes([variant]))
    engine.w32(dims + 4 + 4 * 0, 1 if variant == 0 else 0)
    passed = engine.r32(dims + 4 + 4 * variant) == 1
    for index, value in enumerate(SENTINEL):
        engine.w32(preset + 4 * index if index < 2 else preset + 4 * 3, value)
    engine.w32(preset + 12, flags_before)
    rig.log.clear()
    expected = expected_preset(engine, level_type, level_1d0, mask, passed)
    effective_flag = flag
    if mut("flag_sense"):
        effective_flag = 0 if flag else 1
    try:
        result = engine.call(0x6709b0, flag, regs={"ebx": room}, limit=100000)
    except Fatal:
        assert expected == "fatal", f"unexpected fatal at type {level_type:#x}"
        assert ("string", 0x160) in rig.log and rig.log[-1][:2] == ("fatal", 0x6cc837)
        assert rig.log[-1][2] == 0x7000
        assert (engine.r32(preset), engine.r32(preset + 4), engine.r32(preset + 12)) == (
            SENTINEL[0], SENTINEL[1], flags_before)
        STATS["fatal"] += 1
        rig.cases += 1
        return
    assert expected != "fatal", f"type {level_type:#x} should be fatal"
    assert result == room, f"EAX {result:#x} is not the room"
    assert engine.mu.reg_read(UC_X86_REG_ESP) == BASE_SP, "stack not popped by ret 4"
    if expected is None:
        STATS["zero_def"] += 1
        assert (engine.r32(preset), engine.r32(preset + 4)) == SENTINEL[:2]
        assert engine.r32(preset + 12) == flags_before
        rig.cases += 1
        return
    definition, file_arg = expected
    if effective_flag:
        new_flags = flags_before & 0xfffffffd
    else:
        new_flags = flags_before | 2
    assert engine.r32(preset) == definition, (level_type, mask, hex(engine.r32(preset)), hex(definition))
    assert engine.r32(preset + 4) == file_arg, (level_type, mask, level_1d0)
    assert engine.r32(preset + 12) == new_flags
    for key, hit in (("special_2", file_arg == 2), ("special_3", file_arg == 3),
                     ("special_298", level_type == 0x17 and definition == 0x298),
                     ("special_297", level_type == 0x17 and definition == 0x297)):
        STATS[key] += hit
    if level_type == 0x21:
        STATS["diff_pass" if passed else "diff_skip"] += 1
    rig.cases += 1


def run_neighbor(rig):
    """Check FUN_00670de0 with its room callees hooked and FUN_006709b0 for real."""
    engine = rig.engine
    source = engine.alloc(0x80)
    room = engine.alloc(0x80)
    level = engine.alloc(0x200)
    dims = engine.alloc(0x20)
    source_preset = engine.alloc(0x20)
    room_preset = engine.alloc(0x20)
    engine.w32(source + 0x58, level)
    engine.w32(source + 0x20, source_preset)
    engine.w32(room + 0x58, level)
    engine.w32(room + 0x20, room_preset)
    engine.w32(level + 0x14, dims)
    engine.w32(level + 0x1c0, 3)
    accepted = [1]
    seen_dims = []

    def place(machine):
        """FUN_00670880 stand-in: remember the dimensions it sees, answer `accepted`."""
        seen_dims.append((engine.r32(room + 0x3c), engine.r32(room + 0x40)))
        return accepted[0]

    rig.hook(0x66b3e0, "alloc", 0, room)
    rig.hook(0x670880, "place", 1, place)
    rig.hook(0x66c100, "free", 0, 0)
    rig.hook(0x66b5e0, "link", 1, 0)
    rig.hook(0x66b970, "insert", 0, 0)
    for width, height, side, definition, file_arg, flag, mask, flags_before in itertools.product(
            (32, 56), (40, 0x7fffffff), (0, 1, 2, 3), (167, 0xc6), (5, M32), (0, 1, 9),
            (0, 3, 15), (0, M32)):
        rig.log.clear()
        seen_dims.clear()
        engine.w32(dims + 0x10, width)
        engine.w32(dims + 0x14, height)
        engine.w32(source, rig.make_links(mask))
        for preset in (source_preset, room_preset):
            engine.w32(preset, SENTINEL[0])
            engine.w32(preset + 4, SENTINEL[1])
            engine.w32(preset + 12, flags_before)
        engine.w32(room + 0x3c, 0)
        engine.w32(room + 0x40, 0)
        accepted[0] = 1
        result = engine.call(0x670de0, definition, file_arg, flag, ecx=source,
                             regs={"eax": side}, limit=100000)
        assert result == room
        assert engine.mu.reg_read(UC_X86_REG_ESP) == BASE_SP, "stack not popped by ret 0xc"
        want_dims = (height, width) if mut("dims") else (width, height)
        assert seen_dims == [want_dims], seen_dims
        names = [row[0] for row in rig.log]
        assert names == ["alloc", "place", "link", "insert"], names
        alloc, place_row, link, insert = rig.log
        assert (alloc[2], alloc[3]) == (level, 2)
        assert (place_row[1], place_row[4]) == (side, source) and place_row[6] == room
        assert (link[2], link[3]) == (source, room) and link[6] == (side if not mut("side") else 0)
        assert (insert[2], insert[3]) == (level, room)
        source_flags, source_def = engine.r32(source_preset + 12), engine.r32(source_preset)
        if flag:
            assert source_def == 0x34 + mask and engine.r32(source_preset + 4) == M32
            assert source_flags == flags_before & 0xfffffffd
        else:
            assert (source_def, engine.r32(source_preset + 4), source_flags) == (
                SENTINEL[0], SENTINEL[1], flags_before)
        assert engine.r32(room_preset) == definition and engine.r32(room_preset + 4) == file_arg
        want_flags = (flags_before | 2) if not mut("special") else flags_before
        assert engine.r32(room_preset + 12) == want_flags
        rig.cases += 1
        # Rejection: the placement says no, so the room is freed and the helper returns 0.
        rig.log.clear()
        for preset in (source_preset, room_preset):
            engine.w32(preset, SENTINEL[0])
            engine.w32(preset + 12, flags_before)
        accepted[0] = 0 if not mut("accept") else 1
        result = engine.call(0x670de0, definition, file_arg, flag, ecx=source,
                             regs={"eax": side}, limit=100000)
        assert result == 0, f"rejected call returned {result:#x}"
        assert engine.mu.reg_read(UC_X86_REG_ESP) == BASE_SP
        assert [row[0] for row in rig.log] == ["alloc", "place", "free"], rig.log
        assert (rig.log[2][2]) == room
        assert (engine.r32(room_preset), engine.r32(room_preset + 12)) == (SENTINEL[0], flags_before)
        assert (engine.r32(source_preset), engine.r32(source_preset + 12)) == (SENTINEL[0], flags_before)
        rig.cases += 1


# (name, address, runner): the targets the mutation check breaks one by one.
TARGETS = (("FUN_006709b0", 0x6709b0, run_preset), ("FUN_00670de0", 0x670de0, run_neighbor))
MODEL_MUTATIONS = (("bits", run_preset), ("base", run_preset), ("flag_sense", run_preset),
                   ("dims", run_neighbor), ("side", run_neighbor), ("special", run_neighbor),
                   ("accept", run_neighbor))


def main():
    """Run the checks, or require each deliberate defect to be caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, help="local game.exe 1.14d")
    parser.add_argument("--mutation-check", action="store_true",
                        help="break the model and each target, require every check to fail")
    options = parser.parse_args()
    if not options.mutation_check:
        total = 0
        for name, _address, runner in TARGETS:
            rig = Rig(options.exe)
            runner(rig)
            print(f"ok: {name}: {rig.cases} cases")
            total += rig.cases
        for key in ("special_2", "special_3", "special_298", "special_297", "diff_pass",
                    "diff_skip", "zero_def", "fatal"):
            assert STATS[key], f"branch {key} never reached; extend the cases"
        print(f"ok: {total} DRLG room helper cases; branches {STATS}")
        return
    caught = 0
    for name, runner in MODEL_MUTATIONS:
        MUT.add(name)
        try:
            runner(Rig(options.exe))
        except AssertionError:
            caught += 1
            print(f"mutation model {name}: caught")
        else:
            raise SystemExit(f"mutation model {name}: NOT caught")
        finally:
            MUT.discard(name)
    for name, address, runner in TARGETS:
        try:
            runner(Rig(options.exe, target=address))
        except (AssertionError, RuntimeError, Fatal):
            caught += 1
            print(f"mutation target {name}: caught")
        else:
            raise SystemExit(f"mutation target {name}: NOT caught")
    print(f"ok: {caught} mutations caught")


if __name__ == "__main__":
    main()
