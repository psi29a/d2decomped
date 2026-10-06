# SPDX-License-Identifier: GPL-3.0-or-later
"""The merc's think (MonAI 61 "Hireable", FUN_005e52d0) against a port.

    uv run python merc.py [cases] [seed] [--dump]

game.exe's think runs natively on random setups: the merc and its owner
(the player) at random subtiles, their modes, the owner's path end and
footstep ring (player data +0xa0 / +0xa8), the levels under them, the 0x40
collision bit under the merc, a target or none (FUN_005ddc30), and which
moves find a path. Its effects are hooked: a move set off (FUN_005a7c20:
mode, spot, the path ctx's type / pct / steps from FUN_005a6260, the stop
distance), a stand (FUN_005de080), a teleport (FUN_0054dc40 / FUN_00554ea0),
the attack think (FUN_005e5050). The rolls are game.exe's own on the merc's
seed (+0x20). `merc_think` below is components/rules/merc.hpp line for line;
the effects and the seed after must agree. Prints `ok` or each mismatch.
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

MASK = 0xffffffff
OWNER_SPOT = ((0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0), (1, 1))     # DAT_006ea998 / 978 by DAT_006e34f0
KSTEP = (13, 26, 39, 53, 68, 85, 105)


def direction64(x, y, tx, ty):
    """monsters.hpp direction64 (FUN_0064fdc0)."""
    across, down = abs(tx - x), abs(ty - y)
    steep = across <= down
    major, minor = (down, across) if steep else (across, down)
    eighth = 0 if major == 0 else max(0, min(127, ((minor * 0x10000 * 0x7f) & MASK) // ((major * 0x10000) & MASK) if ((minor * 0x10000 * 0x7f) & MASK) < 0x80000000 else 0))
    d = sum(1 for s in KSTEP if s <= eighth)
    if not steep: d = (-d - 1) & 0xf
    if ty < y: d = (-d - 1) & 0x1f
    return (d + 8) & 0x3f if tx < x else (((-d - 1) & 0x3f) + 8) & 0x3f


class Seed:
    def __init__(self, lo, hi): self.lo, self.hi = lo, hi
    def step(self):
        v = self.lo * 0x6ac690c5 + self.hi
        self.lo, self.hi = v & MASK, v >> 32
        return self.lo
    def mask(self, n): return (n - 1) & self.step()                      # FUN_00472210
    def rand(self, n):                                                    # FUN_0045c390 / FUN_0045c3e0
        if n < 1: return 0
        lo = self.step()
        return (n - 1) & lo if n & (n - 1) == 0 else lo % n


def gap(merc, size, ox, oy):
    """FUN_005dc380: each axis less the merc's size (not under 0); (short + 2 x long) / 2."""
    dx, dy = max(abs(merc[0] - ox) - size, 0), max(abs(merc[1] - oy) - size, 0)
    return (dy + dx * 2 if dx > dy else dx + dy * 2) // 2


def spot_gap(x, y, tx, ty):
    """FUN_005dc5c0."""
    dx, dy = abs(x - tx), abs(y - ty)
    return (dy + dx * 2) // 2 if dy < dx else (dx + dy * 2) // 2


