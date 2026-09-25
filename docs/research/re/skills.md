# Skills — research notes and plan (1.14d)

Status: research only. d2d already has the skill tree (spending points,
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
  - **progressive table @ 0x7325b0**, read by FUN_0056e740. Its size is
    unclear: `srvprgfunc` values go up to 143, but the run of code pointers
    ends about 50 entries in.
- Per-class server code, by source file (the `.\SKILLS\*.cpp` strings, all
  xrefs in `docs/research/re/game-strings.tsv`): SkillAss.cpp from
  0x5d3e80 (FUN_005d3e80, 5d6630, 5d76e0, 5d7b60, 5d7ce0), SkillMonst.cpp
  from 0x5cb940, SkillNec.cpp, SkillSor.cpp, SkilPal.cpp, SkillDruid.cpp,
  SkillAma.cpp, SkillBar.cpp, SkillItem.cpp. D2Common's skills (levels,
  damage, mana) are around `.\SKILLS\Skills.cpp` @ 0x6438b0..0x643ec2 and
  0x56bad0..0x56bee9.
- The `skillcalc` column name (0x6e6460) is referenced from FUN_00612750,
  the likely start of the calc-expression compiler.

## Still to trace before porting

1. The level breakpoints for `*Lev1..5` (commonly 2–8, 9–16, 17–22, 23–28,
   29+). Neither a cmp cluster nor a data table of 8/16/22/28 turned up;
   look in the Skills.cpp cluster.
2. The calc-expression bytecode and its evaluator (the `ln` / `dm` / `par` /
   `skill()` operators).
3. The mana cost formula with `manashift` / `lvlmana` / `minmana`.
4. `srvdofunc[2]` (0x56f1f0), the generic "weapon hit with skill bonuses"
   most melee skills use; and 0x5d3490 / 0x5d35d0 for the Assassin charges.
5. How the client picks left/right skills and the hotkeys (the control
   panel's skill buttons, `leftskill`).

## Plan (phases, each shippable)

1. **Skill data + selection**: load Skills.txt, the right and left skill
   buttons on the HUD, skill choice from the learned ones, hotkeys F1–F8,
   mana cost. Attack (srvdofunc 1) as today's swing.
2. **Weapon skills** (`srvdofunc[2]`-style: Bash, Jab, Sacrifice, Zeal-like):
   the calcs for damage % and attack rating, `UseAttackRate` timing, through
   `rules::player_blow` with the skill's bonuses.
3. **Passives and masteries** (`passivestat*`): Claw Mastery, Weapon Block,
   Critical Strike, Dodge / Avoid / Evade: they feed `Fighter`.
4. **Missile spells** (Fire Bolt, Magic Arrow, Poison Dagger): EMin/EMax by
   level, synergies, cast rate (FCR), the missile system that already flies
   quill spikes.
5. **Auras, summons, charge-ups, sentries**: auras as states on units in
   range, summons as monsters on the player's side (the merc code), the
   Assassin charges (progressive table), traps.
