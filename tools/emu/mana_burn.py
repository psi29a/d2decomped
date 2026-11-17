# SPDX-License-Identifier: GPL-3.0-or-later
"""What a Mana Burn monster's landed melee hit takes off a player's mana, by game.exe's own code.

    uv run python mana_burn.py gen > cases.tsv
    uv run python mana_burn.py game cases.tsv > game.tsv
    ../../build/tests/test_mana_burn --mana-burn cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv
    uv run python mana_burn.py compare game.tsv d2d.tsv
    ../../build/tests/test_mana_burn --mana-burn-roll cases.tsv | grep -E '^[0-9]+ ' > roll.tsv
    uv run python mana_burn.py compare game.tsv roll.tsv exact   # rules::mana_burn_fine alone, byte for byte

A case is `lo hi seed`: lo / hi the whole points d2d keeps (Monster::mana_lo / mana_hi; FUN_005a1f90 stores `x << 8` in
stats 62 / 63), seed the attacker's unit seed (low word, high 666). `game` runs the melee chain on a monster attacker
(type 1, no hireling) and a player with 60000 mana: the gate FUN_0057dbf0 builds the damage record with FUN_0057b7d0
(a plain monster: mana drain += FUN_0057a8e0 over stats 62 / 63, `<< 8` again on top of the stored `<< 8`), the
resolver FUN_0057d4f0 applies it (FUN_0057c6c0 -> FUN_0057aa60 takes the record's +0x3c off the player's mana). Real:
those, FUN_0057a880 (the roll), FUN_0057aa60, FUN_0057a980, FUN_0057aa00; the stat code is a dict (the real getter and
setter return what was stored, checked on a fake stat list), the events, the reach and the other consumers are hooked
to 0 / 1 as in vse_run.py. `game` prints `lo hi seed fine`: the mana lost, in 256ths of a point.
The d2d side prints the same, from the real monster_update on a zombie standing at a foe and the real Fight::hurt_side
on a player with 60000 mana (the mana it lost).
`compare GAME D2D` accepts a case when d2d's amount lies in game.exe's range for that row, `lo << 16` up to (not
including) `hi << 16` (equal when lo == hi), and the game's own amount lies in it and equals the rule's model
(`lo << 16 + draw`, the unit's seed after one draw); `compare GAME D2D exact` asks d2d's amount to equal game.exe's.
It prints the counts and exits 1 when any case fails, on an empty file, files of unequal length, a malformed line or
cases out of step.
"""
import sys

M32 = 0xFFFFFFFF


def gen():
    for lo, hi in ((0, 0), (1, 3), (4, 7), (7, 18), (10, 10), (25, 40), (30, 81), (100, 150)):
        for seed in range(1, 251):
            print(lo, hi, seed)


def model(lo, hi, seed):
    """FUN_0057a880 on the stat range: `(lo << 16) + draw % ((hi - lo) << 16)` on the unit's seed after one draw."""
    a, b = lo << 16, hi << 16
    if b <= a:
        return a
    low, high = seed, 666
    for _ in range(2):
        p = low * 0x6AC690C5 + high
        low, high = p & M32, (p >> 32) & M32
    n = b - a
    return a + (low & (n - 1) if n & (n - 1) == 0 else low % n)


