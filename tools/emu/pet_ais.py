# SPDX-License-Identifier: GPL-3.0-or-later
"""The pets' thinks but NecroPet's (docs/research/re/pet-ai.md) against ports.

    uv run python pet_ais.py <ai|all> [cases] [seed] [--dump]

Each think runs natively in game.exe on random setups. What it reads comes
from the setup: the owner (or none), town, levels, the foes the searches
find (FUN_005dd7f0 / FUN_005ddc30 / FUN_005d2f80 and the driver's, AI
params [2], [5], [6]) with their distances and melee flags, the gaps
(FUN_005dc380) and distances (FUN_006416d0) between the units, clear lines
(FUN_005dc640), states, stats, MonStats aip1..5 / Skill1..2 / Sk*mode /
Velocity / Run, the AI control's +0x14 / +0x18 / +0x1c and the frame.
Following (FUN_005e3ea0) and the follow-or-fight test (FUN_005e45d0) are
necropet.py's; here they're logged and answer from the setup. The moves
run natively down to FUN_005a7c20 (logged with the path ctx), skills
(FUN_005dead0 / FUN_005de000) and stands / waits are logged. The port
below each must give the same log, AI control and seed. Prints `ok`.
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu
from merc import Seed

UNITS = ("pet", "owner", "foe", "foe2", "near")
MASK = 0xffffffff


class Port:
    """The port's view of a setup and its log (the same tuples the hooks write)."""
    def __init__(self, s, moves):
        self.s, self.out, self.moves = s, [], moves
        self.seed = Seed(*s["seed"])
        self.ctx = {"type": 0, "pct": 0, "steps": 0}
        self.ctrl = list(s["ctrl"])
        self.follows, self.decides = list(s["follow"]), list(s["decide"])
        self.row = s["row"]

    def aip(self, k): return self.row["aip"][k]
    def pace(self, typ, pct, steps):
        if typ: self.ctx["type"] = typ
        if pct: self.ctx["pct"] = pct & MASK
        if steps: self.ctx["steps"] = min(steps & 0xff, 0x4d)
    def go(self, x, y, mode=2, unit=None):                                 # FUN_005a7c20
        ok = self.moves.random() >= self.s["fails"]
        self.out.append(("move", x & 0xffff, y & 0xffff, mode, unit, self.ctx["type"], self.ctx["pct"], self.ctx["steps"], int(ok)))
        self.ctx = {"type": 0, "pct": 0, "steps": 0}
        return ok
    def pos(self, name): return self.s["pos"][name]
    def about(self, name, n):                                             # FUN_005df400 / de200 / df530
        cx, cy = self.pos(name)
        if self.seed.step() & 1 == 0: ox, oy = self.seed.rand(n), n
        else: ox, oy = n, self.seed.rand(n)
        if self.seed.step() & 1: ox = -ox
        if self.seed.step() & 1: oy = -oy
        return self.go(cx + ox, cy + oy)
    def away(self, name, reach, flag):                                    # FUN_005defe0
        (px, py), (ux, uy) = self.pos("pet"), self.pos(name)
        sx = -1 if px < ux else 1 if ux < px else 0
        sy = -1 if py < uy else 1 if uy < py else 0
        reach &= 0xff
        if reach > 5: self.pace(0, 0, reach)
        ok = self.go(px + reach * sx, py + reach * sy)
        return ok
    def at(self, name, mode=2, typ=0, flags=0):                           # FUN_005deb60 at a unit
        if typ: self.pace(typ, 0, 0)
        ok = self.go(0, 0, mode, name)
        if not ok:
            if flags & 1: self.out.append(("unreachable",))
            if flags & 2:
                if self.seed.step() % 100 < 0x46: self.about("pet", 4); return 1
                self.stand(10)
        return ok
    def run_at(self, name):                                               # FUN_005ded20: run (a walk in state 0x3c), 1 off
        if self.s["states"][0x3c]: self.ctx = {"type": 0, "pct": 0, "steps": 0}
        return self.go(0, 0, 2 if self.s["states"][0x3c] else 0xf, name)
    def keep_off(self, name, most, want):                                 # FUN_005de4e0: `want` off the unit, `most` a move
        d = self.gap("pet", name)
        sign = -1 if d < want else 1
        step = min(abs(d - want), most)
        (px, py), (ux, uy) = self.pos("pet"), self.pos(name)
        dx, dy = abs(ux - px), abs(uy - py)
        total = max(dx + dy, step)
        ax = ay = 0
        if total > 0:
            ax, ay = dx * step // total, dy * step // total
            while ax + ay < step: ax += 1; ay += 1
        sx = -1 if ux < px else 1 if px < ux else 0
        sy = -1 if uy < py else 1 if py < uy else 0
        return self.go(px + sx * ax * sign, py + sy * ay * sign)
    def circle(self, typ):                                                # FUN_005ecbc0: path type 5 / 6 round the owner
        self.pace(typ, 0, 4)
        return self.go(0, 0, 2, "owner")
    def line_off(self, name, reach):                                      # FUN_005de6f0: `reach` off the unit toward the pet
        d = self.dist("pet", name)
        if d == 0: return 0
        (px, py), (ux, uy) = self.pos("pet"), self.pos(name)
        tr = lambda a, b: int(a / b)
        return self.go(ux + tr((px - ux) * reach, d), uy + tr((py - uy) * reach, d))
    def die(self): self.go(0, 0, 0)                                      # FUN_005ddfc0(0): mode 0, death
    def teleport(self):                                                   # FUN_00554ea0 into the owner's room
        self.out.append(("teleport",))
        return self.s["teleport_ok"]
    def stand(self, n): self.out.append(("stand", n))
    def wait(self, n): self.out.append(("wait", n))
    def event(self, kind, frames): self.out.append(("event", kind, frames))
    def skill(self, mode, skill, unit, x=0, y=0): self.out.append(("skill", mode & 0xff, skill & MASK, unit, x & MASK, y & MASK)); return 1
    def seq(self, skill, unit, x=0, y=0): self.out.append(("seq", skill & MASK, unit, x & MASK, y & MASK)); return 1
    def follow(self, mode, run, pct, reach):
        self.out.append(("follow", mode, run, pct & MASK, reach))
        return self.follows.pop(0)
    def decide(self, foe, melee, stay, reach):
        self.out.append(("decide", foe, int(bool(melee)), stay, reach))
        return self.decides.pop(0)
    def gap(self, a, b): return self.s["gap"][frozenset((a, b))]
    def dist(self, a, b): return self.s["dist"][frozenset((a, b))]
    def pct(self):
        vel, run = self.row["velocity"], self.row["run"]
        return 100 if vel < 1 or run * 100 // vel - 100 > 99 else run * 100 // vel - 100


