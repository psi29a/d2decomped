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

Traced and built: **NecroPet** (below). The rest still run d2d's own
follow-and-fight in `Fight::pets_turn`.

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
