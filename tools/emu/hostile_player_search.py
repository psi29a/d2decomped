# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00465c60's player-versus-player branch and FUN_004dc440.

Run from tools/emu after setting D2_MPQ_DIR:
    uv run python hostile_player_search.py --mutation-check

Two synthetic live type-0 units (mode 1, so FUN_00464820's dead-player arm
stays out) stand outside town. FUN_00465c60, FUN_004dc440 and FUN_0047a310 run
natively; the room/level getters, the pet-owner lookup FUN_00479150 and the
roster lookup FUN_004792e0 (a record only for the local id, its +0x34 the
relation list) are hooked. The model is d2d's gate: the other player is
targetable exactly when the local player's relation node for it has bit 8.
"""
import argparse

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

INTERACT = 0x465C60
RELATION_FLAG = 0x4DC440
LIMIT = 2000   # instructions a call may take; both return in far fewer


class Game:
    """Native player-interaction functions over two units and one relation."""

    def __init__(self, record_id=101):
        """Lay out the units, the roster record (for `record_id`) and the hooks."""
        self.record_id = record_id
        self.machine = machine = emu.Emu()
        self.local = machine.alloc(0x200)
        self.other = machine.alloc(0x200)
        self.room = machine.alloc(0x100)
        self.roster = machine.alloc(0x20)
        self.relation = machine.alloc(0x20)
        self.record = machine.alloc(0x100)
        for unit, unit_id in ((self.local, 101), (self.other, 202)):
            machine.w32(unit, 0)                # type 0: a player
            machine.w32(unit + 0xC, unit_id)
            machine.w32(unit + 0x10, 1)         # mode 1 (neutral): alive
        machine.w32(self.record + 0x34, self.roster)
        machine.w32(self.roster, self.relation)
        machine.w32(self.relation, 202)
        machine.w32(self.relation + 8, 0)
        machine.hook(0x620BB0, lambda current: self.room, 1)
        machine.hook(0x61AB00, lambda current: 0, 1)          # not a town
        machine.hook(0x479150, lambda current: 0xFFFFFFFF, 0)  # no pet owner
        machine.hook(0x4792E0, self.roster_record, 0)

    def roster_record(self, current):
        """FUN_004792e0: the roster record of the id in EDX (one id has it, the local by default)."""
        return self.record if current.mu.reg_read(UC_X86_REG_EDX) == self.record_id else 0

    def run(self, flags):
        """Return the native relation mask and interaction answer."""
        machine = self.machine
        machine.w32(self.relation + 4, flags)
        relation = self.finish(machine.call(RELATION_FLAG, 8, ecx=101, edx=202, limit=LIMIT), "FUN_004dc440")
        interact = self.finish(machine.call(INTERACT, ecx=self.local, edx=self.other, limit=LIMIT), "FUN_00465c60")
        return relation, interact

    def finish(self, result, name):
        """The call's EAX, once it returned (EIP at emu.RET_MAGIC) inside LIMIT instructions."""
        eip = self.machine.mu.reg_read(UC_X86_REG_EIP)
        if eip != emu.RET_MAGIC:
            raise SystemExit(f"FAIL: {name} did not return within {LIMIT} instructions (eip {eip:08x})")
        return result


def model(flags, mutation=None):
    """d2d's player gate, with negative controls for mutation checking."""
    if mutation == "always_not_hostile":
        return 0
    if mutation == "loot_bit":
        return int((flags & 1) != 0)
    return int((flags & 8) != 0)


def check(game, mutation=None):
    """Compare all relation-bit combinations and return the difference count."""
    differences = 0
    for flags in range(16):
        relation, interact = game.run(flags)
        expected = model(flags, mutation)
        if int(relation != 0) != expected or int(interact != 0) != expected:
            differences += 1
    return differences


def main():
    """Check every relation mask, then (--mutation-check) that each mutant is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    game = Game()
    differences = check(game)
    if differences:
        raise SystemExit(f"FAIL: {differences} relation masks differ")
    print("ok: 16 FUN_00465c60 player cases through native FUN_004dc440")
    if args.mutation_check:
        for mutation in ("always_not_hostile", "loot_bit"):
            caught = check(game, mutation)
            if not caught:
                raise SystemExit(f"mutation {mutation}: NOT caught")
            print(f"mutation {mutation}: caught ({caught} differ)")
        caught = check(Game(record_id=202))
        if not caught:
            raise SystemExit("mutation other_id_record: NOT caught")
        print(f"mutation other_id_record: caught ({caught} differ)")


if __name__ == "__main__":
    main()
