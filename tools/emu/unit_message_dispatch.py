# SPDX-License-Identifier: GPL-3.0-or-later
"""The per-unit-type message dispatch, as the oracle: FUN_00480c10 (HandleMessage).

    uv run python unit_message_dispatch.py 1 1 > g.txt
    sort -u g.txt   # every unit type reached the callee collision.md's call chain names

docs/research/re/collision.md cites FUN_00480c10 as what carries a command
into FUN_00461250 ("the client's command switch", a player's queue, "The
sends" and "The action send"). The decompile names it HandleMessage and
shows it is itself a four-way switch on the target unit's type
(`D2UnitStrc.eUnitType`, structs: D2UnitStrc), hooked here so each callee
records what it was given instead of running (none of the four is small
enough to run for real with a fake unit): PLAYER forwards all four
arguments unchanged to PlayerMessage (0x461250, the function the doc's
"FUN_00461250" names); MONSTER and OBJECT forward the first three to
MonsterMessage (0x4aff60) / ObjectMessage (0x4bd6d0), dropping the neutral
mode; ITEM calls ItemMessage (0x4c1b80) with the message's own nSkillId and
nItemId fields (`D2ServerMessageHandleStrc`, structs: D2ServerMessageHandleStrc)
in place of the message pointer and the neutral mode. A fifth, unhandled
type reaches none of them.
"""
import sys

import emu

UNIT_SIZE = 244
MESSAGE_SIZE = 28


def main(count, seed):
    e = emu.Emu()
    seen = []

    def capture(nm, nargs):
        def fn(ev):
            regs = (ev.mu.reg_read(emu.UC_X86_REG_ECX), ev.mu.reg_read(emu.UC_X86_REG_EDX))
            stack = tuple(ev.arg(i) for i in range(nargs))
            seen.append((nm, regs, stack))
            return 0
        return fn

    e.hook(0x461250, capture("PlayerMessage", 2), 2)    # ECX, EDX, 2 stack args: RET 8
    e.hook(0x4AFF60, capture("MonsterMessage", 1), 1)    # ECX, EDX, 1 stack arg: RET 4
    e.hook(0x4BD6D0, capture("ObjectMessage", 1), 1)     # ECX, EDX, 1 stack arg: RET 4
    e.hook(0x4C1B80, capture("ItemMessage", 2), 2)       # ECX, EDX, 2 stack args: RET 8

    message = e.alloc(MESSAGE_SIZE)
    e.w32(message + 0, 0xAAAA)    # nSkillId
    e.w32(message + 4, 0xBBBB)    # nItemId
    unit_type_names = {0: "PLAYER", 1: "MONSTER", 2: "OBJECT", 3: "MISSILE", 4: "ITEM", 5: "WARP"}
    for unit_type, name in unit_type_names.items():
        unit = e.alloc(UNIT_SIZE)
        e.w32(unit + 0, unit_type)
        seen.clear()
        e.call(0x480C10, message, 0x1234, ecx=0x13, edx=unit)
        if not seen:
            print(name, "no callee reached")
            continue
        callee, regs, stack = seen[0]
        if callee == "PlayerMessage":
            ok = regs == (0x13, unit) and stack == (message, 0x1234)
            print(name, "->", callee, "OK" if ok else f"DISAGREE {regs} {stack}")
        elif callee == "ItemMessage":
            ok = regs == (0x13, unit) and stack == (0xAAAA, 0xBBBB)
            print(name, "->", callee, "OK" if ok else f"DISAGREE {regs} {stack}")
        else:
            ok = regs == (0x13, unit) and stack == (message,)
            print(name, "->", callee, "OK" if ok else f"DISAGREE {regs} {stack}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
