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

**The seed.** Every InitFn draws from the game's object seed: the
dispatcher (`FUN_0054f5d0`, table 0x731bc0 by objects.txt InitFn) hands
them `{game, unit, room, FUN_00546fa0()'s rng, objects record, ...}`.
That rng is objrgn.cpp's (`FUN_00546c60`, game +0x10f0), seeded at game
start with `{the game seed's second step, 666}`; the first made the
monster regions (monsters.md). One for the game, drawn as objects come up.
After the InitFn, a PreOperate object (record +0x13d) draws `rand(14)`
and starts in mode 2 (ON, already opened) on 0, unless the unit's flag
0x80 is set (not traced).

**Which shrine** (InitFn 1, `FUN_0054f9d0`): objects.txt Parm0 (+0x178).

- 0: any Shrines.txt row, `1 + rand(count - 1)`.
- 1: a health shrine (effectclass 2); 2: mana (3).
- else: one step, `% 10 == 0` magic (1), else a boost (4).

A class picks `rand(n)` of its rows in row order (`FUN_0054f770`, lists from
`FUN_00546c60`). Both paths re-roll (8 tries at most) while the level id is under
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

**Chest init** (InitFn 3, `FUN_0054fcb0`), stored in
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
- Then the trap fires (`FUN_00582510`, the table at 0x732cec). Trap 8
  first checks its monster (below); a flying scimitar in act 1 means no
  trap at all. Otherwise the chest's trap event (`FUN_005417d0`) and
  `FUN_00553380` follow, whatever the trap did.

| Trap | Function | Does |
|---|---|---|
| 1 | 0x582490 → `FUN_00582420` | trap monster 330 trap-lightning (AI Trap-Missile) |
| 2, 6 | 0x5824b0 | 326 trap-firebolt (Trap-Missile) |
| 3 | 0x5824d0 | 329 trap-poisoncloud (Trap-Poison) |
| 4 | 0x5824f0 | 369 trap-nova (Trap-Nova) |
| 5, 7 | `FUN_00582380` | fires: objects 162 (fire large) at the chest, 160 (fire small) one subtile east |
| 8 | `FUN_005822f0` | one or two undead of the level's family |

**Trap monsters** (`FUN_00582420`): the monster at the chest's subtile
(`FUN_005b3090` → `FUN_005b2a00`, mode 1, flags 0x88); if that fails, the
first spot clear of collision mask 0x3f11 (`FUN_0064e840`), then
`FUN_005b2f20` (radius 3, mode 8). Their AIs (the server AI table at
0x73ca20, 16-byte rows; MonStats +0x1e is the AI, +0x56 / +0x5c / +0x62 are
aip1..3 by difficulty) all run the same way:

- While there's a target within aip1 subtiles and it has acted fewer than
  aip2 times: on one think it acts (count + 1), on the next it idles
  aip3 frames.
- Otherwise it flags the target (0x20000 at +0xc4) and goes to mode 0,
  death. aip2 is 1 for all four, so each trap acts **once** and is gone. A
  player out of range when it's made gets nothing.
- Trap-Missile (77, `FUN_005fb5b0`; trap-firebolt, trap-lightning): mode 4
  (A1) at the target, aip1 25, aip3 15. With no skill set, A1 fires the
  mode's missile (`FUN_005a7670` → `FUN_005a6d50`: `FUN_0063e6b0`, MonStats
  +0x3a MissA1) through `FUN_0056ecb0` with skill 0 at level
  **MonsterSkillBonus + 1**, or the monster's level when +0xc4 has 0x200
  (not traced). MonsterSkillBonus is DifficultyLevels +0x10: 0 / 3 / 7, so
  level 1 / 4 / 8.
- Trap-Poison (80, `FUN_005fb900`) and Trap-Nova (92, `FUN_005fb9b0`): the
  monster's Skill1 (MonStats +0x170, mode Sk1mode +0x180) at the target
  (`FUN_005dead0`); aip1 20, aip3 15. Monster skills are given at Sk*lvl
  (+0x198) + MonsterSkillBonus (`FUN_00573cb0` with `FUN_00573930`): level
  1 / 4 / 8 again.

**What each one does**, at missile level L = 1 / 4 / 8. A missile's
elemental damage (`FUN_0064b860` → `FUN_0064b100` / `FUN_0064b1d0`) is
EMin / Emax plus the per-level columns, `<< HitShift`. A row with a Skill
takes that skill's damage at L instead:

| Trap | What | Damage |
|---|---|---|
| 1 | chainlightning (its Skill is Chain Lightning) | lightning 1-40 / 1-73 / 1-117 |
| 2, 6 | trapfirebolt, one at the target | fire 4-16 / 43-58 / 95-114; explodes (fireexplode) |
| 3 | PrimePoisonNova (do 99, `FUN_005ccd10`): 8 primepoisoncloud at the offsets at 0x6e31a8 / 0x6e31e8, speed Param1 << 6, then 8 more between them at Param2 << 6 (calc2 2) | poison 10 / 25 / 45 per frame in 256ths, over 300 / 360 / 440 frames |
| 4 | Trap Nova (do 22, the nova ring of trapnova) | lightning 1-20 / 34-59 / 78-111 |

- MonStats' own columns mislead here. Trap-poisoncloud's MissA1
  (trappoisonjavcloud) never fires, because its AI casts Skill1. Trap-nova's
  MissA1 / MissS1 (firebolt, nova) aren't used either: the skill's
  srvmissilea is trapnova.

**Fires** (5, 7): both made in mode 2 (`FUN_005540a0`). The small one only
when the subtile east is still in the chest's room. Their InitFn 22
(`FUN_0054fb40`) and OperateFn 11 (`FUN_005843d0`) only switch modes. The
only readers of objects.txt Damage (+0x19c) are `FUN_005df990` /
`FUN_005dfa00`, reached from the gas and exploding traps (`FUN_00581680`,
`FUN_005818b0`, `FUN_00581cd0`) and the exploding barrel (`FUN_00584240`),
not from fire. So the chest fire does no damage as far as traced.

**Trap 8** (`FUN_005822f0`):

- Count: one step of the seed at the start of the game's monster-region
  block (game +0x10f0), `(seed & 1) + 1`.
- Family (`FUN_005474c0`, cached per level at region +0x1c): the first of
  the level's region monsters (region +0x14, 0x34 each) that's a zombie
  (zombie1..5, ids 5..9; mummy1..5, 96..100, in act 2) or a skeleton
  (0..3), skeleton archer (170..173) or skeleton mage (274..277,
  379..382, 383..386, 387..390). It gives the family's first id. With
  none it's 234, a flying scimitar, and in act 1 (`FUN_00582250`) that
  means nothing happens.
- Each one (`FUN_00582280`): `FUN_005b2490` picks the level's variant
  (`FUN_0063ec70`: the Levels mon list entry with the same BaseId, else
  a step along the family by `FUN_006510c0`, not traced). It's placed at
  the chest in mode 8, tried at radius −1, then 1, then 3.

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
- Seeds: the object seed starts as game.exe's, but d2d starts it afresh
  per level and draws in DS1 order; game.exe draws one rng across the
  game as rooms come up, so which shrine a spot gets can still differ.
- Built: every recharge and boost shrine (the skill shrine as
  item_allskills 127 on the skill levels), gem (18: `FUN_00582c40`, the
  first inventory gem with a misc.txt BetterGem goes up one, else a
  chipped gem, `rand(6)`: gcw gcr gcg gcb gcy gcv), warping (20: see
  monsters.md), locked chests and keys, the empty quarter, and all eight
  traps (`Town::spring_trap`; rules in shrines.hpp: `kTrapMissile`,
  `kTrapLevel`, `kPoisonNova`, `trap_undead`). The trap monster isn't
  made: its one shot leaves the chest at once. A ring's missiles strike
  each foe once.
- Simplified: no aip1 range check (whoever opens the chest is close);
  chainlightning doesn't hop; trapfirebolt's fireexplode isn't spawned;
  the fires last until a new game; trap 8's monsters stand up aware,
  without mode 8, one step apart; the variant comes from the level's
  region rows (Levels' mon list and `FUN_006510c0` not read).
- Not built: chest 397's table.
- Traced but not emulator-checked: everything under "Trap monsters" and
  "Trap 8". Open: unit flag 0x200's source, `FUN_006510c0`'s variant step,
  the AI's target pick.

## Magic shrines (2026-09-27)

The shrine effect table at 0x6e1850 has 12-byte entries {fn, stat, state},
indexed by Shrines.txt Code, run from `FUN_00583c70`.

- 17 portal: `FUN_00582a30` opens a town portal (`FUN_0056d130`, object 0x3b) from the nearest free spot to the player + 5 subtiles on each axis (collision 0x1c09, size 3). Built (server.hpp `open_portal_at`).
- 19 storm: `FUN_00582da0`. Every player and monster found by the unit search over Arg1 (2000) loses Arg0 % (50) of its current life. Then 16 missile 62 (fireball) shots at level clvl / 5 (1..8):
  - x offset ±5k subtiles for k = 1..4 (+ when k is odd);
  - y offset 5, −10, 15, −20.
- 21 exploding / 22 poison: `FUN_005830e0` / `FUN_00583410`.
  - Arg0 + rand(Arg1 − Arg0) potions (`opm` exploding / `gpm` choking gas), each with quantity 1 (stat 70), drop at the operator.
  - Then 6 missiles, 45 explosivepotion / 48 chokinggaspoition, fly at offsets (±6 / 0, ±6) at level clvl / 5.

d2d: `World::operate` and `Fight::shrine_missiles`. The potions are
friendly missiles that burst their row damage over sHitPar1 subtiles.
ponytail: the missiles hit monsters only, the poison cloud is a single
burst, and "everyone" means everyone within 30 cells.

## Town portals

- **Reading one:** a Scroll of Town Portal (tsc) or a Tome's charge (tbk
  quantity) casts Skills.txt 219 / 220: anim SC, srvdofunc 113
  (`FUN_005bf3d0`), not in town (checkfunc 5). Sounds:
  player_townportal_cast (2230) on casting, object_townportal (2633) as it
  opens, player_townportal_enter (2231) going through.
