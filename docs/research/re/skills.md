# Skills — research notes and plan (1.14d)

Status: research only; the machinery below is traced, the per-skill
functions aren't. d2d already has the skill tree (spending points,
`docs/research/re/skill-tree.md`); no skill can be used yet. The combat these
skills plug into is in `combat.md`.

## Skills.txt (357 rows, 30 per class)

What a skill is, column by column (the ones that matter first):

- **Dispatch**: `srvstfunc` (on start, 1..65), `srvdofunc` (on the action
  frame, 1..152), `srvprgfunc1..3` (progressive charge finishers),
  `cltstfunc` / `cltdofunc` / `cltprgfunc*` (the client's visuals), plus
  `passive`, `aura`, `summon`, `progressive`. Across the classes: 26
  passives, 26 auras, 32 summons, 6 progressive skills.
- **Animation**: `anim` (SC cast 118, A1 attack 30, SQ sequence 19, S1..S4,
  TH, KK), `seqtrans` / `seqnum` for sequences; `UseAttackRate` = swing at
  the weapon's attack speed (IAS / WSM, `rules::attack_ticks`), else cast
  rate (FCR).
- **Cost**: `mana`, `lvlmana`, `manashift` (mana in 1/2^shift points),
  `minmana`, `startmana`, `usemanaondo`, `AttackNoMana` (fall back to a
  plain attack when out of mana).
- **Targeting**: `range` (h2h / rng / both / none), `itypea1..3` /
  `etypea1..2` (weapon types allowed / excluded), `TargetableOnly`,
  `SearchEnemyXY`, `SearchEnemyNear`, `TargetCorpse`, `LineOfSight`,
  `leftskill` (can go on the left button), `InTown`.
- **Damage**: `ToHit` + `LevToHit`, or `ToHitCalc`; `SrcDam` (128 = all of
  the weapon damage); physical `MinDam` / `MaxDam` with `MinLevDam1..5`,
  elemental `EType`, `EMin` / `EMax` with `EMinLev1..5` / `EMaxLev1..5`,
  `ELen` + `ELevLen1..3`, all shifted by `HitShift`; synergies in
  `DmgSymPerCalc` / `EDmgSymPerCalc` (e.g. Fire Bolt:
  `(skill('Fire Ball'.blvl)+skill('Meteor'.blvl))*par8`).
- **Formulas**: `calc1..4`, `passivecalc1..5`, `aurastatcalc1..6`,
  `auralencalc` are expressions over `lvl`, `blvl`, `par1..8`, `ln12` /
  `ln34` / `ln56` (linear in level: par_a + par_b × (lvl − 1)), `dm12` /
  `dm34` / `dm56` (diminishing returns), and `skill('Name'.blvl)`.
- **Passives / auras**: `passivestate`, `passiveitype`, `passivestat1..5` +
  `passivecalc1..5` (e.g. Claw Mastery: `passive_mastery_melee_th` = `ln12`,
  `_dmg` = `ln34`, `_crit` = `dm56`); `aurastate`, `aurastat1..6` +
  `aurastatcalc1..6`, `aurarangecalc`, `auralencalc`.
- **Missiles**: `srvmissile`, `srvmissilea..c` / `cltmissile*`: Missiles.txt
  rows (the same table as the quill rat's spikes, `Scene::MissileInfo`).

SkillDesc.txt holds the icon, tree position and tooltip lines, and is
already read for the tree.

## game.exe anchors

- Dispatch tables in `.data` (found by the Assassin functions' addresses,
  then `XrefsRange.java` for the code that indexes them):
  - **`srvstfunc` @ 0x732140** (index 0 empty), read by FUN_0056f640 and
    FUN_0056f7f0. `[1]` = 0x56ca40 (Attack), `[23]` = 0x5d32f0
    (Assassin charge-up start), `[32]` = 0x5d7ea0 (Bash).
  - **`srvdofunc` @ 0x7322b0**, read by FUN_0056d790, FUN_0056d810,
    FUN_0056f7f0 and FUN_005d5220. `[1]` = 0x56f070 (Attack), `[2]` =
    0x56f1f0 (weapon hit with skill damage: Bash, ...), `[22]` = 0x5c9b50
    (Poison Nova), `[34]` = 0x5d3490 (Tiger Strike, Cobra Strike, Royal
    Strike), `[35]` = 0x5d35d0 (Fists of Fire, Claws of Thunder, Blades of
    Ice).
  - a third table @ 0x7325b0, read by FUN_0056e740 (~50 entries). Not the
    progressive table: `srvprgfunc` values index `srvdofunc` (see below).
- Per-class server code, by source file (the `.\SKILLS\*.cpp` strings, all
  xrefs in `docs/research/re/game-strings.tsv`): SkillAss.cpp from
  0x5d3e80 (FUN_005d3e80, 5d6630, 5d76e0, 5d7b60, 5d7ce0), SkillMonst.cpp
  from 0x5cb940, SkillNec.cpp, SkillSor.cpp, SkilPal.cpp, SkillDruid.cpp,
  SkillAma.cpp, SkillBar.cpp, SkillItem.cpp. D2Common's skills (levels,
  damage, mana) are around `.\SKILLS\Skills.cpp` @ 0x6438b0..0x643ec2 and
  0x56bad0..0x56bee9.
- FUN_00612750 loads the keyword tables (`compcode`, `elemtypes`,
  `hitclass`, `monmode`, `plrmode`, `skillcalc`, `misscalc`, `skills`,
  `events`, ...) from data\\global\\excel.

## Traced (2026-09-25, second pass)

### Skill records
Skills.txt compiles to 0x23c-byte records at `[0x744304]+0xb98` (count
`+0xba0`). Offsets used below: +0x12e ResultFlags, +0x130 HitFlags, +0x134
HitClass, +0x138 calc1, +0x13c calc2, +0x140 calc3, +0x144 calc4 (calc
fields hold offsets into the calc bytecode), +0x148..0x164 par1..par8,
+0x186 minmana, +0x188 manashift, +0x18a mana, +0x18c lvlmana,
+0x198/0x19c/0x1a0 ToHit / LevToHit / ToHitCalc, +0x1a4 HitShift, +0x1a5
SrcDam (0 → 128), +0x1dc conversion element, +0x1e0 / +0x1e4 EMin / EMax,
+0x210 EDmgSymPerCalc, +0x214..0x224 ELen, ELevLen1..3, ELenSymPerCalc,
+0x98 passivestat1..5 (shorts), +0xa4 passivecalc1..5.

### Per-level values
- **Level brackets** (FUN_00644b70): `*Lev1` for each level 2..8, `Lev2`
  9..16, `Lev3` 17..22, `Lev4` 23..28, `Lev5` 29 up.
- **Elemental damage** (FUN_00644d50 min, FUN_00644e40 max):
  `(EMin + brackets) << HitShift`, plus the synergy calc as a percent of
  that, plus the elemental mastery (stats 0x14a..0x14c) when asked.
- **Elemental length** (FUN_00644f20): `ELen + ELevLen1..3` over 2..8,
  9..16, 17 up, plus ELenSymPerCalc as a percent.
- **To-hit** (FUN_006449f0): `ToHit + LevToHit × (lvl − 1)`, or ToHitCalc.
- **Mana cost** (FUN_0056bf.. / FUN_0056c160, 8.8 mana):
  `(mana + lvlmana × (lvl − 1)) << manashift`, at least `minmana × 256`;
  skills cast from item charges don't cost mana.

### The calc language
Calcs are bytecode run by a stack machine, **FUN_006c0bc0**(code, length,
operand callback, function table, function count, context):

| op | meaning |
|---|---|
| 1 n | call function n of the table (0..3 arguments) |
| 4 / 5 / 6 | operand from the callback (1 / 2 / 4-byte argument) |
| 7 / 8 / 9 | push a 1 / 2 / 4-byte constant |
| 0x0a..0x0f | `<` `>` `<=` `>=` `==` `!=` |
| 0x10..0x14 | `+` `−` `×` `÷` (÷0 → 0) power |
| 0x15 | negate |
| 0x16 | `c ? a : b` |
| anything else | end: the top of the stack is the result |

The function table (0x745774, 7 entries with arity): 0x6436e0 and
0x6436f0 (two arguments; by shape min and max), 0x643700 `rand(a, b)`,
0x646c00 (a skill reference), 0x643740 (a missile field, FUN_0064b340),
0x643770 `stat(id, base / accr / mod)`, 0x646c60 (skill field, three
arguments).

Operands resolve through **FUN_00646460**(unit, code, skill, level). The
code order is **skillcalc.txt** (missiles use misscalc.txt):
`0 ln12, 1 dm12, 2 ln34, 3 dm34, 4 ln56, 5 dm56, 6 ln78, 7 dm78,
8..15 par1..par8, 16 lvl, 17 edmn, 18 edmx, 19 edln, 20 toht, 21 mana,
22 mps, 23 math, 24 madm, 25 macr, 26..34 m1en m1ex m1el m2en m2ex m2el
m3en m3ex m3el, 35..37 m1rn m2rn m3rn, 38 edns, 39 edxs, 40 ulvl, 41 blvl,
42 usmc, 43..48 missile physical, 49..53 enma exma edma enms exms, 54 len,
55..58 clc1..clc4, 59 rng, 60..65 ast1..ast6, 66..70 pst1..pst5, 71 pets,
72 skpt`.
- `ln` (FUN_004e6ca0): `a + b × (lvl − 1)` on the pair (par1, par2), etc.
- `dm` (FUN_00645b20): `a + (b − a) × ⌊110·lvl / (lvl + 6)⌋ / 100`, at most `b`.
- `edmn` / `edmx` are elemental damage in whole points (`edns` / `edxs` in
  256ths), `mps` = mana cost × 25 / 2, `ulvl` = character level, `blvl` =
  base skill level.
- `math` / `madm` / `macr` (FUN_00647e00): the skill's passive calc for
  its to-hit / damage / crit mastery stat (0x156..0x158 melee, 0x159..0x15b
  throw).
The keywords aren't in game.exe's strings: they come from skillcalc.txt /
misscalc.txt, which FUN_00612750 loads, so game.exe can compile the calc
text itself (the compiler isn't traced; 1.14d also ships the compiled
.bin). d2d can compile the Skills.txt text to the same ops with the same
operand and function lists.

### Melee skills, start to finish
- **srvstfunc** (e.g. [32] Bash, FUN_005d7ea0):
  1. Roll to hit with the skill's to-hit bonus (FUN_0057ec10 →
     FUN_0057d9b0, combat.md).
  2. On a hit: record flags from ResultFlags / HitFlags / HitClass,
     enhanced damage % = **calc1**, conversion % = calc4 into the element at
     +0x1dc.
  3. Build the damage with SrcDam (FUN_0057dbf0 → FUN_0057b7d0).
  4. Physical += **calc2** × 256 (post-damage add).
  5. Apply the skill's self state.
