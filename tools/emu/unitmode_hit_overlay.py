# SPDX-License-Identifier: GPL-3.0-or-later
"""The elemental-hit overlay pick, as the oracle: FUN_00480d20
(UNITMODE_ShowElementalHitOverlay).

    uv run python unitmode_hit_overlay.py

Not yet cited anywhere in docs/research/re; it is FUN_00480e70's own
overlay call ("the modes are set by FUN_00480e70(unit, mode)", collision.md).
The decompile: a no-op for a null unit (ECX) or any unit type but player (0)
or monster (1, offset 0x0, `unit->type`); for a player, modes 0/4 are a hit,
0x13 a maybe-hit; for a monster, modes 0/3 are a hit, 0xd a maybe-hit;
every other mode is a no-op. On a hit, the high nibble of `unit->hit_class`
(offset 0xb0) picks the overlay, but only when its low nibble is also set:
0x10 OVERLAY_doubledamage1 (54), 0x20 OVERLAY_fire_hit (81), 0x30
OVERLAY_ice_explode (36), 0x40 OVERLAY_lightning (1), 0x70 OVERLAY_bash
(147), 0x80 OVERLAY_hit_thorns (21), 0x90 OVERLAY_sanctuaryknockback (113)
-- any other high nibble (0x00, 0x50, 0x60, 0xa0+) or a zero low nibble is
a no-op. This oracle hooks
FUN_00470390 (GFX_AddOverlayToUnit) to record the overlay id it got, over
every (unit type, mode, hit_class) combination that exercises each branch,
and diffs against that map: no disagreement.
"""
import emu

ADD_OVERLAY = 0x470390
SHOW_HIT_OVERLAY = 0x480D20

# eOverlayId values: doubledamage1=54, fire_hit=81, ice_explode=36, lightning=1, bash=147,
# hit_thorns=21, sanctuaryknockback=113.
OVERLAY_BY_MASK = {0x10: 54, 0x20: 81, 0x30: 36, 0x40: 1, 0x70: 147, 0x80: 21, 0x90: 113}
UNIT_TYPE_PLAYER, UNIT_TYPE_MONSTER = 0, 1
HIT_MODES = {UNIT_TYPE_PLAYER: (0, 4), UNIT_TYPE_MONSTER: (0, 3)}
MAYBE_HIT_MODE = {UNIT_TYPE_PLAYER: 0x13, UNIT_TYPE_MONSTER: 0xD}


def run_case(e, unit, unit_type, mode, hit_class):
    seen = []
    e.hook(ADD_OVERLAY, lambda ev: (seen.append((ev.mu.reg_read(emu.UC_X86_REG_ECX),
                                                   ev.mu.reg_read(emu.UC_X86_REG_EDX))), 0)[1], 6)
    if unit:
        e.w32(unit + 0x00, unit_type)
        e.w32(unit + 0xB0, hit_class)
    e.call(SHOW_HIT_OVERLAY, ecx=unit, edx=mode)

    is_hit = unit and unit_type in HIT_MODES and (mode in HIT_MODES[unit_type] or mode == MAYBE_HIT_MODE[unit_type])
    mask = hit_class & 0xF0
    expect_overlay = OVERLAY_BY_MASK.get(mask) if (is_hit and mask and (hit_class & 0xF)) else None
    expect = [(unit, expect_overlay)] if expect_overlay is not None else []
    ok = seen == expect
    print(bool(unit), unit_type, mode, hex(hit_class), "OK" if ok else f"DISAGREE got={seen} expect={expect}")


def main():
    e = emu.Emu()
    unit = e.alloc(0x200)
    masks = list(OVERLAY_BY_MASK) + [0x00, 0x50, 0x60, 0xA0]
    for unit_arg in (0, unit):
        for unit_type in (UNIT_TYPE_PLAYER, UNIT_TYPE_MONSTER, 2):
            for mode in (0, 1, 3, 4, 0xD, 0x13):
                for mask in masks:
                    for low in (0x0, 0x5):
                        run_case(e, unit_arg, unit_type, mode, mask | low)


if __name__ == "__main__":
    main()
