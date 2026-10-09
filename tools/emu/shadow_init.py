# SPDX-License-Identifier: GPL-3.0-or-later
"""The Shadows' inits (docs/research/re/pet-ai.md) against ports.

    uv run python shadow_init.py [cases] [seed] [--dump out.inc]

FUN_005eb490 (ShadowWarrior) runs natively in game.exe on random setups,
its eligibility (FUN_005eab20) and levels (FUN_005eb420) too, the skill
table rows (summon +0xbc, pettype +0xbe) from the setup. What it asks of the
units answers from the setup: the owner (FUN_0058f0d0) and its type, its
skill handles (FUN_006439f0 / FUN_006439b0) and levels (FUN_006442a0, with
or without bonuses), its class's skills (FUN_00451f60 / FUN_00646140 /
FUN_006460f0), a pet's pettype (FUN_00574a20); what it gives the pet
(FUN_00647280) and sets as its left / right skill (FUN_00643bc0 / c50) is
logged. FUN_005ecb70 (ShadowMaster) too. The ports must give the same log
and AI control. Prints `ok`.
"""
import random
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu
from merc import Seed

SKILLS = 400
PETTYPES = 20
MASK = 0xffffffff


def setup(rng):
    owner_class = rng.randint(0, 6)
    skills = rng.sample(range(1, SKILLS), rng.choice((0, 1, 5, 30)))
    if rng.random() < 0.2: skills.append(rng.choice((-1, SKILLS, SKILLS + 5)))
    rows = {}
    for sid in skills + [268]:
        rows[sid] = {"summon": rng.choice((0, 0, 0, rng.randint(1, 600), 0x1a5, 0x10000 - 1)),
                     "pettype": rng.choice((-1, -1, rng.randint(0, PETTYPES + 2), 7))}
    return {"owner": rng.random() < 0.9, "owner_type": rng.choice((0, 0, 0, 1)), "owner_class": owner_class,
            "summon_id": 268, "summoned": rng.random() < 0.9, "summon_level": rng.choice((0, 1, 7, 20, 45)),
            "pet_cls": rng.choice((0x1a5, 0x1a6, rng.randint(0, 600))), "pet_id": rng.randint(1, 100), "pet_pettype": rng.choice((7, rng.randint(0, PETTYPES))),
            "attack": rng.random() < 0.5, "skills": skills, "rows": rows,
            "hard": {sid: rng.choice((None, None, 0, 1, rng.randint(1, 20))) for sid in skills},
            "aip3": rng.choice((0, 5, 20, 36, 60)), "diff": rng.randint(0, 2),
            "seed": (rng.getrandbits(32), rng.getrandbits(32))}


# --- the ports ---------------------------------------------------------------

def eligible(s, sid):                                                     # FUN_005eab20
    if sid < 0 or sid >= SKILLS: return False
    row = s["rows"][sid]
    if row["summon"] == 0: return True
    if row["summon"] == s["pet_cls"] & 0xffff: return False
    if 0 <= row["pettype"] < PETTYPES: return s["pet_pettype"] != row["pettype"]
    return True


def warrior_init(s):
    """FUN_005eb490: AI control 0, 0, 1; with a player owner +0x1c its level
    in the summoning skill (MonStats aip8 (N)); Attack (given if missing) on
    both hands, then each skill of the owner's class it may have at its
    hard points / 2 + +0x1c / 2 (1..24; none 1)."""
    log, ctrl = [], [0, 0, 1]
    if not s["owner"] or s["owner_type"] != 0: return log, ctrl
    if s["summoned"]: ctrl[2] = s["summon_level"]
    if not s["attack"]: log.append(("give", 0, 1))
    log += [("left", 0), ("right", 0)]
    for sid in s["skills"]:
        if not eligible(s, sid): continue
        hard = s["hard"].get(sid)
        base = 1 if hard is None else hard
        log.append(("give", sid, max(1, min(int(base / 2) + int(ctrl[2] / 2), 0x18))))
    return log, ctrl


def master_init(s):
    """FUN_005ecb70: +0x14 -1, +0x18 aip3 + 1, +0x1c a seed step's low bit."""
    seed = Seed(*s["seed"])
    return [], [MASK, s["aip3"] + 1, seed.step() & 1], (seed.lo, seed.hi)


def ai_ok(s, a):
    """FUN_005eabf0: may the Warrior's AI use skill a["id"]? One it may have
    (FUN_005eab20); aitype 4 / 13 (melee kinds) only in melee, the rest only
    out of it; not an aitype-1 buff it's already in; no target, not aitype 2,
    4, 5, 11, 12, 13; aitype 2 not with its aurastate on nor the target in
    its auratargetstate; flags bit 2 with its aurastate on: not when the
    state's aurastat1 is 3 or more."""
    sid, row = a["id"], s["rows"].get(a["id"])
    if not eligible(s, sid): return 0
    t, st, st2 = row["aitype"], row["state"], row["state2"]
    if a["melee"] != (t in (4, 13)): return 0
    if t == 1 and st > 0 and st in a["pet_states"]: return 0
    if not a["target"] and t in (2, 4, 5, 11, 12, 13): return 0
    if t == 2 and (st > 0 and st in a["pet_states"] or st2 > 0 and st2 in a["target_states"]): return 0
    if row["flags"] & 4 and st > 0 and st in a["pet_states"] and a["aura"] is not None and a["aura"] >= 3: return 0
    return 1


