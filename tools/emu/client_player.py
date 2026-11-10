# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's global getters FUN_00463dd0 / FUN_00463de0 / FUN_0044bf00 / FUN_0044bf10 as the oracle.

    uv run --project tools/emu python tools/emu/client_player.py

Each getter takes no arguments and returns one global dword: the client's own
player ([0x7a6a70], both FUN_00463dd0 and FUN_00463de0), [0x70f234] and
[0x70f2bc]. The script writes several values into those globals, calls each
getter in game.exe and prints `address global value`; it exits 1 when a getter
returns anything but its global's value (docs/research/re/collision.md,
"The client's command switch").
"""
import sys

import emu

GETTERS = {0x463dd0: 0x7A6A70, 0x463de0: 0x7A6A70, 0x44BF00: 0x70F234, 0x44BF10: 0x70F2BC}
VALUES = (0, 1, 0x12345678, 0xFFFFFFFF, 0x8)


def main():
    machine = emu.Emu()
    bad = 0
    for getter, glob in GETTERS.items():
        for value in VALUES:
            machine.w32(glob, value)
            got = machine.call(getter) & 0xFFFFFFFF
            print(f"{getter:08x} {glob:08x} {got:08x}")
            if got != value:
                bad += 1
                print(f"MISMATCH {getter:08x}: wrote {value:08x}, got {got:08x}", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
