# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005ddd00 (the run-state think behind Howl, `FUN_005aafb0` -> `FUN_005ddd00`) in stock game.exe 1.14d.

    uv run python howl_think.py --exe /path/to/game.exe [--mutation-check]

FUN_005ddd00 takes ECX = the game, EDX = a word and four stack words (the unit
and three more; `ret 0x10`). It reads the unit and the fourth word only: the
second and third stack words are never read, so the run length that
`FUN_005aafb0` works out and passes does nothing (docs/research/re/bugs.md 41).

Every callee is hooked and logged (name, registers, stack words); the gates are
scripted over every combination, and the log is compared with a model written
from the disassembly. The second and third stack words are random each run and
must not change the log.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import itertools
import random
import struct
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("reads-third-word", "swapped-gate-order", "missing-second-check", "wrong-state-id", "no-corpse-state",
             "type-any", "state-struct-field")
mutation = None
# name: (address, stack words popped by the callee)
CALLEES = {"flagged": (0x5541B0, 0), "has-state": (0x639DF0, 2), "class-ok": (0x623470, 0), "mode-check": (0x451F30, 0),
           "busy": (0x5A0180, 0), "base-mode": (0x451F80, 0), "set-mode": (0x5B0DA0, 2), "put-state": (0x56E970, 0),
           "restart": (0x620C10, 2), "arm": (0x5B0E00, 2), "stat": (0x540E60, 2), "send": (0x5417D0, 4)}


class Rig:
    """The hooked callees: each logs (name, ECX, EDX, stack words) and returns the scripted value."""

    def __init__(self, machine):
        """Hook every callee of FUN_005ddd00."""
        self.m = machine
        self.log = []
        self.script = {}
        for name, (address, words) in CALLEES.items():
            machine.hook(address, self._make(name, words), words)

    def _make(self, name, words):
        """A hook for one callee."""
        def hook(current):
            """Record the call and return the scripted value (a list is consumed in order)."""
            args = tuple(current.arg(index) for index in range(words))
            extra = ()
            if name == "put-state":
                extra = (current.read(current.mu.reg_read(uc.UC_X86_REG_ECX), 0x24),)
            self.log.append((name, current.mu.reg_read(uc.UC_X86_REG_ECX), current.mu.reg_read(uc.UC_X86_REG_EDX), args) + extra)
            value = self.script.get(name, 0)
            return value.pop(0) if isinstance(value, list) else value
        return hook


def model(game, word, unit, stack, unit_type, stat, gates):
    """The calls FUN_005ddd00 makes: (name, ECX, EDX, stack words[, state bytes]); None = not constrained."""
    log = [("flagged", unit, None, ())]
    wrong_type = unit_type != 1 and mutation != "type-any"
    if gates["flagged"][0] or unit == 0 or wrong_type:
        return log
    if mutation == "swapped-gate-order":
        log.append(("class-ok", 0x4444, None, ()))
    log.append(("has-state", None, None, (unit, 0x36 if mutation != "wrong-state-id" else 0x37)))
    if gates["has-state"]:
        return log
    if mutation != "swapped-gate-order":
        log.append(("class-ok", 0x4444, None, ()))
    if not gates["class-ok"]:
        return log
    log.append(("mode-check", unit, 4, ()))
    if not gates["mode-check"]:
        log.append(("flagged", unit, None, ()))
        if not gates["flagged"][1] and mutation != "missing-second-check":
            return log
    log.append(("busy", unit, 0xA, ()))
    if gates["busy"]:
        return log
    log.append(("base-mode", 0x4444, None, ()))
    log.append(("set-mode", unit, gates["base-mode"], (0xB, 1)))
    if not gates["set-mode"]:
        return log
    third = stack[1] if mutation == "reads-third-word" else stack[3]
    marker = 0x39 if mutation == "state-struct-field" else 0x38
    state = struct.pack("<9I", word, unit, 0x4D, 1, third, 0xFFFFFFFF, 0, marker, 0)
    log.append(("put-state", None, None, (), state))
    if mutation != "no-corpse-state":
        log.append(("restart", None, None, (unit, word)))
    log.append(("arm", game, unit, (stat, 0xB)))
    log.append(("stat", game, unit, (2, 0)))
    log.append(("send", game, unit, (2, game_field(game) + 1, 0, 0)))
    return log


