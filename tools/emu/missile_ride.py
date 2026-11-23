# SPDX-License-Identifier: GPL-3.0-or-later
"""A missile put on its owner's spot (FUN_005a99e0) on local game.exe 1.14d, against a model.

Run: uv run python missile_ride.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_005a99e0

FUN_005a99e0 takes the missile in EBX (ECX the game passes through to the
owner lookup) and returns nothing. It is missile srvdofunc 20's and 30's
(0x73c768 + 20 * 4, + 30 * 4: FUN_005af540, FUN_005b0010; Missiles.txt
blade creeper and rabiesplague). Runs for real: FUN_005a99e0, the path
readers FUN_006488c0 / FUN_00648900, the unit's room FUN_00620bb0 (and
FUN_00648a80), the move FUN_00650be0. Hooked: the owner lookup
FUN_00552fd0 (ECX game, EDX missile: the case's owner, logged) and the
path move FUN_00650910 (EAX path, room, x | y << 16: logged, the case's
answer). Each case builds a missile and an owner of some unit type with a
dynamic or static path and compares the calls and the missile path's point
count (+0x28) with the model. `--mutation-check` plants defects in the
model and requires each to be caught. Checks game.exe only: d2d's missile
do 20 is written up in missiles.cpp (missiles.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
RIDE, OWNER, PATH_MOVE = 0x5A99E0, 0x552FD0, 0x650910
GAME = 0x1234560
MUTATIONS = ("static_includes_3", "x_y_swapped", "room_of_the_missile", "moves_without_path",
             "points_kept", "points_always_cleared", "coords_not_truncated")


def static_type(kind):
    """The unit types with a static path (object 2, item 4, tile 5): FUN_00620bb0's and FUN_005a99e0's test."""
    signed = kind - (1 << 32) if kind >= 1 << 31 else kind
    return signed == 2 or 3 < signed <= 5


class Model:
    """What FUN_005a99e0 should do, read from its disassembly (missiles.md)."""

    def __init__(self, mutation=None):
        self.mutation = mutation

    def run(self, case):
        """The calls it makes and the missile path's point count after."""
        log = [("owner", GAME, case["missile"])]
        points = case["points"]
        if not case["owner"] or (not case["missile_path"] and self.mutation != "moves_without_path"):
            return log, points
        kind = case["owner_type"]
        static = static_type(kind) or (self.mutation == "static_includes_3" and kind == 3)
        if static:
            x, y, room = case["static"]
        elif case["owner_path"]:
            x, y, room = case["dynamic"]
        else:
            x, y, room = 0, 0, 0
        if self.mutation == "x_y_swapped":
            x, y = y, x
        if self.mutation == "room_of_the_missile":
            room = case["missile_room"]
        if self.mutation != "coords_not_truncated":
            x, y = x & 0xFFFF, y & 0xFFFF
        log.append(("move", case["missile_path"], room, (x | y << 16) & 0xFFFFFFFF))
        if (case["moved"] and self.mutation != "points_kept") or self.mutation == "points_always_cleared":
            points = 0
        return log, points


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.case = None
        self.log = []
        self.machine.hook(OWNER, self.owner, 0)
        self.machine.hook(PATH_MOVE, self.path_move, 2)

    def owner(self, machine):
        """FUN_00552fd0 (ECX game, EDX unit): the case's owner."""
        self.log.append(("owner", machine.mu.reg_read(emu.UC_X86_REG_ECX), machine.mu.reg_read(emu.UC_X86_REG_EDX)))
        return self.case["owner"]

    def path_move(self, machine):
        """FUN_00650910 (EAX path, room, x | y << 16): logged; 1 when the case's move succeeds."""
        self.log.append(("move", machine.mu.reg_read(emu.UC_X86_REG_EAX), machine.arg(0), machine.arg(1)))
        return int(self.case["moved"])

    def run(self, case):
        """Builds the case's units, calls FUN_005a99e0 with EBX = the missile."""
        machine = self.machine
        self.case, self.log = case, []
        machine.call(RIDE, ecx=GAME, regs={"ebx": case["missile"]})
        points = machine.view("path", case["missile_path"]).points if case["missile_path"] else case["points"]
        return self.log, points


def cases(rig, seed, count):
    """Random missiles and owners; the owner's type, paths and coordinates vary."""
    rng = random.Random(seed)
    machine = rig.machine
    for _ in range(count):
        missile = machine.alloc(0x100)
        missile_path = machine.alloc(0x200) if rng.random() < 0.9 else 0
        owner = machine.alloc(0x100) if rng.random() < 0.9 else 0
        kind = rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF))
        has_path = rng.random() < 0.9
        points = rng.randint(1, 77)
        case = {"missile": missile, "missile_path": missile_path, "owner": owner, "owner_type": kind,
                "owner_path": 0, "points": points, "moved": rng.random() < 0.7, "missile_room": 0,
                "static": (rng.randint(0, 0x1FFFF), rng.randint(0, 0x1FFFF), rng.randint(1, 0xFFFFFF) << 4),
                "dynamic": (rng.randint(0, 0xFFFF), rng.randint(0, 0xFFFF), rng.randint(1, 0xFFFFFF) << 4)}
        machine.view("unit", missile).path = missile_path
        if missile_path:
            path = machine.view("path", missile_path)
            path.points = points
            case["missile_room"] = rng.randint(1, 0xFFFFFF) << 4
            path.room = case["missile_room"]
        if owner:
            unit = machine.view("unit", owner)
            unit.type = kind
            if static_type(kind):
                block = machine.alloc(0x20)
                held = machine.view("static_path", block)
                held.x, held.y, held.room = case["static"]
                unit.path = block
            elif has_path:
                block = machine.alloc(0x200)
                walk = machine.view("path", block)
                walk.x, walk.y, walk.room = case["dynamic"]
                unit.path = case["owner_path"] = block
            else:
                unit.path = 0
        yield case


def compare(rig, model, seed, count):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    for number, case in enumerate(cases(rig, seed, count)):
        got, want = rig.run(case), model.run(case)
        if got != want:
            return f"case {number} (owner type {case['owner_type']:#x}): {got} != {want}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d (default: bin/game.exe in the data folder emu.py finds)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=3000)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    difference = compare(Rig(str(exe)), Model(), args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL FUN_005a99e0 {difference}")
    print(f"ok: {args.count} FUN_005a99e0 cases")
    if args.mutation_check:
        for mutation in MUTATIONS:
            caught = compare(Rig(str(exe)), Model(mutation), args.seed, args.count)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:160]}")


if __name__ == "__main__":
    main()
