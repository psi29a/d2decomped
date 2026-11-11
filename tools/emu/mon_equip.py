# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's MonEquip gear walk, by game.exe 1.14d's own FUN_005d6b60.

    uv run python mon_equip.py gen 400 20261111 > cases.txt     # synthetic MonEquip tables and calls
    uv run python mon_equip.py game cases.txt > game.txt         # game.exe's answers
    uv run python mon_equip.py inc cases.txt game.txt > ../../tests/monsters/mon_equip_cases.inc

tests/monsters/test_mon_equip.cpp runs rules::monster_gear on the same cases
and checks it against the .inc. The tables are synthetic (no game data):
each has rows for three monsters 0..2 (levels, oninit, item codes "aaa",
"bbb", "ccc" or "    ", body slots and qualities including ones outside
1..10 and 0..7) and each monster's first row (MonStats +0x2a, sometimes
another monster's row or none). A case: a table, a monster, its level, the
oninit flag, an owner (none, or the body slots it wears), the slots the
monster already wears and its unit seed (+0x20 / +0x24).

Game side: a fake data table block at 0x744304 (MonStats rows of 0x1a8 at
+0xa78, count +0xa80; the 0x1c-byte MonEquip rows at +0xbe0, ended by a
monster -1 row as FUN_00659e60 ends them), FUN_005d30a0 and FUN_005d6b60
run for real. Hooked (not the claim): FUN_0063bde0 (the item in a body
slot: the case's worn slots, the owner's slots, and every slot an earlier
call filled), FUN_00628590 (the owner's item: a marker the output shows as
"own") and FUN_00573b20 (makes the item: recorded; a code that isn't "    "
fills its slot). The call's ilvl must reach FUN_00573b20 unchanged.

