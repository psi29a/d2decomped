# SPDX-License-Identifier: GPL-3.0-or-later
"""The unplaced spawners' MonAI thinks and the special AIs 3 / 6 / 9 / 12 run natively, dumped for d2d's replay.

    uv run python ai_group2.py <think,think|all> [cases] [seed] [--dump tests/ai_group2_cases.inc]

EvilHole (FUN_005fb410), InvisoSpawner (FUN_005e0160), MosquitoNest (FUN_005e0260), 7TIllusion (FUN_005ea080),
InvisoPet (FUN_005ea7a0); special AI 3's init (FUN_005e5730) and think (FUN_005e5870), 6 (FUN_005e7c10),
9 (FUN_005e7f80), 12 (FUN_005e8340). Each runs in game.exe on random scenes: the params block (target,
AI distance, melee), the AI control's +0x14 / +0x18 / +0x1c, MonStats aip1..8 and Skill1, the unit's mode
and seed, the frame. Every helper it calls is hooked at its entry: the acts (stand, wait, a mode, a mode at
a point, a skill, a walk, a run, a wander's draws as FUN_005de200 makes them, a walk to a point), the effects
(a command sent, out of the group, a command done, the AI toggle, states, overlays, a monster made) and the
answers the scene gives (its command, its group hit, hit, its owner, in town, its search's find, in reach,
a spot, a spawn). FUN_0045c390 / FUN_00472210 run for real on the unit's seed.

Each run comes to rules::Think (act, n, x, y, unit, wait, event), the effects, the AI control and seed after;
--dump writes them as tests/ai_group2_cases.inc lines for tests/monsters/test_ai_group2.cpp. Prints `ok`.
Never runs a game; no MPQs are read.
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu
from merc import Seed

MASK = 0xffffffff
S32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
THINKS = {
    "EvilHole": 0x5fb410, "InvisoSpawner": 0x5e0160, "MosquitoNest": 0x5e0260, "7TIllusion": 0x5ea080, "InvisoPet": 0x5ea7a0,
    "FormationInit": 0x5e5730, "Formation": 0x5e5870, "NpcMelee": 0x5e7c10, "MinionRetreat": 0x5e7f80, "StationaryNpc": 0x5e8340,
}
ORDER = list(THINKS)
# rules::MonAct by name; rules::ThinkUnit by the scene's unit; rules::ThinkEffect::Kind by name.
ACTS = ["idle", "a1", "a2", "s2", "skill", "walk", "run", "approach", "keep", "circle", "wander", "none", "die", "around",
        "home", "point", "untraced", "s3", "s4", "seq", "fly", "gone", "s1", "sc", "mode_at", "move_to", "spawn", "direct"]
UNIT = {"foe": 0, None: 1, "found": 2, "mon": 5}
EFFECT = {"state": 0, "overlay": 1, "rally": 3, "command_done": 4, "toggle": 9, "spawn": 19, "leave_group": 20}
MODE_ACT = {4: "a1"}
DEMON_HOLE = 0x2c7


def setup(rng, think):
    s = {"think": think, "diff": rng.randint(0, 2), "frame": rng.randint(100, 5000)}
    s["seed"] = (rng.getrandbits(32), rng.getrandbits(32))
    s["dist"] = rng.choice((rng.randint(0, 6), rng.randint(0, 30), rng.randint(0, 60)))
    s["melee"] = rng.random() < 0.4
    s["got_hit"] = rng.random() < 0.3
    word = lambda: rng.choice((0, 0, 1, 2, -1, rng.randint(0, 12), rng.randint(0, 99), s["frame"] + rng.randint(-60, 60)))
    s["ctrl"] = [word(), word(), word()]
    s["aip"] = [rng.choice((0, 1, 2, 3, rng.randint(0, 30), rng.randint(0, 250), -rng.randint(1, 5))) for _ in range(8)]
    s["skill"] = rng.choice((-1, rng.randint(0, 400)))
    s["sk_mode"] = rng.randint(0, 15)
    s["mode"] = rng.choice((1, 1, 10, 11, 11, rng.randint(0, 15)))
    s["demon_hole"] = rng.random() < 0.3
    s["in_town"] = rng.random() < 0.3
    s["owner"] = rng.random() < 0.85
    s["owner_mode"] = rng.choice((1, 2, 3, 3, rng.randint(0, 15)))
    cx, cy = rng.randint(4000, 6000), rng.randint(4000, 6000)
    s["spots"] = {u: (cx + rng.randint(-30, 30), cy + rng.randint(-30, 30)) for u in ("mon", "foe", "own", "found")}
    s["self_owner"] = rng.random() < 0.5
    s["group_hit"] = rng.random() < 0.25
    s["command"] = rng.choice((None, None, 2, 8, rng.randint(1, 14)))
    s["command_spot"] = (cx + rng.randint(-40, 40), cy + rng.randint(-40, 40))
    s["found"] = rng.random() < 0.6
    s["found_melee"] = rng.random() < 0.5
    s["reach"] = rng.random() < 0.4
    s["spawned"] = rng.random() < 0.7
    s["spot_free"] = rng.random() < 0.7
    s["sets_off"] = rng.random() < 0.7
    if think in ("FormationInit", "InvisoPet") and not s["owner"]:
        s["owner"] = True                               # FormationInit asserts without one; InvisoPet's non-idle path reads it
    return s


CUR = {}


def install(e):
    cur = CUR
    def out(*item): cur["out"].append(item); return 0
    def name(addr): return cur["names"].get(addr)
    def seed():
        u = cur["units"]["mon"]
        return Seed(e.r32(u + 0x20), e.r32(u + 0x24))
    def put(sd):
        u = cur["units"]["mon"]
        e.w32(u + 0x20, sd.lo); e.w32(u + 0x24, sd.hi)
    # The acts.
    e.hook(0x5de080, lambda e: out("stand", S32(e.arg(0))), 1)
    e.hook(0x5de0f0, lambda e: out("wait", S32(e.arg(0))), 1)
    e.hook(0x5ddfc0, lambda e: out("mode_at", S32(e.arg(0)), S32(e.arg(1)), S32(e.arg(2))), 3)
    e.hook(0x5dead0, lambda e: out("skill", e.arg(0) & 0xff, S32(e.arg(1)), name(e.arg(2)), S32(e.arg(3)), S32(e.arg(4))) or 1, 5)
    e.hook(0x5ddf90, lambda e: out("mode", e.arg(0), name(e.arg(1))), 2)
    e.hook(0x5dec80, lambda e: out("walk", name(e.arg(0)), S32(e.arg(1))) or 1, 2)
    e.hook(0x5ded40, lambda e: out("run", name(e.arg(0)), S32(e.arg(1))) or 1, 2)
    def wander(e):                                                     # FUN_005de200's three draws, as mon_ais.py makes them
        n = e.arg(0) & 0xff
        sd = seed()
        if sd.step() & 1 == 0: x, y = sd.rand(n), n
        else: x, y = n, sd.rand(n)
        if sd.step() & 1: x = -x
        if sd.step() & 1: y = -y
        put(sd)
        return out("wander", n, x, y) or 1
    e.hook(0x5de200, wander, 1)
    e.hook(0x5ded90, lambda e: out("walkto", S32(e.arg(0)), S32(e.arg(1))), 2)
    def goto(e):                                                       # FUN_005dee50: a walk to the point, set off?
        ok = int(cur["s"]["sets_off"])
        out("goto", S32(e.arg(0)), S32(e.arg(1)), ok)
        return ok
    e.hook(0x5dee50, goto, 2)
    # The effects.
    e.hook(0x58f730, lambda e: out("rally", e.r32(e.arg(0) + 8)), 1)
    e.hook(0x58f160, lambda e: out("leave_group"), 0)
    e.hook(0x58ed10, lambda e: out("command_done"), 0)
    def toggle(e):                                                     # FUN_005b0e00 back to the MonAI row's think: the reset branch
        out("toggle", e.arg(1))
        for k in range(3): e.w32(e.arg(0) + 0x14 + 4 * k, 0)
        return 0
    e.hook(0x5b0e00, toggle, 2)
    e.hook(0x540e60, lambda e: 0, 2)
    e.hook(0x5417d0, lambda e: out("event", S32(e.arg(1)) - cur["s"]["frame"]), 4)
    e.hook(0x639db0, lambda e: out("state", e.arg(1), e.arg(2)) if e.arg(0) == cur["made"] else out("state?"), 3)
    e.hook(0x621e40, lambda e: out("overlay", e.arg(1)) if e.arg(0) == cur["made"] else out("overlay?"), 3)
    e.hook(0x620c10, lambda e: 0, 2)
    # The answers.
    def command(e):
        c = cur["s"]["command"]
        if c is None: return 0
        e.w32(cur["cmd"] + 8, c)
        e.w32(cur["cmd"] + 0xc, cur["s"]["command_spot"][0]); e.w32(cur["cmd"] + 0x10, cur["s"]["command_spot"][1])
        return cur["cmd"]
    e.hook(0x58ee80, command, 0)
    def group(e):                                                      # FUN_0058f380(ECX unit, EDX &found; 0, LAB_005e5710)
        if e.arg(1) != 0x5e5710: raise RuntimeError(f"group callback {e.arg(1):#x}")
        if cur["s"]["group_hit"]: e.w32(e.mu.reg_read(UC_X86_REG_EDX), 1)
        return 0
    e.hook(0x58f380, group, 2)
    e.hook(0x5dd2b0, lambda e: int(cur["s"]["got_hit"]), 0)
    def leader(e):                                                     # FUN_0058f0d0(ECX unit): its leader / owner
        s = cur["s"]
        if not s["owner"]: return 0
        return cur["units"]["mon"] if s["self_owner"] else cur["units"]["own"]
    e.hook(0x58f0d0, leader, 0)
    e.hook(0x553540, lambda e: cur["units"]["foe"] if cur["s"]["owner"] else 0, 2)
    e.hook(0x620bb0, lambda e: 0x1234, 1)
    e.hook(0x61ab00, lambda e: int(cur["s"]["in_town"]), 1)
    def search(e):                                                     # FUN_005dd7f0 / FUN_005de890(ctrl, &, &melee): the unit found
        s = cur["s"]
        e.w32(e.arg(2), int(s["found_melee"]))
        return cur["units"]["found"] if s["found"] else 0
    e.hook(0x5dd7f0, search, 3)
    e.hook(0x5de890, search, 3)
    e.hook(0x622c40, lambda e: int(cur["s"]["reach"]), 3)
    def spawn_spot(e):                                                 # FUN_0063efa0(unit, &class, &x, &y, &mode, 0, 0): its spawn column
        for k, v in enumerate((0x5e, 70, 80, 1)): e.w32(e.arg(1 + k), v)
        return 0
    e.hook(0x63efa0, spawn_spot, 7)
    e.hook(0x463740, lambda e: 0x1234, 1)
    def make(e):                                                       # FUN_005b2f20: the monster made, or none
        if not cur["s"]["spawned"]: return 0
        out("spawn", S32(e.arg(2)))
        return cur["made"]
    e.hook(0x5b2f20, make, 6)
    e.hook(0x63ec70, lambda e: e.arg(1), 2)                            # FUN_0063ec70(room, class): the class asked
    def location(e):                                                   # FUN_0054dc40(ECX game, EDX room; 0, class, &x, &y, 0)
        e.w32(e.arg(2), 42); e.w32(e.arg(3), 53)
        return 1
    e.hook(0x54dc40, location, 5)
    e.hook(0x5fd350, lambda e: int(cur["s"]["spot_free"]), 3)
    e.hook(0x6488c0, lambda e: cur["s"]["spots"][cur["paths"][e.arg(0)]][0], 1)   # a unit's path spot
    e.hook(0x648900, lambda e: cur["s"]["spots"][cur["paths"][e.arg(0)]][1], 1)
    e.hook(0x45adf0, lambda e: cur["s"]["spots"][name(e.mu.reg_read(UC_X86_REG_ECX)) or "mon"][0], 0)
    e.hook(0x45ae20, lambda e: cur["s"]["spots"][name(e.mu.reg_read(UC_X86_REG_ECX)) or "mon"][1], 0)


def run_game(e, s):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    game, params, ctrl, row, cmd, data = (alloc(n) for n in (0x200, 0x40, 0x40, 0x200, 0x40, 0x1000))
    units, names, paths = {}, {}, {}
    for k, n in enumerate(("mon", "foe", "own", "found")):
        u, path = alloc(0x200), alloc(0x80)
        e.w32(u, 1); e.w32(u + 4, 0x100 + k); e.w32(u + 0xc, k + 1); e.w32(u + 0x2c, path)
        units[n], names[u], paths[path] = u, n, n
    mon = units["mon"]
    e.w32(mon + 4, DEMON_HOLE if s["demon_hole"] else 0x141)
    e.w32(mon + 0x10, s["mode"])
    e.w32(units["own"] + 0x10, s["owner_mode"])
    if s["self_owner"]: e.w32(mon + 0x10, s["mode"])
    e.w32(mon + 0x20, s["seed"][0]); e.w32(mon + 0x24, s["seed"][1])
    e.mu.mem_write(game + 0x6d, bytes([s["diff"]])); e.w32(game + 0xa8, s["frame"])
    for k, v in enumerate(s["aip"]): e.mu.mem_write(row + 0x56 + 6 * k + 2 * s["diff"], struct.pack("<h", v))
    e.mu.mem_write(row + 0x170, struct.pack("<h", s["skill"]))
    e.mu.mem_write(row + 0x180, bytes([s["sk_mode"]]))
    e.w32(0x744304, data); e.w32(data + 0xa80, 0x2ce)                 # MonStats' row count (InvisoSpawner's mummy1 test)
    for k, v in enumerate(s["ctrl"]): e.w32(ctrl + 0x14 + 4 * k, v & MASK)
    block = alloc(0x40); e.w32(mon + 0x14, block); e.w32(block + 0x28, ctrl)   # unit +0x14 -> +0x28: its AI control
    e.w32(params, ctrl); e.w32(params + 7 * 4, row)
    e.w32(params + 8, units["foe"])
    e.w32(params + 20, s["dist"]); e.w32(params + 24, int(s["melee"]))
    made = alloc(0x200)
    CUR.update(s=s, out=[], units=units, names=names, paths=paths, cmd=cmd, made=made)
    e.call(THINKS[s["think"]], params, ecx=game, edx=mon)
    flagged = bool(e.r32(mon + 0xc4) & 0x20000)
    spawned_flags = e.r32(made + 0xc4)
    return CUR["out"], [S32(e.r32(ctrl + 0x14 + 4 * k)) for k in range(3)], (e.r32(mon + 0x20), e.r32(mon + 0x24)), flagged, spawned_flags


def outcome(s, out, flagged):
    """The native calls in rules::Think's terms: (act, n, x, y, unit, wait, event), the effects, the walks asked."""
    act, effects, asks = ["none", 0, 0, 0, UNIT["foe"], 0, 0], [], []
    def to(kind, n=0, x=0, y=0, unit="foe"):
        act[:] = [kind, n, x, y, UNIT[unit], act[5], act[6]]
    for item in out:
        k = item[0]
        if k == "stand": to("idle", item[1])
        elif k == "wait": act[5] = item[1]
        elif k == "event": act[6] = item[1]
        elif k == "mode_at":
            if item[1] == 0: to("die" if flagged else "gone")
            else: to("mode_at", item[1], item[2], item[3], None)
        elif k == "mode": to(MODE_ACT[item[1]], unit=item[2])
        elif k == "skill" and item[2] == s["skill"] and item[3] == "foe": to("skill", 0)
        elif k == "skill": to("skill", item[2], item[4], item[5], item[3])
        elif k in ("walk", "run"): to(k, item[2], unit=item[1])
        elif k == "wander": to("wander", item[1], item[2], item[3])
        elif k == "walkto": to("move_to", 0, item[1], item[2], None)
        elif k == "goto":
            asks.append(item[3])
            if item[3]: to("move_to", 0, item[1], item[2], None)
        elif k == "rally": effects.append((EFFECT["rally"], item[1], 0))
        elif k == "spawn": effects.append((EFFECT["spawn"], item[1] if s["think"] == "InvisoSpawner" else -1, 0))
        elif k in ("state", "overlay"): effects.append((EFFECT[k], item[1], item[2] if k == "state" else 0))
        elif k == "toggle": effects.append((EFFECT["toggle"], item[1], 0))
        elif k in EFFECT: effects.append((EFFECT[k], 0, 0))
        else: raise RuntimeError(f"{s['think']}: unexpected {item}")
    return act, effects, asks