def game(path):
    import life_apply
    g = life_apply.Game(real=(0x57aa60, 0x57a980, 0x57aa00, 0x57a8e0, 0x57a880))
    e = g.e
    for a_, k in ((0x57d3d0, 2), (0x5d10c0, 2), (0x57c9f0, 1), (0x57cee0, 2), (0x621e40, 3), (0x554010, 4), (0x5347c0, 0), (0x5c0c30, 3)):
        e.hook(a_, lambda e: 0, k)
    e.hook(0x622c40, lambda e: 1, 3)
    e.hook(0x5a4ee0, lambda e: 1, 0)
    e.hook(0x622d00, lambda e: 0, 1)
    e.hook(0x623c20, lambda e: 0, 0)
    e.hook(0x40b430, lambda e: e.alloc(0x100), 3)
    e.hook(0x63ee90, lambda e: 0, 1)
    e.w32(g.attacker, 1); e.w32(g.attacker + 4, 5)
    e.w32(g.attacker + 0xc, 1234); e.w32(g.target + 0xc, 4321)
    e.w32(g.attacker + 0x10, 3); e.w32(g.target + 0x10, 1)
    e.w32(g.target, 0); e.w32(g.target + 4, 1)
    big = 60000 << 8
    for line in lines(path):
        p = line.split()
        if len(p) != 3 or not all(x.isdigit() for x in p):
            sys.exit(f"malformed case in {path}: {line.strip()!r}")
        lo, hi, seed = (int(x) for x in p)
        g.reset(30000, 0, 0)
        g.stats[(g.target, 8)] = big; g.stats[(g.target, 9)] = big
        g.stats[(g.attacker, 6)] = 1000 << 8; g.stats[(g.attacker, 7)] = 2000 << 8
        g.stats[(g.attacker, 62)] = lo << 8; g.stats[(g.attacker, 63)] = hi << 8
        g.stats[(g.attacker, 21)] = 3; g.stats[(g.attacker, 22)] = 5
        e.w32(g.attacker + 0x20, seed); e.w32(g.attacker + 0x24, 666)
        rec = e.alloc(0x100)
        e.w32(rec, 0); e.w32(rec + 4, 1)
        e.call(0x57dbf0, g.target, rec, 0x80, ecx=g.game, edx=g.attacker)
        e.call(0x57d4f0, g.target, 0, ecx=g.game, edx=g.attacker)
        print(lo, hi, seed, big - g.stats.get((g.target, 8), 0))


def lines(path):
    try:
        with open(path) as f:
            return f.read().splitlines()
    except OSError as e:
        sys.exit(f"cannot read {path}: {e.strerror}")


def read(path):
    rows = []
    for line in lines(path):
        p = line.split()
        if len(p) != 4 or not all(x.lstrip("-").isdigit() for x in p):
            sys.exit(f"malformed line in {path}: {line.strip()!r}")
        rows.append(tuple(int(x) for x in p))
    if not rows:
        sys.exit(f"{path} is empty")
    return rows


def inside(value, lo, hi):
    """In game.exe's range for `lo hi`: `lo << 16` up to (not including) `hi << 16`, exactly `lo << 16` when hi <= lo."""
    a, b = lo << 16, hi << 16
    return value == a if b <= a else a <= value < b


def compare(game_path, d2d_path, exact=False):
    g, d = read(game_path), read(d2d_path)
    if len(g) != len(d):
        sys.exit(f"{len(g)} game lines, {len(d)} d2d lines")
    bad_game = bad_d2d = 0
    for (lo, hi, seed, fine), (lo2, hi2, seed2, fine2) in zip(g, d):
        if (lo, hi, seed) != (lo2, hi2, seed2):
            sys.exit(f"rows out of step: {(lo, hi, seed)} {(lo2, hi2, seed2)}")
        bad_game += fine != model(lo, hi, seed) or not inside(fine, lo, hi)
        bad_d2d += fine2 != fine if exact else not inside(fine2, lo, hi)
    what = "d2d not game.exe's amount" if exact else "d2d outside game.exe's range"
    print(f"{len(g)} cases: game.exe off its own rule {bad_game}, {what} {bad_d2d}")
    if bad_game or bad_d2d:
        sys.exit(1)
    print("ok")


USAGE = "usage: mana_burn.py gen | game CASES | compare GAME D2D [exact]"


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["gen"]:
        gen()
    elif len(a) == 2 and a[0] == "game":
        game(a[1])
    elif len(a) in (3, 4) and a[0] == "compare" and a[3:] in ([], ["exact"]):
        compare(a[1], a[2], a[3:] == ["exact"])
    else:
        sys.exit(USAGE)
