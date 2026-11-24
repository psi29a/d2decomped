# SPDX-License-Identifier: GPL-3.0-or-later
"""Who a monster goes for when the player, the merc and pets are all near
(FUN_005dd7f0), and whom the merc goes for (FUN_005ddc30 mode 6): game.exe's
own code against d2d's real rules code (pets_merc_harness.cpp).

Runs in upstream d2decomped's tools/emu (needs its `emu` module and the
read-only game.exe as `$D2_MPQ_DIR/bin/game.exe`).

    python pets_merc.py N SEED cases.txt > g.txt
    g++ -O2 -std=c++23 -I../../components $(find ../../components -name '*.hpp' -printf '-I%h\n' | sort -u) pets_merc_harness.cpp ../../build/components/rules/librules.a -o h
    ./h before < cases.txt | diff g.txt - | grep -c '^>'
    ./h after < cases.txt | diff g.txt -

Kind S (the monster's search). An evil searcher with no skill-set target,
one player in list 0: game.exe builds the list with its own FUN_005b1880
(the player) and FUN_005b1900 (each pet, inserted right after the head:
0x5b1966..0x5b1979), the merc first (hired before any summon), then 0..3
summons oldest first; in a share of the cases (REVIVE_SHARE, default 0.3)
the merc's call comes last, as FUN_00579aa0's re-insert on a revive or a
hire after the summons does (its removal, FUN_005b1a90, is not run);
FUN_005dd7f0 runs natively. Dead pets are not
generated: game.exe takes a dying monster out of its list (FUN_005a6520 ->
FUN_005b1a90). The case line gives d2d's foes in fight/tick.cpp's order
(player, merc, `actor->pets` oldest first; no merc = a dead pet entry, as
merc_foe builds it) with ai.cpp search_target's inputs: ai_distance,
`away` = town, `dead` the player's mode 0 / 0x11.

Kind M (the merc's target). A good searcher, monsters in its room
(FUN_005dce60 order: the room's units from +0x74 along +0xe8), the path
count to the primary random; FUN_005ddc30 runs natively. The case line
gives merc.cpp's SightFoe inputs in that order (near_distance with the
monster's size, MonStats threat, enemy = alive, out of town, not good).

Hooked (not the claim; as upstream search.py): the room (FUN_00620bb0),
outdoors (FUN_0061aa40), town (FUN_0061ab00), sight (FUN_00622aa0),
in-melee (FUN_00622c40), alignment (FUN_006259b0 / FUN_005543b0), size
(FUN_00620510), the area (FUN_0061b130), FUN_005dd510's pathfinder
(FUN_00649970), the pool allocator FUN_0040b430. MonStats rows are a fake
table (threat at +0x4e).

Output: `S i id dist` / `M i id dist` (id 0: none; dist the out distance,
`nearest` when S finds none, 0x7fffffff when M finds none).
"""
import os
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

THREATS = (0, 1, 2, 10, 14, 0, 1, 11)
REVIVE_SHARE = float(os.environ.get("REVIVE_SHARE", "0.3"))   # share of the cases with a merc and a summon where the merc entered the list after them


def ai_distance(dx, dy):
    dx, dy = abs(dx), abs(dy)
    return (min(dx, dy) + 2 * max(dx, dy)) // 2


def near_distance(dx, dy, size):
    return ai_distance(max(abs(dx) - size, 0), max(abs(dy) - size, 0))


def offset(rng, aidist):
    """A spot round the searcher, often on an edge (0x37, the best, a tie)."""
    r = rng.random()
    if r < 0.35:
        d = rng.choice((0, 1, 2, 5, 6, 0x13, 0x14, aidist - 1, aidist, aidist + 1, 0x36, 0x37, 0x38, 0x30, 0x31))
        d = max(d, 0)
        return rng.choice(((d, 0), (-d, 0), (0, d), (0, -d)))
    if r < 0.6:
        return rng.randint(-8, 8), rng.randint(-8, 8)
    return rng.randint(-60, 60), rng.randint(-60, 60)


