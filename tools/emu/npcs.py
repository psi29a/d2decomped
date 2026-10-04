# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's town NPC think (MonAI Npc, FUN_005e7130) as the oracle.

    uv run python npcs.py <map seed> [thinks]

A fresh game at the seed, the Rogue Encampment's rooms brought up in list
order and populated (FUN_0052d0f0, newest first), then each preset monster
the DS1 gave a path (FUN_00555910 hands it to AI control +0x38) thinks
`thinks` times through the driver FUN_005b1740. The think's effects are
hooked: stand n (FUN_005de080), walk to x,y (FUN_005def30), a mode
(FUN_005ddf90), a facing (FUN_00648820). After a walk the NPC is put at the
spot, or on every other walk 4 subtiles short on x (the next think walks
again); a mode it set has ended by the next think. One line per NPC: its
class, spot, unit seed and path (action:x,y), then the thinks' effects in
turn, level subtiles; lines sorted (drlg-dump ... npcs prints the same).
"""
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

import drlg
import monsters

THINKS = 40


def dump(e, seed, thinks=THINKS):
    g = monsters.new_game(e, seed)
    act = monsters.act_of(e, g, 1)
    lvl = drlg.find_level(e, e.r32(act + 0x48), 1)
    x0, y0 = e.s32(lvl + 0x1c) * 5, e.s32(lvl + 0x20) * 5
    made = []
    def on_made(mu, addr, size, _):                         # FUN_00555910 after FUN_005557d0: EAX the unit
        made.append(mu.reg_read(UC_X86_REG_EAX))
    hook = e.mu.hook_add(UC_HOOK_CODE, on_made, begin=0x55594e, end=0x55594e)
    try:
        r = e.r32(lvl + 0x10)
        while r:
            e.call(0x61b730, ecx=r)
            r = e.r32(r + 0x24)
        room1 = e.call(0x61a180, act)
        while room1:
            e.call(0x52d0f0, ecx=g, edx=room1)
            room1 = e.r32(room1 + 0x7c)
    finally:
        e.mu.hook_del(hook)
    out = []
    for unit in made:
        if not unit or e.r32(unit) != 1: continue
        ctrl = e.r32(e.r32(unit + 0x14) + 0x28)
        if not ctrl or not e.r32(ctrl + 0x38): continue
        path = e.r32(unit + 0x2c)
        def at(): return int.from_bytes(e.mu.mem_read(path + 2, 2), "little"), int.from_bytes(e.mu.mem_read(path + 6, 2), "little")
        def put(x, y):
            e.mu.mem_write(path + 2, x.to_bytes(2, "little")); e.mu.mem_write(path + 6, y.to_bytes(2, "little"))
        x, y = at()
        points = e.r32(ctrl + 0x38)                         # {count, entries}: 12-byte {action, x, y}
        line = [f"npc {e.r32(unit + 4)} @{x - x0},{y - y0} seed {e.r32(unit + 0x20):08x} path"
                + "".join(f" {e.r32(e.r32(points + 4) + 12 * k)}:{e.s32(e.r32(points + 4) + 12 * k + 4) - x0},{e.s32(e.r32(points + 4) + 12 * k + 8) - y0}"
                          for k in range(e.r32(points))) + ":"]
        walks = [0]
        def arg(k): return e.r32(e.mu.reg_read(UC_X86_REG_ESP) + 4 + 4 * k)
        def stand(_e): a = [arg(0)]; line.append(f"s{a[0]}"); e.w32(unit + 0x10, 1); return 0
        def walk(_e):
            a = [arg(0), arg(1)]
            walks[0] += 1
            short = 4 if walks[0] % 2 == 0 else 0
            line.append(f"w{a[0] - x0},{a[1] - y0}"); put(a[0] + short, a[1]); e.w32(unit + 0x10, 1); return 1
        def mode(_e): a = [arg(0)]; line.append(f"m{a[0]}"); return 1
        def face(_e): a = [arg(1)]; line.append(f"f{a[0]}"); return 0
        saved = {}
        for addr, fn, n in ((0x5de080, stand, 1), (0x5def30, walk, 2), (0x5ddf90, mode, 2), (0x648820, face, 2)):
            saved[addr] = bytes(e.mu.mem_read(addr, 5))
            e.hook(addr, fn, n)
        try:
            ctx = e.alloc(0x20)                             # FUN_005b1740's block: [0] AI control, [7] the MonStats row
            e.w32(ctx, ctrl)
            e.w32(ctx + 0x1c, e.r32(e.r32(0x744304) + 0xa78) + e.r32(unit + 4) * 0x1a8)
            for _ in range(thinks):
                e.w32(unit + 0x10, 1)
                e.call(0x5e7130, ctx, ecx=g, edx=unit)
        finally:
            for addr, code in saved.items():
                e.mu.mem_write(addr, code)
                e.mu.ctl_remove_cache(addr, addr + 5)
        out.append(" ".join(line))
    return "\n".join(sorted(out))


if __name__ == "__main__":
    print(dump(drlg.boot(), int(sys.argv[1], 0), int(sys.argv[2]) if len(sys.argv) > 2 else THINKS))
