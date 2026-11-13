# SPDX-License-Identifier: GPL-3.0-or-later
"""A kill award's ExpRatio step (FUN_0057e390) as the oracle.

    uv run python xp_ratio.py [cases] [seed] > game.tsv

For a character level (Experience.txt row) and an award already through
FUN_0057e2f0 (kill_exp), FUN_0057e390 multiplies by the level's ExpRatio
and shifts by the MaxLvl row's ExpRatio (both read live from Experience.txt
through TXT_Experience_GetExpRatio, FUN_00613e60, called with the level and
with 0 for the shift), the same shape d2d's rules::kill_award inlines: one
order of operations when award * ratio would overflow 0x7fffffff, the other
otherwise, left alone outside shift 1..31 or award <= 0.

One line per case: `ratio shift award result`, the form
tests/test_combat.cpp's exp_ratio_apply cases check against.
"""
import random
import sys

import skill_tips


def main():
    """Print `ratio shift award result` for each random case, from game.exe's own ExpRatio table."""
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    e = skill_tips.boot()
    shift = e.call(0x613E60, 0)
    levels = [lvl for lvl in range(1, 100) if e.call(0x613E60, lvl) != 0] or [1]
    for _ in range(cases):
        level = rng.choice(levels)
        ratio = e.call(0x613E60, level)
        award = rng.choice((0, rng.randint(-5000, 0), rng.randint(1, 5000), rng.randint(1, 0x7FFFFFFF)))
        result = e.call(0x57E390, level, regs={"eax": award & 0xFFFFFFFF})
        print(ratio, shift, award, to_signed(result))


def to_signed(v):
    """Reinterpret v's low 32 bits as a signed int32."""
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v >= 0x80000000 else v


if __name__ == "__main__":
    sys.exit(main())
