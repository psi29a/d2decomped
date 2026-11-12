# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's throwing potion damage line, run by its own code.

    uv run --project tools/emu python tools/emu/throw_potion_text.py

FUN_00485410 (the tooltip's damage lines, from FUN_0048dd90) starts with
items of type 0x26 (tpot, FUN_00629bb0): the thrown missile
(FUN_006288a0: Weapons.txt missiletype) at level 1 gives the line, and
nothing else of the function runs. Loads the string tables and every
excel table as skill_tips.py does, then runs FUN_00485410 for real on each
tpot row of Weapons.txt, with the missile's own getters (FUN_0064b100 /
FUN_0064b1d0 / FUN_0064b2a0 element, FUN_0064af20 / FUN_0064aff0 physical,
FUN_0064b0c0 EType) on the loaded Missiles.txt. Hooked: FUN_00463dd0 (the
player: a blank unit), FUN_00629bb0 (the type test: yes) and
FUN_006288a0 (the missile id). d2d's item_lines (components/game/
item_text.hpp) is the Python below, on the same Missiles.txt row; prints
both lines and the count that differ (colour codes shown as {cN}).
"""
import sys

import emu
import skill_tips

GAME_ETYPE_COLOUR = {1: 1, 2: 4, 4: 3, 5: 2}       # d2d: fire red, lightning gold, cold blue, poison green, magic white
ETYPES = {"fire": 1, "ltng": 2, "mag": 3, "cold": 4, "frze": 4, "pois": 5}


def excel(name):
    text = emu.mpq_cache().read(f"data\\global\\excel\\{name}.txt").decode("latin1").splitlines()
    head = text[0].split("\t")
    return [dict(zip(head, line.split("\t"))) for line in text[1:]]


def num(row, column):
    try:
        return int(row.get(column, "") or 0)
    except ValueError:
        return 0


def d2d_line(missile, throw, to):
    """item_text.hpp's tpot branch: the row's element and physical part at level 1."""
    shift = num(missile, "HitShift") & 31
    etype = ETYPES.get(missile.get("EType", ""), 0)
    low = num(missile, "EMin") << shift if etype else 0
    high = max(num(missile, "Emax") << shift, low) if etype else 0
    colour = GAME_ETYPE_COLOUR.get(etype, 0)
    if etype == 5:
        seconds = max(int(num(missile, "ELen") / 25), 1)
        low, high = int(low / seconds), int(high / seconds)
    low = (low + (num(missile, "MinDamage") << shift)) >> 8
    high = (high + (num(missile, "MaxDamage") << shift)) >> 8
    high = max(high, low)
    text = f"{{c0}}{throw} {{c{colour}}}{low}"   # TextLine: white, then `tail` from the split
    if high != low:
        text += f" {to} {high}"
    return text + "|"


def same_colour_once(text):
    """game.exe's colour codes as {cN}, a code that repeats the colour in force dropped."""
    out, current = text.split("\xffc")[0], None
    for part in text.split("\xffc")[1:]:
        if part[0] != current:
            out += f"{{c{part[0]}}}"
            current = part[0]
        out += part[1:]
    return out


def main():
    e = skill_tips.boot()
    player = e.alloc(0x200)
    missile_id = [0]
    e.hook(0x463DD0, lambda _e: player, 0)
    e.hook(0x629BB0, lambda _e: 1, 2)
    e.hook(0x6288A0, lambda _e: missile_id[0], 1)
    missiles = {num(row, "Id"): row for row in excel("Missiles") if row.get("Id")}
    throw, to = (skill_tips.wide(e, e.call(0x524A30, ecx=string)) for string in (0xD8B, 0xD88))
    out, item = e.alloc(0x1000), e.alloc(0x200)
    bad = runs = 0
    for row in excel("Weapons"):
        if row.get("type") != "tpot":
            continue
        missile_id[0] = num(row, "missiletype")
        e.mu.mem_write(out, bytes(0x1000))
        e.call(0x485410, item, out)
        game = skill_tips.wide(e, out).replace("\n", "|")
        game = same_colour_once(game)
        mine = d2d_line(missiles[missile_id[0]], throw, to)
        runs += 1
        bad += game != mine
        print(f"{row['code']}: game {game!r} d2d {mine!r}")
    print(f"{bad} of {runs} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
