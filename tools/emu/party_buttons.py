# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe's Party screen buttons on the host against d2d's World::party_action.

Run for real: the invite (FUN_005a5be0), cancel (FUN_005a5b30) and accept
(FUN_005a5a30) handlers, the accept's per-player pass (FUN_005a5970 with
FUN_005a5900), the removal's per-player pass (FUN_005a5cb0), FUN_0055b650,
the boxes (FUN_005a5840 hear, FUN_005a58a0 squelch, FUN_005a57b0 loot with
FUN_005a5750) and the relation list itself (FUN_0055b140, FUN_0055b110,
FUN_0055b3f0, FUN_0055b3a0, FUN_0055b440, FUN_0055b300), on real node lists.
Hooked: the player-data lookup, the unit and party lookups, the party control
(FUN_00540100 create, FUN_00540320 add), the broadcast walk FUN_005538d0 (its
callback then run for each player here, as it does), the flag setter
FUN_0055b260 (it also sends 0x8c) and the packet senders.

The steps and the states checked are tests/game/test_lan_party.cpp's: the
same button states (node +0xc), party ids, flags and party lines.

Run from tools/emu:
    .venv/bin/python party_buttons.py --mutation-check
"""
import argparse

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

INVITE, CANCEL, ACCEPT = 0x5A5BE0, 0x5A5B30, 0x5A5A30
ACCEPT_PASS, REMOVAL_PASS = 0x5A5970, 0x5A5CB0
HEAR, SQUELCH, LOOT = 0x5A5840, 0x5A58A0, 0x5A57B0


class Rig:
    """Four players (Amy, Bob, Cid, Dan) with real relation lists, the parties kept here."""
    def __init__(self):
        """Allocate the game, units and player data; hook the callees."""
        self.machine = machine = emu.Emu()
        self.game = machine.alloc(0x2000)
        self.names = {}
        self.data = {}
        self.party = {}
        self.parties = []                  # [id, [unit, ...] newest first], newest first
        self.counter = 0
        self.hardcore = set()
        self.lines = []                    # (to, about, type)
        self.walks = []                    # (callback, arg)
        self.units = [self.unit(1, "Amy"), self.unit(2, "Bob"), self.unit(3, "Cid"), self.unit(4, "Dan")]
        self.by_id = {machine.r32(unit + 0xC): unit for unit in self.units}

        def ecx(current):
            return current.mu.reg_read(UC_X86_REG_ECX)

        def edx(current):
            return current.mu.reg_read(UC_X86_REG_EDX)

        def node_of(self_unit, other):
            node = machine.r32(self.data[self_unit] + 0x38)
            other_id = machine.r32(other + 0xC)
            while node:
                if machine.r32(node) == other_id:
                    return node
                node = machine.r32(node + 0x10)
            return 0

        def flag_set(current):
            """Hook FUN_0055b260 (ECX game, EDX self; other, mask, on): the node's flags, without its 0x8c."""
            node = node_of(edx(current), current.arg(0))
            if node:
                flags = machine.r32(node + 4)
                machine.w32(node + 4, flags | current.arg(1) if current.arg(2) else flags & ~current.arg(1))
            return 0

        def party_create(current):
            """Hook FUN_00540100: FUN_005400b0's id (the counter, at least 3, past ids in use), prepended."""
            party_id = max(self.counter, 3)
            while any(party_id == known for known, _ in self.parties):
                party_id = max((party_id + 1) & 0xFFFF, 3)
            self.counter = (party_id + 1) & 0xFFFF
            self.parties.insert(0, [party_id, []])
            return party_id

        def party_add(current):
            """Hook FUN_00540320 (EDX party; unit): a member first in its party."""
            unit = current.arg(0)
            members = next(found for known, found in self.parties if known == edx(current))
            if unit not in members:
                members.insert(0, unit)
                self.party[unit] = edx(current)
            return 1

        machine.hook(0x6221A0, lambda current: self.data[current.arg(0)], 1)
        machine.hook(0x40B430, lambda current: machine.alloc(edx(current)), 3)
        machine.hook(0x554630, lambda current: self.party.get(ecx(current), 0xFFFF), 0)
        machine.hook(0x540710, lambda current: self.party.get(edx(current), 0xFFFF), 0)
        machine.hook(0x552F60, lambda current: self.by_id.get(current.arg(0), 0), 1)
        machine.hook(0x5531C0, ecx, 1)                       # the unit's client: the unit stands for it
        machine.hook(0x5A5690, lambda current: self.lines.append((ecx(current), current.arg(0), current.arg(1))), 2)
        machine.hook(0x538670, lambda current: (4 if ecx(current) in self.hardcore else 0) & edx(current), 0)
        machine.hook(0x55B260, flag_set, 3)
        machine.hook(0x540100, party_create, 0)
        machine.hook(0x540320, party_add, 1)
        def walk(current):
            """Hook FUN_005538d0 (EDX the callback; its argument): kept to run after the call; the
            accept's argument {inviter, accepter, party} is in the caller's frame, so a copy."""
            copy = machine.alloc(12)
            machine.mu.mem_write(copy, machine.read(current.arg(0), 12))
            self.walks.append((edx(current), copy))
            return 0

        machine.hook(0x5538D0, walk, 1)
        machine.stub(0, {0x639DF0: 2, 0x40B480: 3, 0x53DDC0: 1, 0x53DEC0: 0, 0x53DA90: 0})
        self.node_of = node_of

    def unit(self, unit_id, name):
        """A player unit (type 0, class 4) with its player data (+0x38 the relation list)."""
        unit = self.machine.alloc(0x100)
        self.machine.w32(unit + 4, 4)
        self.machine.w32(unit + 0xC, unit_id)
        self.data[unit] = self.machine.alloc(0x100)
        self.names[unit] = name
        return unit

    def click(self, handler, actor, other, on=0):
        """A table entry as FUN_005a6000 calls it, then any broadcast walk it asked for, for each player."""
        self.walks.clear()
        self.machine.call(handler, other, on, self.machine.alloc(4), ecx=self.game, edx=actor)
        for callback, arg in list(self.walks):
            assert callback == ACCEPT_PASS, hex(callback)
            for player in self.units:
                self.machine.call(callback, arg, ecx=self.game, edx=player)

    def accept(self, actor, other):
        """FUN_005a5a30, its walk's argument {inviter, accepter, party} read back for the pass."""
        self.walks.clear()
        self.machine.call(ACCEPT, other, 0, self.machine.alloc(4), ecx=self.game, edx=actor)
        for callback, arg in list(self.walks):
            assert callback == ACCEPT_PASS
            assert self.machine.r32(arg) == other and self.machine.r32(arg + 4) == actor
            for player in self.units:
                self.machine.call(ACCEPT_PASS, arg, ecx=self.game, edx=player)

    def remove(self, unit):
        """FUN_005407a0 as party.md reads it: out of the party, FUN_005a5cb0 for every player;
        a party left under two disbanded, FUN_005a5cb0 again for its last member."""
        party_id = self.party.pop(unit, 0xFFFF)
        if party_id == 0xFFFF:
            return
        entry = next(entry for entry in self.parties if entry[0] == party_id)
        entry[1].remove(unit)
        for player in self.units:
            self.machine.call(REMOVAL_PASS, unit, ecx=self.game, edx=player)
        if len(entry[1]) < 2:
            self.parties.remove(entry)
            for last in entry[1]:
                self.party.pop(last, None)
                for player in self.units:
                    self.machine.call(REMOVAL_PASS, last, ecx=self.game, edx=player)

    def button(self, self_unit, other):
        """FUN_0055b3f0 run for real: the node's +0xc."""
        return self.machine.call(0x55B3F0, ecx=self_unit, edx=other) & 0xFFFF

    def flags(self, self_unit, other):
        """The node's +4 flags."""
        node = self.node_of(self_unit, other)
        return self.machine.r32(node + 4) if node else 0

    def told(self, unit):
        """The party lines `unit` was sent since the last look: [(type, name)]."""
        lines = [(kind, self.names[self.by_id[about]]) for to, about, kind in self.lines if to == unit]
        self.lines = [line for line in self.lines if line[0] != unit]
        return lines


