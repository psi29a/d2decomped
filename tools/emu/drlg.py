# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's DRLG as the oracle: generate an act for a map seed, dump a level.

    uv run python drlg.py <map seed> [level]     # prints the same dump as tools/drlg-dump

Struct offsets (docs/research/re/drlg.md): act +0x47c level list (next
+0x1ac); level +0x10 rooms (next +0x24), +0x14 outdoor record, +0x1c
{x, y, w, h}, +0x1c4 seed, +0x1d0 id; room +0x04 seed low, +0x34 {x, y,
w, h}, +0x48 kind (1 plain, 2 preset); outdoor record grids at +0x04,
+0x18, +0x2c, +0x40 ({cells, rows, w, h}).
"""
import sys

import emu

_e = None


def boot(fresh=False):
    """game.exe with every data table loaded (FUN_00619300, presets and LvlSub DS1s too)."""
    global _e
    if _e is None or fresh:
        _e = emu.Emu()
        emu.serve_files(_e)
        _e.crt_init()
        if _e.call(0x619300, 0, 1, 1) != 0: raise SystemExit("drlg: data tables failed to load")
    return _e


def alloc_act(e, act, seed, level=0, difficulty=0):
    """FUN_006194a0 (stdcall: act, seed, client, game, difficulty, pool, level to build, fn, fn), server side;
    its +0x48 is the DRLG act (FUN_00642da0)."""
    return e.r32(e.call(0x6194a0, act, seed, 0, 0, difficulty, 0, level, 0, 0) + 0x48)


def levels(e, act):
    l = e.r32(act + 0x47c)
    while l:
        yield l
        l = e.r32(l + 0x1ac)


def find_level(e, act, lid):
    return next((l for l in levels(e, act) if e.r32(l + 0x1d0) == lid), 0)


def grid(e, g):
    cells, rows, w, h = (e.r32(g + 4 * i) for i in range(4))
    return w, h, [[e.r32(cells + 4 * (e.r32(rows + 4 * y) + x)) for x in range(w)] for y in range(h)]


def dump(e, lvl):
    s32 = e.s32
    x, y, w, h = (s32(lvl + 0x1c + 4 * i) for i in range(4))
    out = [f"level {e.r32(lvl + 0x1d0)} at {x},{y} size {w}x{h}"]
    o = e.r32(lvl + 0x14)
    if e.r32(lvl) == 3 and o:
        out.append(f"flags {e.r32(o):x}")
        for name, off in (("g04", 0x04), ("g18", 0x18), ("g2c", 0x2c)):
            gw, gh, cells = grid(e, o + off)
            out.append(f"{name} {gw}x{gh}")
            out += [" ".join(f"{c:x}" for c in row) for row in cells]
    rooms = []
    r = e.r32(lvl + 0x10)
    while r:
        rx, ry, rw, rh = (s32(r + 0x34 + 4 * i) for i in range(4))
        extra = ""
        if e.r32(lvl) in (1, 2) and e.r32(r + 0x48) == 2:   # a maze or preset level: which preset, file, and where it starts
            pi = e.r32(e.r32(r + 0x20) + 8)
            extra = f" def {e.r32(pi)} file {e.s32(pi + 4)} at {e.s32(pi + 0x10) - x},{e.s32(pi + 0x14) - y}"
        rooms.append((ry - y, rx - x, rw, rh, e.r32(r + 0x48), e.r32(r + 4), extra))
        r = e.r32(r + 0x24)
    out.append(f"rooms {len(rooms)}")
    out += [f"{rx},{ry} {rw}x{rh} kind {k} seed {sd:08x}{ex}" for ry, rx, rw, rh, k, sd, ex in sorted(rooms)]
    return "\n".join(out)


def dt1_index(e):
    """tile header address -> (DT1 path, tile index), over every DT1 game.exe has loaded (list at 0x8adbb4)."""
    ranges, libs = [], {}
    n = e.r32(0x8adbb4)
    while n:
        buf = e.r32(n + 0x104)
        ranges.append((e.r32(buf + 0x110), e.r32(buf + 0x10c), e.cstr(n)))
        libs[e.r32(n + 0x108)] = e.cstr(n).split("\\")[-1].lower()
        n = e.r32(n + 0x10c)
    def find(p):
        if p in libs: return libs[p], -1
        for base, count, name in ranges:
            if base <= p < base + 0x60 * count: return name.split("\\")[-1].lower(), (p - base) // 0x60
        return "?", p
    return find


def tiles_dump(e, lvl):
    """Bring every room up (FUN_0061b730) and list its tiles: room +0x54 -> walls (+0x08 count, +0x14 array),
    floors (+0x0c, +0x1c), shadows (+0x10, +0x24); 0x30-byte entries {.., +8 x, +0xc y, .., +0x18 tile, +0x1c orientation}."""
    x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
    rooms = []
    r = e.r32(lvl + 0x10)
    while r:
        e.call(0x61b730, ecx=r)
        rooms.append(r)
        r = e.r32(r + 0x24)
    find = dt1_index(e)
    out = []
    for r in sorted(rooms, key=lambda r: (e.s32(r + 0x38), e.s32(r + 0x34))):
        t = e.r32(r + 0x54)
        dt1s = [find(e.r32(r + 0x68 + 4 * i))[0] for i in range(32) if e.r32(r + 0x68 + 4 * i)]
        out.append(f"room {e.s32(r + 0x34) - x0},{e.s32(r + 0x38) - y0} mask {e.r32(r + 0x50):x} dt1s {' '.join(dt1s)}")
        for name, cnt, arr in (("wall", 0x08, 0x14), ("floor", 0x0c, 0x1c), ("shadow", 0x10, 0x24)):
            a = e.r32(t + arr)
            for i in range(e.r32(t + cnt)):
                en = a + 0x30 * i
                f, k = find(e.r32(en + 0x18))
                out.append(f" {name} {e.s32(en + 8)},{e.s32(en + 12)} o{e.r32(en + 0x1c)} {f}:{k}")
    return "\n".join(out)


def units_dump(e, lvl):
    """Each room's units (room +0x5c, next +0xc) once rooms are up (tiles_dump brings them), list order:
    type +0x14 : id +4, mode +0, room-relative subtiles x +8 / y +0x18, flags +0x1c."""
    x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
    rooms = []
    r = e.r32(lvl + 0x10)
    while r:
        units = []
        u = e.r32(r + 0x5c)
        while u:
            units.append(f"{e.s32(u + 0x14)}:{e.s32(u + 4)} m{e.s32(u)} {e.s32(u + 8)},{e.s32(u + 0x18)} f{e.r32(u + 0x1c):x}")
            u = e.r32(u + 0xc)
        if units: rooms.append((e.s32(r + 0x38) - y0, e.s32(r + 0x34) - x0, " ".join(units)))
        r = e.r32(r + 0x24)
    return "\n".join(f"units {x},{y}: {us}" for y, x, us in sorted(rooms))


def seeds_dump(e, lvl):
    """Each room's room1 seed (room +0x30 -> +0x6c) once rooms are up (tiles_dump brings them)."""
    x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
    rooms = []
    r = e.r32(lvl + 0x10)
    while r:
        rooms.append((e.s32(r + 0x38) - y0, e.s32(r + 0x34) - x0, e.r32(e.r32(r + 0x30) + 0x6c)))
        r = e.r32(r + 0x24)
    return "\n".join(f"room1 {x},{y} seed {s:08x}" for y, x, s in sorted(rooms))


