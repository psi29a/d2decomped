# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the GAME\\Roster.cpp party roster accessors in stock game.exe 1.14d.

    uv run python roster_small.py [--mutation-check]

The roster is a linked list of records: head at 0x7bb5c0, next at record +0x80. A record
has the unit id at +0x10, an optional second id list at +0x38 (nodes of id, next), the
relation cell pointer at +0x34 (a cell holding the first node of id, mask, next), the
level at +0x20, the area at +0x22, a flags dword at +0x30, the name at +0x66 and state
words (+0x14, +0x1c, +0x24, +0x28, +0x2c). The local player unit is the pointer at 0x7a6a70
(FUN_00463dd0 returns it; its id is at +0xc).

Runs game.exe's own code on random rosters and checks, against a model written from the
decompile and the disassembly: the return value (or the fatal source line), every byte of
the roster arena after the call, the ordered log of the hooked callees (registers and stack
arguments) and the callee-saved registers. Void functions are checked on their effects only
(EAX is not part of their contract). FUN_00479d30 is a custom call (EBX the record, DI the
flag mask, no stack arguments, EDI is clobbered); FUN_00479e20 and FUN_00479fe0 call it. The
relation list functions 0x4dc320 / 0x4dc440 / 0x4dc3e0 and the lookups 0x4792e0 / 0x47a310
run as stock, and so does FUN_006221a0 (the player-data fallback of FUN_0047a210; its fatal lines 0xfb4 /
0xfb6 are for a null or non-player unit, which FUN_0047a210 has already rejected). A fatal error (a call to
0x40_8a60 with a source line) is a "halt" with that line; the call site must lie in the function under test or
in a callee the case names (`fatal_from`). 0x6cc837, the report's first argument, is an empty string: only
the call site names the file.

Hooked (their work is not claimed): 0x40_8a60 (fatal report), 0x47_8740 (the C->S 0x5d relation
send, logged), 0x47_9ab0 and 0x49_a640 (roster redraw, logged), 0x61_1560 (the ignore-list
singleton, a two-slot fake object whose calls are logged), 0x40_b380 (relation node allocator,
logged, blocks from the arena), the pool entries of `_monster_world`.

Covered: FUN_004795a0, 004795c0, 00479320, 00479b90, 00479ba0, 00479d30, 00479e20, 00479f60,
00479fe0, 0047a0c0, 0047a100, 0047a1e0, 0047a210, 0047a690, 004792e0.

