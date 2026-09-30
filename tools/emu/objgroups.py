"""game.exe's random object groups (FUN_00552610) placed for a map seed and level.

    uv run python objgroups.py <map seed> [level]

A fresh game at the seed (game seed {seed, 666} stepped for the monster
regions, FUN_00546c60's object seed), the level's act made, every room of
the level brought up (FUN_0061b730, level-list order), then one populate
pass the way FUN_0052d160 walks the act's room1 list (newest first):
FUN_0054f060's seed step, the presets (FUN_005559a0), the object groups.
The monsters (FUN_0054ec90) are left out: this is the placement a level
gets before any of them stand in its rooms.

One line per room: its cell, the room seed going into 552610 and after,
and each object it made (FUN_00555230: objects.txt id @ level subtile);
then the object seed after the last room.
"""
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

import drlg


def dump(e, seed, lid):
    game = e.alloc(0x2000)
    state = seed * 0x6ac690c5 + 666                         # FUN_00547d20: the regions' step
    e.w32(game + 0xd0, state & 0xffffffff); e.w32(game + 0xd4, state >> 32)
    e.w32(game + 0x7c, seed); e.w32(game + 0x84, 1); e.w32(game + 0x70, 1)
    e.call(0x5479c0, e.alloc(8), state & 0xffffffff, 0, 1, ecx=0, edx=game + 0xf0)   # monster regions (presets count into them)
    e.call(0x546c60, ecx=game)                               # objrgn: the object seed
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
        made.append((mu.reg_read(UC_X86_REG_ECX), mu.reg_read(UC_X86_REG_EDX), e.s32(sp + 4) - x0 * 5, e.s32(sp + 8) - y0 * 5))
    hook = e.mu.hook_add(UC_HOOK_CODE, on_make, begin=0x555230, end=0x555230)
    out = []
    room1 = e.call(0x61a180, act)
    while room1:
        r = e.r32(room1 + 0x10)
        e.call(0x54f060, ecx=game, edx=room1)
        e.call(0x5559a0, ecx=game, edx=room1)
        pre, n = e.r32(room1 + 0x6c), len(made)
        e.call(0x552610, ecx=game, edx=room1)
        objs = "".join(f" {i}@{x},{y}" for t, i, x, y in made[n:])
        out.append(f"room {e.s32(r + 0x34) - x0},{e.s32(r + 0x38) - y0} seed {pre:08x} post {e.r32(room1 + 0x6c):08x}{objs}")
        room1 = e.r32(room1 + 0x7c)
    e.mu.hook_del(hook)
    out.append(f"rgn {e.r32(e.r32(game + 0x10f0)):08x}")
    return "\n".join(out)


if __name__ == "__main__":
    print(dump(drlg.boot(), int(sys.argv[1], 0), int(sys.argv[2]) if len(sys.argv) > 2 else 2))