def game_field(game):
    """The +0xa8 word of the game block, as the harness writes it."""
    return 0x1000 + (game & 0xFF)


def run_case(machine, rig, gates, unit_kind, rng):
    """One FUN_005ddd00 call; returns the inputs and the words popped."""
    game = machine.alloc(0x100)
    machine.w32(game + 0xA8, game_field(game))
    unit = 0 if unit_kind == "null" else machine.alloc(0x40)
    data = machine.alloc(0x40)
    unit_type = {"null": 0, "player": 1, "monster": 0, "item": 4}.get(unit_kind, 1)
    if unit:
        machine.w32(unit, unit_type)
        machine.w32(unit + 4, 0x4444)
        machine.w32(unit + 0x14, 0 if unit_kind == "no-data" else data)
        machine.w32(data + 0x28, rng.getrandbits(32))
    word = rng.getrandbits(32)
    stack = (unit, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32))
    rig.log.clear()
    rig.script = {"flagged": list(gates["flagged"]), "has-state": gates["has-state"], "class-ok": gates["class-ok"],
                  "mode-check": gates["mode-check"], "busy": gates["busy"], "base-mode": gates["base-mode"],
                  "set-mode": gates["set-mode"]}
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    machine.call(0x5DDD00, *stack, ecx=game, edx=word, limit=5000)
    popped = (machine.mu.reg_read(uc.UC_X86_REG_ESP) - (top - 4 * 5)) // 4
    stat = 0 if unit_kind == "no-data" or not unit else machine.r32(data + 0x28)
    return game, unit, word, stack, unit_type, stat, popped


def compare(machine, rig, gates, unit_kind, rng):
    """Run one scenario and compare the hook log with the model, field by field."""
    game, unit, word, stack, unit_type, stat, popped = run_case(machine, rig, gates, unit_kind, rng)
    want = model(game, word, unit, stack, unit_type, stat, gates)
    got = rig.log
    if [entry[0] for entry in got] != [entry[0] for entry in want]:
        raise AssertionError(f"gates {gates} {unit_kind}: calls {[e[0] for e in got]}, model {[e[0] for e in want]}")
    if popped != 5:
        raise AssertionError(f"FUN_005ddd00 popped {popped - 1} stack words, game.exe pops 4")
    for have, need in zip(got, want):
        for index, label in ((1, "ECX"), (2, "EDX"), (3, "stack")):
            if need[index] is not None and have[index] != need[index]:
                raise AssertionError(f"{have[0]} {label}: {have[index]}, model {need[index]}")
        if have[0] == "put-state" and have[4] != need[4]:
            raise AssertionError(f"state record {have[4].hex()}, model {need[4].hex()}")
    return len(got)


def run(exe):
    """Every gate combination for each unit kind, with random unread words; returns the summary line."""
    machine = emu.Emu(str(exe))
    rig = Rig(machine)
    rng = random.Random(0x5DDD00)
    cases = 0
    for unit_kind in ("player", "null", "monster", "no-data", "item"):
        for bits in itertools.product((0, 1), repeat=7):
            gates = {"flagged": [bits[0], bits[1]], "has-state": bits[2], "class-ok": bits[3], "mode-check": bits[4],
                     "busy": bits[5], "base-mode": 0x5A5A, "set-mode": bits[6]}
            for _ in range(3):
                compare(machine, rig, gates, unit_kind, rng)
                cases += 1
    return f"ok: {cases} gate combinations, the second and third stack words unread"


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
