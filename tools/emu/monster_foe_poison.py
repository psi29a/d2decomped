# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a monster target's poison-state replacement in stock game.exe 1.14d.

Run from tools/emu after setting D2_MPQ_DIR:

    .venv/bin/python monster_foe_poison.py --mutation-check

FUN_0057ac50 runs for real with its state-list services hooked.  Each case
records the hpregen value and expiry the stock function creates or updates on
a type-1 target.  The d2d model is Fight::take_blows' conversion of the
record's per-frame poison (256ths of life) and its stronger-poison replacement
rule.  ``--mutation-check`` breaks the target-kind gate, strength comparison,
and per-frame conversion one at a time.
"""
import argparse
import sys

import emu

MUTATION = ""


def model(current, per_frame, length, now):
    """Return (negative hpregen, end frame, d2d life/ms)."""
    old_per, old_end = current
    if MUTATION == "players_only":
        return -old_per, old_end, old_per / (256.0 * 40.0)
    replace = old_end <= now or per_frame >= old_per
    if MUTATION == "weaker_wins":
        replace = old_end <= now or per_frame <= old_per
    chosen = per_frame if replace else old_per
    end = now + length if replace else old_end
    divisor = 40.0 if MUTATION == "whole_points" else 256.0 * 40.0
    return -chosen, end, chosen / divisor


class Rig:
    """Run FUN_0057ac50 with synthetic stat-list storage."""

    def __init__(self):
        self.e = emu.Emu()
        self.game = self.e.alloc(0x200)
        self.target = self.e.alloc(0x200)
        self.attacker = self.e.alloc(0x200)
        self.stat_list = self.e.alloc(0x100)
        self.current = (0, 0)
        self.result = (0, 0)
        self.e.view("unit", self.target).type = 1
        self.e.view("unit", self.attacker).type = 1
        self.e.w32(self.attacker + 0xC, 77)
        self.e.w32(self.game + 0x1C, self.e.alloc(0x40))
        self.e.hook(0x554010, lambda _harness: self.game, 0)
        self.e.hook(0x6256B0, lambda _harness: self.stat_list if self.current[1] > self.e.r32(self.game + 0xA8) else 0, 2)
        self.e.hook(0x625D00, lambda _harness: -self.current[0], 3)
        self.e.hook(0x6251F0, lambda harness: self._created(harness), 5)
        self.e.hook(0x6270B0, lambda harness: self._stat(harness), 4)
        self.e.hook(0x627150, lambda harness: self._stat(harness), 4)
        self.e.hook(0x6260B0, lambda harness: self._expiry(harness), 2)
        self.e.stub(0, {0x639DB0: 3, 0x5417D0: 4, 0x6252D0: 2,
                        0x625CE0: 2, 0x626E10: 3, 0x540E60: 2})

    def _created(self, harness):
        self.result = (self.result[0], harness.arg(2))
        return self.stat_list

    def _stat(self, harness):
        value = harness.arg(2)
        if value >> 31:
            value -= 1 << 32
        self.result = (value, self.result[1])
        return 0

    def _expiry(self, harness):
        self.result = (self.result[0], harness.arg(1))
        return 0

    def poison(self, current, per_frame, length, now):
        self.current = current
        self.result = (-current[0], current[1])
        self.e.w32(self.game + 0xA8, now)
        self.e.call(0x57AC50, self.attacker, per_frame, length,
                    regs={"esi": self.target})
        return self.result


CASES = (
    ((0, 0), 256, 25, 100),
    ((512, 500), 256, 40, 200),
    ((512, 500), 512, 40, 200),
    ((512, 500), 768, 10, 200),
    ((512, 150), 128, 30, 200),
)


def run(mutation=""):
    global MUTATION
    MUTATION = mutation
    rig = Rig()
    bad = 0
    for current, per_frame, length, now in CASES:
        got = rig.poison(current, per_frame, length, now)
        want_stat, want_end, rate = model(current, per_frame, length, now)
        game_rate = -got[0] / (256.0 * 40.0)
        if got != (want_stat, want_end) or abs(game_rate - rate) > 1e-12:
            print(f"case {current}/{per_frame}/{length}/{now}: game {got}, d2d {(want_stat, want_end)}, rate {rate:.8f}")
            bad += 1
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    bad = run()
    if bad:
        sys.exit(f"FAIL: {bad} monster-poison cases differ")
    print("ok: 5 FUN_0057ac50 monster-target poison cases")
    if args.mutation_check:
        for mutation in ("players_only", "weaker_wins", "whole_points"):
            caught = run(mutation)
            print(f"mutant {mutation}: {'caught' if caught else 'NOT CAUGHT'} ({caught})")
            if not caught:
                return 1
        print("mutation check ok (3 ways)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
