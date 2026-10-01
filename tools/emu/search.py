"""A hostile monster's target search (FUN_005dd7f0) against ai.cpp's
search_target / rules::search_pick. Each case has random players with pets
(the merc, summons) in the player lists at game +0x10f8, and random acts,
rooms, towns, deaths, distances and walls. The AI flags (0x40, 8), outdoor
rooms and the spawn area's flag are random too.

    uv run python search.py [cases] [seed] [--dump]

game.exe runs natively apart from these hooks: the room (FUN_00620bb0),
outdoors (FUN_0061aa40), town (FUN_0061ab00), sight (FUN_00622aa0), in-melee
(FUN_00622c40), the skill-set target (FUN_005dd610: none) and alignment
(FUN_006259b0: hostile). Lists 8 and 9 (neutral and allied monsters) stay
empty; d2d has neither. The distance (FUN_005dc530) and death (FUN_005541b0)
are game.exe's own. Prints `ok` or each mismatch. `--dump` prints cases as
C++ lines for tests/test_monsters.cpp.
"""
import random
import sys

import emu


def ai_distance(dx, dy):
    dx, dy = abs(dx), abs(dy)
    return (min(dx, dy) + 2 * max(dx, dy)) // 2


def port(foes, best, need_sight):
    """components/rules/monsters.hpp search_pick, line for line."""
    target, nearest, skip = -1, 0x7fffffff, True
    for i, f in enumerate(foes):
        d = f["distance"]
        if not f["pet"]:
            skip = f["away"]
            if not skip: nearest = min(nearest, d)
            skip = skip or d >= 0x37
            if f["dead"]: d = 0x7fffffff
        if skip or d >= best or (need_sight and f["blocked"]): continue
        target, best = i, d
    return target, best, nearest


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = int(args[0]) if args else 3000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    dump = "--dump" in sys.argv
    e = emu.Emu()
    rooms, town, blocked, outdoor = {}, set(), set(), [False]
    e.hook(0x620bb0, lambda e: rooms.get(e.arg(0), 0), 1)
    e.hook(0x61aa40, lambda e: int(outdoor[0]), 1)
    e.hook(0x61ab00, lambda e: int(e.arg(0) in town), 1)
    e.hook(0x622aa0, lambda e: int(e.arg(1) in blocked), 3)
    e.hook(0x622c40, lambda e: 0x55, 3)
    e.hook(0x5dd610, lambda e: 0, 4)
    e.hook(0x6259b0, lambda e: 0, 1)

    def unit(kind, x, y, act, mode=1):
        u, path = e.alloc(0x100), e.alloc(0x10)
        e.w32(u, kind); e.w32(u + 0x10, mode); e.w32(u + 0x2c, path)
        e.mu.mem_write(u + 0x18, bytes([act]))
        e.mu.mem_write(path + 2, x.to_bytes(2, "little")); e.mu.mem_write(path + 6, y.to_bytes(2, "little"))
        return u

    game = e.alloc(0x1200)
    row, data, area, control = e.alloc(0x100), e.alloc(0x60), e.alloc(0x40), e.alloc(0x20)
    out_dist, out_melee = e.alloc(4), e.alloc(4)
    bad, mark, found, pets = 0, e.brk, 0, 0
    for case in range(cases):
        e.brk = mark
        rooms.clear(); town.clear(); blocked.clear()
        difficulty, aidist = rng.randrange(3), rng.choice((0, 0, 20, 35, 50))
        e.mu.mem_write(game + 0x6d, bytes([difficulty]))
        e.mu.mem_write(row + 0x52 + difficulty, bytes([aidist]))
        mx, my, act = 1000, 1000, rng.randrange(5)
        mon = unit(1, mx, my, act)
        e.w32(mon + 0x14, data); e.w32(data, row)
        rooms[mon] = e.alloc(4)
        outdoor[0] = rng.random() < 0.3
        force, sighted = rng.random() < 0.2, rng.random() < 0.3
        has_area, area_flag = rng.random() < 0.7, rng.random() < 0.4
        e.w32(data + 0x50, area if has_area else 0); e.w32(area + 0x24, int(area_flag))
        e.w32(control + 8, (0x40 if force else 0) | (8 if sighted else 0))
        foes, units = [], []
        spread = rng.choice((10, 40, 80))
        for slot in range(8):
            e.w32(game + 0x10f8 + 4 * slot, 0)
            if rng.random() < 0.5: continue
            nodes = []
            for n in range(1 + rng.choice((0, 0, 1, 2, 3))):
                pet = n > 0
                x, y = mx + rng.randint(-spread, spread), my + rng.randint(-spread, spread)
                far_act = rng.random() < 0.1
                mode = rng.choice((1, 1, 1, 0, 0x11 if not pet else 0xc))
                u = unit(1 if pet else 0, x, y, (act + 1) % 5 if far_act else act, mode)
                if rng.random() < 0.05: e.mu.mem_write(u + 0xc6, b"\x01")
                gone = rng.random() < 0.05
                if not gone: rooms[u] = e.alloc(4)
                in_town = not gone and rng.random() < 0.1
                if in_town: town.add(rooms[u])
                if rng.random() < 0.4: blocked.add(u)
                dead = not pet and (mode in (0, 0x11) or e.read(u + 0xc6, 1)[0] & 1 == 1)
                foes.append({"distance": ai_distance(x - mx, y - my), "pet": pet, "away": not pet and (far_act or gone or in_town),
                             "dead": dead, "blocked": u in blocked})
                units.append(u)
                node = e.alloc(0x10); e.w32(node, u)
                if nodes: e.w32(nodes[-1] + 8, node)
                nodes.append(node)
            e.w32(game + 0x10f8 + 4 * slot, nodes[0])
        for slot in (8, 9): e.w32(game + 0x10f8 + 4 * slot, 0)
        got = e.call(0x5dd7f0, control, out_dist, out_melee, ecx=game, edx=mon)
        # ai.cpp search_target: sight, then the pick, then the flags.
        need_sight = force or (not outdoor[0] and not sighted and not (has_area and area_flag))
        target, best, nearest = port(foes, aidist or 0x23, need_sight)
        want = units[target] if target >= 0 else 0
        want_flags = 8 if (sighted or target >= 0) else 0
        want_area = int(not (not sighted and area_flag)) if target >= 0 and has_area and not force and not outdoor[0] else int(area_flag)
        found += target >= 0; pets += target >= 0 and foes[target]["pet"]
        have = (got, e.s32(out_dist), e.r32(control + 8), e.r32(area + 0x24))
        need = (want, best if target >= 0 else nearest, want_flags, want_area)
        if have != need:
            bad += 1
            print(f"case {case}: game {have}, port {need}; foes {foes}")
        if dump and case < 16 and foes:
            fs = ", ".join(f"{{{f['distance']}, {int(f['pet'])}, {int(f['away'])}, {int(f['dead'])}, {int(f['blocked'])}}}" for f in foes)
            print(f"{{ {aidist or 0x23}, {int(need_sight)}, {target}, {need[1]}, {{ {fs} }} }},")
    print(f"ok: {cases} cases, {found} found a target, {pets} of them a pet" if not bad else f"{bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
