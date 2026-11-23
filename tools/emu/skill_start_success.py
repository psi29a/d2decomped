# SPDX-License-Identifier: GPL-3.0-or-later
"""Hydra and Charge client-start status against the d2d decision model.

Run from tools/emu with the repository venv:

    python skill_start_success.py --exe /path/to/game.exe --mutation-check

Runs stock FUN_004e39a0 and FUN_004c9b40 on synthetic unit/skill records.
Their subsystem boundaries are hooked and driven by each case. The compared
model is components/rules/combat/skills.hpp client_start_succeeds: Hydra needs the
caster and selected point in rooms and refuses a non-InTown skill in town;
Charge needs a selected skill record and dynamic path and rejects the local
player's already-selected flagged record. The accepted Charge case returns
through its real in-reach branch; the remaining path/stat-list side effects
are outside this status claim.

FUN_004c6140 consumes these statuses: when stsuccessonly is set, zero returns
at 0x4c649e before its start-sound and cast-overlay blocks. The existing
skill_start_sound.py runs that caller and checks the same sound gate.

``--mutation-check`` plants four defects in the model and one entry ``ret``
in each stock function, requiring every negative control to be detected.
"""
import argparse
import dataclasses
import sys

import emu

HYDRA_START = 0x4E39A0
CHARGE_START = 0x4C9B40
TABLES = 0x744304
SKILL_ROW = 0x23C
CALL_LIMIT = 100000


class Mismatch(Exception):
    """The stock function and the d2d model disagree."""


@dataclasses.dataclass(frozen=True)
class StartCase:
    """Inputs kept by d2d's ClientStartContext."""

    caster_room: bool = True
    target_room: bool = True
    target_town: bool = False
    skill_in_town: bool = False
    skill_record: bool = True
    dynamic_path: bool = True
    same_local_flagged: bool = False


HYDRA_CASES = (
    StartCase(),
    StartCase(caster_room=False),
    StartCase(target_room=False),
    StartCase(target_town=True),
    StartCase(target_town=True, skill_in_town=True),
    StartCase(skill_record=False),
)

CHARGE_CASES = (
    StartCase(),
    StartCase(skill_record=False),
    StartCase(dynamic_path=False),
    StartCase(same_local_flagged=True),
)


def model(function, case, mutation=None):
    """The C++ client_start_succeeds rule, optionally deliberately broken."""
    if function == 17:
        caster = case.caster_room or mutation == "hydra_caster"
        target = case.target_room or mutation == "hydra_target"
        town_ok = case.skill_in_town or not case.target_town or mutation == "hydra_town"
        skill = case.skill_record or mutation == "hydra_skill"
        return caster and target and town_ok and skill
    if function == 25:
        return case.skill_record and case.dynamic_path and not case.same_local_flagged
    return True


def install_tables(machine):
    """Install one valid Skills.txt row and return its address."""
    tables = machine.alloc(0xC00)
    skills = machine.alloc(SKILL_ROW)
    machine.w32(TABLES, tables)
    machine.w32(tables + 0xB98, skills)
    machine.w32(tables + 0xBA0, 1)
    return skills


def hydra_results(executable, patch_entry=False):
    """Run FUN_004e39a0 for HYDRA_CASES."""
    machine = emu.Emu(exe=executable)
    skill = install_tables(machine)
    unit = machine.alloc(0x100)
    state = {"case": HYDRA_CASES[0]}
    machine.hook(0x620BB0, lambda current: 0x11110000 if state["case"].caster_room else 0, 1)
    machine.hook(0x4C52E0, lambda current: 1, 1)
    machine.hook(0x463740, lambda current: 0x22220000 if state["case"].target_room else 0, 1)
    machine.hook(0x61AB00, lambda current: int(state["case"].target_town), 1)
    if patch_entry:
        machine.mu.mem_write(HYDRA_START, b"\xc3")
        machine.mu.ctl_remove_cache(HYDRA_START, HYDRA_START + 1)
    results = []
    for case in HYDRA_CASES:
        state["case"] = case
        machine.mu.mem_write(skill + 5, bytes((1 if case.skill_in_town else 0,)))
        skill_id = 0 if case.skill_record else 1
        results.append(bool(machine.call(HYDRA_START, ecx=unit, edx=skill_id, limit=CALL_LIMIT)))
    return results


def charge_results(executable, patch_entry=False):
    """Run FUN_004c9b40 through its guards and real in-reach success return."""
    machine = emu.Emu(exe=executable)
    install_tables(machine)
    unit = machine.alloc(0x100)
    node = machine.alloc(0x40)
    target = machine.alloc(0x100)
    state = {"case": CHARGE_CASES[0]}
    machine.w32(unit, 0)                    # player: permits the in-reach early success
    machine.w32(target, 1)
    machine.hook(0x620250, lambda current: node if state["case"].skill_record else 0, 1)
    machine.hook(0x643CE0, lambda current: 0, 3)
    machine.hook(0x463DD0, lambda current: unit if state["case"].same_local_flagged else 0, 0)
    machine.hook(0x6446A0, lambda current: 1 if state["case"].same_local_flagged else 0, 1)
    machine.hook(0x4648F0, lambda current: target, 0)
    machine.hook(0x622C40, lambda current: 1, 3)
    machine.hook(0x4C7630, lambda current: 1, 0)
    if patch_entry:
        machine.mu.mem_write(CHARGE_START, b"\xc3")
        machine.mu.ctl_remove_cache(CHARGE_START, CHARGE_START + 1)
    results = []
    for case in CHARGE_CASES:
        state["case"] = case
        machine.w32(unit + 0x2C, 0x33330000 if case.dynamic_path else 0)
        results.append(bool(machine.call(CHARGE_START, ecx=unit, edx=0, limit=CALL_LIMIT)))
    return results


def check(executable, mutation=None, patch_function=None):
    """Compare both stock functions with the model and return the case count."""
    hydra = hydra_results(executable, patch_function == 17)
    charge = charge_results(executable, patch_function == 25)
    expected_hydra = [model(17, case, mutation) for case in HYDRA_CASES]
    expected_charge = [model(25, case, mutation) for case in CHARGE_CASES]
    if hydra != expected_hydra:
        raise Mismatch(f"FUN_004e39a0: stock {hydra}, d2d {expected_hydra}")
    if charge != expected_charge:
        raise Mismatch(f"FUN_004c9b40: stock {charge}, d2d {expected_charge}")
    return len(hydra) + len(charge)


def main():
    """Run parity, then optional deliberate negative controls."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="local game.exe 1.14d")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    cases = check(options.exe)
    print(f"ok: {cases} Hydra / Charge client-start status cases")
    if not options.mutation_check:
        return 0
    caught = 0
    for mutation in ("hydra_caster", "hydra_target", "hydra_town", "hydra_skill"):
        try:
            check(options.exe, mutation=mutation)
        except Mismatch:
            caught += 1
            print(f"mutation {mutation}: caught")
        else:
            raise SystemExit(f"mutation {mutation}: NOT caught")
    for function in (17, 25):
        try:
            check(options.exe, patch_function=function)
        except Mismatch:
            caught += 1
            print(f"mutation cltstfunc {function} entry ret: caught")
        else:
            raise SystemExit(f"mutation cltstfunc {function} entry ret: NOT caught")
    print(f"mutation check ok ({caught} ways)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
