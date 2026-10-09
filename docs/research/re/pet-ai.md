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
DruidWolf, DruidBear, ShadowWarrior and ShadowMaster with their inits
(`components/rules/pets.hpp`, "The other thinks" and "The Shadows").
InvisoPet, 7TIllusion and Buffy aren't any player skill's summon.

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
- **ShadowWarrior** (`0x5eafa0`): no owner, stand 100. +0x18 (the mana
  load) drops by aip4 + 1, back to 0 under 0 or over aip8(H) × 64 (MonStats
  +0x84; aip8(NM) is +0x82, both read whatever the difficulty; shadowwarrior
  has 5 / 64). The driver's target unless over aip1 off, or the owner over
  aip2 off; decide (reach 6). With a target and an owner with both a left
  and a right skill (`FUN_00620190` / `FUN_006201d0`: the skill list's +8 /
  +0xc): one at random (rand(2) set: the left); in melee
  rand(100) under aip3 − 2 × +0x1c (5..100) takes Attack instead. Not
  usable (`FUN_005ead50`), the other; not that either, Attack (given by
  `FUN_00647280` if missing). `FUN_005ead50`: the skill's class
  (`FUN_00645040`) the owner's and its AI type allowing it
  (`FUN_005eabf0`); Attack then always; else rand(100) over 100 − mana ×
  160 / 100 fails, the frame before +0x14 fails, +0x18 held in aip8(NM)
  (1..128) .. aip8(H) × 32 (1..256), rand(+0x18) over rand(100) passes, and
  +0x18 grows by (320 − +0x1c) × mana / (+0x1c + 100). A missile kind
  (`FUN_00645460` 1) out of melee runs at the target (`FUN_005ded20`); else
  the skill in its mode (`FUN_00644360`) and +0x14 = frame + calc / 3 + 18.
  Else stand 25.

Checked: `tools/emu/pet_ais.py` runs each in game.exe with the follow /
decide answers, searches, gaps and skills from random setups and every
move down to `FUN_005a7c20`: 1 500 cases each, all equal (log, AI control,
seed). Its `--dump` writes `tests/pet_ais_cases.inc`, which
`tests/test_monsters.cpp` replays through the C++ (up to 16 an AI: the
literal stays under 64 KB). d2d runs
them in `Fight::pet_think_turn` / `pet_cast`.

## The Shadows

AI table rows 105 / 106 (`0x73ca20`): ShadowWarrior think `0x5eafa0`, init
`0x5eb490`; ShadowMaster think `0x5eb970`, init `0x5ecb70`. Both read
MonStats aip slots by fixed offset as well as by difficulty: +0x56..+0x60
are aip1 (N, NM, H) and aip2 (N, NM, H), +0x80 aip8 (N) is the summoning
skill (268 Shadow Warrior, 279 Shadow Master), +0x82 / +0x84 aip8 (NM / H).

### Their skill lists

A new monster (`FUN_004ae8d0`, Monster.cpp) gets each MonStats Skill1..8
(+0x170) with Sk*lvl (+0x198) above 0 at Sk*lvl + DifficultyLevels
MonsterSkillBonus (row +0x10, `FUN_00611d30`), its mode Sk*mode (+0x180,
`FUN_00644340`). Both Shadows' rows list Fists of Fire, Blade Fury, Blades
of Ice, Dragon Claw, Dragon Flight, Claws of Thunder (their seq_sw*
sequences) and Attack (A2), at level 1. A skill joins the list's tail
(`FUN_00647110`, mode: Skills.txt monanim +0x11 for a monster, anim +0x10
for a player); giving one it has sets its level (+0x28, `FUN_00647280`).
The Shadow Master gets nothing more: its init (`FUN_005ecb70`) gives no
skills, nor does do 49 (`FUN_005d6e70`: stats `FUN_005d6cf0`, gear
`FUN_005d6b60`), nor Skills.txt (no sumskill). MonAI 143
ShadowMasterNoInit runs its think with no init (no monster uses it).
A handle's kind (`FUN_00645460`) is Skills.txt range (+0x14: none 0, h2h
1, rng 2, both 3, which counts 2 with a missile weapon and 1 else); its
mode `FUN_00644360` (+8).

### ShadowWarrior's init (FUN_005eb490)

