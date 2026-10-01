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


if __name__ == "__main__":
    print(dump(drlg.boot(), int(sys.argv[1], 0), int(sys.argv[2]) if len(sys.argv) > 2 else 2, sys.argv[-1] == "drops"))
