# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the skill picker's key label FUN_004a8df0 in stock game.exe 1.14d.

    uv run python skill_key_label.py --exe /path/to/game.exe [--mutation-check]

FUN_004a8df0 (ECX = the action id, EDX = the y to draw at, two stack words that
go to the text draw; `ret 8`) draws the key bound to an action. It asks the key
table (FUN_00469aa0, run for real over a random table at 0x7a6f90: 10-byte
entries of action int, key word, slot int) for slot 1 only, so an action bound
only in slot 0 draws nothing (docs/research/re/bugs.md 48). The name
(FUN_0046a530, also slot 1), the string width (FUN_00501820) and the draw
(FUN_00502320) are hooked; the copy FUN_00526790 (wcsncpy with zero padding, no
terminator at 16 characters) and the length FUN_00526800 run for real. A name wider than 0x28 is copied into the 16-character buffer at 0x7c0810 and cut a
character at a time until it fits; a width over 0xd moves y left by the excess.
The buffer is followed by the init flag at 0x7c0830, so a 16-character copy has no
terminator: the string runs on into the flag word, and the cut's first zero lands on
the flag (it is re-initialised on the next call). The model simulates the memory.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("slot-0", "either-slot", "no-trim", "trim-limit-0x30", "no-shift", "shift-wrong-sign", "flag-never-clears", "key-0-is-none")
mutation = None
TABLE, ENTRIES = 0x7A6F90, (0x7A7409 - 0x7A6F96) // 10 + 1
BUFFER, FLAG = 0x7C0810, 0x7C0830
WIDTH_PER_CHAR = 7


def wide(machine, address):
    """The UTF-16 string at address as a Python string."""
    out = ""
    while True:
        unit = struct.unpack("<H", machine.read(address + 2 * len(out), 2))[0]
        if unit == 0:
            return out
        out += chr(unit)


class Rig:
    """Hooks and the log of the draw call."""

    def __init__(self, machine):
        """Hook the name, width, copy, length and draw calls."""
        self.m = machine
        self.log = []
        self.name = 0
        machine.hook(0x46A530, self._name, 0)
        machine.hook(0x501820, lambda current: WIDTH_PER_CHAR * len(wide(machine, self.reg("ECX"))), 0)
        machine.hook(0x502320, self._draw, 3)

    def reg(self, name):
        """A register at the hooked call."""
        return self.m.mu.reg_read(getattr(uc, "UC_X86_REG_" + name))

    def _name(self, current):
        """FUN_0046a530 (ECX action, EDX slot): log and return the scripted name."""
        self.log.append(("name", self.reg("ECX"), self.reg("EDX")))
        return self.name

    def _draw(self, current):
        """FUN_00502320 (ECX text, EDX y; three words): log what is drawn."""
        self.log.append(("draw", wide(self.m, self.reg("ECX")), self.reg("ECX"), self.reg("EDX"),
                         (current.arg(0), current.arg(1), current.arg(2))))
        return 0


def key_of(table, action, slot):
    """FUN_00469aa0: the first entry with this action and slot; its key word, or 0xffff."""
    for entry_action, key, entry_slot in table:
        if entry_action == action and entry_slot == slot:
            return key
    return 0xFFFF


