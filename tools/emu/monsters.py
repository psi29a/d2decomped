"""game.exe's room population as the oracle: which monsters each room of a level spawns.

    uv run python monsters.py <map seed> [level] [difficulty]

A game (FUN_00530930's part that matters: seeds, regions, object seed,
sunitproxy, quests), act 1 allocated as the server does (FUN_0053ac70),
every room of the level brought up (FUN_0061b730, level-list order), then
each room's pass (FUN_0052d0f0: presets, FUN_00542b40, object groups,
FUN_0054ec90) the way FUN_0052d160 walks the act's room1 list (newest
first), as objgroups.py does.
"""
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EAX

import drlg
import emu

emu.Emu.w_IsBadCodePtr = lambda self: (0, 1)      # quests' init tests its callbacks


def new_game(e, seed, difficulty=0):
    g = e.alloc(0x2000)
    e.mu.mem_write(g + 0x6d, bytes([difficulty]))
    e.w32(g + 0x70, 1)                      # expansion
    e.mu.mem_write(g + 0x78, struct.pack("<H", 101))
    e.w32(g + 0x7c, seed)                   # FUN_0052c280 with -seed
    e.w32(g + 0x84, 1)
    e.w32(g + 0xd0, seed)
    e.w32(g + 0xd4, 666)
    e.call(0x541470, ecx=g)
    e.call(0x53f4b0, 0x100000 | difficulty << 12, 0, ecx=g, edx=0)
    e.call(0x5379a0, ecx=g)
    e.call(0x53ff90, ecx=g)
    e.call(0x547d20, ecx=g)                 # regions
    e.w32(g + 0x80, e.call(0x546c60, ecx=g))   # object seed
    e.call(0x536070, ecx=g)                 # sunitproxy
    e.call(0x545d80, ecx=g)                 # quests
    return g


def act_of(e, g, lid, act=0):
    """FUN_0053ac70's FUN_006194a0 for the game, into game +0xbc, building `lid` (the server builds the town)."""
    a = e.call(0x6194a0, act, e.r32(g + 0x7c), 0, g, e.read(g + 0x6d, 1)[0], e.r32(g + 0x1c), lid, 0, 0)
    e.w32(g + 0xbc + 4 * act, a)
    return a


def dump(e, seed, lid, difficulty=0):
    """One line per room with monsters, in population order: `mon <x>,<y>: <class>@<x>,<y>[m<mode>]/<leader> ...`,
    level-relative tiles for the room and subtiles for the monsters; <leader> is the index (in the line) of
    the first monster its placement made: a preset unit (FUN_00555910), a group (FUN_0054df80) or a boss
    and its company (FUN_005a43e0 from FUN_0054ec90). Flavie (266) is left out: an NPC, not a spawn."""
    g = new_game(e, seed, difficulty)
    act = act_of(e, g, lid)
    lvl = drlg.find_level(e, e.r32(act + 0x48), lid)
    if not lvl: raise SystemExit(f"monsters: level {lid} not built")
    x0, y0 = e.s32(lvl + 0x1c) * 5, e.s32(lvl + 0x20) * 5
    made, lead = [], [0]
    def on_make(mu, addr, size, _):       # FUN_00555230(ECX type, EDX class; x, y, game, room1, 1, mode, flags)
        sp = mu.reg_read(UC_X86_REG_ESP)
        if mu.reg_read(UC_X86_REG_ECX) == 1 and mu.reg_read(UC_X86_REG_EDX) != 266:
            made.append((mu.reg_read(UC_X86_REG_EDX), e.s32(sp + 4) - x0, e.s32(sp + 8) - y0, e.s32(sp + 0x14), lead[0]))
    def on_group(mu, addr, size, _):
        ret = e.r32(mu.reg_read(UC_X86_REG_ESP))
        if addr != 0x5a43e0 or 0x54ec90 <= ret < 0x54ef42: lead[0] = len(made)
    hooks = [e.mu.hook_add(UC_HOOK_CODE, on_make, begin=0x555230, end=0x555230)]
    hooks += [e.mu.hook_add(UC_HOOK_CODE, on_group, begin=a, end=a) for a in (0x555910, 0x54df80, 0x5a43e0)]
    rooms = set()
    r = e.r32(lvl + 0x10)
    while r:
        e.call(0x61b730, ecx=r)             # every room up first (room1, tiles, collision), level-list order
        rooms.add(r)
        r = e.r32(r + 0x24)
    out = []
    try:
        room1 = e.call(0x61a180, act)       # then FUN_0052d160's walk: the act's room1 list, newest first
        while room1:
            r = e.r32(room1 + 0x10)
            if r in rooms:
                del made[:]
                e.call(0x52d0f0, ecx=g, edx=room1)
                if made:
                    out.append(f"mon {e.s32(r + 0x34) - x0 // 5},{e.s32(r + 0x38) - y0 // 5}: "
                               + " ".join(f"{c}@{x},{y}{'' if m == 1 else f'm{m}'}/{l}" for c, x, y, m, l in made))
            room1 = e.r32(room1 + 0x7c)
    finally:
        for h in hooks: e.mu.hook_del(h)
    return "\n".join(out)


if __name__ == "__main__":
    seed = int(sys.argv[1], 0)
    lid = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    print(dump(drlg.boot(), seed, lid, int(sys.argv[3]) if len(sys.argv) > 3 else 0))
