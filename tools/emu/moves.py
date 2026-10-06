# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's move: its path, its chase check and when it thinks, against
components/rules/monsters.hpp toward_path / chase_check and ai.cpp.

    uv run python moves.py [cases] [seed] [--dump] [--break]

1. The toward pather (FUN_00679c80, path types 2 / 5 / 6 / 0xd: a monster's
   walk and run) natively on random walls, starts, ends, `near` and step
   counts. Hooks: the collision test (FUN_0064d910: the walls) and the aim
   (FUN_0064fe40: nothing; it moves no point here).
2. The chase check (FUN_006503f0 -> FUN_00650350) natively on random paths:
   type, point / count, budget, stop distance, ends, the target's kind,
   size and spot. FUN_00641530 (unit_distance), FUN_00679250 (the
   target's spot), FUN_00649120 / FUN_00649140 (the budget) are game.exe's.
   Hooks: the sizes (FUN_00620510), the path compute (FUN_00649970: records
   the type it's asked for, returns a random count) and the update mark
   (FUN_0064c040).
3. Which modes think at once when they end (FUN_005a8030: DAT_0073c6d0)
   against ai.cpp's WL and RN.
4. The search pather (FUN_0067b850, path type 1) and a player's path (type
   7, FUN_00679ed0: toward, then the search when close) against
   monsters.hpp search_path / player_path, natively on random walls, with
   and without a unit to walk to. Hooks: the collision test only.

Prints `ok` or each mismatch. `--dump` prints cases as C++ lines for
tests/test_monsters.cpp; `--break` breaks the port (a re-path at 4 off, not
5; no stop at a step back) to show the check bites.
"""
import random
import struct
import sys

import emu

DIRS = ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1))      # DAT_006f1798
TRY = ((5, 4, 6), (4, 5, 6), (4, 3, 5), (4, 3, 2), (3, 4, 2), (6, 5, 4), (5, 4, 6), (4, 3, 5), (3, 4, 2), (2, 3, 4),
       (6, 7, 5), (6, 7, 5), (6, 7, 5), (2, 1, 3), (2, 1, 3), (6, 7, 0), (7, 0, 6), (0, 1, 7), (1, 0, 2), (2, 1, 0),
       (7, 0, 6), (0, 7, 6), (0, 1, 7), (0, 1, 2), (1, 0, 2))                        # DAT_006f1518
NEAR = ((-1, -1, -1, 0, 2, 4, 6, 8), (-1, -1, 0, 1, 2, 4, 6, 8), (-1, 0, 0, 2, 3, 5, 7, 8), (0, 1, 2, 2, 4, 5, 7, 8),
        (2, 2, 3, 4, 5, 6, 7, 9), (4, 4, 5, 5, 6, 7, 8, 9), (6, 6, 7, 7, 7, 8, 10, 10), (8, 8, 8, 8, 9, 9, 10, 11))
BREAK = "--break" in sys.argv


def unit_distance(dx, dy, a, b):
    dx, dy = abs(dx), abs(dy)
    if dx < 8 and dy < 8 and a < 4 and b < 4:
        n = NEAR[dy][dx]
        if n < 0: return 0
        if a == 3 or b == 3: n = max(n - 1, 0)
        return n + 1 if a < 2 or b < 2 else n
    half = a // 2 + b // 2
    across, down = max(dx - half, 0), max(dy - half, 0)
    return down + across * 2 if down < across else across + down * 2


def sign(v): return 1 if v >= 0 else -1


def toward_path(x, y, tx, ty, steps, near, blocked):
    """monsters.hpp toward_path, line for line."""
    def line():
        dx, dy = tx - x, ty - y
        lx, ly = abs(dx) + 1, abs(dy) + 1
        at = [x, y]
        if lx == ly:
            while True:
                last = tuple(at)
                if at[0] == tx: return True, (tx, ty)
                at[1] += sign(dy); at[0] += sign(dx)
                if blocked(*at): return False, last
        major = 0 if ly < lx else 1
        minor = 1 - major
        mend, mstep, nstep = (tx, sign(dx), sign(dy)) if major == 0 else (ty, sign(dy), sign(dx))
        mlen, nlen = (lx, ly) if major == 0 else (ly, lx)
        if at[major] == mend: return True, (tx, ty)
        err = nlen
        while True:
            last = tuple(at)
            at[major] += mstep
            if blocked(*at): return False, last
            err += nlen
            if err >= mlen:
                at[minor] += nstep; err -= mlen
                if err > 0 and blocked(*at): return False, last
            if at[major] == mend: return True, (tx, ty)
    clear, end = line()
    if clear or unit_distance(end[0] - tx, end[1] - ty, 1, 1) <= near: return [end]
    points, at = [], (x, y)
    if end != at: points.append(end); at = end
    prev, taken, turned = 0xff, 0, False
    while taken < steps and at != (tx, ty):
        turned = False
        dx, dy = tx - at[0], ty - at[1]
        ux, uy = dx, dy
        if abs(dx) >= abs(dy) * 2: uy = -1 if dy < 0 else dy & 1
        elif abs(dx) * 2 <= abs(dy): ux = -1 if dx < 0 else dx & 1
        tries = TRY[max(-2, min(2, ux)) * 5 + 12 + max(-2, min(2, uy))]
        d = next((t for t in tries if t != 0xff and not blocked(at[0] + DIRS[t][0], at[1] + DIRS[t][1])), -1)
        if d < 0 or (((d - 4) & 7) == prev and not BREAK): turned = False; break
        frm = at
        at = (at[0] + DIRS[d][0], at[1] + DIRS[d][1])
        if d != prev:
            if frm != (x, y): points.append(frm)
            turned = True
        taken += 1
        prev = d
    if not turned and taken: points.append(at)
    return points


def search_path(x, y, tx, ty, to_unit, blocked):
    """monsters.hpp search_path, line for line."""
    if to_unit and all(blocked(tx + a, ty + b) for a, b in ((-2, -2), (-2, 2), (2, -2), (2, 2), (-2, 0), (0, -2), (2, 0), (0, 2))):
        return []
    def estimate(p):
        across, down = abs(p[0] - tx), abs(p[1] - ty)
        return down + across * 2 if down <= across else across + down * 2
    pool = [dict(at=(x, y), est=estimate((x, y)), walked=0, parent=-1, kids=[], closed=False)]
    pool[0]["cost"] = pool[0]["est"]
    opn = []
    def push(i):
        k = next((n for n, o in enumerate(opn) if pool[i]["cost"] <= pool[o]["cost"]), len(opn))
        opn.insert(k, i)
    def link(node, kid):
        if len(node["kids"]) < 8: node["kids"].append(kid)
    def cost(a, b): return 2 if a[0] == b[0] or a[1] == b[1] else 3
    push(0)
    def relax(frm, spot):
        walked = pool[frm]["walked"] + cost(pool[frm]["at"], spot)
        idx = next((i for i, n in enumerate(pool) if n["at"] == spot), -1)
        if idx < 0:
            if len(pool) == 200: return False
            pool.append(dict(at=spot, est=estimate(spot), walked=walked, parent=frm, kids=[], closed=False))
            pool[-1]["cost"] = pool[-1]["est"] + walked
            push(len(pool) - 1); link(pool[frm], len(pool) - 1)
            return True
        link(pool[frm], idx)
        node = pool[idx]
        if walked >= node["walked"]: return True
        node.update(parent=frm, walked=walked, cost=node["est"] + walked)
        if not node["closed"]: return True
        stack = [idx]
        while stack:
            top = stack.pop()
            for kid in pool[top]["kids"]:
                nxt = pool[kid]
                through = pool[top]["walked"] + cost(pool[top]["at"], nxt["at"])
                if through < nxt["walked"]:
                    nxt.update(parent=top, walked=through, cost=nxt["est"] + through); stack.append(kid)
        return True
    best = -1
    while opn:
        i = opn.pop(0); pool[i]["closed"] = True; node = pool[i]
        if best < 0 or node["est"] < pool[best]["est"] or (node["est"] == pool[best]["est"] and pool[best]["walked"] + 5 < node["walked"]):
            best = i
        if node["est"] == 0: break
        frm = node["at"]
        if any(not blocked(frm[0] + a, frm[1] + b) and not relax(i, (frm[0] + a, frm[1] + b))
               for a, b in ((-1, -1), (-1, 1), (1, -1), (1, 1), (-1, 0), (0, -1), (1, 0), (0, 1))): break
    points, last = [], (-2, -2)
    i = best
    while i >= 0 and len(points) <= 77:
        node = pool[i]
        if node["parent"] < 0: break
        par = pool[node["parent"]]["at"]
        step = (node["at"][0] - par[0], node["at"][1] - par[1])
        if step != last: points.append(node["at"]); last = step
        i = node["parent"]
    if not points or len(points) > 77: return []
    return points[::-1]


def player_path(x, y, tx, ty, near, to_unit, blocked):
    """monsters.hpp player_path, line for line."""
    toward = toward_path(x, y, tx, ty, 0x49, near, blocked)
    if toward and unit_distance(toward[-1][0] - tx, toward[-1][1] - ty, 1, 1) <= near and toward[-1] != (x, y): return toward
    if (x - tx) ** 2 + (y - ty) ** 2 < 0x145:
        searched = search_path(x, y, tx, ty, to_unit, blocked)
        if searched: return searched
    return toward


def chase_check(target, distance, stop, mover, mx, my, idx, count, at_end, budget):
    """monsters.hpp chase_check, line for line: (result, budget)."""
    far = 4 if BREAK else 5
    ran_out = idx >= count and not at_end
    if target and distance <= stop: return 0, budget
    if not ((mover and (abs(mx) > far or abs(my) > far)) or ran_out) if target else not ran_out: return 1, budget
    if budget == 0: return 0, budget
    return 2, max(budget - idx, 0)


def check_paths(e, rng, cases, dump):
    walls, asked = set(), set()
    def collide(e):
        at = (e.arg(1) & 0xffff, e.arg(2) & 0xffff)
        asked.add(at)
        return int(at in walls)
    e.hook(0x64d910, collide, 5)
    e.hook(0x64fe40, lambda e: 0, 1)
    mark, bad, short, dumped = e.brk, 0, 0, 0
    for case in range(cases):
        e.brk = mark
        walls.clear(); asked.clear()
        x, y = 1000, 1000
        spread = rng.choice((3, 8, 14))
        while True:
            tx, ty = x + rng.randint(-spread, spread), y + rng.randint(-spread, spread)
            if (tx, ty) != (x, y): break
        density = rng.choice((0.0, 0.1, 0.25, 0.4))
        for wx in range(x - 16, x + 17):
            for wy in range(y - 16, y + 17):
                if (wx, wy) != (x, y) and rng.random() < density: walls.add((wx, wy))
        near, steps = rng.choice((0, 1, 1)), rng.choice((5, 5, 5, 1, 2, 14))
        unit, path, ctx = e.alloc(0x100), e.alloc(0x200), e.alloc(0x40)
        e.w32(unit, 1); e.w32(unit + 0x2c, path)
        e.w32(path, x << 16 | 0x8000); e.w32(path + 4, y << 16 | 0x8000); e.w32(path + 0x30, unit)
        e.w32(path + 0x3c, 0xd); e.w32(path + 0x7c, 0x800); e.w32(path + 0x1c, 1)
        e.w32(ctx, y << 16 | x); e.w32(ctx + 4, ty << 16 | tx); e.w32(ctx + 8, 1)
        e.mu.mem_write(ctx + 0x14, bytes([near])); e.w32(ctx + 0x18, steps); e.w32(ctx + 0x30, path)
        n = e.call(0x679c80, ecx=ctx)
        got = [struct.unpack("<HH", e.read(path + 0x9c + 4 * i, 4)) for i in range(n)]
        want = toward_path(x, y, tx, ty, steps, near, lambda a, b: (a, b) in walls)
        short += want[-1:] != [(tx, ty)]
        if got != want:
            bad += 1
            if bad <= 10: print(f"path case {case}: game {got}, port {want}; to {tx - x},{ty - y} near {near} steps {steps}")
        if dump and dumped < 8 and density and len(want) > 1:     # the walls it asked about
            dumped += 1
            ws = ", ".join(f"{{{a - x}, {b - y}}}" for a, b in sorted(walls & asked))
            ps = ", ".join(f"{{{a - x}, {b - y}}}" for a, b in want)
            print(f"{{ {tx - x}, {ty - y}, {steps}, {near}, {{ {ps} }}, {{ {ws} }} }},")
    print(f"paths ok: {cases} cases, {short} short of their end" if not bad else f"paths: {bad} of {cases} differ")
    return bad


def check_chase(e, rng, cases, dump):
    sizes, calls, results = {}, [], []
    e.hook(0x620510, lambda e: sizes.get(e.arg(0), 0), 1)
    def compute(e):
        path = e.arg(0)
        calls.append((e.r32(path + 0x3c), e.r32(path + 0x10)))
        return results.pop(0)
    e.hook(0x649970, compute, 3)
    e.hook(0x64c040, lambda e: 0, 1)
    mark, bad, kinds = e.brk, 0, [0, 0, 0]
    for case in range(cases):
        e.brk = mark
        sizes.clear(); calls.clear()
        results[:] = [rng.choice((0, 0, 1, 3, 7)), rng.choice((0, 2, 9))]
        x, y = 1000, 1000
        unit, path = e.alloc(0x100), e.alloc(0x200)
        e.w32(unit, 1); e.w32(unit + 0x2c, path)
        sizes[unit] = rng.randint(1, 3)
        e.w32(path, x << 16 | 0x8000); e.w32(path + 4, y << 16 | 0x8000); e.w32(path + 0x30, unit)
        typ = rng.choice((2, 0xd, 0xf))
        idx, count = rng.randint(0, 6), rng.randint(1, 6)
        budget, stop = rng.choice((0, 1, 3, 0x14, rng.randint(0, 30))), rng.choice((0, 0, 1, 3))
        ex, ey = (x, y) if rng.random() < 0.3 else (x + rng.randint(-8, 8), y + rng.randint(-8, 8))
        e.w32(path + 0x18, ey << 16 | ex)
        e.w32(path + 0x24, idx); e.w32(path + 0x28, count); e.w32(path + 0x34, 0x20); e.w32(path + 0x3c, typ)
        e.mu.mem_write(path + 0x93, bytes([stop, budget]))
        kind = rng.choice((None, 0, 1, 1, 2))
        mover, dist, mx, my = False, 0, 0, 0
        if kind is not None:
            spread = rng.choice((2, 6, 12))
            fx, fy = x + rng.randint(-spread, spread), y + rng.randint(-spread, spread)
            foe, fpath = e.alloc(0x100), e.alloc(0x200)
            e.w32(foe, kind); e.w32(foe + 0x2c, fpath)
            if kind == 2: e.w32(fpath + 0xc, fx); e.w32(fpath + 0x10, fy)
            else: e.w32(fpath, fx << 16 | 0x8000); e.w32(fpath + 4, fy << 16 | 0x8000)
            sizes[foe] = rng.randint(0 if kind == 2 else 1, 3)
            e.w32(path + 0x58, foe)
            ax, ay = fx + rng.randint(-8, 8), fy + rng.randint(-8, 8)
            e.w32(path + 0x14, ay << 16 | ax)
            mover, dist, mx, my = kind < 2, unit_distance(fx - x, fy - y, sizes[unit], sizes[foe]), ax - fx, ay - fy
        sp1, scripted = e.r32(path + 0x10), list(results)
        got = e.call(0x6503f0, regs={"esi": path})
        have = (got, e.read(path + 0x94, 1)[0], list(calls), e.r32(unit + 0xc4) & 1)
        r, left = chase_check(kind is not None, dist, stop, mover, mx, my, idx, count, (ex, ey) == (x, y), budget)
        want_calls, ret = [], r
        if r == 2:
            want_calls.append((0xd if kind is not None else 2, sp1 if kind is not None else ey << 16 | ex))
            ret = scripted[0]
            if not ret: want_calls.append((0xf, want_calls[0][1])); ret = scripted[1]
        need = (ret, left, want_calls, int(r == 2))
        kinds[min(r, 2)] += 1
        if have != need:
            bad += 1
            if bad <= 10: print(f"chase case {case}: game {have}, port {need}")
        if dump and case < 12:
            print(f"{{ {int(kind is not None)}, {dist}, {stop}, {int(mover)}, {mx}, {my}, {idx}, {count}, {int((ex, ey) == (x, y))}, {budget}, {r}, {left} }},")
    print(f"chase ok: {cases} cases, {kinds[0]} stop, {kinds[1]} go on, {kinds[2]} re-path" if not bad else f"chase: {bad} of {cases} differ")
    return bad


def check_search(e, rng, cases, dump):
    walls, asked = set(), set()
    def collide(e):
        spot = (e.arg(1) & 0xffff, e.arg(2) & 0xffff)
        asked.add(spot)
        return int(spot in walls)
    e.hook(0x64d910, collide, 5)
    e.hook(0x64fe40, lambda e: 0, 1)
    mark, bad, found, dumped = e.brk, [0, 0], [0, 0], 0
    for case in range(cases):
        e.brk = mark
        walls.clear(); asked.clear()
        x, y = 1000, 1000
        spread = rng.choice((3, 8, 14, 20))
        while True:
            tx, ty = x + rng.randint(-spread, spread), y + rng.randint(-spread, spread)
            if (tx, ty) != (x, y): break
        density = rng.choice((0.1, 0.25, 0.4, 0.5))
        for wx in range(x - 24, x + 25):
            for wy in range(y - 24, y + 25):
                if (wx, wy) != (x, y) and rng.random() < density: walls.add((wx, wy))
        if rng.random() < 0.3:                                   # a wall across the way, with a gap or none
            gap = rng.choice((None, rng.randint(-6, 6)))
            mx, my = (x + tx) // 2, (y + ty) // 2
            for k in range(-10, 11):
                if k != gap: walls.add((mx + k, my) if abs(tx - x) < abs(ty - y) else (mx, my + k))
            walls.discard((x, y))
        near, to_unit = rng.choice((0, 0, 1)), rng.random() < 0.3
        unit, path, ctx, target = e.alloc(0x100), e.alloc(0x200), e.alloc(0x40), e.alloc(0x100)
        e.w32(unit, 0); e.w32(unit + 0x2c, path)
        e.w32(path, x << 16 | 0x8000); e.w32(path + 4, y << 16 | 0x8000); e.w32(path + 0x30, unit)
        e.w32(path + 0x3c, 7); e.w32(path + 0x7c, 0x800); e.w32(path + 0x1c, 1)
        e.w32(path + 0x58, target if to_unit else 0)
        e.w32(ctx, y << 16 | x); e.w32(ctx + 4, ty << 16 | tx); e.w32(ctx + 8, 1)
        e.mu.mem_write(ctx + 0x14, bytes([near])); e.w32(ctx + 0x18, 0x49); e.w32(ctx + 0x30, path)
        blocked = lambda a, b: (a, b) in walls
        for which, (addr, port) in enumerate(((0x67b850, lambda: search_path(x, y, tx, ty, to_unit, blocked)),
                                              (0x679ed0, lambda: player_path(x, y, tx, ty, near, to_unit, blocked)))):
            e.w32(path + 0x24, 0); e.w32(path + 0x28, 0)
            n = e.call(addr, ecx=ctx)
            got = [struct.unpack("<HH", e.read(path + 0x9c + 4 * i, 4)) for i in range(n)]
            want = port()
            found[which] += bool(want) and want[-1] == (tx, ty)
            if got != want:
                bad[which] += 1
                if bad[which] <= 6: print(f"{('search', 'player')[which]} case {case}: game {got}, port {want}; to {tx - x},{ty - y} near {near} unit {to_unit}")
        if dump and dumped < 40 and 2 < len(want) < 8 and len(walls & asked) < 40:
            dumped += 1
            ws = ", ".join(f"{{{a - x}, {b - y}}}" for a, b in sorted(walls & asked))
            ps = ", ".join(f"{{{a - x}, {b - y}}}" for a, b in want)
            print(f"{{ {tx - x}, {ty - y}, {near}, {int(to_unit)}, {{ {ps} }}, {{ {ws} }} }},   // player")
    for which, name in enumerate(("search", "player")):
        print(f"{name} ok: {cases} cases, {found[which]} reach their end" if not bad[which] else f"{name}: {bad[which]} of {cases} differ")
    return sum(bad)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = int(args[0]) if args else 20000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    dump = "--dump" in sys.argv
    e = emu.Emu()
    bad = check_paths(e, rng, cases, dump)
    bad += check_chase(e, rng, cases, dump)
    bad += check_search(emu.Emu(), rng, max(cases // 10, 1), dump)   # unhooked: the chase stubs the sizes
    flagged = [m for m in range(16) if e.read(0x73c6d0 + m, 1)[0]]
    if flagged != [2, 15]: print(f"think at once: game modes {flagged}, ai.cpp [2 (WL), 15 (RN)]"); bad += 1
    else: print("think at once: WL and RN only, as ai.cpp")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
