# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's target search (FUN_005dd7f0) against ai.cpp's search_target
/ rules::search_pick / rules::search_near. Each case has random players with
pets (the merc, summons) in the player lists at game +0x10f8, monsters in
lists 8 (good) and 9 (neutral) and evil ones besides, all in random rooms
(near, town, unsearched, none), acts, deaths, distances, sizes, threats and
walls. The searcher is evil or neutral, with no skill-set target, Attract's
(kind 2: a monster by id, maybe gone) or Confuse's (kind 3), a random seed;
the AI flags (0x40, 8), outdoor rooms and the spawn area's flag are random.

    uv run python search.py [cases] [seed] [--dump] [--break]

game.exe runs natively, FUN_005dd610's skill-set target and FUN_005dd0b0's
mode 5 room search included, apart from these hooks: the room
(FUN_00620bb0), outdoors (FUN_0061aa40), town (FUN_0061ab00), sight
(FUN_00622aa0), in-melee (FUN_00622c40), alignment (FUN_006259b0 /
FUN_005543b0, a table), size (FUN_00620510), the area at a spot
(FUN_0061b130), and FUN_005dd510's pathfinder (FUN_00649970: a path is
always found, as d2d has it). The MonStats rows (threat) are a fake table.
Prints `ok` or each mismatch. `--dump` prints cases as C++ lines for
tests/test_monsters.cpp; `--break` breaks the port (list 9 ignores sight,
threat 2 counts as low) to show the check bites.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

BREAK = "--break" in sys.argv


def ai_distance(dx, dy):
    dx, dy = abs(dx), abs(dy)
    return (min(dx, dy) + 2 * max(dx, dy)) // 2


def near_distance(dx, dy, size):
    return ai_distance(max(abs(dx) - size, 0), max(abs(dy) - size, 0))


def friends(a, b):
    return b == 0 if a == 0 else a == 2 and b == 2


def confuse_align(align, draw):
    return (2 if draw else 0) if align == 1 else (2 - align if draw else align)


def search_pick(foes, best, need_sight):
    """components/rules/monsters.hpp search_pick, line for line."""
    target, nearest, skip, nine, nine_best = -1, 0x7fffffff, True, -1, 0x7fffffff
    for i, f in enumerate(foes):
        d = f["distance"]
        if f["list"]:
            if f["away"] or (need_sight and f["blocked"] and not (BREAK and f["list"] == 9)): continue
            if f["list"] == 8 and d < best: target, best = i, d
            if f["list"] == 9 and d < nine_best: nine, nine_best = i, d
            continue
        if not f["pet"]:
            skip = f["away"]
            if not skip: nearest = min(nearest, d)
            skip = skip or d >= 0x37
            if f["dead"]: d = 0x7fffffff
        if skip or d >= best or (need_sight and f["blocked"]): continue
        target, best = i, d
    if nine >= 0 and target < 0: target, best = nine, nine_best
    return target, best, nearest


def search_near(foes, align, need_sight):
    """components/rules/monsters.hpp search_near, line for line."""
    pick = [-1, 0x7fffffff, -1, 0x7fffffff]
    for i, f in enumerate(foes):
        if f["skip"]: continue
        if not f["self"] and not f["dead"] and not friends(align, f["align"]):
            k = 0 if f["threat"] >= 2 + BREAK else 2
            if f["distance"] > 0x23 or f["distance"] >= pick[k + 1] or (need_sight and f["blocked"]): continue
            pick[k], pick[k + 1] = i, f["distance"]
        elif f["monster"] and align == 0 and need_sight and f["waking"]:
            need_sight = False
    return pick


def search_sight(foes):
    """monsters.hpp search_sight, line for line: (first, best, second, best)."""
    first, fb, second, sb = -1, 0x7fffffff, -1, 0x7fffffff
    for i, f in enumerate(foes):
        if not f["enemy"] or f["distance"] >= 0x31: continue
        primary = f["threat"] >= 2
        if f["distance"] >= (fb if primary else sb) or f["blocked"]: continue
        if primary: first, fb = i, f["distance"]
        else: second, sb = i, f["distance"]
    return first, fb, second, sb


