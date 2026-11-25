# SPDX-License-Identifier: GPL-3.0-or-later
"""Run game.exe's one-way player-relation nodes and emit d2d test cases.

FUN_0055b140, 0055b1e0, 0055b260, 0055b300, 0055b6c0 and 0055b720 run
for real on synthetic player units and player-data blocks. Unit-data lookup,
state 7, party lookup, pool allocation/free, GetTickCount and S->C 0x8c are
hooked and checked at their boundaries. The emitted rows apply the same two
flag changes and stamp to rules::PlayerRelations.

Run from tools/emu:
    .venv/bin/python player_relations.py --dump ../../tests/player_relations_cases.inc --mutation-check
"""
import argparse
import itertools

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu


class Rig:
    """Synthetic units around the stock relation-list functions."""

    def __init__(self):
        """Allocate the game, two player units and their data blocks; hook the callees."""
        self.now = 0x12345678
        emu.Emu.w_GetTickCount = lambda _current: (self.now, 0)
        self.machine = machine = emu.Emu()
        self.game = machine.alloc(0x100)
        machine.w32(self.game + 0x1C, machine.alloc(4))
        self.self_unit = machine.alloc(0x100)
        self.other_unit = machine.alloc(0x100)
        self.self_data = machine.alloc(0x200)
        self.other_data = machine.alloc(0x200)
        machine.w32(self.self_unit, 0)
        machine.w32(self.self_unit + 0xC, 10)
        machine.w32(self.other_unit, 0)
        machine.w32(self.other_unit + 0xC, 20)
        self.party = {self.self_unit: 0xFFFF, self.other_unit: 7}
        self.sent = []
        self.freed = []

        def unit_data(current):
            """Hook FUN_006221a0: a unit's player data block."""
            unit = current.arg(0)
            return {self.self_unit: self.self_data, self.other_unit: self.other_data}.get(unit, 0)

        def state(current):
            """Hook FUN_00639df0: state 7 is never on (the relation-add gate)."""
            assert current.arg(1) == 7
            return 0

        def party(current):
            """Hook FUN_00554630: the unit's party id, 0xffff for none."""
            return self.party.get(current.mu.reg_read(UC_X86_REG_ECX), 0xFFFF)

        def allocate(current):
            """Hook the 20-byte node allocation (FUN_0040b430)."""
            assert current.mu.reg_read(UC_X86_REG_EDX) == 20
            block = current.alloc(20)
            return block

        def release(current):
            """Hook FUN_0040b480: record the node freed."""
            self.freed.append(current.mu.reg_read(UC_X86_REG_EDX))
            return 0

        def send(current):
            """Hook FUN_0053de20: record the S->C 0x8c sent (to, flags)."""
            self.sent.append((current.arg(0), current.arg(1) & 0xFFFF))
            return 0

        machine.hook(0x6221A0, unit_data, 1)
        machine.hook(0x639DF0, state, 2)
        machine.hook(0x554630, party, 0)
        machine.hook(0x40B430, allocate, 3)
        machine.hook(0x40B480, release, 3)
        machine.hook(0x53DE20, send, 2)

    def add(self):
        """Run FUN_0055b140: add self's node for the other player."""
        self.machine.call(0x55B140, self.other_unit, ecx=self.game, edx=self.self_unit)

    def remove(self):
        """Run FUN_0055b1e0: unlink and free that node."""
        self.machine.call(0x55B1E0, self.other_unit, ecx=self.game, edx=self.self_unit)

    def set(self, mask, on):
        """Run FUN_0055b260: set or clear `mask` in the node."""
        self.machine.call(0x55B260, self.other_unit, mask, on, ecx=self.game, edx=self.self_unit)

    def get(self, mask):
        """Run FUN_0055b300: the node's flags & `mask`."""
        return self.machine.call(0x55B300, mask, ecx=self.self_unit, edx=self.other_unit)

    def stamp(self, value):
        """Run FUN_0055b720: write the pair's hostile stamp."""
        self.machine.call(0x55B720, value, ecx=self.self_unit, edx=self.other_unit)

    def get_stamp(self):
        """Run FUN_0055b6c0: read the pair's hostile stamp."""
        return self.machine.call(0x55B6C0, ecx=self.self_unit, edx=self.other_unit)

    def node(self):
        """The head of self's relation list (player data +0x38)."""
        return self.machine.r32(self.self_data + 0x38)


def stock_rows(mutation=None):
    rows = []
    rig = Rig()
    masks = (1, 2, 4, 8, 3, 12, 0x80000000)
    for index, (first_mask, second_mask, first_on, second_on, query_mask) in enumerate(
            itertools.product(masks, masks, (0, 1), (0, 1), masks)):
        rig.add()
        node = rig.node()
        assert node and rig.machine.r32(node) == 20
        assert rig.machine.r32(node + 4) == 0 and rig.machine.r32(node + 8) == 0
        assert rig.machine.r32(node + 12) == 1 and rig.machine.r32(node + 16) == 0
        rig.add()
        assert rig.node() == node
        rig.set(first_mask, first_on)
        rig.set(second_mask, second_on)
        stamp = (0x10203040 + index * 0x10101) & 0xFFFFFFFF
        rig.stamp(stamp)
        actual_flags = rig.machine.r32(node + 4)
        actual_query = rig.get(query_mask)
        flags = actual_flags
        query = actual_query
        if mutation == "hostile-bit" and first_mask == 8:
            flags ^= 8
        if mutation == "clear-is-set" and not second_on:
            flags |= second_mask
        if mutation == "query-whole":
            query = flags
        rows.append((first_mask, second_mask, query_mask, stamp, flags, query, first_on, second_on))
        assert rig.get_stamp() == stamp
        assert rig.machine.r32(rig.self_data + 0x160) == rig.now
        assert rig.sent[-1] == (20, actual_flags & 0xFFFF)
        rig.remove()
        assert rig.node() == 0 and rig.freed[-1] == node
    return rows


def main():
    """Check the frozen cases against game.exe, or dump them; --mutation-check runs the negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rows = stock_rows()
    if args.mutation_check:
        for mutation in ("hostile-bit", "clear-is-set", "query-whole"):
            assert stock_rows(mutation) != rows, f"negative control escaped: {mutation}"
            print(f"negative control caught: {mutation}")
    if args.dump:
        with open(args.dump, "w", encoding="utf-8") as output:
            output.write("// Stock game.exe 1.14d relation nodes; generated by tools/emu/player_relations.py.\n")
            output.write("// { first mask, second mask, query mask, stamp, final flags, query result, first on, second on }\n")
            for row in rows:
                first_mask, second_mask, query_mask, stamp, flags, query, first_on, second_on = row
                output.write(f"    {{ {first_mask:#010x}u, {second_mask:#010x}u, {query_mask:#010x}u, {stamp:#010x}u, "
                             f"{flags:#010x}u, {query:#010x}u, {first_on}, {second_on} }},\n")
    print(f"ok: {len(rows)} FUN_0055b140-family relation cases")


if __name__ == "__main__":
    main()