# --- the ports ---------------------------------------------------------------

def druid_bear(p):
    """FUN_005ed730 (MonAI 112 DruidBear)."""
    s = p.s
    if not s["owner"]:
        p.stand(10); return
    p.ctrl[2] = s["owner_id"]
    d = p.gap("pet", "owner")
    if d > 0x32: p.follow(3, 0, 0, 0); return
    pct = p.pct()
    if d > 0x1c: p.follow(0, 0, 100, 0); return
    if d > 0x12:
        if s["owner_mode"] in (2, 6) and p.follow(0, 0, 0, 0): return
        if s["owner_mode"] == 3 and p.follow(0, 0, 100, 0): return
    first = s["foe"]
    t, melee = None, False
    if s["foe"]:
        melee = s["foe_melee"]
        if s["foe_dist"] <= 0x1c: t = "foe"
    if t and not s["clear"]["foe"]: t = None
    if t is None and first and p.gap("pet", "foe") < 0x1c and s["clear"]["foe"]: t = "foe"
    if not melee:
        if t:
            if p.seed.rand(100) < p.aip(1): p.pace(0, pct, 0x28)
            p.go(0, 0, 2, t); return
    elif t:
        if p.seed.rand(100) < p.aip(2): p.seq(s["row"]["skill1"], t); return
        p.go(0, 0, 4, t); p.wait(p.aip(0)); return
    if d < 0x11: p.stand(15)
    else: p.follow(0, 0, 0, 0)


def spirit_wolf(p):
    """FUN_005ecee0 (MonAI 108 DruidWolf, Spirit Wolf: row 0x1a4)."""
    s = p.s
    if not s["owner"]:
        p.stand(10); return
    p.ctrl[2] = s["owner_id"]
    if s["town"]:
        if not p.decide(None, 0, 0, 6): p.stand(0x21)
        return
    reach = p.aip(3)
    t = "foe" if s["foe"] and s["foe_dist"] <= reach else None
    melee = s["foe_melee"] if s["foe"] else False
    c = "foe" if s["foe"] else None
    if not (t and s["clear"]["foe"]):
        t = None
        if c and p.gap("pet", c) < reach and not s["clear"][c]: t = c
    pct = p.pct()
    far = p.dist("owner", "pet")
    if p.row["skill1"] >= 0 and far > 0x32:
        p.skill(p.row["mode1"], p.row["skill1"], None, *p.pos("owner")); p.event(0, 4); p.wait(10); return
    m = s["owner_mode"]
    if far > p.aip(4) and p.follow(0, 1, 100, 0): return
    if far > p.aip(2):
        if m in (2, 6) and p.follow(0, 0, 0, 0): return
        if m == 3 and p.follow(0, 1, 100, 0): return
    if p.decide(t, melee, 1, 6): return
    if t:
        if melee:
            p.go(0, 0, 4, t); p.wait(p.aip(0)); return
        if p.dist("owner", t) < reach:
            p.pace(0, pct, 0); p.run_at(t); return
        if far > 10: p.keep_off("owner", 8, 6); return
        p.stand(15); return
    if p.seed.rand(100) < p.aip(1): p.about("pet", 10); return
    p.stand(15)


def fenris(p):
    """FUN_005ed2a0 (MonAI 108 DruidWolf, Dire Wolf: Fenris)."""
    s = p.s
    if not s["owner"]:
        p.stand(10); return
    p.ctrl[2] = s["owner_id"]
    if s["town"]:
        if not p.decide(None, 0, 0, 6): p.stand(0x21)
        return
    reach = p.aip(3)
    t = "foe" if s["foe"] and s["foe_dist"] <= reach else None
    melee = s["foe_melee"] if s["foe"] else False
    if t and not s["clear"]["foe"]: t = None
    if t is None and s["foe"] and s["foe_dist"] < reach and s["clear"]["foe"]: t = "foe"
    far = p.dist("owner", "pet")
    if t and reach < far and reach < p.dist("owner", t): t = None
    pct = p.pct()
    if p.row["skill2"] >= 0 and far > 0x32:
        p.skill(p.row["mode2"], p.row["skill2"], None, *p.pos("owner")); p.event(0, 2); p.wait(10); return
    m = s["owner_mode"]
    if far > p.aip(4) or (far > reach and m == 3):
        p.follow(0, 1, 100, 0); return
    if far > reach and m in (2, 6):
        p.follow(0, 0, 0, 0); return
    if p.decide(t, melee, 1, 6): return
    swing = False
    if p.row["skill1"] < 0 or s["states"][0x8a] or (p.seed.rand(100) >= p.aip(2) and t and p.ctrl[0] == 0):
        p.ctrl[0] = 0
    else:
        n = "near" if s["near"] else None
        if n and p.dist(n, "pet") < int(reach / 2):
            if s["melee_of"][n]:
                p.ctrl[0] = 0
                p.skill(p.row["mode1"], p.row["skill1"], n); return
            if not melee:
                p.run_at(n); p.ctrl[0] = 1; p.ctrl[1] = 5; return
            swing = True
    if not swing and not melee:
        if t: p.pace(0, pct, 0); p.run_at(t); return
        if p.seed.rand(100) >= p.aip(1): p.stand(15)
        else: p.about("pet", 10)
        return
    p.go(0, 0, 4, t); p.wait(p.aip(0))


