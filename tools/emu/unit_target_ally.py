# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify SKILL_CanUnitTargetAlly (UNIT/SUnit.cpp FUN_00554de0) against stock game.exe 1.14d.

    uv run python unit_target_ally.py [--mutation-check]

FUN_00554de0(ECX pGame - never read, EDX pUnit, stack pOther) -> BOOL. A
monster on either side is first resolved to its owner (FUN_0058f0d0,
hooked and logged here; a monster with no owner stays a monster). If the
two resolved pointers are then the same, it's an ally (1) outright. Else,
unless both resolved units are UNIT_PLAYER, the answer is
D2Common::PATH::Path::IsAllyOf(unit, other) (0x650d70, hooked and logged).
Two players: each side's party id is -1 when its own unit +0x80
(uSideData.pServerSide.pGame) is null, else FUN_00540710's own search
(party.md), replicated here in Python (`party_search`; every unit has no
inventory, so the search GUID is always the unit's own id - no override).
An ally needs the same party id and that id not -1.

unit +0x00 eUnitType, +0x0c nUnitGUID, +0x60 pInventory (left null), +0x80
pGame. No MPQs are read. `--mutation-check` breaks the model three ways and
requires each to be caught.
"""
import argparse
import hashlib
import random
import struct
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FUNCTION = 0x554DE0
GET_OWNER_UNIT = 0x58F0D0
IS_ALLY_OF = 0x650D70
UNIT_PLAYER, UNIT_MONSTER = 0, 1
UNIT_TYPE_OFF, UNIT_ID_OFF, UNIT_GAME_OFF = 0x00, 0x0C, 0x80
UNIT_SIZE = 0x94
PARTY_CONTROL_OFF = 0x1D2C
GAME_SIZE = 7668
MUTATIONS = ("ignore-owner-resolution", "ignore-party-match", "no-otherid-minus1-guard")
mutation = None


def mut(name):
    """Whether the current --mutation-check pass is deliberately breaking name."""
    return mutation == name


class Mismatch(Exception):
    """game.exe and the model disagree."""


def require(condition, message):
    """Raise Mismatch(message) unless condition holds."""
    if not condition:
        raise Mismatch(message)


def build_party_game(e, control):
    """A game whose pPartyControl holds `control` ([(party_id, [member ids]), ...], in
    the order FUN_00540710 visits it); returns the game's address."""
    game = e.alloc(GAME_SIZE)
    sub_head = 0
    for party_id, members in reversed(control):
        member_head = 0
        for guid in members:
            node = e.alloc(8)
            e.w32(node, guid & 0xFFFFFFFF)
            e.w32(node + 4, member_head)
            member_head = node
        sub = e.alloc(12)
        e.mu.mem_write(sub, struct.pack("<hxx", party_id))
        e.w32(sub + 4, member_head)
        e.w32(sub + 8, sub_head)
        sub_head = sub
    control_block = e.alloc(8)
    e.w32(control_block + 4, sub_head)
    e.w32(game + PARTY_CONTROL_OFF, control_block)
    return game


def build_player(e, unit_id, game):
    """A UNIT_PLAYER unit record with `unit_id` and `game` (0 for no game)."""
    unit = e.alloc(UNIT_SIZE)
    e.w32(unit + UNIT_TYPE_OFF, UNIT_PLAYER)
    e.w32(unit + UNIT_ID_OFF, unit_id & 0xFFFFFFFF)
    e.w32(unit + UNIT_GAME_OFF, game)
    return unit


def build_monster(e):
    """A fresh non-player unit, type UNIT_MONSTER."""
    unit = e.alloc(UNIT_SIZE)
    e.w32(unit + UNIT_TYPE_OFF, UNIT_MONSTER)
    return unit


class Side:
    """One side's original unit and, after FUN_0058f0d0's owner resolution, what it
    becomes: itself (a player, or a monster with no owner) or its owner player."""

    def __init__(self, e, rng, control):
        """Build a random side (monster-with-owner, ownerless monster, or player) in e's memory."""
        self.is_monster = rng.random() < 0.5
        if self.is_monster:
            self.original = build_monster(e)
            self.has_owner = rng.random() < 0.7
            if self.has_owner:
                game = build_party_game(e, control) if rng.random() < 0.85 else 0
                self.owner_id = rng.randrange(-(1 << 31), 1 << 31)
                self.resolved = build_player(e, self.owner_id, game)
                self.resolved_type, self.resolved_game = UNIT_PLAYER, game
            else:
                self.resolved, self.resolved_type, self.resolved_game, self.owner_id = self.original, UNIT_MONSTER, None, None
        else:
            game = build_party_game(e, control) if rng.random() < 0.85 else 0
            self.owner_id = rng.randrange(-(1 << 31), 1 << 31)
            self.original = self.resolved = build_player(e, self.owner_id, game)
            self.resolved_type, self.resolved_game, self.has_owner = UNIT_PLAYER, game, False


def one_case(e, rng, owner_map):
    """Run FUN_00554de0 and the model on one random pair of sides, requiring agreement."""
    control = [(rng.randrange(3, 200), [rng.randrange(-(1 << 31), 1 << 31) for _ in range(rng.randrange(0, 3))])
               for _ in range(rng.randrange(1, 4))]
    left = Side(e, rng, control)
    right = left if rng.random() < 0.1 else Side(e, rng, control)
    # Two different monsters owned by the same player (two of one summoner's pets):
    # different originals, the same resolved owner - the only way to tell owner
    # resolution (FUN_0058f0d0) apart from a bare pointer compare of the originals.
    if right is not left and left.is_monster and left.has_owner and right.is_monster and right.has_owner and rng.random() < 0.3:
        right.resolved, right.resolved_type, right.resolved_game, right.owner_id = left.resolved, left.resolved_type, left.resolved_game, left.owner_id

    owner_map.clear()
    for side in (left, right):
        if side.is_monster and side.has_owner:
            owner_map[side.original] = side.resolved
    ally_log = rng.choice((True, False))
    e.hook(IS_ALLY_OF, lambda _e, ans=ally_log: int(ans), 2)

    got = bool(e.call(FUNCTION, right.original, ecx=0, edx=left.original))

    # The model: owner resolution, then the shortcuts/branches FUN_00554de0 takes.
    resolved_left = left.original if mut("ignore-owner-resolution") else left.resolved
    resolved_right = right.original if mut("ignore-owner-resolution") else right.resolved
    if resolved_left == resolved_right:
        want = True
    elif resolved_left != resolved_right and not (left.resolved_type == UNIT_PLAYER and right.resolved_type == UNIT_PLAYER):
        want = ally_log
    elif mut("ignore-party-match"):
        want = True
    else:
        # An independent replica of FUN_00540710's search (every unit here has no
        # inventory, so the search GUID is always the unit's own id): not a second
        # call into the function under test, so a mutated `resolved_*` here can't
        # feed it a unit of the wrong type the way a direct re-call would.
        left_id = party_search(control, left.owner_id) if left.resolved_game else -1
        right_id = party_search(control, right.owner_id) if right.resolved_game else -1
        no_guard = mut("no-otherid-minus1-guard")
        want = left_id == right_id and (no_guard or right_id != -1)
    require(got == want,
            f"left(monster={left.is_monster} owner={left.has_owner}) right(monster={right.is_monster} owner={right.has_owner}): got {got} want {want}")


def party_search(control, unit_id):
    """FUN_00540710's own search, replicated: the first sub whose members hold `unit_id`,
    in `control`'s order, else -1."""
    for party_id, members in control:
        if unit_id in members:
            return party_id
    return -1


def run_all(count=3000, seed=0x554DE0):
    """Run count random cases through FUN_00554de0 and the model, requiring agreement."""
    rng = random.Random(seed)
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    e = emu.Emu(str(exe))
    owner_map = {}

    def owner_hook(current):
        """Hook: FUN_0058f0d0's owner resolution, from owner_map."""
        pointer = current.mu.reg_read(UC_X86_REG_ECX)
        return owner_map.get(pointer, 0)
    e.hook(GET_OWNER_UNIT, owner_hook, 0)

    for _ in range(count):
        one_case(e, rng, owner_map)
    return count


def main():
    """Run the cases, or --mutation-check each deliberate model defect."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    global mutation
    if options.mutation_check:
        mutation = None
        try:
            run_all()
        except Mismatch as exc:
            sys.exit(f"baseline failed (not a caught mutation): {exc}")
        for name in MUTATIONS:
            mutation = name
            try:
                run_all()
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"NOT caught: {name}")
        print(f"ok: {len(MUTATIONS)} deliberate model defects all caught")
        return
    print(f"ok: {run_all()} SKILL_CanUnitTargetAlly cases")


if __name__ == "__main__":
    main()
