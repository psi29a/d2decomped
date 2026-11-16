# SPDX-License-Identifier: GPL-3.0-or-later
# gaps-runs: 00625b70 006260b0 006261d0
"""Verify three unit stat-list accessors in stock game.exe 1.14d (Stats\\StatsEx.cpp).

    D2_MPQ_DIR=/path/to/game-data python stats_unit_list.py [--mutation-check]

Runs game.exe's own code against a hand-written model, with the harness of
_stats_world.py (a unit's stat list is the 0x70-byte record at unit +0x5c):

  FUN_00625b70  [unit, state]: the state's bit in the list's state bitmap
  FUN_006260b0  [list, value]: store the expiry frame, flag 2 when positive
  FUN_006261d0  [unit, stat, out, max]: the stat's layers, from the expanded array only

FUN_00626110 (the copy FUN_006261d0 tail-calls) and the state-bit mask table
0x6ce268 run for real. No MPQs are read; the allocator is not reached.
`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to be caught. See docs/research/re/stats.md.
"""
import argparse
import random
import struct
import sys

import _stats_world as sw
from _stats_world import EXPANDED, L, Mismatch, normal_entries, require

STATE_TEST, SET_EXPIRY, UNIT_LAYERS = 0x625B70, 0x6260B0, 0x6261D0
MASKS = 0x6CE268
BITMAP_POINTER, EXPIRY = 0x58, 0x18
FLAGS, HAS_EXPIRY = 0x10, 2
MUTATIONS = ("state-ignores-gate", "state-boolean", "state-unsigned-index", "expiry-always-flag",
             "expiry-nonnegative-flag", "expiry-clears-flag", "layers-ignores-gate", "layers-plain-array",
             "layers-ignores-max", "layers-no-count")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Rig:
    """The emulated game and the case counter."""

    def __init__(self):
        """Build the machine and require the state-bit table to be the 32 single bits."""
        self.w = sw.World()
        self.cases = 0
        table = struct.unpack("<32I", self.w.m.read(MASKS, 128))
        require(table == tuple(1 << i for i in range(32)), "mask table 0x6ce268 is not 1 << n")

    def unit(self, stat_list):
        """A unit record whose +0x5c points at `stat_list`."""
        unit = self.w.m.alloc(0x120)
        self.w.m.w32(unit + 0x5C, stat_list)
        return unit

    def list_with_bitmap(self, rng, flags):
        """An installed list whose +0x58 points into a 16-dword bitmap (index -8..7); returns (list, words)."""
        machine = self.w.m
        words = [rng.choice((0, 0xFFFFFFFF, rng.randrange(1 << 32), 1 << rng.randrange(32))) for _ in range(16)]
        base = machine.alloc(64)
        machine.mu.mem_write(base, struct.pack("<16I", *words))
        image = L()
        image.f[FLAGS] = flags
        image.f[BITMAP_POINTER] = base + 32
        return self.w.install(image), words


def state_model(flags_ok, words, state):
    """Expected FUN_00625b70 result for a list with the gate `flags_ok` and bitmap `words` (index 0 at 8)."""
    if not flags_ok and not mut("state-ignores-gate"):
        return 0
    index = ((state & 0xFFFFFFFF) >> 5) % 8 if mut("state-unsigned-index") else state >> 5
    word = words[index + 8] if 0 <= index + 8 < 16 else None
    if word is None:
        return None
    value = word & (1 << (state & 31))
    return int(bool(value)) if mut("state-boolean") else value


def check_state(rig, rng):
    """FUN_00625b70: null unit, null list, the gate flag, and every state bit through a signed shift."""
    w = rig.w
    for state in (0, 5, 31, 32):
        require(w.run(STATE_TEST, 0, state, nstack=2)[0] == 0, "null unit")
        rig.cases += 1
    no_list = rig.unit(0)
    for state in (0, 77):
        require(w.run(STATE_TEST, no_list, state, nstack=2)[0] == 0, "null list")
        rig.cases += 1
    for _ in range(40):
        # Only bit 31 of +0x10 gates; every other flag combination is noise.
        flags = rng.randrange(1 << 32)
        stat_list, words = rig.list_with_bitmap(rng, flags)
        unit = rig.unit(stat_list)
        for state in range(-256, 224):
            expected = state_model(bool(flags & EXPANDED), words, state)
            if expected is None:
                continue
            got, _ = w.run(STATE_TEST, unit, state, nstack=2)
            require(got == expected, f"state {state} flags {flags:#x}: {got:#x} != {expected:#x}")
            rig.cases += 1
        w.check_lists("state test")


