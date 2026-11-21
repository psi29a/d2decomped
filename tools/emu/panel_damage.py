# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's char panel skill damage handlers, run by their own code.

    uv run --project tools/emu python tools/emu/panel_damage.py

The descdam table at 0x72d768 picks the handler that fills the panel's
"Damage" value (FUN_004ed570). Run for real here: descdam 1 (FUN_004ea170),
5
(FUN_004ead60), 6 (FUN_004e9d20), 8 (FUN_004eb290), 9 (FUN_004eb390), 10 (FUN_004eb460),
11 (FUN_004eb640),
12 (FUN_004eb830), 13 (FUN_004eb240), 17 (FUN_004ebd50), 19 (FUN_004ea310),
20 (FUN_004ea240), the two hands' FUN_004e99f0 / FUN_004e93a0 / FUN_004e9610, 21 (FUN_004ea790), 22 (FUN_004eaaa0)
with FUN_004ea4a0 (the throw),
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
FUN_0063a380 / FUN_0063a3a0 (damblue / damred), FUN_0063bef0 /
FUN_0062ba80 / FUN_00629bb0 (the wielded weapon, throwable, a tpot) and
FUN_00645830 (its mastery), FUN_006235a0 (a two-handed class),
FUN_0063bde0 / FUN_0062a4e0 (a hand's item, usable), FUN_004e9300 /
FUN_004e9260 (the wielded hand's weapon, the other's), FUN_00627910 (their
stats on and off). FUN_004e96e0 (the
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
WIELDED, THROWABLE, ITEM_TYPE_IS, MASTERY = 0x63BEF0, 0x62BA80, 0x629BB0, 0x645830
DUAL_CLASS, HAND_ITEM, USABLE, MAIN_HAND, OTHER_HAND, ITEM_STATS = 0x6235A0, 0x63BDE0, 0x62A4E0, 0x4E9300, 0x4E9260, 0x627910
SKILL_NODE, HAS_STATE, NODE_LEVEL, PARAM_PCT, DAM_BLUE, DAM_RED = 0x6439B0, 0x639DF0, 0x6442A0, 0x4CC7C0, 0x63A380, 0x63A3A0
GAME_ETYPE = {-1: 0, 0: 1, 1: 2, 2: 4, 3: 5, 4: 3, 5: 9}     # d2d's etype (fire ltng cold pois mag stun) -> EType byte
ELEM_COLOUR = (1, 9, 3, 2, 3)                                  # d2d's kElemColour
HANDLERS = (1, 5, 6, 8, 9, 10, 11, 12, 13, 17, 19, 20, 21, 22, 24)


def d2d_line(descdam, case):
    """rules::attack_line's damage for these handlers, from the hooks' answers."""
    etype_colour = ELEM_COLOUR[case["etype"]] if 0 <= case["etype"] < 5 else 0
    low, high, colour = 0, 0, etype_colour

    def weapon_part(hand=None, start=0):
        """FUN_004e86f0 + FUN_004e89a0: the weapon with its elements, clamped, damblue / damred."""
        w_low, w_high, w_colour = (case[f"w_low_{hand}"], case[f"w_high_{hand}"], start) if hand else (case["w_low"], case["w_high"], start)
        if case["we_low"] or case["we_high"]:
            w_low += case["we_low"]; w_high += case["we_high"]; w_colour = case["we_colour"]
        if w_high > 0:
            w_low = max(w_low, 1); w_high = max(w_high, w_low + 1)
        return w_low, w_high, 1 if case["red"] else 3 if case["blue"] else w_colour

    def hand_part(descdam, hand, start):
        """FUN_004e9610 (a weapon) or FUN_004ea4a0 (Double Throw's throw) on one hand, None the unit."""
        if descdam != 13:
            return weapon_part(hand, start)
        stats = case["stats"]
        item_ok = case[f"hand_{hand}"] if hand else case["wielded"]
        if not item_ok or not case[f"throwable_{hand}" if hand else "throwable"]:
            return 0, 0, start
        str_bonus, dex_bonus = (case[f"str_bonus_{hand}"], case[f"dex_bonus_{hand}"]) if hand else (case["str_bonus"], case["dex_bonus"])
        pct = 0
        if str_bonus:
            pct += trunc_div(stats.get(0, 0) * str_bonus, 100)
        if dex_bonus:
            pct += trunc_div(stats.get(2, 0) * dex_bonus, 100)
        pct = max(pct + stats.get(25, 0), -90)
        low = stats.get(159, 0) + trunc_div((stats.get(18, 0) + pct) * stats.get(159, 0), 100)
        high = stats.get(160, 0) + trunc_div((stats.get(17, 0) + pct) * stats.get(160, 0), 100)
        extra = case["calcs"][11] + (case["mastery"] if case["throw_state"] else 0)
        low += mul_div(low, extra, 100); high += mul_div(high, extra, 100)
        high = max(high, low)
        e_low, e_high, colour = 0, 0, start
        if case["we_low"] or case["we_high"]:
            e_low, e_high, colour = case["we_low"], case["we_high"], case["we_colour"]
        if e_high > 0:
            e_low = max(e_low, 1); e_high = max(e_high, e_low + 1)
        colour = 1 if case["red"] else 3 if case["blue"] else colour
        return low + e_low, high + max(e_high, e_low), colour

    def element_part():
        """FUN_004e72d0."""
        length = case["elen"] if case["etype"] == 3 else 1
        return case["emin"] * length >> 8, case["emax"] * length >> 8, etype_colour

    def gear_element(low, high):
        """FUN_004e8d90: the unit's own damage of the skill's element."""
        stats = case["stats"]
        if case["etype"] == 3:
            length = stats.get(101, 0)
            if length < 1:
                length = trunc_div(stats.get(59, 0), max(stats.get(326, 0), 1))
            return low + (stats.get(57, 0) * length >> 8), high + (stats.get(58, 0) * length >> 8)
        if 0 <= case["etype"] < 5:
            own = (48, 50, 54, 57, 52)[case["etype"]]
            return low + stats.get(own, 0), high + stats.get(own + 1, 0)
        return low, high

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
    elif descdam == 6:
        colour = 0
        weapon_share()
        upper = (low, high, colour)
        stats = case["stats"]
        low = (case["phys_min"] >> 8) + (case["emin"] >> 8); high = (case["phys_max"] >> 8) + (case["emax"] >> 8)
        low, high = gear_element(low, high)
        lower = (low, high, etype_colour)
        if upper[:2] == (0, 0):
            return lower
        if lower[:2] == (0, 0):
            return upper
        return ("two", two_text(*upper[:2]), two_text(*lower[:2]), upper[2], lower[2])
    elif descdam in (1, 13, 19, 20) and (descdam == 13 or case["dual"]):
        element = element_part() if descdam != 20 else (0, 0, 0)
        first, second = (0, 0, element[2]), (0, 0, element[2])
        if case["dual"]:
            hand_a = case["hand_a"] and case["usable_a"] and not case["potion_a"]
            hand_b = case["hand_b"] and case["usable_b"] and not case["potion_b"]
            if hand_a and hand_b:
                if case["other"]:
                    second = hand_part(descdam, "b", element[2])
                if case["main"]:
                    first = hand_part(descdam, "a", element[2])
            else:
                first = hand_part(descdam, None, element[2])
        scaled = []
        for value in (first, second):
            if value[0] != 0 and value[1] != 0:
                value = (trunc_div(value[0] * case["srcdam"], 128) + element[0], trunc_div(value[1] * case["srcdam"], 128) + element[1], value[2])
            scaled.append(value)
        first, second = scaled
        if first[:2] == (0, 0):
            return second
        if second[:2] == (0, 0):
            return first
        return ("two", two_text(*first[:2]), two_text(*second[:2]), first[2], second[2])
    elif descdam == 19:
        return weapon_part()
    elif descdam in (1, 20):
        return d2d_line(7, case)
    elif descdam == 7:
        low, high, colour = weapon_part()
        if case["srcdam"] != 128:
            low, high = mul_div(low, case["srcdam"], 128), mul_div(high, case["srcdam"], 128)
        low += (case["phys_min"] >> 8) + (case["emin"] >> 8); high += (case["phys_max"] >> 8) + (case["emax"] >> 8)
        return low, high, colour
    elif descdam in (21, 22):
        stats = case["stats"]
        thrown = case["wielded"] and case["throwable"]
        if descdam == 21 and not thrown:
            return None
        low = high = colour = 0
        if thrown and not (descdam == 22 and case["potion"]):
            pct = 0
            if case["str_bonus"]:
                pct += trunc_div(stats.get(0, 0) * case["str_bonus"], 100)
            if case["dex_bonus"]:
                pct += trunc_div(stats.get(2, 0) * case["dex_bonus"], 100)
            pct = max(pct + stats.get(25, 0), -90)
            low = stats.get(159, 0) + trunc_div((stats.get(18, 0) + pct) * stats.get(159, 0), 100)
            high = stats.get(160, 0) + trunc_div((stats.get(17, 0) + pct) * stats.get(160, 0), 100)
            mastery = case["mastery"] if case["throw_state"] else 0
            low += mul_div(low, mastery, 100); high += mul_div(high, mastery, 100)
            if descdam == 21:
                low, high = gear_element(low, high)
                high = max(high, low)
            else:
                high = max(high, low)
                e_low, e_high, colour = 0, 0, 0
                if case["we_low"] or case["we_high"]:
                    e_low, e_high, colour = case["we_low"], case["we_high"], case["we_colour"]
                if e_high > 0:
                    e_low = max(e_low, 1); e_high = max(e_high, e_low + 1)
                colour = 1 if case["red"] else 3 if case["blue"] else colour
                low += e_low; high += max(e_high, e_low)
        low, high = mul_div(low, case["srcdam"], 128), mul_div(high, case["srcdam"], 128)
        element = element_part()
        if descdam == 21:
            return low + element[0], high + element[1], element[2]
        upper = (low, high, colour)
        if upper[:2] == (0, 0):
            return element
        if element[:2] == (0, 0):
            return upper
        return ("two", two_text(*upper[:2]), two_text(*element[:2]), upper[2], element[2])
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

    def weapon(em):                                   # (&min, &max, &colour, calc2, node, share, item, 0): the physical part
        hand = "a" if em.arg(6) == item_a else "b" if em.arg(6) == item_b else None
        em.w32(em.arg(0), case[f"w_low_{hand}"] if hand else case["w_low"]); em.w32(em.arg(1), case[f"w_high_{hand}"] if hand else case["w_high"])
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
    item_a, item_b = e.alloc(0x100), e.alloc(0x100)

    def by_item(at, key):                             # a per-hand case value for the item at `at`
        return case[f"{key}_a"] if at == item_a else case[f"{key}_b"] if at == item_b else case[key]

    e.hook(DUAL_CLASS, lambda _e: int(case["dual"]), 1)
    e.hook(HAND_ITEM, lambda em: (item_a if case["hand_a"] else 0) if em.arg(1) == 4 else (item_b if case["hand_b"] else 0), 2)
    e.hook(USABLE, lambda em: int(by_item(em.arg(0), "usable")), 1)
    e.hook(MAIN_HAND, lambda _e: item_a if case["main"] else 0, 0)
    e.hook(OTHER_HAND, lambda _e: item_b if case["other"] else 0, 0)
    e.hook(ITEM_STATS, lambda _e: 0, 3)

    def shield(em):                                   # (inventory, &item)
        em.w32(em.arg(1), item if case["shield"] else 0)
        return int(case["shield"])

    e.hook(SHIELD, shield, 2)
    e.hook(ITEM_RECORD, lambda _e: record, 1)
    e.hook(STR_BONUS, lambda em: by_item(em.arg(0), "str_bonus") & 0xFFFF, 1)
    e.hook(DEX_BONUS, lambda em: by_item(em.arg(0), "dex_bonus") & 0xFFFF, 1)
    e.hook(SKILL_NODE, lambda _e: 0x5000 if case["holy_node"] else 0, 1)
    e.hook(HAS_STATE, lambda em: int(case["throw_state"] if em.arg(1) == 0x4E else case["holy_state"] if em.arg(1) == 0x65 else 0), 2)
    e.hook(WIELDED, lambda _e: item if case["wielded"] else 0, 1)
    e.hook(THROWABLE, lambda em: int(by_item(em.arg(0), "throwable")), 1)
    e.hook(ITEM_TYPE_IS, lambda em: int(by_item(em.arg(0), "potion")) if em.arg(1) == 0x26 else 0, 2)
    e.hook(MASTERY, lambda _e: case["mastery"] & 0xFFFFFFFF, 4)
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
    e.w32(unit + 0x60, e.alloc(0x40))                              # an inventory: FUN_004ea4a0 / FUN_004ea790 test it
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
                   **{stat: rng.choice((0, 0, 3, 17, 250)) for stat in range(48, 56)},
                   57: rng.choice((0, 0, 40, 900)), 58: rng.choice((0, 0, 80, 1500)), 59: rng.choice((0, 25, 100)),
                   101: rng.choice((0, 0, -1, 50)), 326: rng.choice((0, 1, 2, 3)),
                   159: rng.randrange(-2, 60), 160: rng.randrange(-2, 120),
                   0: rng.randrange(10, 400), 2: rng.randrange(10, 300), 17: rng.choice((0, 0, 150, 37)), 18: rng.choice((0, 0, 99, 250))},
            weapon=rng.random() < 0.7, wield=rng.choice((-1, 1, 2)),
            shield=rng.random() < 0.8, sh_min=rng.randrange(0, 30), sh_max=rng.randrange(0, 60),
            holy_node=rng.random() < 0.6, holy_state=rng.random() < 0.6, smite_pct=rng.choice((0, 15, 90, 300, -200)),
            str_bonus=rng.choice((0, 0, 50, 100, -10)), dex_bonus=rng.choice((0, 0, 50, 75)),
            wielded=rng.random() < 0.8, throwable=rng.random() < 0.8, potion=False,
            dual=rng.random() < 0.7, main=rng.random() < 0.85, other=rng.random() < 0.7, usable=rng.random() < 0.9,
            **{f"{key}_{hand}": value() for hand in "ab" for key, value in (
                ("hand", lambda: rng.random() < 0.8), ("usable", lambda: rng.random() < 0.85),
                ("potion", lambda: descdam != 13 and rng.random() < 0.1), ("throwable", lambda: rng.random() < 0.85),
                ("w_low", lambda: rng.randrange(-5, 200)), ("w_high", lambda: rng.randrange(-5, 400)),
                ("str_bonus", lambda: rng.choice((0, 0, 50, 75))), ("dex_bonus", lambda: rng.choice((0, 0, 30, 100))))},
            throw_state=rng.random() < 0.5, mastery=rng.choice((0, 30, 120, 400)),
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