def main():
    n_cases, seed, cases_path = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
    rng = random.Random(seed)
    e = emu.Emu()
    rooms, town, blocked, outdoor, align, size = {}, set(), set(), [False], {}, {}
    e.hook(0x620bb0, lambda e: rooms.get(e.arg(0), 0), 1)
    e.hook(0x61aa40, lambda e: int(outdoor[0]), 1)
    e.hook(0x61ab00, lambda e: int(e.arg(0) in town), 1)
    e.hook(0x622aa0, lambda e: int(e.arg(0) in blocked or e.arg(1) in blocked), 3)
    e.hook(0x622c40, lambda e: 0x55, 3)
    e.hook(0x6259b0, lambda e: align.get(e.arg(0), 2), 1)
    e.hook(0x5543b0, lambda e: align.__setitem__(e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EDX) & 0xff), 1)
    e.hook(0x620510, lambda e: size.get(e.arg(0), 2), 1)
    e.hook(0x61b130, lambda e: 1, 3)
    e.hook(0x649970, lambda e: 0, 3)
    e.hook(0x40b430, lambda e: e.alloc(e.mu.reg_read(UC_X86_REG_EDX)), 3)

    tables, rows = e.alloc(0x1000), e.alloc(0x1a8 * 8)
    e.w32(0x744304, tables); e.w32(tables + 0xa78, rows); e.w32(tables + 0xa80, 8)
    for k, t in enumerate(THREATS): e.mu.mem_write(rows + 0x1a8 * k + 0x4e, bytes([t]))
    game = e.alloc(0x2000)
    row, data, control = e.alloc(0x100), e.alloc(0x60), e.alloc(0x40)
    out_dist, out_melee = e.alloc(4), e.alloc(4)
    room_a, room_t = e.alloc(0x100), e.alloc(0x100)
    arr = e.alloc(0x10)
    mark = e.brk
    out = open(cases_path, "w")
    mx, my = 1000, 1000

    def unit(kind, x, y, mode=1, uid=0):
        u, path = e.alloc(0x100), e.alloc(0x100)
        e.w32(u, kind); e.w32(u + 0x10, mode); e.w32(u + 0x2c, path); e.w32(u + 0xc4, 4); e.w32(u + 0xc, uid)
        e.w32(u + 0xd0, 0xb)
        e.mu.mem_write(path + 2, x.to_bytes(2, "little")); e.mu.mem_write(path + 6, y.to_bytes(2, "little"))
        e.w32(path + 0x28, 1)
        return u

    for case in range(n_cases):
        e.brk = mark
        rooms.clear(); town.clear(); blocked.clear(); align.clear(); size.clear()
        e.w32(game + 0x1c, 0); e.w32(game + 0x8c, 0)
        for k in range(10): e.w32(game + 0x10f8 + 4 * k, 0)
        difficulty, aidist = rng.randrange(3), rng.choice((0, 0, 20, 35, 50))
        best = aidist or 0x23
        e.mu.mem_write(game + 0x6d, bytes([difficulty]))
        e.mu.mem_write(row + 0x52, bytes(3)); e.mu.mem_write(row + 0x52 + difficulty, bytes([aidist]))
        # ---- kind S: the monster's search over the player's list
        mon = unit(1, mx, my, uid=1)
        e.w32(mon + 0x14, data); e.w32(data, row); e.w32(data + 0x28, control); e.w32(data + 0x38, 0); e.w32(data + 0x50, 0)
        rooms[mon] = room_a; align[mon] = 0
        outdoor[0] = rng.random() < 0.3
        force, sighted = rng.random() < 0.2, rng.random() < 0.3
        e.w32(control + 8, (0x40 if force else 0) | (8 if sighted else 0))
        need_sight = force or (not outdoor[0] and not sighted)
        in_town = rng.random() < 0.1
        pdx, pdy = offset(rng, best)
        pmode = rng.choice((1, 1, 1, 1, 2, 3, 0, 0x11))
        player = unit(0, mx + pdx, my + pdy, pmode, uid=10)
        rooms[player] = room_t if in_town else room_a
        if in_town: town.add(room_t)
        if rng.random() < 0.3: blocked.add(player)
        e.call(0x5b1880, 0, ecx=game, edx=player)
        has_merc = rng.random() < 0.85
        n_summons = rng.choice((0, 0, 1, 2, 3))
        revived = has_merc and n_summons > 0 and rng.random() < REVIVE_SHARE   # hired / revived after the summons: FUN_00579aa0 inserts it again
        pets = []                                                    # d2d's order: the merc first, then the summons oldest first
        for k in range((1 if has_merc else 0) + n_summons):
            dx, dy = offset(rng, best) if rng.random() < 0.5 else (pdx + rng.randint(-3, 3), pdy + rng.randint(-3, 3))
            p = unit(1, mx + dx, my + dy, rng.choice((1, 1, 2, 3)), uid=20 + k)
            rooms[p] = rng.choice((room_a, room_a, room_t)); size[p] = rng.randint(1, 3)
            d2, c2 = e.alloc(0x60), e.alloc(0x40)
            e.w32(p + 0x14, d2); e.w32(d2 + 0x28, c2); e.w32(p + 4, rng.randrange(8))
            if rng.random() < 0.3: blocked.add(p)
            pets.append((p, dx, dy, 20 + k))
        # game.exe's list: each FUN_005b1900 call inserts right after the head. d2d's `serial` is the call's rank.
        calls = pets[1:] + pets[:1] if revived else pets
        serial = {}
        for rank, (p, _, _, uid) in enumerate(calls, 1):
            e.call(0x5b1900, 0, 0, ecx=game, edx=p)
            serial[uid] = rank
        got = e.call(0x5dd7f0, control, out_dist, out_melee, ecx=game, edx=mon)
        gid = e.r32(got + 0xc) if got else 0
        print(f"S {case} {gid} {e.s32(out_dist)}")
        foes = [(10, ai_distance(pdx, pdy), 0, int(in_town), int(pmode in (0, 0x11)), int(player in blocked), 0)]
        order = pets if has_merc else [(None, 0, 0, 29)] + pets       # d2d: player, merc (absent: dead), pets oldest first
        for p, dx, dy, uid in order:
            foes.append((uid, ai_distance(dx, dy), 1, int(in_town), int(p is None), int(p in blocked), serial.get(uid, 0)))
        out.write(f"S {case} {best} {int(need_sight)} {len(foes)} " + " ".join(" ".join(map(str, f)) for f in foes) + "\n")
        # ---- kind M: the merc's search over the monsters in its room
        e.brk = mark
        rooms.clear(); town.clear(); blocked.clear(); align.clear(); size.clear()
        merc = unit(1, mx, my, uid=2)
        md, mc = e.alloc(0x60), e.alloc(0x40)
        e.w32(merc + 0x14, md); e.w32(md, row); e.w32(md + 0x28, mc); e.w32(md + 0x38, 0)
        rooms[merc] = room_a; align[merc] = 2
        has_path = rng.random() < 0.5
        e.w32(e.r32(merc + 0x2c) + 0x28, int(has_path))
        for r in (room_a, room_t): e.w32(r, arr); e.w32(r + 0x24, 2); e.w32(r + 0x74, 0); e.w32(r + 0x78, 1)
        e.w32(arr, room_a); e.w32(arr + 4, room_t)
        town.add(room_t)
        listed = {room_a: [], room_t: []}
        mons = []
        for k in range(rng.choice((0, 1, 2, 3, 4, 6, 8))):
            dx, dy = offset(rng, 0x31)
            mode = rng.choice((1, 1, 1, 1, 2, 0, 0xc))
            u = unit(1, mx + dx, my + dy, mode, uid=100 + k)
            d2, c2 = e.alloc(0x60), e.alloc(0x40)
            cls = rng.randrange(8)
            e.w32(u + 0x14, d2); e.w32(d2 + 0x28, c2); e.w32(u + 4, cls)
            size[u] = rng.randint(1, 3); align[u] = rng.choice((0, 0, 0, 0, 1, 2))
            r = room_t if rng.random() < 0.1 else room_a
            rooms[u] = r; listed[r].append(u)
            if rng.random() < 0.25: blocked.add(u)
            mons.append((u, dx, dy, mode, cls, r))
        listed[room_a].insert(rng.randrange(len(listed[room_a]) + 1), merc)
        for r, us in listed.items():
            for a_, b_ in zip(us, us[1:]): e.w32(a_ + 0xe8, b_)
            if us: e.w32(us[-1] + 0xe8, 0); e.w32(r + 0x74, us[0])
        got6 = e.call(0x5ddc30, out_dist, out_melee, ecx=game, edx=merc)
        print(f"M {case} {e.r32(got6 + 0xc) if got6 else 0} {e.s32(out_dist) if got6 else 0x7fffffff}")
        info = {m[0]: m for m in mons}
        sight = []
        for r in (room_a, room_t):
            for u in listed[r]:
                if u == merc: continue
                _, dx, dy, mode, cls, _ = info[u]
                enemy = mode not in (0, 0xc) and r != room_t and align[u] != 2
                sight.append((e.r32(u + 0xc), near_distance(dx, dy, size[u]), THREATS[cls], int(enemy), int(u in blocked or merc in blocked)))
        out.write(f"M {case} {int(has_path)} {len(sight)} " + " ".join(" ".join(map(str, f)) for f in sight) + "\n")
    out.close()


if __name__ == "__main__":
    main()
