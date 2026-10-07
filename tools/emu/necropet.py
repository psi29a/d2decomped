# SPDX-License-Identifier: GPL-3.0-or-later
"""The pets' think (MonAI 67 "NecroPet", FUN_005e4cf0) against a port.

    uv run python necropet.py [cases] [seed] [--dump]

NecroPet runs the golems, the Valkyrie, the skeletons and skeletal mages.
game.exe's think runs natively on random setups: the pet and its owner at
random subtiles, the owner's mode, path (+0x10 and its end +0x18),
footstep ring and last arrival spot (player data +0x148), how many pets it
has and how many crowd this one, the levels under them, town, a foe or none
(FUN_005dd7f0: its distance and whether it's in melee; FUN_005dc640: a
clear line), and which moves find a path. Effects are hooked as merc.py
does; `pet_think` below is components/rules/merc.hpp's necropet_think line
for line. The effects and the pet's seed after must agree.
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_EDX

import emu
from merc import OWNER_SPOT, Seed, direction64, gap, spot_gap


def pet_think(s, seed, move):
    """components/rules/merc.hpp necropet_think. Effects as tuples."""
    out = []
    ctx = {"type": 0, "pct": 0, "steps": 0}                             # FUN_005a6260
    def pace(typ, pct, steps):
        if typ: ctx["type"] = typ
        if pct: ctx["pct"] = pct
        if steps: ctx["steps"] = min(steps, 0x4d)
    def go(x, y, mode=2, unit=0):                                         # FUN_005a7c20
        ok = move()
        out.append(("move", x & 0xffff, y & 0xffff, mode, unit, ctx["type"], ctx["pct"] & 0xffffffff, ctx["steps"], int(ok)))
        ctx.update(type=0, pct=0, steps=0)
        return ok
    def offset(cx, cy, n):                                                # FUN_005df400 / df530
        if seed.step() & 1 == 0: ox, oy = seed.rand(n), n
        else: ox, oy = n, seed.rand(n)
        if seed.step() & 1: ox = -ox
        if seed.step() & 1: oy = -oy
        return cx + ox, cy + oy
    px, py = s["pet"]; ox, oy = s["owner"]; ex, ey = s["end"]
    level_at = s["level_at"]

    def follow(mode, run, pct, reach):                                   # FUN_005e3ea0
        walk = (lambda x, y: go(x, y, 0xf)) if run else go
        if mode == 3:
            out.append(("teleport",))
            return 0
        if mode == 0:
            d8 = s["dir8_of"][direction64(ox, oy, ex, ey)]
            lvl = level_at(ox, oy)
            for _ in range(8):
                vx, vy = OWNER_SPOT[d8]
                tx, ty = ex + vx * 8, ey + vy * 8
                if level_at(tx, ty) == lvl:
                    pace(0, pct, 0x28)
                    if walk(tx, ty): return 1
                    if walk(((px + tx) & 0xffffffff) >> 1, ((py + ty) & 0xffffffff) >> 1): return 1
                d8 = (d8 + 1) & 7
            if go(*offset(px, py, 4)): return 1
            if run and go((px + ox) // 2, (py + oy) // 2, 0xf): return 1
            if go((px + ox) // 2, (py + oy) // 2): return 1
            return int(go(ox, oy))
        if mode == 1:
            if s["owner_mode"] == 2:                                     # the owner walking: to its path's end
                pace(0, 0, 100)
                if go(ex, ey): return 1
                if go((px + ex) // 2, (py + ey) // 2): return 1
            if not pct: pct = seed.rand(0x28) + 0x28
            cursor, tried, looked = s["cursor"], False, 0
            while True:
                cursor = 0x13 if cursor == 0 else cursor - 1
                fx, fy = s["ring"][cursor]
                if fx and fy and spot_gap(px, py, fx, fy) > 5:
                    pace(0, pct, 100)
                    if walk(fx, fy): return 1
                    pace(0xf, pct, 100)
                    if walk(fx, fy): return 1
                    if not tried:
                        tried = True
                        pace(1, pct, 100)
                        if walk(fx, fy): return 1
                looked += 1
                if looked > 0x13:
                    pace(0, 0xf, 0)
                    return int(go(*offset(px, py, max(gap(s["pet"], s["size"], ox, oy) >> 2, 4))))
        if mode == 2:
            if seed.rand(100) < 10:
                if go(*offset(px, py, seed.rand(3) + 3)): return 1
                pace(0, 0, 0x28)
                if go(ex, ey): return 1
        if mode == 4 and s["crowd"] > 0:
            if away(reach): return 1
            if go(*offset(px, py, reach)): return 1
        if mode == 5:
            pace(7, 0, 0)
            if go(*offset(ox, oy, reach)): return 1
            if away(reach): return 1
            pace(0, 0, 0x28)
            return int(go(ex, ey))
        out.append(("stand", 15))
        return 1

    def away(reach):                                                     # FUN_005defe0: reach off on each axis, away from the owner
        sx = -1 if px < ox else 1 if ox < px else 0
        sy = -1 if py < oy else 1 if oy < py else 0
        if reach > 5: pace(0, 0, reach)
        return go(px + reach * sx, py + reach * sy)

    def decide(t, melee, roll15, reach):                                 # FUN_005e45d0
        d = gap(s["pet"], s["size"], ox, oy)
        reach = min(reach + (s["pets"] >> 1), 0x24)
        if d <= 1 and s["owner_mode"] == 1 and not melee:
            return follow(5, 0, 0, reach)
        if t and not s["town"]:
            return 0 if d <= 0x50 else follow(3, 0, 0, reach)
        mode = 2
        if s["owner_mode"] in (2, 6, 3): mode = 0
        if s["cur"][0] != ex and s["cur"][1] != ey: mode = 0
        if level_at(ox, oy) != level_at(px, py): mode = 1
        if d > reach: mode = 1
        if d > 0x32: mode = 3
        if spot_gap(px, py, *s["arrive"]) < 0x1c:
            if mode == 1 and d < 0x1e: mode = 4
            elif mode == 2: mode = 4
        if roll15 and mode == 2: return 0
        return follow(mode, 0, 0, reach)

    if s["owner_none"]:
        out.append(("stand", 10)); return out, ctx
    d = gap(s["pet"], s["size"], ox, oy)
    if d > 0x32:
        follow(3, 0, 0, 0); return out, ctx
    vel, runv = s["velocity"], s["run"]
    pct = 100 if vel < 1 or runv * 100 // vel - 100 > 99 else runv * 100 // vel - 100
    if d >= 0x1d:
        follow(0, 0, pct, 0); return out, ctx
    found = s["target"]
    first = found if found and not found["bit30"] else None
    t = found if found and found["dist"] <= 0x18 else None
    melee = found["melee"] if found else False
    if t is None or found["dist"] > 6:
        t = None
        if first and gap(s["pet"], s["size"], *first["pos"]) < 0x24: t = first
    if not s["clear"]: t = None
    roll = seed.step() % 100
    if decide(t, melee, roll <= 14, 7 if roll > 14 else 8): return out, ctx
    if t and not s["town"]:
        if melee:
            if seed.rand(100) > 0x4f: out.append(("stand", 10))
            else: go(0, 0, 4, 1)
            return out, ctx
        pace(0, 0, 0xc)
        pace(0xd, 0, 0)
        if not go(0, 0, 2, 1):
            out.append(("unreachable",))
            if seed.step() % 100 < 0x46: go(*offset(px, py, 4))
            else: out.append(("stand", 10))
        return out, ctx
    go(*offset(px, py, 4))
    return out, ctx


def dump_case(s, want):
    """A C++ test row (tests/test_monsters.cpp) for a case, keyed by its shape; None to skip."""
    out, (lo, hi) = want
    if ("teleport",) in out[:-1]: return None                          # d2d's teleport always lands
    px, py = s["pet"]
    tried = [m for m in out if m[0] == "move" and m[3] != 4]
    last = out[-1] if out else None
    if last and last[0] == "teleport": kind, frames = "teleport", 0
    elif last and last[0] == "stand": kind, frames = "stand", last[1]
    elif any(m[0] == "move" and m[3] == 4 for m in out): kind, frames = "swing", 0
    elif tried and tried[-1][8]: kind, frames = ("chase" if tried[-1][4] else "moved"), 0
    else: kind, frames = "failed", 0
    unreachable = ("unreachable",) in out
    t = s["target"]
    rel = lambda x, y: f"{x - px}, {y - py}"
    mv = ", ".join(f"{{ {0 if m[4] else m[1] - px}, {0 if m[4] else m[2] - py}, {m[3]}, {m[4]}, {m[5]}, {m[6] if m[6] < 0x80000000 else m[6] - (1 << 32)}, {m[7]}, {m[8]} }}" for m in tried)
    rg = ", ".join(f"{{ {rel(a, b)} }}" for a, b in s["ring"])
    sp = s["split"] - px if s["split"] is not None else 9999
    foe = f"1, {int(t['melee'])}, {int(t['bit30'])}, {rel(*t['pos'])}, {t['dist']}" if t else "0, 0, 0, 0, 0, 0"
    key = (kind, unreachable, tuple(m[3:8] for m in tried)[:3], s["owner_mode"])
    return key, (f"{{ {s['size']}, {rel(*s['owner'])}, {s['owner_mode']}, {rel(*s['cur'])}, {rel(*s['end'])}, {s['cursor']}, {rel(*s['arrive'])}, "
                 f"{s['pets']}, {s['crowd']}, {int(s['town'])}, {s['velocity']}, {s['run']}, {foe}, {int(s['clear'])}, {sp}, "
                 f"{s['seed'][0]}u, {s['seed'][1]}u, Kind::{kind}, {frames}, {int(unreachable)}, {lo}u, {hi}u, {{ {mv} }}, {{ {rg} }} }},")


def setup(rng):
    px, py = 5000 + rng.randint(-40, 40), 5000 + rng.randint(-40, 40)
    far = rng.choice((1, 3, 10, 20, 30, 45, 60, 130))
    ox, oy = px + rng.randint(-far, far), py + rng.randint(-far, far)
    ex, ey = (ox, oy) if rng.random() < 0.3 else (ox + rng.randint(-12, 12), oy + rng.randint(-12, 12))
    cx, cy = (ex, ey) if rng.random() < 0.5 else (ox + rng.randint(-3, 3), oy + rng.randint(-3, 3))
    ring = [(ox + rng.randint(-30, 30), oy + rng.randint(-30, 30)) for _ in range(20)]
    split = rng.choice((None, None, px + rng.randint(-20, 20)))
    tfar = rng.choice((2, 5, 10, 20, 40))
    return {
        "pet": (px, py), "owner": (ox, oy), "end": (ex, ey), "cur": (cx, cy), "size": rng.choice((1, 2, 3)),
        "owner_mode": rng.choice((1, 1, 2, 3, 6, 4)), "owner_none": False,
        "ring": ring, "cursor": rng.randint(0, 19),
        "arrive": (ox + rng.randint(-40, 40), oy + rng.randint(-40, 40)),
        "pets": rng.choice((1, 2, 5, 10, 20, 40)), "crowd": rng.choice((0, 0, 1, 3)),
        "town": rng.random() < 0.2,
        "velocity": rng.choice((0, 5, 6, 8, 9, 11, 13)), "run": rng.choice((0, 6, 8, 9, 10, 15, 30)),
        "target": None if rng.random() < 0.3 else {
            "pos": (px + rng.randint(-tfar, tfar), py + rng.randint(-tfar, tfar)),
            "dist": rng.randint(0, 40), "melee": rng.random() < 0.4, "bit30": rng.random() < 0.1},
        "clear": rng.random() < 0.8,
        "split": split, "level_at": (lambda x, y, split=split: 2 if split is not None and x >= split else 1),
        "fails": rng.random(), "seed": (rng.getrandbits(32), rng.getrandbits(32)),
    }


CUR = {}


def install(e):
    cur = CUR
    def out(item): cur["out"].append(item); return 0
    e.hook(0x58f0d0, lambda e: 0 if cur["s"]["owner_none"] else cur["owner"], 0)
    e.hook(0x552f60, lambda e: 0, 0)
    e.hook(0x639df0, lambda e: 0, 2)
    e.hook(0x620510, lambda e: cur["s"]["size"] if e.arg(0) == cur["pet"] else 2, 1)
    e.hook(0x620bb0, lambda e: cur["room"], 1)
    e.hook(0x61ad30, lambda e: cur["room"], 3)
    e.hook(0x61b130, lambda e: cur["s"]["level_at"](e.arg(1) & 0xffff, e.arg(2) & 0xffff), 3)
    e.hook(0x61ab00, lambda e: int(cur["s"]["town"]), 1)
    def search(e):
        t = cur["s"]["target"]
        if t is None: e.w32(e.arg(2), 0); return 0              # FUN_005dd7f0 clears the melee flag
        e.w32(e.arg(1), t["dist"]); e.w32(e.arg(2), int(t["melee"]))
        return cur["target"]
    e.hook(0x5dd7f0, search, 3)
    e.hook(0x5dc640, lambda e: int(cur["s"]["clear"]), 0)
    def crowd(e):
        e.w32(e.arg(1) + 8, cur["s"]["crowd"])
        return 0
    e.hook(0x574de0, crowd, 2)
    e.hook(0x574f40, lambda e: cur["s"]["pets"], 0)
    e.hook(0x5de080, lambda e: out(("stand", e.arg(0))), 1)
    e.hook(0x540e60, lambda e: 0, 2)
    e.hook(0x5dd230, lambda e: out(("unreachable",)), 1)
    e.hook(0x64c040, lambda e: 0, 1)
    def pace(e):
        ctx = cur["ctx"]
        typ, pct, steps = e.mu.reg_read(UC_X86_REG_EDX), e.arg(0), e.arg(1) & 0xff
        if typ: ctx["type"] = typ
        if pct: ctx["pct"] = pct
        if steps: ctx["steps"] = min(steps, 0x4d)
        return 0
    e.hook(0x5a6260, pace, 2)
    def setoff(e):
        m, ctx = e.arg(1), cur["ctx"]
        mode, unit, x, y = e.r32(m), e.r32(m + 8), e.r32(m + 0xc) & 0xffff, e.r32(m + 0x10) & 0xffff
        ok = cur["moves"].random() >= cur["s"]["fails"]
        out(("move", x, y, mode, int(unit != 0), ctx["type"], ctx["pct"], ctx["steps"], int(ok)))
        ctx.update(type=0, pct=0, steps=0)
        return int(ok)
    e.hook(0x5a7c20, setoff, 3)
    e.hook(0x54dc40, lambda e: out(("teleport",)), 5)


def run_game(e, s, rng_moves):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    game, pet, owner, target, pdata, ppath, opath, tpath, pcd, params, ai, room, row = (
        alloc(n) for n in (0x2000, 0x200, 0x200, 0x200, 0x200, 0x200, 0x200, 0x200, 0x400, 0x40, 0x100, 0x100, 0x200))
    px, py = s["pet"]; ox, oy = s["owner"]; ex, ey = s["end"]; cx, cy = s["cur"]
    e.w32(pet, 1); e.w32(pet + 4, 0x16b); e.w32(pet + 0x10, 1); e.w32(pet + 0x14, pdata)
    e.w32(pdata + 0x28, ai); e.w32(pet + 0x20, s["seed"][0]); e.w32(pet + 0x24, s["seed"][1]); e.w32(pet + 0x2c, ppath)
    e.w32(ppath, px << 16 | 0x8000); e.w32(ppath + 4, py << 16 | 0x8000); e.w32(ppath + 0x30, pet); e.w32(ppath + 0x1c, room)
    e.w32(owner, 0); e.w32(owner + 0x10, s["owner_mode"]); e.w32(owner + 0x14, pcd); e.w32(owner + 0x2c, opath)
    e.w32(opath, ox << 16 | 0x8000); e.w32(opath + 4, oy << 16 | 0x8000); e.w32(opath + 0x30, owner); e.w32(opath + 0x1c, room)
    e.mu.mem_write(opath + 0x10, struct.pack("<HH", cx, cy))
    e.mu.mem_write(opath + 0x18, struct.pack("<HH", ex, ey))
    e.mu.mem_write(pcd + 0xa0, bytes([s["cursor"]]))
    for k, (fx, fy) in enumerate(s["ring"]): e.w32(pcd + 0xa8 + 8 * k, fx); e.w32(pcd + 0xac + 8 * k, fy)
    e.w32(pcd + 0x148, s["arrive"][0]); e.w32(pcd + 0x14c, s["arrive"][1])
    if s["target"]:
        tx, ty = s["target"]["pos"]
        e.w32(target, 1); e.w32(target + 0x10, 1); e.w32(target + 0x2c, tpath)
        e.w32(target + 0xc4, 0x40000000 if s["target"]["bit30"] else 0)
        e.w32(tpath, tx << 16 | 0x8000); e.w32(tpath + 4, ty << 16 | 0x8000); e.w32(tpath + 0x30, target); e.w32(tpath + 0x1c, room)
    e.mu.mem_write(row + 0x32, struct.pack("<hh", s["velocity"], s["run"]))
    e.w32(params, ai); e.w32(params + 7 * 4, row)
    CUR.update(s=s, out=[], ctx={"type": 0, "pct": 0, "steps": 0}, owner=owner, pet=pet, target=target, room=room, moves=rng_moves)
    e.call(0x5e4cf0, params, ecx=game, edx=pet)
    return CUR["out"], (e.r32(pet + 0x20), e.r32(pet + 0x24))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = int(args[0]) if args else 3000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    e = emu.Emu()
    install(e)
    dir8_of = list(e.read(0x745600, 64))
    mark, bad, kinds, dumped = e.brk, 0, {}, {}
    dump = "--dump" in sys.argv
    for case in range(cases):
        e.brk = mark
        s = setup(rng)
        s["dir8_of"] = dir8_of
        move_seed = rng.getrandbits(32)
        got = run_game(e, s, random.Random(move_seed))
        moves = random.Random(move_seed)
        seed = Seed(*s["seed"])
        want_out, _ = pet_think(s, seed, lambda: moves.random() >= s["fails"])
        want = (want_out, (seed.lo, seed.hi))
        kind = got[0][0][0] if got[0] else "-"
        kinds[kind] = kinds.get(kind, 0) + 1
        if dump and got == want and len(dumped) < 40:
            line = dump_case(s, want)
            if line and line[0] not in dumped: dumped[line[0]] = line[1]
        if got != want:
            bad += 1
            if bad <= 6:
                print(f"case {case}: game {got}\n          port {want}\n          {dict((k, v) for k, v in s.items() if k not in ('ring', 'level_at', 'dir8_of'))}")
    for line in dumped.values(): print(line)
    print(f"necropet ok: {cases} cases, {kinds}" if not bad else f"necropet: {bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
