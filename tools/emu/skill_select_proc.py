# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00464310 and all six Skills.txt SelectProc callbacks.

Run from tools/emu after setting D2_MPQ_DIR:
    .venv/bin/python skill_select_proc.py --mutation-check

The skill and MonStats tables are synthetic. FUN_00464310, FUN_004642b0 and
the callback table at 0x711f34 run natively; only the hand-skill accessors,
the corpse-hidden predicate, MonStats2 bit lookup and the proc-3 mode gate are
hooked to the case. This needs game.exe only, not MPQs.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
SELECT_ACTIVE = 0x464310
LEFT_SKILL = 0x620190
RIGHT_SKILL = 0x6201D0
SKILL_ID = 0x643CE0
HIDDEN = 0x63A770
MONSTATS2_FLAG = 0x4638A0
SWITCHABLE_MODE = 0x623470
MONSTER_DATA_FLAG = 0x4AC7E0
TABLES_GLOBAL = 0x744304
SKILL_SIZE = 0x23C
MONSTATS_SIZE = 0x1A8
CALL_LIMIT = 3000
mutation = None


class Mismatch(Exception):
    """The stock result disagreed with the independent callback model."""


class Game:
    """Synthetic Skills and MonStats records around the native select runner."""

    def __init__(self, exe):
        self.machine = machine = emu.Emu(exe=exe)
        self.tables = machine.alloc(0xC00)
        self.skills = machine.alloc(7 * SKILL_SIZE)
        self.monstats = machine.alloc(MONSTATS_SIZE)
        self.player = machine.alloc(0x200)
        self.target = machine.alloc(0x200)
        self.monster_data = machine.alloc(0x40)
        machine.w32(TABLES_GLOBAL, self.tables)
        machine.w32(self.tables + 0xB98, self.skills)
        machine.w32(self.tables + 0xBA0, 7)
        machine.w32(self.tables + 0xA78, self.monstats)
        machine.w32(self.tables + 0xA80, 1)
        for proc in range(1, 7):
            machine.mu.mem_write(self.skills + proc * SKILL_SIZE + 0x15, bytes([proc]))
        machine.w32(self.target, 1)
        machine.w32(self.target + 4, 0)
        machine.w32(self.target + 0x10, 0xC)
        machine.w32(self.target + 0x14, self.monster_data)
        self.case = {}
        machine.hook(LEFT_SKILL, lambda current: int(self.case["left"] != 0), 1)
        machine.hook(RIGHT_SKILL, lambda current: int(self.case["right"] != 0), 1)
        machine.hook(SKILL_ID, self.skill_id, 3)
        machine.hook(HIDDEN, lambda current: int(self.case["hidden"]), 1)
        machine.hook(MONSTATS2_FLAG, self.monstats2_flag, 0)
        machine.hook(SWITCHABLE_MODE, lambda current: int(self.case["switchable"]), 0)

    def skill_id(self, current):
        """Return the configured left or right id from the synthetic node token."""
        return self.case["left"] if current.arg(0) == 1 else self.case["right"]

    def monstats2_flag(self, current):
        """FUN_004638a0: expose corpseSel (7) and soft (12) from the case."""
        bit = current.mu.reg_read(UC_X86_REG_EDX)
        return int(self.case["corpse_sel"] if bit == 7 else self.case["soft"] if bit == 12 else False)

    def run(self, case):
        """Run FUN_00464310 with the case's active hands and target fields."""
        self.case = case
        machine = self.machine
        machine.w32(self.target, case["type"])
        machine.w32(self.target + 0x10, case["mode"])
        machine.w32(self.monstats + 0x32, case["velocity"])
        machine.w32(self.monster_data + 0x16, case["data_flags"])
        # The hand hooks return tokens 1 and 2; SKILL_ID tells them apart.
        machine.hook(LEFT_SKILL, lambda current: 1 if self.case["left"] else 0, 1)
        machine.hook(RIGHT_SKILL, lambda current: 2 if self.case["right"] else 0, 1)
        return int(machine.call(SELECT_ACTIVE, regs={"esi": self.target, "edi": self.player}, limit=CALL_LIMIT) != 0)


def proc_takes(proc, case):
    """Independent model of callbacks 1..6."""
    corpse = case["type"] == 1 and case["mode"] == 0xC and not case["hidden"] and case["corpse_sel"]
    if proc in (1, 5):
        return corpse
    if proc == 2:
        return corpse and (case["velocity"] != 0 or mutation == "ignore_velocity")
    if proc == 3:
        flags_clear = (case["data_flags"] & 0xA) == 0 or mutation == "ignore_boss"
        return corpse and case["velocity"] != 0 and case["switchable"] and flags_clear
    if proc in (4, 6):
        return corpse and (case["soft"] or mutation == "ignore_soft")
    return False


def model(case):
    """FUN_00464310's left-first, then right selection."""
    if case["type"] != 1:
        return 0
    if mutation == "ignore_right":
        return int(proc_takes(case["left"], case))
    return int(proc_takes(case["left"], case) or proc_takes(case["right"], case))


def cases():
    """Cover every proc and each significant callback branch."""
    base = {"left": 0, "right": 0, "type": 1, "mode": 0xC, "hidden": False,
            "corpse_sel": True, "velocity": 8, "soft": True,
            "switchable": True, "data_flags": 0}
    out = []
    for proc in range(1, 7):
        for change in ({}, {"left": proc}, {"right": proc}, {"left": proc, "type": 0},
                       {"left": proc, "mode": 1}, {"left": proc, "hidden": True},
                       {"left": proc, "corpse_sel": False}, {"left": proc, "velocity": 0},
                       {"left": proc, "soft": False}, {"left": proc, "switchable": False},
                       {"left": proc, "data_flags": 2}, {"left": proc, "data_flags": 8}):
            out.append(base | change)
    out.append(base | {"left": 2, "right": 1, "velocity": 0})
    return out


def check(exe):
    """Compare the native runner and callbacks with the model."""
    game = Game(exe)
    differences = []
    all_cases = cases()
    for case in all_cases:
        got, want = game.run(case), model(case)
        if got != want:
            differences.append((case, got, want))
    if differences:
        raise Mismatch(f"{len(differences)} of {len(all_cases)} select-proc cases differ; first {differences[0]}")
    return len(all_cases)


def main():
    """Run the positive comparison and requested negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    if hashlib.sha256(Path(exe).read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("skill_select_proc.py requires stock game.exe 1.14d (1.14.3.71)")
    count = check(exe)
    print(f"ok: {count} FUN_00464310 cases over all six SelectProc callbacks")
    if options.mutation_check:
        global mutation
        for defect in ("ignore_velocity", "ignore_boss", "ignore_soft", "ignore_right"):
            mutation = defect
            try:
                check(exe)
            except Mismatch:
                print(f"mutation {defect}: failed as required")
            else:
                raise SystemExit(f"mutation {defect}: NOT caught")
        mutation = None


if __name__ == "__main__":
    main()
