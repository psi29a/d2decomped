# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's MonUMod eligibility (FUN_005a03e0) as the oracle: may this mod row be rolled on this monster?

    uv run python umod_can_take.py game > game.tsv          # `monster_row mod_row allowed`
    ../../build/tests/test_umod_can_take --umod-can-take | grep -E '^[0-9]+ [0-9]+ [01]$' > d2d.tsv
    uv run python umod_can_take.py compare game.tsv d2d.tsv # `N pairs, M differ`; exit 1 on a difference or a bad input
    uv run python umod_can_take.py probe                    # the MonUMod rows as game.exe holds them

Every MonStats row x every MonUMod row (the table at tables+0xc50, 0x20 bytes a row; d2d's `id` is the row's number), in an expansion game
(the second argument of FUN_005a03e0 is game +0x70), through FUN_005a03e0 itself, natively: the row's
enabled byte (+6), its exclusions (+0xa / +0xc through FUN_005a0070, the monster type matrix) and fPick (+9:
1 FUN_0046c140 flag 4, 2 the multishot flags of MonStats +0xc / +0xe, 3 FUN_0046c140 flag 2). The monster
is a fake unit {type 1, class = the row}. A row's weight (cpick / upick) is checked by the caller
(FUN_005a0500 / FUN_005a0600), not here. The classic-game refusal (a row id of 100 or more) is not run.
"""
import sys

USAGE = "usage: umod_can_take.py game | compare GAME D2D | probe"


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            field = line.split()
            if not field:
                continue
            if len(field) != 3 or not all(f.isdigit() for f in field) or field[2] not in ("0", "1"):
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            out.append(tuple(int(f) for f in field))
    return out


def compare(game_path, d2d_path):
    game, d2d = rows(game_path), rows(d2d_path)
    if not game or len(game) != len(d2d):
        sys.exit(f"empty or unequal: {len(game)} game, {len(d2d)} d2d (an empty file is not a pass)")
    differ = 0
    for g, d in zip(game, d2d, strict=True):
        if g[:2] != d[:2]:
            sys.exit(f"pairs out of step: game {g[0]} {g[1]}, d2d {d[0]} {d[1]}")
        if g[2] != d[2]:
            differ += 1
            print(f"DIFF monster {g[0]} mod {g[1]}: game {g[2]} d2d {d[2]}")
    print(f"{len(game)} pairs, {differ} differ")
    sys.exit(1 if differ else 0)


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("game", 1), ("compare", 3), ("probe", 1)}:
        sys.exit(USAGE)
    if a[0] == "compare":
        compare(a[1], a[2])
    import skill_tips
    e = skill_tips.boot()
    tables = e.r32(0x744304)
    nrows = e.r32(tables + 0xa80)
    mods, nmods = e.r32(tables + 0xc50), e.r32(tables + 0xc54)
    unit = e.alloc(0x100)
    e.w32(unit, 1)
    if a[0] == "probe":
        print("monsters", nrows, "umod rows", nmods, hex(mods))
        for r in range(nmods):
            rec = mods + 0x20 * r
            print(r, "id/word4", e.r16(rec + 4), "en", e.read(rec + 6, 1)[0], "fpick", e.read(rec + 9, 1)[0], "ex", e.r16(rec + 0xa), e.r16(rec + 0xc), "w8", e.read(rec + 8, 1)[0])
        return
    for m in range(nrows):
        e.w32(unit + 4, m)
        for r in range(nmods):
            rec = mods + 0x20 * r
            ok = e.call(0x5a03e0, rec, 1, regs={"ebx": unit}) & 0xff
            print(m, r, ok)


if __name__ == "__main__":
    main()
