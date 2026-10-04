# SPDX-License-Identifier: GPL-3.0-or-later
"""A missile's flight: game.exe makes it (FUN_0059fa30) and steps it each
frame (its srvdofunc 1, FUN_005ae1f0 -> FUN_00554ca0 -> FUN_00650840),
against a line-for-line copy of components/rules/missiles.hpp.

    uv run python missiles.py [cases] [seed] [--dump] [--break]

A Blood Moor game (monsters.py's), every room up. Each case puts a Quill
Rat at a random free subtile as the shooter and fires a missile from it at
a random point: an arrow, a Quill Rat's spike, a Fallen Shaman's fire,
Fire Bolt, Ice Bolt, Andariel's poison bolt, Charged Bolt (its callback
FUN_005c9290: the wiggling path) at a random level, or an arrow row with
a random Vel / MaxVel / Accel. Half the cases put a monster of a random size near the line as the
only foe (FUN_00554200 hooked: hostile to that one). The hit
(FUN_005adf10) is hooked to record the frame and the unit and end the
flight. Each frame's 16.16 position, then how it ended, are compared.

Prints `ok` or each mismatch. `--dump` prints cases that ran to their
range or a foe as C++ lines for tests/test_monsters.cpp (missile_flights); `--break`
breaks the copy (no 75 / 100) to show the check bites. The rooms: a
missile's room (path +0x1c) moves with it only when it was sent outside
its first room (path flag 1, FUN_006492f0); a subtile off that room and
its near list reads 0x27 and stops it (FUN_00463740, FUN_0064d450).
"""
import random
import sys

import drlg
import monsters

BREAK = "--break" in sys.argv
DIRS = ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1))      # DAT_006f1798
TRY = ((5, 4, 6), (4, 5, 6), (4, 3, 5), (4, 3, 2), (3, 4, 2), (6, 5, 4), (5, 4, 6), (4, 3, 5), (3, 4, 2), (2, 3, 4),
       (6, 7, 5), (6, 7, 5), (6, 7, 5), (2, 1, 3), (2, 1, 3), (6, 7, 0), (7, 0, 6), (0, 1, 7), (1, 0, 2), (2, 1, 0),
       (7, 0, 6), (0, 7, 6), (0, 1, 7), (0, 1, 2), (1, 0, 2))                        # DAT_006f1518
WIGGLE = [(-1, 0, 1)[i % 3] for i in range(31)] + [1]                                 # FUN_0067a240's local table
ROWS = {"arrow": 0, "spike1": 7, "spike5": 11, "shafire1": 22, "chargedbolt": 56, "firebolt": 58, "icebolt": 59, "andypoisonbolt": 203}


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


# --- the port: components/rules/missiles.hpp, line for line ---

def missile_velocity(vel, vel_lev, level):
    """FUN_0059fa30: ((Vel + VelLev x lvl / 8) << 8) x 75 / 100."""
    v = (vel + cdiv(vel_lev * level, 8)) << 8
    return v if BREAK else cdiv(v * 75, 100)


def missile_range(rng, lev_range, level):
    """FUN_0059fa30: Range + LevRange x lvl, frames (FUN_0064a330 clamps to a short)."""
    return max(-0x8000, min(0x7fff, rng + lev_range * level))


def aim(table, x, y, tx, ty):
    """FUN_0064fc60: the unit vector (x 4096) from 16.16 (x, y) to (tx, ty)."""
    neg_x = tx < x
    lo_x, hi_x = (tx, x) if neg_x else (x, tx)
    hi_y, lo_y = (ty, y) if ty >= y else (y, ty)
    steep = s32(hi_x - lo_x) <= s32(hi_y - lo_y)
    major, minor = (hi_y - lo_y, hi_x - lo_x) if steep else (hi_x - lo_x, hi_y - lo_y)
    i = 0 if major == 0 else cdiv(s32(minor * 0x7f), s32(major))
    small, big = table[i]
    vx, vy = (small, big) if steep else (big, small)
    if ty < y: vy = -vy
    if neg_x: vx = -vx
    return vx, vy


