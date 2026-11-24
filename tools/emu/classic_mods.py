# SPDX-License-Identifier: GPL-3.0-or-later
"""A classic (version 0) item's mods as the oracle: FUN_0065fe10's table.

    uv run python classic_mods.py gen 4000 1 > cases.tsv
    uv run python classic_mods.py game cases.tsv > g.txt   # game.exe's line per case
    build/tests/test_classic_mods cases.tsv > o.txt        # ours; diff g.txt o.txt
    uv run python classic_mods.py inc cases.tsv g.txt > tests/classic_mods_cases.inc

FUN_0065fe10 sends a version-0 item's mod to the handler and stat of its
Properties row in the table at 0x745b58 (drops.md "A classic item's mods").
A case is `seed code ilvl def dur prop param min max kind`. The item is a
unit of type 4 of that base (unit +4), version 0 (FUN_0062a6c0), ilvl (FUN_00628220),
its own seed {seed, 666} at data +4, its defence (0x1f) and durability (0x48 / 0x49) set.
FUN_0065fe10 runs for real (ECX the kind, ESI the item, EDI the mod {row,
param, min, max}, FUN_0065fec0's arguments otherwise: state 0, flags 0x40), with
drops.py's stat store (FUN_00627030 add, FUN_00627150 set, FUN_00627260 set
stat, FUN_006253b0 get) hooked. The line: the item's list (stat:param=value,
ValShift applied as drops.py prints it), its defence, durability, sockets
(0xc2), flag 0x800 and its own seed's low after.
"""
import random
import sys

import drops

# Bases for the jobs: armor (body, helm, shield), one- and two-handed and
# thrown weapons, a bow, and two that are neither (ring, amulet).
BASES = ("qui", "lea", "cap", "buc", "lrg", "ssd", "2hs", "axe", "jav", "tkf", "lbw", "spr", "rin", "amu")


def properties(e):
    """Properties.txt codes in row order (the table's index), the Expansion line left out."""
    return [r["code"] for r in drops.txt_rows(e, "Properties") if r.get("code") and r["code"] != "Expansion"]


def gen(count, seed):
    o = drops.Oracle(items=True)
    codes = properties(o.e)[:244]
    armor = {r["code"]: r for r in drops.txt_rows(o.e, "Armor")}
    weapons = {r["code"]: r for r in drops.txt_rows(o.e, "Weapons")}
    rng = random.Random(seed)
    for n in range(count):
        prop = codes[n % len(codes)]
        code = rng.choice(BASES)
        row = armor.get(code) or weapons.get(code)
        low, high = (int(row["minac"] or 0), int(row["maxac"] or 0)) if code in armor else (0, 0)
        defence = rng.randint(low, high) if high else 0
        dur = int(row["durability"] or 0) if row else 0
        span = rng.choice((1, 3, 10, 40, 200))
        low = rng.randint(-span, span)
        high = rng.choice((low, low + rng.randint(1, span), low - rng.randint(1, span), rng.randint(-span, span)))
        param = rng.choice(("", "0", str(rng.randint(1, 3)), str(rng.randint(0, 30)), str(rng.randint(0, 356)), str(rng.randint(-5, 400))))
        if prop.endswith("/time"):                   # FUN_0065dd80 is fatal outside this (ids 0x444, 0x44c..0x44e)
            param, low = str(rng.randint(0, 3)), rng.randint(-256, 767)
            high = rng.randint(low, 767)
        print(rng.getrandbits(32), code, rng.randint(1, 99), defence, dur, prop, param or "-", low, high, rng.choice((0, 1, 2, 3, 4, 5)), sep="\t")


def cases(path):
    for line in open(path):
        f = line.rstrip("\r\n").split("\t")
        yield int(f[0]), f[1], int(f[2]), int(f[3]), int(f[4]), f[5], "" if f[6] == "-" else f[6], int(f[7]), int(f[8]), int(f[9])


def game(path):
    o = drops.Oracle(items=True)
    e = o.e
    rows = {c: i for i, c in enumerate(properties(e))}
    item, data, mod = o.item, o.data, e.alloc(0x10)
    for seed, code, ilvl, defence, dur, prop, param, low, high, kind in cases(path):
        for a, n in ((item, 0x200), (data, 0x200)): e.mu.mem_write(a, b"\0" * n)
        e.w32(item, 4)
        e.w32(item + 4, o.codes.index(code))
        e.w32(item + 0x14, data)
        e.call(0x62a6c0, item, 0)                     # version 0: a classic item
        e.call(0x628220, item, ilvl)
        e.w32(data + 4, seed)                          # its own seed {seed, 666}
        e.w32(data + 8, 666)
        o.stats, o.mods, o.sets = {0x1f: defence, 0x48: dur, 0x49: dur}, {}, {}
        value = int(param) if param.lstrip("-").isdigit() else 0
        for k, v in enumerate((rows[prop], value, low, high)): e.w32(mod + 4 * k, v & 0xFFFFFFFF)
        e.call(0x65fe10, kind, 0, 0, 0, 0, 0, 0, 0x40, 0, regs={"esi": item, "edi": mod})
        st = o.stats
        props = ",".join(f"{s}:{p}={((v & 0xffffffff ^ 0x80000000) - 0x80000000) >> o.shift.get(s, 0)}" for (s, p), v in sorted(o.mods.items()) if v)
        print(f"{seed:08x} {code} i{ilvl} {prop}({param}) {low}..{high} k{kind}: [{props}] def {st.get(0x1f, 0)}"
              f" dur {st.get(0x48, 0)}/{st.get(0x49, 0)} sock {st.get(0xc2, 0)} f{(e.r32(data + 0x18) >> 11) & 1} -> {e.r32(data + 4):08x}")


def inc(path, game_lines):
    for job, line in zip(cases(path), open(game_lines)):
        seed, code, ilvl, defence, dur, prop, param, low, high, kind = job
        print(f'{{ {seed}u, "{code}", {ilvl}, {defence}, {dur}, "{prop}", "{param}", {low}, {high}, {kind}, "{line.rstrip()}" }},')


if __name__ == "__main__":
    {"gen": lambda: gen(int(sys.argv[2]), int(sys.argv[3])), "game": lambda: game(sys.argv[2]), "inc": lambda: inc(sys.argv[2], sys.argv[3])}[sys.argv[1]]()
