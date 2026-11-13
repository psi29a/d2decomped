# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify GetPartyIdFromUnit (UNIT/Party.cpp FUN_00540710) against stock game.exe 1.14d.

    uv run python unit_party_id.py [--mutation-check]

FUN_00540710(ECX pGame, EDX pUnit) -> int16 party id, or -1. The search GUID
starts as the unit's own (unit +0x0c), but when the unit has an inventory
(+0x60 non-null) it becomes FUN_0063d450(inventory) instead - run for real
here, no callees: a guard (inventory +0 == 0x01020304) and a field read
(+0x24, nOwnerGUID), else -1. That overwrite applies even when the guard
fails (the search GUID becomes -1, not the unit's own id back) - not a bug,
just what the asm does (docs/research/re/party.md).

With the unit's type UNIT_PLAYER (0; any other type halts, not exercised
here) and a non-null game: with no pPartyControl (+0x1d2c) the function
returns -1 outright; otherwise it walks the control's `pSub` list
(D2UnitPartyControlSub, newest first via pPrev) and each sub's own member
list (D2UnitPartyControlSubSub via pNext), returning that sub's nPartyId on
the first GUID match, else -1 once every sub is exhausted.

No MPQs are read. `--mutation-check` breaks the model four ways and
requires each to be caught.
"""
import argparse
import hashlib
import random
import struct
import sys
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FUNCTION = 0x540710
CURSOR_GUID = 0x63D450
UNIT_PLAYER = 0
GAME_SIZE = 7668
PARTY_CONTROL_OFF = 0x1D2C          # SGAMEDATA::pPartyControl
UNIT_TYPE_OFF, UNIT_ID_OFF, UNIT_INVENTORY_OFF = 0x00, 0x0C, 0x60
UNIT_SIZE = 0x64
INV_SIGNATURE_OFF, INV_OWNER_OFF, INV_SIZE = 0x00, 0x24, 0x40
INV_MAGIC = 0x01020304
MUTATIONS = ("ignore-inventory", "no-partycontrol-guard", "stop-at-first-sub", "sign-flip-found")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Mismatch(Exception):
    """game.exe and the model disagree."""


def require(condition, message):
    """Raise Mismatch(message) unless condition holds."""
    if not condition:
        raise Mismatch(message)


def search_guid(unit_id, inventory):
    """The GUID GetPartyIdFromUnit searches for, as the asm computes it."""
    if inventory is None or mut("ignore-inventory"):
        return unit_id
    signature, owner = inventory
    return owner if signature == INV_MAGIC else -1


def model(search, control):
    """(party id, or -1) for one call; control is None (no pPartyControl) or a list of
    (party_id, [member guids]) subs in pPrev traversal order (newest first)."""
    if control is None:
        return -1 if not mut("no-partycontrol-guard") else 0
    for party_id, members in control:
        if search in members:
            return -party_id if mut("sign-flip-found") else party_id
        if mut("stop-at-first-sub"):
            break
    return -1


def to_int16(value):
    """Reinterpret value's low 16 bits as a signed int16."""
    value &= 0xFFFF
    return value - 0x10000 if value >= 0x8000 else value


def build_game(e, control):
    """A game record with pPartyControl built from `control` (None, or a list of
    (party_id, [member guids]) subs, newest first); returns the game's address."""
    game = e.alloc(GAME_SIZE)
    if control is None:
        return game
    sub_head = 0
    for party_id, members in reversed(control):        # build back to front so pPrev visits `control` in its own order
        member_head = 0
        for guid in members:
            node = e.alloc(8)
            e.w32(node, guid & 0xFFFFFFFF)
            e.w32(node + 4, member_head)
            member_head = node
        sub = e.alloc(12)
        e.mu.mem_write(sub, struct.pack("<hxx", party_id & 0xFFFF if party_id >= 0 else party_id))
        e.w32(sub + 4, member_head)
        e.w32(sub + 8, sub_head)
        sub_head = sub
    control_block = e.alloc(8)
    e.w32(control_block + 4, sub_head)
    e.w32(game + PARTY_CONTROL_OFF, control_block)
    return game


def build_unit(e, unit_id, inventory):
    """A UNIT_PLAYER unit record with `unit_id` and an optional (signature, owner) inventory."""
    unit = e.alloc(UNIT_SIZE)
    e.w32(unit + UNIT_TYPE_OFF, UNIT_PLAYER)
    e.w32(unit + UNIT_ID_OFF, unit_id & 0xFFFFFFFF)
    if inventory is not None:
        signature, owner = inventory
        inv = e.alloc(INV_SIZE)
        e.w32(inv + INV_SIGNATURE_OFF, signature & 0xFFFFFFFF)
        e.w32(inv + INV_OWNER_OFF, owner & 0xFFFFFFFF)
        e.w32(unit + UNIT_INVENTORY_OFF, inv)
    return unit


def one_case(e, rng):
    """Build one random game/unit pair, run FUN_00540710, compare with the model."""
    has_inventory = rng.random() < 0.4
    inventory = (INV_MAGIC if rng.random() < 0.7 else rng.randrange(1 << 32), rng.randrange(-(1 << 31), 1 << 31)) if has_inventory else None
    unit_id = rng.randrange(-(1 << 31), 1 << 31)
    unit = build_unit(e, unit_id, inventory)

    has_control = rng.random() < 0.85
    control = None
    if has_control:
        subs = []
        for _ in range(rng.randrange(1, 5)):
            party_id = rng.randrange(3, 200)
            members = [rng.randrange(-(1 << 31), 1 << 31) for _ in range(rng.randrange(0, 4))]
            if rng.random() < 0.3:                      # sometimes plant the searched GUID
                members.insert(rng.randrange(len(members) + 1), search_guid(unit_id, inventory))
            subs.append((party_id, members))
        control = subs

    game = build_game(e, control)
    got = to_int16(e.call(FUNCTION, ecx=game, edx=unit))
    want = model(search_guid(unit_id, inventory), control)
    require(got == want, f"unit_id {unit_id:#x} inventory {inventory} control {control}: got {got} want {want}")


def run_all(count=4000, seed=0x540710):
    """Run count random cases through FUN_00540710 and the model, requiring agreement."""
    rng = random.Random(seed)
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    e = emu.Emu(str(exe), real=(CURSOR_GUID,))
    for _ in range(count):
        one_case(e, rng)
    return count


def main():
    """Run the cases, or --mutation-check each deliberate model defect."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    global mutation
    if options.mutation_check:
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
    print(f"ok: {run_all()} GetPartyIdFromUnit cases")


if __name__ == "__main__":
    main()
