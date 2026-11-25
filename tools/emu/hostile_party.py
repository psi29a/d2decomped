# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe's hostility party branch on synthetic players.

FUN_005a5dc0, its callback FUN_005a5d50, and FUN_005a5d20 run for real.
Party lookup, party iteration, level stats, the relation setter and packet
senders are hooked. The checks cover the no-party pair, target-party walk,
level-9 member gate, relation directions, and same-party removal.

Run from tools/emu:
    .venv/bin/python hostile_party.py --mutation-check
"""
import argparse

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu


class Rig:
    """Synthetic declarer, target and party members around FUN_005a5dc0 / FUN_005a5d50 / FUN_005a5d20."""
    def __init__(self):
        """Allocate the units with their parties and levels; hook the callees."""
        self.machine = machine = emu.Emu()
        self.game = machine.alloc(0x100)
        self.declarer = self.unit(10)
        self.target = self.unit(20)
        self.member = self.unit(30)
        self.low_member = self.unit(40)
        self.party = {self.declarer: 3, self.target: 7, self.member: 7, self.low_member: 7}
        self.level = {self.declarer: 9, self.target: 9, self.member: 9, self.low_member: 8}
        self.sets = []
        self.walks = []
        self.removed = []

        def party(current):
            """Hook FUN_00554630: the unit's party id, 0xffff for none."""
            return self.party.get(current.mu.reg_read(UC_X86_REG_ECX), 0xFFFF)

        def level(current):
            """Hook FUN_006253b0 (stat 0xc): the unit's level."""
            assert current.arg(1) == 0xC and current.arg(2) == 0
            return self.level[current.arg(0)]

        def relation_set(current):
            """Hook FUN_005a60e0: record the (from, to) hostile pair set."""
            self.sets.append((current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
            return 1

        def walk(current):
            """Hook FUN_00540510: record the party walk (party, callback, arg)."""
            self.walks.append((current.mu.reg_read(UC_X86_REG_EDX), current.arg(0), current.arg(1)))
            return 0

        def remove(current):
            """Hook FUN_005407a0: record the player taken out of the party."""
            self.removed.append(current.mu.reg_read(UC_X86_REG_EDX))
            return 0

        machine.hook(0x554630, party, 0)
        machine.hook(0x6253B0, level, 3)
        machine.hook(0x5A60E0, relation_set, 1)
        machine.hook(0x540510, walk, 2)
        machine.hook(0x5407A0, remove, 0)
        machine.stub(0, {0x5531C0: 1, 0x5A5690: 2})

    def unit(self, unit_id):
        """Allocate a player unit (type 0) with this id."""
        unit = self.machine.alloc(0x100)
        self.machine.w32(unit, 0)
        self.machine.w32(unit + 0xC, unit_id)
        return unit

    def clear(self):
        """Forget what the hooks recorded."""
        self.sets.clear()
        self.walks.clear()
        self.removed.clear()


def observed():
    """Run game.exe's party-hostility functions over the cases; returns the rig and what they did."""
    rig = Rig()
    result = {}

    rig.party[rig.target] = 0xFFFF
    rig.machine.call(0x5A5DC0, rig.game, regs={"eax": rig.declarer, "edi": rig.target})
    result["solo"] = tuple(rig.sets)

    rig.clear()
    rig.party[rig.target] = 7
    rig.machine.call(0x5A5DC0, rig.game, regs={"eax": rig.declarer, "edi": rig.target})
    result["party_walk"] = tuple(rig.walks)

    rig.clear()
    rig.machine.call(0x5A5D50, rig.declarer, ecx=rig.game, edx=rig.member)
    result["member"] = tuple(rig.sets)
    rig.clear()
    rig.machine.call(0x5A5D50, rig.declarer, ecx=rig.game, edx=rig.low_member)
    result["low_member"] = tuple(rig.sets)
    rig.clear()
    rig.machine.call(0x5A5D50, rig.declarer, ecx=rig.game, edx=rig.declarer)
    result["self_member"] = tuple(rig.sets)

    rig.clear()
    rig.machine.call(0x5A5D20, 0, 0, rig.machine.alloc(4), ecx=rig.game, edx=rig.declarer)
    result["leave_walk"] = tuple(rig.walks)
    result["removed"] = tuple(rig.removed)
    return rig, result


def expected(rig, mutation=None):
    """What d2d's model says each case does; `mutation` breaks it on purpose."""
    solo = ((rig.declarer, rig.target), (rig.target, rig.declarer))
    member = ((rig.declarer, rig.member), (rig.member, rig.declarer))
    if mutation == "reverse-pair":
        solo = tuple(reversed(solo))
    if mutation == "level-8-included":
        low_member = ((rig.declarer, rig.low_member), (rig.low_member, rig.declarer))
    else:
        low_member = ()
    removed = () if mutation == "keep-same-party" else (rig.declarer,)
    callback = 0x5A5D50
    notify = 0x5A5C70
    return {
        "solo": solo,
        "party_walk": ((7, callback, rig.declarer),),
        "member": member,
        "low_member": low_member,
        "self_member": (),
        "leave_walk": ((3, notify, rig.declarer),),
        "removed": removed,
    }


def main():
    """Compare game.exe with the model; --mutation-check checks each broken model is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig, stock = observed()
    assert stock == expected(rig), (stock, expected(rig))
    if args.mutation_check:
        for mutation in ("reverse-pair", "level-8-included", "keep-same-party"):
            assert stock != expected(rig, mutation), f"negative control escaped: {mutation}"
            print(f"negative control caught: {mutation}")
    print("ok: FUN_005a5dc0 / 005a5d50 / 005a5d20 party hostility")


if __name__ == "__main__":
    main()