def walk_order(n, seed, kind):
    """A room order (list indices) to bring a level up in, as drlg-dump's: list, reverse, or shuffled on the seed."""
    order = list(range(n))
    if kind == "reverse": order.reverse()
    elif kind == "shuffle":
        x = seed
        for i in range(n - 1, 0, -1):
            x = (x * 1103515245 + 12345) & 0x7fffffff
            j = x % (i + 1)
            order[i], order[j] = order[j], order[i]
    return order


def bring_up(e, lvl, seed, kind):
    """The level's rooms brought up (FUN_0061b730) in walk_order; returns the room list (list order)."""
    rooms = []
    r = e.r32(lvl + 0x10)
    while r:
        rooms.append(r)
        r = e.r32(r + 0x24)
    for i in walk_order(len(rooms), seed, kind): e.call(0x61b730, ecx=rooms[i])
    return rooms


def collision_dump(e, seed, lid, kind="shuffle"):
    """Bring the level's rooms up (FUN_0061b730: tiles, then room1 and its grid, FUN_0064c900) in walk_order,
    then each room's collision (room +0x30 -> room1 +0x20: {x, y, w, h, ...} subtiles, u16 cells at +0x24), (y, x) order."""
    act = alloc_act(e, 0, seed, lid)
    lvl = find_level(e, act, lid)
    if not lvl: raise SystemExit(f"drlg: level {lid} not built")
    x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
    rooms = bring_up(e, lvl, seed, kind)
    out = []
    for r in sorted(rooms, key=lambda r: (e.s32(r + 0x38), e.s32(r + 0x34))):
        c = e.r32(e.r32(r + 0x30) + 0x20)
        w, h = e.s32(c + 8), e.s32(c + 12)
        out.append(f"col {e.s32(r + 0x34) - x0},{e.s32(r + 0x38) - y0}")
        out += [" " + "".join(f"{e.r16(c + 0x24 + 2 * (y * w + x)):02x}" for x in range(w)) for y in range(h)]
    return "\n".join(out)


def level_dump(e, seed, lid):
    act = alloc_act(e, 0, seed, lid)
    lvl = find_level(e, act, lid)
    if not lvl: raise SystemExit(f"drlg: level {lid} not built")
    global _last_level
    _last_level = lvl
    return dump(e, lvl)


_last_level = 0

if __name__ == "__main__":
    # drlg.py <seed> [level] [tiles]  or  drlg.py <first>-<last> <level> <out dir> (one <seed>.txt each)
    lid = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    e = boot()
    if "-" in sys.argv[1]:
        a, b = (int(v, 0) for v in sys.argv[1].split("-"))
        for seed in range(a, b + 1):
            with open(f"{sys.argv[3]}/{seed}.txt", "w") as f: f.write(level_dump(e, seed, lid) + "\n")
    else:
        print(level_dump(e, int(sys.argv[1], 0), lid))
        if len(sys.argv) > 3 and sys.argv[3] in ("tiles", "units"):
            t = tiles_dump(e, _last_level)
            print(t if sys.argv[3] == "tiles" else units_dump(e, _last_level))
