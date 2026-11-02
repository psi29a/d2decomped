#!/usr/bin/env python3
"""MONSTERMISSILE oracle: a monster's missile damage record, made by game.exe's own code (card monstermissile).

Runs game.exe 1.14d under upstream's emulator (copy this file into upstream tools/emu next to missiles.py and run it there):

    D2_MPQ_DIR=<mpq dir> D2_PATCH_INSTALLER=<LODPatch_114d.exe> python mm_launch.py [--exe PATH] TABLES_DIR

TABLES_DIR holds monstats.txt, missiles.txt, skills.txt (mpq-cat, 1.14d patch layer). For each difficulty (a fresh game each) and each
Act 1 / Act 2 shooter row (MissA1, MissA2, MissS1, MissC and the srvmissilea of its skills) the script

  1. spawns the real monster (FUN_00555230, class = hcIdx) in a booted Blood Moor game (the game's difficulty byte = 0 / 1 / 2),
  2. sets the attack mode with game.exe's own FUN_005a4f50 (the mode's stat overlay: A1 / A2 / SC+S1) and reads the stats 0x15 / 0x16
     (min / max damage) back with FUN_00625480: game.exe's own difficulty scaling, no table read,
  3. makes the missile with FUN_0059fa30 (-> FUN_0059f900 -> FUN_0064b860 / FUN_0064ac60) and reads the missile's stats 0x15 / 0x16
     (physical, 256ths) and the element stats 0x30..0x3b with FUN_00625480,
  4. prints  diff mon mode source missile | owner_min owner_max (points) | game_min game_max (256ths) | table_min table_max (the monstats column) |
     read_min read_max (the reading: owner << 8 * SrcDamage / 128 + MinDamage << HitShift) | d2d_min d2d_max (d2d's expression, whole points
     src * SrcDamage / 128 + MinDamage, ai.cpp:2016) | elements (stat, value) pairs.

The "reading" and "d2d" columns are line-for-line ports (Python), not the real d2d binary: tag V run for game.exe, port for d2d.
"""
import csv
import os
import struct
import sys

import drlg
import emu
import missiles
import monsters

PAIR = {"fire": 0x30, "ltng": 0x32, "mag": 0x34, "cold": 0x36}


def SRC_IS_ROW_PATH(src): return src.startswith("Miss")


MODE = {"A1": 4, "A2": 5, "S1": 8, "SC": 7}      # monmode.txt order: DT NU WL GH A1 A2 BL SC S1 S2 S3 S4
STAT = {"min": 0x15, "max": 0x16}
ELEM = (0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b)


def rows(d, name):
    r = list(csv.reader(open(os.path.join(d, name), encoding="latin-1"), delimiter="\t"))
    return [dict(zip(r[0], x)) for x in r[1:] if x and x[0] and x[0] != "Expansion"]


def num(s):
    try: return int(s)
    except ValueError: return 0


class G:
    def __init__(self, difficulty, seed=3, lid=2):
        e = self.e = drlg.boot()
        self.g = monsters.new_game(e, seed, difficulty)
        act = monsters.act_of(e, self.g, lid)
        lvl = drlg.find_level(e, e.r32(act + 0x48), lid)
        drlg.bring_up(e, lvl, seed, "list")
        self.rooms = []
        room1 = e.call(0x61a180, act)
        while room1:
            c = e.r32(room1 + 0x20)
            self.rooms.append((room1, *(e.s32(c + 4 * i) for i in range(4)), c + 0x24))
            room1 = e.r32(room1 + 0x7c)
        self.rows = e.r32(e.r32(0x744304) + 0xb64)

    def spot(self, k):
        room = self.rooms[k % len(self.rooms)]
        return room[0], room[1] + room[3] // 2, room[2] + room[4] // 2

    def unit(self, cls, k):
        r, x, y = self.spot(k)
        return self.e.call(0x555230, x, y, self.g, r, 1, 1, 0, ecx=1, edx=cls), x, y

    def stat(self, u, s):
        return self.e.call(0x625480, u, s, 0)


