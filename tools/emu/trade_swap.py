# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the trade item move FUN_00568770 (PLAYER\\PlrTrade.cpp) in stock game.exe 1.14d.

    uv run python trade_swap.py --exe /path/to/game.exe [--mutation-check]

FUN_00568770 (ECX = the game, EDX = a unit id, one stack word = the item record;
`ret 4`) is the swap the trade code runs for a unit: it looks the unit up
(FUN_00554100: it returns a flag and writes two answers through its stack
pointers; the first answer (the one at [ebp-0x10]) non-zero with the flag is an
error, line 0xc99; the second is the partner id), finds the partner (FUN_00552f60, null is line 0xca1),
resolves the item (FUN_0055a2a0), reads its spot (x / y from the record's path:
type 2 and 4..5 straight from the path +0xc / +0x10, other types through
FUN_006488c0 / FUN_00648900, 0 for no path), asks FUN_00560200 to move it (line
0xcad) and, on success, sends the results; on failure FUN_00557fd0 and
FUN_005679e0 report with 9. On success with either side's state byte (+0x50)
equal to 4 it also sends the four 0xcc7..0xcca messages and FUN_00567020.

Every callee is hooked and scripted; the model is written from the disassembly.
docs/research/re/stubs.md names this as the page-2 swap that FUN_00561b00
refuses earlier: this oracle checks what the function does when it is called,
not that it is never called.

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
MUTATIONS = ("error-needs-either", "partner-null-continues", "path-type-3", "move-args-swapped", "retry-skips-report",
             "state-4-either-only-first", "message-order", "no-trade-item-call")
mutation = None
TABLE = 0x6E11D8
# name: (address, stack words popped by the callee)
CALLEES = {"lookup": (0x554100, 3), "msg": (0x5531C0, 1), "reply": (0x53CAB0, 0), "partner": (0x552F60, 1), "resolve": (0x55A2A0, 2),
           "pathx": (0x6488C0, 1), "pathy": (0x648900, 1), "mark": (0x624690, 2), "flag": (0x628280, 2), "move": (0x560200, 8),
           "can": (0x55F590, 1), "attach": (0x563350, 3), "send": (0x567350, 3), "unit": (0x6221A0, 1), "tell": (0x53CBE0, 1),
           "extra": (0x567020, 3), "fail": (0x557FD0, 0), "report": (0x5679E0, 4)}


class Rig:
    """The hooked callees: each logs (name, ECX, EDX, EAX, stack words) and returns the scripted value."""

    def __init__(self, machine):
        """Hook every callee."""
        self.m = machine
        self.log = []
        self.script = {}
        for name, (address, words) in CALLEES.items():
            machine.hook(address, self._make(name, words), words)

    def _make(self, name, words):
        """A hook for one callee."""
        def hook(current):
            """Log the call; FUN_00554100 writes its two answers through the stack pointers."""
            args = tuple(current.arg(index) for index in range(words))
            regs = tuple(current.mu.reg_read(getattr(uc, "UC_X86_REG_" + reg)) for reg in ("ECX", "EDX", "EAX"))
            self.log.append((name, regs, args))
            if name == "lookup":
                self.m.w32(args[1], self.script["lookup_a"])
                self.m.w32(args[2], self.script["lookup_b"])
                return self.script["lookup"]
            if name == "unit":
                return self.script["units"][args[0]]
            return self.script.get(name, 0)
        return hook


def path_value(machine, rec, rec_type, offset, script, name):
    """What the model says the item's x (offset 0xc) / y (0x10) is for the record."""
    if rec_type == 2 or 3 < rec_type <= 5:
        return machine.r32(machine.r32(rec + 0x2C) + offset)
    pointer = machine.r32(rec + 0x2C)
    if rec_type == 3 and mutation == "path-type-3":
        return machine.r32(pointer + offset)
    return 0 if pointer == 0 else script[name]