def raven(p):
    """FUN_005ecc10 (MonAI 107 Raven)."""
    s = p.s
    if not s["owner"]:
        p.stand(10); return
    if p.ctrl[0] == -1:
        p.ctrl[0] = s["skillcalc"] if p.row["skill1"] >= 0 else 3
    elif p.ctrl[0] == 0:
        p.out.append(("die",)); return
    d = p.gap("pet", "owner")
    if d > 0x32: p.follow(3, 0, 0, 0); return
    if d > 0x1c: p.follow(0, 0, p.pct(), 0); return
    target, near, melee = ("foe2" if s["param"][2] else None), s["param"][5], s["param"][6]
    reach = (p.aip(1) + p.aip(0)) // 2 if p.aip(1) + p.aip(0) >= 0 else -((-(p.aip(1) + p.aip(0))) // 2)
    if target and p.ctrl[1] < s["frame"]:
        if p.seed.rand(100) < p.aip(3) and near < p.aip(4):
            if melee:
                p.go(0, 0, 4, target); p.ctrl[0] -= 1; p.ctrl[1] = s["frame"] + p.aip(2) * 10; return
            p.at(target); return
    if p.aip(0) < d or d < p.aip(1): pass
    else:
        if p.circle(5 if p.ctrl[2] else 6): return
        p.ctrl[2] = int(p.ctrl[2] == 0)
        if p.circle(5 if p.ctrl[2] else 6): return
    if not p.line_off("owner", reach): p.follow(1, 0, 0, 0)


def hydra(p):
    """FUN_005e9e60 (MonAI 86 Hydra)."""
    s = p.s
    if p.ctrl[0] < s["frame"]:
        p.die(); return
    if s["param"][2] and s["param"][5] < 0x19 and p.seed.step() % 100 < 0x3c:
        p.skill(p.row["mode1"], p.row["skill1"], "foe2"); return
    p.stand(10)


def totem(p):
    """FUN_005ed9e0 (MonAI 109 Totem: Oak Sage, Heart of Wolverine, Spirit of Barbs)."""
    s = p.s
    if not s["owner"]:
        p.stand(10); return
    t = "foe" if s["foe"] and s["foe_dist"] <= 0x18 else None
    melee = s["foe_melee"] if s["foe"] else False
    if melee and t and p.seed.step() % 100 < p.aip(0) and p.away(t, 6, 1): return
    if p.seed.step() % 100 < p.aip(1): t, melee = None, False
    d = p.dist("pet", "owner")
    if d > p.aip(2) and p.teleport():
        p.stand(0x19); return
    m = s["owner_mode"]
    if d > p.aip(3):
        if m in (2, 6) and p.follow(0, 0, 0, 0): return
        if m == 3 and p.follow(0, 0, 0x3c, 0): return
    if p.decide(t, melee, 0, 6): return
    p.stand(0x19)


def vines(p):
    """FUN_005ec6c0 (MonAI 110 Vines: Poison Creeper)."""
    s = p.s
    if not s["owner"]:
        p.stand(0x19); return
    if p.dist("pet", "owner") >= p.aip(4) and p.follow(3, 0, 0, 6): return
    if s["town"]:
        if not p.decide(None, 0, 0, 6): p.stand(p.aip(2))
        return
    c = "foe2" if s["foe2"] and s["foe2_dist"] < p.aip(1) else None
    melee = s["foe2_melee"] if s["foe2"] else False
    if p.decide(c, melee, 0, 6): return
    if c:
        if s["states"][2] or s["stats"][0x2d] == 100:
            p.away(c, p.aip(3), 0); return
        if not melee:
            p.at(c, flags=7); return
        if p.row["skill1"] >= 0 and p.aip(0) + p.ctrl[1] < s["frame"]:
            p.skill(p.row["mode1"], p.row["skill1"], c); p.ctrl[1] = s["frame"]; return
    p.stand(p.aip(2))


def cycle(p):
    """FUN_005ec8c0 (MonAI 111 CycleOfLife: Carrion Vine 0x1aa, Solar Creeper 0x1ab)."""
    s = p.s
    if not s["owner"]: return
    t2 = "foe2" if s["param"][2] else None
    hit = s["param"][6]
    if p.dist("pet", "owner") >= p.aip(4) and p.follow(3, 0, 0, 6): return
    if t2 and s["dead"]["foe2"]: t2, hit = None, False
    c, cd, melee = None, 0, False
    if p.row["skill1"] > 0 and p.row["skill1"] < SKILLS:
        r = min(max(s["skillcalc"], 5), 0x32) if s["skillcalc"] >= 6 else 5
        c = "near" if s["near"] else None
        if c: cd = p.dist("pet", c)
    if cd < p.aip(1):
        if c: melee = s["melee_of"][c]
    else: c = None
    if p.decide(c, melee, 0, 6): return
    feed = True
    if s["cls"] == 0x1aa: feed = s["stats"][6] < s["maxlife"]
    elif s["cls"] == 0x1ab: feed = s["stats"][8] < s["maxmana"]
    if c and melee and feed and p.aip(0) + p.ctrl[1] < s["frame"]:
        p.skill(8, p.row["skill1"], c); p.ctrl[1] = s["frame"]; return
    if hit and t2 and p.seed.rand(100) < 0x19:
        p.away(t2, p.aip(3), 0); return
    if not c:
        p.stand(p.aip(2)); return
    p.at(c, flags=7)