def main():
    argv = sys.argv[1:]
    d = argv[-1]
    mons = {m["Id"]: m for m in rows(d, "monstats.txt")}
    mis = {m["Missile"]: m for m in rows(d, "missiles.txt")}
    sk = {s["skill"]: s for s in rows(d, "skills.txt")}
    lv = rows(d, "levels.txt")
    monlvl = {num(r["Level"]): r for r in rows(d, "monlvl.txt")}
    ids = set()
    for l in lv:
        if num(l.get("Act", "")) in (0, 1) and l.get("Name"):
            for k in range(1, 11):
                for p in ("mon", "nmon", "umon"):
                    if l.get(f"{p}{k}", "") in mons: ids.add(l[f"{p}{k}"])
    ids |= {b for b in ("andariel", "bloodraven", "radament", "summoner") if b in mons}
    # the brief's other shooters (not in Act 1 / 2 spawn lists): the Fetish blowdart family, Corpse Spitters, Maggot Queens
    ids |= {b for b in ("fetishblow1", "fetishblow2", "fetishblow3", "fetishblow4", "regurgitator1", "regurgitator2", "regurgitator3",
                        "maggotqueen1", "maggotqueen2", "maggotqueen3") if b in mons}
    todo = []
    for mid in sorted(ids, key=lambda i: int(mons[i]["hcIdx"])):
        m = mons[mid]
        for c, mode in (("MissA1", "A1"), ("MissA2", "A2"), ("MissS1", "S1"), ("MissC", "SC")):
            if m.get(c) in mis: todo.append((mid, mode, c, m[c]))
        for k in range(1, 9):
            s = sk.get(m.get(f"Skill{k}", ""))
            if not s or s.get("srvmissilea") not in mis: continue
            mn = s["srvmissilea"]
            if s.get("srvdofunc") == "85":     # FUN_005cb0c0 (ShamanFire, UnHolyBolt): the row's name stem + (its number + TransLvl) at the target
                stem = mn.rstrip("0123456789")
                first = int(mn[len(stem):] or 1)
                mn = f"{stem}{first + num(m.get('TransLvl', ''))}"
                if mn not in mis: print(f"no row {mn} for {mid}"); continue
            todo.append((mid, "A1", f"Skill{k}:{s['skill']}", mn))
    print(f"launches per difficulty: {len(todo)}")
    bad = {"game_vs_read": 0, "owner_vs_table": 0, "d2d_vs_game": 0}
    total = 0
    kinds, diffs = {}, []
    ecases, ediffs = [0, 0, 0], []
    unblock, tohit_rows = {}, set()
    for diff in (0, 1, 2):
        g = G(diff)
        e = g.e
        for n, (mid, mode, src, mn) in enumerate(todo):
            m, r = mons[mid], mis[mn]
            u, x, y = g.unit(int(m["hcIdx"]), n)
            if not u:
                print(f"spawn failed {mid}"); continue
            e.call(0x5a4f50, ecx=u, edx=MODE[mode])
            omin, omax = g.stat(u, 0x15), g.stat(u, 0x16)
            sfx = ("", "(N)", "(H)")[diff]
            sc = "A1" if mode == "A1" else "A2" if mode == "A2" else "S1"
            mlvl = g.stat(u, 0xc)
            dm = num(monlvl[min(mlvl, 110)][("L-DM", "L-DM(N)", "L-DM(H)")[diff]])
            def pct(v): return v if num(m.get("noRatio", "")) else dm * v // 100      # monsters.hpp pct(): base * percent / 100
            tmin = pct(num(m[f"{sc}MinD{sfx}"])); tmax = max(pct(num(m[f"{sc}MaxD{sfx}"])), tmin)
            p = e.alloc(0x60)
            for i, v in {0: 0x21, 1: u, 4: int(r["Id"]), 5: x, 6: y, 7: x + 8, 8: y + 3, 0xc: (0, 3, 7)[diff] + 1}.items(): e.w32(p + 4 * i, v)
            mu = e.call(0x59fa30, ecx=g.g, edx=p)
            if not mu:
                print(f"no missile {mid} {mn}"); continue
            gmin, gmax = g.stat(mu, 0x15), g.stat(mu, 0x16)
            S = num(r["SrcDamage"]) if r["SrcDamage"] not in ("", "-1") else 0
            sh = num(r["HitShift"])
            def trunc7(v): return int((v + (0x7f if v < 0 else 0)) / 128) if False else (v + ((v >> 31) & 0x7f)) >> 7
            rmin = trunc7((omin << 8) * S) + (num(r["MinDamage"]) << sh) if S else (num(r["MinDamage"]) << sh)
            rmax = trunc7((omax << 8) * S) + (num(r["MaxDamage"]) << sh) if S else (num(r["MaxDamage"]) << sh)
            cast = mode == "SC"
            dmin = (0 if cast else omin * S // 128) + num(r["MinDamage"])     # d2d ai.cpp:2016 (whole points, row min raw)
            dmax = (0 if cast else omax * S // 128) + num(r["MaxDamage"])
            elems = [(hex(s_), g.stat(mu, s_)) for s_ in ELEM if g.stat(mu, s_)]
            # elements: d2d's MissA path carries only the monster's own El1..3 whose mode is the shot's (ai.cpp:2019-2020), never the row's EType;
            # fire / lightning / magic / cold min-max stat pairs (poison per-frame stats not compared)
            want, chance = {}, False
            for k in (1, 2, 3):
                el = m.get(f"El{k}Mode", "")
                if el == {"A1": "A1", "A2": "A2", "S1": "S1", "SC": "SC"}[mode] and 0 < num(m.get(f"El{k}Pct{sfx}", "")) < 100: chance = True   # El Pct: a chance, one launch cannot tell
                if el == {"A1": "A1", "A2": "A2", "S1": "S1", "SC": "SC"}[mode] and m.get(f"El{k}Type") in PAIR:
                    lo, hi = num(m.get(f"El{k}MinD{sfx}", "")), num(m.get(f"El{k}MaxD{sfx}", ""))
                    lo, hi = pct(lo), max(pct(hi), pct(lo))
                    if hi > 0: want[PAIR[m[f"El{k}Type"]]] = (lo << 8, hi << 8)
            have = {st: (g.stat(mu, st), g.stat(mu, st + 1)) for st in (0x30, 0x32, 0x34, 0x36) if g.stat(mu, st + 1)}
            row_el = r["EType"] in PAIR or r["EType"] == "pois"
            if chance and SRC_IS_ROW_PATH(src):
                ecases[2] += 1
            elif SRC_IS_ROW_PATH(src):
                ecases[0] += 1
                if have != want:
                    ecases[1] += 1
                    ediffs.append(f"  element differs: diff {diff} {mid} {src} {mn}: game {have}{' (+ row element ' + r['EType'] + ' ' + r['EMin'] + '-' + r['Emax'] + ' x ' + r['ELen'] + ')' if row_el else ''}, d2d MissA path {want}")
            total += 1
            unblock.setdefault(mn, set()).add(gmax == 0)
            if r["ToHit"]: tohit_rows.add(mn)
            if (gmin, gmax) != (rmin, rmax): bad["game_vs_read"] += 1
            if (omin, omax) != (tmin, tmax): bad["owner_vs_table"] += 1
            if (gmin, gmax) != (dmin << 8, dmax << 8):
                bad["d2d_vs_game"] += 1
                cause = ("level bracket" if any(num(r[c]) for c in ("MinLevDam1", "MaxLevDam1")) and diff > 0 else "") or ("min<<hitshift" if sh != 8 and (num(r["MinDamage"]) or num(r["MaxDamage"])) else "") or ("src fraction" if S else "other")
                kinds[cause] = kinds.get(cause, 0) + 1
                diffs.append(f"  d2d differs ({cause}): diff {diff} {mid} {src} {mn} S={S} shift={sh} row min/max {r['MinDamage']}/{r['MaxDamage']}: owner {omin}-{omax}, game {gmin}-{gmax} (256ths), d2d {dmin}-{dmax} points")
            print("\t".join(str(v) for v in (diff, mid, mode, src, mn, "own", omin, omax, "pred(lvl %d)" % mlvl, tmin, tmax, "game256", gmin, gmax, "read256", rmin, rmax,
                                           "d2dpts", dmin, dmax, "game_pts", f"{gmin/256:.2f}", f"{gmax/256:.2f}", "elems", elems)))
    print(f"cases {total}: game vs reading differ {bad['game_vs_read']}; owner stat vs MonLvl x pct / 100 differ {bad['owner_vs_table']}; "
          f"d2d port (whole points << 8) vs game 256ths differ {bad['d2d_vs_game']} {kinds}")
    print(f"elements on the MissA path (d2d carries the monster's own El rows only): {ecases[0]} cases, {ecases[1]} differ; {ecases[2]} cases skipped (El Pct 1..99: a chance per hit, one launch cannot tell)")
    print("rows with a physical part (game record max > 0: blockable): " + " ".join(sorted(k for k, v in unblock.items() if v == {False})))
    print("rows with NO physical part (cannot be blocked): " + " ".join(sorted(k for k, v in unblock.items() if v == {True})))
    print("rows mixed over launches (weapon / chance): " + " ".join(sorted(k for k, v in unblock.items() if len(v) == 2)))
    print("rows whose ToHit is set (the AR roll runs): " + " ".join(sorted(tohit_rows)))
    print("\n".join(diffs + ediffs))


if __name__ == "__main__": main()
