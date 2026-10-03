# SPDX-License-Identifier: GPL-3.0-or-later
"""An object's blast (FUN_005dfa00: the trap object, the exploding barrel)
against rules::object_blast: random lives, levels, dexterity, defense and
object seeds, both blast types (0 physical, 1 fire).

    uv run python blast.py [cases] [seed]

The unit / room / stat helpers it calls are hooked (no town, no wall, stats
from the case, objects.txt Damage 100); the damage record it hands
FUN_0057c1e0 is read back (+8 physical, +0x10 fire). Prints `ok` or each
mismatch; `--dump` prints the cases as C++ test lines.
"""
import random
import sys

import emu


def rand(seed, bound):
    """FUN_0045c3e0 on seed = [low, high]."""
    if bound < 1: return 0
    product = seed[0] * 0x6AC690C5 + seed[1]
    seed[0], seed[1] = product & 0xFFFFFFFF, product >> 32
    return seed[0] & (bound - 1) if bound & (bound - 1) == 0 else seed[0] % bound


def port(life, level, dex, defense, seed):
    """components/rules/shrines.hpp object_blast, line for line."""
    low = max(life >> 5, 1)
    high = max(life >> 3, low + 1)
    roll = (rand(seed, level >> 2) + level) & 0xFF
    chance = max((roll - (dex >> 1) * 5 - level) * 2 - defense + 125, 65)
    product = seed[0] * 0x6AC690C5 + seed[1]
    seed[0], seed[1] = product & 0xFFFFFFFF, product >> 32
    if seed[0] % 100 >= chance: return 0
    return low + rand(seed, high - low + 256)


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 3000
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    dump = "--dump" in sys.argv
    e = emu.Emu()
    stats = {}
    got = {}
    objtxt = e.alloc(0x200); e.mu.mem_write(objtxt + 0x19c, bytes([100]))
    e.hook(0x620bb0, lambda e: 0x1000, 1)                          # the unit's room
    e.hook(0x61a1b0, lambda e: 2, 1)                               # its level: the Blood Moor
    e.hook(0x61ab00, lambda e: 0, 1)                               # not a town
    e.hook(0x622b50, lambda e: 0, 3)                               # no wall between
    e.hook(0x625480, lambda e: stats.get(e.arg(1), 0), 3)          # GetStat(unit, stat, layer)
    e.hook(0x640e90, lambda e: objtxt, 1)
    e.hook(0x639df0, lambda e: 0, 2)

    def record(e):
        rec = e.arg(1)
        got["phys"], got["fire"] = e.s32(rec + 8), e.s32(rec + 0x10)
        return 0
    e.hook(0x57c1e0, record, 2)
    e.hook(0x57c6c0, lambda e: 0, 3)
    e.hook(0x57cee0, lambda e: 0, 2)
    game, target, obj = e.alloc(0x200), e.alloc(0x100), e.alloc(0x100)
    e.w32(obj + 4, 250)
    bad = hits = 0
    for case in range(cases):
        life = rng.choice((rng.randrange(1, 64), rng.randrange(256, 2000 << 8)))
        level, dex = rng.randrange(1, 100), rng.choice((0, rng.randrange(0, 40), rng.randrange(15, 300)))
        defense = rng.choice((0, rng.randrange(0, 200), rng.randrange(0, 3000)))
        kind = rng.randrange(2)
        low, high = rng.getrandbits(32), rng.getrandbits(32)
        stats.clear(); stats.update({6: life, 0xc: level, 2: dex, 0x1f: defense})
        got.clear()
        e.w32(obj + 0x20, low); e.w32(obj + 0x24, high)
        e.call(0x5dfa00, target, kind, ecx=game, edx=obj)
        game_damage = got.get("fire" if kind else "phys", 0)
        seed = [low, high]
        want = port(life, level, dex, defense, seed)
        hits += bool(game_damage)
        if game_damage != want or [e.r32(obj + 0x20), e.r32(obj + 0x24)] != seed:
            bad += 1
            print(f"case {case}: life {life} lvl {level} dex {dex} def {defense} seed {low:#x},{high:#x}: game {game_damage}, port {want}")
        if dump and case < 12:
            print(f"{{ {life}, {level}, {dex}, {defense}, {low:#x}u, {high:#x}u, {game_damage} }},")
    print(f"ok: {cases} cases, {hits} hits" if not bad else f"{bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
