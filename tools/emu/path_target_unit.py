# SPDX-License-Identifier: GPL-3.0-or-later
"""Setting a path's target unit and asking for a route, as the oracle:
FUN_00480780 (PATH_SetTargetUnitAndCalculate).

    uv run python path_target_unit.py 1 1 > g.txt
    sort -u g.txt   # found/not-found, each forwarded the same way

docs/research/re/collision.md cites FUN_00480780 for "a new path from where
the unit is" (FUN_00480c10 -> FUN_00461250 -> FUN_00480780, "Out of reach"),
command 0x18 (3, RN) and command 0 in "FUN_00461250"'s table, and "What
keeps a walk to a unit going". The decompile is three real calls in a row:
UNITS_FindClientSideUnit(nTargetUnitId, eUnitType) (0x463990, hooked:
controls found/not-found; a real client-side unit hash lookup, not the
claim), then always SetTarget(pDynamicPath, pTargetUnit) (0x648b90,
D2Common::PATH::DynamicPath, structs: D2DynamicPathStrc — hooked to
record, since it only writes a field), and, only if a target was found,
PATH_CalculatePath(pDynamicPath, pUnit, 0) (0x649970, hooked to a fixed
return: this is the pathfinder itself, a 800-byte function the oracle for
"The grid" and "A walk's stop" above already exercises for real, not this
one's job). No target: SetTarget(path, 0) and a 0 return, no path call.
A target: SetTarget(path, target), then the path call, whose return value
comes back unchanged.
"""
import sys

import emu

UNIT_SIZE = 244


def run_case(e, found):
    calls = []
    e.hook(0x463990, lambda ev: (0x5000 if found else 0), 0)
    e.hook(0x648B90, lambda ev: (calls.append(("SetTarget", ev.arg(0), ev.arg(1))), 0)[1], 2)
    e.hook(0x649970, lambda ev: (calls.append(("PATH_CalculatePath", ev.arg(0), ev.arg(1), ev.arg(2))), 0x7)[1], 3)

    unit = e.alloc(UNIT_SIZE)
    dynamic_path = e.alloc(16)
    e.w32(unit + 44, dynamic_path)   # D2UnitStrc.pPath (D2PathUnion), structs: D2UnitStrc
    result = e.call(0x480780, 1234, ecx=unit, edx=1) & 0xFFFFFFFF   # eUnitType 1 = UNIT_MONSTER

    target = 0x5000 if found else 0
    expect = [("SetTarget", dynamic_path, target)] + ([("PATH_CalculatePath", dynamic_path, unit, 0)] if found else [])
    expect_result = 0x7 if found else 0
    ok = calls == expect and result == expect_result
    print(found, "OK" if ok else f"DISAGREE calls={calls} result={result:#x}")


def main(count, seed):
    e = emu.Emu()
    for found in (False, True):
        run_case(e, found)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