def wiggle_points(x, y, tx, ty, steps, seed):
    """FUN_0067a240 (path type 10, Charged Bolt): steps / 2 two-subtile steps from (x, y), each the 8-way
    direction toward (tx, ty) (FUN_00678c10 -> DAT_006f1518) turned by a seed draw."""
    dx, dy = tx - x, ty - y
    ax, ay = dx, dy
    if abs(dx) >= abs(dy) * 2: ay = -1 if dy < 0 else dy & 1
    elif abs(dx) * 2 <= abs(dy): ax = -1 if dx < 0 else dx & 1
    base = TRY[max(-2, min(2, ax)) * 5 + 12 + max(-2, min(2, ay))][0]
    points = []
    for _ in range(steps >> 1):
        product = seed[0] * 0x6AC690C5 + seed[1]
        seed[0], seed[1] = product & 0xFFFFFFFF, product >> 32
        d = (WIGGLE[seed[0] & 0x1f] + base) & 7
        points.append((x, y))
        x, y = (x + DIRS[d][0] * 2) & 0xFFFF, (y + DIRS[d][1] * 2) & 0xFFFF
    points.append((x, y))
    return points


class Flight:
    def __init__(self, table, x, y, tx, ty, vel, max_vel, accel, points=None, rooms=None):
        self.table, self.rooms = table, rooms
        self.room = rooms.holding(rooms.start, x, y) if rooms else None
        self.follow = bool(rooms) and not rooms.inside(self.room, tx, ty)        # FUN_006492f0: path flag 1
        self.x, self.y = (x << 16) + 0x8000, (y << 16) + 0x8000
        self.dx, self.dy = aim(table, self.x, self.y, (tx << 16) + 0x8000, (ty << 16) + 0x8000)
        self.vel, self.max_vel, self.accel, self.count = vel, max_vel, accel, 0
        self.points, self.index = points, 0

    def step(self, blocked):
        """FUN_00650840's move: (moved, the subtiles it crossed). moved False: the flight is over."""
        if self.accel:
            self.count += 1
            if self.count > 4:
                self.vel += self.accel
                if self.vel > self.max_vel: self.vel, self.accel = self.max_vel, 0
                elif self.vel < 0: self.vel = 0
                self.count = 0
        speed = s32(self.vel * 0x400) >> 6
        step_x, step_y = s32(self.dx * speed) >> 12, s32(self.dy * speed) >> 12
        if step_x == 0 and step_y == 0: return self.stop()
        snapped = False
        if self.points is not None:
            px, py = (self.points[self.index][0] << 16) + 0x8000, (self.points[self.index][1] << 16) + 0x8000
            reach = max(abs(step_x), abs(step_y))
            if abs(px - self.x) <= reach and abs(py - self.y) <= reach:
                step_x, step_y, snapped = px - self.x, py - self.y, True
        crossed = self.walk(step_x, step_y, blocked)
        if crossed is None: return False, []
        self.x, self.y = self.x + step_x, self.y + step_y
        if self.follow and not self.rooms.inside(self.room, self.x >> 16, self.y >> 16):   # FUN_0064fad0
            self.room = self.rooms.holding(self.room, self.x >> 16, self.y >> 16)
            if self.room is None: return self.stop(crossed)
        if snapped:
            self.index += 1
            if self.index >= len(self.points): return self.stop(crossed)
            self.retarget()
            if self.index >= len(self.points): return self.stop(crossed)
        return True, crossed

    def walk(self, step_x, step_y, blocked):
        """FUN_00650150: the subtiles the step crosses, in steps halved (FUN_00678f00) to a subtile at most,
        ten at most; None at a blocked one, the flight snapped to the centre of the last free point."""
        sub_x, sub_y = step_x, step_y
        while not -0x10000 <= sub_x <= 0x10000: sub_x >>= 1; sub_y >>= 1
        while not -0x10000 <= sub_y <= 0x10000: sub_x >>= 1; sub_y >>= 1
        end = (((self.x + step_x) >> 16) & 0xFFFF, ((self.y + step_y) >> 16) & 0xFFFF)
        at_x, at_y = self.x, self.y
        here = ((at_x >> 16) & 0xFFFF, (at_y >> 16) & 0xFFFF)
        crossed = []
        while here != end:
            next_x, next_y = (at_x + sub_x) & 0xFFFFFFFF, (at_y + sub_y) & 0xFFFFFFFF
            there = (next_x >> 16, next_y >> 16)
            if there != here:
                if (self.rooms and self.rooms.holding(self.room, *there) is None) or blocked(*there):   # 0x27 off the near rooms
                    self.x, self.y = (at_x & ~0xFFFF) + 0x8000, (at_y & ~0xFFFF) + 0x8000
                    return None
                crossed.append(there)
                if len(crossed) >= 10: break
            at_x, at_y, here = next_x, next_y, there
        return crossed

    def retarget(self):
        """FUN_0064fe40: the next point not under it; past the last, the flight's over."""
        while True:
            px, py = (self.points[self.index][0] << 16) + 0x8000, (self.points[self.index][1] << 16) + 0x8000
            if (px, py) != (self.x, self.y): break
            if self.index >= len(self.points) - 1:
                self.index = len(self.points)
                return
            self.index += 1
        self.dx, self.dy = aim(self.table, self.x, self.y, px, py)

    def stop(self, crossed=()):
        self.x, self.y = (self.x & ~0xFFFF) + 0x8000, (self.y & ~0xFFFF) + 0x8000
        return False, list(crossed)