- **Opening** (`FUN_0056d130`): a Portal object (objects.txt 59, TP) at the
  nearest free spot from the caster (collision 0x3e01, size 3), then its twin
  in the act's town at spawn index 11 — the town DS1's special tile 33, +3
  subtiles — at the nearest free spot (`FUN_0056cf40`, mask 0xbe11); each
  unit linked to the other (`FUN_00621ce0`), the portal's +4 byte the other's
  level. A player's new portal closes their old pair.
- **The object:** OP 15 frames at FrameDelta 200 (768 ms), then ON looping
  (CycleAnim2); Lit1 18 / Lit2 19, (120, 120, 255); its COF's HD layer is
  transparent with draw effect 3 (additive), TR opaque. OperateRange 2.
- **Going through** (OperateFn 15, `FUN_00584870`): out by the linked portal.
  Refused within 5 s of the player's hostility time (player data +0x160; not
  in single player) and to other parties' portals.

d2d: `World::read_portal` / `open_portal_at` / `use_portal` (server.hpp),
the View's `portals`, drawn and lit as object 59 (town.hpp `view_units`,
`frame_light`); the client names them -2000 - which for Interact.
ponytail: the caster-side spot is the nearest free 0.6 cells south of the
player, not game.exe's search; hover names are the destination level's
name (untraced); Lit1 while opening isn't used.

## Random object groups per room (FUN_00552610, 2026-09-29)

Called from FUN_0052d160 (room populate) between the room's preset units
(FUN_005559a0) and its monster population (FUN_0054ec90); see monsters.md
"When a room populates". So its seed steps happen **before** monsters roll,
and building it changes every subsequent monster/champion roll in the room.

**Inputs.** Levels.txt ObjGrp0..7 (`+0xe5..+0xec`, bytes: objgroup.txt row
via the Offset column) and ObjPrb0..7 (`+0xed..+0xf4`, bytes: 0..100).
objgroup.txt (`data\global\excel\objgroup.txt`) columns:
`GroupName, Offset, ID0, DENSITY0, PROB0, ID1..PROB7, SHRINES, WELLS`
(28 in all). The compiled `objgroup.bin` has a 4-byte header (u32 = 133
rows in 1.14d) then 0x34-byte records indexed by **Offset**: 8 x u32 ids
(0x00..0x1f, objects.txt row), 8 x u8 densities (0x20..0x27, passed to
PopulateFn as `param`), 8 x u8 probs (0x28..0x2f, cumulative weights that
sum to 100), 4 tail bytes (SHRINES / WELLS, unread by 552610). Rows Act 1
outdoors touch:

| Off | Name | Ids | Densities | Probs |
|---|---|---|---|---|
| 3 | Indoor Chests | 5, 6 | 48, 48 | 50, 50 |
| 4 | Rogues for act1 w/o staked rogues | 54, 55, 56 | 30, 30, 30 | 37, 37, 26 |
| 5 | sewer shrines | 279..282 | 0 | 25 each |
| 6 | Cave Wells | 138, 275, 276, 277 | 0 | 25 each |
| 7 | Crypt caskets | 3, 28 | 125, 125 | 75, 25 |
| 33 | outsideforestobj1 | 139, 140, 144 | 30, 30, 30 | 25, 50, 25 |
| 34 | outsideforestobj2 | 141, 139 | 30, 30 | 50, 50 |
| 38 | outside forest object 3 | 155, 174, 175 | 30, 30, 30 | 34, 33, 33 |

**Algorithm** (fastcall(game, room1); room1 seed = `{s, 666}` at room1+0x6c;
LCG step is `s = s * 0x6ac690c5 + high`, standard drlg step):