def merc_think(s, seed, move):
    """components/rules/merc.hpp hireable_think. `s`: the setup; `move(kind,
    x, y, mode, ctx)` sets off (True: it found a path). Effects as tuples."""
    out = []
    ctx = {"type": 0, "pct": 0, "steps": 0}                             # FUN_005a6260
    def pace(typ, pct, steps):
        if typ: ctx["type"] = typ
        if pct: ctx["pct"] = pct
        if steps: ctx["steps"] = min(steps, 0x4d)
    def go(x, y, mode=2):                                                 # FUN_005a7c20 via dee50 / ded90 / deb60
        ok = move(x & 0xffff, y & 0xffff, mode, dict(ctx))
        out.append(("move", x & 0xffff, y & 0xffff, mode, ctx["type"], ctx["pct"], ctx["steps"], int(ok)))
        ctx.update(type=0, pct=0, steps=0)
        return ok
    def offset(cx, cy, n):                                                # FUN_005df400 / de200 / df530
        if seed.step() & 1 == 0: ox, oy = seed.rand(n), n
        else: ox, oy = n, seed.rand(n)
        if seed.step() & 1: ox = -ox
        if seed.step() & 1: oy = -oy
        return cx + ox, cy + oy
    mx, my = s["merc"]; ox, oy = s["owner"]; ex, ey = s["end"]
    def follow(mode, run, pct):                                          # FUN_005e3930
        if mode == 3:
            out.append(("teleport",))
            return
        if mode == 0:
            d8 = s["dir8_of"][direction64(ox, oy, ex, ey)]
            lvl = s["level_at"](ox, oy)
            for _ in range(8):
                vx, vy = OWNER_SPOT[d8]
                tx, ty = ex + vx * 8, ey + vy * 8
                if s["level_at"](tx, ty) == lvl:
                    pace(0, pct, 0x28)
                    if go(tx, ty): return
                d8 = (d8 + 1) & 7
            go(*offset(mx, my, 4))
            return
        if s["owner_mode"] == 2:                                         # mode 1, the owner walking: to its path's end
            pace(0, 0, 100)
            go(ex, ey)
            return
        cursor, tried, looked = s["cursor"], False, 0
        while True:
            cursor = 0x13 if cursor == 0 else cursor - 1
            fx, fy = s["ring"][cursor]
            if spot_gap(mx, my, fx, fy) > 5:
                pace(0, pct, 100)
                if go(fx, fy): return
                pace(0xf, pct, 100)
                if go(fx, fy, 0xf if run else 2): return
                if not tried:
                    tried = True
                    pace(1, pct, 100)
                    if go(fx, fy, 0xf if run else 2): return
            looked += 1
            if looked > 0x13:
                n = max(gap(s["merc"], s["size"], ox, oy) >> 2, 4)
                pace(0, 0xf, 0)
                go(*offset(mx, my, n))
                return
    if s["merc_mode"] in (2, 0xf): return out, ctx
    d = gap(s["merc"], s["size"], ox, oy)
    if d > 100: follow(3, 0, 0); return out, ctx
    if d > 24: follow(1, 1, 0x3c); return out, ctx
    if d > 16 and s["owner_mode"] in (2, 6): follow(0, 0, 0); return out, ctx
    if d > 16 and s["owner_mode"] == 3: follow(0, 1, 0x3c); return out, ctx
    if s["merc_mode"] != 1:
        out.append(("stand", 5)); return out, ctx
    ranged = s["cls"] not in (0x152, 0x230, 0x231)
    if s["on40"] and seed.mask(0x80) < (12 if ranged else 6):
        go(*offset(mx, my, 5)); return out, ctx
    if not s["town"] and s["target"] is not None and s["target"] < 0x19:
        out.append(("attack",)); return out, ctx
    if s["on40"]:
        go(*offset(mx, my, 5)); return out, ctx
    if s["level_at"](ox, oy) != s["level_at"](mx, my):
        follow(0, 1, 0x3c); return out, ctx
    if gap(s["merc"], s["size"], ox, oy) <= 1:
        pace(7, 0, 0)
        if go(*offset(ox, oy, 4)): return out, ctx
        sx = -1 if mx < ox else 1 if ox < mx else 0
        sy = -1 if my < oy else 1 if oy < my else 0
        if go(mx + 4 * sx, my + 4 * sy): return out, ctx
        pace(0, 0, 0x28)
        go(ex, ey)
        return out, ctx
    if seed.rand(100) < 5:
        go(*offset(ox, oy, 16)); return out, ctx
    out.append(("stand", 5))
    return out, ctx


