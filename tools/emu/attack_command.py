# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the attack command builder FUN_00461700 (UNIT\\Player.cpp) in stock game.exe 1.14d.

    uv run python attack_command.py --exe /path/to/game.exe [--mutation-check]

FUN_00461700 (ESI = the click record, one stack word: 1 = at the unit in the
record, 0 = at a spot; `ret 4`) sends the player's attack command through
FUN_00481030 (ECX = the command code, EDX = the record's +4 word, three stack
words). The record: +0 flags (bit 0 left, bit 1 right, 0x20 shift, 8 the
alternate), +4 the skill word, +8 the unit (type at +0, id at +0xc; none gives
type 6, id -1), +0xc / +0x10 the spot. Both buttons can send in one call. The
gate FUN_00468740() <= FUN_00454970() must hold.

At a spot (stack word 0) the shift flag is not looked at: left sends 5 (8
alternate) and right 0xc (0xf alternate): "Attack in place" at a spot is the
same command as a plain attack, which d2d's ponytail note in
components/game/world/commands.cpp says isn't swung. At a unit shift picks
codes 7 / 0xa (left) and 0xe / 0x11 (right), plain 6 / 9 and 0xd / 0x10.

Everything FUN_00461700 calls is hooked and scripted; the model is written from
the disassembly. `--mutation-check` breaks it one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import itertools
import random
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("spot-uses-shift", "unit-shift-swapped", "right-before-left", "gate-reversed", "null-unit-type", "alternate-ignored",
             "left-only")
mutation = None


def left_code(flags, at_unit):
    """The command code for the left button (flag 1): at a unit shift (0x20) and the alternate (8) pick it."""
    shift, alt = bool(flags & 0x20), bool(flags & 8)
    if not at_unit:
        if mutation == "spot-uses-shift" and shift:
            return 7 if not alt else 0xA
        return 8 if alt else 5
    if mutation == "unit-shift-swapped":
        shift = not shift
    if mutation == "alternate-ignored":
        alt = False
    return (0xA if alt else 7) if shift else (9 if alt else 6)


def right_code(flags, at_unit):
    """The command code for the right button (flag 2)."""
    shift, alt = bool(flags & 0x20), bool(flags & 8)
    if not at_unit:
        return 0xF if alt else 0xC
    if mutation == "alternate-ignored":
        alt = False
    return (0x11 if alt else 0xE) if shift else (0x10 if alt else 0xD)


def model(flags, at_unit, skill, unit_type, unit_id, x, y, gate_ok):
    """The FUN_00481030 calls (ECX code, EDX skill word, three stack words) for a record."""
    if gate_ok == (mutation == "gate-reversed"):
        return []
    if at_unit:
        words = (unit_type if mutation != "null-unit-type" else 0, unit_id, 0)
    else:
        words = (x, y, 0)
    calls = []
    buttons = [(1, left_code), (2, right_code)]
    if mutation == "right-before-left":
        buttons.reverse()
    if mutation == "left-only":
        buttons = buttons[:1]
    for bit, code in buttons:
        if flags & bit:
            calls.append((code(flags, at_unit), skill, words))
    return calls


def run(exe):
    """Every flag combination at a unit, no unit and a spot; returns the summary line."""
    machine = emu.Emu(str(exe))
    rng = random.Random(0x461700)
    gate = {"count": 0, "limit": 0}
    log = []
    machine.hook(0x468740, lambda current: gate["count"], 0)
    machine.hook(0x454970, lambda current: gate["limit"], 0)

    def send(current):
        """FUN_00481030 (ECX code, EDX skill, three words)."""
        log.append((current.mu.reg_read(uc.UC_X86_REG_ECX), current.mu.reg_read(uc.UC_X86_REG_EDX),
                    tuple(current.arg(index) for index in range(3))))
        return 0

    machine.hook(0x481030, send, 3)
    cases = 0
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    for flags_low, shift, alt, at_unit, has_unit, gate_ok in itertools.product(range(4), (0, 1), (0, 1), (1, 0), (1, 0), (1, 0)):
        for _ in range(3):
            flags = flags_low | (0x20 if shift else 0) | (8 if alt else 0) | (rng.getrandbits(8) << 8 & 0xF00)
            record = machine.alloc(0x40)
            unit = machine.alloc(0x40) if has_unit else 0
            unit_type, unit_id = (rng.randrange(0, 7), rng.getrandbits(32)) if has_unit else (6, 0xFFFFFFFF)
            if has_unit:
                machine.w32(unit, unit_type)
                machine.w32(unit + 0xC, unit_id)
            skill, x, y = rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)
            for offset, value in ((0, flags), (4, skill), (8, unit), (0xC, x), (0x10, y)):
                machine.w32(record + offset, value)
            gate["count"] = rng.randrange(0, 50)
            gate["limit"] = gate["count"] + (rng.randrange(0, 5) if gate_ok else -rng.randrange(1, 5))
            log.clear()
            machine.call(0x461700, at_unit, regs={"esi": record}, limit=2000)
            popped = (machine.mu.reg_read(uc.UC_X86_REG_ESP) - (top - 8)) // 4
            want = model(flags, at_unit, skill, unit_type, unit_id, x, y, gate_ok)
            if log != want or popped != 2:
                raise AssertionError(f"flags {flags:#x} unit-target {at_unit} unit {has_unit} gate {gate_ok}: sent {log}, model {want}, popped {popped}")
            cases += 1
    return f"ok: {cases} attack commands over every flag combination"


def main():
    """Entry point: run the checks, or every mutation and require each to fail."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()
