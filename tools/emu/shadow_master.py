# SPDX-License-Identifier: GPL-3.0-or-later
"""The Shadow Master's think (FUN_005eb970, docs/research/re/pet-ai.md) against a port.

    uv run python shadow_master.py [cases] [seed]

The think runs natively in game.exe on random setups, with its cast
(FUN_005eb8b0), its state-group test (FUN_005eb7f0), the walk away
(FUN_005df140) and the distances (FUN_005b0bd0) native too; the skill,
state and missile tables from the setup. A pool of units answers what it
asks: type, spot, owner (FUN_0058f0d0), target (FUN_00553540), dying
(FUN_005541b0), foe (FUN_00554200), in melee (FUN_00622c40), states
(FUN_00639df0), resists (FUN_00625480), FUN_005eb650; the pet's skills
(a list at +0xa8: id, level, kind, mode), its life % (FUN_00621f20), its
right skill (FUN_00620190), FUN_0063a2b0, the line to its target
(FUN_00622aa0), its aura stat (FUN_006256b0 / FUN_00625d00) and the unit
scan's results (FUN_005dd0b0 with FUN_005eb6d0). Stands, casts, runs,
walks, follow-or-fight (FUN_005e45d0, answered from the setup) and left
skills set are logged. The port must give the same log, AI control and
seed. Prints `ok`.
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu
from merc import Seed

SKILLS = 300
STATES = 40
MISSILES = 50
MASK = 0xffffffff
NAMES = ("pet", "owner", "u2", "u3", "u4", "u5", "u6", "u7")
ELEM_STAT = {0: 0x24, 1: 0x27, 2: 0x29, 3: 0x25, 4: 0x2b, 5: 0x2d, 12: 0x2b}   # EType -> its resist


def setup(rng):
    pick = lambda: rng.choice(NAMES[1:] + (None, None))
    base = (rng.randint(4900, 5100), rng.randint(4900, 5100))
    units = {}
    for n in NAMES:
        units[n] = {"type": 0 if n == "owner" else rng.choice((1, 1, 1, 0, 3)),
                    "pos": (base[0] + rng.randint(-40, 40), base[1] + rng.randint(-40, 40)),
                    "targetable": rng.random() < 0.85, "dying": rng.random() < 0.15, "foe": rng.random() < 0.7,
                    "melee": rng.random() < 0.4, "states": {s for s in range(1, STATES) if rng.random() < 0.08},
                    "res": {s: rng.choice((0, 0, 25, 75, -50, 100)) for s in set(ELEM_STAT.values())},
                    "target": pick(), "owner": pick() if rng.random() < 0.3 else None, "valid": rng.random() < 0.6,
                    "mlevel": rng.choice((0, 10, 24, 25, 60))}
    units["pet"]["type"] = 1
    units["pet"]["owner"] = "owner" if rng.random() < 0.9 else None
    for n in NAMES:
        if units[n]["pos"] == units["pet"]["pos"] and n != "pet": units[n]["pos"] = (units[n]["pos"][0] + 1, units[n]["pos"][1])
    groups = [rng.choice((0, 0, 1, 2, 3)) for _ in range(STATES)]
    skills = []
    for _ in range(rng.choice((0, 1, 4, 8, 12))):
        sid = rng.choice((rng.randint(1, SKILLS - 1), rng.randint(-2, SKILLS + 2)))
        skills.append({"id": sid, "level": rng.randint(0, 30), "kind": rng.choice((0, 1, 1, 2)), "mode": rng.randint(0, 15)})
    rows = {}
    for sk in skills + [{"id": 0}]:
        rows[sk["id"]] = {"aitype": rng.choice(list(range(0, 15)) + [4, 5, 12, 13, 1, 7, 8]), "bonus": rng.randint(-20, 40),
                          "rank": rng.randint(-10, 60), "elem": rng.choice((0, 1, 2, 3, 4, 5, 6, 9, 12)),
                          "state": rng.choice((0, 0, -1, rng.randint(1, STATES - 1))), "state2": rng.choice((0, 0, rng.randint(1, STATES - 1))),
                          "flags": rng.choice((0, 4, 0xff)), "srvmissile": rng.choice((-1, -1, 3)), "missile": rng.choice((-1, rng.randint(0, MISSILES - 1), MISSILES + 1)),
                          "stat": rng.randint(0, 300), "dofunc": rng.choice((0x13, 0x13, 0, 5))}
    scan = [rng.choice(NAMES[2:]) if rng.random() < 0.5 else None for _ in range(3)]
    return {"units": units, "skills": skills, "rows": rows, "groups": groups,
            "ranges": [rng.choice((0, 1, 3, 6, 30)) for _ in range(MISSILES)],
            "has_list": rng.random() < 0.95, "driver": pick() if rng.random() < 0.9 else None,
            "driver_melee": rng.random() < 0.4, "driver_dist": rng.randint(0, 50),
            "p": {o: rng.choice((0, rng.randint(1, 40), rng.randint(20, 60), -3)) for o in (0x56, 0x58, 0x5a, 0x5c, 0x5e, 0x60)},
            "aip3": rng.choice((0, 30, 60, 100)), "diff": rng.randint(0, 2),
            "ctrl": [rng.choice((0, 0, 1, 3, -1)), rng.choice((0, rng.randint(1, SKILLS - 1))), rng.choice((-1, 0, 1, 2, 5, 40))],
            "scan": {"near": scan[0], "near_d": rng.choice((0, 10, 30, 100, 600)), "count": rng.choice((0, 2, 3, 4, 8, 16)),
                     "onear": scan[1], "ocount": rng.choice((0, 2, 4, 6)), "all": rng.choice((0, 1, 3, 8)),
                     "kin": rng.choice((0, 2, 6, 10)), "help": scan[2]},
            "life": rng.choice((10, 40, 47, 60, 70, 100)), "right": rng.random() < 0.5, "low": rng.random() < 0.5,
            "line": rng.random() < 0.6, "aura": rng.choice((None, 0, 1, 2, 3, 5)), "town": rng.random() < 0.1,
            "decide": [rng.random() < 0.1 for _ in range(4)], "casts": [rng.random() < 0.6 for _ in range(40)],
            "moves": [rng.random() < 0.6 for _ in range(4)],
            "seed": (rng.getrandbits(32), rng.getrandbits(32))}


# --- the port ----------------------------------------------------------------

class Port:
    def __init__(self, s):
        self.s, self.u, self.out = s, s["units"], []
        self.seed = Seed(*s["seed"])
        self.ctrl = list(s["ctrl"])
        self.decides, self.casts, self.moves = list(s["decide"]), list(s["casts"]), list(s["moves"])

    def d2(self, a, b):                                                   # FUN_005b0bd0
        (ax, ay), (bx, by) = self.u[a]["pos"], self.u[b]["pos"]
        return (ax - bx) ** 2 + (ay - by) ** 2
    def has(self, unit, state): return unit is not None and state in self.u[unit]["states"]
    def row(self, sid): return self.s["rows"][sid] if 0 <= sid < SKILLS else None
    def skill_of(self, sid): return next((k for k in self.s["skills"] if k["id"] == sid), None)
    def group_mate(self, unit, state):                                     # FUN_005eb7f0: another state of its group on
        if not 0 <= state < STATES or self.s["groups"][state] == 0: return False
        return any(self.s["groups"][o] == self.s["groups"][state] and o in self.u[unit]["states"] for o in range(STATES) if o != state)
    def decide(self, foe, melee):
        self.out.append(("decide", foe, int(bool(melee))))
        return self.decides.pop(0)
    def stand(self, n): self.out.append(("stand", n))
    def cast_raw(self, sid, target, x=0, y=0):                            # FUN_005dead0 with the skill's mode
        self.out.append(("skill", self.skill_of(sid)["mode"], sid, target, x, y))
        return self.casts.pop(0)
    def approach(self, target):                                           # FUN_005ded00(target, 4)
        self.out.append(("approach", target))
        return self.moves.pop(0)

    def cast(self, sid, melee, target, x=0, y=0):                         # FUN_005eb8b0
        if target is not None and target in ("pet", self.u["pet"]["owner"]): return 0
        sk = self.skill_of(sid)
        if sk is None: return 0
        if target is not None:
            if not self.u[target]["targetable"] or self.s["town"]: return 0
        if sk["kind"] == 1 and not melee: return self.approach(target)
        return self.cast_raw(sid, target, x, y)

    def think(self):
        s, u, p = self.s, self.u, self.s["p"]
        if not s["has_list"]: self.stand(100); return
        owner = u["pet"]["owner"]
        melee = s["driver_melee"]
        target = s["driver"]
        if owner and self.d2("pet", owner) > p[0x60] * p[0x60] and self.decide(None, melee): return
        if self.ctrl[0] > 0:
            mine = u["pet"]["target"]
            if mine: target = mine
            if target is None: self.ctrl[0] = self.ctrl[1] = 0
            else:
                self.ctrl[0] -= 1
                if self.cast(self.ctrl[1], melee, target): return
        if p[0x5e] < s["driver_dist"]: target = None
        if target is None:
            for sk in s["skills"]:
                row = self.row(sk["id"])
                if row is None: continue
                if row["aitype"] == 1:
                    st = row["state"]
                    if st <= 0 or self.has("pet", st): continue
                    if self.group_mate("pet", st) and self.seed.rand(100) >= 4: continue
                    if self.seed.step() % 100 >= 0x3c: continue
                    if sk["kind"] == 1 and not melee: ok = self.approach(None)
                    else: ok = self.cast_raw(sk["id"], None)
                    if ok: return
                elif row["aitype"] == 6 and not s["right"]:
                    if self.seed.step() % 100 < 0x14: self.out.append(("left", sk["id"]))
        helper = None
        if owner:
            theirs = u[owner]["target"]
            if theirs and not u[theirs]["dying"] and u[theirs]["foe"]:
                target = helper = theirs
            if self.d2("pet", owner) <= 0x90 and self.decide(target, melee): return
        if target is None: self.stand(0x19); return
        chance = max(5, min(s["aip3"] - 2 * max(self.ctrl[2], 1), 100))
        if melee and self.seed.rand(100) < chance and self.cast(0, melee, target): return
        sc = s["scan"]
        if not melee:
            pick = helper or sc["onear"] or (sc["help"] if sc["help"] and self.d2("pet", sc["help"]) < 0x400 else None)
            if pick: target = pick
            if not u[target]["valid"]:
                theirs = u[target]["owner"]
                if theirs and not u[theirs]["dying"] and self.d2("pet", theirs) < 0x400: target = theirs
        life, d2 = s["life"], self.d2("pet", target)
        low = p[0x5a] > 0 and s["low"]
        clear = not s["line"]
        if sc["count"] > 3 and self.seed.rand(0x20) < 2 * sc["count"]:
            if owner and self.d2("pet", owner) > 0x24:
                self.out.append(("run", owner)); return
            (px, py), (tx, ty) = u["pet"]["pos"], u[target]["pos"]
            sx, sy = (-1 if px < tx else 1 if tx < px else 0), (-1 if py < ty else 1 if ty < py else 0)
            self.out.append(("away", px + 8 * sx, py + 8 * sy, 4))
            if self.moves.pop(0): return
        aura = 0
        entries = [(target, 0, 0)]
        for sk in s["skills"]:
            sid, lvl = sk["id"], sk["level"]
            row = self.row(sid)
            if row is None: continue
            who = target
            res = u[target]["res"].get(ELEM_STAT[row["elem"]], 0) if row["elem"] in ELEM_STAT else 0
            score = row["bonus"] + int(row["rank"] / 4) + lvl + int(-res / 10)
            st, kind, got = row["state"], row["aitype"], 0
            def roll(): return self.seed.rand(p[0x5c])
            def aura_full():
                nonlocal aura
                if row["flags"] & 4 and st > 0 and self.has("pet", st) and s["aura"] is not None:
                    v = s["aura"]
                    aura += v
                    return v >= 3
                return False
            if kind == 1:
                if st > 0 and not self.has("pet", st): continue
                if sc["near_d"] <= 0x19: score -= 6
                score += -10 if self.group_mate("pet", st) else 10
                got, who = roll() + score, "pet"
            elif kind == 2:
                if st > 0 and self.has("pet", st): continue
                if row["state2"] > 0 and self.has(target, row["state2"]): continue
                if sc["near_d"] <= 0x19: score -= 10
                got = roll() + score
            elif kind == 3:
                if sc["kin"] > 5: score -= 2 * sc["kin"]
                if sc["near_d"] <= 0x19: score -= 7
                if sc["all"] < 3: score -= 10
                got = sc["all"] * 3 - 9 + roll() + score
            elif kind in (4, 12):
                if kind == 12 and u[target]["type"] == 1 and u[target]["mlevel"] < 0x19: continue
                if d2 > p[0x56] * p[0x56]: score -= 10
                score += p[0x58]
                if melee or d2 <= 0x19: score += 10
                if kind == 4:
                    if row["flags"] & 4:
                        if aura_full(): continue
                        score += p[0x5a]
                    elif p[0x5a] > 0 and not low: score -= 10
                    else: score += aura * 4 + 3
                    got = roll() + score
                else:
                    if aura_full(): continue
                    got = roll() + score
                    if life < 0x4b: got += 8
                    if life < 0x32: got += 0xc
            elif kind in (5, 11):
                if not clear: continue
                if row["srvmissile"] < 0 and row["missile"] >= 0 and 0 <= row["missile"] < MISSILES and d2 >= (s["ranges"][row["missile"]] - 1) ** 2: continue
                if sc["near_d"] <= 0x19: score -= 5
                if d2 <= 0x19: score -= 5
                if low: score -= 5
                got = roll() + score + (sc["all"] * 3 if kind == 11 else 0)
            elif kind == 6:
                r = self.seed.rand(100)
                if r < (6 if s["right"] else 0x14): self.out.append(("left", sid))
                continue
            elif kind == 7:
                r = roll()
                if life > 0x42: continue
                who = None
                if not owner: self.seed.rand(0x28); self.seed.rand(0x28)
                got = r + score + (0x14 if life < 0x2d else 10)
            elif kind == 8:
                r = roll()
                if life > 0x42: continue
                who = "pet"
                got = (r + score) * (4 if life < 0x2d else 2)
            elif kind == 13:
                score += p[0x58]
                score += -5 if p[0x5a] > 0 and not low else aura
                if (life < 0x32 or sc["count"] > 3) and sc["onear"] and sc["ocount"] < 4 and self.d2("pet", sc["onear"]) > 0x19:
                    score += 0x14; who = sc["onear"]
                else:
                    if d2 < 0x19: continue
                    if d2 > 0x144: score += 10
                got = roll() + score
            else:
                continue
            COVER[kind] = COVER.get(kind, 0) + 1
            if got > entries[-1][2]: entries.append((who, sid, got))
        for who, sid, _ in reversed(entries):
            if self.seed.step() & 3 == 0: continue
            if self.cast(sid, u["pet"]["melee"] if who is None else u[who]["melee"], who):
                row = self.row(sid)
                if row and row["dofunc"] == 0x13: self.ctrl[1], self.ctrl[0] = sid, 0x19
                return
        if target is not None and self.cast(0, u[target]["melee"], target): return
        self.stand(0xf)


# --- game.exe ----------------------------------------------------------------

CUR = {}
COVER = {}


def install(e):
    cur = CUR
    reg = lambda r: e.mu.reg_read(r)
    out = lambda item: cur["log"].append(item)
    name = lambda a: cur["names"].get(a)
    unit = lambda n: cur["ptrs"][n] if n else 0
    U = lambda a: cur["s"]["units"][name(a)]
    e.hook(0x58f0d0, lambda e: unit(U(reg(UC_X86_REG_ECX))["owner"]), 0)
    e.hook(0x540e60, lambda e: 0, 2)
    e.hook(0x5e45d0, lambda e: out(("decide", name(e.arg(0)), int(bool(e.arg(2))))) or int(cur["decide"].pop(0)), 6)
    e.hook(0x553540, lambda e: unit(U(e.arg(1))["target"]), 2)
    e.hook(0x5541b0, lambda e: int(U(reg(UC_X86_REG_ECX))["dying"]), 0)
    e.hook(0x554200, lambda e: int(U(e.arg(0))["foe"]), 1)
    e.hook(0x622c40, lambda e: int(U(e.arg(1))["melee"]) if e.arg(1) else int(U(e.arg(0))["melee"]), 3)
    e.hook(0x639df0, lambda e: int(e.arg(0) != 0 and e.arg(1) in U(e.arg(0))["states"]), 2)
    e.hook(0x625480, lambda e: U(e.arg(0))["res"].get(e.arg(1), 0) & MASK if e.arg(0) else 0, 3)
    e.hook(0x5eb650, lambda e: int(U(reg(UC_X86_REG_EDX))["valid"]), 0)
    def first(e):
        node = cur["nodes"][0] if cur["nodes"] else 0
        return node
    e.hook(0x643910, lambda e: first(e), 1)
    e.hook(0x6438f0, lambda e: cur["next"].get(e.arg(0), 0), 1)
    e.hook(0x643ce0, lambda e: cur["node_skill"][e.arg(0)]["id"] & MASK, 3)
    e.hook(0x6442a0, lambda e: cur["node_skill"][e.arg(1)]["level"], 3)
    def handle(e):
        assert reg(UC_X86_REG_ECX) == cur["ptrs"]["pet"]
        sid = reg(UC_X86_REG_EDX)
        return next((n for n in cur["nodes"] if cur["node_skill"][n]["id"] & MASK == sid), 0)
    e.hook(0x6439b0, handle, 1)
    e.hook(0x645460, lambda e: cur["node_skill"][e.arg(1)]["kind"], 2)
    e.hook(0x644360, lambda e: cur["node_skill"][e.arg(0)]["mode"], 1)
    e.hook(0x5dead0, lambda e: out(("skill", e.arg(0), e.arg(1), name(e.arg(2)), e.arg(3), e.arg(4))) or int(cur["casts"].pop(0)), 5)
    e.hook(0x5ded00, lambda e: out(("approach", name(e.arg(0)))) or int(cur["moves"].pop(0)), 2)
    e.hook(0x5ded20, lambda e: out(("run", name(e.arg(0)))) or 1, 1)
    e.hook(0x5deb60, lambda e: out(("away", e.arg(2), e.arg(3), e.arg(5))) or int(cur["moves"].pop(0)), 6)
    e.hook(0x5a6260, lambda e: 0, 2)
    e.hook(0x5de080, lambda e: out(("stand", e.arg(0))), 1)
    e.hook(0x620190, lambda e: int(cur["s"]["right"]), 1)
    e.hook(0x643bc0, lambda e: out(("left", e.arg(1))) or 0, 3)
    e.hook(0x620bb0, lambda e: 0x77, 1)
    e.hook(0x61ab00, lambda e: int(cur["s"]["town"]), 1)
    e.hook(0x621f20, lambda e: cur["s"]["life"], 1)
    e.hook(0x63a2b0, lambda e: int(cur["s"]["low"]), 1)
    e.hook(0x622aa0, lambda e: int(cur["s"]["line"]), 3)
    e.hook(0x6256b0, lambda e: 0 if cur["s"]["aura"] is None else 0x99, 2)
    e.hook(0x625d00, lambda e: cur["s"]["aura"], 3)
    def scan(e):
        st, sc = e.arg(0), cur["s"]["scan"]
        assert e.r32(st) == unit(cur["s"]["units"]["pet"]["owner"])
        vals = {1: unit(sc["near"]), 2: sc["near_d"], 3: sc["count"], 4: unit(sc["onear"]), 6: sc["ocount"], 7: sc["all"], 8: sc["kin"], 9: unit(sc["help"])}
        for k, v in vals.items(): e.w32(st + 4 * k, v)
        return 0
    e.hook(0x5dd0b0, scan, 3)


def run_game(e, s):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    data = alloc(0x1000)
    skills, states, missiles = alloc(0x23c * SKILLS), alloc(0x3c * STATES), alloc(0x1a4 * MISSILES)
    e.w32(0x744304, data)
    e.w32(data + 0xba0, SKILLS); e.w32(data + 0xb98, skills)
    e.w32(data + 0xc4, STATES); e.w32(data + 0xbc, states)
    e.w32(data + 0xb6c, MISSILES); e.w32(data + 0xb64, missiles)
    for k, g in enumerate(s["groups"]): e.mu.mem_write(states + 0x3c * k + 0x1e, struct.pack("<h", g))
    for k, r in enumerate(s["ranges"]): e.mu.mem_write(missiles + 0x1a4 * k + 0x96, struct.pack("<h", r))
    for sid, r in s["rows"].items():
        if not 0 <= sid < SKILLS: continue
        a = skills + 0x23c * sid
        e.mu.mem_write(a + 4, bytes([r["flags"]]))
        e.mu.mem_write(a + 0x2e, struct.pack("<h", r["dofunc"]))
        e.mu.mem_write(a + 0x46, struct.pack("<hh", r["srvmissile"], r["missile"]))
        e.mu.mem_write(a + 0x54, struct.pack("<h", r["stat"]))
        e.mu.mem_write(a + 0x80, struct.pack("<hh", r["state"], r["state2"]))
        e.mu.mem_write(a + 0x174, struct.pack("<h", r["rank"]))
        e.mu.mem_write(a + 0x1dc, bytes([r["elem"]]))
        e.mu.mem_write(a + 0x230, bytes([r["aitype"]]) + b"\0" + struct.pack("<h", r["bonus"]))
    game, params, ctrl, row = (alloc(n) for n in (0x200, 0x40, 0x40, 0x200))
    ptrs, names = {}, {}
    for n in NAMES:
        u, path, mdata, mrow = alloc(0x200), alloc(0x40), alloc(0x10), alloc(0x100)
        info = s["units"][n]
        e.w32(u, info["type"]); e.w32(u + 0x2c, path); e.w32(u + 0x14, mdata); e.w32(mdata, mrow)
        e.w32(u + 0xc4, 4 if info["targetable"] else 0)
        x, y = info["pos"]
        e.mu.mem_write(path, struct.pack("<HHHH", 0, x, 0, y))
        e.mu.mem_write(mrow + 0xa0 + s["diff"], bytes([info["mlevel"]]))
        ptrs[n], names[u] = u, n
    pet = ptrs["pet"]
    nodes = [alloc(0x10) for _ in s["skills"]]
    e.w32(pet + 0xa8, alloc(0x10) if s["has_list"] else 0)
    e.w32(pet + 0x20, s["seed"][0]); e.w32(pet + 0x24, s["seed"][1])
    e.mu.mem_write(game + 0x6d, bytes([s["diff"]]))
    for off, v in s["p"].items(): e.mu.mem_write(row + off, struct.pack("<h", v))
    e.mu.mem_write(row + 0x62 + 2 * s["diff"], struct.pack("<h", s["aip3"]))
    for k, v in enumerate(s["ctrl"]): e.w32(ctrl + 0x14 + 4 * k, v)
    e.w32(params, ctrl); e.w32(params + 7 * 4, row)
    e.w32(params + 8, ptrs[s["driver"]] if s["driver"] else 0)
    e.w32(params + 20, s["driver_dist"]); e.w32(params + 24, int(s["driver_melee"]))
    CUR.update(s=s, log=[], ptrs=ptrs, names=names, nodes=nodes,
               next={a: b for a, b in zip(nodes, nodes[1:])}, node_skill=dict(zip(nodes, s["skills"])),
               decide=list(s["decide"]), casts=list(s["casts"]), moves=list(s["moves"]))
    e.call(0x5eb970, params, ecx=game, edx=pet)
    return CUR["log"], [e.r32(ctrl + 0x14 + 4 * k) for k in range(3)], (e.r32(pet + 0x20), e.r32(pet + 0x24))


# --- the unit scan's callback (FUN_005eb6d0) and FUN_005eb650 ---------------

MONSTATS = 0x1b0


def scan_setup(rng):
    units = []
    for k in range(rng.randint(0, 10)):
        units.append({"type": rng.choice((1, 1, 1, 0, 3)), "dying": rng.random() < 0.1, "side": rng.random() < 0.3,
                      "mid": rng.choice((0x19a, 0x19b, 0x19c, 0x19d, 0x19e, 0x19f, 0x1a0, 0x1a5, rng.randint(0, MONSTATS - 1))),
                      "targetable": rng.random() < 0.85, "foe": rng.random() < 0.8,
                      "pos": (5000 + rng.randint(-35, 35), 5000 + rng.randint(-35, 35)),
                      "f0c": rng.choice((0, 0x40, 0x80, 0xc0)), "f0d": rng.choice((0x80, 0x80, 0x81, 0, 1)), "f16": rng.choice((0, 0, 2, 4, 8, 1, 0x10))})
    units.append(None)                                                    # the pet itself
    rng.shuffle(units)
    return {"units": units, "owner": rng.random() < 0.8, "pet_pos": (5000, 5000), "owner_pos": (5000 + rng.randint(-20, 20), 5000 + rng.randint(-20, 20))}


def valid(u):                                                             # FUN_005eb650
    if u["type"] != 1 or u["dying"]: return False
    if not 0 <= u["mid"] < MONSTATS or u["f0d"] & 1 or not u["f0d"] & 0x80: return False
    return bool(u["f0c"] & 0x40 or u["f0c"] & 0x80 or u["f16"] & 0xe)


def scan_port(s):
    """FUN_005eb6d0 for each unit: [0] the owner, [1] / [2] the closest foe (by
    squared distance) within 32 of the pet, [3] how many within 10, [4] / [5]
    the closest to the owner, [6] how many within 10 of it, [7] how many
    within 32 of the pet, [8] the pet's side's traps (MonStats 0x19a..0x1a0
    but 0x19e), [9] the last foe within 32 worth chasing (FUN_005eb650)."""
    big = 0x7fffffff
    st = ["owner" if s["owner"] else 0, 0, big, 0, 0, big, 0, 0, 0, 0]
    d2 = lambda a, b: (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2
    for k, u in enumerate(s["units"]):
        if u is None or u["dying"]: continue
        if u["type"] == 1 and u["side"] and u["mid"] in (0x19a, 0x19b, 0x19c, 0x19d, 0x19f, 0x1a0):
            st[8] += 1; continue
        if not u["targetable"] or not u["foe"]: continue
        if s["owner"]:
            d = d2(s["owner_pos"], u["pos"])
            if d <= 0x64: st[6] += 1
            if d < st[5]: st[4], st[5] = k, d
        d = d2(s["pet_pos"], u["pos"])
        if d > 0x400: continue
        st[7] += 1
        if d <= 0x64: st[3] += 1
        if d < st[2]: st[1], st[2] = k, d
        if valid(u): st[9] = k
    return st


def scan_check(e, cases, rng):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    cur = {}
    e.hook(0x5541b0, lambda e: int(cur["u"][e.mu.reg_read(UC_X86_REG_ECX)]["dying"]), 0)
    e.hook(0x650d70, lambda e: int(cur["u"][e.arg(1)]["side"]), 2)
    e.hook(0x463860, lambda e: cur["u"][e.mu.reg_read(UC_X86_REG_ECX)]["mid"], 0)
    e.hook(0x554200, lambda e: int(cur["u"][e.arg(0)]["foe"]), 1)
    mark, bad = e.brk, 0
    for case in range(cases):
        e.brk = mark
        s = scan_setup(rng)
        data, mons = alloc(0x1000), alloc(0x1a8 * MONSTATS)
        e.w32(0x744304, data); e.w32(data + 0xa80, MONSTATS); e.w32(data + 0xa78, mons)
        def unit(pos, typ=1):
            u, path = alloc(0x200), alloc(0x40)
            e.w32(u, typ); e.w32(u + 0x2c, path)
            e.mu.mem_write(path, struct.pack("<HHHH", 0, pos[0], 0, pos[1]))
            return u
        pet, owner = unit(s["pet_pos"]), unit(s["owner_pos"], 0)
        ptrs, cur["u"] = [], {}
        for k, info in enumerate(s["units"]):
            if info is None: ptrs.append(pet); continue
            u = unit(info["pos"], info["type"])
            e.w32(u + 4, k + 1)                                           # its MonStats row (flags below)
            e.mu.mem_write(mons + 0x1a8 * (k + 1) + 0xc, bytes([info["f0c"], info["f0d"]]))
            if not 0 <= info["mid"] < MONSTATS: e.w32(u + 4, MONSTATS + 3)
            mdata = alloc(0x40); e.w32(u + 0x14, mdata); e.mu.mem_write(mdata + 0x16, struct.pack("<H", info["f16"]))
            e.w32(u + 0xc4, 4 if info["targetable"] else 0)
            cur["u"][u] = info
            ptrs.append(u)
        cur["u"][pet] = {"dying": False}
        st = alloc(0x28)
        e.w32(st, owner if s["owner"] else 0); e.w32(st + 8, 0x7fffffff); e.w32(st + 0x14, 0x7fffffff)
        for u in ptrs: e.call(0x5eb6d0, u, st, ecx=0x1234, edx=pet)
        got = [e.r32(st + 4 * k) for k in range(10)]
        index = {u: k for k, u in enumerate(ptrs)}
        got = ["owner" if got[0] else 0, index.get(got[1], 0) if got[1] else 0] + got[2:4] + [index.get(got[4], 0) if got[4] else 0] + got[5:9] + [index.get(got[9], 0) if got[9] else 0]
        want = scan_port(s)
        if got != want:
            bad += 1
            if bad <= 3: print(f"scan case {case}:\n  game {got}\n  port {want}\n  {s}")
    print(f"scan ok: {cases} cases" if not bad else f"scan: {bad} of {cases} differ")
    return bad


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    e = emu.Emu()
    install(e)
    mark, bad, kinds = e.brk, 0, {}
    for case in range(cases):
        e.brk = mark
        s = setup(rng)
        got = run_game(e, s)
        port = Port(s)
        port.think()
        want = (port.out, [v & MASK for v in port.ctrl], (port.seed.lo, port.seed.hi))
        kind = got[0][-1][0] if got[0] else "-"
        kinds[kind] = kinds.get(kind, 0) + 1
        if got != want:
            bad += 1
            if bad <= 3:
                print(f"case {case}:\n  game {got}\n  port {want}")
    print("scored by aitype:", dict(sorted(COVER.items())))
    bad += scan_check(emu.Emu(), cases, rng)
    print(f"ok: {cases} cases, {kinds}" if not bad else f"{bad} of {cases} differ, {kinds}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