def setup(rng):
    mx, my = 5000 + rng.randint(-40, 40), 5000 + rng.randint(-40, 40)
    far = rng.choice((1, 3, 10, 20, 30, 60, 130))
    ox, oy = mx + rng.randint(-far, far), my + rng.randint(-far, far)
    ex, ey = (ox, oy) if rng.random() < 0.3 else (ox + rng.randint(-12, 12), oy + rng.randint(-12, 12))
    ring = [(ox + rng.randint(-30, 30), oy + rng.randint(-30, 30)) for _ in range(20)]
    split = rng.choice((None, None, mx + rng.randint(-20, 20)))
    return {
        "cls": rng.choice((0x10f, 0x152, 0x167, 0x230, 0x231)),
        "merc": (mx, my), "owner": (ox, oy), "end": (ex, ey), "size": rng.choice((1, 2, 3)),
        "merc_mode": rng.choice((1, 1, 1, 1, 2, 4, 0xf)), "owner_mode": rng.choice((1, 2, 3, 6, 4)),
        "ring": ring, "cursor": rng.randint(0, 19),
        "on40": rng.random() < 0.2, "town": rng.random() < 0.3,
        "target": rng.choice((None, rng.randint(0, 40))),
        "split": split, "level_at": (lambda x, y, split=split: 2 if split is not None and x >= split else 1),
        "fails": rng.random(), "seed": (rng.getrandbits(32), rng.getrandbits(32)),
    }


CUR = {}                                                                  # the case the hooks answer for


def install(e):
    """The hooks, once (each takes a stub slot)."""
    cur = CUR
    def out(item): cur["out"].append(item); return 0
    e.hook(0x58f0d0, lambda e: cur["owner"], 0)
    e.hook(0x58ee80, lambda e: 0, 0)
    e.hook(0x639df0, lambda e: 0, 2)
    e.hook(0x639db0, lambda e: 0, 3)
    e.hook(0x620510, lambda e: cur["s"]["size"] if e.arg(0) == cur["merc"] else 2, 1)
    e.hook(0x620bb0, lambda e: cur["room"], 1)
    e.hook(0x61ad30, lambda e: cur["room"], 3)
    e.hook(0x61b130, lambda e: cur["s"]["level_at"](e.arg(1) & 0xffff, e.arg(2) & 0xffff), 3)
    e.hook(0x61ab00, lambda e: int(cur["s"]["town"]), 1)
    e.hook(0x64d910, lambda e: int(cur["s"]["on40"] and e.arg(4) == 0x40), 5)
    def search(e):
        if cur["s"]["target"] is None: return 0
        e.w32(e.arg(0), cur["s"]["target"])
        return 0x1234
    e.hook(0x5ddc30, search, 2)
    e.hook(0x5e5050, lambda e: out(("attack",)), 5)
    e.hook(0x5de080, lambda e: out(("stand", e.arg(0))), 1)
    e.hook(0x540e60, lambda e: 0, 2)
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
        mode, x, y = e.r32(m), e.r32(m + 0xc) & 0xffff, e.r32(m + 0x10) & 0xffff
        ok = cur["moves"].random() >= cur["s"]["fails"]
        out(("move", x, y, mode, ctx["type"], ctx["pct"], ctx["steps"], int(ok)))
        ctx.update(type=0, pct=0, steps=0)
        return int(ok)
    e.hook(0x5a7c20, setoff, 3)
    e.hook(0x54dc40, lambda e: out(("teleport",)), 5)