def check_expiry(rig, rng):
    """FUN_006260b0: null list is a no-op; value above zero sets flag 2; the value always lands at +0x18."""
    w = rig.w
    require(w.run(SET_EXPIRY, 0, 5, nstack=2)[1] == 8, "null list pops 8")
    for _ in range(60):
        for value in (1, 5, 0x7FFFFFFF, 0, -1, -0x80000000, 0x100, 0xFFFFFFFF, rng.randrange(1 << 32)):
            # A fresh list per value, so a flag left by the last call cannot hide a missing one.
            w.models.clear()
            image = L()
            for offset in sw.SCALARS:
                image.f[offset] = rng.randrange(1 << 32)
            stat_list = w.install(image)
            value &= 0xFFFFFFFF
            signed = value - (1 << 32) if value & 0x80000000 else value
            model = w.models[stat_list]
            before = model.copy()
            if mut("expiry-always-flag") or (signed > 0) or (mut("expiry-nonnegative-flag") and signed >= 0):
                model.f[FLAGS] |= HAS_EXPIRY
            if mut("expiry-clears-flag") and signed <= 0:
                model.f[FLAGS] &= ~HAS_EXPIRY
            model.f[EXPIRY] = value
            w.run(SET_EXPIRY, stat_list, value, nstack=2)
            w.check_lists(f"expiry {signed} from {before.f[FLAGS]:#x}")
            rig.cases += 1


def check_layers(rig, rng):
    """FUN_006261d0: gate on the expanded flag, copy from the expanded array only, return the count."""
    w = rig.w
    out = w.m.alloc(0x100)
    w.m.mu.mem_write(out, b"\xaa" * 0x100)
    require(w.run(UNIT_LAYERS, rig.unit(0), 1, out, 4, nstack=4)[0] == 0, "null list")
    for _ in range(150):
        w.models.clear()
        image = L()
        flags = rng.randrange(1 << 32) & ~EXPANDED
        image.f[FLAGS] = flags | rng.choice((0, EXPANDED))
        for name in ("plain", "exp"):
            count = rng.randrange(0, 8)
            image.arr[name] = normal_entries(rng, count, stats=6, layers=4)
            image.cap[name] = count
        unit = rig.unit(w.install(image))
        gate = bool(image.f[FLAGS] & EXPANDED)
        entries = image.arr["plain" if mut("layers-plain-array") else "exp"]
        for stat in range(0, 8):
            for limit in (0, 1, 2, 3, 9, -1, -5):
                w.m.mu.mem_write(out, b"\xaa" * 0x100)
                wanted = [e for e in entries if e[0] >> 16 == stat]
                if limit < 0:
                    take = []
                else:
                    take = wanted[:limit + 1] if mut("layers-ignores-max") else wanted[:limit]
                if not gate and not mut("layers-ignores-gate"):
                    take = []
                if mut("layers-ignores-gate") and not gate:
                    take = [e for e in image.arr["exp"] if e[0] >> 16 == stat][:max(limit, 0)]
                got, _ = w.run(UNIT_LAYERS, unit, stat, out, limit, nstack=4)
                count = 0 if mut("layers-no-count") else len(take)
                require(got == count, f"layers stat {stat} max {limit}: returned {got}, expected {count}")
                expected = b"".join(struct.pack("<II", *e) for e in take)
                expected += b"\xaa" * (0x100 - len(expected))
                require(w.m.read(out, 0x100) == expected, f"layers buffer differs (stat {stat}, max {limit}, gate {gate})")
                w.check_lists("layers")
                rig.cases += 1


def run_all(seed, rounds):
    """Run every group; returns the case count."""
    rng = random.Random(seed)
    rig = Rig()
    for _ in range(rounds):
        check_state(rig, rng)
        check_expiry(rig, rng)
        check_layers(rig, rng)
    return rig.cases


def main():
    """Run the oracle, or with --mutation-check require every model defect to be caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="break the model on purpose and require failures")
    options = parser.parse_args()
    if options.mutation_check:
        mutation = None
        run_all(1, 1)   # the unmodified model must pass first: a baseline failure stops the command
        for name in MUTATIONS:
            mutation = name
            try:
                run_all(1, 1)
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"mutation {name} was not caught")
        print(f"ok: {len(MUTATIONS)} model mutations caught")
        return
    print(f"ok: {run_all(1, 2)} unit stat-list accessor cases")


if __name__ == "__main__":
    main()
