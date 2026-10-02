# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's random object groups (FUN_00552610) placed for a map seed and level.

    uv run python objgroups.py <map seed> [level]

A fresh game at the seed (game seed {seed, 666} stepped for the monster
regions, FUN_00546c60's object seed), the level's act made, every room of
the level brought up (FUN_0061b730, level-list order), then one populate
pass the way FUN_0052d160 walks the act's room1 list (newest first):
FUN_0054f060's seed step, the presets (FUN_005559a0), the object groups,
then the monsters (FUN_0054ec90) as FUN_0052d0f0 runs them: their
footprints (0x100) block the next rooms' groups, so the object seed moves
with them.

One line per room: its cell, the room seed going into 552610 and after,
the object seed after the room,
and each object it made (FUN_00555230: objects.txt id @ level subtile);
then the object seed after the last room.

`drops`: then, after the last room, three items dropped at each group
object in turn (FUN_00555da0 from its room1, each landing stamped 0x200
into the room's collision as the item's own would be): "drop x,y" per item,
level subtiles.
"""
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

import drlg
import drops as drops_oracle
import monsters

LEVELS = "2,8,4,9"                                          # game: Blood Moor, Den of Evil, Stony Field, Cave 1 (none touching)
TOWNS = {1, 40, 75, 103, 109}                               # a camp room's line leaves out its seeds (d2d doesn't keep them)
CONTAINERS = {1, 3, 4, 5, 14, 26}                           # OperateFn: casket, urn, chest, barrel, corpse / crate, bookshelf


def dump(e, seed, lid, drops=False):
    game = e.alloc(0x2000)
    state = seed * 0x6ac690c5 + 666                         # FUN_00547d20: the regions' step
    e.w32(game + 0xd0, state & 0xffffffff); e.w32(game + 0xd4, state >> 32)
    e.w32(game + 0x7c, seed); e.w32(game + 0x84, 1); e.w32(game + 0x70, 1)
    e.call(0x5479c0, e.alloc(8), state & 0xffffffff, 0, 1, ecx=0, edx=game + 0xf0)   # monster regions (presets count into them)
    e.call(0x546c60, ecx=game)                               # objrgn: the object seed
    e.call(0x536070, ecx=game)                               # sunitproxy (a game-seed step)
    e.call(0x545d80, ecx=game)                               # the quests (the Cairn Stones' preset asks for its record)
    e.call(0x541470, ecx=game)                               # the event list
    act = e.call(0x6194a0, 0, seed, 0, game, 0, 0, lid, 0, 0)
    e.w32(game + 0xbc, act)
    lvl = drlg.find_level(e, e.r32(act + 0x48), lid)
    if not lvl: raise SystemExit(f"objgroups: level {lid} not built")
    x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
    r = e.r32(lvl + 0x10)
    while r:
        e.call(0x61b730, ecx=r)
        r = e.r32(r + 0x24)
    made = []
    def on_make(mu, addr, size, _):
        sp = mu.reg_read(UC_X86_REG_ESP)
        made.append((mu.reg_read(UC_X86_REG_ECX), mu.reg_read(UC_X86_REG_EDX), e.s32(sp + 4) - x0 * 5, e.s32(sp + 8) - y0 * 5, here[0]))
    hook = e.mu.hook_add(UC_HOOK_CODE, on_make, begin=0x555230, end=0x555230)
    out, here, spots = [], [0], []
    room1 = e.call(0x61a180, act)
    while room1:
        r = e.r32(room1 + 0x10)
        e.call(0x54f060, ecx=game, edx=room1)
        e.call(0x5559a0, ecx=game, edx=room1)
        pre, n = e.r32(room1 + 0x6c), len(made)
        here[0] = room1
        e.call(0x552610, ecx=game, edx=room1)
        spots += made[n:]
        objs = "".join(f" {i}@{x},{y}" for t, i, x, y, _ in made[n:])
        out.append(f"room {e.s32(r + 0x34) - x0},{e.s32(r + 0x38) - y0} seed {pre:08x} post {e.r32(room1 + 0x6c):08x} rgn {e.r32(e.r32(game + 0x10f0)):08x}{objs}")
        e.call(0x54ec90, ecx=game, edx=room1)               # the room's monsters, after its line
        room1 = e.r32(room1 + 0x7c)
    e.mu.hook_del(hook)
    out.append(f"rgn {e.r32(e.r32(game + 0x10f0)):08x}")
    if drops:
        rooms = []                                          # each room1's collision (room1 +0x20): x, y, w, h, cells
        room1 = e.call(0x61a180, act)
        while room1:
            c = e.r32(room1 + 0x20)
            if c and e.r32(c + 0x20): rooms.append([e.s32(c + 4 * k) for k in range(4)] + [e.r32(c + 0x20)])
            room1 = e.r32(room1 + 0x7c)
        xy, spot = e.alloc(8), e.alloc(8)
        for _, _, x, y, r1 in spots:
            for _ in range(3):
                e.w32(xy, x + x0 * 5); e.w32(xy + 4, y + y0 * 5)
                e.call(0x555da0, spot, 1, 1, ecx=r1, edx=xy)
                sx, sy = e.s32(spot), e.s32(spot + 4)
                for cx, cy, cw, ch, cells in rooms:
                    if cx <= sx < cx + cw and cy <= sy < cy + ch:
                        a = cells + ((sy - cy) * cw - cx + sx) * 2
                        e.mu.mem_write(a, (int.from_bytes(e.mu.mem_read(a, 2), "little") | 0x200).to_bytes(2, "little"))
                        break
                out.append(f"drop {sx - x0 * 5},{sy - y0 * 5}")
    return "\n".join(out)


def openers(e):
    """drops.py's Oracle and Objects (their hooks off: `swap(True)` puts them in for an open, `swap(False)` takes them out)."""
    if getattr(e, "openers", None): return e.openers
    saved, put = {}, e.hook
    def keep(addr, fn, n):
        saved.setdefault(addr, bytes(e.mu.mem_read(addr, 5)))
        put(addr, fn, n)
    e.hook = keep
    try: o = drops_oracle.Oracle(); objects = drops_oracle.Objects(o)
    finally: del e.hook
    hooked = {a: bytes(e.mu.mem_read(a, 5)) for a in saved}
    def swap(on):
        for a in saved:
            e.mu.mem_write(a, hooked[a] if on else saved[a])
            e.mu.ctl_remove_cache(a, a + 5)                 # unicorn keeps the code it translated
    swap(False)
    e.openers = (o, objects, swap)
    return e.openers


def step(e, at, n):
    """n steps of the seed at `at` ({low, high}, the 0x6ac690c5 LCG)."""
    low, high = e.r32(at), e.r32(at + 4)
    for _ in range(n):
        x = low * 0x6ac690c5 + high
        low, high = x & 0xffffffff, x >> 32
    e.w32(at, low); e.w32(at + 4, high)


def game_dump(e, seed, levels, kind="shuffle"):
    """One game: each level in turn made, its rooms brought up (FUN_0061b730) in drlg.walk_order, populated as
    FUN_0052d160 walks the act's room1 list (newest first: FUN_0052d0f0), then every container its rooms made
    opened in the order made (drops.py Objects, on the game's object seed). Per room: its room1 seed before
    FUN_0054f060, the room seed into 552610 and after, the object seed and the game seed's low after, every
    object made (id@x,y); a town's rooms without the seeds. Per container: what dropped | extras, then the object seed and its unit seed after;
    each item made (drops and extras) steps the game seed twice (FUN_00555230). The object seed's full state
    (low:high) and the game seed's after each level."""
    o, objects, swap = openers(e)
    g = monsters.new_game(e, seed)
    act = monsters.act_of(e, g, levels[0])
    dact = e.r32(act + 0x48)
    rgn = e.r32(g + 0x10f0)
    made, pre, out = [], [0, 0], []
    def on_make(mu, addr, size, _):                         # FUN_00555230(ECX type, EDX class; x, y, ...)
        sp = mu.reg_read(UC_X86_REG_ESP)
        made.append([mu.reg_read(UC_X86_REG_ECX), mu.reg_read(UC_X86_REG_EDX), e.s32(sp + 4), e.s32(sp + 8), 0])
    def on_seed(mu, addr, size, _):                         # FUN_00552df0(ECX unit): the unit just made
        if made and not made[-1][4]: made[-1][4] = mu.reg_read(UC_X86_REG_ECX)
    def on_groups(mu, addr, size, _):
        pre[0] = e.r32(mu.reg_read(UC_X86_REG_EDX) + 0x6c)
    def on_monsters(mu, addr, size, _):
        pre[1] = e.r32(mu.reg_read(UC_X86_REG_EDX) + 0x6c)
    hooks = [e.mu.hook_add(UC_HOOK_CODE, fn, begin=a, end=a) for fn, a in ((on_make, 0x555230), (on_seed, 0x552df0), (on_groups, 0x552610), (on_monsters, 0x54ec90))]
    try:
        for lid in levels:
            lvl = drlg.find_level(e, dact, lid) or e.call(0x642ae0, ecx=dact, edx=lid)
            if not e.r32(lvl + 0x10): e.call(0x6424a0, lvl)
            x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
            rooms = []
            r = e.r32(lvl + 0x10)
            while r:
                rooms.append(r)
                r = e.r32(r + 0x24)
            head = e.call(0x61a180, act)
            for i in drlg.walk_order(len(rooms), seed, kind): e.call(0x61b730, ecx=rooms[i])
            out.append(f"level {lid}")
            mine = []
            room1 = e.call(0x61a180, act)
            while room1 and room1 != head:
                r = e.r32(room1 + 0x10)
                r1 = e.r32(room1 + 0x6c)
                del made[:]
                pre[:] = [0, 0]
                e.call(0x52d0f0, ecx=g, edx=room1)
                objs = [m for m in made if m[0] == 2]
                mine += objs
                seeds = "" if lid in TOWNS else f" r1 {r1:08x} seed {pre[0]:08x} post {pre[1]:08x}"
                out.append(f"room {e.s32(r + 0x34) - x0},{e.s32(r + 0x38) - y0}{seeds} rgn {e.r32(rgn):08x} game {e.r32(g + 0xd0):08x}"
                           + "".join(f" {i}@{x - x0 * 5},{y - y0 * 5}" for _, i, x, y, _ in objs))
                room1 = e.r32(room1 + 0x7c)
            swap(True)
            try:
                for _, cls, x, y, unit in mine:
                    op = objects_op(e, cls)
                    if op not in CONTAINERS or not unit: continue
                    fn = {1: 0x586410, 3: 0x5866c0, 4: 0x585f60, 5: 0x5868a0, 14: 0x5867a0, 26: 0x584060}[op]
                    e.mu.mem_write(o.game + 0x6d, b"\0")
                    ctx = e.alloc(0x14)
                    for k, v in enumerate((o.game, unit, 0, rgn, cls)): e.w32(ctx + 4 * k, v)
                    objects.lid, objects.extra, o.made, o.players, o.mf = lid, [], [], 1, 0
                    e.call(fn, ecx=ctx)
                    step(e, g + 0xd0, 2 * (len(o.made) + len(objects.extra)))
                    items = "".join(f" {c}:{q}" + (f"*{m}" if m else "") for c, q, m in o.made)
                    shut = " shut" if op == 1 and not o.made else ""
                    out.append(f"open {cls}@{x - x0 * 5},{y - y0 * 5}:{items} |" + "".join(f" {t}" for t in objects.extra)
                               + f"{shut} -> {e.r32(rgn):08x} {e.r32(unit + 0x20):08x}")
            finally:
                swap(False)
            out.append(f"rgn {e.r32(rgn):08x}:{e.r32(rgn + 4):08x} game {e.r32(g + 0xd0):08x}:{e.r32(g + 0xd4):08x}")
    finally:
        for h in hooks: e.mu.hook_del(h)
    return "\n".join(out)


_ops = {}
def objects_op(e, cls):
    if not _ops:
        for r in drops_oracle.txt_rows(e, "objects"):
            if r.get("Id", "").isdigit(): _ops[int(r["Id"])] = int(r.get("OperateFn") or 0)
    return _ops.get(cls, 0)


if __name__ == "__main__":
    if sys.argv[-1] == "game":
        import os
        print(game_dump(drlg.boot(), int(sys.argv[1], 0), [int(v) for v in os.environ.get("LEVELS", LEVELS).split(",")], os.environ.get("ORDER", "shuffle")))
        raise SystemExit
    print(dump(drlg.boot(), int(sys.argv[1], 0), int(sys.argv[2]) if len(sys.argv) > 2 else 2, sys.argv[-1] == "drops"))