def ai_setup(rng, s):
    sid = rng.choice(s["skills"]) if s["skills"] else 268
    if 0 <= sid < SKILLS:
        s["rows"][sid].update(aitype=rng.randint(0, 14), state=rng.choice((0, -1, 5, 9)), state2=rng.choice((0, 5, 9)), flags=rng.choice((0, 4, 0xff)))
    return {"id": sid, "melee": rng.random() < 0.5, "target": rng.random() < 0.7,
            "pet_states": {x for x in (5, 9) if rng.random() < 0.4}, "target_states": {x for x in (5, 9) if rng.random() < 0.4},
            "aura": rng.choice((None, 0, 2, 3, 7))}


# --- game.exe ----------------------------------------------------------------

CUR = {}


def install(e):
    cur = CUR
    reg = lambda r: e.mu.reg_read(r)
    out = lambda item: cur["log"].append(item)
    e.hook(0x58f0d0, lambda e: cur["owner"] if cur["s"]["owner"] else 0, 0)
    e.hook(0x6439f0, lambda e: 0x10 if cur["s"]["summoned"] and reg(UC_X86_REG_EDX) == cur["s"]["summon_id"] else 0, 1)
    def handle(e):                                                        # FUN_006439b0: a unit's skill handle
        unit, sid = reg(UC_X86_REG_ECX), reg(UC_X86_REG_EDX)
        if unit == cur["pet"]: return 0x20 if sid == 0 and cur["s"]["attack"] else 0
        return 0x1000 + sid if cur["s"]["hard"].get(sid) is not None else 0
    e.hook(0x6439b0, handle, 1)
    def level(e):
        h, bonus = e.arg(1), e.arg(2)
        if h == 0x10: assert bonus == 1; return cur["s"]["summon_level"]
        assert bonus == 0
        return cur["s"]["hard"][h - 0x1000]
    e.hook(0x6442a0, level, 3)
    e.hook(0x647280, lambda e: out(("give", e.arg(1), e.arg(2))) or 0, 6)
    e.hook(0x643bc0, lambda e: out(("left", e.arg(1))) or 0, 3)
    e.hook(0x643c50, lambda e: out(("right", e.arg(1))) or 0, 3)
    e.hook(0x451f60, lambda e: cur["s"]["owner_class"], 0)
    e.hook(0x646140, lambda e: len(cur["s"]["skills"]) if e.arg(0) == cur["s"]["owner_class"] else 0, 1)
    e.hook(0x6460f0, lambda e: cur["s"]["skills"][e.arg(1)] & MASK, 2)
    def pettype(e):
        assert reg(UC_X86_REG_ECX) == cur["owner"] and reg(UC_X86_REG_EDX) == cur["s"]["pet_id"]
        return cur["s"]["pet_pettype"]
    e.hook(0x574a20, pettype, 0)
    def states(e):
        a = cur["s"].get("ai", {})
        unit, st = e.arg(0), e.arg(1)
        if unit == cur["pet"]: return int(st in a["pet_states"])
        assert unit == cur["target"] and unit
        return int(st in a["target_states"])
    e.hook(0x639df0, states, 2)
    e.hook(0x6256b0, lambda e: 0 if cur["s"]["ai"]["aura"] is None else 0x99, 2)
    e.hook(0x625d00, lambda e: (e.arg(1) == 7 or (_ for _ in ()).throw(AssertionError)) and cur["s"]["ai"]["aura"], 3)


def run_game(e, s, addr):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    data = alloc(0x1000)
    table = alloc(0x23c * SKILLS)
    e.w32(0x744304, data); e.w32(data + 0xba0, SKILLS); e.w32(data + 0xb98, table); e.w32(data + 0xbf0, PETTYPES)
    for sid, row in s["rows"].items():
        if 0 <= sid < SKILLS:
            e.mu.mem_write(table + 0x23c * sid + 0xbc, struct.pack("<Hb", row["summon"], row["pettype"]))
            e.mu.mem_write(table + 0x23c * sid + 4, bytes([row.get("flags", 0)]))
            e.mu.mem_write(table + 0x23c * sid + 0x54, struct.pack("<h", 7))
            e.mu.mem_write(table + 0x23c * sid + 0x80, struct.pack("<hh", row.get("state", 0), row.get("state2", 0)))
            e.mu.mem_write(table + 0x23c * sid + 0x230, bytes([row.get("aitype", 0)]))
    game, params, ctrl, row, pet, owner = (alloc(n) for n in (0x200, 0x40, 0x40, 0x200, 0x100, 0x100))
    e.w32(pet + 4, s["pet_cls"]); e.w32(pet + 0xc, s["pet_id"]); e.w32(pet + 0x20, s["seed"][0]); e.w32(pet + 0x24, s["seed"][1])
    e.w32(owner, s["owner_type"])
    e.mu.mem_write(game + 0x6d, bytes([s["diff"]]))
    e.mu.mem_write(row + 0x80, struct.pack("<h", s["summon_id"]))
    e.mu.mem_write(row + 0x62 + 2 * s["diff"], struct.pack("<h", s["aip3"]))
    for k in range(3): e.w32(ctrl + 0x14 + 4 * k, 0x5a5a5a5a)
    e.w32(params, ctrl); e.w32(params + 7 * 4, row)
    CUR.update(s=s, log=[], pet=pet, owner=owner)
    if addr == 0x5eabf0:
        a = s["ai"]
        target = alloc(0x40) if a["target"] else 0
        CUR.update(target=target)
        e.w32(params + 0x18, int(a["melee"])); e.w32(params + 8, target)
        return e.call(addr, pet, a["id"] & MASK, params, ecx=game, edx=owner)
    e.call(addr, params, ecx=game, edx=pet)
    return CUR["log"], [e.r32(ctrl + 0x14 + 4 * k) for k in range(3)], (e.r32(pet + 0x20), e.r32(pet + 0x24))


