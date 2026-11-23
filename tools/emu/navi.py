# SPDX-License-Identifier: GPL-3.0-or-later
"""Flavie's think (MonAI Navi, FUN_005e7e20) run natively.

    uv run python navi.py [cases] [seed] [--dump tests/navi_cases.inc]

Random scenes: a player talking to her (FUN_00572dc0, her interact list),
a player she heeds or none (FUN_005ddf20: none is Flavie herself) and its
reach (the search's own distance; under 4 sets its flag), that player busy
(FUN_00535060), what her think's flag word holds before that search
([ebp-4], never set: the search writes it only under 4; town-npcs.md
"Flavie"), her greeting timer (AI control +0x18), a monster she could
shoot (FUN_005ddc30) and its distance, the unit seed. Hooked: those, the
greeting (FUN_00553380), the attack (FUN_005ddf90) and the stand
(FUN_005de080). The unit seed draw (FUN_0045c390) runs natively.
--dump writes `scene | outcome` lines for tests/monsters/test_monster_thinks.cpp,
which runs rules::navi_think on each. Prints `ok`.
"""
import random
import sys

import emu

CUR = {}


def install(e):
    cur = CUR
    def out(*item): cur["out"].append(item); return 0
    e.hook(0x572dc0, lambda _e: int(cur["s"]["listed"]), 0)
    def heeds(_e):                                                     # FUN_005ddf20(&flag): a player under 16, flag under 4
        s = cur["s"]
        if not (s["visitor"] and s["reach"] < 16): return cur["mon"]
        if s["reach"] < 4: e.w32(e.arg(0), 1)
        return cur["foe"]
    e.hook(0x5ddf20, heeds, 1)
    e.hook(0x535060, lambda _e: int(cur["s"]["busy"]), 0)
    def second(_e):                                                    # FUN_005ddc30(&distance, &melee)
        s = cur["s"]
        e.w32(e.arg(0), s["enemy_dist"] if s["enemy"] else 0x7fffffff)
        return cur["enemy"] if s["enemy"] else 0
    e.hook(0x5ddc30, second, 2)
    e.hook(0x553380, lambda _e: out("greet", e.arg(0) == cur["foe"]), 1)
    e.hook(0x5ddf90, lambda _e: out("attack", e.arg(0), e.arg(1) == cur["enemy"]), 2)
    e.hook(0x5de080, lambda _e: out("stand", e.arg(0)), 1)


def setup(rng):
    return {"listed": rng.random() < 0.2, "visitor": rng.random() < 0.7, "reach": rng.choice((0, 1, 2, 3, 4, 5, rng.randint(0, 20))),
            "busy": rng.random() < 0.2, "stale": rng.choice((0, 0, 1, rng.getrandbits(32))), "timer": rng.choice((0, 0, 1, 2, 59, 60, -1, -5, rng.randint(-100, 100))),
            "enemy": rng.random() < 0.5, "enemy_dist": rng.choice((23, 24, 25, 26, rng.randint(0, 60))),
            "seed": (rng.getrandbits(32), rng.getrandbits(32))}


def run(e, s):
    mark = e.brk
    game, ctx, ctrl, mon, foe, enemy = (e.alloc(n) for n in (0x200, 0x40, 0x40, 0x200, 0x200, 0x200))
    e.w32(ctx, ctrl); e.w32(ctrl + 0x18, s["timer"] & 0xffffffff)
    e.w32(mon, 1); e.w32(mon + 0x20, s["seed"][0]); e.w32(mon + 0x24, s["seed"][1])
    e.w32(foe, 0); e.w32(enemy, 1)
    CUR.update(s=s, out=[], mon=mon, foe=foe, enemy=enemy)
    e.w32(emu.STACK + emu.STACK_SIZE - 0x1000 - 16, s["stale"])         # [ebp-4] at entry: below the argument, the return and EBP
    e.call(0x5e7e20, ctx, ecx=game, edx=mon)
    after = (e.s32(ctrl + 0x18), e.r32(mon + 0x20), e.r32(mon + 0x24))
    e.brk = mark
    return CUR["out"], after


def outcome(out):
    """The calls in navi_think's terms: kind (0 stand, 3 attack), stand frames, greeted."""
    kind, frames, greet = 0, 0, 0
    for item in out:
        if item[0] == "stand": kind, frames = 0, item[1]
        elif item[0] == "attack":
            if item[1] != 4 or not item[2]: raise RuntimeError(f"unexpected attack: {out}")
            kind = 3
        elif item[0] == "greet":
            if not item[1]: raise RuntimeError(f"greeted someone else: {out}")
            greet = 1
    return kind, frames, greet


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
        key = (act, s["listed"], s["visitor"] and s["reach"] < 16, s["stale"] != 0, s["timer"] > 0, s["timer"] < 0)
        kinds[act] = kinds.get(act, 0) + 1
        scene = [int(s["listed"]), int(s["visitor"]), s["reach"], int(s["busy"]), int(s["stale"] != 0), s["timer"], int(s["enemy"]), s["enemy_dist"], *s["seed"]]
        lines.setdefault(key, []).append(" ".join(map(str, scene)) + " | " + " ".join(map(str, (*act, *after))))
    if "--dump" in sys.argv:
        with open(sys.argv[sys.argv.index("--dump") + 1], "w") as f:
            f.write("// tools/emu/navi.py --dump: game.exe's Flavie think (MonAI Navi, FUN_005e7e20), a line each.\n")
            for rows in lines.values(): f.write("".join(f'"{line}",\n' for line in rows[:8]))
    print(f"ok: {cases} cases, {kinds}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
