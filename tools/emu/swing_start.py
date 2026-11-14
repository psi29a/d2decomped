# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's player attack swing as the oracle: when the strike and the end of the swing come, counted from the tick the mode starts.

    set -o pipefail; ../../build/tests/test_swing_start --swing-start | grep '^D ' > d2d.tsv
    uv run python swing_start.py compare d2d.tsv
    uv run python swing_start.py table          # the start-frame table and the weapon-class map of game.exe, as read from the image

FUN_005539b0 files a mode's events from the start frame FUN_00623b10 returns (shifted left by 8): walking the animation at the
rate in the unit (+0x4c), it files the action event (an AnimData flag byte, 1, 2 or 4) on the tick its frame is reached and the end
event (type 1) when the position reaches the frame count x 256. FUN_00623b10 is 0 for a monster and for every player mode but A1
and A2 (S3 and S4 only when FUN_006235a0, a class test true for a Barbarian or Assassin, is true); for an A1 or A2 it looks up the weapon held
(FUN_00623990 -> FUN_00629fe0, a class key mapped through 0x7446a0) in the table at 0x6e8e60, one row a weapon class and one
column a player class. This script runs FUN_00623b10, FUN_00629fe0 and FUN_005539b0 with the real AnimData flag bytes, and checks
the closed form that d2d implements:

    start  = FUN_00623b10
    strike = max(1, ceil(256 (flag - start) / rate)) ticks after the mode starts
    end    = max(1, ceil(256 (frames - start) / rate))   (game.exe's own floor is 2: the two differ only when one tick covers the whole
             rest of the animation, rate >= 256 (frames - start), which no player record reaches)

`d2d.tsv` is test_swing_start's grid: `D class mode weapon ias wsm rate record frames speed ticks strike`, d2d's swing length and
strike tick for each cell (set_pmode on a character wearing the weapon, rate = the attack speed it computed: the rate itself is
checked elsewhere, tools/emu/player_attack_rate.py). `compare` prints the cells where d2d's swing end or strike tick is not game.exe's,
the totals, then `N cells  same` or `N cells  M differ`; it exits 1 on a difference.

Hooked (not the claim): FUN_00623990 (which weapon the unit holds: a fake item, or none; game.exe takes slot 4, else slot 5, a weapon
(type weap) that is usable, FUN_0063c9b0 -> FUN_0062a4e0: not broken (flag 0x100), requirements met (0x4000), so the caller passes the
class key of THAT weapon, or "hth" when none qualifies), FUN_006335f0 (that item's data record, whose
+0xc0 holds the weapon class key FUN_00629fe0 reads), FUN_006235a0 (the Barbarian / Assassin test: no), FUN_00645270 (the shapeshift remap, a no-op),
FUN_00540e60 and FUN_005417d0 (the event queue: the second records each event). Runs natively: FUN_00623b10, FUN_00629fe0 (inside
it), FUN_005539b0.