def model(machine, case):
    """The call log (name, ECX, EDX, EAX, args; None = unconstrained) for a scenario dict."""
    game, unit, rec, new = case["game"], case["unit"], case["rec"], case["new"]
    log = [("lookup", None, unit, None, (unit, None, None))]
    first, second = case["lookup_a"], case["lookup_b"]
    if case["lookup"] and (first != 0 if mutation != "error-needs-either" else (first != 0 or second != 0)):
        log += [("msg", unit, TABLE, None, (0xC99,)), ("reply", None, 0xC, None, ())]
        return log
    log.append(("partner", game, 0, None, (second,)))
    if not case["partner"]:
        log += [("msg", unit, TABLE, None, (0xCA1,)), ("reply", None, 0xC, None, ())]
        if mutation != "partner-null-continues":
            return log
    partner = case["partner"]
    log.append(("resolve", game, rec, None, (0, 0)))
    rec_type = case["rec_type"]
    x = path_value(machine, rec, rec_type, 0xC, case, "pathx_v")
    y = path_value(machine, rec, rec_type, 0x10, case, "pathy_v")
    if not (rec_type == 2 or 3 < rec_type <= 5):
        pointer = machine.r32(rec + 0x2C)
        if pointer:
            log.append(("pathx", None, None, None, (pointer,)))
    if not (rec_type == 2 or 3 < rec_type <= 5):
        pointer = machine.r32(rec + 0x2C)
        if pointer:
            log.append(("pathy", None, None, None, (pointer,)))
    log += [("mark", None, None, None, (new, 4)), ("flag", None, None, None, (new, 1))]
    ident = 0xFFFFFFFF if new == 0 else machine.r32(new + 0xC)
    args = (game, partner, ident, x, y, 0, 1, 0)
    if mutation == "move-args-swapped":
        args = (game, partner, ident, y, x, 0, 1, 0)
    log.append(("move", TABLE, 0xCAD, None, args))
    if not case["move"]:
        log += [("fail", game, new, None, ()), ("unit", None, None, None, (unit,)), ("unit", None, None, None, (partner,)),
                ("report", game, unit, None, (partner, case["obj_unit"], case["obj_partner"], 9))]
        if mutation == "retry-skips-report":
            log = log[:-1]
        return log
    log.append(("can", rec, TABLE, None, (0xCB9,)))
    if case["can"] and mutation != "no-trade-item-call":
        log.append(("attach", game, partner, None, (rec, new, 1)))
    log.append(("send", None, None, unit, (game, machine.r32(rec + 0xC), ident)))
    log += [("unit", None, None, None, (unit,)), ("unit", None, None, None, (partner,))]
    states = (case["state_unit"], case["state_partner"])
    if states[0] == 4 or (states[1] == 4 and mutation != "state-4-either-only-first"):
        order = [(0xCC7, unit, 0), (0xCC8, unit, 1), (0xCC9, partner, 0), (0xCCA, partner, 1)]
        if mutation == "message-order":
            order[0], order[1] = order[1], order[0]
        for number, owner, flag in order:
            log += [("msg", owner, TABLE, None, (number,)), ("tell", None, 0, None, (flag,))]
        log.append(("extra", unit, None, None, (partner, case["obj_unit"], case["obj_partner"])))
    return log


def run(exe):
    """Every gate combination with random records; returns the summary line."""
    machine = emu.Emu(str(exe))
    rig = Rig(machine)
    rng = random.Random(0x568770)
    cases = 0
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    for lookup, la, lb, partner_ok, new_ok, move_ok, can, st_u, st_p, rec_type in itertools.product(
            (0, 1), (0, 7), (0, 5), (0, 1), (0, 1), (0, 1), (0, 1), (4, 1), (4, 1), (0, 2, 3, 4, 5, 6)):
        game, unit = machine.alloc(0x100), rng.getrandbits(32)
        rec = machine.alloc(0x80)
        machine.w32(rec, rec_type)
        path_record = machine.alloc(0x40)
        machine.w32(path_record + 0xC, rng.getrandbits(32))
        machine.w32(path_record + 0x10, rng.getrandbits(32))
        has_path = rng.random() > 0.2 or rec_type == 2 or 3 < rec_type <= 5     # those types read the path without a null test
        machine.w32(rec + 0x2C, path_record if has_path else 0)
        machine.w32(rec + 0xC, rng.getrandbits(32))
        new = machine.alloc(0x40) if new_ok else 0
        if new:
            machine.w32(new + 0xC, rng.getrandbits(32))
        obj_unit, obj_partner = machine.alloc(0x80), machine.alloc(0x80)
        machine.w32(obj_unit + 0x50, st_u)
        machine.w32(obj_partner + 0x50, st_p)
        partner = machine.alloc(0x40) if partner_ok else 0
        values = {"lookup": lookup, "lookup_a": la, "lookup_b": lb, "partner": partner, "resolve": new, "pathx": rng.getrandbits(32),
                  "pathy": rng.getrandbits(32), "move": move_ok, "can": can, "msg": 0x7777, "unit": 0}
        values["units"] = {unit: obj_unit, partner: obj_partner}
        rig.script, rig.log = values, []
        case = dict(values, game=game, unit=unit, rec=rec, new=new, rec_type=rec_type, pathx_v=values["pathx"], pathy_v=values["pathy"],
                    state_unit=st_u, state_partner=st_p, obj_unit=obj_unit, obj_partner=obj_partner)
        machine.call(0x568770, rec, ecx=game, edx=unit, limit=20000)
        popped = (machine.mu.reg_read(uc.UC_X86_REG_ESP) - (top - 8)) // 4
        want = model(machine, case)
        got = rig.log
        if [entry[0] for entry in got] != [entry[0] for entry in want] or popped != 2:
            raise AssertionError(f"{case}: calls {[e[0] for e in got]}, model {[e[0] for e in want]}, popped {popped}")
        for have, need in zip(got, want):
            regs, args = have[1], have[2]
            if have[0] == "reply":      # `mov dl, 0xc`: only the low byte of EDX is set
                regs = (regs[0], regs[1] & 0xFF, regs[2])
            for position, expect in enumerate(need[1:4]):
                if expect is not None and regs[position] != expect:
                    raise AssertionError(f"{have[0]} register {position}: {regs[position]:#x}, model {expect:#x}")
            expected_args = need[4]
            for position, expect in enumerate(expected_args):
                if expect is not None and args[position] != expect:
                    raise AssertionError(f"{have[0]} stack word {position}: {args[position]:#x}, model {expect:#x}\n case {case}")
        cases += 1
    return f"ok: {cases} trade swap scenarios"


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
