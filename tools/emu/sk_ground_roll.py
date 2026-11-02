# SPDX-License-Identifier: GPL-3.0-or-later
"""Does a Fire Ball / Glacial Spike burst roll its damage once for every unit, by game.exe's own code?

    python sk_ground_roll.py game [fireball|glacialspike [TRIALS]] > game.txt
    python sk_ground_roll.py cmp game.txt d2d.txt          # d2d.txt = test_sk_ground --burst-roll NAME TRIALS

Runs next to missiles.py (a Blood Moor game). Three hostile monster units stand
in the burst radius; the missile (the real FUN_0059fa30) has its element min /
max forced through the stat getter (fire 0x30 / 0x31, cold 0x36 / 0x37: 1000 /
5000 in 256ths) and a new seed per trial; the burst hit function runs natively
(FUN_005a9a70 or FUN_005aa8b0: FUN_005a8c70 rolls the record, FUN_0056bad0 hands
it to each unit) and FUN_0056b9c0 is hooked to copy the 0x70-byte record each
unit is given. `game` prints `S trial n records... same` (`same` 1 when the
records are byte-equal: one roll for the burst; a roll per unit would differ
with probability about 1 - 1/4000 a pair). `cmp` counts the trials where d2d's
three monsters (one class, no resistance difference) lost the same life; it
exits 1 when a d2d line has not exactly three positive losses, when either side has no
trials or the two counts differ (run both with the same TRIALS), when game.exe made no
trial equal, or unless that is every trial game.exe made equal.
`game` exits non-zero unless all three monsters and the shooter spawn.
"""
import random
import sys

ROW = {"fireball": 62, "glacialspike": 96}
SKILL = {"fireball": 47, "glacialspike": 55}
HITFN = {"fireball": 0x5a9a70, "glacialspike": 0x5aa8b0}
STATS = {"fireball": (0x30, 0x31), "glacialspike": (0x36, 0x37)}


def game(name, trials):
    import missiles
    g = missiles.Game()
    e = g.e
    rng = random.Random(7)
    lo, hi = STATS[name]
    forced = {lo: 1000, hi: 5000}
    foes, copies = set(), []
    e.hook(0x554200, lambda en: int(en.arg(0) in foes), 1)
    e.hook(0x625480, lambda en: forced.get(en.arg(1), 0) if en.arg(1) in forced else 0, 3)

    def on_apply(en):
        copies.append((en.arg(0), bytes(e.read(en.arg(1), 0x70))))
        return 1
    e.hook(0x56b9c0, on_apply, 3)
    spot = None
    for room in g.rooms:
        rx, ry, rw, rh = room[1:5]
        for _ in range(80):
            x, y = rx + rng.randrange(rw), ry + rng.randrange(rh)
            if all(g.collision(x + dx, y + dy) & 0x1f0f == 0 for dx in range(-8, 9, 2) for dy in range(-8, 9, 2)):
                spot = (x, y)
                break
        if spot:
            break
    x, y = spot
    units = [u for u in (g.unit(19, x + dx, y + dy) for dx, dy in ((0, 0), (2, 1), (-1, 2))) if u]
    shooter = g.unit(63, x + 12, y + 12) or g.unit(63, x - 12, y - 12)
    if len(units) != 3 or not shooter:
        sys.exit(f"setup failed: {len(units)} of 3 monsters and {int(bool(shooter))} of 1 shooter spawned")
    foes.update(u[0] for u in units)
    for t in range(trials):
        p = e.alloc(0x60)
        for i, v in {0: 0x21, 1: shooter[0], 4: ROW[name], 5: x + 1, 6: y + 1, 7: x + 11, 8: y + 1, 0xb: SKILL[name], 0xc: 5}.items():
            e.w32(p + 4 * i, v)
        m = e.call(0x59fa30, ecx=g.g, edx=p)
        e.w32(m + 0x20, rng.getrandbits(32))
        e.w32(m + 0x24, rng.getrandbits(32))
        del copies[:]
        e.call(HITFN[name], 0, ecx=g.g, edx=m)
        recs = [c[1] for c in copies]
        eq = int(len(recs) == len(units) and len(set(recs)) == 1)
        print("S", t, len(recs), " ".join(r[0x10:0x14].hex() + "/" + r[0x24:0x28].hex() for r in recs), eq)


def cmp(game_path, d2d_path):
    equal = [int(ln.split()[-1]) for ln in open(game_path) if ln.startswith("S ")]
    d2d = [tuple(int(v) for v in ln.split()[2:]) for ln in open(d2d_path) if ln.startswith("R ")]
    if any(len(t) != 3 or min(t) <= 0 for t in d2d):
        print("d2d trial without exactly three positive losses: not compared", file=sys.stderr)
        return 1
    same = [len(set(t)) == 1 for t in d2d]
    n = len(equal)
    if n == 0 or len(d2d) != n:
        print(f"trial count mismatch: game.exe {len(equal)}, d2d {len(d2d)}: not compared", file=sys.stderr)
        return 1
    if not any(equal):
        print(f"game.exe made no equal-record trial in {n}: nothing to compare", file=sys.stderr)
        return 1
    bad = sum(1 for k in range(n) if equal[k] and not same[k])
    print(f"{n} trials: game.exe one record in {sum(equal[:n])}, d2d three equal losses in {sum(same[:n])}, differ {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1] == "game":
        game(sys.argv[2] if len(sys.argv) > 2 else "fireball", int(sys.argv[3]) if len(sys.argv) > 3 else 200)
    else:
        sys.exit(cmp(sys.argv[2], sys.argv[3]))
