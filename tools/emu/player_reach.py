# SPDX-License-Identifier: GPL-3.0-or-later
"""May a player's melee swing start on a monster, and may its blow land? game.exe's FUN_00622c40 over a wall map.

The d2d side is tests/game/test_player_reach.cpp (the real Fight::engage and Fight::hit).

    python3 player_reach.py gen N SEED > cases.tsv
    python3 player_reach.py game cases.tsv > game.tsv      # the swing's start: FUN_00622c40 (client walk end, server start)
    python3 player_reach.py hit cases.tsv > hit.tsv        # the blow: FUN_0057ec10 (do 1's to-hit) with its rolls fixed to pass
    test_player_reach --player-reach cases.tsv | grep -E '^[0-9]+ -?[0-9]+$' > d2d.tsv ; python3 player_reach.py compare game.tsv d2d.tsv

A case is `id px py mx my msize mwalk range nwalls x y x y ...` (subtiles; the monster's MonStats2 SizeX
`msize`, `mwalk` 1 = the monster is in mode 2 (walking), `range` the weapon's rangeadder, then the wall
subtiles with their collision word `x y word`). The line printed is `id 1` (game.exe's client ends the walk
and attacks: FUN_00480c80 LAB_00480d06 and the server's start check FUN_00549ba0 at LAB_00549cbf both
take FUN_00622c40(player, target, FUN_00622dc0(target))) or `id 0` (keeps walking).

What runs for real: FUN_00622c40, FUN_00622dc0 (+ FUN_00463860 / FUN_00463900 from a small MonStats
table), FUN_00641530 (the unit distance), FUN_00622aa0 -> FUN_00622920 -> FUN_0064e260 (the line over the
room's collision words, mask 0x804). Hooked: FUN_00620bb0 (a unit's room: the one fake room), FUN_00620510
(a unit's size), FUN_00622870 (the player's weapon range). The units are fake: type, path words +2 / +6
(subtile x / y), mode +0x10.
"""
import random
import sys

REACH, MOVING = 0x622C40, 0x622DC0
ROOM_OF, SIZE_OF, RANGE_OF = 0x620BB0, 0x620510, 0x622870
TABLES = 0x744304
RECORD = 0x1A8
W = H = 64
WORDS = (4, 4, 4, 0x800, 0x804, 1, 0x400)          # 1 and 0x400 are not in mask 0x804: a line goes through


