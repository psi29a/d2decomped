# The pets' AIs

Pets are monsters with an owner (AI control +0x28, the owner's id +0x2c;
FUN_0058f0d0). Their MonStats AI picks a row of the AI table at `0x73ca20`
(16-byte rows: think, init, a count, a special):

| MonAI | name | think | MonStats rows |
|---|---|---|---|
| 67 | NecroPet | `0x5e4cf0` | claygolem, bloodgolem, irongolem, firegolem, valkyrie, necroskeleton, necromage (wolf, bear unused) |
| 86 | Hydra | `0x5e9e60` | hydra1..3 |
| 88 | 7TIllusion | `0x5ea080` | seventombs |
| 101 | AssassinSentry | `0x5ea3d0` | the Assassin's traps |
| 102 | BladeCreeper | `0x5ea540` | bladecreeper |
| 103 | InvisoPet | `0x5ea7a0` | invisopet |
| 104 | DeathSentry | `0x5ea980` | deathsentry |
| 105 / 106 | ShadowWarrior / ShadowMaster | `0x5eafa0` / `0x5eb970` | |
| 107 | Raven | `0x5ecc10` | druidhawk |
| 108 | DruidWolf | `0x5ed710` | spiritwolf, fenris (row 0x1a4 → `0x5ecee0`, else `0x5ed2a0`) |
| 109 | Totem | `0x5ed9e0` | spiritofbarbs, heartofwolverine, oaksage |
| 110 / 111 | Vines / CycleOfLife | `0x5ec6c0` / `0x5ec8c0` | plaguepoppy; cycleoflife, vinecreature |
| 112 | DruidBear | `0x5ed730` | druidbear |

Traced and built: NecroPet (below, `necropet_think`) and Hydra, Totem,
Vines, CycleOfLife, AssassinSentry, DeathSentry, BladeCreeper, Raven,
DruidWolf, DruidBear (`components/rules/pets.hpp`, "The other thinks").
InvisoPet, 7TIllusion and Buffy aren't any player skill's summon. Not
traced yet: ShadowWarrior (`0x5eafa0`, init `0x5eb490`) and ShadowMaster
(`0x5eb970`, init `0x5ecb70`): they copy and score the owner's skills.

## NecroPet (FUN_005e4cf0)

`FUN_005e4cf0(ECX game, EDX pet, [8] AI params)` drops the pet's events of
type 2 (`FUN_00540e60`), then runs `FUN_005e4ac0` when AI control +0x14 is
set, else `FUN_005e4830`. +0x14 is written only by `FUN_0058ec00` from a
talk (`FUN_00548b00` case 1, C→S 0x59 `FUN_0054ca10`: 0x28), so a pet
always takes `FUN_005e4830`.

### FUN_005e4830

1. No owner: if the AI control's +0x1c (the owner's id, refreshed every
   think) still finds a unit, `FUN_005e3ea0` mode 3, else stand 10.