def observed():
    """Run the steps; returns what game.exe left after each, by name."""
    rig = Rig()
    amy, bob, cid, dan = rig.units
    seen = {}
    rig.click(INVITE, amy, bob)
    seen["invite"] = (rig.button(amy, bob), rig.button(bob, amy), rig.told(bob), rig.told(amy))
    rig.click(INVITE, amy, bob)
    seen["invite_again"] = (rig.button(amy, bob), rig.told(bob))
    rig.click(CANCEL, amy, bob)
    seen["cancel"] = (rig.button(amy, bob), rig.button(bob, amy), rig.told(bob))
    rig.accept(bob, amy)
    seen["accept_uninvited"] = (rig.party.get(amy, 0xFFFF), rig.party.get(bob, 0xFFFF))
    rig.click(INVITE, amy, bob)
    rig.told(bob)
    rig.accept(bob, amy)
    seen["accept"] = (rig.party.get(amy), rig.party.get(bob), [entry[1] for entry in rig.parties] == [[bob, amy]],
                      rig.button(amy, bob), rig.button(bob, amy), rig.button(amy, cid), rig.button(cid, amy),
                      rig.button(bob, dan), rig.button(dan, bob), rig.told(bob), rig.told(amy), rig.told(cid))
    rig.click(INVITE, amy, cid)
    rig.click(INVITE, dan, cid)
    seen["two_invites"] = (rig.button(cid, amy), rig.button(cid, dan), rig.button(dan, cid))
    rig.told(cid)
    rig.accept(cid, amy)
    seen["accept_existing"] = (rig.party.get(cid), [entry[1] for entry in rig.parties] == [[cid, bob, amy]],
                               rig.button(cid, amy), rig.button(cid, bob), rig.button(bob, cid),
                               rig.button(cid, dan), rig.button(dan, cid),
                               rig.told(cid), rig.told(amy), rig.told(bob), rig.told(dan))
    rig.click(HEAR, bob, dan, 2)
    rig.click(SQUELCH, bob, dan, 4)
    hear_squelch = (rig.flags(bob, dan), rig.flags(dan, bob))
    rig.click(SQUELCH, bob, dan, 0)
    seen["boxes"] = hear_squelch + (rig.flags(bob, dan),)
    rig.click(LOOT, bob, dan, 1)
    soft = (rig.flags(bob, dan) & 1, rig.told(dan))
    rig.hardcore.add(bob)
    rig.machine.w32(bob + 4, 1)                              # a Sorceress
    rig.click(LOOT, bob, dan, 1)
    on = (rig.flags(bob, dan) & 1, rig.told(dan))
    rig.click(LOOT, bob, dan, 0)
    seen["loot"] = (soft, on, (rig.flags(bob, dan) & 1, rig.told(dan)))
    rig.remove(cid)
    seen["leave"] = (rig.party.get(cid, 0xFFFF), [entry[1] for entry in rig.parties] == [[bob, amy]],
                     rig.button(amy, cid), rig.button(bob, cid), rig.button(dan, cid),
                     rig.button(cid, amy), rig.button(cid, bob), rig.button(cid, dan))
    rig.remove(bob)
    seen["disband"] = (rig.party.get(amy, 0xFFFF), rig.party.get(bob, 0xFFFF), rig.parties == [],
                       all(rig.button(one, other) == 0 for one in rig.units for other in rig.units if one != other))
    rig.click(INVITE, dan, cid)
    rig.accept(cid, dan)
    seen["next_id"] = (rig.party.get(dan), rig.party.get(cid))
    return seen


