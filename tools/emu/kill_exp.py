# SPDX-License-Identifier: GPL-3.0-or-later
"""A kill's experience by the level difference (FUN_0057e2f0) as the oracle.

    uv run python kill_exp.py [cases] [seed]

ECX the experience, EDX the character's level, EAX the monster's; one line
per case, `exp clvl mlvl award`, the form tests/test_combat.cpp checks
rules::kill_exp with.
"""
import random
import sys

import emu


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    e = emu.Emu()
    for _ in range(cases):
        exp, clvl, mlvl = rng.choice((rng.randint(1, 5000), rng.randint(1, 0x300000))), rng.randint(1, 99), rng.randint(1, 99)
        if rng.random() < 0.5: mlvl = max(1, min(99, clvl + rng.randint(-12, 12)))
        e.call(0x57e2f0, ecx=exp, edx=clvl, regs={"eax": mlvl})
        print(exp, clvl, mlvl, e.mu.reg_read(emu.UC_X86_REG_EAX))
    return 0


if __name__ == "__main__":
    sys.exit(main())