def touches(mx, my, ux, uy, size):
    """FUN_00641cb0 for a size-1 missile at (mx, my): a unit of `size` there."""
    dx, dy = abs(mx - ux), abs(my - uy)
    if size == 1: return dx == 0 and dy == 0
    if size == 2: return dx + dy <= 1
    return dx <= 1 and dy <= 1


def fly(table, case, blocked):
    """The whole flight as missiles.hpp runs it: [(x, y) per frame], (end frame, kind, foe)."""
    vel = missile_velocity(case["vel"], case["vel_lev"], case["level"])
    flight = Flight(table, *case["from"], *case["to"], vel, case["max_vel"] << 8, case["accel"], case.get("points"), case.get("rooms"))
    left, active = case["range"], case["range"] - case["activate"]
    track = []
    while True:
        moved, entered = flight.step(blocked)
        track.append((flight.x, flight.y))
        if not moved: return track, (len(track), "wall")
        left -= 1
        if left < 1: return track, (len(track), "range")
        if left > active: continue
        for spot in entered:
            for k, (ux, uy, size) in enumerate(case["foes"]):
                if touches(*spot, ux, uy, size): return track, (len(track), "unit", k)


class Rooms:
    """Room rects (subtiles) and near lists, as game.exe's room1s hold them (+0x4c rect, +0 / +0x24 near list)."""
    def __init__(self, rect, near, start):
        self.rect, self.near, self.start = rect, near, start

    def inside(self, room, x, y):
        rx, ry, rw, rh = self.rect[room]
        return rx <= x < rx + rw and ry <= y < ry + rh

    def holding(self, room, x, y):
        """FUN_00463740: the room or the first of its near list holding (x, y)."""
        if self.inside(room, x, y): return room
        return next((r for r in self.near[room] if self.inside(r, x, y)), None)


# --- game.exe ---