- **srvdofunc** on the action frame: [1] Attack (FUN_0056f070): the plain
  attack record (a missile weapon fires instead). [2] (FUN_0056f1f0):
  apply the skill's states (target state for the calc at +0x60 ticks, self
  state), then resolve the record built at start. Charge-up skills use
  [34] / [35] (FUN_005d3490 / FUN_005d35d0) and their `srvprgfunc1..3`,
  which are **indices into the same srvdofunc table** (Fists of Fire:
  143, 38, 39). The table at 0x7325b0 is not the progressive table (likely
  aura / passive events).

### Choosing skills
- The save holds them: header +0x38 sixteen hotkeys (u32 skill id,
  0xffff = none, 0x8000 = assigned to the left button), +0x78 left skill,
  +0x7c right skill, +0x80 / +0x84 the weapon-swap pair. Erza: left Dragon
  Talon, right Death Sentry, swap Attack / Shadow Master.
- The client's skill buttons are drawn by SkillsBar.cpp (FUN_004c83c0 ..
  FUN_004c9280), the skill picker by SkillsPal.cpp (FUN_004c9b40); icons
  are `Spells\<Cl>Skillicon` (already loaded for the tree).

### Still unknown
- Each skill's own srvstfunc / srvdofunc body beyond Attack, [2] and Bash's
  start (Dragon Talon's kicks, sentries, missiles, auras): trace them per
  phase.
- The calc text compiler (d2d writes its own), and which of 0x6436e0 /
  0x6436f0 is min and which max.
- Cast rate (FCR) breakpoints and the per-class animation tables for
  FHR / FBR.
- The skill bar and picker layout (SkillsBar.cpp / SkillsPal.cpp).

## Plan (phases, each shippable)

0. **Combat corrections found in this pass** (combat.md, "Corrections").
1. **Skill data + selection + calcs**: Skills.txt records; a calc compiler
   (Skills.txt text → the same ops) and evaluator over skillcalc.txt's
   operands; the save's left/right skills and hotkeys (F1–F8); the HUD
   buttons and picker; mana cost. Attack (srvdofunc 1) as today's swing.
2. **Weapon skills** (srvstfunc builds the record, srvdofunc[2] resolves it:
   Bash, Jab, Sacrifice, the Assassin kicks): calc1 damage %, calc2
   post-damage add, toht, ResultFlags (knockback, stun), SrcDam,
   `UseAttackRate` timing, all through `rules::player_blow`.
3. **Passives and masteries** (`passivestat*`): Claw Mastery, Weapon Block,
   Critical Strike, Dodge / Avoid / Evade: they feed `Fighter`.
4. **Missile spells** (Fire Bolt, Magic Arrow, Poison Dagger): EMin/EMax by
   level, synergies, cast rate (FCR), the missile system that already flies
   quill spikes.
5. **Auras, summons, charge-ups, sentries**: auras as states on units in
   range, summons as monsters on the player's side (the merc code), the
   Assassin charges (srvprgfunc through srvdofunc), traps.
