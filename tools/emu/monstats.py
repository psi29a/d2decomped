# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's monster stat init (FUN_006538a0, FUN_00573cb0's helper) as the oracle.

    uv run python monstats.py

For every MonStats row and difficulty, at the row's own Level, FUN_006538a0
gives min / max life, defense, to-hit, A1 / A2 / S1 damage and experience;
this checks them against the rule rules::monster_stats ports: MonLvl
(the L- columns) x the MonStats percentage / 100, or the MonStats value
itself for a noRatio row (flag +0xc & 4). Prints mismatches and a count.
"""
import csv
import os

import drlg

CACHE = os.path.join(os.path.dirname(__file__), ".cache")


def table(name):
    with open(os.path.join(CACHE, f"data_global_excel_{name}.txt"), encoding="latin1") as f:
        rows = list(csv.reader(f, delimiter="\t"))
    return [{k.lower(): v for k, v in zip(rows[0], r)} for r in rows[1:]]


def num(text):
    try: return int(text)
    except ValueError: return 0


def main():
    e = drlg.boot()
    monstats = [r for r in table("monstats") if r.get("id") and r["id"] != "Expansion"]
    monlvl = table("monlvl")
    out = e.alloc(0x40)
    checked = bad = 0
    for row, r in enumerate(monstats):
        for diff, sfx in enumerate(("", "(N)", "(H)")):
            level = min(num(r["level" + sfx.lower()]), len(monlvl) - 1)
            lvl = monlvl[level]
            e.mu.mem_write(out, bytes(0x40))
            if e.call(0x6538a0, row, 1, diff, level, 0x3f, out) == 0: continue
            got = [e.s32(out + 4 * i) for i in range(11)]
            ratio = r["noratio"] != "1"
            def scaled(base, column): return int(num(lvl[("l-" + base + sfx).lower()]) * num(r[(column + sfx).lower()]) / 100) if ratio else num(r[(column + sfx).lower()])
            want = [scaled("HP", "minHP"), scaled("HP", "maxHP"), scaled("AC", "AC"), scaled("TH", "S1TH"), scaled("XP", "Exp"),
                    scaled("DM", "A1MinD"), scaled("DM", "A1MaxD"), scaled("DM", "A2MinD"), scaled("DM", "A2MaxD"),
                    scaled("DM", "S1MinD"), scaled("DM", "S1MaxD")]
            checked += 1
            if got != want:
                bad += 1
                if bad <= 20: print(r["id"], diff, "game", got, "ours", want)
    print(f"{checked - bad}/{checked} rows x difficulties match")


if __name__ == "__main__":
    main()