AI control 0, 0, 1. With an owner that is a player: +0x1c its level, with
bonuses, in the skill aip8 (N) names (`FUN_006439f0`, `FUN_006442a0(.., 1)`).
Attack (given at level 1 if missing, `FUN_00647280`) goes on both hands
(`FUN_00643bc0` / `c50`). Then each skill of the owner's class
(`FUN_00451f60`, `FUN_00646140` / `FUN_006460f0`) it may have
(`FUN_005eab20`: no Skills.txt `summon`, or one that isn't this monster and
whose `pettype` isn't this pet's, `FUN_00574a20`) at the owner's hard points
/ 2 + +0x1c / 2, 1..24 (`FUN_005eb420`; none counts 1). Each think it gives
itself the owner's left and right skills again at +0x1c / 3 + their level
with bonuses / 2, at least 1 (`FUN_005eaf00`).

### ShadowMaster's init (FUN_005ecb70)

+0x14 −1, +0x18 aip3 + 1, +0x1c a seed step's low bit.

### ShadowMaster's think (FUN_005eb970)

Its row's P56..P60 below are the fixed aip slots above; "rand(P5c)" is
rand(aip2 (N)). The driver's target, distance and melee are AI params [2]
/ [5] / [6]; distances are squared subtiles (`FUN_005b0bd0`).

1. No skill list (+0xa8): stand 100.
2. Over P60 from its owner: decide (no foe, reach 6).
3. +0x14 above 0 (a repeat): the pet's own target (`FUN_00553540`) if any,
   else the driver's; none clears +0x14 / +0x18; else +0x14 − 1 and cast
   +0x18 at it (the cast below).
4. The driver's target over P5e off, or none: each skill it has, by
   aitype — 1 with an aurastate it isn't in (one of its State group on:
   only rand(100) < 4), 60 in 100: cast it at nothing (a kind-1 skill out
   of melee walks instead, `FUN_005ded00(0, 4)`); 6 with no left skill
   (`FUN_00620190`), 20 in 100 it becomes the left skill (`FUN_00643bc0`).
5. With an owner: the owner's target, if not dying and a foe
   (`FUN_00554200`), becomes the target (the helper). Within 12 of the
   owner: decide on the target. No target: stand 25.
6. In melee, rand(100) under aip3 − 2 × max(+0x1c, 1) (5..100): Attack.
7. The scan (`FUN_005dd0b0` mode 1, `FUN_005eb6d0` on each unit; below).
   Out of melee the target becomes the helper, else the closest foe to the
   owner, else the last one worth chasing within 32. Not worth chasing
   (`FUN_005eb650`, below): its owner instead when not dying and within 32.
8. Over 3 foes within 10 and rand(32) < 2 × that: over 6 from the owner,
   run to it; else 8 away from the target (`FUN_005df140`, path type 0xf).
9. Each skill scores aibonus + reqlevel / 4 + its level − (the target's
   resist to its EType: none 36 damage, fire 39, light 41, magic 37, cold /
   12 43, poison 45) / 10, then by aitype (near: the closest foe's
   squared distance ≤ 25; `all`: foes within 32; d²: to the target):
   - 1 (with its aurastate on, or none): −6 if near; −10 with another of
     its State group on, else +10; + rand(P5c); at itself.
   - 2 (not in its aurastate, the target not in auratargetstate): −10 if
     near; + rand(P5c).
   - 3: −2 × the side's traps when over 5; −7 if near; −10 under 3 foes;
     + all × 3 − 9 + rand(P5c).
   - 4 / 12: −10 past P56² off; + P58; +10 in melee or within 5. A
     progressive skill (Skills.txt flags bit 2, a charge-up) in its
     aurastate: the state's aurastat1, its charges (`FUN_006256b0` /
     `FUN_00625d00`), adds to a running sum and 3+ drops it. 4: + P5a with that flag, else −10 when P5a > 0 and
     `FUN_0063a2b0` says no, else + sum × 4 + 3. 12: only at a monster whose
     MonStats +0xa0 by difficulty is 25+; + 8 under 75 % life, + 12 more
     under 50 %. + rand(P5c).
   - 5 / 11: only with a clear line (`FUN_00622aa0` mask 4); no srvmissile
     and a srvmissilea whose Range − 1 the target is past: dropped; −5 each
     if near, within 5, or `FUN_0063a2b0`; + rand(P5c); 11 + all × 3.
   - 6: rand(100) under 20 (6 with a left skill): it becomes the left skill.
   - 7: life over 66 %: dropped; + rand(P5c) + 10 (+20 under 45 %), at no
     unit (bug #17). 8: likewise ×2 (×4 under 45 %), at itself.
   - 13: + P58; −5 when P5a > 0 and `FUN_0063a2b0` says no, else + the sum.
     Under 50 % life or over 3 foes near, with the owner's closest foe and
     under 4 near the owner and that foe over 5 off: +20 at it. Else under
     5 off dropped, over 18 off +10. + rand(P5c).
   - else nothing.
   A skill joins the list only beating the last one's score (the list
   starts with Attack at the target, score 0).
