# The dynamic path record — game.exe 1.14d

`.\PATH\Path.cpp`, 0x648460..0x649d00. A unit that moves has a dynamic path record (0x200 bytes in Redivivus's struct;
the functions here read up to +0x1f8) at unit +0x2c. This page covers the 70 small functions in
that file that were not yet traced: field accessors, flag toggles, the
collision and path-type wrappers, and the missile path dispatcher. The oracle
`tools/emu/path_dynamic.py` runs each of them in game.exe against a model on
random records and compares the whole path and unit record afterwards (5,065
cases). The names are Redivivus's (`D2DynamicPathStrc`), used as leads; the
offsets below are what the code reads.

Not claimed: d2d's own movement code matches these. The routines that build
and walk paths (`FUN_00649400`, `FUN_00649560`, `FUN_00649970`, the A* and
direct-path builders) were already traced or are outside this page.

## The record (offsets used by these functions)

| Offset | Size | Field |
|---|---|---|
| 0x00, 0x04 | 4, 4 | precise X, Y (16.16: tile in the high word, subtile in the low) |
| 0x02, 0x06 | 2, 2 | the same words read as the subtile X, Y by the collision calls |
| 0x08, 0x0c | 4, 4 | screen X, Y |
| 0x10, 0x12 | 2, 2 | target X, Y (SP1) |
| 0x14, 0x16 | 2, 2 | previous target (SP2) |
| 0x18, 0x1a | 2, 2 | saved target (SP3) |
| 0x1c, 0x20 | 4, 4 | room, next room |
| 0x24, 0x28 | 4, 4 | path point index, path point count (max 77) |
| 0x2c | 4 | server path (on a unit's own path record this is read as the path) |
| 0x30 | 4 | owner unit (its +0 is the unit type) |
| 0x34 | 4 | flags |
| 0x38 | 4 | recalc counter |
| 0x3c, 0x40 | 4, 4 | path type, previous path type |
| 0x44, 0x48 | 4, 4 | unit size, unit width |
| 0x4c, 0x50 | 4, 4 | collision flag, collision pattern |
| 0x54 | 2 | collision flags result |
| 0x58, 0x5c, 0x60 | 4 each | target unit, its type, its GUID |
| 0x64, 0x65, 0x66 | 1 each | direction, new direction, diff direction |
| 0x67, 0x68 | 1, 1 | missile sub offset, offset |
| 0x7c, 0x80, 0x84, 0x88 | 4 each | velocity, saved velocity, max velocity, acceleration |
| 0x90, 0x91, 0x92, 0x93 | 1 each | distance, max distance, IDA* variable, step |
| 0x98 | 4 | direction offset of the path type |
| 0x9c.. | 4 each | path points (X word, Y word), 78 of them |
| 0x1d4 | 4 | count of saved init coordinates; entries follow at 0x1d8 |

Flags (+0x34) these functions touch: 0x02, 0x04, 0x20 (path built), 0x40
(direction frozen), 0x2000 (save the previous type), 0x4000 (previous type
saved), 0x8000 (save the velocity), 0x10000 (velocity saved), 0x20000 (has
saved init coordinates).

## Functions

In address order. "get"/"set" is the whole behaviour unless noted.

| Function | Does |
|---|---|
| `FUN_00648460` | `STRUCT_FreePath(pool, path)`: the allocator's debug free (`FUN_0040b480`) with `Path.cpp` line 0x3d9 |
| `FUN_00648480` | collision pattern from MonStats (EAX = unit): record `[class]` of the table at `sgptDataTable +0xa78` (0x1a8 bytes each, count at +0xa80); byte +0xd bit 6 → 0x1804; else byte +0xc bit 3 → 0x3401, else 0x3c01. No range guard: a class below 0 or at/above the count, or a null unit, reads address 0xd and faults |
| `FUN_006485f0` | `SetDirection` (ECX path, DL direction & 0x3f): owner object or item sets the facing directly; missile sets it unless flag 0x40; any other owner (or none) turns toward it: if different from the new direction, stores it and diff = `DIFFDIRECTIONS[((new − current) & 0x3f) * 4]` (table at 0x6eb548) |
| `FUN_00648640` | `InterpolateDirection`: unless equal or flag 0x40, adds the diff to the facing (mod 64), then snaps to the new direction when the remainder is under 0x1f and the diff under 0x3f, or the remainder over 0x1f and the diff over 0x3f |
| `FUN_006486c0`, `FUN_006486f0`, `FUN_00648720` | velocity, max velocity, acceleration getters |
| `FUN_006486d0`, `FUN_00648700` | max velocity, acceleration setters |
| `FUN_00648730` | clears flag 0x20 and the point count |
| `FUN_00648750` | flag 0x20 on for a non-zero argument, off for zero |
| `FUN_00648770` | flag 0x20 getter (returns the masked bit) |
| `FUN_00648790` | point count = argument, capped at 77 (signed compare: negatives are stored) |
| `FUN_006487c0`, `FUN_006487d0` | point index; `GetPathPoint` writes `&path + 0x9c` to its out pointer and returns the count |
| `FUN_00648810` | new direction (+0x65) |
| `FUN_006488a0` | direction and new direction both = argument & 0x3f; a null path does nothing |
| `FUN_00648940`, `FUN_00648970` | precise X, Y; a null path is fatal, `Path.cpp` lines 0x6a8, 0x6b3 |
| `FUN_006489a0`, `FUN_006489b0` | precise X, Y setters |
| `FUN_006489c0`, `FUN_006489d0` | screen X, Y getters |
| `FUN_006489e0`, `FUN_006489f0` | screen X, Y setters (fastcall: ECX path, EDX value) |
| `FUN_00648a00`, `FUN_00648a10`, `FUN_00648a20`, `FUN_00648a30` | target X, Y and saved target X, Y words, zero-extended |
| `FUN_00648a40`, `FUN_00648a60` | X, Y word of the last path point; 0 with no points |
| `FUN_00648a80`, `FUN_00648a90`, `FUN_00648aa0`, `FUN_00648ab0` | room get/set, next room get/clear |
| `FUN_00648ad0` | `SetSP1(x, y)`: target words stored and the target unit cleared; a null path does nothing |
| `FUN_00648b00` | `SetSP1Shifted(x, y)`: (x + 2y, 2y − x) through `FUN_00643540`, then each `>> 11` (arithmetic) into the target words |
| `FUN_00648b30`, `FUN_00648b40` | flag 0x02 get / set-or-clear |
| `FUN_00648b60`, `FUN_00648b80` | flag 0x04 set-or-clear / get |
| `FUN_00648b90` | `SetTarget(unit)`: stores the pointer; copies the unit's type (+0) and GUID (+0xc) only when it is non-null |
| `FUN_00648bb0` | `GetTargetTypeAndGUID`: writes the target type and GUID; no target is fatal, line 0x7c0 |
| `FUN_00648bf0` | target unit; null path → 0 |
| `FUN_00648c10` | collision flag (+0x4c); null path → 0xffff |
| `FUN_00648c30` | `ChangeCollisionType(flag)`: with a room, remove the old flag at the subtile (words +2, +6) then store the new one and add it. A missile owner (unit type 3) uses the size at +0x44 (`FUN_0064eba0` remove, `FUN_0064ea00` add); any other owner (tested: player, monster, none) the width at +0x48 (`FUN_0064ec10`, `FUN_0064ea90`). Without a room it only stores the flag |
| `FUN_00648cd0`, `FUN_00648ce0` | collision pattern get / set |
| `FUN_00648cf0` | `SetPathType(type)`: see below |
| `FUN_00648dc0` | `RestoreVelocityAndPathType`: no owner is fatal (0x87e); restores the saved velocity if flag 0x8000; a player goes to type 7, anything else back to its saved type if flag 0x2000 |
| `FUN_00648e30`, `FUN_00648e40`, `FUN_00648e60`, `FUN_00648ea0` | path type get; distance set / get; max distance get |
| `FUN_00648ef0` | collision flags result (word at +0x54) |
| `FUN_00648f00` | sets the IDA* variable; only path type 0 may, otherwise fatal, line 0x8e8 |
| `FUN_00648f40` | `GetInitCoords`: out = `&path + 0x1d8`, returns the count at +0x1d4; without flag 0x20000 fatal, line 0x8f4 |
| `FUN_00648f90`, `FUN_00648fe0` | X, Y word of the last saved init coordinate (0 with none); fatal without flag 0x20000, lines 0x902, 0x911 |
| `FUN_00649030` | **inverted**: a zero argument sets flag 0x40, non-zero clears it; null path does nothing |
| `FUN_00649050` | previous target words = 0 |
| `FUN_006490a0` | step number **+ 1** |
| `FUN_006490c0` | missile sub offset, offset (one byte each) |
| `FUN_00649180`, `FUN_00649190` | unit width get / set, **argument is a unit** (`unit +0x2c` → path +0x48) |
| `FUN_006491b0` | **argument is a unit**: sets or clears bit 0x2000 of the unit's flags at +0xc8 and zeroes the recalc counter of its path (`unit +0x2c`, +0x38); no path is fatal, line 0x9a2 |
| `FUN_00649210` | `PeriodicPathRecalc(unit)`: only if unit +0xc8 bit 13 is set and +0xc4 bit 21 clear; bumps the path's counter (+0x38) and on the second call resets it and, if `FUN_00650540` says the target is not reached, calls `FUN_00650350(unit, has target)` and on success `FUN_00624690(unit, 2)`. No path is fatal, line 0x9b5 |
| `FUN_006492c0` | server path (+0x2c) has index (+0x24) below count (+0x28), signed; null → 0 |
| `FUN_00649760` | `CalculateMissilePath` (EAX path, ECX unit): type 4 → `FUN_006492f0`, and if it builds nothing flag 0x20 is cleared and 0 returned; type 10 → `FUN_0067a240`, type 14 → `FUN_0067a140` (both with `unit + 0x20`); any other type is fatal, line 0x207. A built path sets flag 0x20 and returns the count |
| `FUN_00649ca0` | `UpdateCollisionWidth(unit)`: with a path, a unit in a room has its collision removed (`FUN_00649560(unit, 1)`), the width set to `FUN_00648580(unit, size)`, then added back (`FUN_00649400`); without a room only the width is set. No path does nothing |

## SetPathType (FUN_00648cf0)

Flags per type come from `PATHTYPEFLAGS` (0x6eb690), the direction offset from
`PATHTYPEPOINTS` (0x6eb648), 18 entries each. A player (owner unit type 0) may
not take type 2 (fatal, line 0x861). Otherwise: if the type's flags have 0x2000
and the path lacks 0x4000, the current type is saved at +0x40; if they have
0x8000 and the path lacks 0x10000, the velocity is saved at +0x80; then
`flags = (flags & 0xfff800ff) | typeflags`, the type and offset are stored. A
saved type of 0xb or 8 is then fatal (lines 0x871, 0x872), and so is type 4
with a max distance of 0x4e or more (line 0x873) — the checks run on the state
after the store.

## Notes for d2d and for Redivivus

- Redivivus types `FUN_006491b0` (`PATH_SetOrClearFlag0x2000`) as taking a
  `D2DynamicPathStrc *`; the code reads `[arg + 0x2c]` as the path and writes
  `[arg + 0xc8]`, so the argument is the unit and the flag is unit flag bit 13
  that `FUN_00649210` tests. Not filed anywhere.
- `FUN_00649030` (`SetUnsetFlags0x40`) and `FUN_006490a0` (`GetStepNum`) read
  the opposite of their names; callers should be checked before d2d copies the
  names.
- The oracle hooks `FUN_00408a60` to catch the fatal branches, and records
  rather than runs the callees listed above; those callees are not claimed here.
