# SPDX-License-Identifier: GPL-3.0-or-later
"""What lets a held button or a warp's click go out: game.exe 1.14d's own gates, run by their own code.

    uv run python click_gates.py > ../../tests/click_gates_cases.inc
    uv run python click_gates.py | diff - ../../tests/click_gates_cases.inc     # re-check

collision.md "What a held frame sends" and "The server's pending interact".
Three gates, each line {kind, a, b, c, d, result, after}:

- kind 0, FUN_004623c0's throttle (stack the player unit, EBX the mouse
  context): a = the flags, b = the player's mode, c = the clock 0x7a0498,
  d = the stamp 0x7a5268 before; result 1 goes, after = the stamp after.
  Runs natively (the dead zone, FUN_00474080, GetXPos / GetYPos on a real
  path, GetEntryIndex reading the clock), as click_move.py sets it up; the
  spot is 5 subtiles out, so only the throttle can answer 0.
- kind 1, FUN_00462930 (ECX the context: flags, player, unit under the
  cursor), a held frame (flags 8) with 0x7a526c armed: a = the unit's type
  (-1 none), b = the player's mode (-1 no player); result its answer,
  after = 0x7a526c after. Pure, nothing hooked.
- kind 2, FUN_00461dc0 with a warp (type 5) under the cursor (EAX the
  context, stack the target's type and id): a = now (0x7a048c), b = the
  refusal 0x7a04c8 before, c = FUN_00641530's distance (hooked); result
  0 nothing sent, 1 FUN_00481030 with 0x13 alone (sent at once), 2
  FUN_00481030 with the context's action and FUN_00460780 with 0x13 (walk,
  then queue); after = 0x7a04c8 after. FUN_0044daa0 / FUN_0044dac0 /
  FUN_0044dad0 run natively; FUN_00481030, FUN_00460780 are hooked and
  logged.

d2d's rules::held_walk_goes, held_frame_goes, warp_click_goes and
kWarpReach (components/rules/click_spot.hpp) are checked against these by
tests/test_click_gates.cpp.
"""
from unicorn.x86_const import UC_X86_REG_ECX

import emu

ORIGIN_X, ORIGIN_Y = 1000, 2000
CLOCK, LAST_INDEX = 0x7A0498, 0x7A5268      # click_move.py: GetEntryIndex's clock, the throttle's stamp
ARMED = 0x7A526C                            # FUN_00462930: 1 or 2 from a press, 0 from a release
NOW_MS, UNTIL_MS = 0x7A048C, 0x7A04C8       # FUN_0044daa0's GetTickCount copy, FUN_0044dac0 / FUN_0044dad0's refusal
WALK_ACTION = 0x1234                        # the context's +0x18: the walk's action, told apart from 0x13


def throttle(machine):
    unit = machine.alloc(0x100)
    path = machine.alloc(0x200)
    ctx = machine.alloc(0x40)
    machine.view("unit", unit).type = 0
    machine.view("unit", unit).path = path
    machine.view("path", path).x, machine.view("path", path).y = ORIGIN_X, ORIGIN_Y
    clocks = ((0, 0), (0, 6), (0, 7), (5, 11), (5, 12), (100, 106), (100, 107), (0xFFFFFFFE, 4), (0xFFFFFFFE, 5), (10, 3))
    for mode in range(8):
        machine.view("unit", unit).mode = mode
        for flags in (0x4, 0x44, 0x8, 0x48, 0x10):
            for last, now in clocks:
                machine.w32(ctx, flags)
                machine.w32(ctx + 0xC, ORIGIN_X + 5)
                machine.w32(ctx + 0x10, ORIGIN_Y)
                machine.w32(CLOCK, now)
                machine.w32(LAST_INDEX, last)
                sent = machine.call(0x4623C0, unit, regs={"ebx": ctx}) & 0xFFFFFFFF
                yield 0, flags, mode, now, last, sent, machine.r32(LAST_INDEX)


def held_frame(machine):
    ctx = machine.alloc(0x40)
    player = machine.alloc(0x100)
    target = machine.alloc(0x100)
    machine.view("unit", player).type = 0
    for target_type in (-1, 0, 1, 2, 3, 4, 5, 6, 7):
        machine.view("unit", target).type = max(target_type, 0)
        for mode in range(-1, 13):
            if mode >= 0:
                machine.view("unit", player).mode = mode
            machine.w32(ctx, 0x8)
            machine.w32(ctx + 4, player if mode >= 0 else 0)
            machine.w32(ctx + 8, target if target_type >= 0 else 0)
            machine.w32(ARMED, 1)
            sent = machine.call(0x462930, ecx=ctx) & 0xFFFFFFFF
            yield 1, target_type, mode, 0, 0, sent, machine.r32(ARMED)


def warp_click(machine):
    ctx = machine.alloc(0x40)
    player = machine.alloc(0x100)
    target = machine.alloc(0x100)
    machine.view("unit", player).type = 0
    machine.view("unit", target).type = 5
    log = []
    case = {}
    machine.hook(0x641530, lambda e: case["distance"], 2)
    machine.hook(0x481030, lambda e: log.append(("send", e.mu.reg_read(UC_X86_REG_ECX))) or 0, 3)
    machine.hook(0x460780, lambda e: log.append(("queue", e.mu.reg_read(UC_X86_REG_ECX))) or 0, 2)
    times = ((0, 0), (499, 500), (500, 500), (501, 500), (0xFFFFFFFF, 0), (0, 0xFFFFFFFF), (1000, 1500), (1500, 1000))
    for now, until in times:
        for distance in (0, 4, 5, 6, 50):
            case["distance"] = distance
            log.clear()
            machine.w32(ctx, 0x4)
            machine.w32(ctx + 4, player)
            machine.w32(ctx + 8, target)
            machine.w32(ctx + 0x18, WALK_ACTION)
            machine.w32(NOW_MS, now)
            machine.w32(UNTIL_MS, until)
            machine.call(0x461DC0, 5, 77, regs={"eax": ctx})
            if not log:
                result = 0
            elif log == [("send", 0x13)]:
                result = 1
            elif log == [("send", WALK_ACTION), ("queue", 0x13)]:
                result = 2
            else:
                raise AssertionError(f"now {now} until {until} distance {distance}: unexpected calls {log}")
            yield 2, now, until, distance, 0, result, machine.r32(UNTIL_MS)


def main():
    machine = emu.Emu()
    print("// Stock game.exe 1.14d: FUN_004623c0's throttle (0), FUN_00462930 held (1), FUN_00461dc0's warp (2);")
    print("// generated by tools/emu/click_gates.py. kind, a, b, c, d, result, after")
    for gate in (throttle, held_frame, warp_click):
        for kind, a, b, c, d, result, after in gate(machine):
            print(f"    {{ {kind}, {a}, {b}, {c}, {d}, {result}, {after} }},")


if __name__ == "__main__":
    main()
