# SPDX-License-Identifier: GPL-3.0-or-later
"""A body slot's item taken off, refused with an item on the cursor, as the
oracle: FUN_00560cd0's guard (.\\ITEMS\\ItemMode.cpp).

    uv run python takeoff_cursor.py

Expected: `ok: FUN_00560cd0 refuses with a cursor item (4 cases)`.

FUN_00560cd0 takes the game in ECX, the player in EDX and the slot, a
flag and an out pointer on the stack (`ret 0xc`). It writes 0 to the out
pointer, reads the player's inventory (+0x60) and asks FUN_0063c1e0 for
its cursor item (+0x20, the inventory's magic 0x1020304 checked), run
for real here. With one it answers 0 at once (0x560d2a), before the slot
is asked (FUN_0063bde0, hooked). Without, an empty slot answers 0 with
the out pointer 0 (0x560d4e). FUN_00580030 calls it with flag 1 and
ignores the answer, so a spent stack stays in its slot: d2d's
Fight::out_of_ammo returns while Hero::held is set
(tests/game/test_game_missiles.cpp).
"""
import sys

from unicorn.x86_const import UC_X86_REG_EIP

import emu

START = 0x560CD0
GAME, MAGIC = 0x6A3E0000, 0x1020304


def main(exe=None):
    e = emu.Emu(exe)
    player, inv, out, cursor_item = e.alloc(0x100), e.alloc(0x100), e.alloc(0x10), e.alloc(0x100)
    e.w32(player + 0x60, inv)
    e.w32(inv, MAGIC)
    asked = []

    def in_slot(h):
        if (h.arg(0), h.arg(1)) != (inv, 4):
            raise AssertionError(f"slot asked: {h.arg(0):x} {h.arg(1)}")
        asked.append(1)
        return 0
    e.hook(0x63BDE0, in_slot, 2)
    results = []
    for cursor in (0, cursor_item):
        for flag in (0, 1):
            e.w32(inv + 0x20, cursor)
            e.w32(out, 0xDEAD)
            asked.clear()
            answer = e.call(START, 4, flag, out, ecx=GAME, edx=player, limit=10000)
            if e.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
                raise AssertionError("did not return")
            results.append((cursor != 0, flag, answer, e.r32(out), len(asked)))
    for held, flag, answer, written, slot_asked in results:
        want = (0, 0, 0 if held else 1)
        if (answer, written, slot_asked) != want:
            raise AssertionError(f"cursor {held} flag {flag}: got answer {answer} out {written} asked {slot_asked}, want {want}")
    print(f"ok: FUN_00560cd0 refuses with a cursor item ({len(results)} cases)")
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if args:
        sys.exit(__doc__)
    sys.exit(main(exe))