def shots(p, spend):
    """FUN_005ea2b0: the trap's shots (AI control +0x18; computed when below 0); out of
    them, no owner, or the owner in town: it dies (1)."""
    s = p.s
    if not s["owner"] or s["town"]:
        p.die(); return 1
    if p.ctrl[1] < 0:
        if p.row["skill1"] < 0 or p.row["skill1"] >= SKILLS:
            p.die(); return 1
        p.ctrl[1] = s["skillcalc"]
    if p.ctrl[1] > 0:
        if spend: p.ctrl[1] -= 1
        return 0
    p.die(); return 1


def sentry(p):
    """FUN_005ea3d0 (MonAI 101 AssassinSentry: Charged Bolt, Lightning, Wake of Fire, Inferno)."""
    s = p.s
    if shots(p, False): return
    c = "foe2" if s["foe2"] else None
    if not c or s["foe2_dist"] >= p.aip(3):
        p.stand(p.aip(2)); return
    if p.seed.rand(100) >= p.aip(0):
        p.stand(p.aip(1)); return
    if shots(p, True) == 0:
        p.skill(s["skillmode"], p.row["skill1"], c)


def death_sentry(p):
    """FUN_005ea980 (MonAI 104 DeathSentry)."""
    s = p.s
    if shots(p, False): return
    if s["skilllvl"] <= 0: return
    if not s["foe2"]:
        p.stand(p.aip(1)); return
    if s["corpse"] and s["corpse_id"] != p.ctrl[0] and p.dist("foe2", "near") < s["radius"] // 2 if s["radius"] >= 0 else -((-s["radius"]) // 2):
        if shots(p, True): return
        p.ctrl[0] = s["corpse_id"]
        p.skill(9, p.row["skill1"], "near"); return
    if s["foe2_dist"] < p.aip(3) and p.seed.rand(100) < p.aip(2):
        if shots(p, True): return
        p.skill(0xe, p.row["skill2"], "foe2"); return
    p.stand(p.aip(1))


def blade_creeper(p):
    """FUN_005ea540 (MonAI 102 BladeCreeper: Blade Sentinel)."""
    s = p.s
    if p.row["skill1"] < 0 or p.row["skill1"] >= SKILLS:
        p.die(); return
    if p.ctrl[0] < 0: p.ctrl[0] = s["skillcalc"] + s["frame"]
    if s["frame"] > p.ctrl[0]:
        p.die(); return
    if p.ctrl[2] == 0:
        p.ctrl[2] = 1
    if not s["ends"]:
        p.stand(3); return
    a, b = s["ends"]
    first, second = (a, b) if p.ctrl[1] == 0 else (b, a)
    p.pace(0xf, 0, 0x14)
    if p.go(*first): return
    p.ctrl[1] = int(p.ctrl[1] == 0)
    if p.go(*second): return
    if not (p.about("owner", 5) if s["owner"] else p.about("pet", 2)): p.stand(5)     # FUN_005df530 with no unit: FUN_005de200(2)


def usable(p, h, sid, melee):
    """FUN_005ead50: may the Shadow use skill `sid` (its handle `h`)? Its owner's class's,
    Skills.txt's AI type allows it (FUN_005eabf0), Attack always; else a mana roll, the
    +0x14 frame, and a roll against the +0x18 load, which grows by the skill's mana."""
    s = p.s
    if h is None or not s["owner"]: return 0
    if s["skill_class"].get(sid, 7) != s["owner_class"]: return 0
    if not s["aiok"].get(sid, False): return 0
    if sid == 0: return 1
    mana = s["mana"].get(sid, 0)
    if p.seed.rand(100) > 100 - int(mana * 0xa0 / 100): return 0
    if s["frame"] < p.ctrl[0]: return 0
    x82, x84 = p.row["x82"], p.row["x84"]
    lo = 1 if x82 < 2 else x82 if x82 < 0x80 else 0x80
    hi = 0x100 if x84 > 0xff else x84 if x84 > 1 else 1
    if p.ctrl[1] < lo or hi * 0x20 < p.ctrl[1]: p.ctrl[1] = lo
    a = p.seed.rand(p.ctrl[1])
    if p.seed.rand(100) < a: return 0
    p.ctrl[1] += int((0x140 - p.ctrl[2]) * mana / (p.ctrl[2] + 100))
    return 1


def shadow_warrior(p):
    """FUN_005eafa0 (MonAI 105 ShadowWarrior)."""
    s = p.s
    if not s["owner"]:
        p.stand(100); return
    x84 = p.row["x84"]
    n = 1 if x84 < 2 else min(x84, 0x100)
    p.ctrl[1] += -1 - p.aip(3)
    if p.ctrl[1] < 0 or n * 0x40 < p.ctrl[1]: p.ctrl[1] = 0
    d = p.dist("pet", "owner")
    t, melee = ("foe2" if s["param"][2] else None), s["param"][6]
    if p.aip(0) < s["param"][5] or p.aip(1) < d: t = None
    if p.decide(t, melee, 0, 6): return
    if t and s["left"] is not None and s["right"] is not None:
        has = {s["left"], s["right"]} | ({0} if s["pet_attack"] else set())
        handle = lambda sid: ("h", sid) if sid in has else None
        pl, pr = handle(s["left"]), handle(s["right"])                # FUN_00620190 / FUN_006201d0: the owner's left / right
        if pl and pr:
            pick = pl if p.seed.rand(2) else pr
            pid = pick[1]
            k = max(p.ctrl[2], 1)
            chance = p.aip(2) - 2 * k
            chance = 5 if chance <= 5 else min(chance, 100)
            if melee and p.seed.rand(100) < chance:
                pick, pid = handle(0), 0
            if not usable(p, pick, pid, melee):
                pick = pr if pick == pl else pl
                if not usable(p, pick, pick[1], melee):
                    pick = handle(0)
                    if not pick:
                        has.add(0); pick = handle(0)
            if pick:
                pid = pick[1]
                if s["kind"].get(pid, 0) != 1 or melee:
                    p.skill(s["smode"].get(pid, 0), pid, t)
                    p.ctrl[0] = int(s["skillcalc"] / 3) + 0x12 + s["frame"]
                    return
                p.run_at(t); return
    p.stand(0x19)


