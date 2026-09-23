# DS1 objects → NPCs and objects — game.exe 1.14d

A DS1's object list is `{type, id, x, y, flags}` with x/y in subtiles
(5 per cell). Type 1 = monster/NPC, type 2 = object.

## Type 1 (NPCs)

`id` indexes the act's rows of **MonPreset.txt** (`Act`, `Place`). `Place`
is a **MonStats2.txt** `Id` (layers `HD..S8`, per-layer component
`HDv..S8v` — comma lists, first is the default — and `BaseW`). The monster
token (`Code`, the folder under `data\global\monsters\`) is in MonStats.txt
at the same row index (both tables are hcIdx-ordered). Act 1 town:
0 gheed GH, 2 akara PS, 3 chicken CK, 4 rogue1 RG, 5 kashya RC,
7 warriv1 WA, 8 charsi CI, 13 cow CW. MonPreset/MonStats2 only exist in
the 1.14d patch data (installer layer, see mpq-archives.md).

## Type 2 (objects)

`id` → objects.txt `Id` through a table compiled into game.exe:
`u32 table[5][150]` at `0x748ad8` (act-major, 600 bytes per act),
read by `FUN_006658e0(act, id)`; `FUN_00665860(act)` counts an act's
entries up to the first 0 (act 1: 113, act 2: 135, act 3: 116, act 4: 66,
act 5: 150). Embedded as `apps/d2d/obj_preset.hpp`.

objects.txt then gives `Token` (folder under `data\global\objects\`),
layer flags `HD..S8`, and per mode (`Mode0..7` = NU OP ON S1..S5) its
frame counts, deltas and light radius. Composite files:
`objects\<tok>\COF\<tok><mode>HTH.cof`, layers `<tok><LY>LIT<mode>HTH.dcc`.

Act 1 town: 1 → 37 torch `TO` (ON), 2 → 39 camp fire `RB` (ON),
3/4 → 35/36 flags `N1`/`N2`, 33 → 78 `TA`, 52 → 119 waypoint `wp` (ON),
102 → 267 stash `b6`, 110 → 385 `ss`.

Start mode in d2d: ON when the object has a light in ON (`Mode2` and
`Lit2`), else NU. D2 really sets it in the object's `InitFn`.