def run(exe):
    """Random key tables and names through FUN_004a8df0; returns the summary line."""
    machine = emu.Emu(str(exe))
    rig = Rig(machine)
    rng = random.Random(0x4A8DF0)
    cases = 0
    for case in range(600):
        action = rng.randrange(1, 200)
        table = [(rng.randrange(1, 200), rng.getrandbits(16), rng.choice((0, 1))) for _ in range(ENTRIES)]
        mode = case % 5
        for index, entry in enumerate(table):
            if entry[0] == action:
                table[index] = (rng.randrange(300, 400), entry[1], entry[2])
        if mode in (1, 3):
            table[rng.randrange(ENTRIES)] = (action, rng.randrange(0, 0xFFFF), 0)
        if mode in (2, 3):
            table[rng.randrange(ENTRIES)] = (action, rng.choice((0, rng.randrange(0, 0xFFFF))), 1)
        if mode == 4:
            table[rng.randrange(ENTRIES)] = (action, 0xFFFF, 1)
        for index, (entry_action, key, slot) in enumerate(table):
            machine.mu.mem_write(TABLE + 10 * index, struct.pack("<IHI", entry_action, key, slot))
        text = "".join(rng.choice("ABCDEFGHIJ") for _ in range(rng.randrange(0, 21)))
        pointer = machine.put(text.encode("utf-16-le") + b"\0\0")
        rig.name, rig.log = pointer, []
        initialised = rng.getrandbits(1)
        machine.w32(FLAG, initialised)
        machine.mu.mem_write(BUFFER, b"\x41\0" * 16)
        before = machine.read(BUFFER, 72)
        y, first, second = rng.randrange(0, 600), rng.getrandbits(32), rng.getrandbits(32)
        top = emu.STACK + emu.STACK_SIZE - 0x1000
        machine.call(0x4A8DF0, first, second, ecx=action, edx=y, limit=20000)
        popped = (machine.mu.reg_read(uc.UC_X86_REG_ESP) - (top - 12)) // 4
        slot = 0 if mutation == "slot-0" else 1
        key = key_of(table, action, slot)
        if mutation == "either-slot" and key == 0xFFFF:
            key = key_of(table, action, 0)
        if mutation == "key-0-is-none" and key == 0:
            key = 0xFFFF
        if key == 0xFFFF:
            if rig.log or popped != 3 or machine.r32(FLAG) != initialised:
                raise AssertionError(f"case {case}: no key but calls {rig.log}, popped {popped}")
            cases += 1
            continue
        names = [entry for entry in rig.log if entry[0] == "name"]
        if names != [("name", action, 1 if mutation != "slot-0" else 0)] and mutation not in ("slot-0", "either-slot"):
            raise AssertionError(f"case {case}: name asked for {names}")
        # The buffer, its init flag and the word after it, as units; the model runs the C on them.
        units = list(struct.unpack("<36H", before))
        if not initialised:
            units[:16] = [0] * 16
            units[16] = 1
        string_units = [ord(char) for char in text]
        width = lambda chars: WIDTH_PER_CHAR * len(chars)
        limit = 0x30 if mutation == "trim-limit-0x30" else 0x28
        copied = width(string_units) > limit and mutation != "no-trim"
        if copied:
            chunk = string_units[:16] + [0] * max(0, 16 - len(string_units))
            units[:16] = chunk[:16]
            length = 0
            while units[length]:
                length += 1
            index = length - 1
            while True:
                units[index] = 0
                measured = 0
                while units[measured]:
                    measured += 1
                if width(units[:measured]) <= limit:
                    break
                index -= 1
            drawn_units = units[:measured]
        else:
            drawn_units = string_units
        final_width = width(drawn_units)
        want_y = y
        if final_width > 0xD and mutation != "no-shift":
            want_y = (y + (0xD - final_width if mutation != "shift-wrong-sign" else final_width - 0xD)) & 0xFFFFFFFF
        wanted = "".join(chr(unit) for unit in drawn_units)
        draws = [entry for entry in rig.log if entry[0] == "draw"]
        if len(draws) != 1 or draws[0][1] != wanted or draws[0][3] != want_y or draws[0][4] != (first, second, 0) or \
                (draws[0][2] == BUFFER) != copied or popped != 3:
            raise AssertionError(f"case {case}: text {text!r} drawn {draws}; model {wanted!r} at y {want_y:#x}, copied {copied}, popped {popped}")
        after = list(struct.unpack("<36H", machine.read(BUFFER, 72)))
        if mutation == "flag-never-clears":
            units[16] = 1
        if after != units:
            raise AssertionError(f"case {case}: buffer and flag {after}, model {units}")
        cases += 1
    return f"ok: {cases} key-label scenarios"


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
