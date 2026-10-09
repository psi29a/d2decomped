# SPDX-License-Identifier: GPL-3.0-or-later
"""The Shadows' inits (docs/research/re/pet-ai.md) against ports.

    uv run python shadow_init.py [cases] [seed]

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


def run_game(e, s, addr):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    data = alloc(0x1000)
    table = alloc(0x23c * SKILLS)
    e.w32(0x744304, data); e.w32(data + 0xba0, SKILLS); e.w32(data + 0xb98, table); e.w32(data + 0xbf0, PETTYPES)
    for sid, row in s["rows"].items():
        if 0 <= sid < SKILLS:
            e.mu.mem_write(table + 0x23c * sid + 0xbc, struct.pack("<Hb", row["summon"], row["pettype"]))
    game, params, ctrl, row, pet, owner = (alloc(n) for n in (0x200, 0x40, 0x40, 0x200, 0x100, 0x100))
    e.w32(pet + 4, s["pet_cls"]); e.w32(pet + 0xc, s["pet_id"]); e.w32(pet + 0x20, s["seed"][0]); e.w32(pet + 0x24, s["seed"][1])
    e.w32(owner, s["owner_type"])
    e.mu.mem_write(game + 0x6d, bytes([s["diff"]]))
    e.mu.mem_write(row + 0x80, struct.pack("<h", s["summon_id"]))
    e.mu.mem_write(row + 0x62 + 2 * s["diff"], struct.pack("<h", s["aip3"]))
    for k in range(3): e.w32(ctrl + 0x14 + 4 * k, 0x5a5a5a5a)
    e.w32(params, ctrl); e.w32(params + 7 * 4, row)
    CUR.update(s=s, log=[], pet=pet, owner=owner)
    e.call(addr, params, ecx=game, edx=pet)
    return CUR["log"], [e.r32(ctrl + 0x14 + 4 * k) for k in range(3)], (e.r32(pet + 0x20), e.r32(pet + 0x24))


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    e = emu.Emu()
    install(e)
    mark, bad, gave = e.brk, 0, 0
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
        got = run_game(e, s, 0x5ecb70)
        if got != master_init(s):
            bad += 1
            if bad <= 5: print(f"master case {case}:\n  game {got}\n  port {master_init(s)}")
    print(f"ok: {cases} cases each ({gave} skills given)" if not bad else f"{bad} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
