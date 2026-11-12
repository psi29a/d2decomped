#!/usr/bin/env python3
"""A knockback's pace: game.exe's own path step against d2d's knock_moved / knock_ms.

game side, under unicorn (emu.py):
1. FUN_00623f50 (a unit's mode speed) on a fake player in mode 0x13 (KB) or a monster in mode 0xd
   (KB), with the case's velocitypercent (stat 0x43) and faster run/walk (stat 96): the velocity it
   hands the path (FUN_00648690, hooked: recorded). Hooked as walk_velocity.py does (not the claim):
   FUN_00645270, FUN_00620f00, FUN_006216e0 (no special mode), FUN_006213d0 (animation rate),
   FUN_00625480 / FUN_00625500 (the case's stats).
2. A path at the case's start (16.16 subtiles, the subtile's centre) with one point, the case's end,
   path type 8 and that velocity: FUN_0064fe40 sets its direction (FUN_0064fc60's slope table at
   0x6eb7e0) and step (velocity x dir >> 8), then FUN_00650090 (the step, snapping onto the point
   once it's within a step on both axes) runs a frame at a time, its delta added to the position as
   FUN_00650660 does, till it snaps. Collision (FUN_00650150) isn't run: the cases are open ground.
   Printed: num velocity frames step_x step_y (the first frame's delta, 1/65536 subtile).

d2d side: `test_knockback --knock-pace cases.tsv` (tests/game/test_knockback.cpp): the same start
and end through knock_ms (frames) and knock_moved (the first frame's step).

    uv run python knock_pace.py gen N SEED > cases.tsv
    uv run python knock_pace.py game cases.tsv [game.exe] > g.txt
    ../../build/tests/test_knockback --knock-pace cases.tsv | grep -E '^[0-9]+ ' > d.txt
    uv run python knock_pace.py compare g.txt d.txt

cases.tsv: num kind(0 player, 1 monster) sx sy ex ey vp frw (start and end subtiles; stat 0x43 and 96).
compare: frames must match; the step may differ by game.exe's slope table (FUN_0064fc60 quantizes the
direction to 1/127 of the slope), printed as the largest gap.
"""
import math
import random
import struct
import sys


def gen(n, seed):
    rng = random.Random(seed)
    rows = []
    for reach in (5, 10):                         # 5 subtiles; a Sand Leaper (class 0x4e) 10
        for octant in range(64):                  # every direction, a step apart
            angle = octant * math.tau / 64
            rows.append((rng.randrange(2), 5000, 5000, 5000 + round(reach * math.cos(angle)), 5000 + round(reach * math.sin(angle)), 100, 0))
    while len(rows) < n:
        reach = rng.choice((5, 5, 5, 10))
        angle = rng.random() * math.tau
        sx, sy = rng.randint(100, 30000), rng.randint(100, 30000)
        ex, ey = sx + round(reach * math.cos(angle)), sy + round(reach * math.sin(angle))
        if (ex, ey) == (sx, sy):
            continue
        rows.append((rng.randrange(2), sx, sy, ex, ey, rng.choice((100, rng.randint(-100, 300))), rng.choice((0, rng.randint(-100, 400)))))
    for i, r in enumerate(rows):
        print(i, *r, sep="\t")


def game(path, exe=None):
    sys.path.insert(0, ".")
    from emu import Emu
    emu = Emu(exe)
    unit = emu.alloc(0x200)
    path_ = emu.alloc(0x200)
    out = emu.alloc(16)
    st = {}
    got = {}
    emu.hook(0x645270, lambda e: 0, 6)
    emu.hook(0x620f00, lambda e: 0, 4)
    emu.hook(0x6216e0, lambda e: 0, 0)
    emu.hook(0x6213d0, lambda e: 256, 0)
    emu.hook(0x625480, lambda e: st.get(e.arg(1), 0) & 0xFFFFFFFF, 3)
    emu.hook(0x625500, lambda e: st.get(e.arg(1), 0) & 0xFFFFFFFF, 3)
    emu.hook(0x6485f0, lambda e: 0, 0)

    def setvel(e):
        got["vel"] = struct.unpack("<i", struct.pack("<I", e.arg(1)))[0]
        return 0
    emu.hook(0x648690, setvel, 4)
    for line in open(path):
        f = line.split()
        if not f:
            continue
        num, kind, sx, sy, ex, ey, vp, frw = map(int, f)
        st.clear()
        st[0x43], st[96] = vp, frw
        emu.mu.mem_write(unit, b"\0" * 0x200)
        emu.w32(unit, 0 if kind == 0 else 1)
        emu.w32(unit + 0x10, 0x13 if kind == 0 else 0xd)
        emu.w32(unit + 0x2c, path_)
        got.clear()
        emu.call(0x623f50, unit, 0, 0)
        vel = got.get("vel")
        emu.mu.mem_write(path_, b"\0" * 0x200)
        emu.w32(path_, (sx << 16) + 0x8000)
        emu.w32(path_ + 4, (sy << 16) + 0x8000)
        emu.w32(path_ + 0x24, 0)
        emu.w32(path_ + 0x28, 1)
        emu.w32(path_ + 0x3c, 8)
        emu.w32(path_ + 0x7c, vel or 0)
        emu.mu.mem_write(path_ + 0x9c, struct.pack("<HH", ex, ey))
        emu.call(0x64fe40, 0, ecx=path_, edx=0)
        frames, first = 0, None
        while frames < 100:
            snapped = emu.call(0x650090, out, regs={"esi": path_})
            step_x, step_y = emu.s32(out), emu.s32(out + 4)
            if first is None:
                first = (step_x, step_y)
            emu.w32(path_, emu.r32(path_) + step_x)
            emu.w32(path_ + 4, emu.r32(path_ + 4) + step_y)
            frames += 1
            if snapped:
                break
        print(num, vel, frames, *first)


def compare(a, b):
    ga = {l.split()[0]: l.split() for l in open(a) if l.strip()}
    gb = {l.split()[0]: l.split() for l in open(b) if l.strip()}
    bad, velfail, gap, missing = [], 0, 0, [k for k in ga if k not in gb]
    for k, g in ga.items():
        if k not in gb:
            continue
        if int(g[1]) != 0x1000:
            velfail += 1
        if g[2] != gb[k][1]:
            bad.append(k)
        gap = max(gap, abs(int(g[3]) - int(gb[k][2])), abs(int(g[4]) - int(gb[k][3])))
    print(f"cases {len(ga)} compared {len(ga) - len(missing)} frame mismatches {len(bad)} velocity not 0x1000 {velfail} "
          f"largest step gap {gap} / 65536 subtile")
    for k in bad[:12]:
        print("  ", k, "game", " ".join(ga[k][1:]), "d2d", " ".join(gb[k][1:]))
    if missing:
        print(f"  missing d2d results for {len(missing)} cases, first: {missing[:5]}")
    return 1 if bad or velfail or missing or not ga or gap > 0x10000 // 100 else 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == "game":
        game(*sys.argv[2:4])
    elif cmd == "compare":
        sys.exit(compare(*sys.argv[2:4]))
    else:
        sys.exit(__doc__)