def dump_case(s, act, effects, asks, ctrl, seed):
    """A tests/ai_group2_cases.inc line: the scene | the walks asked | the effects | what it comes to."""
    own = s["spots"]["mon"] if s["self_owner"] else s["spots"]["own"]
    nums = [ORDER.index(s["think"]), s["frame"], s["dist"], int(s["melee"]), int(s["got_hit"]), *s["ctrl"], *s["aip"], int(s["skill"] >= 0)]
    nums += [s["mode"], int(s["demon_hole"]), int(s["in_town"]), int(s["owner"]), s["mode"] if s["self_owner"] else s["owner_mode"], *own]
    nums += [int(s["self_owner"]), *s["spots"]["mon"], int(s["group_hit"]), s["command"] or 0, *s["command_spot"]]
    nums += [int(s["found"]), int(s["found_melee"]), int(s["reach"]), int(s["spawned"]), int(s["spot_free"]), *s["seed"]]
    tail = [ACTS.index(act[0])] + act[1:] + ctrl + list(seed)
    return (" ".join(map(str, nums)) + " | " + " ".join(map(str, asks)) + " | " + " ; ".join(" ".join(map(str, x)) for x in effects)
            + " | " + " ".join(map(str, tail)))


def check(e, think, cases, rng, dumped):
    mark, kinds = e.brk, {}
    for _ in range(cases):
        e.brk = mark
        s = setup(rng, think)
        out, ctrl, seed, flagged, spawned_flags = run_game(e, s)
        if any(item[0] == "spawn" for item in out) and think == "EvilHole" and spawned_flags != 0x4020000:
            raise AssertionError(f"EvilHole: young flags {spawned_flags:#x}")
        if any(item[0] in ("state?", "overlay?") for item in out):
            raise AssertionError(f"{think}: a state or overlay on another unit than the one made")
        act, effects, asks = outcome(s, out, flagged)
        if act[0] == "die" and not flagged:
            raise AssertionError(f"{think}: died unflagged")
        kinds[act[0]] = kinds.get(act[0], 0) + 1
        if dumped is not None:
            key = (act[0], tuple(item[0] for item in out), tuple(asks))
            rows = dumped.setdefault(think, {}).setdefault(key, [])
            if len(rows) < 6: rows.append(dump_case(s, act, effects, asks, ctrl, seed))
    print(f"{think} ok: {cases} cases, {kinds}")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.endswith(".inc")]
    which = args[0] if args else "all"
    cases = int(args[1]) if len(args) > 1 else 2000
    rng = random.Random(int(args[2]) if len(args) > 2 else 1)
    e = emu.Emu()
    install(e)
    dumped = {} if "--dump" in sys.argv else None
    for think in (THINKS if which == "all" else which.split(",")): check(e, think, cases, rng, dumped)
    if dumped is not None:
        path = sys.argv[sys.argv.index("--dump") + 1]
        with open(path, "w", newline="\n") as f:
            f.write(f"// tools/emu/ai_group2.py {which} {cases} {args[2] if len(args) > 2 else 1} --dump: game.exe's own runs, a line each.\n")
            for rows in dumped.values():
                for lines in rows.values(): f.write("".join(f'"{line}",\n' for line in lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
