# SPDX-License-Identifier: GPL-3.0-or-later
"""The merc's attack think (FUN_005e5050) and skill pick (FUN_005e4d30)
against a port (components/rules/merc.hpp merc_attack / merc_skill_pick).

    uv run python merc_attack.py [cases] [seed]

game.exe with its tables (drlg.boot): hireling.txt, MonStats, Skills are
its own. Random cases: the merc's class (each LoD hireling Id), level, AI
control +0x14, its gap to the foe, in melee or not, which of its skills it
has at what level and which aura states are on, which moves find a path.
Hooked: the stat read (level), the pet record (the hireling Id), sizes, the
skill lookup and level (FUN_006439b0 / FUN_006442a0), states (FUN_00639df0),
melee (FUN_00622c40), the effects: a move (FUN_005a7c20), a skill
(FUN_005dead0), an aura started (FUN_005701b0), a swing (FUN_005ddf90), a
stand (FUN_005de080). The rolls are game.exe's own on the merc's seed.
"""
import random
import struct
import sys

import drlg
import merc as think

MASK = 0xffffffff


def hirelings(e):
    """The loaded hireling.txt rows (DAT_0096bdd0, 0x118 each)."""
    base, count = e.r32(0x96bdd0), e.r32(0x96bdd4)
    rows = []
    for k in range(count):
        r = base + k * 0x118
        b = e.read(r, 0x118)
        s32 = lambda o: struct.unpack_from("<i", b, o)[0]
        rows.append({
            "version": struct.unpack_from("<H", b, 0)[0], "id": s32(4), "cls": s32(8), "level": s32(0x1c),
            "default": s32(0x64),
            "skills": [s32(0x78 + 4 * k2) for k2 in range(6)], "chance": [s32(0x90 + 4 * k2) for k2 in range(6)],
            "per": [s32(0xa8 + 4 * k2) for k2 in range(6)], "mode": list(b[0xc0:0xc6]),
        })
    return rows


def row_for(rows, hid, level):
    """FUN_006562f0: the LoD rows of `hid`, the last with Level <= level (else the first)."""
    found = None
    for r in rows:
        if r["version"] != 100 or r["id"] != hid: continue
        if found is not None and level < r["level"]: return found
        found = r
    return found


def skill_info(e, sid):
    t = e.r32(0x744304)
    if sid < 0 or sid >= e.r32(t + 0xba0): return None
    r = e.r32(t + 0xb98) + sid * 0x23c
    return {"flags": e.read(r + 4, 1)[0], "aura": e.read(r + 0x230, 1)[0], "state": struct.unpack("<h", e.read(r + 0x80, 2))[0]}