def expected(mutation=None):
    """tests/game/test_lan_party.cpp's states; `mutation` breaks one on purpose."""
    want = {
        "invite": (4, 2, [(5, "Amy")], []),
        "invite_again": (4, []),
        "cancel": (0, 0, [(6, "Amy")]),
        "accept_uninvited": (0xFFFF, 0xFFFF),
        "accept": (3, 3, True, 1, 1, 0, 1, 0, 1, [(8, "Amy")], [(7, "Bob")], []),
        "two_invites": (2, 2, 4),
        "accept_existing": (3, True, 1, 1, 1, 0, 1, [(8, "Amy")], [(7, "Cid")], [(7, "Cid")], []),
        "boxes": (6, 0, 2),
        "loot": ((0, []), (1, [(2, "Bob")]), (0, [(11, "Bob")])),
        "leave": (0xFFFF, True, 0, 0, 0, 1, 1, 0),
        "disband": (0xFFFF, 0xFFFF, True, True),
        "next_id": (4, 4),
    }
    if mutation == "invite-states-swapped":
        want["invite"] = (2, 4, [(5, "Amy")], [])
    if mutation == "outsider-can-invite":
        want["accept"] = want["accept"][:6] + (0,) + want["accept"][7:]
    if mutation == "rival-offer-kept":
        want["accept_existing"] = want["accept_existing"][:5] + (2,) + want["accept_existing"][6:]
    if mutation == "loot-his":
        want["loot"] = ((0, []), (1, [(1, "Bob")]), (0, [(11, "Bob")]))
    if mutation == "leaver-sees-invite":
        want["leave"] = want["leave"][:5] + (0,) + want["leave"][6:]
    if mutation == "ids-from-1":
        want["next_id"] = (2, 2)
    return want


def main():
    """Compare game.exe with the steps' states; --mutation-check checks each broken one is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    seen = observed()
    want = expected()
    for step, states in want.items():
        assert seen[step] == states, (step, seen[step], states)
    if args.mutation_check:
        for mutation in ("invite-states-swapped", "outsider-can-invite", "rival-offer-kept", "loot-his", "leaver-sees-invite", "ids-from-1"):
            assert seen != expected(mutation), f"negative control escaped: {mutation}"
            print(f"negative control caught: {mutation}")
    print(f"ok: {len(want)} party-button steps match test_lan_party")


if __name__ == "__main__":
    main()