SKILLS = 400
AIS = {"hydra": (0x5e9e60, hydra, 351), "totem": (0x5ed9e0, totem, 424), "vines": (0x5ec6c0, vines, 425),
       "carrion": (0x5ec8c0, cycle, 0x1aa), "solar": (0x5ec8c0, cycle, 0x1ab),
       "sentry": (0x5ea3d0, sentry, 412), "deathsentry": (0x5ea980, death_sentry, 416), "bladecreeper": (0x5ea540, blade_creeper, 413),
       "raven": (0x5ecc10, raven, 419), "bear": (0x5ed730, druid_bear, 428), "spiritwolf": (0x5ecee0, spirit_wolf, 420), "fenris": (0x5ed2a0, fenris, 421),
       "shadowwarrior": (0x5eafa0, shadow_warrior, 417)}


# --- the setups and the harness ----------------------------------------------

def setup(rng, cls):
    s = {"cls": cls}
    px, py = 5000 + rng.randint(-40, 40), 5000 + rng.randint(-40, 40)
    s["pos"] = {u: (px + rng.randint(-30, 30), py + rng.randint(-30, 30)) for u in UNITS}
    s["pos"]["pet"] = (px, py)
    s["owner"] = rng.random() > 0.04
    s["owner_id"] = 2
    s["owner_mode"] = rng.choice((1, 1, 2, 3, 6, 4))
    s["town"] = rng.random() < 0.15
    s["frame"] = rng.randint(100, 5000)
    s["diff"] = rng.randint(0, 2)
    pairs = [frozenset((a, b)) for i, a in enumerate(UNITS) for b in UNITS[i + 1:]]
    s["gap"] = {k: rng.choice((0, 1, 2, rng.randint(0, 20), rng.randint(0, 60), rng.randint(0, 120))) for k in pairs}
    s["dist"] = {k: rng.choice((0, 1, 2, rng.randint(0, 20), rng.randint(0, 60), rng.randint(0, 120))) for k in pairs}
    for name in ("foe", "foe2", "near"):
        s[name] = rng.random() < 0.75
        s[name + "_dist"] = rng.choice((rng.randint(0, 8), rng.randint(0, 40)))
        s[name + "_melee"] = rng.random() < 0.4
    s["clear"] = {u: rng.random() < 0.75 for u in UNITS}
    s["melee_of"] = {u: rng.random() < 0.4 for u in UNITS}
    s["dead"] = {u: rng.random() < 0.2 for u in UNITS}
    s["states"] = {k: rng.random() < 0.2 for k in (2, 0xc, 0x3c, 0x8a)}
    s["stats"] = {k: rng.randint(0, 120) for k in (6, 8, 0x2d, 0x77)}
    s["maxlife"], s["maxmana"] = rng.randint(1, 120), rng.randint(1, 120)
    s["skillcalc"] = rng.randint(0, 60)
    s["row"] = {"aip": [rng.choice((0, rng.randint(0, 100))) for _ in range(5)],
                "skill1": rng.choice((-1, rng.randint(0, 400))), "skill2": rng.choice((-1, rng.randint(0, 400))),
                "mode1": rng.randint(0, 15), "mode2": rng.randint(0, 15),
                "velocity": rng.choice((0, 5, 6, 8, 10)), "run": rng.choice((0, 6, 9, 10, 20)),
                "x82": rng.choice((0, 1, 2, 10, 60, 200)), "x84": rng.choice((0, 1, 2, 10, 60, 300))}
    s["ctrl"] = [rng.choice((-1, 0, 1, rng.randint(0, 6000))) for _ in range(3)]
    s["param"] = {2: rng.random() < 0.6, 5: rng.randint(0, 40), 6: rng.random() < 0.4}
    s["follow"] = [rng.random() < 0.5 for _ in range(8)]
    s["decide"] = [rng.random() < 0.4 for _ in range(4)]
    s["teleport_ok"] = rng.random() < 0.6
    s["skillmode"] = rng.randint(0, 15)
    s["skilllvl"] = rng.choice((0, 1, rng.randint(1, 20)))
    s["corpse"] = rng.random() < 0.5
    s["corpse_id"] = 5
    s["radius"] = rng.randint(0, 40)
    s["ends"] = None if rng.random() < 0.1 else tuple((px + rng.randint(-20, 20), py + rng.randint(-20, 20)) for _ in range(2))
    ids = [0, rng.randint(1, 60), rng.randint(1, 60), rng.randint(1, 60)]
    s["ids"] = ids
    s["left"] = rng.choice(ids[1:] + [0, None])
    s["right"] = rng.choice(ids[1:] + [0, None])
    s["pet_attack"] = rng.random() < 0.7
    s["owner_class"] = rng.randint(0, 6)
    s["skill_class"] = {i: rng.choice((s["owner_class"], s["owner_class"], 7, rng.randint(0, 6))) for i in ids}
    s["aiok"] = {i: rng.random() < 0.7 for i in ids}
    s["mana"] = {i: rng.choice((0, 2, 5, 10, 30, 70)) for i in ids}
    s["kind"] = {i: rng.choice((0, 1, 1, 2)) for i in ids}
    s["smode"] = {i: rng.randint(0, 15) for i in ids}
    s["fails"] = rng.random()
    s["seed"] = (rng.getrandbits(32), rng.getrandbits(32))
    return s


