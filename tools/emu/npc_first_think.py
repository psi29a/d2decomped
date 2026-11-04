# SPDX-License-Identifier: GPL-3.0-or-later
"""When a town NPC first thinks, by game.exe's own code.

    uv run python npc_first_think.py <map seed> [level]

A fresh game at the seed, the town's rooms brought up and populated as
npcs.py does (FUN_0052d0f0). Each preset monster is made by game.exe's
own spawn (FUN_00555910 -> FUN_005557d0 -> FUN_0054e600 -> FUN_005b2f20
-> FUN_005b2a00 -> FUN_00555230 -> FUN_00554850). No client has the
rooms yet (room +0x78 0), so FUN_00554850 takes FUN_00553220, and the
think filed is the NU mode's aidel (FUN_005a73e0). Then, AHEAD frames
on, a player's client comes into each room: FUN_0053a8e0 runs for real.
The room's first client (+0x78 under 2) sends each monster in its list
(+0x74, next +0xe8) to FUN_00573780, which files event 2 at frame + 2
for one in NU.

Hooked (not the claim): FUN_005417d0 is watched, not replaced: each
event 2 (the AI think) it files is noted as frame - game +0xa8, the
last one per unit (FUN_00540e60 drops the earlier ones). FUN_0053a8e0's
client side is stubbed: FUN_0053bc50 (the client's view), FUN_0061a660
(the client added: only +0x78 counted up), FUN_00537860 (the client's
unit: none) and FUN_00571f90 (a unit sent to the client).

One line per NPC npcs.py thinks for (a path, or a Vendor): its class,
its mode, `made` (frames to the think filed as it's made) and `in play`
(frames to the one filed as its room's first client comes). d2d:
npc_patrol files it 2 frames after it first meets the state
(UnitState::filed), as its level comes into play.
"""
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX

import drlg
import monsters
import npcs
import objgroups

AHEAD = 40                    # frames between the rooms' making and the client's coming
CLIENT_COMES = 0x53A8E0       # FUN_0053a8e0: ECX game, EDX room, stack client
QUEUE_EVENT = 0x5417D0        # FUN_005417d0: ECX game, EDX unit, stack (type, frame, 0, 0)
THINK_EVENT = 2


def dump(e, seed, lid=1):
    g = monsters.new_game(e, seed)
    act = monsters.act_of(e, g, lid, drlg.act_of(lid))
    lvl = drlg.find_level(e, e.r32(act + 0x48), lid)
    stubs = {addr: bytes(e.mu.mem_read(addr, 5)) for addr in objgroups.QUEST_STUBS}
    for addr, (args, answer) in objgroups.QUEST_STUBS.items(): e.hook(addr, lambda _e, answer=answer: answer, args)
    made, filed = [], {}

    def on_made(mu, addr, size, _):                         # FUN_00555910 after FUN_005557d0: EAX the unit
        made.append(mu.reg_read(UC_X86_REG_EAX))

    def on_queue(mu, addr, size, _):
        if e.arg(0) == THINK_EVENT:
            game, unit = mu.reg_read(UC_X86_REG_ECX), mu.reg_read(UC_X86_REG_EDX)
            filed[unit] = e.arg(1) - e.r32(game + 0xa8)
    hooks = [e.mu.hook_add(UC_HOOK_CODE, on_made, begin=0x55594e, end=0x55594e),
             e.mu.hook_add(UC_HOOK_CODE, on_queue, begin=QUEUE_EVENT, end=QUEUE_EVENT)]
    rooms = []
    try:
        r = e.r32(lvl + 0x10)
        while r:
            e.call(0x61b730, ecx=r)
            r = e.r32(r + 0x24)
        room1 = e.call(0x61a180, act)
        while room1:
            e.call(0x52d0f0, ecx=g, edx=room1)
            rooms.append(room1)
            room1 = e.r32(room1 + 0x7c)
        at_making = dict(filed)
        filed.clear()
        e.w32(g + 0xa8, e.r32(g + 0xa8) + AHEAD)

        def add_client(_e):
            room = e.arg(0)
            e.w32(room + 0x78, e.r32(room + 0x78) + 1)
            return 0
        saved = {}
        for addr, fn, n in ((0x53bc50, lambda _e: 0, 0), (0x61a660, add_client, 2), (0x537860, lambda _e: 0, 2), (0x571f90, lambda _e: 0, 1)):
            saved[addr] = bytes(e.mu.mem_read(addr, 5))
            e.hook(addr, fn, n)
        client = e.alloc(0x200)
        try:
            for room in rooms: e.call(CLIENT_COMES, client, ecx=g, edx=room)
        finally:
            for addr, code in saved.items():
                e.mu.mem_write(addr, code)
                e.mu.ctl_remove_cache(addr, addr + 5)
    finally:
        for hook in hooks: e.mu.hook_del(hook)
        for addr, code in stubs.items():
            e.mu.mem_write(addr, code)
            e.mu.ctl_remove_cache(addr, addr + 5)
    out = []
    for unit in made:
        if not unit or e.r32(unit) != 1: continue
        ctrl = e.r32(e.r32(unit + 0x14) + 0x28)
        cls = e.r32(unit + 4)
        if not ctrl or cls in npcs.HIREABLE or (cls not in npcs.VENDOR and not e.r32(ctrl + 0x38)): continue
        out.append(f"npc {cls} mode {e.r32(unit + 0x10)} made {at_making.get(unit, 'none')} in play {filed.get(unit, 'none')}")
    return "\n".join(sorted(out))


if __name__ == "__main__":
    print(dump(drlg.boot(), int(sys.argv[1], 0), int(sys.argv[2]) if len(sys.argv) > 2 else 1))
