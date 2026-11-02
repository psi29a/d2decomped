# SPDX-License-Identifier: GPL-3.0-or-later
"""Where a standing missile hits a monster, by game.exe's own code.

    python sk_ground_hit.py game [SPAN [ROW [FRAMES [PER]]]] > game.txt   # ROW blizzard1 (default) or firewall
    python sk_ground_hit.py cmp game.txt d2d.txt                          # d2d.txt = test_sk_ground --hit ROW FRAMES PER

Runs next to missiles.py (a Blood Moor game, every room up). Per case a monster
unit of a MonStats class (size 1..3; the first PER of each size, default 2)
stands at a free subtile F and a missile of ROW (blizzard1: Missiles.txt row
159, Size 2; firewall: row 69, Size 1) is made by the real FUN_0059fa30 (params
flags 3, owner a shooter unit, start = F - (a, b), level 1) and stepped by its
own missile do (row +0xc: FUN_005ae480 / FUN_005ae1f0, whose unit search
FUN_00641cb0 tests the footprint of the missile's size against the unit's) for
FRAMES frames. FUN_005adf10 (the hit) is hooked to record the unit; the case's
monster is the only hostile one (FUN_00554200 hooked, as in missiles.py). The
mark a standing missile leaves in the room's collision is undone per case.

`game` prints `H size class a b hit first count`: `hit` 1 when the monster was
struck at any frame, `first` that frame, `count` the strikes. `cmp` takes the
offsets (-SPAN..SPAN) of each class both sides ran (not the ones d2d never
targets) and prints, per class, the offsets only one side hits; it exits 1 on any.
"""
import random
import sys

ROWS = {"blizzard1": 159, "firewall": 69}


def game(span, row, nframes, per):
    import missiles
    shard = ROWS[row]
    g = missiles.Game()
    e = g.e
    rng = random.Random(1)
    found = {1: [], 2: [], 3: []}
    free = []
    for room in g.rooms:
        rx, ry, rw, rh = room[1:5]
        for _ in range(60):
            x, y = rx + rng.randrange(rw), ry + rng.randrange(rh)
            if all(g.collision(x + dx, y + dy) & 0x1f0f == 0 for dx in range(-12, 13, 3) for dy in range(-12, 13, 3)):
                free.append((x, y))
                break
    rng.shuffle(free)
    probe = free.pop()
    for cls in range(0, 800):
        if all(len(v) >= per for v in found.values()):
            break
        u = g.unit(cls, *probe)
        if u and u[3] in found and len(found[u[3]]) < per:
            found[u[3]].append(cls)
    print("# classes by size:", {k: len(v) for k, v in found.items()}, file=sys.stderr)
    n = 0
    for size, classes in found.items():
        for cls in classes:
            fx, fy = free[n % len(free)]
            n += 1
            foe = g.unit(cls, fx, fy)
            if not foe:
                continue
            fu, fx, fy, fsize = foe
            g.foe = fu
            shooter = g.unit(63, fx + 14, fy + 14) or g.unit(63, fx - 14, fy - 14)
            if not shooter:
                print("# no shooter", cls, file=sys.stderr)
                continue
            src = shooter[0]
            for a in range(-span, span + 1):
                for b in range(-span, span + 1):
                    sx, sy = fx - a, fy - b
                    r = g.room_at(sx, sy)
                    if r is None:
                        continue
                    idx = r[5] + 2 * ((sy - r[2]) * r[3] + (sx - r[1]))
                    word = e.r16(idx)
                    p = e.alloc(0x60)
                    for i, v in {0: 3, 1: src, 4: shard, 5: sx, 6: sy, 7: sx, 8: sy, 0xc: 1}.items():
                        e.w32(p + 4 * i, v)
                    m = e.call(0x59fa30, ecx=g.g, edx=p)
                    if not m:
                        print(f"H {size} {cls} {a} {b} nomissile -")
                        continue
                    fn = e.r32(0x73c768 + 4 * g.row(shard, 0xc, 2))
                    g.hit.clear()
                    first = total = 0
                    first = -1
                    for frame in range(1, nframes + 1):
                        k = len(g.hit)
                        e.call(fn, ecx=g.g, edx=m)
                        got = sum(1 for h in g.hit[k:] if h[0] == fu or h[1] == fu)
                        if got and first < 0:
                            first = frame
                        total += got
                    print(f"H {size} {cls} {a} {b} {1 if first >= 0 else 0} {first} {total}")
                    e.mu.mem_write(idx, word.to_bytes(2, "little"))


def load(path, is_game):
    out = {}
    for ln in open(path):
        p = ln.split()
        if len(p) < 6 or p[0] != "H" or p[5] in ("-", "nomissile"):
            continue
        out.setdefault(int(p[2]), {})[(int(p[3]), int(p[4]))] = (int(p[1]), int(p[7]) if is_game else int(p[5]), len(p) > 6 and not is_game and p[6] == "1")
    return out


def cmp(game_path, d2d_path):
    g, d = load(game_path, True), load(d2d_path, False)
    cases = mism = 0
    # Not a hit-area case: a class d2d never hits (hidden or untargetable rows), one that walks off its subtile in d2d
    # whatever the test says, and one game.exe never hits (its unit is not hostile there).
    both = set(g) & set(d)
    nohit = {c for c in both if not any(n for _, n, _ in d[c].values())}
    walks = {c for c in both if any(m for _, _, m in d[c].values())} - nohit
    deaf = {c for c in both if not any(n for _, n, _ in g[c].values())} - nohit - walks
    shared = sorted(both - nohit - walks - deaf)
    print(f"{len(both)} classes both ran; skipped: {len(nohit)} d2d never hits, {len(walks)} walk in d2d, {len(deaf)} game.exe never hits")
    for cls in shared:
        cells = set(g[cls]) & set(d[cls])
        gs = {k for k in cells if g[cls][k][1]}
        ds = {k for k in cells if d[cls][k][1]}
        cases += len(cells)
        mism += len(gs ^ ds)
        if gs ^ ds:
            size = g[cls][next(iter(cells))][0]
            print(f"class {cls} (size {size}): game.exe {len(gs)} offsets, d2d {len(ds)}; only game.exe {len(gs - ds)}, only d2d {len(ds - gs)}")
    print(f"{cases} cases over {len(shared)} classes, {mism} differ")
    return 1 if mism else 0


if __name__ == "__main__":
    if sys.argv[1] == "game":
        a = sys.argv[2:]
        game(int(a[0]) if a else 7, a[1] if len(a) > 1 else "blizzard1", int(a[2]) if len(a) > 2 else 9, int(a[3]) if len(a) > 3 else 2)
    else:
        sys.exit(cmp(sys.argv[2], sys.argv[3]))