CUR = {}


def install(e):
    cur = CUR
    def out(item): cur["out"].append(item); return 0
    def name(addr): return cur["names"].get(addr, hex(addr) if addr else None)
    def unit(n): return cur["units"][n] if n else 0
    e.hook(0x58f0d0, lambda e: unit("owner") if cur["s"]["owner"] else 0, 0)
    e.hook(0x552f60, lambda e: 0, 0)
    e.hook(0x620bb0, lambda e: cur["room"], 1)
    e.hook(0x61ab00, lambda e: int(cur["s"]["town"]), 1)
    e.hook(0x61b130, lambda e: 1, 3)
    e.hook(0x620510, lambda e: 1, 1)
    def search(e):
        s = cur["s"]
        if not s["foe"]: e.w32(e.arg(2), 0); return 0
        e.w32(e.arg(1), s["foe_dist"]); e.w32(e.arg(2), int(s["foe_melee"]))
        return unit("foe")
    e.hook(0x5dd7f0, search, 3)
    def search6(e):
        s = cur["s"]
        if not s["foe2"]: return 0
        e.w32(e.arg(0), s["foe2_dist"]); e.w32(e.arg(1), int(s["foe2_melee"]))
        return unit("foe2")
    e.hook(0x5ddc30, search6, 2)
    def regs(e): return e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EDX)
    e.hook(0x5dc640, lambda e: int(cur["s"]["clear"].get(name(regs(e)[1]), False)), 0)
    e.hook(0x5dc380, lambda e: cur["s"]["gap"][frozenset(map(name, regs(e)))], 0)
    e.hook(0x6416d0, lambda e: cur["s"]["dist"][frozenset((name(e.arg(0)), name(e.arg(1))))], 2)
    def decide(e):
        out(("decide", name(e.arg(0)), int(bool(e.arg(2))), e.arg(4), e.arg(5)))
        return int(cur["decide"].pop(0))
    e.hook(0x5e45d0, decide, 6)
    def follow(e):
        out(("follow", e.arg(1), e.arg(2), e.arg(3), e.arg(4)))
        return int(cur["follow"].pop(0))
    e.hook(0x5e3ea0, follow, 5)
    e.hook(0x5de080, lambda e: out(("stand", e.arg(0))), 1)
    e.hook(0x5de0f0, lambda e: out(("wait", e.arg(0))), 1)
    def skill(e): out(("skill", e.arg(0) & 0xff, e.arg(1), name(e.arg(2)), e.arg(3), e.arg(4))); return 1
    e.hook(0x5dead0, skill, 5)
    def seq(e): out(("seq", e.arg(0), name(e.arg(1)), e.arg(2), e.arg(3))); return 1
    e.hook(0x5de000, seq, 4)
    e.hook(0x5417d0, lambda e: out(("event", e.arg(0), e.arg(1) - cur["s"]["frame"])), 4)
    e.hook(0x540e60, lambda e: 0, 2)
    e.hook(0x57ccb0, lambda e: out(("die",)), 2)
    e.hook(0x6439f0, lambda e: 1, 0)
    e.hook(0x6442a0, lambda e: cur["s"]["skilllvl"], 3)
    e.hook(0x646ca0, lambda e: cur["s"]["skillcalc"], 4)
    e.hook(0x644360, lambda e: cur["s"]["smode"].get(e.arg(0) - 0x1000, 0) if e.arg(0) >= 0x1000 else cur["s"]["skillmode"], 1)
    e.hook(0x56e390, lambda e: unit("near") if cur["s"]["corpse"] else 0, 2)
    e.hook(0x4cc7c0, lambda e: cur["s"]["radius"], 0)
    e.hook(0x56ede0, lambda e: 0, 5)
    def ends(e):
        s = cur["s"]
        if not s["ends"]: return 0
        blk = e.alloc(0x40)
        (ax, ay), (bx, by) = s["ends"]
        for off, v in ((0xc, ax), (0x10, ay), (0x14, bx), (0x18, by)): e.w32(blk + off, v)
        return blk
    e.hook(0x58ee80, ends, 0)
    e.hook(0x554ea0, lambda e: out(("teleport",)) or int(cur["s"]["teleport_ok"]), 5)
    # The Shadows' skills: owner handles 0x10 (left) / 0x20 (right), the pet's 0x1000 + id.
    e.hook(0x620190, lambda e: 0x10 if cur["s"]["left"] is not None else 0, 1)       # the left skill (skill list +8)
    e.hook(0x6201d0, lambda e: 0x20 if cur["s"]["right"] is not None else 0, 1)      # the right (+0xc)
    def skill_id(e):
        h = e.arg(0)
        return cur["s"]["left"] if h == 0x10 else cur["s"]["right"] if h == 0x20 else h - 0x1000
    e.hook(0x643ce0, skill_id, 3)
    def add_skill(e):
        if e.arg(0) == cur["units"]["pet"]: cur["has"].add(e.arg(1))
        return 0
    e.hook(0x647280, add_skill, 6)
    def pet_skill(e):
        sid = e.mu.reg_read(UC_X86_REG_EDX)
        return 0x1000 + sid if sid in cur["has"] else 0
    e.hook(0x6439b0, pet_skill, 1)
    e.hook(0x645460, lambda e: cur["s"]["kind"].get(e.arg(1) - 0x1000, 0), 2)
    e.hook(0x645040, lambda e: cur["s"]["skill_class"].get(e.arg(0), 7), 1)
    e.hook(0x5eabf0, lambda e: int(cur["s"]["aiok"].get(e.arg(1), False)), 3)
    e.hook(0x6459f0, lambda e: cur["s"]["mana"].get(e.arg(0), 0), 2)
    e.hook(0x45c4b0, lambda e: cur["blank"], 0)
    e.hook(0x4efcb0, lambda e: cur["s"]["skillcalc"], 0)
    e.hook(0x5a61f0, lambda e: cur["ctx"].update(type=0, pct=0, steps=0), 0)
    e.hook(0x5dd230, lambda e: out(("unreachable",)), 1)
    e.hook(0x639df0, lambda e: int(cur["s"]["states"].get(e.arg(1), False)), 2)
    e.hook(0x639db0, lambda e: 0, 3)
    e.hook(0x5d2f80, lambda e: unit("near") if cur["s"]["near"] else 0, 2)
    e.hook(0x622c40, lambda e: int(cur["s"]["melee_of"].get(name(e.arg(1)), False)), 3)
    e.hook(0x5541b0, lambda e: int(cur["s"]["dead"].get(name(e.mu.reg_read(UC_X86_REG_ECX)), False)), 0)
    e.hook(0x625480, lambda e: cur["s"]["stats"].get(e.arg(1), 0), 3)
    e.hook(0x625d10, lambda e: cur["s"]["maxlife"], 1)
    e.hook(0x625d60, lambda e: cur["s"]["maxmana"], 1)
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
        mode, u, x, y = e.r32(m), e.r32(m + 8), e.r32(m + 0xc) & 0xffff, e.r32(m + 0x10) & 0xffff
        ok = cur["moves"].random() >= cur["s"]["fails"]
        if u: x = y = 0
        out(("move", x, y, mode, name(u), ctx["type"], ctx["pct"], ctx["steps"], int(ok)))
        ctx.update(type=0, pct=0, steps=0)
        return int(ok)
    e.hook(0x5a7c20, setoff, 3)