Output `id | seed_low seed_high calls`: each FUN_00573b20 call as
`code/slot/quality` ("____" for "    "), "-" for none.
"""
import random
import struct
import sys

CODES = (b"aaa ", b"bbb ", b"ccc ", b"    ")
SPACES = 0x20202020


def gen(count, seed):
    rng = random.Random(seed)
    tables = 8
    for table in range(tables):
        rows = []
        for monster in range(3):
            levels = sorted((rng.randint(0, 30) for _ in range(rng.randint(0, 6))), reverse=True)
            for level in levels:
                slots = [rng.choice((1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 4, 0, 11, 12, -3)) for _ in range(3)]
                codes = [rng.choice(CODES).decode().replace(" ", "_") or "_" for _ in range(3)]
                qualities = [rng.choice((2, 3, 4, 6, 7, 0, -1, 8, 9)) for _ in range(3)]
                rows.append((monster, level, int(rng.random() < 0.3), codes, slots, qualities))
        if rng.random() < 0.5:
            rng.shuffle(rows)
        firsts = []
        for monster in range(3):
            mine = [i for i, row in enumerate(rows) if row[0] == monster]
            pick = rng.random()
            firsts.append(-1 if not rows or pick < 0.1 else rng.randrange(len(rows)) if pick < 0.2 or not mine else mine[0])
        print("T", table, *firsts)
        for monster, level, oninit, codes, slots, qualities in rows:
            print("R", table, monster, level, oninit, ",".join(codes), ",".join(map(str, slots)), ",".join(map(str, qualities)))
    for case in range(count):
        owner = -1 if rng.random() < 0.5 else rng.getrandbits(11) & ~1
        filled = 0 if rng.random() < 0.6 else rng.getrandbits(11) & ~1
        print("C", case, rng.randrange(tables), rng.randrange(3), rng.randint(0, 35), int(rng.random() < 0.3), owner, filled,
              rng.getrandbits(32), 666 if rng.random() < 0.7 else rng.getrandbits(32))


def read_cases(path):
    tables, cases = {}, []
    for line in open(path):
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "T":
            tables[int(fields[1])] = {"firsts": [int(v) for v in fields[2:5]], "rows": []}
        elif fields[0] == "R":
            codes = [code.replace("_", " ").ljust(4) for code in fields[5].split(",")]
            tables[int(fields[1])]["rows"].append((int(fields[2]), int(fields[3]), int(fields[4]), codes,
                                                   [int(v) for v in fields[6].split(",")], [int(v) for v in fields[7].split(",")]))
        elif fields[0] == "C":
            cases.append([int(v) for v in fields[1:]])
    if not cases:
        sys.exit(f"{path}: no cases")
    return tables, cases


def game(path):
    import emu
    tables, cases = read_cases(path)
    e = emu.Emu()
    block = e.alloc(0x2000)
    e.w32(0x744304, block)
    monstats = e.alloc(3 * 0x1a8)
    e.w32(block + 0xa78, monstats)
    e.w32(block + 0xa80, 3)
    equip = e.alloc(64 * 0x1c)
    e.w32(block + 0xbe0, equip)
    unit, owner, game_block = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
    unit_inv, owner_inv = e.alloc(0x40), e.alloc(0x40)
    state = {}

    def slot_item(e):
        inv, slot = e.arg(0), e.arg(1)
        if inv == owner_inv:
            return 0x0D000000 | slot if state["owner"] >> slot & 1 else 0
        return 1 if state["filled"] >> slot & 1 else 0

    def make(e):
        record, ilvl, quality = e.arg(0), e.arg(1), e.arg(2)
        code, slot = e.r32(record), e.read(record + 4, 1)[0]
        assert ilvl == state["ilvl"], (ilvl, state["ilvl"])
        if code >> 24 == 0x4F:
            name = "own"
        else:
            name = struct.pack("<I", code).decode("latin1").rstrip(" ") or "____"
        state["calls"].append(f"{name}/{slot}/{quality}")
        if code != SPACES:
            state["filled"] |= 1 << slot
        return 0

    e.hook(0x63bde0, slot_item, 2)
    e.hook(0x628590, lambda e: 0x4F000000 | (e.arg(0) & 0xFF), 1)
    e.hook(0x573b20, make, 3)
    for case_id, table_id, monster, level, oninit, owner_mask, filled, low, high in cases:
        table = tables[table_id]
        rows = table["rows"] + [(-1, 0, 0, ["    "] * 3, [0, 0, 0], [0, 0, 0])]
        data = b""
        for mon, row_level, row_oninit, codes, slots, qualities in rows:
            record = struct.pack("<hhB3x", mon, row_level, row_oninit) + b"".join(code.encode("latin1") for code in codes)
            record += bytes(v & 0xFF for v in slots) + bytes(v & 0xFF for v in qualities) + b"\0\0"
            data += record
        e.mu.mem_write(equip, data)
        for mon in range(3):
            e.mu.mem_write(monstats + mon * 0x1a8 + 0x2a, struct.pack("<h", table["firsts"][mon]))
        e.mu.mem_write(unit, b"\0" * 0x100)
        e.w32(unit, 1); e.w32(unit + 4, monster); e.w32(unit + 0x20, low); e.w32(unit + 0x24, high); e.w32(unit + 0x60, unit_inv)
        e.w32(owner + 0x60, owner_inv)
        state.update(owner=max(owner_mask, 0), filled=filled, ilvl=level + 7, calls=[])
        e.call(0x5d6b60, unit, 0x20, level, level + 7, oninit, ecx=game_block, edx=owner if owner_mask >= 0 else 0)
        print(case_id, "|", e.r32(unit + 0x20), e.r32(unit + 0x24), ",".join(state["calls"]) or "-")


def inc(cases_path, game_path):
    tables, cases = read_cases(cases_path)
    answers = {}
    for line in open(game_path):
        fields = line.split()
        if len(fields) == 5 and fields[1] == "|":
            answers[int(fields[0])] = fields[2:]
    missing = [case[0] for case in cases if case[0] not in answers]
    if missing:
        sys.exit(f"{game_path}: no answer for cases {missing[:5]}")
    print("// Stock game.exe 1.14d, FUN_005d6b60 on synthetic MonEquip tables; generated by tools/emu/mon_equip.py.")
    print("// #ifdef MON_EQUIP_ROWS: table, monster, level, oninit, codes, slots, qualities; MON_EQUIP_FIRSTS: table, each monster's first row;")
    print("// else the cases: table, monster, level, oninit, owner's slots (-1 none), worn slots, seed low / high -> seed after, calls.")
    print("#ifdef MON_EQUIP_ROWS")
    for table_id, table in sorted(tables.items()):
        for mon, level, oninit, codes, slots, qualities in table["rows"]:
            quoted = ", ".join(f'"{code}"' for code in codes)
            print(f"    {{ {table_id}, {mon}, {level}, {oninit}, {{ {quoted} }}, {{ {', '.join(map(str, slots))} }}, {{ {', '.join(map(str, qualities))} }} }},")
    print("#elif defined(MON_EQUIP_FIRSTS)")
    for table_id, table in sorted(tables.items()):
        print(f"    {{ {table_id}, {{ {', '.join(map(str, table['firsts']))} }} }},")
    print("#else")
    for case_id, table_id, monster, level, oninit, owner_mask, filled, low, high in cases:
        after_low, after_high, calls = answers[case_id]
        print(f'    {{ {table_id}, {monster}, {level}, {oninit}, {owner_mask}, {filled}, {low}u, {high}u, {after_low}u, {after_high}u, "{calls}" }},')
    print("#endif")


def main():
    args = sys.argv[1:]
    if args[:1] == ["gen"] and len(args) <= 3:
        gen(int(args[1]) if len(args) > 1 else 400, int(args[2]) if len(args) > 2 else 20261111)
    elif args[:1] == ["game"] and len(args) == 2:
        game(args[1])
    elif args[:1] == ["inc"] and len(args) == 3:
        inc(args[1], args[2])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
