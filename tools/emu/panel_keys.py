# SPDX-License-Identifier: GPL-3.0-or-later
"""Run game.exe's UI-open check (FUN_00453910) for the panel keys beside the stash, Cube or a store.

    uv run python panel_keys.py [--exe Game.exe] [--dump ../../tests/panel_keys_cases.inc]

The Inventory, Character and Skill Tree keys (UI 1, 2, 4; 0x468950,
0x468940, 0x468af0) and the mini-panel's buttons (FUN_0047ec50) open
their panel through FUN_00455f20, which asks FUN_00453910 first: with
mode 2 (toggle) and the panel shut, it walks the UIs that are up and
reads each one's row of the table at 0x711130 (0 pass, 1 shut that one,
2 or more refuse). Each case raises one UI (none, the store 0xc, the
stash 0x19, the Cube 0x1a; the flags at 0x7a27c0) and runs the check for
real. Hooked: FUN_004a0000 (0, no modal), FUN_00455f20 (a shut, recorded),
FUN_004b3230 / FUN_004b3c20. A line: the UI up, the key's UI, 1 allowed / 0 refused.
"""
import argparse

import emu

FLAGS = 0x7A27C0
UP = (0, 0xC, 0x19, 0x1A)
KEYS = (1, 2, 4)


def capture(exe):
    machine = emu.Emu(exe)
    shut = []
    machine.hook(0x4A0000, lambda e: 0, 0)
    machine.hook(0x455F20, lambda e: shut.append(e.mu.reg_read(emu.UC_X86_REG_ECX)) or 1, 1)
    machine.hook(0x4B3230, lambda e: 0, 0)
    machine.hook(0x4B3C20, lambda e: 0, 0)
    machine.w32(0x70F9CC, 0x26)                    # the UI count the check asserts
    rows = []
    for up in UP:
        for key in KEYS:
            machine.mu.mem_write(FLAGS, bytes(4 * 0x26))
            if up: machine.w32(FLAGS + 4 * up, 1)
            shut.clear()
            allowed = machine.call(0x453910, regs={"eax": 2, "edi": key}, limit=10000)
            assert not shut, (up, key, shut)      # none of these rows shuts: pass or refuse
            rows.append((up, key, allowed))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe")
    parser.add_argument("--dump")
    args = parser.parse_args()
    rows = capture(args.exe)
    if args.dump:
        with open(args.dump, "w", newline="\n") as out:
            out.write("// tools/emu/panel_keys.py: the UI up, the key's UI, game.exe allows it (FUN_00453910)\n")
            for up, key, allowed in rows:
                out.write(f"{{0x{up:x}, {key}, {allowed}}},\n")
    for up, key, allowed in rows:
        print(f"0x{up:x}\t{key}\t{allowed}")


if __name__ == "__main__":
    main()
