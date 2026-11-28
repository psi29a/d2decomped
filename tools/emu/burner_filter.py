# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a standing burner's unit callback (LAB_005a8850 / FUN_005a8730).

Run from tools/emu with the repository virtual environment:

    .venv/bin/python burner_filter.py --mutation-check

The two stock callback bodies run for real over a synthetic unit, missile row
and callback context.  State 0x56, last-collision and owner-hostility lookups
are hooked as the callback's subsystem boundaries.  The model covers its unit
type and flag gates and every result of those three callees.
"""
import random
import sys

import _monster_world as mw

BURNER_FILTER = 0x5A_8850
STATE_TEST = 0x63_9DF0
LAST_COLLISION = 0x64_A470
HOSTILE = 0x55_4200
MUTATIONS = ("accept-friend", "accept-other-type", "ignore-flags", "ignore-state", "ignore-last")
mutation = None


def model(case):
    """Return whether LAB_005a8850 accepts this synthetic unit."""
    unit_type, flags, next_hit, has_state, was_last, has_owner, hostile, collide_friend = case
    type_ok = unit_type in (0, 1) or mutation == "accept-other-type"
    flags_ok = flags & 0xC == 0xC or mutation == "ignore-flags"
    state_ok = not (next_hit and has_state) or mutation == "ignore-state"
    last_ok = not was_last or mutation == "ignore-last"
    side_ok = not has_owner or hostile or collide_friend or mutation == "accept-friend"
    return type_ok and flags_ok and state_ok and last_ok and side_ok


class Oracle:
    """One stock-code runner with the callback's three query callees hooked."""

    def __init__(self):
        self.world = mw.World()
        self.machine = self.world.m
        self.unit = self.machine.alloc(0x100)
        self.owner = self.machine.alloc(0x100)
        self.missile = self.machine.alloc(0x100)
        self.row = self.machine.alloc(0x200)
        self.context = self.machine.alloc(0x20)
        self.has_state = False
        self.was_last = False
        self.hostile = False
        self.machine.hook(STATE_TEST, lambda _current: int(self.has_state), 2)
        self.machine.hook(LAST_COLLISION, lambda _current: int(self.was_last), 2)
        self.machine.hook(HOSTILE, lambda _current: int(self.hostile), 1)

    def run(self, case):
        """Run one case and return the stock callback's boolean result."""
        unit_type, flags, next_hit, self.has_state, self.was_last, has_owner, self.hostile, collide_friend = case
        self.machine.mu.mem_write(self.unit, bytes(0x100))
        self.machine.mu.mem_write(self.row, bytes(0x200))
        self.machine.mu.mem_write(self.context, bytes(0x20))
        self.machine.w32(self.unit, unit_type)
        self.machine.w32(self.unit + 0xC4, flags)
        self.machine.mu.mem_write(self.row + 0x187, bytes([int(collide_friend)]))
        self.machine.mu.mem_write(self.row + 0x188, bytes([int(next_hit)]))
        self.machine.w32(self.context + 4, self.missile)
        self.machine.w32(self.context + 8, self.owner if has_owner else 0)
        self.machine.w32(self.context + 0xC, self.row)
        return bool(self.world.run(BURNER_FILTER, ecx=self.unit, edx=self.context, nstack=0))


def cases():
    """Return direct mutation witnesses followed by deterministic random cases."""
    result = [
        (1, 0xC, False, False, False, True, False, False),
        (2, 0xC, False, False, False, True, True, False),
        (1, 0x8, False, False, False, True, True, False),
        (1, 0xC, True, True, False, True, True, False),
        (1, 0xC, False, False, True, True, True, False),
    ]
    rng = random.Random(20261128)
    for _case in range(1000):
        result.append((rng.randint(-1, 3), rng.randint(0, 0xF), bool(rng.getrandbits(1)),
                       bool(rng.getrandbits(1)), bool(rng.getrandbits(1)), bool(rng.getrandbits(1)),
                       bool(rng.getrandbits(1)), bool(rng.getrandbits(1))))
    return result


def run_all():
    """Compare every stock result with the model and return the case count."""
    oracle = Oracle()
    suite = cases()
    for number, case in enumerate(suite):
        got, want = oracle.run(case), model(case)
        mw.require(got == want, f"case {number}: game.exe accepted {got}, model accepted {want}")
    return len(suite)


def main():
    """Run the comparison or require each deliberate model defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "LAB_005a8850 burner-filter cases")


if __name__ == "__main__":
    main()
