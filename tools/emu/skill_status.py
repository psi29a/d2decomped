# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether a skill can be used now, as game.exe says: FUN_00647960's status.

    uv run python skill_status.py           # every case: `name status`, then ok / FAIL
    uv run python skill_status.py client    # FUN_004610c0 on each status: `skill status go skill_used voice`

FUN_00647960(unit, entry) answers 0 (usable) or why not, in this order:
3 (no row, or the row's InGame flag clear), 7 (level 0), 6 (an aura),
5 (a passive), 2 (FUN_00647640's own gate: scrolls / tomes, Throw, Left
Hand Throw / Swing; then the weapon-type gate FUN_00643f80), 1 (mana:
FUN_00647540), 4 (restrict: FUN_00644060), 1 (startmana: FUN_006440f0),
2 (an item's charges: FUN_00647840), 8 (skilldelay: FUN_006478f0).
skills.md "Can the skill go now".

Runs for real (the claim): FUN_00647960, FUN_00647640 (but its inventory
lookups), FUN_00647540, FUN_00644060, FUN_006440f0, FUN_00647840,
FUN_006478f0 and the delay calc FUN_00646ca0 on the real Skills.txt rows.
Hooked (each case's input, not the claim): FUN_006442a0 (the level),
FUN_00643f80 (the weapon-type gate, test_weapon_gate's), FUN_00625480 (the
unit's stats: 6 life, 8 mana, 0xcc an item's charges), FUN_00639df0 (its
states), FUN_0063a510 (a restrict state on: wolf / bear), FUN_0063a400,
FUN_00644180 (no bonus levels), FUN_0063b2c0 / FUN_0063dfd0 / FUN_0063dfa0
(the inventory: the case's item, or none), and for Throw FUN_006235a0 / FUN_0063c9b0 / FUN_0062ba80 as
throw.md's harness does.

`client`: the client's check before a skill is sent, FUN_004610c0 (EDI the
unit, EBX the entry's slot): a status 1, 2 or 4 on a skill with
AttackNoMana (Skills.txt bit 0x1c, +7 & 0x10) puts Attack's entry
(FUN_006439f0(unit, 0)) in the slot and asks again; 0 or 5 goes (1), else
nothing goes and FUN_00461070 voices table 0x711ddc[status] (0x15
needmana, 0x13 impossible, 0 none). Runs for real: FUN_004610c0,
FUN_00644140, FUN_00461070. Hooked: FUN_004d9fc0 (the status: the case's
for the skill, 0 for Attack), FUN_006439f0 (Attack's entry), FUN_004cb9c0
(the voice: recorded).
"""
import sys

from unicorn.x86_const import UC_X86_REG_EDX

RECORD = 0x23c
FIRE_BOLT, INFERNO, METEOR, MIGHT, CRITICAL_STRIKE, THROW, BOOK_OF_TOWNPORTAL = 36, 41, 56, 98, 9, 2, 220
INFERNO_STATE, BLOOD_MANA, SKILLDELAY = 0xc, 0x72, 0x79

# name, skill, inputs, game.exe's status. Inputs not given: level 1, the weapon gate passes, 100 mana and life,
# no state, no restrict state, the character's own skill (item -1), an inventory.
CASES = [
    ("usable", FIRE_BOLT, {}, 0),
    ("no_row", None, {}, 3),
    ("level_0", FIRE_BOLT, {"level": 0}, 7),
    ("aura", MIGHT, {}, 6),
    ("aura_level_0", MIGHT, {"level": 0}, 7),
    ("passive", CRITICAL_STRIKE, {}, 5),
    ("passive_no_weapon", CRITICAL_STRIKE, {"weapon": 0}, 5),
    ("no_weapon", FIRE_BOLT, {"weapon": 0}, 2),
    ("no_weapon_no_mana", FIRE_BOLT, {"weapon": 0, "mana": 0}, 2),
    ("no_mana", FIRE_BOLT, {"mana": 0}, 1),
    ("no_mana_restricted", FIRE_BOLT, {"mana": 0, "restricted": 1}, 1),
    ("restricted", FIRE_BOLT, {"restricted": 1}, 4),
    ("blood_mana_life", FIRE_BOLT, {"mana": 0, "states": {BLOOD_MANA}}, 0),
    ("blood_mana_no_life", FIRE_BOLT, {"life": 0, "states": {BLOOD_MANA}}, 1),
    ("item_charges", FIRE_BOLT, {"item": 7, "charges": 3, "mana": 0}, 0),
    ("item_no_charges", FIRE_BOLT, {"item": 7, "charges": 0}, 1),
    ("item_held_charges", FIRE_BOLT, {"item": 7, "charges": 3, "mana": 0, "held": 1, "stat_charges": 3}, 0),
    ("item_held_no_charges", FIRE_BOLT, {"item": 7, "charges": 3, "mana": 0, "held": 1, "stat_charges": 0}, 2),
    ("scroll_none_left", BOOK_OF_TOWNPORTAL, {"quantity": 0}, 2),
    ("scroll_left", BOOK_OF_TOWNPORTAL, {"quantity": 3}, 0),
    ("scroll_no_inventory", BOOK_OF_TOWNPORTAL, {"quantity": 3, "inventory": 0}, 2),
    ("inferno_start_5", INFERNO, {"mana": 5 * 256}, 1),
    ("inferno_start_6", INFERNO, {"mana": 6 * 256}, 0),
    ("inferno_burning_5", INFERNO, {"mana": 5 * 256, "states": {INFERNO_STATE}}, 0),
    ("delay_meteor", METEOR, {"states": {SKILLDELAY}}, 8),
    ("delay_meteor_off", METEOR, {}, 0),
    ("delay_fire_bolt", FIRE_BOLT, {"states": {SKILLDELAY}}, 0),
    ("delay_meteor_no_mana", METEOR, {"mana": 0, "states": {SKILLDELAY}}, 1),
    ("throw_throwable", THROW, {"throwable": 1}, 0),
    ("throw_throwable_weapon_gate", THROW, {"throwable": 1, "weapon": 0}, 2),
    ("throw_not_throwable", THROW, {"throwable": 0}, 2),
]


class Oracle:
    """game.exe booted with FUN_00647960's inputs hooked to the current case."""

    def __init__(self):
        """Boot game.exe (skill_tips.boot), allocate the unit, entry and item, and hook the inputs."""
        import skill_tips
        self.e = e = skill_tips.boot()
        tables = e.r32(0x744304)
        self.skills = e.r32(tables + 0xb98)
        self.unit, self.entry = e.alloc(0x200), e.alloc(0x40)
        self.inventory, self.item = e.alloc(0x100), e.alloc(0x100)
        e.view("unit", self.unit).type = 0                       # a player
        self.case = {}
        get = lambda key, default: self.case.get(key, default)
        e.hook(0x6442a0, lambda e: get("level", 1), 3)
        e.hook(0x643f80, lambda e: get("weapon", 1), 1)
        e.hook(0x625480, lambda e: {6: get("life", 100 * 256), 8: get("mana", 100 * 256),
                                    0xcc: get("stat_charges", 0)}.get(e.arg(1), 0), 3)
        # FUN_00647840's walk for the entry's item: the case's item (its id at +0xc), or an empty inventory
        e.w32(self.item + 0xc, 7)
        e.hook(0x63b2c0, lambda e: self.item if get("held", 0) else 0, 1)
        e.hook(0x63dfd0, lambda e: self.item, 1)
        e.hook(0x63dfa0, lambda e: 0, 1)
        e.hook(0x639df0, lambda e: int(e.arg(1) in get("states", set())), 2)
        e.hook(0x63a510, lambda e: get("restricted", 0), 0)
        e.stub(0, {0x63a400: 1, 0x644180: 1, 0x6235a0: 1, 0x625500: 3})
        e.hook(0x62ba80, lambda e: get("throwable", 0), 1)
        e.hook(0x63c9b0, self.weapon_in_hand, 4)

    def weapon_in_hand(self, e):
        """FUN_0063c9b0(inventory, &item, &body location, &selected): the right hand's weapon, selected."""
        e.w32(e.arg(1), self.item)
        e.w32(e.arg(2), 4)
        e.w32(e.arg(3), 1)
        return 1

    def status(self, skill, case):
        """FUN_00647960 on Skills.txt row `skill` (None for no row) under `case`: its status."""
        self.case = case
        entry = self.e.view("skill_entry", self.entry)
        entry.row = 0 if skill is None else self.skills + skill * RECORD
        entry.level = case.get("level", 1)
        entry.quantity = case.get("quantity", 0)
        entry.item = case.get("item", -1)
        entry.charges = case.get("charges", 0)
        self.e.view("unit", self.unit).inventory = self.inventory if case.get("inventory", 1) else 0
        return self.e.call(0x647960, self.unit, self.entry, limit=200000) & 0xFFFFFFFF


JAB = 10
# skill, status, game.exe's (go, the skill that goes, the voice message)
CLIENT = [(skill, status, want) for skill, wants in (
    (FIRE_BOLT, {0: (1, 36, 0), 1: (0, 36, 0x15), 2: (0, 36, 0x13), 3: (0, 36, 0x13), 4: (0, 36, 0x13),
                 5: (1, 36, 0), 6: (0, 36, 0), 7: (0, 36, 0), 8: (0, 36, 0)}),
    (JAB, {0: (1, 10, 0), 1: (1, 0, 0), 2: (1, 0, 0), 3: (0, 10, 0x13), 4: (1, 0, 0),
           5: (1, 10, 0), 6: (0, 10, 0), 7: (0, 10, 0), 8: (0, 10, 0)}),
) for status, want in wants.items()]


def client(oracle):
    """FUN_004610c0 for each skill and status: what goes, and the voice."""
    e, bad = oracle.e, 0
    entry, attack, slot = oracle.entry, e.alloc(0x40), e.alloc(4)
    e.w32(attack, oracle.skills)                              # Attack, row 0
    e.w32(0x7a6a70, oracle.unit)                              # FUN_00463dd0: the player
    told = []
    now = {}
    e.hook(0x4d9fc0, lambda e: now["status"] if e.mu.reg_read(UC_X86_REG_EDX) == entry else 0, 0)
    e.hook(0x6439f0, lambda e: attack if e.mu.reg_read(UC_X86_REG_EDX) == 0 else 0, 0)
    e.hook(0x4cb9c0, lambda e: told.append(e.mu.reg_read(UC_X86_REG_EDX)), 0)
    for skill, status, want in CLIENT:
        now["status"] = status
        told.clear()
        e.w32(entry, oracle.skills + skill * RECORD)
        e.w32(slot, entry)
        go = e.call(0x4610c0, 0, limit=100000, regs={"edi": oracle.unit, "ebx": slot}) & 0xFFFFFFFF
        used = (e.r32(e.r32(slot)) - oracle.skills) // RECORD
        got = (go, used, told[0] if told else 0)
        bad += got != want
        print(f"{skill} {status} {got[0]} {got[1]} {got[2]:#x}" + ("" if got == want else f"  FAIL: skills.md says {want}"))
    print("ok" if not bad else f"FAIL: {bad} of {len(CLIENT)}")
    return 1 if bad else 0


def main():
    """Run CASES (or `client`) and return 1 if any differs from skills.md, else 0."""
    if sys.argv[1:] not in ([], ["client"]):
        sys.exit("usage: skill_status.py [client]")
    if sys.argv[1:] == ["client"]:
        return client(Oracle())
    oracle, bad = Oracle(), 0
    for name, skill, case, want in CASES:
        got = oracle.status(skill, case)
        bad += got != want
        print(f"{name} {got}" + ("" if got == want else f"  FAIL: skills.md says {want}"))
    print("ok" if not bad else f"FAIL: {bad} of {len(CASES)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
