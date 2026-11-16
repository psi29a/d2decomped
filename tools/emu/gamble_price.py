# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's gamble price (FUN_0062efb0 mode 2 -> FUN_00629370) as the oracle.

    uv run python gamble_price.py gen > cases.tsv            # every code a gamble tab can hold x clvl 1..99 x reduced 0 / 10
    uv run python gamble_price.py game cases.tsv > game.tsv  # `id price`
    ../../build/tests/test_gamble_price --gamble-price cases.tsv | grep -E '^[a-z0-9]+\\.[0-9]+\\.[0-9]+ -?[0-9]+$' > d2d.tsv
    uv run python gamble_price.py compare game.tsv d2d.tsv   # `N cases, M differ`; exit 1 on a difference or a bad input
    uv run python gamble_price.py probe                      # what else the price depends on (nothing)
A case is `id code clvl reduced`. The item is a fake unit {type 4, class, data +0x30 = 1} on the
price oracle's rig (price_oracle.py): FUN_0062efb0 mode 2 reads the player's level (stat 12) and
reduced prices (stat 0x57), then FUN_00629370 prices the item's normal code (FUN_006287d0).
Item data +0x30 is the item's version (101 in an expansion game, 0 in a classic one, drops.md);
with 0, mode 2 answers the normal record's +0xd4 instead: that path is not run here.
"""
import struct
import sys

USAGE = "usage: gamble_price.py gen | game CASES | compare GAME D2D | probe"


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            field = line.split()
            if not field:
                continue
            if len(field) != 2 or not field[1].lstrip("-").isdigit():
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            out.append((field[0], int(field[1])))
    return out


def compare(game_path, d2d_path):
    game, d2d = rows(game_path), rows(d2d_path)
    if not game or len(game) != len(d2d):
        sys.exit(f"empty or unequal: {len(game)} game, {len(d2d)} d2d (an empty file is not a pass)")
    differ = 0
    for (gid, gprice), (did, dprice) in zip(game, d2d, strict=True):
        if gid != did:
            sys.exit(f"case ids out of step: game {gid}, d2d {did}")
        if gprice != dprice:
            differ += 1
            if differ <= 10:
                print(f"DIFF {gid} game {gprice} d2d {dprice}")
    print(f"{len(game)} cases, {differ} differ")
    sys.exit(1 if differ else 0)


def oracle():
    import price_oracle as po

    class G(po.Oracle):
        def gprice(self, code, clvl, reduced, quality=4, ident=0, eth=0, npc="gheed", diff=0, mode=2, w30=1):
            e = self.e
            mem = bytearray(0x100)
            struct.pack_into("<I", mem, 0, quality)
            struct.pack_into("<I", mem, 0x18, (0x10 if ident else 0) | (0x400000 if eth else 0))
            struct.pack_into("<i", mem, 0x28, -1)
            struct.pack_into("<H", mem, 0x30, w30)
            e.mu.mem_write(self.data, bytes(mem))
            e.w32(self.item + 4, self.cls[code])
            self.istats = {}
            self.pstats = {po.STAT_CLVL: clvl, po.STAT_REDUCED: reduced}
            r = e.call(0x62efb0, diff, self.quests, self.hc[npc], mode, ecx=self.player, edx=self.item)
            return r - (1 << 32) if r & 0x80000000 else r

    return po, G()


def codes(po, g):
    """The bases a gamble tab can hold: gamble.txt's codes and the exceptional / elite versions they upgrade to (FUN_005786a0)."""
    records = dict(g.items)
    out = set(r.get("code", "").strip() for r in po.txt(g.e, "gamble")) - {""}
    for code in list(out):
        for off in (0x88, 0x8c):
            up = struct.pack("<I", g.e.r32(records[code] + off)).decode("latin1").strip("\0 ")
            if up and up != "0":
                out.add(up)
    return [c for c, _ in g.items if c in out and c in g.cls]


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("gen", 1), ("game", 2), ("compare", 3), ("probe", 1)}:
        sys.exit(USAGE)
    if a[0] == "compare":
        compare(a[1], a[2])
    po, g = oracle()
    if a[0] == "probe":
        for code in ("cap", "rin", "amu", "jav", "bkf", "lsd", "gsd", "7wb"):
            if code not in g.cls:
                continue
            base = g.gprice(code, 30, 0)
            var = {(q, i, et, n, d): g.gprice(code, 30, 0, q, i, et, n, d) for q in (1, 2, 4, 6, 7) for i in (0, 1) for et in (0, 1)
                   for n in ("gheed", "akara", "charsi") for d in (0, 2)}
            print(code, base, sorted(set(var.values())), [g.gprice(code, 30, 0, w30=w) for w in (0, 1, 2, 50, 99)],
                  [g.gprice(code, c, 0) for c in (1, 5, 6, 30, 99)])
    elif a[0] == "gen":
        for code in codes(po, g):
            for clvl in range(1, 100):
                for red in (0, 10):
                    print(f"{code}.{clvl}.{red}", code, clvl, red)
    elif a[0] == "game":
        with open(a[1], encoding="utf-8") as cases:
            for line in cases:
                f = line.split()
                if not f:
                    continue
                if len(f) != 4 or f[1] not in g.cls or not (f[2].isdigit() and f[3].isdigit()):
                    sys.exit(f"malformed case: {line.strip()!r}")
                print(f[0], g.gprice(f[1], int(f[2]), int(f[3])))


if __name__ == "__main__":
    main()
