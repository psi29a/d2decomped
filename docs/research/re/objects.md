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

## Shrines and chests

**Which shrine** (InitFn 1, `FUN_0054f9d0`): objects.txt Parm0 (+0x178).

- 0: any Shrines.txt row, `1 + rand(count - 1)` on the object's seed.
- 1: a health shrine (effectclass 2); 2: mana (3).
- else: one step of the object's seed, `% 10 == 0` magic (1), else a boost (4).

A class picks `rand(n)` of its rows in row order (`FUN_0054f770`, lists from
`FUN_00546c60`) on the *game's* object seed (`FUN_00546fa0`), not the
object's. Both paths re-roll (8 tries at most) while the level id is under
the row's LevelMin. Then 5 → 3, 4 → 2 (the exchanges are never placed) and
16 → 18 (Enirhs becomes a gem shrine).

**Operating a shrine** (OperateFn 2, `FUN_00583c70`): the effect table at
0x6e1850 by Code, 12 bytes a row (function, stat, state).

| Code | Function | Does |
|---|---|---|
| 1..3 | 0x5828e0 / 0x582860 / 0x5828a0 | life and / or mana to max |
| 4, 5 | 0x582940 / 0x5829a0 | Arg0 % of one to Arg1 % on the other |
| 6, 8..11, 13, 15 | 0x583b30 | Arg0 on its stat for Duration frames: 171 armor %, 39/43/41/45 resists, 27 mana regen %, 85 exp % |
| 7 | 0x5839b0 | Arg0 % of the attack rating as tohit (19), Arg1 damagepercent (25) |
| 12 | 0x583bf0 | +Arg0 all skills |
| 14 | 0x583a70 | stamina filled, staminarecoverybonus (28) 1000 |
| 16..22 | 0x582a00 .. 0x583410 | magic shrines |

Shrines reset after Shrines.txt "reset time in minutes" (0: never).

**Chest init** (InitFn 3, `FUN_0054fcb0`, on the object's seed), stored in
the object's flag byte:

- First the trap (`FUN_0054fbb0`, also InitFn 2): `rand(100) < MonLvl1 / 8
  + 5` gives a trap type `1..8` (`FUN_004bc500`), in the low 7 bits.
- Then, when objects.txt Lockable (+0x171) is set, `rand(100) < MonLvl1 / 2
  + 8` locks it (0x80). One more seed step.
- MonLvl1 is Levels +0x10, the classic normal column, on every difficulty.

**Opening** (OperateFn 4, `FUN_00585f60`):

- Locked: the player needs a key (`FUN_0055f140`: an inventory item of type
  key; one off its quantity, 0x46, or the key goes). Without one nothing
  opens. A locked chest drops **two** rounds.
- `rand(100) > 24`, or locked, or the flag behind `FUN_005540d0`: it drops.
  So an unlocked chest is empty one time in four. With that flag and
  nothing dropped, up to 10 more tries.
- objects.txt 397 has its own tiered table (gold, potions); not in act 1's
  levels.
- Then the trap fires (`FUN_00582510`, the table at 0x732cec):

| Trap | Function | Does |
|---|---|---|
| 1 | 0x582490 → `FUN_00582420` | trap monster 330 trap-lightning (MissA1 chainlightning) |
| 2, 6 | 0x5824b0 | 326 trap-firebolt (trapfirebolt) |
| 3 | 0x5824d0 | 329 trap-poisoncloud (trappoisonjavcloud) |
| 4 | 0x5824f0 | 369 trap-nova (MissS1 nova) |
| 5, 7 | 0x582380 | objects at the chest and one subtile over |
| 8 | 0x5822f0 | one or two of the level's monsters (`FUN_005474c0`) |

The drop's item level: `FUN_0055a6d0` takes the chest unit and no level,
so it's the unit's, the area level.

**Chest treasure class** (`FUN_00585b90`): the TC is `"Act %d%s Chest %s"` (`FUN_0065a2c0` builds the
table at 0x96c5f4: "", " (N)", " (H)"; A, B, C). `FUN_00654e80` picks by
difficulty, act and class. The class comes from the area level against the
act's two marker levels at 0x6e1988: (2, 37), (41, 73), (76, 102),
(104, 108), (109, 136). With `third = (|hi - lo| + 1) / 3`: under
`lo + third` A, under `lo + 2 third` B, else C.

In d2d: `components/rules/shrines.hpp`, `Town::operate`.

- Proven: the tables and branches above, read from the decompile. Not
  emulator-checked.
- Seeds: the object's and the game's object seed aren't emulated, so which
  shrine a spot gets differs from game.exe.
- Built: every recharge and boost shrine (the skill shrine as
  item_allskills 127 on the skill levels), gem (18: `FUN_00582c40`, the
  first inventory gem with a misc.txt BetterGem goes up one, else a
  chipped gem, `rand(6)`: gcw gcr gcg gcb gcy gcv), warping (20: see
  monsters.md), locked chests and keys, the empty quarter, traps 2, 3, 6.
- Traps: the missile flies once from the chest with its Missiles.txt
  element at the area level. The trap monsters' own AIs (Trap-Missile,
  Trap-Poison, Trap-Nova) aren't built.
- Not built: magic shrines 17 (portal), 19 (storm), 21 (exploding), 22
  (poison); traps 1 and 4 (their missiles' damage comes from a skill,
  not traced), 5 and 7 (objects), 8 (monsters); chest 397's table.
