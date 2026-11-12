# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's char panel skill damage handlers, run by their own code.

    uv run --project tools/emu python tools/emu/panel_damage.py

The descdam table at 0x72d768 picks the handler that fills the panel's
"Damage" value (FUN_004ed570). Run for real here: descdam 5
(FUN_004ead60), 9 (FUN_004eb390), 24 (FUN_004eae70), FUN_004e9660 (the SrcDam share of the
weapon) and FUN_004e6fb0 (EType's colour). Hooked: FUN_00647bc0 /
FUN_00647d00 (the skill's MinDam / MaxDam), FUN_00644d50 / FUN_00644e40
(EMin / EMax with mastery), FUN_00644f20 (ELen), FUN_004e86f0 (the
weapon's physical damage) and FUN_004e89a0 (its elements, its clamp and
damblue / damred at its end, as 004e8b25 and 004e8d58 read). FUN_004e96e0
(the draw) records min, max and colour. d2d's rules::attack_line
(components/rules/skills.hpp) is the Python below; prints the cases that
differ.
"""
import itertools
import random
import sys

from emu import Emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

TABLE = 0x72D768                  # descdam -> handler
PHYS_MIN, PHYS_MAX, ELEM_MIN, ELEM_MAX, ELEM_LEN = 0x647BC0, 0x647D00, 0x644D50, 0x644E40, 0x644F20
WEAPON, WEAPON_ELEMENTS, DRAW = 0x4E86F0, 0x4E89A0, 0x4E96E0
GAME_ETYPE = {-1: 0, 0: 1, 1: 2, 2: 4, 3: 5, 4: 3, 5: 9}     # d2d's etype (fire ltng cold pois mag stun) -> EType byte
ELEM_COLOUR = (1, 9, 3, 2, 3)                                  # d2d's kElemColour
HANDLERS = (5, 9, 24)


def d2d_line(descdam, case):
    """rules::attack_line's damage for these handlers, from the hooks' answers."""
    etype_colour = ELEM_COLOUR[case["etype"]] if 0 <= case["etype"] < 5 else 0
    low, high, colour = 0, 0, etype_colour

    def weapon_share():
        nonlocal low, high, colour
        if case["srcdam"] == 0:
            return
        w_low, w_high = case["w_low"], case["w_high"]
        if case["we_low"] or case["we_high"]:
            w_low += min(case["we_low"], case["we_high"]); w_high += case["we_high"]; colour = case["we_colour"]
        if w_high > 0:
            w_low = max(w_low, 1); w_high = max(w_high, w_low + 1)
        low += int(w_low * case["srcdam"] / 128); high += int(w_high * case["srcdam"] / 128)
        colour = 1 if case["red"] else 3 if case["blue"] else colour

    def own_damage():
        nonlocal low, high
        length = case["elen"] if case["etype"] == 3 else 1
        low += case["phys_min"] >> 8; high += case["phys_max"] >> 8
        low += case["emin"] * length >> 8; high += case["emax"] * length >> 8

    if descdam in (5, 24):
        weapon_share()
        own_damage()
    elif descdam == 9:
        shift = case["hitshift"]
        low = ((case["mindam"] << shift) + case["emin"]) * 75 >> 8
        high = ((case["maxdam"] << shift) + case["emax"]) * 75 >> 8
        weapon_share()
    return low, high, colour


def main():
    e = Emu()
    e.crt_init()
    case = {}
    drawn = []
    e.hook(PHYS_MIN, lambda _e: case["phys_min"], 4)
    e.hook(PHYS_MAX, lambda _e: case["phys_max"], 4)
    e.hook(ELEM_MIN, lambda _e: case["emin"], 4)
    e.hook(ELEM_MAX, lambda _e: case["emax"], 4)
    e.hook(ELEM_LEN, lambda _e: case["elen"], 4)

    def weapon(em):                                   # (&min, &max, &colour, ..): the physical part
        em.w32(em.arg(0), case["w_low"]); em.w32(em.arg(1), case["w_high"])
        return 0

    def weapon_elements(em):                          # (unit, &min, &max, &colour, node)
        low_at, high_at, colour_at = em.arg(1), em.arg(2), em.arg(3)
        if case["we_low"] or case["we_high"]:
            em.w32(low_at, em.r32(low_at) + case["we_low"]); em.w32(high_at, em.r32(high_at) + case["we_high"])
            em.w32(colour_at, case["we_colour"])
        low, high = signed(em.r32(low_at)), signed(em.r32(high_at))
        if high > 0:
            low = max(low, 1); em.w32(low_at, low); em.w32(high_at, max(high, low + 1))
        if case["blue"]: em.w32(colour_at, 3)
        if case["red"]: em.w32(colour_at, 1)
        return 0

    def draw(em):
        drawn.append((signed(em.mu.reg_read(UC_X86_REG_ECX)), signed(em.mu.reg_read(UC_X86_REG_EDX)), em.arg(0)))
        return 0

    e.hook(WEAPON, weapon, 8)
    e.hook(WEAPON_ELEMENTS, weapon_elements, 5)
    e.hook(DRAW, draw, 2)
    unit, node, row = e.alloc(0x100), e.alloc(0x100), e.alloc(0x23C)
    rng = random.Random(1114)
    bad = runs = 0
    for descdam, trial in itertools.product(HANDLERS, range(4000)):
        case.update(
            etype=rng.choice(list(GAME_ETYPE)), hitshift=rng.randrange(9), srcdam=rng.choice((0, 0, 32, 64, 100, 128, 255)),
            mindam=rng.randrange(-5, 60), maxdam=rng.randrange(-5, 90),
            phys_min=rng.randrange(-2000, 40000), phys_max=rng.randrange(-2000, 60000),
            emin=rng.randrange(-3000, 80000), emax=rng.randrange(-3000, 120000), elen=rng.choice((0, 1, 25, 75, 200)),
            w_low=rng.randrange(-20, 300), w_high=rng.randrange(-20, 500),
            we_low=0, we_high=0, we_colour=rng.choice((1, 2, 3, 9)), blue=rng.random() < 0.2, red=rng.random() < 0.2)
        if rng.random() < 0.5:
            case["we_low"] = rng.randrange(0, 40); case["we_high"] = case["we_low"] + rng.randrange(0, 60)
        e.mu.mem_write(row, bytes(0x23C))
        e.mu.mem_write(row, (descdam * 7 + trial).to_bytes(2, "little"))
        e.mu.mem_write(row + 0x1A4, bytes((case["hitshift"], case["srcdam"])))
        e.w32(row + 0x1A8, case["mindam"]); e.w32(row + 0x1AC, case["maxdam"])
        e.mu.mem_write(row + 0x1DC, bytes((GAME_ETYPE[case["etype"]],)))
        drawn.clear()
        e.call(e.r32(TABLE + 4 * descdam), row, 5, 0, 0, 0, ecx=unit, edx=node)
        runs += 1
        want = d2d_line(descdam, case)
        if len(drawn) != 1 or drawn[0] != want:
            bad += 1
            if bad <= 10:
                print(f"descdam {descdam}: game {drawn} d2d {want} {case}")
    print(f"{bad} of {runs} differ")
    return 1 if bad else 0


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


if __name__ == "__main__":
    sys.exit(main())