`--mutation-check` breaks the model on purpose, one defect at a time, and requires each to fail.
"""
import random
import sys

import _monster_world as mw
from _game_world import ANY, Arena, Rig, require, verify

HEAD = 0x7B_B5C0
PLAYER = 0x7A_6A70
COVERED = (0x47_95A0, 0x47_95C0, 0x47_9320, 0x47_9B90, 0x47_9BA0, 0x47_9D30, 0x47_9E20, 0x47_9F60, 0x47_9FE0,
           0x47_A0C0, 0x47_A100, 0x47_A1E0, 0x47_A210, 0x47_A690, 0x47_92E0)
MUTATIONS = (
    "next-null-returns-zero", "name-offset-0x65", "level-32-bit", "word-1c-byte", "id-null-zero", "state-24-null-minus1",
    "find-skips-id-list", "find-minus-one-matches", "find-last-match", "find-returns-node", "self-fatal-line",
    "state-set-swapped", "unit-name-any-type", "unit-name-no-fallback", "unit-name-fallback-0", "byte-store-dword",
    "toggle-sets-always", "toggle-wrong-mask", "toggle-self-allowed", "add-skips-dup-check",
    "squelch-branches-swapped", "squelch-packet-flag-bool", "squelch-null-still-runs",
    "hostile-level-8", "hostile-level-10", "hostile-area-ignored", "hostile-no-area-sentinel", "hostile-skips-send",
    "hostile-sentinel-byte", "hostile-area-compare-byte", "leave-area-byte", "unit-type-byte",
    "leave-area-0", "leave-flag-0", "player-null-zero", "relation-send-type-5",
)
mutation = None
FATAL = {"name": 0x174, "self": 0x75, "unit": 0x5DB, "unit-type": 0x5DC, "unit-none": 0x5E5, "toggle": 0x463, "leave": 0x4ED}
ENTRY, NODES, CELLS, RELS, UNIT = 0x100, 0x800, 0x1000, 0x1400, 0x1800


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Machine(Rig):
    """The rig with the callees of the roster functions hooked."""

    def __init__(self):
        """Hook the sends, the redraws, the ignore-list singleton and the relation node allocator."""
        super().__init__()
        self.arena = None
        self.fake = self.m.alloc(0x40)
        self.slots_made = None
        lg = self.logger
        lg(0x47_8740, 1, "relation-send", regs=("ecx", "edx"))
        lg(0x47_9AB0, 0, "redraw-479ab0", regs=())
        lg(0x49_A640, 0, "redraw-49a640", regs=())
        self.m.hook(0x61_1560, self.singleton, 0)
        self.m.hook(0x40_B380, self.node_alloc, 2)

    def write_global(self, address, value):
        """Write a game global (outside the arena)."""
        self.m.w32(address, value)

    def singleton(self, current):
        """0x61_1560: the ignore-list object: [object] -> vtable of two logging slots (add at 0, delete at 4)."""
        if self.slots_made is None:
            self.slots_made = (self.slot(lambda c: self.slot_call("ignore-add", c), 1),
                               self.slot(lambda c: self.slot_call("ignore-del", c), 1))
        current.w32(self.fake, self.fake + 8)
        current.w32(self.fake + 8, self.slots_made[0])
        current.w32(self.fake + 12, self.slots_made[1])
        return self.fake

    def slot_call(self, name, current):
        """A vtable slot call: log the name and the one stack argument (the roster name pointer)."""
        self.log.append((name, current.arg(0)))
        return 0

    def node_alloc(self, current):
        """0x40_b380 (ECX size, EDX string, stack: line, flag): log it and hand out the next arena block."""
        regs = mw.REGISTERS
        self.log.append(("alloc-b380", current.mu.reg_read(regs["ecx"]), current.mu.reg_read(regs["edx"]),
                         current.arg(0), current.arg(1)))
        return self.arena.hook_alloc(0xC)


class Roster:
    """Roster set-up in emulator memory and a model copy, plus the model of each covered function."""

    def __init__(self, rig, rng):
        """Build a random roster: records, id lists, relation lists, the player unit and the globals."""
        self.rig, self.rng, self.log = rig, rng, []
        self.a = a = Arena(rig, rng)
        rig.arena = a
        n = rng.randint(0, 5)
        self.ids = ids = rng.sample(range(0, 30), n)
        self.entries = [a.at(ENTRY * k) for k in range(n)]
        next_node, next_rel = NODES, RELS
        for k, entry in enumerate(self.entries):
            a.put(entry + 0x10, ids[k])
            a.put(entry + 0x80, self.entries[k + 1] if k + 1 < n else 0)
            a.put(entry + 0x20, rng.choice([0, 1, 8, 9, 10, rng.getrandbits(16)]), 2)
            a.put(entry + 0x22, rng.choice([0xFFFF, 1, 2, 3, 0x7F, 0x101, 0x201, 0x1FF]), 2)
            a.put(entry + 0x34, a.at(CELLS + 0x10 * k) if rng.random() < 0.9 else 0)
            alt = 0
            for _ in range(rng.choice([0, 0, 1, 2])):          # second id list at +0x38
                node = a.at(next_node)
                a.put(node, rng.choice(ids + [40, 41, 0xFFFFFFFF]))
                a.put(node + 4, alt)
                alt, next_node = node, next_node + 8
            a.put(entry + 0x38, alt)
            prev = 0
            for _ in range(rng.choice([0, 1, 2, 3])):          # relation list: cell -> node -> node
                node = a.at(next_rel)
                a.put(node, rng.choice(ids + [40, 41, 0x77]))
                a.put(node + 4, rng.getrandbits(16))
                a.put(node + 8, prev)
                prev, next_rel = node, next_rel + 16
            a.put(a.at(CELLS + 0x10 * k), prev)
        self.head = self.entries[0] if n else 0
        rig.write_global(HEAD, self.head)
        self.unit = a.at(UNIT)
        a.put(self.unit, 0)
        a.put(self.unit + 0xC, rng.choice(ids + [99]) if ids else 99)
        a.put(self.unit + 0x14, rng.choice([0, 0, a.at(UNIT + 0x40)]))
        self.player = self.unit if rng.random() < 0.9 else 0
        rig.write_global(PLAYER, self.player)
        a.sync_pool()

    # --- model of the stock functions ------------------------------------------------------------
    def myid(self):
        """FUN_00463dd0's unit id, or -1 without a local player."""
        if self.player:
            return self.a.r32(self.player + 0xC)
        return 0 if mut("player-null-zero") else 0xFFFFFFFF

    def find(self, uid):
        """FUN_004792e0: the record whose +0x10 equals `uid` or whose +0x38 list holds it."""
        a = self.a
        if uid == 0xFFFFFFFF and not mut("find-minus-one-matches"):
            return 0
        found = 0
        entry = self.head
        while entry:
            if a.r32(entry + 0x10) == uid:
                if not mut("find-last-match"):
                    return entry
                found = entry
            node = 0 if mut("find-skips-id-list") else a.r32(entry + 0x38)
            while node:
                if a.r32(node) == uid:
                    if not mut("find-last-match"):
                        return node if mut("find-returns-node") else entry
                    found = entry
                node = a.r32(node + 4)
            entry = a.r32(entry + 0x80)
        return found

    def rel_cell(self, uid):
        """FUN_0047a310: the relation cell pointer of the record for `uid`, 0 without one."""
        entry = self.find(uid)
        return self.a.r32(entry + 0x34) if entry else 0

    def rel_get(self, mine, other, mask):
        """FUN_004dc440: the mask bits set in my relation to `other` (0 when absent or mine == other)."""
        a = self.a
        cell = self.rel_cell(mine)
        if not cell or mine == other:
            return 0
        node = a.r32(cell)
        while node:
            if a.r32(node) == other:
                return a.r32(node + 4) & mask
            node = a.r32(node + 8)
        return 0

    def rel_add(self, mine, other):
        """FUN_004dc320: add a relation node (mask 0) at the head unless one exists."""
        a = self.a
        cell = self.rel_cell(mine)
        if not cell or (mine == other and not mut("toggle-self-allowed")):
            return
        if not mut("add-skips-dup-check"):
            node = a.r32(cell)
            while node:
                if a.r32(node) == other:
                    return
                node = a.r32(node + 8)
        node = a.model_alloc(0xC)
        self.log.append(("alloc-b380", 0xC, 0x6DB9A4, 0x74, 0))
        a.w(node + 4, 0)
        a.w(node + 8, 0)
        a.w(node, other)
        a.w(node + 8, a.r32(cell))
        a.w(cell, node)

    def rel_set(self, mine, other, mask, on):
        """FUN_004dc3e0: set or clear mask bits of my relation to `other`."""
        a = self.a
        cell = self.rel_cell(mine)
        if not cell or (mine == other and not mut("toggle-self-allowed")):
            return
        node = a.r32(cell)
        while node:
            if a.r32(node) == other:
                a.w(node + 4, (a.r32(node + 4) | mask) if on else (a.r32(node + 4) & ~mask))
                return
            node = a.r32(node + 8)

    def toggle(self, entry, mask):
        """FUN_00479d30 (EBX the record, DI the mask): add the relation if needed; set every mask bit when none is set, else clear them all."""
        if not entry:
            return ("halt", FATAL["toggle"])
        mask &= 0xFFFF
        mine, other = self.myid(), self.a.r32(entry + 0x10)
        self.rel_add(mine, other)
        was = self.rel_get(mine, other, mask)
        on = 1 if (was == 0 or mut("toggle-sets-always")) else 0
        self.rel_set(mine, other, (mask ^ 4) if mut("toggle-wrong-mask") else mask, on)
        return ("ret", ANY)

    def squelch(self, entry):
        """FUN_00479e20: set or clear the ignore bit 4, send the 0x5d relation packet, then add or remove the ignore-list name."""
        if not entry and not mut("squelch-null-still-runs"):
            return ("ret", ANY)
        got = self.toggle(entry, 4)
        if got[0] == "halt":
            return got
        mine, other = self.myid(), self.a.r32(entry + 0x10)
        now = self.rel_get(mine, other, 4)
        self.log.append(("relation-send", 5 if mut("relation-send-type-5") else 3, other, (1 if now else 0) if mut("squelch-packet-flag-bool") else now))
        added = (self.rel_get(mine, other, 4) != 0) != mut("squelch-branches-swapped")
        self.log.append(("ignore-add" if added else "ignore-del", entry + 0x66))
        return ("ret", ANY)

    def hostile(self, entry):
        """FUN_00479fe0: set or clear the hostile bit 8 when the record's level (+0x20) is 9+ or the bit is already set; leave the area when shared."""
        a = self.a
        mine = self.myid()
        limit = 8 if mut("hostile-level-8") else 10 if mut("hostile-level-10") else 9
        if not entry:
            return ("ret", ANY)
        other = a.r32(entry + 0x10)
        if self.rel_get(mine, other, 8) == 0 and a.r16(entry + 0x20) < limit:
            return ("ret", ANY)
        got = self.toggle(entry, 8)
        if got[0] == "halt":
            return got
        if not mut("hostile-skips-send"):
            self.log.append(("relation-send", 4, other, self.rel_get(mine, other, 8)))
        sentinel = a.r8(entry + 0x22) != 0xFF if mut("hostile-sentinel-byte") else a.r16(entry + 0x22) != 0xFFFF
        if self.rel_get(mine, other, 8) != 0 and (sentinel or mut("hostile-no-area-sentinel")):
            own = self.find(mine)
            if not own:
                return ("halt", 0x74 if mut("self-fatal-line") else FATAL["self"])
            same = a.r8(own + 0x22) == a.r8(entry + 0x22) if mut("hostile-area-compare-byte") else a.r16(own + 0x22) == a.r16(entry + 0x22)
            if same or mut("hostile-area-ignored"):
                return self.leave_area()
        return ("ret", ANY)

    def self_entry(self):
        """FUN_00479320: the local player's record; fatal without one."""
        own = self.find(self.myid())
        if not own:
            return ("halt", 0x74 if mut("self-fatal-line") else FATAL["self"])
        return ("ret", own)

    def leave_area(self):
        """FUN_00479f60: clear the local record's area and set its +0x30 flag, redraw (logged); returns the record."""
        a = self.a
        own = self.find(self.myid())
        if not own:
            return ("halt", FATAL["leave"])
        a.w(own + 0x22, 0 if mut("leave-area-0") else 0xFFFF, 1 if mut("leave-area-byte") else 2)
        a.w(own + 0x30, 0 if mut("leave-flag-0") else 1)
        self.log.append(("redraw-479ab0",))
        self.log.append(("redraw-49a640",))
        return ("ret", own)

    def unit_name(self, unit):
        """FUN_0047a210: the name of the roster record for a player unit, else the unit's player data pointer."""
        a = self.a
        if not unit:
            return ("halt", FATAL["unit"])
        if (a.r8(unit) if mut("unit-type-byte") else a.r32(unit)) != 0 and not mut("unit-name-any-type"):
            return ("halt", FATAL["unit-type"])
        entry = self.find(a.r32(unit + 0xC))
        if entry:
            return ("ret", entry + 0x66)
        if mut("unit-name-no-fallback"):
            return ("ret", 0)
        data = a.r32(unit + 0x14)
        if data == 0 and not mut("unit-name-fallback-0"):
            return ("halt", FATAL["unit-none"])
        return ("ret", data)