def gen(n, seed):
    rng = random.Random(seed)
    cid = 0
    out = []
    # A: no wall, every offset within 10 subtiles, every size, walking or not, weapon ranges 0..3
    for dx in range(-10, 11):
        for dy in range(-10, 11):
            for size in (1, 2, 3):
                for walk in (0, 1):
                    for rng_ in (0, 1, 2, 3):
                        out.append(f"{cid} 30 30 {30 + dx} {30 + dy} {size} {walk} {rng_} 0")
                        cid += 1
    # B: random walls between (and around) the pair: thin segments, diagonals, pillars, dots
    for _ in range(n):
        px, py = rng.randrange(20, 40), rng.randrange(20, 40)
        mx, my = px + rng.randrange(-9, 10), py + rng.randrange(-9, 10)
        size, walk, rng_ = rng.choice((1, 2, 2, 3)), rng.randrange(2), rng.choice((0, 0, 1, 2, 3))
        walls = []
        shape = rng.randrange(5)
        # a wall through a point near the middle
        cx, cy = (px + mx) // 2 + rng.randrange(-2, 3), (py + my) // 2 + rng.randrange(-2, 3)
        word = rng.choice(WORDS)
        length = rng.randrange(1, 6)
        if shape == 0:                                   # horizontal
            walls = [(cx + k - length // 2, cy) for k in range(length)]
        elif shape == 1:                                 # vertical
            walls = [(cx, cy + k - length // 2) for k in range(length)]
        elif shape == 2:                                 # diagonal
            walls = [(cx + k - length // 2, cy + k - length // 2) for k in range(length)]
        elif shape == 3:                                 # anti-diagonal
            walls = [(cx + k - length // 2, cy - k + length // 2) for k in range(length)]
        else:                                            # a 2x2 pillar
            walls = [(cx, cy), (cx + 1, cy), (cx, cy + 1), (cx + 1, cy + 1)]
        for _ in range(rng.choice((0, 0, 1, 3))):        # some scatter
            walls.append((rng.randrange(14, 46), rng.randrange(14, 46)))
        walls = list(dict.fromkeys(walls))
        cells = " ".join(f"{x} {y} {word if (x, y) in walls[:length] else rng.choice(WORDS)}" for x, y in walls)
        out.append(f"{cid} {px} {py} {mx} {my} {size} {walk} {rng_} {len(walls)} {cells}")
        cid += 1
    print("\n".join(out))


def game(path, mode="start"):
    from emu import Emu
    e = Emu()
    rows = 0x300
    table = e.alloc(0xB00)
    monstats = e.alloc(rows * RECORD)
    e.w32(TABLES, table)
    e.w32(table + 0xA78, monstats)
    e.w32(table + 0xA80, rows)
    for row in range(rows):
        e.w32(monstats + row * RECORD, row << 16)
    words = e.alloc(W * H * 2)
    coll = e.alloc(0x40); e.w32(coll + 8, W); e.w32(coll + 0x20, words)
    room = e.alloc(0x80); e.w32(room + 0x20, coll)
    for off, v in ((0x4c, 0), (0x50, 0), (0x54, W), (0x58, H)):
        e.w32(room + off, v)
    unit, target = e.alloc(0x200), e.alloc(0x200)
    upath, tpath = e.alloc(0x100), e.alloc(0x100)
    for u, p in ((unit, upath), (target, tpath)):
        e.w32(u + 0x2c, p)
    e.w32(unit, 0)                                      # a player
    e.w32(target, 1); e.w32(target + 4, 0x10)           # a monster, class 0x10 (not a tentacle or vulture)
    cur = {"size": 2, "range": 0}

    def size_of(emu):
        return 2 if emu.arg(0) == unit else cur["size"]

    if mode == "hit":                                    # FUN_0057ec10's own chain, its rolls fixed
        e.hook(0x554200, lambda _e: 1, 1)                # FUN_00554200: a foe
        e.hook(0x5a4ee0, lambda _e: 1, 0)                # FUN_005a4ee0: a monster that may be struck
        e.hook(0x57d9b0, lambda _e: 1, 2)                # FUN_0057d9b0: the to-hit roll passes
        e.hook(0x57dfb0, lambda _e: 0, 3)                # FUN_0057dfb0: no block, dodge or avoid
    e.hook(ROOM_OF, lambda _e: room, 1)
    e.hook(SIZE_OF, size_of, 1)
    e.hook(RANGE_OF, lambda _e: cur["range"], 1)
    for line in lines_of(path):
        f = line.split()
        if not f:
            continue
        cid, px, py, mx, my, size, walk, rng_, nw = (int(v) for v in f[:9])
        grid = bytearray(W * H * 2)
        for k in range(nw):
            x, y, w = int(f[9 + 3 * k]), int(f[10 + 3 * k]), int(f[11 + 3 * k])
            grid[(y * W + x) * 2:(y * W + x) * 2 + 2] = w.to_bytes(2, "little")
        e.mu.mem_write(words, bytes(grid))
        for p, x, y in ((upath, px, py), (tpath, mx, my)):
            e.mu.mem_write(p + 2, x.to_bytes(2, "little"))
            e.mu.mem_write(p + 6, y.to_bytes(2, "little"))
        e.w32(target + 0x10, 2 if walk else 1)          # monster mode WL / NU
        cur["size"], cur["range"] = size, rng_
        if mode == "hit":                                # do 1: FUN_0057ec10(attacker, target, stat 325, range 0)
            got = e.call(0x57EC10, target, 0, 0, ecx=0x1234, edx=unit) & 1
        else:
            moving = e.call(MOVING, target) & 0xFF
            got = e.call(REACH, unit, target, moving) & 0xFF
        print(cid, 1 if got else 0)


def lines_of(path):
    try:
        with open(path) as handle:
            return handle.read().splitlines()
    except OSError as error:
        sys.exit(f"cannot read {path}: {error.strerror}")


def compare(game_path, d2d_path):
    """`N cases, M differ`; exit 1 when any differ, on an empty or unequal file, a malformed line or ids out of step."""
    def rows(path):
        out = []
        for number, line in enumerate(lines_of(path), 1):
            f = line.split()
            if len(f) != 2 or not all(v.lstrip("-").isdigit() for v in f) or f[1] not in ("0", "1"):
                sys.exit(f"{path}:{number}: malformed line {line.strip()!r}")
            out.append((int(f[0]), int(f[1])))
        return out
    a, b = rows(game_path), rows(d2d_path)
    if not a or len(a) != len(b):
        sys.exit(f"empty or unequal files: {len(a)} and {len(b)} lines")
    if any(x[0] != y[0] for x, y in zip(a, b)):
        sys.exit("ids out of step")
    bad = sum(x[1] != y[1] for x, y in zip(a, b))
    print(f"{len(a)} cases, {bad} differ")
    return 1 if bad else 0


USAGE = "usage: player_reach.py gen N SEED | game CASES | hit CASES | compare GAME D2D"


if __name__ == "__main__":
    args = sys.argv[1:]
    verb = args[0] if args else ""
    if verb == "gen" and len(args) == 3 and all(v.isdigit() for v in args[1:]):
        gen(int(args[1]), int(args[2]))
    elif verb in ("game", "hit") and len(args) == 2:
        lines_of(args[1])
        game(args[1], "start" if verb == "game" else "hit")
    elif verb == "compare" and len(args) == 3:
        sys.exit(compare(args[1], args[2]))
    else:
        sys.exit(USAGE)
