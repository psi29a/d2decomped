# SPDX-License-Identifier: GPL-3.0-or-later
"""A set's bonus list as the oracle: FUN_00663b40 -> FUN_00660120, run on a set piece going on.

    uv run python set_list.py gen 2000 1 > cases.tsv
    uv run python set_list.py game cases.tsv > g.txt     # each equip: the piece handed on and the rows run
    uv run python set_list.py inc g.txt > ../../tests/set_list_cases.inc

Equipping a set piece (FUN_0055c2c0 -> FUN_00663cc0(owner, item, 0, 0)) runs
FUN_00663b40 with 0: it finds the owner's stat list for the item's set (one
of six, states 0x6ee424, by stat 0x47 = the Sets.txt row) and clears it
(FUN_00627340), or makes one in the first free state, then FUN_00660120
(ECX owner, EDX the item, the state) refills it: with n = 0x6eda40[mask]
(FUN_0062a370's worn-member mask) the partial rows 0 .. min(n, pieces - 1) * 2 - 3
whose property is set, then with n >= pieces the full rows from the first
until one is unset. Every row goes through FUN_0065fe10 (kind 4) with the
item in ESI, which hands it to the Properties funcs (FUN_0065fd70): so
dmg-min / dmg-max land by that piece (FUN_0065ecf0 / FUN_0065ee80).
Unequipping (FUN_00663cc0(.., 1, 0) -> FUN_00663b40 with 1) frees the list
(FUN_006277e0, FUN_00626cd0) and runs no row.

Case lines, one run at a time (the owner's lists start empty each run):
  S run set pieces partial full    a set: its Sets.txt row, its piece count,
                                   which of the 8 partial / 8 full rows are set (bit i)
  E run item set member on mask    an event: the item (its unit id) goes on (1) or
                                   off (0); mask: its set's members worn after it
`game` runs FUN_00663b40 for real (FUN_00660120 and the 0x6eda40 count
too), with the owner's stat lists a model in Python, and prints each S line
and each E line followed by `piece <id>` (FUN_0065fe10's ESI item, or -1
for none) and the rows handed on (partial i as i, full j as 8 + j, `-`
none). tests/test_set_list.cpp reads the same lines and prints d2d's.

What the run shows (gen 2000 1): on every equip all the rows get the item
going on, never another piece; an unequip runs none.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_ESI

import emu

SET_LIST = 0x663B40
DATA_TABLES, SETS_COUNT = 0x744304, 0xC10        # FUN_00663b40 bounds the set row by the Sets count there
SETS_RECORD_PARTIAL, SETS_RECORD_FULL, ROW_SIZE = 0x10, 0x90, 0x10


def gen(count, seed):
    rng = random.Random(seed)
    item_id = 1
    for run in range(count):
        sets = rng.sample(range(40), rng.choice((1, 1, 2, 3)))
        members = {}
        for set_row in sets:
            pieces = rng.randint(2, 6)
            partial = rng.randrange(256) if rng.random() < 0.7 else rng.choice((0, 1, 5, 0x15, 0xFF))
            full = (1 << rng.randint(0, 8)) - 1 if rng.random() < 0.7 else rng.randrange(256)
            print("S", run, set_row, pieces, partial, full)
            members[set_row] = list(range(item_id, item_id + pieces))
            item_id += pieces
        worn = set()
        for _ in range(rng.randint(2, 14)):
            set_row = rng.choice(sets)
            ids = members[set_row]
            item = rng.choice(ids)
            on = item not in worn if rng.random() < 0.8 else rng.random() < 0.5
            if on:
                worn.add(item)
            else:
                worn.discard(item)
            mask = sum(1 << index for index, each in enumerate(ids) if each in worn)
            print("E", run, item, set_row, ids.index(item), int(on), mask)


class Owner:
    """The owner's stat lists: handle -> [state, set row], those attached."""

    def __init__(self, e):
        self.e = e
        self.lists = {}

    def find(self, state):
        return next((handle for handle, (each, _) in self.lists.items() if each == state), 0)