1. Level record `lvl = game.levels[room.level_id]` (rec 0x220 bytes).
2. Eligibility (`FUN_00552560`): pass on all four of `FUN_0061a210` == 0
   (room flag 0x30000 not set on the LEVEL, not a wilderness reveal),
   `FUN_0061a1f0(room)` != 0 (real level id), `FUN_0061abb0(room)` == 0
   (room-status +0x48 not "1", i.e. not town), and `FUN_0061ab00(room)` == 0
   (a byte at `level_record+0x1d0`, not yet named — probably a "no object
   groups" gate). Then bump the objrgn per-level entry's counter (+4) and
   cache its target (+8, `0x7fffffff` → FUN_0061abf0 result). A final
   density throttle (`FUN_00552400`) can also veto.
3. For slot i = 0..7:
   a. Step room seed once. `roll = seed % 100`.
   b. Density-skip: if the objrgn slot's `weight*128/count > 96` and the
      objgroup row's byte +0x167 is set, force `roll = 100`.
   c. If `lvl.ObjGrp[i] != 0` and `roll <= lvl.ObjPrb[i]`:
      Step seed again. Walk the 8 entries of `objgroup[ObjGrp[i]]`,
      accumulating `probs`; on the first index where `roll2 % 100 < acc`,
      look up objects.txt row for that id, check `+0x172 <= difficulty`
      (some level-gate byte), read `+0x1b2` as the **PopulateFn index**
      (< 10; asserts otherwise), call
      `PopulateFn[fn](param, obj_id, density=100)` (fastcall) where param
      is the objgroup entry's byte at +0x20+j.

**Populate-fn table** (0x731d00, 10 x u32; d2d game.exe 1.14d):

| Idx | Address | Handles |
|---|---|---|
| 0 | 0 | (unused, asserts if row < 10 but ptr == 0) |
| 1 | 00550c20 | object-id 3 (barrels), 79, 1, 4, 89, 208, 209 (per switch) |
| 2 | 00552b50 | |
| 3 | 00551470 | |
| 4 | 00551850 | |
| 5 | 00551c00 | |
| 6 | 00551690 | |
| 7 | 00551200 | |
| 8 | 005516c0 | |
| 9 | 00551580 | |

Each PopulateFn draws from the **object seed** (game+0x10f0, `objrgn.cpp`,
FUN_00546c60) — not the room seed — to pick a subtile and place the unit
via FUN_00555230, which itself steps the game seed once (per monsters.md
"Every unit made steps the game seed").

**objrgn init** (FUN_00546c60, `.\OBJECTS\objrgn.cpp`): game+0x10f0 is
allocated as an 0x1110-byte struct at game start; one game-seed step
(matches the second step monsters.md notes); then for each level 1..N a
per-level 0x90 struct is allocated at `objrgn+0x48+id*4`: `+0` = a Levels
byte, `+8` = 0x7fffffff sentinel, `+0x1c` = -1. Also 8 "buckets" at
`objrgn+0x28..+0x44` count objects.txt rows by their byte at `+0xb2`, with
per-bucket arrays at `objrgn+0x8..+0x24`.

**Ripple on d2d today.** Currently d2d's Blood Moor matches game.exe (155
monsters at seed 3, 107 fallen1 / 24 quillrat1 / 24 zombie1) because the
emu oracle also skips 552610 (it only calls FUN_006194a0 for level alloc,
not FUN_0052d160). Adding 552610 to d2d without adding it to the oracle
will change every subsequent room-seed roll and break the count. The port
therefore needs its oracle side (emu drives per-room populate) landed
alongside the C++ implementation.

**d2d today.** `d2d::rules::place_object_groups` (monsters.hpp) runs the
seed steps of the algorithm — 8 unconditional room-seed steps plus one
extra per fired slot, and returns which objgroup entry was picked. Called
per room at `build_level` time (gamedata.cpp) with the room's index and
the level's room count so the throttle can fire past 75 % population, the
picks land in `Level::npcs` alongside preset objects (positions are a
naive object-seed pick, not the PopulateFn's own), and the post-552610
seed is cached on `Level::post_object_group_seeds`. `populate()` then
feeds that cached seed to `SpawnRoom` so monster rolls line up with
game.exe's without recomputing 552610 per tick. Levels.txt ObjGrp0..7 /
ObjPrb0..7 land on `LevelMon`; objgroup.txt on `GameData::obj_groups`;
objects.txt PopulateFn byte isn't parsed (the pick uses `add_object` to
compute mode / trap / shrine like a preset object). On the Blood Moor's
default map seed: 34 random object-group placements land in the level.

Not built: bit-exact PopulateFn positions (`FUN_00731d00`'s 9 handlers
each have their own subtile-pick algorithm on the object seed), and the
objgroup +0x167 gate that game.exe reads with an objgroup id as if it
were an objects.txt row (a likely bug we skip).