def ids_to_try(ro):
    """Ids worth looking up: every record id, ids in the +0x38 lists, the player, absent ones, -1, 0."""
    seen = set(ro.ids) | {0, 0xFFFFFFFF, 40, 41, 0x77, 99, 7, 1 << 31}
    for entry in ro.entries:
        node = ro.a.r32(entry + 0x38)
        while node:
            seen.add(ro.a.r32(node))
            node = ro.a.r32(node + 4)
    return sorted(seen)


def check_simple(rig, rng):
    """The pure readers, the lookup and the self lookup: FUN_004795a0, 004795c0, 00479b90, 00479ba0, 0047a0c0, 0047a1e0, 004792e0, 00479320."""
    ro = Roster(rig, rng)
    a, count = ro.a, 0
    for entry in ro.entries + [0]:
        want = ("ret", a.r32(entry + 0x80) if entry else (0 if mut("next-null-returns-zero") else ro.head))
        count += verify(rig, ro, "next", 0x47_95A0, want, ecx=entry)
        want = ("ret", entry + (0x65 if mut("name-offset-0x65") else 0x66)) if entry else ("halt", FATAL["name"])
        count += verify(rig, ro, "name", 0x47_95C0, want, ecx=entry)
        level = (a.r32(entry + 0x20) if mut("level-32-bit") else a.r16(entry + 0x20)) if entry else 0
        count += verify(rig, ro, "level", 0x47_9B90, ("ret", level), ecx=entry)
        word = (a.r8(entry + 0x1C) if mut("word-1c-byte") else a.r32(entry + 0x1C)) if entry else 0
        count += verify(rig, ro, "word-1c", 0x47_9BA0, ("ret", word), ecx=entry)
        ident = a.r32(entry + 0x10) if entry else (0 if mut("id-null-zero") else 0xFFFFFFFF)
        count += verify(rig, ro, "id", 0x47_A0C0, ("ret", ident), ecx=entry)
        state = a.r32(entry + 0x24) if entry else (0xFFFFFFFF if mut("state-24-null-minus1") else 0)
        count += verify(rig, ro, "state-24", 0x47_A1E0, ("ret", state), ecx=entry)
    for uid in ids_to_try(ro):
        count += verify(rig, ro, "find", 0x47_92E0, ("ret", ro.find(uid)), edx=uid)
    count += verify(rig, ro, "self", 0x47_9320, ro.self_entry())
    return count