10. From the last: a seed step whose low 2 bits aren't 0 tries it (melee by
    `FUN_00622c40`). Cast (`FUN_005eb8b0`): not at its owner or itself;
    it has the skill (`FUN_006439b0`); a unit target must be targetable
    (+0xc4 bit 2) and the pet out of town; kind 1 out of melee walks at it
    (`FUN_005ded00(target, 4)`), else the skill in its mode. A srvdofunc 19
    skill (the channelled Inferno, Arctic Blast) cast is repeated: +0x18 it,
    +0x14 25.
11. Attack at the target, else stand 15.

**The scan (FUN_005eb6d0)**, skipping itself and the dying: its side's
traps (`FUN_00650d70`, MonStats id 0x19a..0x1a0 but 0x19e) count [8].
Else targetable foes: by squared distance to the owner, ≤ 100 count [6]
and the closest is [4] / [5]; within 1024 of the pet count [7], ≤ 100 [3],
the closest [1] / [2], and one worth chasing is [9] (the last).
**Worth chasing (FUN_005eb650):** a monster, not dying, whose MonStats
flags aren't npc but are killable, and boss, primeevil, or a
superunique / champion / unique (monster data +0x16 & 0xe).

The Assassin's skills' aitypes: 1 Quickness (Burst of Speed), Fade, Venom,
Blade Shield; 2 Cloak of Shadows; 3 the sentries, Shadow Warrior / Master;
4 the claws and kicks; 5 Fire Trauma (Fire Blast), Psychic Hammer, Blade
Sentinel, Blade Fury; 10 Claw Mastery, Weapon Block (never scored); 11
Shock Field (Shock Web), Mind Blast; 12 Cobra Strike; 13 Dragon Flight.

Checked: `tools/emu/shadow_init.py` (both inits and `FUN_005eabf0`, 3 000 cases),
`tools/emu/shadow_master.py` (the think with the cast, group test, walk
away and distances native, 5 000 cases, every aitype scored; the scan
callback with `FUN_005eb650` native, 2 000 cases), all equal.

### In d2d

`Fight::summon_one` builds a Shadow's skill list (`Pet::skill_list`: the
MonStats skills, then `shadow_warrior_init` or `shadow_master_init`).
`Fight::pet_think_turn` runs `shadow_warrior_think` on a PetScene with the
owner's left / right skills (d2s header), giving them each think, and
`shadow_master_think` on the units round the pet (the monsters within 64
subtiles of it or its owner, its fellow pets), its world turned into a
PetAct. `Fight::shadow_cast` does the skill on the action frame: Attack and
the claws / kicks as the owner's blow with the skill's swing, missile
skills through `fire`, Psychic Hammer and Mind Blast through `spot`,
Dragon Flight beside the target then its kick (skills.md "Dragon
Flight"). Charge-ups charge the Shadow (`Pet::charges`, as the player's),
finishers take their bonus and release them; the AIs see a charge-up's
aurastate on it with its count. A Shadow moves as an Assassin: its state
`shadowwarrior` (States gfxtype 2, gfxclass 6) makes `FUN_00645270` read
it as player class 6, so CharStats Walk / RunVelocity (its MonStats
Velocity is 0). A pet's walk at its foe ends in melee as its think tests
it (`FUN_00622c40`: MeleeRng + 1).
Not built: other states on any unit (buffs, `FUN_0063a2b0`); buffs,
traps and summons cast by a Shadow do nothing; MonStats +0xa0 (aitype
12's floor).