Tags: V (the oracle's own run): the start frame and the event ticks of every cell. U: a monster-held weapon path. A class key the map
lacks gives row 0 (the nothing-in-hand row: start 1 for an Amazon or Sorceress), V run; every vanilla weapon's key is in the map.
"""
import collections
import struct
import sys

import emu
import skill_tips

KEYS = {"bow": b"bow ", "1hs": b"1hs ", "1ht": b"1ht ", "stf": b"stf ", "2hs": b"2hs ", "2ht": b"2ht ", "xbw": b"xbw ", "ht1": b"ht1 ", "hth": None}
# The test's weapon codes -> their weapons.txt wclass (the held weapon's class key)
WCLASS = {"none": "hth", "ssd": "1hs", "dgr": "1ht", "gsd": "2hs", "spr": "2ht", "sst": "stf", "sbw": "bow", "lxb": "xbw", "ktr": "ht1"}
CLASS = ["Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"]
MODE = {7: "A1", 8: "A2", 12: "KK", 13: "S1", 15: "S3", 16: "S4"}
FLAGS = (1, 2, 4)   # the flag bytes that file the action event (the jump table at 0x553afc); 3 files another kind


def parse_animdata(blob):
    """name -> (frames, speed, flags[144]): 256 buckets of (u32 count, count x 160-byte records: name[8], u32 frames, u32 speed, flags[144])."""
    recs, pos = {}, 0
    for _ in range(256):
        if pos + 4 > len(blob):
            raise SystemExit("animdata.d2 is cut short")
        (count,) = struct.unpack_from("<I", blob, pos)
        pos += 4
        if pos + 160 * count > len(blob):
            raise SystemExit("animdata.d2 is cut short")
        for _ in range(count):
            name = blob[pos:pos + 8].split(b"\0")[0].decode("latin1")
            frames, speed = struct.unpack_from("<II", blob, pos + 8)
            recs[name] = (frames, speed, blob[pos + 16:pos + 160])
            pos += 160
    return recs


class Game:
    def __init__(self):
        self.recs = parse_animdata(emu.mpq_cache().read(r"data\global\animdata.d2"))
        e = self.e = skill_tips.boot()
        self.game, self.unit, self.rec, self.item, self.itrec = (e.alloc(0x200) for _ in range(5))
        self.events, self.weapon = [], None
        e.hook(0x540e60, lambda e_: 0, 2)
        e.hook(0x5417d0, lambda e_: self.events.append((e_.arg(0), e_.arg(1), e_.arg(2))) or 0, 4)
        e.hook(0x645270, lambda e_: 0, 6)
        e.hook(0x6235a0, lambda e_: 0, 1)
        e.hook(0x623990, lambda e_: self.item if self.weapon is not None else 0, 2)
        e.hook(0x6335f0, lambda e_: self.itrec, 1)

    def one(self, cls, mode, wclass, name, rate):
        e = self.e
        frames, speed, flags = self.recs[name]
        self.weapon = KEYS[wclass]
        if self.weapon is not None:
            e.mu.mem_write(self.itrec + 0xc0, self.weapon)
        self.events.clear()
        e.mu.mem_write(self.rec + 0x10, flags)
        e.w32(self.game + 0xa8, 1000)                      # the game frame the events are counted from
        e.mu.mem_write(self.unit, bytes(0x100))
        e.w32(self.unit + 0x04, cls)
        e.w32(self.unit + 0x10, mode)
        e.w32(self.unit + 0x48, frames * 256)              # the end: frame count x 256
        e.mu.mem_write(self.unit + 0x4c, rate.to_bytes(2, "little"))
        e.w32(self.unit + 0x50, self.rec)
        e.w32(self.unit + 0x60, 1)
        start = e.call(0x623b10, self.unit)
        e.call(0x5539b0, ecx=self.game, edx=self.unit)
        strike = [t - 1000 for kind, t, flag in self.events if kind == 0 and flag in FLAGS]
        end = [t - 1000 for kind, t, flag in self.events if kind == 1]
        return {"start": start, "strike": strike[0] if strike else -1, "end": end[0] if end else -1, "frames": frames, "speed": speed,
                "flag": next((i for i, b in enumerate(flags) if b in FLAGS), -1)}


def ceil_div(a, b):
    return -(-a // b)


def compare(path):
    game = Game()
    stat, by = collections.Counter(), collections.defaultdict(collections.Counter)
    out, cells = [], 0
    for line in open(path, encoding="latin1"):
        p = line.split()
        if len(p) != 12 or p[0] != "D":
            continue
        cls, mode, weapon, ias, wsm, rate = int(p[1]), int(p[2]), p[3], int(p[4]), int(p[5]), int(p[6])
        name, frames, ticks, strike = p[7], int(p[8]), int(p[10]), int(p[11])
        if name not in game.recs:
            stat["no AnimData record"] += 1
            continue
        g = game.one(cls, mode, WCLASS[weapon], name, rate)
        if g["strike"] < 0:
            stat["record without an action flag"] += 1
            continue
        stat["record disagrees with d2d's frame count"] += g["frames"] != frames
        stat["closed form: strike differs"] += max(1, ceil_div((g["flag"] - g["start"]) * 256, rate)) != g["strike"]
        stat["closed form: end differs"] += max(1, ceil_div((g["frames"] - g["start"]) * 256, rate)) != g["end"]
        cells += 1
        key = (CLASS[cls], MODE[mode], "start frame" if g["start"] else "no start frame")
        by[key]["cells"] += 1
        by[key]["end"] += ticks != g["end"]
        by[key]["strike"] += strike != g["strike"]
        if ticks != g["end"] or strike != g["strike"]:
            out.append("C %s %s %s ias %d wsm %d rate %d %s start %d | d2d end %d strike %d | game end %d strike %d | %+d %+d" % (
                CLASS[cls], MODE[mode], weapon, ias, wsm, rate, name, g["start"], ticks, strike, g["end"], g["strike"], ticks - g["end"], strike - g["strike"]))
    for line in out[:int(sys.argv[3]) if len(sys.argv) > 3 else 20]:
        print(line)
    for key in sorted(by):
        c = by[key]
        print("SUM %-12s %s %-14s cells %5d  end differs %5d  strike differs %5d" % (key + (c["cells"], c["end"], c["strike"])))
    for name, count in sorted(stat.items()):
        print("CHECK %s: %d" % (name, count))
    bad = len(out)
    print("%d cells  %s" % (cells, "same" if not bad else "%d differ (end %d, strike %d)" % (bad, sum(c["end"] for c in by.values()), sum(c["strike"] for c in by.values()))))
    if stat["closed form: strike differs"] or stat["closed form: end differs"]:
        raise SystemExit("the closed form does not match game.exe's events")
    return 1 if bad else 0


def table():
    e = skill_tips.boot()
    print("modes 7..16 -> case (0x623c10):", list(e.read(0x623c10, 10)))
    print("wclass map (0x7446a0): count", e.r32(0x7446e0))
    for i in range(e.r32(0x7446e0)):
        print("  %-4s -> row %d" % (bytes(e.read(0x7446a0 + 8 * i, 4)).decode("latin1"), e.r16(0x7446a4 + 8 * i)))
    print("start frame by row (0x6e8e60, one dword a player class):", "  ".join(CLASS_SHORT for CLASS_SHORT in ("ama", "sor", "nec", "pal", "bar", "dru", "ass")))
    for row in range(0, 13):
        print("  row %2d:" % row, " ".join("%3d" % e.s32(0x6e8e60 + 28 * row + 4 * c) for c in range(7)))


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "compare":
        raise SystemExit(compare(sys.argv[2]))
    if len(sys.argv) > 1 and sys.argv[1] == "table":
        return table()
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