def check_setters(rig, rng):
    """FUN_0047a100 (store two words in the record for an id) and FUN_0047a690 (store a byte at +0x14)."""
    ro = Roster(rig, rng)
    a, count = ro.a, 0
    for uid in ids_to_try(ro):
        x, y = rng.getrandbits(32), rng.getrandbits(32)
        entry = ro.find(uid)
        if entry:
            a.w(entry + 0x28, y if mut("state-set-swapped") else x)
            a.w(entry + 0x2C, x if mut("state-set-swapped") else y)
        count += verify(rig, ro, "set-28", 0x47_A100, ("ret", ANY), ecx=uid, edx=x, args=(y,))
        value = rng.getrandbits(32)
        entry = ro.find(uid)
        if entry:
            a.w(entry + 0x14, value if mut("byte-store-dword") else value & 0xFF)
        count += verify(rig, ro, "set-14", 0x47_A690, ("ret", ANY), ecx=uid, edx=value)
    return count


def check_unit_name(rig, rng):
    """FUN_0047a210: player unit -> roster name, with the player-data fallback and its three fatal lines."""
    ro = Roster(rig, rng)
    a, count = ro.a, 0
    units = [0]
    for slot, kind in enumerate((0, 1, 2, 0x100)):           # FUN_0047a210 compares the whole type dword with 0
        for k, uid in enumerate((rng.choice(ro.ids) if ro.ids else 99, 0xFFFFFFFF, 123)):
            unit = a.at(UNIT + 0x80 + 0x100 * slot + 0x40 * k)
            a.put(unit, kind)
            a.put(unit + 0xC, uid)
            a.put(unit + 0x14, rng.choice([0, a.at(UNIT + 0x40)]))
            units.append(unit)
    for unit in units:
        count += verify(rig, ro, "unit-name", 0x47_A210, ro.unit_name(unit), ecx=unit)
    return count


