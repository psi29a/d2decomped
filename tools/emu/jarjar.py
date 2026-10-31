# SPDX-License-Identifier: GPL-3.0-or-later
"""The palace guard's think (MonAI JarJar, FUN_005e7590) run natively.

    uv run python jarjar.py [cases] [seed] [--dump tests/jarjar_cases.inc]

Random scenes: homed or not (FUN_005e6800), home, spot, the frame and the
guard's last frame (AI command 10 +0x18), aside (FUN_0059b8b0), stood aside
(FUN_0059aec0), a player by or none (FUN_005ddf20: none is the guard
itself), its reach (FUN_005dc380), the unit seed. Hooked: stand
(FUN_005de080), walk (FUN_005ded90), the heed (FUN_005e68f0: with a player
by it acts, the guard's MonStats interact always set), FUN_005df530 /
FUN_005de200 with their seed draws; FUN_0059b8f0's spot is none (d2d keeps
none, town_npcs.hpp). The unit seed draws (FUN_0045c390) run natively.
--dump writes `scene | outcome` lines for tests/monsters/test_monster_thinks.cpp, which
runs rules::jarjar_think on each. Prints `ok`.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu
from merc import Seed, spot_gap

CUR = {}


def install(e):
    cur = CUR
    def out(*item): cur["out"].append(item); return 0
    def seed():
        u = cur["mon"]
        return Seed(e.r32(u + 0x20), e.r32(u + 0x24))
    def put(sd): e.w32(cur["mon"] + 0x20, sd.lo); e.w32(cur["mon"] + 0x24, sd.hi)
    def homed(_e):                                                     # FUN_005e6800
        s = cur["s"]
        if s["homed"]: return 0
        e.w32(cur["cmd"] + 0xc, s["x"]); e.w32(cur["cmd"] + 0x10, s["y"])
        out("stand", 20)
        return 1
    e.hook(0x5e6800, homed, 1)
    e.hook(0x58eef0, lambda _e: cur["cmd"], 1)
    e.hook(0x59b8b0, lambda _e: int(cur["s"]["aside"]), 0)
    e.hook(0x59aec0, lambda _e: int(cur["s"]["stood"]), 0)
    e.hook(0x59b8f0, lambda _e: 0, 0)
    def gap(_e):                                                       # FUN_005dc5c0(ECX unit, EDX x, y)
        s = cur["s"]
        return spot_gap(s["x"], s["y"], e.mu.reg_read(UC_X86_REG_EDX), e.arg(0))
    e.hook(0x5dc5c0, gap, 1)
    e.hook(0x5ded90, lambda _e: out("walk", e.arg(0), e.arg(1)), 2)
    e.hook(0x5de080, lambda _e: out("stand", e.arg(0)), 1)
    near = lambda: cur["s"]["visitor"] and cur["s"]["reach"] < 16                # FUN_005dde80: a player within 16
    e.hook(0x5ddf20, lambda _e: cur["foe"] if near() else cur["mon"], 1)
    e.hook(0x5dc380, lambda _e: cur["s"]["reach"] if e.mu.reg_read(UC_X86_REG_EDX) == cur["foe"] else 0, 0)
    def heed(_e):
        if not near(): return 0
        out("heed")
        return 1
    e.hook(0x5e68f0, heed, 1)
    def draws(n):
        sd = seed()
        if sd.step() & 1 == 0: x, y = sd.rand(n), n
        else: x, y = n, sd.rand(n)
        if sd.step() & 1: x = -x
        if sd.step() & 1: y = -y
        put(sd)
        return x, y
    e.hook(0x5df530, lambda _e: out("around", *draws(e.arg(1) & 0xff)), 2)
    e.hook(0x5de200, lambda _e: out("wander", *draws(e.arg(0) & 0xff)), 1)


def setup(rng):
    hx, hy = rng.randint(4000, 6000), rng.randint(4000, 6000)
    frame = rng.randint(300, 5000)
    near = lambda: rng.choice((0, 1, 2, 3, rng.randint(0, 12)))
    s = {"homed": rng.random() < 0.9, "hx": hx, "hy": hy, "frame": frame,
         "last": rng.choice((0, frame, frame - rng.randint(1, 199), frame - rng.randint(200, 400), frame + rng.randint(1, 50))),
         "aside": rng.random() < 0.5, "stood": rng.random() < 0.5, "visitor": rng.random() < 0.5,
         "reach": rng.randint(0, 30), "seed": (rng.getrandbits(32), rng.getrandbits(32))}
    up = rng.random() < 0.5 and s["aside"]                               # about the aside spot, 3 north
    s["x"] = hx + rng.choice((-1, 1)) * near()
    s["y"] = hy - (3 if up else 0) + rng.choice((-1, 1)) * near()
    return s


def run(e, s):
    mark = e.brk
    game, ctx, cmd, mon, foe = (e.alloc(n) for n in (0x200, 0x40, 0x40, 0x200, 0x200))
    e.w32(game + 0xa8, s["frame"])
    e.w32(cmd + 0xc, s["hx"]); e.w32(cmd + 0x10, s["hy"]); e.w32(cmd + 0x18, s["last"])
    e.w32(mon, 1); e.w32(mon + 0x20, s["seed"][0]); e.w32(mon + 0x24, s["seed"][1])
    e.w32(foe, 0)
    CUR.update(s=s, out=[], cmd=cmd, mon=mon, foe=foe)
    e.call(0x5e7590, ctx, ecx=game, edx=mon)
    after = (e.s32(cmd + 0xc), e.s32(cmd + 0x10), e.s32(cmd + 0x18), e.r32(mon + 0x20), e.r32(mon + 0x24))
    e.brk = mark
    return CUR["out"], after


def outcome(out):
    """The calls in jarjar_think's terms: kind (0 stand, 1 walk, 2 heed), frames, x, y.
    A stand after a heed or a walk cancels its walk (the mode's set at once, FUN_005a7c20)."""
    kind, frames, x, y = 0, 0, 0, 0
    for item in out:
        if item[0] == "stand": frames = item[1]; kind = 2 if kind == 2 else 0
        elif item[0] == "walk": kind, x, y = 1, item[1], item[2]
        elif item[0] in ("wander", "around"): raise RuntimeError(f"unreachable roll ran: {out}")
        elif item[0] == "heed": kind = 2
    return kind, frames, x, y


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.endswith(".inc")]
    cases = int(args[0]) if args else 3000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    e = emu.Emu()
    install(e)
    lines, kinds = {}, {}
    for _ in range(cases):
        s = setup(rng)
        out, after = run(e, s)
        act = outcome(out)
        heed_stand = act[0] == 2 and any(item[0] == "stand" for item in out)
        key = (act[0], heed_stand, s["aside"], s["homed"], tuple(item[0] for item in out))
        kinds[key[:2]] = kinds.get(key[:2], 0) + 1
        scene = [int(s["homed"]), s["hx"], s["hy"], s["x"], s["y"], s["frame"], s["last"], int(s["aside"]), int(s["stood"]), int(s["visitor"]), s["reach"], *s["seed"]]
        lines.setdefault(key, []).append(" ".join(map(str, scene)) + " | " + " ".join(map(str, (*act, int(heed_stand), *after))))
    if "--dump" in sys.argv:
        with open(sys.argv[sys.argv.index("--dump") + 1], "w") as f:
            f.write("// tools/emu/jarjar.py --dump: game.exe's palace guard think (FUN_005e7590), a line each.\n")
            for rows in lines.values(): f.write("".join(f'"{line}",\n' for line in rows[:8]))
    print(f"ok: {cases} cases, {kinds}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