def game(path):
    e = emu.Emu()
    owner_unit = e.alloc(0x100)                   # type 0, a player: FUN_0062a0f0 isn't asked
    tables = e.alloc(0x1000)
    e.w32(tables + SETS_COUNT, 64)
    e.w32(DATA_TABLES, tables)
    items, records, sets = {}, {}, {}
    now = {"mask": 0, "owner": Owner(e), "calls": []}

    def item_unit(item_id, set_row):
        if item_id not in items:
            unit = e.alloc(0x200)
            e.w32(unit, 4)
            e.w32(unit + 0xC, item_id)
            records[unit] = e.alloc(0x40)
            e.mu.mem_write(records[unit] + 0x2C, (set_row & 0xFFFF).to_bytes(2, "little"))
            items[item_id] = unit
        return items[item_id]

    def fe10(e):
        row_ptr = e.mu.reg_read(UC_X86_REG_EDI)
        record = sets[now["set"]]
        row = (row_ptr - record - SETS_RECORD_PARTIAL) // ROW_SIZE
        now["calls"].append((e.r32(e.mu.reg_read(UC_X86_REG_ESI) + 0xC), row, e.arg(3)))
        return 0

    def new_list(e):
        handle = e.alloc(0x40)
        now["owner"].lists[handle] = [0, -1]
        return handle

    e.hook(0x627E70, lambda e: 5, 1)                       # item quality: set
    e.hook(0x629DA0, lambda e: e.arg(0), 1)                # the item's record: the unit itself
    e.hook(0x483440, lambda e: records[e.mu.reg_read(UC_X86_REG_ECX)], 0)   # the set item's record: +0x2c its set
    e.hook(0x483410, lambda e: sets.get(e.mu.reg_read(UC_X86_REG_ECX), 0), 0)  # the Sets.txt record
    e.hook(0x62A370, lambda e: now["mask"], 3)
    e.hook(0x62A0F0, lambda e: 0, 1)
    e.hook(0x6256B0, lambda e: now["owner"].find(e.arg(1)), 2)
    e.hook(0x625D00, lambda e: now["owner"].lists.get(e.arg(0), [0, -1])[1] if e.arg(1) == 0x47 else 0, 3)
    e.hook(0x627340, lambda e: 0, 1)                       # clear: the model keeps only state and set
    e.hook(0x6251F0, new_list, 5)
    e.hook(0x626E10, lambda e: 0, 3)
    e.hook(0x6252D0, lambda e: now["owner"].lists[e.arg(0)].__setitem__(0, e.arg(1)) or 0, 2)
    e.hook(0x627030, lambda e: now["owner"].lists[e.arg(0)].__setitem__(1, e.arg(2)) or 0, 4)
    e.hook(0x6277E0, lambda e: now["owner"].lists.pop(e.arg(1), None) and 0, 2)
    e.hook(0x626CD0, lambda e: 0, 1)
    e.hook(0x65FE10, fe10, 9)

    last = None
    for line in open(path):
        fields = line.split()
        run = int(fields[1])
        if run != last:
            now["owner"] = Owner(e)
            sets.clear()
            last = run
        if fields[0] == "S":
            set_row, pieces, partial, full = map(int, fields[2:])
            record = e.alloc(0x120)
            e.w32(record + 0xC, pieces)
            for index in range(8):
                e.w32(record + SETS_RECORD_PARTIAL + ROW_SIZE * index, 0 if partial >> index & 1 else -1)
                e.w32(record + SETS_RECORD_FULL + ROW_SIZE * index, 0 if full >> index & 1 else -1)
            sets[set_row] = record
            print(" ".join(fields))
            continue
        item_id, set_row, _member, on, mask = map(int, fields[2:])
        now.update(mask=mask, set=set_row, calls=[])
        e.call(SET_LIST, 0 if on else 1, ecx=owner_unit, edx=item_unit(item_id, set_row))
        pieces = {piece for piece, _, _ in now["calls"]}
        piece = pieces.pop() if len(pieces) == 1 else -1 if not pieces else "mixed"
        print(" ".join(fields), "piece", piece, "rows", " ".join(str(row) for _, row, _ in now["calls"]) or "-")


def inc(path):
    lines = [line.strip() for line in open(path) if line.strip()]
    runs = len({line.split()[1] for line in lines})
    print(f"// Stock game.exe 1.14d, FUN_00663b40 -> FUN_00660120; generated by tools/emu/set_list.py (gen {runs} 1, then game, then inc).")
    print("// S run set pieces partial full; E run item set member on mask, then the piece handed on and its rows")
    for line in lines:
        print(f'    "{line}",')


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "inc":
        inc(sys.argv[2])
    else:
        sys.exit(__doc__)
