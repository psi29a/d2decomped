# SPDX-License-Identifier: GPL-3.0-or-later
"""The dispatcher's player gate in stock game.exe 1.14d: FUN_0057eec0 as FUN_0054d750 calls it.

    uv run python player_may_act.py

FUN_0054d750 calls FUN_0057eec0(game, unit, 1, 0, 0) (pushes 0, 0, 1 at
LAB_0054d7a8, the call at 0x54d7b0) for every packet id but 0x14, 0x15,
0x3c, 0x43, 0x66 and 0x41; a 0 answer drops the packet. This runs
FUN_0057eec0 for real with those arguments on a player unit in every mode
0 .. 0x12, with and without state 0x36 (FUN_00639df0 hooked), and on a null
unit. Walking (2) and running (3) pass; only state 0x36, a null unit and
modes 0 and 0x11 are refused. No MPQs are read.
"""
import emu

MODES = range(0x13)


def main():
    e = emu.Emu()
    assert e.read(0x54D7A8, 6) == bytes((0x6A, 0, 0x6A, 0, 0x6A, 1)), "FUN_0054d750's arguments are not (1, 0, 0)"
    assert e.read(0x54D7B0, 5) == bytes((0xE8,)) + (0x57EEC0 - 0x54D7B5).to_bytes(4, "little"), "no call to FUN_0057eec0"
    game, unit = e.alloc(0x2000), e.alloc(0x200)
    in_state = [0]

    def state(h):
        assert h.arg(0) in (unit, 0) and h.arg(1) == 0x36
        return in_state[0]

    e.hook(0x639DF0, state, 2)
    refused = []
    for has_state in (0, 1):
        in_state[0] = has_state
        for mode in MODES:
            e.w32(unit + 0x10, mode)
            if e.call(0x57EEC0, 1, 0, 0, ecx=game, edx=unit) == 0:
                refused.append((has_state, mode))
    in_state[0] = 0
    assert e.call(0x57EEC0, 1, 0, 0, ecx=game, edx=0) == 0, "a null unit passes"
    assert refused == [(0, 0), (0, 0x11)] + [(1, mode) for mode in MODES], refused
    print("ok: FUN_0057eec0(1, 0, 0) refuses state 0x36, a null unit and modes 0 and 0x11; walking (2) and running (3) pass")


if __name__ == "__main__":
    main()
