# Stat lists — game.exe 1.14d (`.\Stats\StatsEx.cpp`)

A unit's stats live in the stat list at unit +0x5c. The list holds a
sorted array of 8-byte entries {key, value} (key = stat id << 16 | layer),
found by binary search (`FUN_00624b90`), inserted by `FUN_00624c80` and
removed by `FUN_00624d10`; both resize with the assert file name
`.\Stats\StatsEx.cpp` (lines 0xe8 and 0x110).

## SetStat — FUN_006270b0 (list, stat, value, layer)

Cited, not run (no oracle in this pass). Read from the decompile:

- A null list returns 0.
- It looks the key up. Missing and `value` 0: returns 0, nothing changes.
  Missing otherwise: a new entry is made (`FUN_00624c80`, old value 0).
- `value - old` equal to 0: returns 0, nothing is touched.
- `value` 0 removes the entry (`FUN_00624d10`); else the entry's value
  is overwritten (a set, not an add). Then `FUN_00626920(delta, 0)`
  is called with the change.
- If the list's flags (+0x10) have bit 31 and list +8 is 0, the key
  `stat << 16 | (layer & 0xffff)` is passed to `FUN_00624dd0`, which
  queues it in the list's changed-stat array (+0x50, assert line 0x162)
  only when the stat's ItemStatCost record (`PTR_DAT_00744304 + 0xbcc`,
  0x144 each) has bit 0x10 in byte +5, and never for stats 6, 8, 10,
  0xd and 0xe. It returns 1 when it changed something.

Re-check: `python3 tools/ghidra/fn.py 006270b0`, `python3 tools/ghidra/fn.py 00624dd0`.

d2d: reads the same, cited from the decompile, no oracle run (not a
contradiction). d2d keeps stats in plain arrays
(`d2s::Stats`), has no key list and no changed-stat queue;
objects.md:81 already notes "sets, not adds" for this function.