def run_game(e, s, rng_moves):
    """game.exe's think on setup `s`: (effects, seed after)."""
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    game, merc, owner, mdata, mpath, opath, pcd, params, ai, room = (alloc(n) for n in (0x2000, 0x200, 0x200, 0x200, 0x200, 0x200, 0x400, 0x40, 0x100, 0x100))
    mx, my = s["merc"]; ox, oy = s["owner"]; ex, ey = s["end"]
    e.w32(merc, 1); e.w32(merc + 4, s["cls"]); e.w32(merc + 0x10, s["merc_mode"]); e.w32(merc + 0x14, mdata)
    e.w32(merc + 0x20, s["seed"][0]); e.w32(merc + 0x24, s["seed"][1]); e.w32(merc + 0x2c, mpath)
    e.w32(mpath, mx << 16 | 0x8000); e.w32(mpath + 4, my << 16 | 0x8000); e.w32(mpath + 0x30, merc); e.w32(mpath + 0x1c, room)
    e.w32(owner, 0); e.w32(owner + 0x10, s["owner_mode"]); e.w32(owner + 0x14, pcd); e.w32(owner + 0x2c, opath)
    e.w32(opath, ox << 16 | 0x8000); e.w32(opath + 4, oy << 16 | 0x8000); e.w32(opath + 0x30, owner); e.w32(opath + 0x1c, room)
    e.mu.mem_write(opath + 0x18, struct.pack("<HH", ex, ey))
    e.mu.mem_write(pcd + 0xa0, bytes([s["cursor"]]))
    for k, (fx, fy) in enumerate(s["ring"]): e.w32(pcd + 0xa8 + 8 * k, fx); e.w32(pcd + 0xac + 8 * k, fy)
    e.w32(params, ai)
    CUR.update(s=s, out=[], ctx={"type": 0, "pct": 0, "steps": 0}, owner=owner, merc=merc, room=room, moves=rng_moves)
    e.call(0x5e52d0, params, ecx=game, edx=merc)
    return CUR["out"], (e.r32(merc + 0x20), e.r32(merc + 0x24))


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
        want_out, _ = merc_think(s, seed, lambda x, y, mode, ctx: moves.random() >= s["fails"])
        want = (want_out, (seed.lo, seed.hi))
        kind = got[0][0][0] if got[0] else "-"
        kinds[kind] = kinds.get(kind, 0) + 1
        key = (kind, tuple(m[3:7] for m in want_out if m[0] == "move")[:2], seed.lo != s["seed"][0])
        if dump and got == want and len(dumped) < 60 and key not in dumped:
            dumped[key] = True
            tries = [m for m in want_out if m[0] == "move"]
            last = next((m for m in reversed(want_out) if m[0] == "move" and m[7]), None)
            final = ("moved" if last else "failed") if tries else want_out[0][0] if want_out else "none"
            mv = ", ".join(f"{{ {m[1] - s['merc'][0]}, {m[2] - s['merc'][1]}, {m[3]}, {m[4]}, {m[5]}, {m[6]}, {m[7]} }}" for m in tries)
            rg = ", ".join(f"{{ {a - s['merc'][0]}, {b - s['merc'][1]} }}" for a, b in s["ring"])
            sp = s["split"] - s["merc"][0] if s["split"] is not None else 9999
            print(f"{{ {s['cls']}, {s['size']}, {s['merc_mode']}, {s['owner'][0] - s['merc'][0]}, {s['owner'][1] - s['merc'][1]}, {s['owner_mode']}, "
                  f"{s['end'][0] - s['merc'][0]}, {s['end'][1] - s['merc'][1]}, {s['cursor']}, {int(s['on40'])}, {int(s['town'])}, "
                  f"{-1 if s['target'] is None else s['target']}, {sp}, {s['seed'][0]}u, {s['seed'][1]}u, Kind::{final}, {want[1][0]}u, {want[1][1]}u, "
                  f"{{ {mv} }}, {{ {rg} }} }},   // merc")
        if got != want:
            bad += 1
            if bad <= 8:
                print(f"case {case}: game {got}\n          port {want}\n          {dict((k, v) for k, v in s.items() if k not in ('ring', 'level_at', 'dir8_of'))}")
    print(f"merc ok: {cases} cases, {kinds}" if not bad else f"merc: {bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
