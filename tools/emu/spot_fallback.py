# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the start-spot chooser FUN_0066b2b0 and its last fallback (FUN_0066ae70) in stock game.exe 1.14d.

    uv run python spot_fallback.py --exe /path/to/game.exe [--mutation-check]

FUN_0066b2b0 (ECX = act, EDX = level, three stack words: the spot type, &x, &y;
`ret 0xc`) clears x and y to -1, finds the level, and picks a room: a level with
a spawn list (level record +0x90) asks FUN_0066ad80 (type 0xd) or FUN_0066ac40;
one without tries the waypoint room FUN_0066ad80, a warp slot's room
FUN_0066b1f0, the room at the level's middle less 2 (FUN_00642630) and, last,
FUN_0066ae70 (a random room: docs/research/re/stubs.md, "suspected
unreachable"). If x or y is still -1 the room's centre is used.

Every callee is hooked and scripted over every combination; the model is
written from the disassembly. A level with a spawn list reads its room without a null test (+0x30), so the
scenarios always give it one. It shows the fallback chain, the centre rule (a
signed halving toward zero, so a negative size rounds up) and that the level
record is only released (FUN_0061b730) when a room was found.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import itertools
import random
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("skip-warp", "middle-offset", "centre-floor-halving", "release-on-miss", "type-0xd-ignored", "centre-always",
             "last-fallback-first")
mutation = None
UNSET = 0xFFFFFFFF


def signed(value):
    """A 32-bit word as a signed int."""
    return value - (1 << 32) if value & 0x80000000 else value


def half(value):
    """`cdq; sub eax, edx; sar eax, 1`: halving that rounds toward zero (or the mutation's floor)."""
    value = signed(value & 0xFFFFFFFF)
    return (value >> 1) if mutation == "centre-floor-halving" else (value + (1 if value < 0 else 0)) >> 1


class Rig:
    """The hooked callees with a call log and scripted answers."""

    def __init__(self, machine):
        """Hook every callee of FUN_0066b2b0."""
        self.m = machine
        self.log = []
        self.script = {}
        hooks = {"level": (0x61E470, 0), "find": (0x642BB0, 2), "touch": (0x6424A0, 1), "waypoint": (0x66AD80, 2),
                 "pick": (0x66AC40, 3), "release": (0x61B730, 0), "warp": (0x66B1F0, 0), "middle": (0x642630, 1),
                 "random": (0x66AE70, 0)}
        for name, (address, words) in hooks.items():
            machine.hook(address, self._make(name, words), words)

    def _make(self, name, words):
        """A hook for one callee."""
        def hook(current):
            """Log the call; write the scripted spot through the x / y pointers; return the scripted room."""
            args = tuple(current.arg(index) for index in range(words))
            regs = tuple(current.mu.reg_read(getattr(uc, "UC_X86_REG_" + reg)) for reg in ("ECX", "EDX", "EAX", "EDI"))
            self.log.append((name, args, regs))
            spot = self.script.get(name + "-spot")
            if spot is not None:
                pointers = args if name == "waypoint" else args[1:3] if name == "pick" else None
                if pointers:
                    self.m.w32(pointers[0], spot[0])
                    self.m.w32(pointers[1], spot[1])
            return self.script.get(name, 0)
        return hook


def make_room(machine, rng):
    """A room record with random size and position fields (+0x30 id, +0x34 x, +0x38 y, +0x3c w, +0x40 h)."""
    room = machine.alloc(0x80)
    values = {0x30: rng.getrandbits(32), 0x34: rng.randrange(-500, 500) & 0xFFFFFFFF, 0x38: rng.randrange(-500, 500) & 0xFFFFFFFF,
              0x3C: rng.choice((rng.randrange(0, 200), (-rng.randrange(1, 200)) & 0xFFFFFFFF)),
              0x40: rng.choice((rng.randrange(0, 200), (-rng.randrange(1, 200)) & 0xFFFFFFFF))}
    for offset, value in values.items():
        machine.w32(room + offset, value)
    return room, values


def model(machine, case):
    """Expected (return, x, y, call names) for a scenario dict."""
    calls = ["level", "find"] + (["touch"] if not case["ready"] else [])
    x = y = UNSET
    if case["spawn"]:
        calls.append("waypoint" if case["type"] == 0xD and mutation != "type-0xd-ignored" else "pick")
        room = case["waypoint"] if calls[-1] == "waypoint" else case["pick"]
        if calls[-1] == "waypoint":
            x, y = case["waypoint-spot"] or (UNSET, UNSET)
        else:
            x, y = case["pick-spot"] or (UNSET, UNSET)
        calls.append("release")
        return machine.r32(room + 0x30), x, y, calls
    order = ["waypoint", "warp", "middle", "random"]
    if mutation == "last-fallback-first":
        order = ["random", "waypoint", "warp", "middle"]
    if mutation == "skip-warp":
        order.remove("warp")
    room = 0
    for name in order:
        calls.append(name)
        if name == "waypoint":
            x, y = case["waypoint-spot"] or (UNSET, UNSET)
        room = case[name]
        if room:
            break
    if not room:
        return 0, x, y, calls
    if (x == UNSET or y == UNSET) or mutation == "centre-always":
        x = (half(machine.r32(room + 0x3C)) + signed(machine.r32(room + 0x34))) & 0xFFFFFFFF
        y = (half(machine.r32(room + 0x40)) + signed(machine.r32(room + 0x38))) & 0xFFFFFFFF
    calls.append("release")
    return machine.r32(room + 0x30), x, y, calls


def run(exe):
    """Every gate combination with random rooms; returns the summary line."""
    machine = emu.Emu(str(exe))
    rig = Rig(machine)
    rng = random.Random(0x66B2B0)
    cases = 0
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    for spawn, kind, ready, wp, warp, middle, rand, spot_kind in itertools.product(
            (0, 1), (0xD, 3), (0, 1), (0, 1), (0, 1), (0, 1), (0, 1), range(4)):
        level = machine.alloc(0x100)
        machine.w32(level + 0x90, spawn)
        record = machine.alloc(0x100)
        machine.w32(record + 0x10, ready)
        machine.w32(record + 0x1C, rng.randrange(0, 300))
        machine.w32(record + 0x20, rng.randrange(0, 300))
        machine.w32(record + 0x24, rng.randrange(0, 300))
        machine.w32(record + 0x28, rng.randrange(0, 300))
        # With a spawn list game.exe goes on to read the room (+0x30) without a test, so it is always found here.
        rooms = {name: make_room(machine, rng)[0] if flag else 0
                 for name, flag in (("waypoint", wp or (spawn and kind == 0xD)), ("warp", warp), ("middle", middle),
                                    ("random", rand), ("pick", 1))}
        spot = (rng.randrange(0, 1000), rng.randrange(0, 1000)) if spot_kind & 1 else None
        if spot and spot_kind & 2:
            spot = (UNSET, spot[1])
        case = {"spawn": spawn, "type": kind, "ready": ready, "waypoint": rooms["waypoint"], "warp": rooms["warp"],
                "middle": rooms["middle"], "random": rooms["random"], "pick": rooms["pick"], "waypoint-spot": spot, "pick-spot": spot}
        rig.script = {"level": level, "find": record, "waypoint": rooms["waypoint"], "waypoint-spot": spot, "pick": rooms["pick"],
                      "pick-spot": spot, "warp": rooms["warp"], "middle": rooms["middle"], "random": rooms["random"]}
        rig.log.clear()
        x_cell, y_cell = machine.alloc(8), machine.alloc(8)
        machine.w32(x_cell, 0x1234)
        machine.w32(y_cell, 0x5678)
        act, level_id = rng.getrandbits(8), rng.getrandbits(8)
        got = machine.call(0x66B2B0, kind, x_cell, y_cell, ecx=act, edx=level_id, limit=5000)
        popped = (machine.mu.reg_read(uc.UC_X86_REG_ESP) - (top - 16)) // 4
        want, wx, wy, calls = model(machine, case)
        if mutation == "release-on-miss" and not want:
            calls.append("release")
        names = [entry[0] for entry in rig.log]
        if names != calls:
            raise AssertionError(f"{case}: calls {names}, model {calls}")
        if (got, machine.r32(x_cell), machine.r32(y_cell), popped) != (want, wx, wy, 4):
            raise AssertionError(f"{case}: ret {got:#x} x {machine.r32(x_cell):#x} y {machine.r32(y_cell):#x} popped {popped}; "
                                 f"model {want:#x} {wx:#x} {wy:#x}")
        find = next(entry for entry in rig.log if entry[0] == "find")
        if find[1] != (act, level_id):
            raise AssertionError(f"FUN_00642bb0 got {find[1]}, game passes (act, level)")
        if not spawn and any(entry[0] == "middle" for entry in rig.log):
            middle_call = next(entry for entry in rig.log if entry[0] == "middle")
            lo = machine.r32(record + 0x24) // 2 + machine.r32(record + 0x1C) - 2
            hi = machine.r32(record + 0x28) // 2 + machine.r32(record + 0x20) - 2 + (2 if mutation == "middle-offset" else 0)
            if (middle_call[2][0], middle_call[1][0]) != (record, hi) or middle_call[2][1] != lo & 0xFFFFFFFF:
                raise AssertionError(f"middle room asked for {middle_call}, model ({lo}, {hi})")
        cases += 1
    return f"ok: {cases} scripted start-spot scenarios"


def main():
    """Entry point: run the checks, or every mutation and require each to fail."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()
