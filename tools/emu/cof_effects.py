# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture which blend game.exe 1.14d draws each COF draw effect with.

Usage: uv run python cof_effects.py > ../../tests/cof_effect_cases.inc

A transparent COF layer's draw effect (FUN_004db140) is the draw mode the
composite draw FUN_00470ec0 hands the renderer. The software renderer's
FUN_006c8250 turns a mode into a blend table from the pointer array
D2WinPalette installs (FUN_004fb010 -> FUN_006c8000). This boots the act 1
palette, installs the array, asks FUN_006c8250 for modes 0..6, and names
the table. For the three alpha tables it fits the sprite's share against
the installed palette: the row writer FUN_00608110 reads
T[screen * 256 + sprite]. Each line is {effect, kind, sprite percent}:
kind 0 opaque, 1 additive, 2 multiply, 3 alpha, 4 max-component.
"""
import hashlib
from pathlib import Path

import emu
from palette import boot

ALPHA = (0x7EE468, 0x7FE468, 0x80E468)          # PL2 +0x3500, +0x13500, +0x23500
NAMED = {0: 0, 0x7DE468: 1, 0x821B68: 2, 0x831B68: 4}


def sprite_percent(machine, table):
    palette = machine.read(0x81E668, 0x400)
    cells = machine.read(table, 0x10000)
    best = None
    for percent in (25, 50, 75):
        error = 0
        for screen in range(1, 256, 3):
            for sprite in range(1, 256, 5):
                out = cells[screen * 256 + sprite]
                for channel in range(3):
                    want = (palette[sprite * 4 + channel] * percent + palette[screen * 4 + channel] * (100 - percent)) / 100
                    error += abs(palette[out * 4 + channel] - want)
        if best is None or error < best[0]:
            best = (error, percent)
    return best[1]


def main():
    path = Path(emu.data_path()) / 'bin/game.exe'
    if hashlib.sha256(path.read_bytes()).hexdigest() != '631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf':
        raise SystemExit('cof_effects: requires stock game.exe 1.14d (1.14.3.71)')
    original = emu.Emu(str(path)).read(0x4FB010, 5)
    machine = boot(1)                         # boot cuts FUN_004fb010 off; put it back
    machine.mu.mem_write(0x4FB010, original)
    machine.mu.ctl_remove_cache(0x4FB010, 0x4FB015)
    installed = []

    def install(current):                     # FUN_004f6410: the renderer's slot 0x74 is FUN_006c8000 in GDI
        installed.append(current.arg(0))
        return 0

    machine.hook(0x4F6410, install, 1)
    machine.call(0x4FB010)
    if len(installed) != 1:
        raise SystemExit('cof_effects: FUN_004fb010 did not hand over its table array')
    machine.call(0x6C8000, ecx=installed[0])
    scratch = machine.alloc(4)
    for mode in range(7):
        table = machine.call(0x6C8250, ecx=mode, edx=scratch)
        if table in ALPHA:
            print('{%d, 3, %d},' % (mode, sprite_percent(machine, table)))
        elif table in NAMED:
            print('{%d, %d, 0},' % (mode, NAMED[table]))
        else:
            raise SystemExit(f'cof_effects: mode {mode} picks an unknown table {table:08x}')


if __name__ == '__main__':
    main()
