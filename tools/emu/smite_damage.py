# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's Smite damage as the oracle: FUN_0057b420 with the shield's damage, as FUN_005ce9f0 calls it (0x5ceb31).

    set -o pipefail; ../../build/tests/test_smite_damage --smite-damage > d2d.txt
    uv run python smite_damage.py compare d2d.txt        # last line: N cells  same, or N cells  M differ (exit 1)

Smite hits with the shield's damage: FUN_005ce9f0 calls FUN_0057b420 (edx = 0, the shield as the item, min and max the shield's `mindam` and
`maxdam` shifted left by 8 plus Holy Shield's own min and max, ED = the skill's calc1, SrcDam 0x80). FUN_0057b420 on that path
(0x57b4c3..0x57b4f4) adds stat 111 (normal damage) shifted left by 8 to both ends, lifts a minimum under 1 point to 1 point, and then, if
the maximum is not above the minimum, makes the maximum the minimum plus one point (0x57b4ea..0x57b4f4: `cmp [max], min; jg; add min, 0x100`).
The percent (stat 25 and the item's Str and Dex bonus, FUN_00629860 / FUN_006298a0, gear stats 17 and 18, the skill's ED) comes after, as
MulDiv(x, 100 + percent, 100) on each end, floored at -90; the roll is FUN_0045c3e0 on the unit's seed, `min + roll(max - min)`.

`d2d.txt` is test_smite_damage's grid: `S d0 d1 bm bM hl hh ed n111 dp str dex sb db off | hit phys_fine`. The seed's two words are loaded into the unit
(+0x20 and +0x24) and game.exe rolls the damage; `phys_fine` is d2d's roll in 256ths (the physical after resistance 0): the two are compared
whole, in 256ths. The grid has no negative percent (a percent under -90 and the rounding of a negative percent are other rules).

Runs natively: FUN_0057b420 and the RNG step and MulDiv it calls. Hooked (not the claim): the stat getters FUN_00625480 / FUN_00625500 (stats
0, 2, 0x11, 0x12, 0x19, 0x6f from the case), the item bonus getters FUN_00629860 / FUN_006298a0, FUN_00645830 (a mastery: 0), FUN_00535bc0 and
FUN_0063d340 (which hand: 1), FUN_00639df0 / FUN_00554010 / FUN_005347b0 / FUN_0057d810 / FUN_00451f80 (side effects off the damage).
"""
import sys

import emu

M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


class Game:
    def __init__(self):
        self.e = e = emu.Emu()
        self.stats, self.st = {}, {}
        e.hook(0x625480, lambda e_: self.stats.get((e_.arg(0), e_.arg(1)), 0) & M32, 3)
        e.hook(0x625500, lambda e_: self.stats.get((e_.arg(0), e_.arg(1)), 0) & M32, 3)
        for addr, n in ((0x639df0, 2), (0x554010, 3), (0x5347b0, 0), (0x57d810, 0), (0x451f80, 0)):
            e.hook(addr, lambda e_: 0, n)
        e.hook(0x57d8a0, lambda e_: 0, 2)
        e.hook(0x645830, lambda e_: 0, 4)
        e.hook(0x535bc0, lambda e_: 0, 0)
        e.hook(0x63d340, lambda e_: 1, 2)
        e.hook(0x629860, lambda e_: self.st["sb"], 1)
        e.hook(0x6298a0, lambda e_: self.st["db"], 1)
        self.att = e.alloc(0x100)

    def roll(self, d0, d1, bm, bM, hl, hh, ed, n111, dp, strength, dex, sb, db, off):
        e, att = self.e, self.att
        e.w32(att, 0)
        e.w32(att + 0x20, d0)
        e.w32(att + 0x24, d1)
        self.st.update(sb=sb, db=db)
        self.stats.clear()
        self.stats.update({(att, 0x6f): n111, (att, 0x19): dp, (att, 0): strength, (att, 2): dex, (att, 0x11): off, (att, 0x12): off})
        return s32(e.call(0x57b420, 0x7100000, (bm << 8) + hl, (bM << 8) + hh, ed & M32, 0, 0x80, ecx=att, edx=0))


def compare(path):
    g = Game()
    cells = bad = 0
    shown = 0
    by = {}
    for lineno, line in enumerate(open(path, encoding="latin1"), 1):
        p = line.split()
        if not p or p[0] != "S":
            continue
        if len(p) != 18 or p[15] != "|":
            raise SystemExit("%s:%d: malformed S line (%d fields)" % (path, lineno, len(p)))
        try:
            v = [int(x) for x in p[1:15]]
            hit, phys = int(p[16]), int(p[17])
        except ValueError:
            raise SystemExit("%s:%d: malformed S line (a number is not one)" % (path, lineno)) from None
        if hit != 1:
            raise SystemExit("%s:%d: d2d did not hit with Smite (it always hits)" % (path, lineno))
        got = g.roll(*v)
        d0, d1, bm, bM, hl, hh, ed, n111, dp, strength, dex, sb, db, off = v
        cells += 1
        key = ("n111>0" if n111 else "n111=0") + (" min=max" if bm == bM and hl == hh else "")
        c = by.setdefault(key, [0, 0])
        c[0] += 1
        if got != phys:
            bad += 1
            c[1] += 1
            if shown < 12:
                shown += 1
                print("C seed %d %d shield %d..%d hs %d..%d ed %d n111 %d dp %d str %d dex %d bonus %d %d off %d | d2d %d | game %d | %+d" % (
                    d0, d1, bm, bM, hl, hh, ed, n111, dp, strength, dex, sb, db, off, phys, got, phys - got))
    for key, (n, d) in sorted(by.items()):
        print("SUM %-18s cells %6d  differ %6d" % (key, n, d))
    print("%d cells  %s" % (cells, "same" if not bad else "%d differ" % bad))
    if cells == 0:
        raise SystemExit("no comparable cells in %s: an empty file is not a pass" % path)
    return 1 if bad else 0


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "compare":
        raise SystemExit(compare(sys.argv[2]))
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