class Game:
    def __init__(self, seed=3, lid=2):
        e = self.e = drlg.boot()
        self.g = monsters.new_game(e, seed)
        act = monsters.act_of(e, self.g, lid)
        lvl = drlg.find_level(e, e.r32(act + 0x48), lid)
        drlg.bring_up(e, lvl, seed, "list")
        self.rooms = []
        room1 = e.call(0x61a180, act)
        while room1:
            c = e.r32(room1 + 0x20)
            self.rooms.append((room1, *(e.s32(c + 4 * i) for i in range(4)), c + 0x24))
            room1 = e.r32(room1 + 0x7c)
        self.rect = {r[0]: tuple(e.s32(r[0] + 0x4c + 4 * i) for i in range(4)) for r in self.rooms}
        self.near = {r[0]: [n for n in (e.r32(e.r32(r[0]) + 4 * i) for i in range(e.r32(r[0] + 0x24))) if n] for r in self.rooms}
        self.table = [(e.s32(0x6eb7e0 + 12 * i), e.s32(0x6eb7e4 + 12 * i)) for i in range(128)]
        self.rows = e.r32(e.r32(0x744304) + 0xb64)
        self.hit, self.foe = [], 0
        e.hook(0x554200, lambda e: int(e.arg(0) == self.foe and self.foe != 0), 1)   # hostile: the case's foe only

        def on_hit(e):
            self.hit.append((e.arg(0), e.arg(1)))
            return 2
        e.hook(0x5adf10, on_hit, 2)

    def room_at(self, x, y):
        return next((r for r in self.rooms if r[1] <= x < r[1] + r[3] and r[2] <= y < r[2] + r[4]), None)

    def collision(self, x, y):
        r = self.room_at(x, y)
        return 0xFFFF if r is None else self.e.r16(r[5] + 2 * ((y - r[2]) * r[3] + (x - r[1])))

    def row(self, mid, off, size):
        a = self.rows + mid * 0x1a4 + off
        return int.from_bytes(self.e.read(a, size), "little", signed=size == 2)

    def unit(self, cls, x, y):
        r = self.room_at(x, y)
        u = self.e.call(0x555230, x, y, self.g, r[0], 1, 1, 0, ecx=1, edx=cls)
        if not u: return None
        p = self.e.r32(u + 0x2c)
        return u, self.e.r16(p + 2), self.e.r16(p + 6), self.e.call(0x620510, u)


def make_case(game, rng):
    e = game.e
    for _ in range(1000):
        room = rng.choice(game.rooms)
        x, y = room[1] + rng.randrange(room[3]), room[2] + rng.randrange(room[4])
        if game.collision(x, y) & 0x1f0f == 0: break
    name = rng.choice(list(ROWS) + ["random"])
    mid = ROWS.get(name, 0)
    level = rng.randrange(1, 40)
    case = {"name": name, "mid": mid, "level": level, "from": (x, y)}
    span = rng.choice((3, 10, 30))
    case["to"] = (x + rng.randint(-span, span), y + rng.randint(-span, span))
    if rng.random() < 0.1: case["to"] = (x, y)
    return case