def run_game(e, s, addr, rng_moves):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    data = alloc(0x1000)                                                  # the excel tables: their counts
    e.w32(0x744304, data); e.w32(data + 0xba0, SKILLS); e.w32(data + 0xb98, alloc(0x23c * SKILLS)); e.w32(data + 0xb6c, 1000)
    game, params, ctrl, room, row, pdata = (alloc(n) for n in (0x2000, 0x40, 0x40, 0x100, 0x200, 0x100))
    units, names = {}, {}
    for k, n in enumerate(UNITS):
        u, path = alloc(0x200), alloc(0x100)
        x, y = s["pos"][n]
        e.w32(u, 0 if n == "owner" else 1); e.w32(u + 4, s["cls"] if n == "pet" else 0); e.w32(u + 0xc, k + 1)
        e.w32(u + 0x10, s["owner_mode"] if n == "owner" else 1); e.w32(u + 0x2c, path)
        e.w32(path, x << 16 | 0x8000); e.w32(path + 4, y << 16 | 0x8000); e.w32(path + 0x30, u); e.w32(path + 0x1c, room)
        units[n], names[u] = u, n
    pet = units["pet"]
    e.w32(pet + 0x14, pdata); e.w32(pdata + 0x28, ctrl); e.w32(pdata + 0x2c, alloc(0x40))
    e.w32(pet + 0x20, s["seed"][0]); e.w32(pet + 0x24, s["seed"][1])
    e.mu.mem_write(game + 0x6d, bytes([s["diff"]])); e.w32(game + 0xa8, s["frame"])
    r = s["row"]
    for k, v in enumerate(r["aip"]): e.mu.mem_write(row + 0x56 + 6 * k + 2 * s["diff"], struct.pack("<h", v))
    e.mu.mem_write(row + 0x32, struct.pack("<hh", r["velocity"], r["run"]))
    e.mu.mem_write(row + 0x170, struct.pack("<hh", r["skill1"], r["skill2"]))
    e.mu.mem_write(row + 0x180, bytes([r["mode1"], r["mode2"]]))
    for k, v in enumerate(s["ctrl"]): e.w32(ctrl + 0x14 + 4 * k, v & MASK)
    e.w32(params, ctrl); e.w32(params + 7 * 4, row)
    if s["param"][2]: e.w32(params + 8, units["foe2"])
    e.w32(params + 20, s["param"][5]); e.w32(params + 24, int(s["param"][6]))
    e.w32(units["owner"] + 4, s["owner_class"])
    e.mu.mem_write(row + 0x82, struct.pack("<hh", r["x82"], r["x84"]))
    CUR.update(blank=alloc(0x200), has={0} if s["pet_attack"] else set(), s=s, out=[], ctx={"type": 0, "pct": 0, "steps": 0}, units=units, names=names, room=room, moves=rng_moves,
               follow=list(s["follow"]), decide=list(s["decide"]))
    e.call(addr, params, ecx=game, edx=pet)
    return CUR["out"], [e.r32(ctrl + 0x14 + 4 * k) for k in range(3)], (e.r32(pet + 0x20), e.r32(pet + 0x24))


NAMES = {None: 0, "pet": 1, "owner": 2, "foe": 3, "foe2": 4, "near": 5}
ORDER = list(AIS)


