# SPDX-License-Identifier: GPL-3.0-or-later
"""The Countess's think (FUN_005e5c50) when her walk home can't set off.

    uv run python ai_countess_home.py [--dump ../../tests/countess_home_cases.inc]

Runs game.exe's think on every scene of: out of home's room, the target out
of it, at home exactly, over 40 from home, the walk home setting off or not
(FUN_005dede0's answer), a firewall cast or not (FUN_005e5b70's answer), in
melee or not, a unit seed. Home is the kept point (FUN_0058eef0); rooms are
FUN_0061b130's by spot. The roll is game.exe's own (FUN_0045c390 on the unit
seed). A line: the scene | the act it comes to (home, idle n, a1, run, point)
and the seed after. tests/monsters/test_monster_thinks.cpp runs
rules::countess_think on each, taking ThinkIn::otherwise's next act when the
walk didn't set off. Prints `ok`.
"""
import itertools
import sys

import emu

HOME, NEAR, FOE = (100, 100), (105, 100), (120, 100)
AIP = (40, 7, 30)                                   # aip1 (run %), aip2 (stand), aip3 (+ 10: A1 %)
SEEDS = ((1, 666), (0x1234, 666), (0xdeadbeef, 0x29a), (77, 5))


def run(machine, mem, scene, log):
    """One think: the calls it makes, and the unit seed after."""
    away, target_away, at_home, far, walks, fires, melee, seed = scene
    game, mon, foe, params, row, record = mem
    spots = {mon + 0x100: HOME if at_home else NEAR, foe + 0x100: FOE}
    rooms = {HOME: 1, NEAR: 2 if away else 1, FOE: 2 if target_away else 1}
    machine.w32(mon + 0x20, seed[0])
    machine.w32(mon + 0x24, seed[1])
    machine.w32(params + 0x14, 10)
    machine.w32(params + 0x18, int(melee))
    state = {"spots": spots, "rooms": rooms, "far": far, "walks": walks, "fires": fires}
    CUR.clear()
    CUR.update(state)
    log.clear()
    machine.call(0x5e5c50, params, ecx=game, edx=mon)
    return list(log), (machine.r32(mon + 0x20), machine.r32(mon + 0x24))


CUR = {}


def install(machine, mem, log):
    """Hook everything the think calls but its roll."""
    from unicorn.x86_const import UC_X86_REG_ECX
    game, mon, foe, params, row, record = mem
    machine.hook(0x620bb0, lambda e: 1, 1)                                   # the unit's room: any
    machine.hook(0x58eef0, lambda e: record, 1)                              # the kept point (home)
    machine.hook(0x58ef40, lambda e: 0, 1)
    machine.hook(0x6488c0, lambda e: CUR["spots"][e.arg(0)][0], 1)           # a unit's path spot
    machine.hook(0x648900, lambda e: CUR["spots"][e.arg(0)][1], 1)
    machine.hook(0x45adf0, lambda e: CUR["spots"][e.mu.reg_read(UC_X86_REG_ECX) + 0x100][0], 0)
    machine.hook(0x45ae20, lambda e: CUR["spots"][e.mu.reg_read(UC_X86_REG_ECX) + 0x100][1], 0)
    machine.hook(0x61b130, lambda e: CUR["rooms"][(e.arg(1), e.arg(2))], 3)  # the room at a spot
    machine.hook(0x5a6260, lambda e: 0, 2)
    machine.hook(0x5dede0, lambda e: log.append(("home", int(CUR["walks"]))) or int(CUR["walks"]), 2)
    machine.hook(0x540e60, lambda e: log.append(("dropped",)) or 0, 2)
    machine.hook(0x5dc5c0, lambda e: 41 if CUR["far"] else 30, 1)            # the distance from home
    machine.hook(0x5e5b70, lambda e: log.append(("point",)) or 1 if CUR["fires"] else 0, 1)
    machine.hook(0x5de080, lambda e: log.append(("idle", e.arg(0))) or 0, 1)
    machine.hook(0x5ddf90, lambda e: log.append(("a1",)) or 1, 2)
    machine.hook(0x5de190, lambda e: 0, 2)
    machine.hook(0x5ded20, lambda e: log.append(("run",)) or 1, 1)


def outcome(log):
    """What the think came to: its last act; a walk home counts when it set off."""
    act = ("none", 0)
    for item in log:
        if item[0] == "home":
            if item[1]: act = ("home", 0)
        elif item[0] == "idle": act = ("idle", item[1])
        elif item[0] != "dropped": act = (item[0], 0)
    return act


def main():
    """Run every scene; --dump writes the cases file."""
    machine = emu.Emu()
    mem = game, mon, foe, params, row, record = tuple(machine.alloc(0x400) for _ in range(6))
    machine.w32(mon, 1)                                                      # a monster: its spot is its path's
    machine.w32(mon + 0x2c, mon + 0x100)
    machine.w32(foe, 1)
    machine.w32(foe + 0x2c, foe + 0x100)
    machine.w32(params + 8, foe)
    machine.w32(params + 0x1c, row)
    machine.w32(record + 0xc, HOME[0])
    machine.w32(record + 0x10, HOME[1])
    for column, value in zip((0x56, 0x5c, 0x62), AIP):                       # Normal's aip1..3 (game +0x6d: 0)
        machine.mu.mem_write(row + column, value.to_bytes(2, "little"))
    log = []
    install(machine, mem, log)
    lines = []
    for scene in itertools.product((False, True), (False, True), (False, True), (False, True), (False, True), (False, True), (False, True), SEEDS):
        away, target_away, at_home, far, walks, fires, melee, seed = scene
        if at_home and (away or far): continue                              # at home exactly is in its room, 0 off
        calls, after = run(machine, mem, scene, log)
        act = outcome(calls)
        asked = sum(1 for item in calls if item[0] == "home")
        if sum(1 for item in calls if item == ("dropped",)) != (0 if walks else asked):
            raise AssertionError(f"{scene}: a failed walk home drops the think event each time: {calls}")
        nums = [int(away), int(target_away), int(at_home), 41 if far else 30, int(walks), int(fires), int(melee), *seed]
        lines.append(" ".join(map(str, nums)) + f" | {int(asked > 0)} {act[0]} {act[1]} {after[0]} {after[1]}")
    if "--dump" in sys.argv:
        with open(sys.argv[sys.argv.index("--dump") + 1], "w", encoding="utf-8") as out:
            out.write("// tools/emu/ai_countess_home.py --dump: game.exe's Countess think (FUN_005e5c50), a line each.\n")
            out.write("".join(f'"{line}",\n' for line in lines))
    print(f"ok: {len(lines)} Countess think scenes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