2. Gap to the owner (`FUN_005dc380`, the pet's size) over 50: mode 3 (a
   teleport into the owner's room). 29 or over: mode 0, at pace
   MonStats Run (+0x34) × 100 / Velocity (+0x32) − 100 (100 when that's
   over 99 or Velocity < 1; it can be negative).
3. The foe: `FUN_005dd7f0` (for a good monster: mode 5, the primary else
   the secondary) — dropped when its +0xc4 bit 30 is set — and again
   through `FUN_005dde50` (only if within 0x18). One 6 or nearer by the
   search is kept; else the first one when its gap is under 0x24. Then
   `FUN_005dc640` must find a clear line, else no foe.
4. `seed.step() % 100` (raw, not `FUN_0045c390`): under 15 lets
   `FUN_005e45d0` stay put (and its reach is 8, else 7).
5. `FUN_005e45d0(foe, owner, in melee, ctx, stay, reach)` returning 1 ends
   the think.
6. A foe, outside town: in melee, `rand(100)` over 0x4f stands 10, else
   `FUN_005ddf90(4, foe)` (A1 at it). Out of melee: pace (0, 0, 0xc) then
   `FUN_005ded40(foe, 7)`: path type 0xd, a walk (mode 2) at the unit, 1
   off. When that can't set off (`FUN_005deb60` flags 7): AI flag 0x40
   (`FUN_005dd230`: the next search needs sight), then 70 in 100 a wander
   of 4 (`FUN_005de200`), else stand 10.
7. No foe: wander 4 about itself (`FUN_005df400`).

### FUN_005e45d0 (follow or fight)

`reach += owner's pets / 2` (`FUN_00574f40`: the counts in player data
+0x44's per-pettype list), 36 at most.

- Within 1 of the owner, the owner standing (mode 1) and not in melee:
  mode 5.
- A foe and not in town: over 0x50 off mode 3, else 0 (fight).
- Else mode 2; 0 when the owner walks, runs or walks in town (2, 3, 6),
  or its path's +0x10 differs from its end (+0x18) on both axes; 1 on
  another level (`FUN_0061b130`) or over reach; 3 over 50.
- Within 0x1c (`FUN_005dc5c0`, no size) of where the owner last arrived
  through a warp (player data +0x148 / +0x14c, written by `FUN_00554ea0`):
  1 under 30 off, and 2, become 4.
- `stay` and mode 2: return 0. Else `FUN_005e3ea0(owner, pet, mode, 0, 0,
  reach)`.

### FUN_005e3ea0 (after the owner)

The merc's `FUN_005e3930` grown: `(ECX game, EDX owner; pet, mode, run,
pct, reach)`. A walk is `FUN_005dee50`, a run `FUN_005deeb0` (a walk in
state 0x3c). Returns 1 when a move set off.

- **0**: as the merc's (8 off the owner's path end ahead of it, round the
  compass on the owner's level, pace (pct, 0x28)), each spot then the
  halfway point from the pet. None: wander 4 about the pet; with `run`,
  run halfway to the owner; walk halfway; walk to the owner
  (`FUN_005ded90`, its result returned).
- **1**: the owner walking: pace (0, 100) to its path end, then halfway.
  Then the pace is `pct`, else rand(40) + 40 on the pet's seed, and the
  footsteps as the merc's (newest first; a spot with x or y 0 skipped;
  type 0, 0xf, once 1); none: wander a quarter of the gap (4 at least,
  pace 0xf).
- **2**: rand(100) under 10: wander rand(3) + 3, then pace (0, 0x28) to
  the owner's path end. Then stand 15.
- **3**: `FUN_0054dc40` finds a spot in the owner's room
  (`FUN_00554ea0`), the pet stands 5.
- **4**: the owner's pets within 2 of this one (`FUN_00574de0` with
  `FUN_005e3900`): any, reach away from the owner on each axis
  (`FUN_005defe0`, pace steps reach when over 5), else wander reach. Then
  stand 15.
- **5**: path type 7 (`FUN_005de190` maps type 1 to 7), reach about the
  owner (`FUN_005df530`), reach away, pace (0, 0x28) to its path end.

### The line (FUN_005dc640)

`(ECX pet, EDX foe)`: k = 2 / 3 / 4 by their gap (under 3 / 11 / 25,
else 3), signed toward the pet on each axis (sx, sy). Lines
(`FUN_006229f0(pet, x, y, 0x805)`: `FUN_00622920` from the pet's size to
a size-2 end) to the foe, to (fx − sy, fy + sx), to (fx + sy, fy − sx);
the first clear one returns 1.

### Checked

- `tools/emu/necropet.py` runs FUN_005e4cf0 natively on random setups
  (owner, path, footsteps, arrival, pets, crowding, levels, town, a foe in
  or out of melee, a clear line or not, which moves find a path): 20 000
  cases, all equal to the port, effects and seed; `--dump` gives
  `tests/test_monsters.cpp`'s 40 cases.
- `tools/emu/pet_line.py` runs FUN_005dc640 on random walls: 1 500 cases,
  all equal.
- Port: `components/rules/pets.hpp` (`necropet_think`, `pet_line_clear`),
  run by `Fight::necropet_turn`.

## The other thinks

Each runs on what `FUN_005b1740` (the AI driver) hands it: AI params [0]
the AI control (+0x14 / +0x18 / +0x1c kept between thinks), [2] / [5] /
[6] the driver's target, its distance and in melee, [7] the MonStats row.
aip1..5 are MonStats +0x56 / +0x5c / +0x62 / +0x68 / +0x6e by difficulty
(game +0x6d). "Follow" is `FUN_005e3ea0`, "decide" `FUN_005e45d0` (above).
Distances: `FUN_005dc380` (gap, the first unit's size) and `FUN_006416d0`
(each axis less half of both sizes; (short + 2 × long) / 2).

- **Hydra** (`0x5e9e60`): past its last frame (+0x14) it dies
  (`FUN_005ddfc0(0)`); the driver's target under 25 off, a seed step % 100
  under 60 fires Skill1 (`FUN_005dead0`); else stand 10.
- **Totem** (`0x5ed9e0`, Oak Sage, Heart of Wolverine, Spirit of Barbs): the
  search's foe within 24 in melee, step % 100 under aip1: 6 away from it
  (`FUN_005defe0`, flag 1). Step % 100 under aip2 forgets the foe. Over aip3
  from the owner `FUN_00554ea0` teleports it into the owner's room (then
  stand 25); over aip4 it follows as the owner moves (mode 0, pace 60 when
  running). Then decide (reach 6), else stand 25.
- **Vines** (`0x5ec6c0`, Poison Creeper): aip5 or more from the owner,
  follow mode 3; in town decide, else stand aip3. `FUN_005ddc30`'s foe
  under aip2: decide; a foe in state 2 (poisoned) or with poison resist
  (stat 0x2d) 100 it leaves aip4 away; out of melee a walk at it
  (`FUN_005dec80` flags 7); in melee Skill1 every aip1 frames (+0x18).
  Else stand aip3.
- **CycleOfLife** (`0x5ec8c0`, Carrion Vine 0x1aa, Solar Creeper 0x1ab):
  aip5 from the owner, mode 3. The driver's target unless dying
  (`FUN_005541b0`). A corpse (`FUN_005d2f80`) within Skill1's calc of the
  owner (5..50) under aip2 off: decide; in melee of it, with the owner's
  life (stat 6 < `FUN_00625d10`) or mana (stat 8 < `FUN_00625d60`) short,
  Skill1 in mode 8 every aip1 frames. Else hit by the target, rand(100)
  under 25 aip4 away from it; no corpse stand aip3; else a walk at it.
- **AssassinSentry** (`0x5ea3d0`, init `0x5ea510`: -1, 1, 0) and the trap
  shots `FUN_005ea2b0`: no owner, the owner in town, or +0x18 shots spent
  (Skill1's calc when below 0) and it dies. `FUN_005ddc30`'s foe under aip4:
  rand(100) under aip1 a shot (Skill1 in its skill's mode,
  `FUN_00644360`), a shot spent; else stand aip2; no foe stand aip3.
- **DeathSentry** (`0x5ea980`, init `0x5eaf50`: +0x1c the owner's skill
  level): no skill level, nothing. A corpse (`FUN_0056e390`) not blown last
  (+0x14) within half the radius (`FUN_004cc7c0`) of the foe: Skill1 in
  mode 9. The foe under aip4, rand(100) under aip3: Skill2 in mode 0xe.
  Else stand aip2.
- **BladeCreeper** (`0x5ea540`, Blade Sentinel): +0x14 its last frame
  (Skill1's calc on); past it, dies. Its missile is made once (+0x1c,
  `FUN_0056ede0`). It walks (path type 0xf, 20 steps) to one end of its line
  (`FUN_0058ee80` +0xc / +0x14; +0x18 which first), flipping when one fails;
  else 5 about the owner (`FUN_005df530`; with no owner `FUN_005de200(2)`),
  else stand 5. No line: stand 3.
- **Raven** (`0x5ecc10`): +0x14 its hits (-1 at first: Skill1's
  `FUN_004efcb0`, else 3); 0 and it dies (`FUN_0057ccb0`). Over 50 from the
  owner mode 3, over 28 mode 0. The driver's target past the +0x18 frame,
  rand(100) under aip4 and under aip5 off: in melee a swing (a hit spent,
  +0x18 = frame + aip3 × 10), else a walk at it. Between aip2 and aip1 off
  the owner it circles it (`FUN_005ecbc0`: path type 5 or 6 by +0x1c, 4
  steps, at the owner; a failure flips +0x1c and tries the other). Else
  (aip1 + aip2) / 2 off the owner on the line to itself (`FUN_005de6f0`),
  else follow mode 1.
- **DruidWolf**, Spirit Wolf (`0x5ecee0`, row 0x1a4): in town decide, else
  stand 33. The foe: the search's within aip4 with a clear line, else
  within aip4 by gap when the line *isn't* clear (as written). Over 50 from
  the owner Skill1 ("Teleport 2") at its spot, wait 10. Over aip5 a run
  after it (mode 0, pace 100); over aip3 after it as it moves. Decide
  (staying, 6). A foe: in melee a swing, wait aip1; one the owner has
  within aip4, a run at it (`FUN_005ded20`; a walk in state 0x3c) at the
  pet's pace; else over 10 from the owner it keeps 6 off it
  (`FUN_005de4e0`, 8 a move); else stand 15. No foe: rand(100) under aip2
  wander 10, else stand 15.
- **DruidWolf**, Fenris (`0x5ed2a0`): as the Spirit Wolf with Skill2 for the
  teleport, the fallback needing the clear line, and no foe past aip4 when
  pet and foe are both over aip4 from the owner. Its rage (Skill1, not in
  state 0x8a; with a foe and +0x14 clear only rand(100) under aip3): a
  corpse within 10 of the owner (`FUN_005d2f80`) under aip4 / 2 off, in
  melee Skill1 at it; out of melee a run at it (+0x14 1, +0x18 its id).
- **DruidBear** (`0x5ed730`): over 50 mode 3, over 28 mode 0 pace 100, over
  18 after the moving owner. The foe within 28 with a clear line (or by
  gap). Out of melee a walk at it (rand(100) under aip2 at the pet's pace,
  40 steps); in melee rand(100) under aip3 Skill1 as a sequence
  (`FUN_005de000`, mode 0xe), else a swing and wait aip1. Else stand 15
  within 16 of the owner, else follow.

Checked: `tools/emu/pet_ais.py` runs each in game.exe with the follow /
decide answers, searches, gaps and skills from random setups and every
move down to `FUN_005a7c20`: 1 500 cases each, all equal (log, AI control,
seed). Its `--dump` writes `tests/pet_ais_cases.inc`, which
`tests/test_monsters.cpp` replays through the C++ (128 cases). d2d runs
them in `Fight::pet_think_turn` / `pet_cast`.
