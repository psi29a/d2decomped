# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the player dead/loot arm and active-skill cursor exceptions.

Run from tools/emu after setting D2_MPQ_DIR:
    .venv/bin/python player_cursor_exceptions.py --mutation-check

FUN_00465c60, FUN_0047a4f0, FUN_004dc440, FUN_00464ec0,
FUN_00464f70 and FUN_0047a520 run natively on synthetic units, Skills rows
and roster records. Room, hand-skill and roster-record lookup boundaries are
hooked; no MPQ data is needed.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
INTERACT = 0x465C60
TARGET_PET = 0x464EC0
TARGET_ALLY = 0x464F70
LEFT_SKILL = 0x620190
RIGHT_SKILL = 0x6201D0
SKILL_ID = 0x643CE0
ROSTER_RECORD = 0x4792E0
OWNER_ID = 0x479150
TABLES_GLOBAL = 0x744304
SKILL_SIZE = 0x23C
CALL_LIMIT = 3000
mutation = None


class Mismatch(Exception):
    """The native function disagreed with the independent model."""


class Game:
    """Two players, one pet and their stock roster relationships."""

    def __init__(self, exe):
        self.machine = machine = emu.Emu(exe=exe)
        self.local_id, self.other_id, self.pet_id = 101, 202, 303
        self.local = machine.alloc(0x200)
        self.other = machine.alloc(0x200)
        self.pet = machine.alloc(0x200)
        self.room = machine.alloc(0x100)
        self.records = {unit_id: machine.alloc(0x80) for unit_id in (self.local_id, self.other_id)}
        self.rosters = {unit_id: machine.alloc(0x20) for unit_id in (self.local_id, self.other_id)}
        self.relations = {unit_id: machine.alloc(0x20) for unit_id in (self.local_id, self.other_id)}
        for unit, unit_type, unit_id in ((self.local, 0, self.local_id), (self.other, 0, self.other_id), (self.pet, 1, self.pet_id)):
            machine.w32(unit, unit_type)
            machine.w32(unit + 0xC, unit_id)
            machine.w32(unit + 0x10, 1)
            machine.w32(unit + 0xC4, 2)
            machine.w32(unit + 0xC8, 0x80)
        for unit_id, toward in ((self.local_id, self.other_id), (self.other_id, self.local_id)):
            record, roster, relation = self.records[unit_id], self.rosters[unit_id], self.relations[unit_id]
            machine.w32(record + 0x34, roster)
            machine.w32(roster, relation)
            machine.w32(relation, toward)
            machine.w32(relation + 8, 0)
            machine.w32(record + 0x10, unit_id)
            machine.w32(record + 0x22, 0xFFFF)
        self.tables = machine.alloc(0xC00)
        self.skills = machine.alloc(3 * SKILL_SIZE)
        machine.w32(TABLES_GLOBAL, self.tables)
        machine.w32(self.tables + 0xB98, self.skills)
        machine.w32(self.tables + 0xBA0, 3)
        self.case = {}
        machine.hook(0x620BB0, lambda current: self.room, 1)
        machine.hook(0x61AB00, lambda current: int(self.case.get("town", False)), 1)
        machine.hook(ROSTER_RECORD, self.roster_record, 0)
        machine.hook(OWNER_ID, lambda current: self.case.get("pet_owner", 0xFFFFFFFF), 0)
        machine.hook(LEFT_SKILL, lambda current: 1, 1)
        machine.hook(RIGHT_SKILL, lambda current: 0, 1)
        machine.hook(SKILL_ID, lambda current: self.case.get("skill", 0), 3)

    def roster_record(self, current):
        """Return the roster record selected by the id in EDX."""
        return self.records.get(current.mu.reg_read(UC_X86_REG_EDX), 0)

    def dead_interact(self, town, hostile, corpse_owner, loot):
        """Run FUN_00465c60 on the dead other player."""
        self.case = {"town": town, "pet_owner": 0xFFFFFFFF, "skill": 0}
        machine = self.machine
        machine.w32(self.other + 0x10, 0)
        machine.w32(self.relations[self.local_id] + 4, 8 if hostile else 0)
        machine.w32(self.relations[self.other_id] + 4, 1 if loot else 0)
        machine.w32(self.records[self.other_id] + 0x10, self.local_id if corpse_owner else self.other_id)
        return int(machine.call(INTERACT, ecx=self.local, edx=self.other, limit=CALL_LIMIT) != 0)

    def pet_exception(self, target_pet, owner):
        """Run FUN_00464ec0 with or without TargetPet on the left hand."""
        self.case = {"town": False, "pet_owner": owner, "skill": 1}
        self.machine.mu.mem_write(self.skills + SKILL_SIZE + 7, bytes([2 if target_pet else 0]))
        return int(self.machine.call(TARGET_PET, ecx=self.local, edx=self.pet, limit=CALL_LIMIT) != 0)

    def ally_exception(self, target_ally, local_party, other_party):
        """Run FUN_00464f70 on the other player and the two party slots."""
        self.case = {"town": False, "pet_owner": 0xFFFFFFFF, "skill": 2}
        self.machine.mu.mem_write(self.skills + 2 * SKILL_SIZE + 7, bytes([4 if target_ally else 0]))
        self.machine.w32(self.records[self.local_id] + 0x22, local_party)
        self.machine.w32(self.records[self.other_id] + 0x22, other_party)
        return int(self.machine.call(TARGET_ALLY, ecx=self.local, edx=self.other, limit=CALL_LIMIT) != 0)


