#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A mercenary's gear absorb (stats 142..149) against a monster's elemental and magic hit: the oracle is the merc's LIFE after it.

    uv run python merc_absorb.py gen 20000 7 > cases.tsv
    uv run python merc_absorb.py game cases.tsv > g.tsv          # game.exe, under the emulator, every data table loaded
    <d2d side: test_merc_absorb --life cases.tsv | grep -E '^[0-9]+ -?[0-9]+$' > d.tsv>   # the real merc_foe, monster_blow, Foe::take and Fight::hurt_side
    uv run python merc_absorb.py compare cases.tsv g.tsv d.tsv   # exit 1 on a difference, an empty or malformed file

A case line: `id life max mdr res_fire res_light res_cold pct_fire pct_light pct_cold pct_magic flat_fire flat_light flat_cold
flat_magic class nblows` and then, per blow, `n` and n pairs `type field`. `life` is the merc's life in 256ths, `max` its max
life in whole points; `class` the MonStats class of the hireling (271 Rogue Scout, 338 Desert Guard, 359 Iron Wolf, 560 and 561
Barbarian); `mdr` the magic damage reduction (stat 35); `res_*` the resistances after the cap (-100..75); `pct_*` / `flat_*`
the gear's absorb percent and flat (fire 142 / 143, lightning 144 / 145, magic 146 / 147, cold 148 / 149, as the stat ids);
`type` is d2d's element index (0 fire, 1 lightning, 2 cold, 4 magic) and `field` the blow's damage in whole points. `game`
prints `id life_after`, in 256ths: game.exe's FUN_0057c6c0 run for real on a UNIT OF TYPE 1 (a monster) of the hireling's
class, once per blow, in order: FUN_0057c1e0 (every field through the flat reduction, the resistance and the absorb
FUN_0057bf10, which adds what it takes to the record's +0x48), FUN_0057a980 (life = min(life + absorbed, max life)), then the
record total (+0x4c) off the life, under 0x100 left as 0. The MonStats rows are the game's own (every table loaded); the
stat getter is a dict. A blow after the death is not applied. The d2d side prints the same line from the real code. Compared
byte for byte: the life.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# type -> (record offset, resist stat, absorb percent stat, absorb flat stat)
ROWS = {0: (0x10, 0x27, 0x8E, 0x8F), 1: (0x1C, 0x29, 0x90, 0x91), 2: (0x24, 0x2B, 0x94, 0x95), 4: (0x20, 0x25, 0x92, 0x93)}
MDR_STAT = 35
CLASSES = (271, 338, 359, 560, 561)   # Rogue Scout, Desert Guard, Iron Wolf, the two Barbarian rows
HEAD = 17   # the fixed words before the blows


def gen(count, seed):
    rng = random.Random(seed)
    lines = []
    for index in range(count):
        max_life = rng.choice((30, 100, rng.randint(20, 400), rng.randint(20, 1500)))
        life = rng.choice((max_life, rng.randint(1, max_life), rng.randint(1, max(max_life // 4, 1)), 1)) * 256 + rng.choice((0, 0, rng.randint(0, 255)))
        life = min(life, max_life * 256)
        life = max(life, 0x100)
        mdr = rng.choice((0, 0, rng.randint(1, 20)))
        res = [rng.choice((0, rng.randint(-100, 75), rng.randint(0, 75))) for _ in range(3)]
        pct = [rng.choice((0, 0, rng.randint(1, 60), rng.randint(1, 45), 40, 41)) for _ in range(4)]
        flat = [rng.choice((0, 0, rng.randint(1, 30), rng.randint(1, 80))) for _ in range(4)]
        blows = []
        cls = rng.choice(CLASSES)
        for _ in range(rng.choice((1, 1, 2, 3))):
            types = rng.sample((0, 1, 2, 4), rng.choice((1, 1, 2, 3)))
            blows.append(" ".join(f"{t} {rng.choice((1, 2, 5, 20, rng.randint(1, 60), rng.randint(1, 400)))}" for t in types))
        body = " ".join(f"{len(b.split()) // 2} {b}" for b in blows)
        lines.append(f"{index} {life} {max_life} {mdr} {res[0]} {res[1]} {res[2]} {' '.join(map(str, pct))} {' '.join(map(str, flat))} {cls} {len(blows)} {body}")
    return lines


def parse(line):
    words = [int(word) for word in line.split()]
    if len(words) < HEAD:
        raise ValueError(f"bad case line {line!r}")
    head, rest = words[:HEAD], words[HEAD:]
    blows = []
    for _ in range(head[-1]):
        if not rest or len(rest) < 1 + 2 * rest[0]:
            raise ValueError(f"bad case line {line!r}")
        count = rest[0]
        blows.append([(rest[1 + 2 * k], rest[2 + 2 * k]) for k in range(count)])
        rest = rest[1 + 2 * count:]
    if rest:
        raise ValueError(f"bad case line {line!r}")
    return head, blows


def read(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return [line for line in handle.read().splitlines() if line.strip()]
    except OSError as err:
        raise SystemExit(f"cannot read {path}: {err.strerror}") from None


def game(path):
    import drlg    # game.exe with every data table loaded: the hireling rows are the game's own

    e = drlg.boot()
    stats = {}
    # What FUN_0057c6c0 reaches besides FUN_0057c1e0 and the heal FUN_0057a980 (real): the unit predicates, the logging and the
    # side effects that are not the claim (stun, chill, freeze, poison, mana, stamina); the set-stat and max-life calls the heal needs.
    # Unlike gear_absorb.py the MonStats readers (FUN_00457490, FUN_0063edc0, FUN_0063e9f0, FUN_0063ee90, FUN_0063e990,
    # FUN_0063e940) are NOT stubbed: the unit is a type-1 hireling and they read its real row.
    e.stub(1, {0x554200: 1, 0x620BB0: 1})
    e.stub(0, {0x625500: 3, 0x535BC0: 0, 0x639DF0: 2, 0x639E30: 3, 0x6277E0: 2, 0x626CD0: 1, 0x40B480: 3, 0x5347D0: 7,
               0x5347E0: 3, 0x61AB00: 1, 0x621D50: 2, 0x57AA60: 0, 0x57AAA0: 0, 0x57AAE0: 2, 0x57AF80: 2, 0x57B230: 2, 0x57AC50: 3, 0x57ADD0: 3, 0x5D6410: 0})
    e.stub(100, {0x57C060: 0})
    state = {"max": 0, "unit": 0}
    e.hook(0x625480, lambda e: stats.get((e.arg(0), e.arg(1)), 0) & 0xFFFFFFFF, 3)
    e.hook(0x627260, lambda e: stats.__setitem__((e.arg(0), e.arg(1)), e.arg(2) - (1 << 32 if e.arg(2) >> 31 else 0)) or 0, 4)
    e.hook(0x625D10, lambda e: state["max"], 1)
    e.hook(0x6256B0, lambda e: 0, 2)   # no state list: no Bone Armor, no Cyclone Armor, no Energy Shield on a hireling
    e.hook(0x625D00, lambda e: 0, 3)
    e.hook(0x5541B0, lambda e: 1 if stats.get((state["unit"], 6), 0) <= 0 else 0, 0)   # dead: a unit at life 0, as game.exe's death leaves it
    game_addr = e.alloc(0x1000)
    attacker_addr = e.alloc(0x200)
    attacker = e.view("unit", attacker_addr)
    attacker.type, attacker["class"] = 1, 0x10
    game_view = e.view("game", game_addr)
    game_view.difficulty, game_view.expansion = 0, 0   # a classic Normal game: no resistance penalty
    for line in read(path):
        head, blows = parse(line)
        index, life, max_life, mdr, res_fire, res_light, res_cold, p_fire, p_light, p_cold, p_magic, f_fire, f_light, f_cold, f_magic, cls, _ = head
        unit_addr = e.alloc(0x200)
        unit = e.view("unit", unit_addr)
        unit.type, unit["class"], unit.mode = 1, cls, 1   # a monster of the hireling's class, standing
        state["unit"], state["max"] = unit_addr, max_life << 8
        stats.clear()
        stats[(unit_addr, 6)] = life
        stats[(unit_addr, 7)] = max_life << 8
        stats[(unit_addr, MDR_STAT)] = mdr
        for type_, res, pct, flat in ((0, res_fire, p_fire, f_fire), (1, res_light, p_light, f_light), (2, res_cold, p_cold, f_cold), (4, 0, p_magic, f_magic)):
            _, res_stat, pct_stat, flat_stat = ROWS[type_]
            stats[(unit_addr, res_stat)] = res
            stats[(unit_addr, pct_stat)] = pct
            stats[(unit_addr, flat_stat)] = flat
        for blow in blows:
            if stats[(unit_addr, 6)] <= 0:
                break
            record_addr = e.alloc(0x100)
            record = e.view("damage", record_addr)
            record.flags = 1 | 4
            e.w32(record_addr + 4, 1)
            for type_, field in blow:
                e.w32(record_addr + ROWS[type_][0], field << 8)
            e.call(0x57C6C0, unit_addr, 1, record_addr, ecx=game_addr, edx=attacker_addr)
        print(index, stats[(unit_addr, 6)])


def compare(cases, gfile, dfile):
    case_lines, game_lines, d2d_lines = read(cases), read(gfile), read(dfile)
    if not case_lines:
        raise SystemExit("compare: empty case file")
    if not (len(case_lines) == len(game_lines) == len(d2d_lines)):
        raise SystemExit(f"compare: lengths differ ({len(case_lines)}, {len(game_lines)}, {len(d2d_lines)})")
    bad, by_kind = 0, {}
    for case_line, g_line, d_line in zip(case_lines, game_lines, d2d_lines, strict=True):
        head, blows = parse(case_line)
        g, d = g_line.split(), d_line.split()
        if len(g) != 2 or len(d) != 2 or g[0] != d[0] or g[0] != str(head[0]) or not (g[1].lstrip("-").isdigit() and d[1].lstrip("-").isdigit()):
            raise SystemExit(f"compare: malformed or out-of-step line near id {head[0]}")
        differ = g[1] != d[1]
        absorbing = any(head[7 + k] > 0 or head[11 + k] > 0 for k in range(4))
        kind = f"class {head[15]} " + ("absorb" if absorbing else "plain")
        row = by_kind.setdefault(kind, [0, 0])
        row[0] += 1
        row[1] += differ
        bad += differ
    for kind, (total, differ) in sorted(by_kind.items()):
        print(f"# {kind}: {total} cases, {differ} differ")
    print(f"# total: {len(case_lines)} cases, {bad} differ")
    sys.exit(1 if bad else 0)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("gen", "game", "compare"):
        raise SystemExit(__doc__)
    cmd = sys.argv[1]
    if cmd == "gen" and len(sys.argv) == 4:
        print("\n".join(gen(int(sys.argv[2]), int(sys.argv[3]))))
    elif cmd == "game" and len(sys.argv) == 3:
        game(sys.argv[2])
    elif cmd == "compare" and len(sys.argv) == 5:
        compare(*sys.argv[2:5])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