def dump_case(ai, s, out, ctrl, seed):
    """A tests/pet_ais_cases.inc line: the scene, the calls the think makes (moves,
    follows, decides, teleports, with their answers), what it comes to, the AI
    control and seed after."""
    follows, decides = list(s["follow"]), list(s["decide"])
    calls, act = [], ["failed", 0, 0, 0, -1, 0, 0, 0]               # kind frames unreachable unit skill mode x y
    for item in out:
        k = item[0]
        if k == "move":
            _, x, y, mode, unit, typ, pct, steps, ok = item
            if mode == 0: act = ["die", 0, act[2], 0, -1, 0, 0, 0]; continue      # FUN_005ddfc0(0): no path asked
            calls.append(f"m {x} {y} {mode} {NAMES[unit]} {typ} {pct - (1 << 32) if pct >= 1 << 31 else pct} {steps} {ok}")
            if ok: act = ["swing" if mode == 4 else "chase" if unit else "moved", 0, act[2], NAMES[unit], -1, 0, 0, 0]
        elif k == "follow":
            ok = int(follows.pop(0)); calls.append(f"f {item[1]} {item[2]} {item[3] - (1 << 32) if item[3] >= 1 << 31 else item[3]} {item[4]} {ok}")
            if ok: act = ["followed", 0, act[2], 0, -1, 0, 0, 0]
        elif k == "decide":
            ok = int(decides.pop(0)); calls.append(f"d {NAMES[item[1]]} {item[2]} {item[3]} {item[4]} {ok}")
            if ok: act = ["followed", 0, act[2], 0, -1, 0, 0, 0]
        elif k == "teleport": calls.append(f"t {int(s['teleport_ok'])}")
        elif k == "stand": act = ["stand", item[1], act[2], 0, -1, 0, 0, 0]
        elif k == "wait": act[1] = item[1]
        elif k == "skill": act = ["skill", 0, act[2], NAMES[item[3]], item[2] - (1 << 32) if item[2] >= 1 << 31 else item[2], item[1], item[4], item[5]]
        elif k == "seq": act = ["seq", 0, act[2], NAMES[item[2]], item[1] - (1 << 32) if item[1] >= 1 << 31 else item[1], 0xe, 0, 0]
        elif k == "die": act = ["die", 0, act[2], 0, -1, 0, 0, 0]
        elif k == "unreachable": act[2] = 1
    sx = lambda v: v - (1 << 32) if v >= 1 << 31 else v
    u = list(NAMES)[1:]
    r = s["row"]
    nums = [ORDER.index(ai), s["cls"], int(s["owner"]), s["owner_id"], s["owner_mode"], int(s["town"]), s["frame"]]
    nums += [c for n in u for c in s["pos"][n]]
    nums += [0 if a == b else s["gap"][frozenset((a, b))] for a in u for b in u]
    nums += [0 if a == b else s["dist"][frozenset((a, b))] for a in u for b in u]
    nums += [int(s["foe"]), int(s["foe_melee"]), int(s["foe2"]), int(s["foe2_melee"]), int(s["near"]), s["foe_dist"], s["foe2_dist"]]
    nums += [int(s["param"][2]), int(s["param"][6]), s["param"][5]]
    nums += [int(s["clear"][n]) for n in u] + [int(s["melee_of"][n]) for n in u] + [int(s["dead"][n]) for n in u]
    nums += [int(s["states"][2]), int(s["states"][0x3c]), int(s["states"][0x8a]), s["stats"][0x2d], s["stats"][6], s["stats"][8], s["maxlife"], s["maxmana"]]
    nums += [s["skillcalc"], s["skilllvl"], s["skillmode"], s["corpse_id"], s["radius"]]
    nums += r["aip"] + [r["skill1"], r["skill2"], r["mode1"], r["mode2"], r["velocity"], r["run"]]
    nums += [int(s["corpse"])]
    nums += [int(bool(s["ends"]))] + ([c for pt in s["ends"] for c in pt] if s["ends"] else [0, 0, 0, 0])
    none = lambda v: -1 if v is None else v
    nums += [none(s["left"]), none(s["right"]), s["owner_class"], int(s["pet_attack"]), r["x82"], r["x84"]]
    nums += [c for i in s["ids"] for c in (i, s["skill_class"][i], int(s["aiok"][i]), s["mana"][i], s["kind"][i], s["smode"][i])]
    nums += [sx(v & MASK) for v in s["ctrl"]] + list(s["seed"])
    tail = act[:1] + [str(v) for v in act[1:]] + [str(sx(v)) for v in ctrl] + [str(v) for v in seed]
    return " ".join(map(str, nums)) + " | " + " ; ".join(calls) + " | " + " ".join(map(str, tail))


def check(e, ai, cases, rng, dumped=None):
    addr, port, cls = AIS[ai]
    mark, bad, kinds = e.brk, 0, {}
    for case in range(cases):
        e.brk = mark
        s = setup(rng, cls)
        move_seed = rng.getrandbits(32)
        got = run_game(e, s, addr, random.Random(move_seed))
        p = Port(s, random.Random(move_seed))
        port(p)
        want = (p.out, [v & MASK for v in p.ctrl], (p.seed.lo, p.seed.hi))
        kind = got[0][-1][0] if got[0] else "-"
        kinds[kind] = kinds.get(kind, 0) + 1
        if dumped is not None and got == want and len(dumped.setdefault(ai, {})) < 16:
            key = (tuple(x[0] for x in p.out), tuple(x[3] for x in p.out if x[0] == "move"))
            if key not in dumped[ai]: dumped[ai][key] = dump_case(ai, s, p.out, want[1], want[2])
        if got != want:
            bad += 1
            if bad <= 5:
                print(f"{ai} case {case}:\n  game {got}\n  port {want}\n  {dict((k, v) for k, v in s.items() if k not in ('gap', 'dist', 'pos'))}")
    print(f"{ai} ok: {cases} cases, {kinds}" if not bad else f"{ai}: {bad} of {cases} differ")
    return bad


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.endswith(".inc")]
    which = args[0] if args else "all"
    cases = int(args[1]) if len(args) > 1 else 2000
    rng = random.Random(int(args[2]) if len(args) > 2 else 1)
    e = emu.Emu()
    install(e)
    dumped = {} if "--dump" in sys.argv else None
    bad = sum(check(e, ai, cases, rng, dumped) for ai in (AIS if which == "all" else [which]))
    if dumped is not None:
        with open(sys.argv[sys.argv.index("--dump") + 1], "w") as f:
            f.write("// tools/emu/pet_ais.py --dump: game.exe's own pet thinks (see the script).\nR\"(\n")
            for rows in dumped.values(): f.write("\n".join(rows.values()) + "\n")
            f.write(")\"\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