def model_dead(town, hostile, corpse_owner, loot):
    """FUN_00465c60's player-on-dead-player branch."""
    allowed_loot = corpse_owner or loot
    if mutation == "dead_ignores_loot":
        allowed_loot = corpse_owner
    return int(allowed_loot or (hostile and not town))


def model_pet(target_pet, owner, self_id):
    """FUN_00464ec0's TargetPet/owner result."""
    if mutation == "pet_any_owner":
        return int(target_pet and owner != 0xFFFFFFFF)
    return int(target_pet and owner == self_id)


def model_ally(target_ally, local_party, other_party):
    """FUN_00464f70's nonempty equal-party result."""
    if mutation == "ally_empty_party":
        return int(target_ally and local_party == other_party)
    return int(target_ally and local_party == other_party and local_party != 0xFFFF)


def check(exe):
    """Compare every relevant relation, owner and party combination."""
    game = Game(exe)
    differences, count = [], 0
    for town in (False, True):
        for hostile in (False, True):
            for corpse_owner in (False, True):
                for loot in (False, True):
                    got, want = game.dead_interact(town, hostile, corpse_owner, loot), model_dead(town, hostile, corpse_owner, loot)
                    if got != want:
                        differences.append(("dead", town, hostile, corpse_owner, loot, got, want))
                    count += 1
    for target_pet in (False, True):
        for owner in (0xFFFFFFFF, game.local_id, game.other_id):
            got, want = game.pet_exception(target_pet, owner), model_pet(target_pet, owner, game.local_id)
            if got != want:
                differences.append(("pet", target_pet, owner, got, want))
            count += 1
    for target_ally in (False, True):
        for local_party in (3, 4, 0xFFFF):
            for other_party in (3, 4, 0xFFFF):
                got, want = game.ally_exception(target_ally, local_party, other_party), model_ally(target_ally, local_party, other_party)
                if got != want:
                    differences.append(("ally", target_ally, local_party, other_party, got, want))
                count += 1
    if differences:
        raise Mismatch(f"{len(differences)} of {count} player cursor cases differ; first {differences[0]}")
    return count


def main():
    """Run the positive comparison and requested negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    if hashlib.sha256(Path(exe).read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("player_cursor_exceptions.py requires stock game.exe 1.14d (1.14.3.71)")
    count = check(exe)
    print(f"ok: {count} FUN_00465c60 / FUN_00464ec0 / FUN_00464f70 player cursor cases")
    if options.mutation_check:
        global mutation
        for defect in ("dead_ignores_loot", "pet_any_owner", "ally_empty_party"):
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
