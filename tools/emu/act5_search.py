# SPDX-License-Identifier: GPL-3.0-or-later
"""FUN_005dd0b0 mode 1's Act 5 searches against rules::room_order (its first).

    uv run python act5_search.py [cases] [seed] [--dump]

The real mode-1 dispatcher and PutridDefiler / SiegeBeast callbacks run on
random near-room unit lists. Only unit properties, distances, and the room
topology are hooked. --dump prints C++ cases for tests/act5_search_cases.inc
(tests/monsters/test_monster_ai.cpp).
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu


def first_room_unit(units, rooms):
    for room in rooms:
        for i in range(len(units) - 1, -1, -1):
            if units[i]["room"] == room and units[i]["eligible"]:
                return i
    return -1


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = int(args[0]) if args else 3000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    dump = "--dump" in sys.argv
    e = emu.Emu()
    game, searcher = e.alloc(0x100), e.alloc(0x100)
    table = e.alloc(0x1000)
    e.w32(0x744304, table)
    e.w32(table + 0xa80, 0x300)
    cur = {}

    e.hook(0x620bb0, lambda e: cur["unit_room"].get(e.arg(0), 0), 1)

    def near_list(e):
        e.w32(e.arg(1), cur["room_list"])
        e.w32(e.arg(2), len(cur["rooms"]))
        return 0

    e.hook(0x619790, near_list, 3)
    e.hook(0x5d2140, lambda e: int(cur["info"][e.mu.reg_read(UC_X86_REG_EDX)]["impregnable"]), 0)
    e.hook(0x5dc380, lambda e: cur["info"][e.mu.reg_read(UC_X86_REG_EDX)]["distance"], 0)
    e.hook(0x63ea40, lambda e: int(not cur["info"][e.arg(0)]["alive"]), 1)
    e.hook(0x463860, lambda e: cur["info"][e.mu.reg_read(UC_X86_REG_ECX)]["base"], 0)
    e.hook(0x6259b0, lambda e: cur["info"][e.arg(0)]["align"], 1)
    e.hook(0x552fd0, lambda e: 0, 0)

    def ride_word(e):
        unit = e.mu.reg_read(UC_X86_REG_ECX)
        e.w32(cur["info"][unit]["control"] + 0x14, -1 if cur["info"][unit]["free"] else 0)
        return cur["info"][unit]["control"]

    e.hook(0x541860, ride_word, 0)
    e.hook(0x5b0bd0, lambda e: cur["info"][e.mu.reg_read(UC_X86_REG_EDX)]["squared"], 0)
    bad = found = dumped = 0
    for case in range(cases):
        kind = "defiler" if rng.random() < 0.5 else "imp"
        room_count = rng.randint(1, 4)
        rooms = [e.alloc(0x100) for _ in range(room_count)]
        rng.shuffle(rooms)
        room_list = e.alloc(4 * room_count)
        for i, room in enumerate(rooms):
            e.w32(room_list + 4 * i, room)
        units = []
        unit_room = {searcher: rooms[0]}
        info = {searcher: {"align": 0}}
        reach = rng.choice((5, 20, 40))
        for _ in range(rng.randint(1, 16)):
            unit = e.alloc(0x100)
            e.w32(unit, 1)
            control = e.alloc(0x40)
            room_index = rng.randrange(room_count)
            entry = {
                "unit": unit,
                "room": room_index,
                "impregnable": rng.random() < 0.75,
                "distance": rng.randint(0, 35),
                "alive": rng.random() < 0.85,
                "base": 0x1ec if rng.random() < 0.75 else 0x100,
                "align": rng.choice((0, 0, 0, 1, 2, 3)),
                "free": rng.random() < 0.75,
                "squared": rng.randint(0, reach * reach + 100),
            }
            entry["eligible"] = (entry["impregnable"] and entry["distance"] <= 25) if kind == "defiler" else (
                entry["alive"] and entry["base"] == 0x1ec and entry["align"] == 0
                and entry["free"] and entry["squared"] <= reach * reach)
            units.append(entry)
            unit_room[unit] = rooms[room_index]
            info[unit] = entry
            info[unit]["control"] = control
        for room_index, room in enumerate(rooms):
            in_room = [entry["unit"] for entry in reversed(units) if entry["room"] == room_index]
            for i, unit in enumerate(in_room):
                e.w32(unit + 0xe8, in_room[i + 1] if i + 1 < len(in_room) else 0)
            e.w32(room + 0x74, in_room[0] if in_room else 0)
        cur = {"rooms": rooms, "room_list": room_list, "unit_room": unit_room, "info": info}
        ctx = e.alloc(0x40)
        e.w32(ctx, reach * reach)
        e.w32(ctx + 4, 25)
        callback = 0x5efa50 if kind == "defiler" else 0x5e1720
        got = e.call(0x5dd0b0, ctx, callback, 1, ecx=game, edx=searcher)
        want = first_room_unit(units, list(range(room_count)))
        have = next((i for i, entry in enumerate(units) if entry["unit"] == got), -1)
        if have != want:
            bad += 1
            if bad < 20:
                print(f"case {case} {kind}: game {have}, port {want}")
        found += want >= 0
        if dump and dumped < 20 and want >= 0 and sum(entry["eligible"] for entry in units) > 1:
            dumped += 1
            us = ", ".join(f"{{ {entry['room']}, {str(entry['eligible']).lower()} }}" for entry in units)
            rs = ", ".join(str(i) for i in range(room_count))
            print(f"{{ {{ {us} }}, {{ {rs} }}, {want} }},   // {kind}")
    print(f"ok: {cases} cases, {found} found" if not bad else f"{bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