def dump_case(s, log, ctrl, master):
    """One line: owner, its type, summoned, the level, pet class / pettype, has Attack, aip3, seed,
    the skills (id summon pettype hard), | what's given (id level) ..., | AI control | the Master's."""
    nums = [int(s["owner"]), s["owner_type"], int(s["summoned"]), s["summon_level"], s["pet_cls"], s["pet_pettype"], int(s["attack"]),
            s["aip3"], *s["seed"], len(s["skills"])]
    for sid in s["skills"]:
        r = s["rows"][sid]
        hard = s["hard"].get(sid)
        nums += [sid, r["summon"] - (0x10000 if r["summon"] >= 0x8000 else 0) if 0 <= sid < SKILLS else 0, r["pettype"] if 0 <= sid < SKILLS else -1, -1 if hard is None else hard]
    gives = [v for item in log if item[0] == "give" for v in item[1:]]
    sx = lambda v: v - (1 << 32) if v >= 1 << 31 else v
    a = s["ai"]
    row = s["rows"].get(a["id"], {})
    st, st2 = row.get("state", 0), row.get("state2", 0)
    ai = [int(eligible(s, a["id"])), row.get("aitype", 0), int(a["melee"]), int(a["target"]), int(st > 0 and st in a["pet_states"]),
          int(a["target"] and st2 > 0 and st2 in a["target_states"]), int(bool(row.get("flags", 0) & 4)), -1 if a["aura"] is None else a["aura"], ai_ok(s, a)]
    return " ".join(map(str, nums)) + " | " + " ".join(map(str, gives)) + " | " + " ".join(map(str, ctrl)) + " | " + " ".join(str(sx(v)) for v in master[1]) + " " + " ".join(map(str, master[2])) + " | " + " ".join(map(str, ai))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.endswith(".inc")]
    cases = int(args[0]) if args else 2000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    dumped = [] if "--dump" in sys.argv else None
    e = emu.Emu()
    install(e)
    mark, bad, gave, usable = e.brk, 0, 0, 0
    for case in range(cases):
        e.brk = mark
        s = setup(rng)
        log, ctrl, _ = run_game(e, s, 0x5eb490)
        want = warrior_init(s)
        gave += sum(1 for item in log if item[0] == "give" and item[1])
        if (log, ctrl) != (want[0], [v & MASK for v in want[1]]):
            bad += 1
            if bad <= 5: print(f"warrior case {case}:\n  game {log} {ctrl}\n  port {want}")
        e.brk = mark
        s["ai"] = ai_setup(rng, s)
        if run_game(e, s, 0x5eabf0) != ai_ok(s, s["ai"]):
            bad += 1
            if bad <= 5: print(f"ai_ok case {case}: game {run_game(e, s, 0x5eabf0)} port {ai_ok(s, s['ai'])} {s['ai']} {s['rows'].get(s['ai']['id'])}")
        usable += ai_ok(s, s["ai"])
        e.brk = mark
        got = run_game(e, s, 0x5ecb70)
        if dumped is not None and got == master_init(s) and (log, ctrl) == (want[0], want[1]) and len(s["skills"]) <= 6 \
                and (len(dumped) < 40 and case % 4 == 0 or len(dumped) < 100 and any(item[0] == "give" and item[2] == 0x18 for item in log)):
            dumped.append(dump_case(s, log, ctrl, got))
        if got != master_init(s):
            bad += 1
            if bad <= 5: print(f"master case {case}:\n  game {got}\n  port {master_init(s)}")
    if dumped is not None:
        with open(sys.argv[sys.argv.index("--dump") + 1], "w") as f:
            f.write("// tools/emu/shadow_init.py --dump: game.exe's own Shadow inits (see the script).\nR\"(\n" + "\n".join(dumped) + "\n)\"\n")
    print(f"ok: {cases} cases each ({gave} skills given, {usable} usable by the AI)" if not bad else f"{bad} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
