# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's char panel skill damage handlers, run by their own code.

    uv run --project tools/emu python tools/emu/panel_damage.py

The descdam table at 0x72d768 picks the handler that fills the panel's
"Damage" value (FUN_004ed570). Run for real here: descdam 5
(FUN_004ead60), 8 (FUN_004eb290), 9 (FUN_004eb390), 10 (FUN_004eb460),
11 (FUN_004eb640),
12 (FUN_004eb830), 17 (FUN_004ebd50),
24 (FUN_004eae70), FUN_004e72d0 (the element and its colour),
FUN_004e9660 (the SrcDam share of the weapon) and FUN_004e6fb0 (EType's
colour). Hooked: FUN_00647bc0 / FUN_00647d00 (the skill's MinDam /
MaxDam), FUN_00644d50 / FUN_00644e40 (EMin / EMax with mastery),
FUN_00644f20 (ELen), FUN_004e86f0 (the weapon's physical damage),
FUN_004e89a0 (its elements, its clamp and damblue / damred at its end, as
004e8b25 and 004e8d58 read), FUN_004a89d0 (the SkillDesc row),
FUN_00643ce0 and FUN_00646d00 (its ddam calc1 / calc2), FUN_0044dcc0
(expansion), FUN_006461d0 (Concentration's share), FUN_00625480 (a unit
stat), FUN_00623990 / FUN_0063d340 (a weapon, two-handed), FUN_00646ca0
(calc1..3), FUN_0063c8f0 / FUN_006335f0 / FUN_00629860 / FUN_006298a0 (the
shield, its record, Str / DexBonus), FUN_006439b0 / FUN_00639df0 /
FUN_006442a0 (Holy Shield's node, state, level), FUN_004cc7c0 (Smite's %),
FUN_0063a380 / FUN_0063a3a0 (damblue / damred). FUN_004e96e0 (the
draw) records min, max and colour, FUN_004e9870 (two values) its two strings and
colours. d2d's rules::attack_line
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
WEAPON, WEAPON_ELEMENTS, DRAW, DRAW_TWO = 0x4E86F0, 0x4E89A0, 0x4E96E0, 0x4E9870
SKILL_DESC, SKILL_ID, CALC = 0x4A89D0, 0x643CE0, 0x646D00
EXPANSION, CONCENTRATION, STAT = 0x44DCC0, 0x6461D0, 0x625480
HAS_WEAPON, WIELD, CALC_AT = 0x623990, 0x63D340, 0x646CA0
SHIELD, ITEM_RECORD, STR_BONUS, DEX_BONUS = 0x63C8F0, 0x6335F0, 0x629860, 0x6298A0
SKILL_NODE, HAS_STATE, NODE_LEVEL, PARAM_PCT, DAM_BLUE, DAM_RED = 0x6439B0, 0x639DF0, 0x6442A0, 0x4CC7C0, 0x63A380, 0x63A3A0
GAME_ETYPE = {-1: 0, 0: 1, 1: 2, 2: 4, 3: 5, 4: 3, 5: 9}     # d2d's etype (fire ltng cold pois mag stun) -> EType byte
ELEM_COLOUR = (1, 9, 3, 2, 3)                                  # d2d's kElemColour
HANDLERS = (5, 8, 9, 10, 11, 12, 17, 24)


def d2d_line(descdam, case):
    """rules::attack_line's damage for these handlers, from the hooks' answers."""
    etype_colour = ELEM_COLOUR[case["etype"]] if 0 <= case["etype"] < 5 else 0
    low, high, colour = 0, 0, etype_colour

    def weapon_part():
        """FUN_004e86f0 + FUN_004e89a0: the weapon with its elements, clamped, damblue / damred."""
        w_low, w_high, w_colour = case["w_low"], case["w_high"], 0
        if case["we_low"] or case["we_high"]:
            w_low += case["we_low"]; w_high += case["we_high"]; w_colour = case["we_colour"]
        if w_high > 0:
            w_low = max(w_low, 1); w_high = max(w_high, w_low + 1)
        return w_low, w_high, 1 if case["red"] else 3 if case["blue"] else w_colour

    def element_part():
        """FUN_004e72d0."""
        length = case["elen"] if case["etype"] == 3 else 1
        return case["emin"] * length >> 8, case["emax"] * length >> 8, etype_colour

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
    elif descdam == 17:
        upper, lower = weapon_part(), element_part()
        if upper[:2] == (0, 0):
            return lower
        if lower[:2] == (0, 0):
            return upper
        return ("two", two_text(*upper[:2]), two_text(*lower[:2]), upper[2], lower[2])
    elif descdam == 10:
        if not case["shield"]:
            return None
        stats = case["stats"]
        low, high = case["sh_min"], case["sh_max"]
        if case["holy_node"] and case["holy_state"]:
            low += case["phys_min"] >> 8; high += case["phys_max"] >> 8
        pct = case["smite_pct"]
        if case["str_bonus"]:
            pct += trunc_div(stats.get(0, 0) * case["str_bonus"], 100)
        if case["dex_bonus"]:
            pct += trunc_div(stats.get(2, 0) * case["dex_bonus"], 100)
        pct = max(pct + stats.get(25, 0), -90)
        low += trunc_div(pct * low + stats.get(18, 0), 100); high += trunc_div(pct * high + stats.get(17, 0), 100)
        colour = 1 if case["red"] else 3 if case["blue"] else 0
    elif descdam == 11:
        weapon_share()
        stats = case["stats"]
        two = case["weapon"] and case["wield"] == 2
        w_low, w_high = stats.get(23 if two else 21, 0), stats.get(24 if two else 22, 0)
        w_low = max(w_low, 1); w_high = max(w_high, 2, w_low)
        for calc_id, mastery in ((11, 329), (12, 331), (13, 330)):
            pct = case["calcs"][calc_id]
            if pct and stats.get(mastery, 0):
                pct += mul_div(pct, stats[mastery], 100)
            low += mul_div(w_low, pct, 100); high += mul_div(w_high, pct, 100)
        colour = colour or 3
    elif descdam == 12:
        length = case["elen"] if case["etype"] == 3 else 1
        low = case["emin"] * length >> 8; high = case["emax"] * length >> 8
        share = case["concentration"] if case["expansion"] else max(case["stats"].get(25, 0), -90)
        low += trunc_div(low * share, 100); high += trunc_div(high * share, 100)
    elif descdam == 8:
        times, per = case["calc1"] or 1, case["calc2"] or 1
        low = trunc_div(case["emin"] * times * 25, per) >> 8
        high = trunc_div(case["emax"] * times * 25, per) >> 8
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

    e.hook(EXPANSION, lambda _e: int(case["expansion"]), 0)
    e.hook(CONCENTRATION, lambda _e: case["concentration"], 2)
    e.hook(STAT, lambda em: case["stats"].get(em.arg(1), 0) & 0xFFFFFFFF, 3)
    e.hook(HAS_WEAPON, lambda _e: int(case["weapon"]), 2)
    e.hook(WIELD, lambda _e: case["wield"] & 0xFFFFFFFF, 2)
    e.hook(CALC_AT, lambda em: case["calcs"].get(em.arg(1), 0) & 0xFFFFFFFF, 4)
    item, record = e.alloc(0x100), e.alloc(0x200)

    def shield(em):                                   # (inventory, &item)
        em.w32(em.arg(1), item if case["shield"] else 0)
        return int(case["shield"])

    e.hook(SHIELD, shield, 2)
    e.hook(ITEM_RECORD, lambda _e: record, 1)
    e.hook(STR_BONUS, lambda _e: case["str_bonus"] & 0xFFFF, 1)
    e.hook(DEX_BONUS, lambda _e: case["dex_bonus"] & 0xFFFF, 1)
    e.hook(SKILL_NODE, lambda _e: 0x5000 if case["holy_node"] else 0, 1)
    e.hook(HAS_STATE, lambda _e: int(case["holy_state"]), 2)
    e.hook(NODE_LEVEL, lambda _e: 7, 3)
    e.hook(PARAM_PCT, lambda _e: case["smite_pct"] & 0xFFFFFFFF, 0)
    e.hook(DAM_BLUE, lambda _e: int(case["blue"]), 1)
    e.hook(DAM_RED, lambda _e: int(case["red"]), 1)
    def wide(em, at):
        text = ""
        while (unit_char := em.mu.mem_read(at + 2 * len(text), 2)) != b"\0\0":
            text += unit_char.decode("utf-16-le")
        return text

    def draw_two(em):
        drawn.append(("two", wide(em, em.mu.reg_read(UC_X86_REG_ECX)), wide(em, em.mu.reg_read(UC_X86_REG_EDX)), em.arg(3), em.arg(4)))
        return 0

    e.hook(DRAW_TWO, draw_two, 5)
    e.hook(WEAPON, weapon, 8)
    e.hook(WEAPON_ELEMENTS, weapon_elements, 5)
    e.hook(DRAW, draw, 2)
    unit, node, row, desc = e.alloc(0x100), e.alloc(0x100), e.alloc(0x23C), e.alloc(0x120)
    e.w32(desc + 0x18, 1); e.w32(desc + 0x1C, 2)                  # ddam calc1 / calc2: calc ids 1 and 2
    e.hook(SKILL_DESC, lambda _e: desc, 0)
    e.hook(SKILL_ID, lambda _e: 77, 3)
    e.hook(CALC, lambda em: case["calc1"] if em.arg(1) == 1 else case["calc2"], 4)
    rng = random.Random(1114)
    bad = runs = 0
    for descdam, trial in itertools.product(HANDLERS, range(4000)):
        case.update(
            etype=rng.choice(list(GAME_ETYPE)), hitshift=rng.randrange(9), srcdam=rng.choice((0, 0, 32, 64, 100, 128, 255)),
            mindam=rng.randrange(-5, 60), maxdam=rng.randrange(-5, 90),
            phys_min=rng.randrange(-2000, 40000), phys_max=rng.randrange(-2000, 60000),
            emin=rng.randrange(-3000, 80000), emax=rng.randrange(-3000, 120000), elen=rng.choice((0, 1, 25, 75, 200)),
            w_low=rng.randrange(-20, 300), w_high=rng.randrange(-20, 500),
            we_low=0, we_high=0, we_colour=rng.choice((1, 2, 3, 9)), blue=rng.random() < 0.2, red=rng.random() < 0.2,
            calc1=rng.choice((0, 0, 1, 2, 3, -2, 40)), calc2=rng.choice((0, 0, 1, 3, 7, -3)),
            expansion=rng.random() < 0.5, concentration=rng.choice((0, 0, 15, 40, 113, 262)),
            stats={25: rng.choice((0, 0, 30, 125, -95, -40)), 21: rng.randrange(-3, 60), 22: rng.randrange(-3, 120),
                   23: rng.randrange(-3, 90), 24: rng.randrange(-3, 200), 329: rng.choice((0, 0, 30, 210)),
                   330: rng.choice((0, 0, 50, 180)), 331: rng.choice((0, 0, 20, 140)),
                   0: rng.randrange(10, 400), 2: rng.randrange(10, 300), 17: rng.choice((0, 0, 150, 37)), 18: rng.choice((0, 0, 99, 250))},
            weapon=rng.random() < 0.7, wield=rng.choice((-1, 1, 2)),
            shield=rng.random() < 0.8, sh_min=rng.randrange(0, 30), sh_max=rng.randrange(0, 60),
            holy_node=rng.random() < 0.6, holy_state=rng.random() < 0.6, smite_pct=rng.choice((0, 15, 90, 300, -200)),
            str_bonus=rng.choice((0, 0, 50, 100, -10)), dex_bonus=rng.choice((0, 0, 50, 75)),
            calcs={11: rng.choice((0, 30, 70, 300)), 12: rng.choice((0, 30, 70, 300)), 13: rng.choice((0, 30, 70, 300, -20))})
        if rng.random() < 0.5:
            case["we_low"] = rng.randrange(0, 40); case["we_high"] = case["we_low"] + rng.randrange(0, 60)
        e.mu.mem_write(row, bytes(0x23C))
        e.mu.mem_write(row, (descdam * 7 + trial).to_bytes(2, "little"))
        e.mu.mem_write(row + 0x1A4, bytes((case["hitshift"], case["srcdam"])))
        e.w32(row + 0x1A8, case["mindam"]); e.w32(row + 0x1AC, case["maxdam"])
        e.mu.mem_write(row + 0x1DC, bytes((GAME_ETYPE[case["etype"]],)))
        e.w32(row + 0x138, 11); e.w32(row + 0x13C, 12); e.w32(row + 0x140, 13)
        e.mu.mem_write(record + 0xFE, bytes((case["sh_min"], case["sh_max"])))
        drawn.clear()
        e.call(e.r32(TABLE + 4 * descdam), row, 5, 0, 0, 0, ecx=unit, edx=node)
        runs += 1
        want = d2d_line(descdam, case)
        if (want is None and drawn) or (want is not None and (len(drawn) != 1 or drawn[0] != want)):
            bad += 1
            if bad <= 10:
                print(f"descdam {descdam}: game {drawn} d2d {want} {case}")
    print(f"{bad} of {runs} differ")
    return 1 if bad else 0


def two_text(low, high):
    """FUN_004e9870's strings: "%d" when equal, else FUN_004e96e0's forms."""
    if low == high:
        return str(low)
    if low >= 10000:
        return f"{trunc_div(low + 500, 1000)}K-{trunc_div(high + 500, 1000)}K"
    if high >= 10000:
        return f"{low}-{trunc_div(high + 500, 1000)}K"
    return f"{low}-{high}"


def mul_div(value, times, over):
    """FUN_00483360 (rules::mul_div): its 32-bit products wrap."""
    if over == 0:
        return 0
    if value <= 0x100000:
        if times <= 0x10000:
            return trunc_div(signed((value * times) & 0xFFFFFFFF), over)
        if over <= times >> 4:
            return signed((trunc_div(times, over) * value) & 0xFFFFFFFF)
    elif over <= value >> 4:
        return signed((trunc_div(value, over) * times) & 0xFFFFFFFF)
    return trunc_div(value * times, over)


def trunc_div(dividend, divisor):
    quotient = abs(dividend) // abs(divisor)
    return quotient if (dividend < 0) == (divisor < 0) else -quotient


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


if __name__ == "__main__":
    sys.exit(main())