def run_case(game, case, rng):
    e = game.e
    mid = case["mid"]
    saved = e.read(game.rows + mid * 0x1a4 + 0x9a, 6)
    if case["name"] == "random":
        vel, maxv, accel = rng.randrange(1, 40), rng.randrange(1, 60), rng.choice((0, rng.randint(-10, 10), rng.randint(-300, 300)))
        e.mu.mem_write(game.rows + mid * 0x1a4 + 0x9a, bytes([vel, rng.randrange(0, 12), maxv, 0]) + (accel & 0xFFFF).to_bytes(2, "little"))
    shooter = game.unit(63, *case["from"])
    if not shooter: return None
    src, sx, sy, _ = shooter
    case["from"] = (sx, sy)
    case["foes"], foe = [], None
    if rng.random() < 0.5:
        tx, ty = case["to"]
        t = rng.random()
        fx, fy = round(sx + (tx - sx) * t) + rng.randint(-2, 2), round(sy + (ty - sy) * t) + rng.randint(-2, 2)
        r = game.room_at(fx, fy)
        if r and game.collision(fx, fy) & 0x1f0f == 0:
            foe = game.unit(rng.choice((19, 63, 58, 2, 5, 38, 156)), fx, fy)   # sizes 1..3
            if foe: case["foes"] = [foe[1:]]
    game.foe = foe[0] if foe else 0
    case.update(vel=game.row(mid, 0x9a, 1), vel_lev=game.row(mid, 0x9b, 1), max_vel=game.row(mid, 0x9c, 1),
                accel=game.row(mid, 0x9e, 2), activate=game.row(mid, 0x136, 1))
    p = e.alloc(0x60)
    index = rng.randrange(4)
    fields = {0: 0x21, 1: src, 4: mid, 5: sx, 6: sy, 7: case["to"][0], 8: case["to"][1], 0xc: case["level"]}
    if case["name"] == "chargedbolt": fields.update({0x15: 0x5c9290, 0x16: index})
    for i, v in fields.items(): e.w32(p + 4 * i, v)
    m = e.call(0x59fa30, ecx=game.g, edx=p)
    e.mu.mem_write(game.rows + mid * 0x1a4 + 0x9a, saved)
    if not m: return None
    path = e.r32(m + 0x2c)
    game_range = e.s32(e.r32(m + 0x14) + 0x10) << 16 >> 16
    case["range"] = missile_range(game.row(mid, 0x96, 2), game.row(mid, 0x98, 2), case["level"])
    if case["name"] == "chargedbolt": case["range"] = min(case["range"], 0x4d)        # FUN_005c9290
    if case["range"] != game_range: print(f"range {case['name']} lvl {case['level']}: game {game_range} port {case['range']}")
    case["rooms"] = Rooms(game.rect, game.near, e.call(0x620bb0, src))
    tx, ty = case["to"]
    if (tx, ty) == (sx, sy): case["to"] = (tx + 1, ty + 1)                              # FUN_0059fa30: never at itself
    if case["name"] == "chargedbolt":
        case["points"] = wiggle_points(sx, sy, *case["to"], case["range"], [(case["to"][0] + index) & 0xFFFFFFFF, 666])
        case["seed"] = index
    fn = e.r32(0x73c768 + 4 * game.row(mid, 0xc, 2))
    game.hit.clear()
    track = []
    for _ in range(200):
        e.call(fn, ecx=game.g, edx=m)
        track.append((e.r32(path), e.r32(path + 4)))
        if game.hit: break
    hit = game.hit[0] if game.hit else (0, -1)
    if hit[0]: end = (len(track), "unit", 0)
    else: end = (len(track), "range" if e.s32(e.r32(m + 0x14) + 0x10) << 16 >> 16 < 1 else "wall")
    return track, end


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    cases = int(args[0]) if args else 300
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    game = Game()
    bad = done = 0
    ends = {}
    dumped = []
    for _ in range(cases):
        case = make_case(game, rng)
        got = run_case(game, case, rng)
        if got is None: continue
        done += 1
        blocked = lambda x, y: bool(game.collision(x, y) & 0x184 & 5) or game.collision(x, y) == 0xFFFF
        want = fly(game.table, case, blocked)
        key = (case["name"], got[1][1])
        ends[key] = ends.get(key, 0) + 1
        if got != want:
            bad += 1
            if bad <= 5:
                print(f"mismatch {case['name']} lvl {case['level']} {case['from']} -> {case['to']} foes {case['foes']}")
                for i, (a, b) in enumerate(zip(got[0], want[0])):
                    if a != b:
                        print(f"  frame {i}: game {a[0] / 65536:.4f},{a[1] / 65536:.4f}  port {b[0] / 65536:.4f},{b[1] / 65536:.4f}")
                        break
                print(f"  game end {got[1]} ({len(got[0])} frames)  port end {want[1]} ({len(want[0])} frames)")
        elif "--dump" in sys.argv and want[1][1] != "wall" and sum(c[0] == key for c in dumped) < 2 and key not in {c[0] for c in dumped if c[1][1][1] == want[1][1]}:
            dumped.append((key, want, case))
    for key, (track, end), case in dumped:
        h = 0x811C9DC5
        for x, y in track:
            for v in (x, y):
                for k in range(4): h = ((h ^ ((v >> (8 * k)) & 0xFF)) * 0x01000193) & 0xFFFFFFFF
        foe = case["foes"][0] if case["foes"] else (0, 0, 0)
        wig = case.get("seed", -1) if case.get("points") is not None else -1
        print(f"    {{ {case['from'][0]}, {case['from'][1]}, {case['to'][0]}, {case['to'][1]}, {case['vel']}, {case['vel_lev']}, {case['max_vel']}, "
              f"{case['accel']}, {case['level']}, {case['range']}, {case['activate']}, {wig}, {foe[0]}, {foe[1]}, {foe[2]}, "
              f"{end[0]}, {int(end[1] == 'unit')}, {track[-1][0]:#x}u, {track[-1][1]:#x}u, {h:#010x}u }},   // {case['name']} lvl {case['level']}")
    print(" ".join(f"{name}:{kind}={n}" for (name, kind), n in sorted(ends.items())))
    print(f"{done} flights, {bad} mismatches" if bad else f"ok ({done} flights)")


if __name__ == "__main__":
    main()