def merc_attack(s, seed, sk, move):
    """components/rules/merc.hpp merc_attack (+ merc_skill_pick), line for line."""
    out = []
    melee_cls = s["cls"] in (0x152, 0x230, 0x231)
    chance = 0x62 if melee_cls else min(s["ai14"] + 0x28 + 2 * s["level"], 0x5f)
    roll = seed.step() % 100
    if chance <= roll: s["ai14"] += 10; use = False
    else: s["ai14"] = 0; use = True
    def pick():
        row = row_for(s["rows"], s["hid"], s["level"])
        if row is None: return
        d = max(s["level"] - row["level"], 0)
        total, cum = row["default"], [None] * 6
        for k in range(6):
            sid = row["skills"][k]
            if sid < 1 or sid >= s["skill_count"]: break
            cum[k] = 0
            have = s["have"].get(sid, 0)
            info = sk(sid)
            on = info["aura"] == 1 and 0 <= info["state"] < s["state_count"] and info["state"] in s["states"]
            if have <= 0 or on: continue
            if sid == 0x29 and have // 2 + 4 < s["dist2"]: continue
            p = row["per"][k] * d
            total += int(p / 4) + row["chance"][k]
            cum[k] = total
        n = total + 1
        r = 0 if n < 1 else seed.rand(n)
        if row["default"] <= r:
            k = 0
            while k < 6 and (cum[k] is None or cum[k] < r):
                if cum[k] is None: k = 6; break
                k += 1
            if k < 6 and row["skills"][k] > 0:
                if sk(row["skills"][k])["flags"] & s["aura_mask"]:
                    out.append(("aura", row["skills"][k])); out.append(("stand", 10)); return
                out.append(("skill", row["mode"][k], row["skills"][k])); return
        if s["cls"] == 0x10f:
            out.append(("skill", s["sk1mode"], s["skill1"])); return
        if s["cls"] in (0x152, 0x167, 0x230, 0x231) and s["melee"]:
            out.append(("swing",)); return
        out.append(("stand", 10))
    if s["aip1"] == 0:
        if s["gap"] < 4 and seed.step() % 100 < 0x32:
            if move("about"): return out
            if move("away"): return out
            pick(); return out
        if use: pick(); return out
    else:
        if s["gap"] > 2 or not s["melee"]:
            move("run"); return out
        if use: pick(); return out
    out.append(("stand", 10))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = int(args[0]) if args else 2000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    e = drlg.boot()
    rows = hirelings(e)
    ids = sorted({r["id"] for r in rows if r["version"] == 100})
    t = e.r32(0x744304)
    skill_count, state_count = e.r32(t + 0xba0), e.r32(t + 0xc4)
    aura_mask = e.r32(0x6ce27c)
    monstats, monstats_count = e.r32(t + 0xa78), e.r32(t + 0xa80)
    sk_cache = {}
    def sk(sid):
        if sid not in sk_cache: sk_cache[sid] = skill_info(e, sid)
        return sk_cache[sid]
    cur = {}
    def out(item): cur["out"].append(item); return 0
    def stat(e):
        return cur["s"]["level"] if e.arg(1) == 0xc else 0
    e.hook(0x625480, stat, 3)
    def record(e):
        rec = e.alloc(0x20); e.w32(rec + 8, cur["s"]["hid"]); return rec
    e.hook(0x574bd0, record, 0)
    e.hook(0x620510, lambda e: cur["sizes"].get(e.arg(0), 1), 1)
    from unicorn.x86_const import UC_X86_REG_EDX
    e.hook(0x6439b0, lambda e: e.mu.reg_read(UC_X86_REG_EDX) if cur["s"]["have"].get(e.mu.reg_read(UC_X86_REG_EDX), 0) > 0 else 0, 1)
    e.hook(0x6442a0, lambda e: cur["s"]["have"].get(e.arg(1), 0), 3)
    e.hook(0x639df0, lambda e: int(e.arg(1) in cur["s"]["states"]), 2)
    e.hook(0x622c40, lambda e: int(cur["s"]["melee"]), 3)
    e.hook(0x5dead0, lambda e: out(("skill", e.arg(0) & 0xff, e.arg(1))), 5)
    e.hook(0x5701b0, lambda e: out(("aura", e.arg(0))), 2)
    e.hook(0x5ddf90, lambda e: out(("swing",)), 2)
    e.hook(0x5de080, lambda e: out(("stand", e.arg(0))), 1)
    e.hook(0x540e60, lambda e: 0, 2)
    def setoff(e):
        ok = cur["moves"].random() >= cur["s"]["fails"]
        out(("move", int(ok)))
        return int(ok)
    e.hook(0x5a7c20, setoff, 3)
    e.hook(0x5a6260, lambda e: 0, 2)
    mark, bad, kinds, dumped = e.brk, 0, {}, set()
    dump = "--dump" in sys.argv
    for case in range(cases):
        if e.brk - mark > 0x2000000: e.brk = mark
        hid = rng.choice(ids)
        lvl = rng.randint(1, 98)
        row = row_for(rows, hid, lvl)
        cls = row["cls"]
        msrow = monstats + cls * 0x1a8
        diff = rng.randint(0, 2)
        aip1 = struct.unpack("<h", e.read(msrow + 0x56 + diff * 2, 2))[0]
        have = {}
        for k, sid in enumerate(row["skills"]):
            if 0 < sid < skill_count and rng.random() < 0.85: have[sid] = rng.randint(1, 30)
        states = set()
        for sid in have:
            info = sk(sid)
            if info["aura"] == 1 and info["state"] >= 0 and rng.random() < 0.4: states.add(info["state"])
        gap = rng.choice((0, 1, 2, 3, 5, 9, 20))
        s = {"cls": cls, "hid": hid, "level": lvl, "ai14": rng.choice((0, 0, 10, 20, 30, 60)), "aip1": aip1, "gap": gap,
             "melee": rng.random() < 0.5, "have": have, "states": states, "fails": rng.random(), "rows": rows,
             "skill_count": skill_count, "state_count": state_count, "aura_mask": aura_mask,
             "skill1": struct.unpack("<h", e.read(msrow + 0x170, 2))[0], "sk1mode": e.read(msrow + 0x180, 1)[0],
             "seed": (rng.getrandbits(32), rng.getrandbits(32))}
        game = e.alloc(0x2000); e.mu.mem_write(game + 0x6d, bytes([diff])); e.w32(game + 0x70, 1)
        merc_u, tgt, owner, mpath, tpath, params, ai, ms2 = (e.alloc(n) for n in (0x200, 0x200, 0x200, 0x200, 0x200, 0x40, 0x100, 0x40))
        for a, n in ((merc_u, 0x200), (tgt, 0x200), (mpath, 0x200), (tpath, 0x200), (params, 0x40), (ai, 0x100), (ms2, 0x40)): e.mu.mem_write(a, bytes(n))
        mx, my = 5000, 5000
        # the foe `gap` off (FUN_005dc380 less the merc's size 1): along x
        tx, ty = mx + gap + 1, my
        s["dist2"] = gap + 1                                              # FUN_006416d0 here: the plain axis gap (sizes 1)
        e.w32(merc_u, 1); e.w32(merc_u + 4, cls); e.w32(merc_u + 0xc, 77); e.w32(merc_u + 0x2c, mpath)
        e.w32(merc_u + 0x20, s["seed"][0]); e.w32(merc_u + 0x24, s["seed"][1])
        e.w32(mpath, mx << 16 | 0x8000); e.w32(mpath + 4, my << 16 | 0x8000); e.w32(mpath + 0x30, merc_u)
        e.w32(tgt, 1); e.w32(tgt + 0x2c, tpath); e.w32(tpath, tx << 16 | 0x8000); e.w32(tpath + 4, ty << 16 | 0x8000); e.w32(tpath + 0x30, tgt)
        e.w32(ai + 0x14, s["ai14"])
        e.w32(params, ai); e.w32(params + 0x1c, msrow); e.w32(params + 0x20, ms2)
        cur.update(s=s, out=[], moves=random.Random(case), sizes={merc_u: 1, tgt: 1})
        e.call(0x5e5050, cls, owner, tgt, merc_u + 0x20, params, ecx=game, edx=merc_u)
        got = (cur["out"], e.r32(ai + 0x14), (e.r32(merc_u + 0x20), e.r32(merc_u + 0x24)))
        # the port, on the same case
        moves = random.Random(case)
        seed = think.Seed(*s["seed"])
        port_out = []
        def move(kind):
            ok = moves.random() >= s["fails"]
            port_out.append(("move", int(ok)))
            if kind == "about":                                        # FUN_005df530's three steps
                if seed.step() & 1 == 0: seed.rand(4)
                else: seed.rand(4)
                seed.step(); seed.step()
            return ok
        s2 = dict(s)
        res = merc_attack(s2, seed, sk, move)
        want = (port_out + res, s2["ai14"], (seed.lo, seed.hi))
        kind = ("aura" if any(x[0] == "aura" for x in want[0]) else want[0][-1][0] if want[0] else "-")
        kinds[kind] = kinds.get(kind, 0) + 1
        if dump and got == want and len(dumped) < 30:
            row = row_for(rows, hid, lvl)
            key = (kind, s["aip1"] == 0, len(port_out))
            if key not in dumped:
                dumped.add(key)
                skill_ids = [sid if 0 < sid < skill_count else 0 for sid in row["skills"]]
                levels = [have.get(sid, 0) for sid in skill_ids]
                auras = [int(bool(sk(sid)["flags"] & aura_mask)) if sid else 0 for sid in skill_ids]
                running = [int(bool(sid) and sk(sid)["aura"] == 1 and sk(sid)["state"] in states) for sid in skill_ids]
                last = next((x for x in res if x[0] == "aura"), res[-1] if res else ("stand", 0))
                kind_cc = {"move": "moved", "skill": "skill", "aura": "aura", "swing": "swing", "stand": "stand"}[last[0]] if res else ("moved" if port_out and port_out[-1][1] else "failed")
                skill_cc = last[2] if last[0] == "skill" else last[1] if last[0] == "aura" else -1
                mode_cc = last[1] if last[0] == "skill" else 0
                ar = lambda xs: "{ " + ", ".join(str(x) for x in xs) + " }"
                print(f"{{ {cls}, {lvl}, {s['aip1']}, {gap}, {int(s['melee'])}, {s['ai14']}, {s['seed'][0]}u, {s['seed'][1]}u, {row['level']}, {row['default']}, "
                      f"{ar(skill_ids)}, {ar(row['chance'])}, {ar(row['per'])}, {ar(row['mode'])}, {ar(levels)}, {ar(auras)}, {ar(running)}, {s['skill1']}, {s['sk1mode']}, "
                      f"{ar(m[1] for m in port_out)}, Kind::{kind_cc}, {skill_cc}, {mode_cc}, {s2['ai14']}, {seed.lo}u, {seed.hi}u }},   // attack")
        if got != want:
            bad += 1
            if bad <= 8: print(f"case {case}: game {got}\n          port {want}\n          cls {cls:#x} hid {hid} lvl {lvl} aip1 {aip1} gap {gap} melee {s['melee']} have {have} states {states} ai14 {s['ai14']}")
    print(f"merc attack ok: {cases} cases, {kinds}" if not bad else f"merc attack: {bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