def check_relations(rig, rng):
    """FUN_00479d30 / 00479e20 / 00479fe0 / 00479f60: the relation toggles, the sends and the hostile rules."""
    ro = Roster(rig, rng)
    count = 0
    for entry in ro.entries + [0]:
        for mask in (4, 8, 0x0C, 0x1_0004, 0):
            want = ro.toggle(entry, mask)
            count += verify(rig, ro, "toggle", 0x47_9D30, want, regs={"ebx": entry, "edi": mask}, keep=("esi", "ebx"))
        count += verify(rig, ro, "squelch", 0x47_9E20, ro.squelch(entry), ecx=entry)
        count += verify(rig, ro, "squelch-again", 0x47_9E20, ro.squelch(entry), ecx=entry)
        count += verify(rig, ro, "hostile", 0x47_9FE0, ro.hostile(entry), ecx=entry, fatal_from=(0x47_9320,))
        count += verify(rig, ro, "hostile-again", 0x47_9FE0, ro.hostile(entry), ecx=entry, fatal_from=(0x47_9320,))
    count += verify(rig, ro, "leave", 0x47_9F60, ro.leave_area())
    return count


def run_all():
    """All check rounds on fresh random rosters; the number of calls checked."""
    rig = Machine()
    count = 0
    for seed in range(1, 300):
        rng = random.Random(seed * 7919)
        count += check_simple(rig, rng) + check_setters(rig, rng) + check_unit_name(rig, rng) + check_relations(rig, rng)
    require(rig.called == set(COVERED), f"covered set differs: {sorted(hex(x) for x in set(COVERED) ^ rig.called)}")
    return count


def main():
    """Command line: run the checks, or with --mutation-check require every defect to be caught."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "ROSTER accessor and relation calls checked")


if __name__ == "__main__":
    main()
