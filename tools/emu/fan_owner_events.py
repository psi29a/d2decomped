# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the fan side-missile event suppression in stock game.exe 1.14d.

Run from tools/emu after setting D2_MPQ_DIR:

    .venv/bin/python fan_owner_events.py --mutation-check

The stock chain run here is:

* FUN_005ad730 reads missile unit flag 2 (FUN_0064a190, scripted as the
  value FUN_0059fa30 stores for a FUN_005db410 side request) and sets damage
  record flag 0x80 before calling FUN_0057c6c0;
* FUN_0057c6c0 runs for real with record flag 0x20 (a missile carries its
  owner's event) and, optionally, 0x80.  It calls the target's event 2 in
  both cases, but calls the shooter's event 6 only without 0x80.

The event receiver is hooked and logged; damage arithmetic and unrelated
side effects are hooked.  The d2d model is the predicate used by
Fight::strike: center missiles retain the owner's Crushing Blow, Open Wounds,
item-slow and item-knockback values, while side missiles clear those four.
`--mutation-check` breaks the predicate three ways.
"""
import argparse
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

MUTATION = ""
SIDE_FLAG = 2
RECORD_OWNER_EVENT = 0x20
RECORD_SKIP_OWNER_EVENT = 0x80


def model(side):
    """Return (owner event 6, target event 2) for d2d's fan-hit rule."""
    owner = not side
    target = True
    if MUTATION == "side_runs" and side:
        owner = True
    elif MUTATION == "middle_skips" and not side:
        owner = False
    elif MUTATION == "side_drops_target" and side:
        target = False
    return owner, target


class Rig:
    """Run the two stock functions with only their event path live."""

    def __init__(self, capture_damage):
        e = self.e = emu.Emu()
        self.game = e.alloc(0x200)
        self.attacker = e.alloc(0x200)
        self.target = e.alloc(0x200)
        self.missile = e.alloc(0x200)
        self.record = e.alloc(0x80)
        self.diffrow = e.alloc(0x60)
        self.tables = e.alloc(0xC00)
        self.missile_rows = e.alloc(0x1A4)
        self.stats = {}
        self.missile_flags = 0
        self.events = []
        self.applied = []

        e.view("game", self.game).difficulty = 0
        e.view("game", self.game).expansion = 1
        e.view("unit", self.attacker).type = 0
        e.view("unit", self.target).type = 1
        e.view("unit", self.target)["class"] = 0
        e.view("unit", self.missile).type = 3
        e.view("unit", self.missile)["class"] = 0
        e.w32(0x744304, self.tables)
        e.w32(self.tables + 0xB64, self.missile_rows)
        e.w32(self.tables + 0xB6C, 1)

        def stat_get(harness):
            return self.stats.get((harness.arg(0), harness.arg(1)), 0)

        def stat_set(harness):
            value = harness.arg(2)
            self.stats[(harness.arg(0), harness.arg(1))] = value - (1 << 32 if value >> 31 else 0)
            return 0

        def event(harness):
            event_id = harness.mu.reg_read(UC_X86_REG_EDX)
            receiver = harness.mu.reg_read(UC_X86_REG_ECX)
            self.events.append((event_id, receiver))
            return 0

        def applied(harness):
            self.applied.append(harness.r32(harness.arg(2)))
            return 0

        e.hook(0x625480, stat_get, 3)
        e.hook(0x627260, stat_set, 4)
        e.hook(0x625D10, lambda _harness: 100 << 8, 1)
        e.hook(0x611D30, lambda _harness: self.diffrow, 1)
        e.hook(0x5C0C30, event, 3)
        if capture_damage:
            e.hook(0x57C6C0, applied, 3)
        e.hook(0x64A190, lambda _harness: self.missile_flags, 1)
        e.hook(0x552FD0, lambda _harness: self.attacker, 0)
        e.stub(0, {0x57C1E0: 2, 0x57DFB0: 3, 0x64A5A0: 1, 0x5A5560: 2, 0x57CEE0: 2,
                   0x63EE90: 1, 0x63E990: 1, 0x63E940: 1, 0x63EDC0: 1,
                   0x63E9F0: 2, 0x5347D0: 7, 0x5347E0: 3, 0x61AB00: 1,
                   0x5541B0: 0, 0x621D50: 2, 0x639DF0: 2, 0x57AA60: 0,
                   0x57AAA0: 0, 0x57AAE0: 2, 0x57AF80: 2, 0x57B230: 2,
                   0x57AC50: 3, 0x57ADD0: 3, 0x5D6410: 0, 0x5A4390: 0})
        e.stub(1, {0x554200: 1, 0x620BB0: 1, 0x457490: 0})

    def propagated_record_flag(self, side):
        """Run FUN_005ad730 through its call of FUN_0057c6c0."""
        e = self.e
        e.mu.mem_write(self.record, bytes(0x80))
        self.missile_flags = SIDE_FLAG if side else 0
        self.applied = []
        e.call(0x5AD730, self.target, self.record, ecx=self.game, edx=self.missile)
        if len(self.applied) != 1:
            raise AssertionError(f"FUN_005ad730 made {len(self.applied)} damage calls")
        return self.applied[0]

    def event_path(self, side):
        """Run FUN_0057c6c0 with the propagated flags and return events 6 / 2."""
        e = self.e
        e.mu.mem_write(self.record, bytes(0x80))
        e.w32(self.record, RECORD_OWNER_EVENT | (RECORD_SKIP_OWNER_EVENT if side else 0))
        e.w32(self.record + 4, 1)
        self.stats[(self.target, 6)] = 100 << 8
        self.stats[(self.target, 7)] = 100 << 8
        self.events = []
        e.call(0x57C6C0, self.target, 1, self.record, ecx=self.game, edx=self.attacker)
        return (any(event_id == 6 for event_id, _receiver in self.events), any(event_id == 2 for event_id, _receiver in self.events)), tuple(self.events), e.r32(self.record), e.r32(self.record + 4)


def run(mutation=""):
    """Return the number of differences over middle and side paths."""
    global MUTATION
    MUTATION = mutation
    propagation = Rig(True)
    events = Rig(False)
    bad = 0
    for side in (False, True):
        propagated = propagation.propagated_record_flag(side)
        want_flag = RECORD_SKIP_OWNER_EVENT if side else 0
        if propagated & RECORD_SKIP_OWNER_EVENT != want_flag:
            print(f"propagation side={int(side)}: record {propagated:#x}, wanted bit {want_flag:#x}")
            bad += 1
        got, calls, record_flags, result_flags = events.event_path(side)
        want = model(side)
        if got != want:
            print(f"events side={int(side)}: game {got}, d2d model {want}; calls {calls}, record {record_flags:#x}/{result_flags:#x}")
            bad += 1
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    bad = run()
    if bad:
        sys.exit(f"FAIL: {bad} fan owner-event checks differ")
    print("ok: FUN_005ad730 propagates the fan side flag and FUN_0057c6c0 skips only owner event 6")
    if args.mutation_check:
        for mutation in ("side_runs", "middle_skips", "side_drops_target"):
            caught = run(mutation)
            print(f"mutant {mutation}: {'caught' if caught else 'NOT CAUGHT'} ({caught})")
            if not caught:
                return 1
        print("mutation check ok (3 ways)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