def sight_choice(foes, pick, path):
    """monsters.hpp sight_choice / search_threat, line for line: (index, distance)."""
    first, fb, second, sb = pick
    if second < 0 or first < 0: return (first, fb) if first >= 0 else (second, sb)
    if sb < 6 and not path:
        best, bd = -1, 0x7fffffff
        for i, f in enumerate(foes):
            if i == first or not f["enemy"] or f["distance"] >= 0x31 or f["threat"] < 2 or f["distance"] >= bd or f["blocked"]: continue
            best, bd = i, f["distance"]
        if best >= 0 and bd <= 0x13: return best, bd
        return second, sb
    return first, fb


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = int(args[0]) if args else 3000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    dump = "--dump" in sys.argv
    e = emu.Emu()
    rooms, town, blocked, outdoor, align, size, area = {}, set(), set(), [False], {}, {}, {}
    e.hook(0x620bb0, lambda e: rooms.get(e.arg(0), 0), 1)
    e.hook(0x61aa40, lambda e: int(outdoor[0]), 1)
    e.hook(0x61ab00, lambda e: int(e.arg(0) in town), 1)
    e.hook(0x622aa0, lambda e: int(e.arg(0) in blocked or e.arg(1) in blocked), 3)
    e.hook(0x622c40, lambda e: 0x55, 3)
    e.hook(0x6259b0, lambda e: align.get(e.arg(0), 2), 1)
    e.hook(0x5543b0, lambda e: align.__setitem__(e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EDX) & 0xff), 1)
    e.hook(0x620510, lambda e: size.get(e.arg(0), 2), 1)
    e.hook(0x61b130, lambda e: area.get(e.arg(0), 0), 3)
    e.hook(0x649970, lambda e: 0, 3)
    hash1 = e.r32(0x6e10e0 + 4)                        # FUN_00552f60: type 1's id hash in the game

    def unit(kind, x, y, act, mode=1):
        u, path = e.alloc(0x100), e.alloc(0x100)
        e.w32(u, kind); e.w32(u + 0x10, mode); e.w32(u + 0x2c, path); e.w32(u + 0xc4, 4)
        e.mu.mem_write(u + 0x18, bytes([act]))
        e.mu.mem_write(path + 2, x.to_bytes(2, "little")); e.mu.mem_write(path + 6, y.to_bytes(2, "little"))
        e.w32(path + 0x28, 1)                           # FUN_00648780: a path
        return u

    tables, rows = e.alloc(0x1000), e.alloc(0x1a8 * 8)
    e.w32(0x744304, tables); e.w32(tables + 0xa78, rows); e.w32(tables + 0xa80, 8)
    threats = (0, 1, 2, 10, 14, 0, 1, 11)
    for k, t in enumerate(threats): e.mu.mem_write(rows + 0x1a8 * k + 0x4e, bytes([t]))
    game = e.alloc(0x2000)
    row, data, spawn, control = e.alloc(0x100), e.alloc(0x60), e.alloc(0x40), e.alloc(0x40)
    out_dist, out_melee = e.alloc(4), e.alloc(4)
    near_room = [e.alloc(0x100) for _ in range(4)]     # A (its own), B, T (town), Z (+0x78 clear)
    arr = e.alloc(0x10)
    bad, mark, found, kinds = 0, e.brk, 0, {}
    found6 = [0, 0]
    town_room, dead_room = near_room[2], near_room[3]
    dumped = [0, 0]
    for case in range(cases):
        e.brk = mark
        rooms.clear(); town.clear(); blocked.clear(); align.clear(); size.clear(); area.clear()
        for k in range(0x80): e.w32(game + hash1 + 4 * k, 0)
        order = near_room[:]; rng.shuffle(order)
        for k, r in enumerate(order): e.w32(arr + 4 * k, r)
        for r in near_room: e.w32(r, arr); e.w32(r + 0x24, 4); e.w32(r + 0x74, 0); e.w32(r + 0x78, int(r != dead_room)); area[r] = rng.choice((1, 1, 2))
        town.add(town_room)
        room_list = {r: [] for r in near_room}
        difficulty, aidist = rng.randrange(3), rng.choice((0, 0, 20, 35, 50))
        e.mu.mem_write(game + 0x6d, bytes([difficulty]))
        e.mu.mem_write(row + 0x52 + difficulty, bytes([aidist]))
        mx, my, act = 1000, 1000, rng.randrange(5)
        mon = unit(1, mx, my, act)
        e.w32(mon + 0x14, data); e.w32(data, row); e.w32(data + 0x28, control)
        rooms[mon] = near_room[0]; room_list[near_room[0]].append(mon)
        my_align = rng.choice((0, 0, 0, 1, 1))
        align[mon] = my_align
        seed = (rng.getrandbits(32), rng.getrandbits(32))
        e.w32(mon + 0x20, seed[0]); e.w32(mon + 0x24, seed[1])
        kind = rng.choice((0, 0, 2, 3, 3))
        outdoor[0] = rng.random() < 0.3
        force, sighted = rng.random() < 0.2, rng.random() < 0.3
        has_area, area_flag = rng.random() < 0.7, rng.random() < 0.4
        e.w32(data + 0x50, spawn if has_area else 0); e.w32(spawn + 0x24, int(area_flag))
        e.w32(control + 8, (0x40 if force else 0) | (8 if sighted else 0))
        info = {mon: {"self": True, "monster": True, "dead": False, "threat": 0, "flag8": sighted, "mode": 1}}
        foes, units = [], []
        spread = rng.choice((10, 40, 80))

        def place(u, pet_or_monster):
            r = rng.choice((None,) + tuple(near_room) * 4 + (near_room[0],) * 4) if rng.random() > 0.05 else None
            if r is not None:
                rooms[u] = r; room_list[r].append(u)
            return r

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
                r = place(u, pet)
                if rng.random() < 0.4: blocked.add(u)
                dead = mode in (0, 0x11 if not pet else 0xc) or e.read(u + 0xc6, 1)[0] & 1 == 1
                k = rng.randrange(len(threats))
                if pet:
                    d2, c2 = e.alloc(0x60), e.alloc(0x40)
                    e.w32(u + 0x14, d2); e.w32(d2 + 0x28, c2); e.w32(u + 4, k); size[u] = rng.randint(1, 3)
                info[u] = {"self": False, "monster": pet, "dead": dead, "threat": threats[k] if pet else 14, "flag8": False, "mode": mode}
                foes.append({"distance": ai_distance(x - mx, y - my), "pet": pet, "away": not pet and (far_act or r is None or r == town_room),
                             "dead": dead and not pet, "blocked": u in blocked, "list": 0})
                units.append(u)
                node = e.alloc(0x10); e.w32(node, u)
                if nodes: e.w32(nodes[-1] + 8, node)
                nodes.append(node)
            e.w32(game + 0x10f8 + 4 * slot, nodes[0])
        # Monsters: good ones in list 8, neutral in 9 (head first), evil in neither.
        lists = {8: [], 9: []}
        ids = []
        for n in range(rng.choice((0, 1, 2, 3, 4, 6))):
            x, y = mx + rng.randint(-spread // 2, spread // 2), my + rng.randint(-spread // 2, spread // 2)
            far_act = rng.random() < 0.1
            mode = rng.choice((1, 1, 1, 1, 0, 0xc))
            u = unit(1, x, y, (act + 1) % 5 if far_act else act, mode)
            if rng.random() < 0.05: e.mu.mem_write(u + 0xc6, b"\x01")
            d2, c2 = e.alloc(0x60), e.alloc(0x40)
            k = rng.randrange(len(threats))
            flag8 = rng.random() < 0.5
            e.w32(u + 0x14, d2); e.w32(d2 + 0x28, c2); e.w32(c2 + 8, 8 if flag8 else 0); e.w32(u + 4, k)
            uid = 100 + n
            e.w32(u + 0xc, uid); e.w32(u + 0xe4, e.r32(game + hash1 + 4 * (uid & 0x7f))); e.w32(game + hash1 + 4 * (uid & 0x7f), u)
            ids.append((uid, u))
            size[u] = rng.randint(1, 3)
            a = rng.choice((0, 1, 1, 2))
            align[u] = a
            place(u, True)
            if rng.random() < 0.4: blocked.add(u)
            dead = mode in (0, 0xc) or e.read(u + 0xc6, 1)[0] & 1 == 1
            info[u] = {"self": False, "monster": True, "dead": dead, "threat": threats[k], "flag8": flag8, "mode": mode}
            if a: lists[10 - a].insert(0, (u, far_act))
        for li in (8, 9):
            nodes = []
            for u, far_act in lists[li]:
                x, y = e.r16(e.r32(u + 0x2c) + 2), e.r16(e.r32(u + 0x2c) + 6)
                foes.append({"distance": ai_distance(x - mx, y - my), "pet": False, "away": far_act, "dead": False, "blocked": u in blocked, "list": li})
                units.append(u)
                node = e.alloc(0x10); e.w32(node, u)
                if nodes: e.w32(nodes[-1] + 8, node)
                nodes.append(node)
            e.w32(game + 0x10f8 + 4 * li, nodes[0] if nodes else 0)
        set_id = 0
        if kind == 2: set_id = rng.choice(ids)[0] if ids and rng.random() < 0.9 else 999
        e.w32(data + 0x38, kind); e.w32(data + 0x34, set_id)
        for r, us in room_list.items():                 # each room's units, head first
            rng.shuffle(us)
            for a_, b_ in zip(us, us[1:]): e.w32(a_ + 0xe8, b_)
            if us: e.w32(us[-1] + 0xe8, 0); e.w32(r + 0x74, us[0])
        # The mode 5 candidates, in FUN_005dcf70's order.
        nears, near_units = [], []
        for r in order:
            for u in room_list[r]:
                i = info[u]
                x, y = e.r16(e.r32(u + 0x2c) + 2), e.r16(e.r32(u + 0x2c) + 6)
                nears.append({"distance": near_distance(x - mx, y - my, size.get(u, 2)), "align": align.get(u, 2), "threat": i["threat"],
                              "self": i["self"], "monster": i["monster"], "dead": i["dead"], "skip": r in (town_room, dead_room),
                              "blocked": u in blocked, "waking": i["monster"] and i["mode"] not in (0, 0xc) and i["flag8"] and area[r] == area[near_room[0]]})
                near_units.append(u)
        got = e.call(0x5dd7f0, control, out_dist, out_melee, ecx=game, edx=mon)
        # ai.cpp search_target: sight, the skill-set target, the search, the flags.
        need_sight = force or (not outdoor[0] and not sighted and not (has_area and area_flag))
        want, best, nearest, setk = 0, 0, 0x7fffffff, kind
        if kind == 2:
            t = dict(ids).get(set_id)
            if t and not info[t]["dead"]:
                x, y = e.r16(e.r32(t + 0x2c) + 2), e.r16(e.r32(t + 0x2c) + 6)
                want, best = t, near_distance(x - mx, y - my, size[t])
        elif kind == 3:
            low = seed[0] * 0x6AC690C5 + seed[1]
            p = search_near(nears, confuse_align(my_align, low & 1), need_sight)
            if p[0] >= 0: want, best = near_units[p[0]], p[1]
        if not want: setk = 0
        if not want and my_align == 0:
            target, best, nearest = search_pick(foes, aidist or 0x23, need_sight)
            want = units[target] if target >= 0 else 0
        elif not want:
            p = search_near(nears, my_align, need_sight)
            i, best = (p[0], p[1]) if p[0] >= 0 else (p[2], p[3])
            want = near_units[i] if i >= 0 else 0
        want_flags = 8 if (sighted or want) else 0
        want_area = int(not (not sighted and area_flag)) if want and has_area and not force and not outdoor[0] else int(area_flag)
        found += bool(want); kinds[(kind, my_align)] = kinds.get((kind, my_align), 0) + bool(want)
        have = (got, e.s32(out_dist), e.r32(control + 8), e.r32(spawn + 0x24), e.r32(data + 0x38), align[mon])
        need = (want, best if want else nearest, want_flags, want_area, setk, my_align)
        if have != need:
            bad += 1
            if bad < 20: print(f"case {case}: game {have}, port {need}; kind {kind} align {my_align}")
        # Mode 6 (FUN_005ddc30, a merc's search) on the same units, the searcher good.
        align[mon] = 2; e.w32(data + 0x38, 0)
        has_path = rng.random() < 0.5                           # FUN_005dd510's type 2 path (FUN_00648780: the count)
        e.w32(e.r32(mon + 0x2c) + 0x28, int(has_path))
        got6 = e.call(0x5ddc30, out_dist, out_melee, ecx=game, edx=mon)
        sights, sight_units = [], []
        for r in order:
            for u in room_list[r]:
                i = info[u]
                x, y = e.r16(e.r32(u + 0x2c) + 2), e.r16(e.r32(u + 0x2c) + 6)
                sights.append({"distance": near_distance(x - mx, y - my, size.get(u, 2)), "threat": i["threat"],
                               "enemy": not i["dead"] and r != town_room and not friends(2, align.get(u, 2)),
                               "blocked": u in blocked or mon in blocked})
                sight_units.append(u)
        pick6 = search_sight(sights)
        k6, d6 = sight_choice(sights, pick6, has_path)
        found6[1] += pick6[0] >= 0 and pick6[2] >= 0 and pick6[3] < 6 and not has_path
        have6, need6 = (got6, e.s32(out_dist) if got6 else 0x7fffffff), ((sight_units[k6], d6) if k6 >= 0 else (0, 0x7fffffff))
        if have6 != need6:
            bad += 1
            if bad < 20: print(f"case {case}: mode 6 game {have6}, port {need6}")
        found6[0] += bool(need6[0])
        align[mon] = my_align
        if dump and dumped[0] < 5 and my_align == 0 and kind == 0 and len(foes) <= 8 and any(f["list"] for f in foes):
            dumped[0] += 1
            fs = ", ".join(f"{{{f['distance']}, {int(f['pet'])}, {int(f['away'])}, {int(f['dead'])}, {int(f['blocked'])}, {f['list']}}}" for f in foes)
            target = units.index(want) if want else -1
            print(f"{{ {aidist or 0x23}, {int(need_sight)}, {target}, {need[1]}, {{ {fs} }} }},")
        if dump and dumped[1] < 5 and len(nears) <= 8 and (my_align == 1 or kind == 3):
            dumped[1] += 1
            a = confuse_align(my_align, (seed[0] * 0x6AC690C5 + seed[1]) & 1) if kind == 3 else my_align
            p = search_near(nears, a, need_sight)
            fs = ", ".join(f"{{{f['distance']}, {f['align']}, {f['threat']}, {int(f['self'])}, {int(f['monster'])}, {int(f['dead'])}, {int(f['skip'])}, {int(f['blocked'])}, {int(f['waking'])}}}" for f in nears)
            print(f"{{ {a}, {int(need_sight)}, {{ {p[0]}, {p[1]}, {p[2]}, {p[3]} }}, {{ {fs} }} }},")
    print(f"ok: {cases} cases, {found} found a target, mode 6 {found6[0]} ({found6[1]} through the path test); found by (kind, alignment): {dict(sorted(kinds.items()))}" if not bad else f"{bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
