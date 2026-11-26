# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's unit event 3 under its states in stock game.exe 1.14d: FUN_005a6920 whole, frame by frame.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     .venv/bin/python pet_poison_town.py [--mutation-check]
     .venv/bin/python pet_poison_town.py inc --out ../../tests/pet_poison_town_cases.inc

FUN_005a6920 runs for real on a unit of type 1 (a pet is one) with merc_regen.py's rig, which hooks its
inputs and side effects (the stat getters, the max life, the event list, the kill). Here the rig's state
lookups say more: FUN_0063a750 (a state of the States group at data tables +0x14c, States.txt `life`:
poison 2, preventheal 0x34, openwounds 0x3e) answers `group`, FUN_006253b0(unit, 0x4a) the unit's own
stat 0x4a (`base`), FUN_00639df0(unit, 0x34) answers `prevent`; FUN_00620bb0 (the unit's room) is null
for `room` 0 and FUN_0061ab00 (the room's town test) answers `town`. The claim, d2d's
rules::regen_event (monsters.cpp):
  - rate = the total stat 0x4a, less `base` under a life-group state;
  - under 0x34 a rate of 0 or more ends the event unfiled; a rate of 0 files and removes it;
  - a negative rate on a life of 0xff or less does nothing in a town room or with no room;
  - else life += rate, the max and the event removed past the max life, 0 under 1 (the kill).
`inc` writes the cases for tests/game/test_pet_poison_town.cpp.
"""
import argparse
import random
import sys
from pathlib import Path

import merc_regen

MUT = set()


def model(life, max_life, total, base, group, prevent, spared, frames):
    """d2d's rules::regen_event run while the event is filed and the unit alive: (life, filed, dead)."""
    filed, dead = True, False
    for _ in range(frames):
        if not filed or dead:
            break
        rate = total - (base if group and "group_keep" not in MUT else 0)
        if prevent and (rate >= 0 or "prevent_all" in MUT):
            filed = False
            break
        if rate == 0:
            filed = False
            break
        limit = 0xfe if "spare_lt" in MUT else 0xff
        if rate < 0 and life <= limit and spared and "no_spare" not in MUT:
            continue
        after = life + rate
        if after > max_life:
            life, filed = max_life, False
        else:
            life = max(after, 0)
            dead = life == 0
    return life, int(filed), int(dead)


class StateRig(merc_regen.Rig):
    """merc_regen's rig with the life group, the unit's own 0x4a, preventheal and the room answering per case."""

    def __init__(self, e):
        super().__init__(e)
        self.group = self.base = self.prevent = 0
        self.has_room = 1
        e.hook(0x63a750, lambda _e: self.group, 1)
        e.hook(0x6253b0, self.own, 3)
        e.hook(0x639df0, lambda _e: self.prevent if _e.arg(1) == 0x34 else 0, 2)
        e.hook(0x620bb0, lambda _e: self.room if self.has_room else 0, 1)

    def run(self, total, life, max_life, town, frames):
        """FUN_005a6920 once a frame while filed and alive: (life, filed, dead). Unlike merc_regen's, a frame that
        files nothing (preventheal on a rate of 0 or more, 0x5a695c) ends the event."""
        self.mode = "regen"
        self.stats = {6: life, 0x4a: total}
        self.max_life, self.town = max_life, town
        self.filed, self.dead = True, False
        self.e.w32(self.merc + 0x10, merc_regen.NU)
        for frame in range(frames):
            if not self.filed or self.dead:
                break
            self.e.w32(self.game + 0xa8, 1000 + frame)
            self.events.clear()
            self.e.call(0x5a6920, 0, 0, ecx=self.game, edx=self.merc)
            filed = [ev for ev in self.events if ev[0] == "file"]
            if filed not in ([], [("file", 3, 1001 + frame)]):
                sys.exit(f"event 3 filed oddly: {self.events}")
            if not filed or ("remove", 3) in self.events:
                self.filed = False
        return merc_regen.s32(self.stats[6]), int(self.filed), int(self.dead)

    def own(self, e):
        """FUN_006253b0: the unit's own stat, 0x4a the case's `base`; the level-up's stat 7 otherwise."""
        if self.mode == "regen" and e.arg(1) == 0x4a:
            return self.base & 0xffffffff
        return self.sets.get(7, 0)


def build(rng):
    """One case: life, max life, total, base, group, prevent, room, town, frames."""
    max_life = rng.choice((0x100, 0x400, rng.randint(0x100, 0x8000)))
    life = rng.choice((1, 0xfe, 0xff, 0x100, 0x101, rng.randint(1, 0x200), rng.randint(1, max_life)))
    life = min(life, max_life)
    base = rng.choice((0, rng.randint(1, 40), rng.randint(1, 600)))
    group = rng.choice((0, 0, 1))
    poison = rng.choice((0, 0, rng.randint(1, 300)))
    total = rng.choice((base - poison, base - poison, base, -rng.randint(1, 0x200), 0))
    prevent = rng.choice((0, 0, 0, 1))
    room = rng.choice((1, 1, 1, 0))
    town = rng.choice((0, 1))
    frames = rng.choice((1, 2, rng.randint(1, 12)))
    return life, max_life, total, base, group, prevent, room, town, frames


def run(rig, mutation, cases=3000, collect=None):
    """Each case through game.exe and the model; the failure count."""
    MUT.clear()
    MUT.update(mutation)
    rng = random.Random(20261126)
    bad = 0
    for index in range(cases):
        life, max_life, total, base, group, prevent, room, town, frames = build(rng)
        rig.group, rig.base, rig.prevent, rig.has_room = group, base, prevent, room
        got = rig.run(total, life, max_life, town, frames)
        want = model(life, max_life, total, base, group, prevent, (not room) or town, frames)
        if got != want:
            bad += 1
            if bad <= 5:
                print(f"case {index}: life {life} max {max_life} total {total} base {base} group {group} "
                      f"prevent {prevent} room {room} town {town} frames {frames}: game {got} model {want}")
        if collect is not None:
            collect.append((life, max_life, total, base, group, prevent, int((not room) or bool(town)), frames) + got)
    return bad


def main():
    """Run the oracle, with --mutation-check prove each mutant of the model is caught, or write the fixture."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=("inc",))
    parser.add_argument("--out")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig = StateRig(merc_regen.boot())
    collect = []
    bad = run(rig, (), collect=collect)
    if bad:
        print(f"FAIL: {bad} cases differ", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        lines = ["// Generated by tools/emu/pet_poison_town.py inc: FUN_005a6920's answers from game.exe 1.14d.",
                 "// { life, max life, total 0x4a, own 0x4a, life group, preventheal, no room or town, frames, life after, filed, dead }"]
        lines += ["{ " + ", ".join(str(v) for v in row) + " }," for row in collect[:600]]
        text = "\n".join(lines) + "\n"
        if args.out:
            Path(args.out).write_text(text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: {len(collect)} FUN_005a6920 cases (life group, preventheal, room and town)")
    if args.mutation_check:
        for name in ("no_spare", "spare_lt", "group_keep", "prevent_all"):
            caught = run(rig, (name,))
            print(f"mutant {name}: {'caught' if caught else 'NOT CAUGHT'} ({caught})")
            if not caught:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
