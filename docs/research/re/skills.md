# Skills — research notes and plan (1.14d)

Status: the machinery is traced and phase 1 is built (skills.hpp,
skillbar.hpp); the per-skill functions aren't traced yet. d2d already has the skill tree (spending points,
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

### Kicks — Dragon Talon (srvstfunc 24 / srvdofunc 42)
- Start (FUN_005d5970): in reach, the kick count = **calc1**
  (`lvl/6+1`); the first kick lands now, the rest on the following action
  frames (FUN_005d5a30).
- Each kick (FUN_005d5880): to hit with toht + stat 325; the record's
  enhanced damage = `ln12`; the last kick adds flags 0x0c.
- Damage (FUN_005d54b0): the skill's own physical damage (FUN_00647bc0:
  (MinDam + brackets + DmgSymPerCalc %) << HitShift) × (1 + ED %), plus
  the kick base (FUN_00646280): stat 137 + the boots' armor.txt
  mindam/maxdam (the file's first pair; a shield's is its smite damage) ×
  (1 + ED % + boots StrBonus × str / 100 + DexBonus × dex / 100 + stat 25
  (at least −90) + stat 17), rolled between. The weapons' stats are taken
  off the unit meanwhile, and the record is marked built (flags | 3), so
  FUN_0057b7d0 skips the weapon damage and the crit / deadly doubling but
  still adds the rest of the gear's elemental damage and leech.
- The last kick's knockback (FUN_005d5a30): 100 % on normal monsters,
  calc2 / calc3 on special ones, calc4 on players.
- The generic `Kick` skill (FUN_00647bc0's flagged branch): players
  (str + dex − 20) / 4, monsters clvl × 3 / 4.

### Stun and Concentrate (the Bash family's states)
- Bash's start (FUN_005d7ea0) on a hit: the record's flags |= the skill's
  +0x12e / +0x130, hit class +0x134, ED = calc1; if EType (+0x1dc) is set
  and **calc4** > 0, the record's conversion type (+0x65) = EType and % (+0x68)
  = calc4; then FUN_0056e0c0 adds the skill's element: length
  (FUN_00644f20), damage (FUN_00644d50 / 00644e40) into FUN_0056c8e0, the
  record filler by element (1 fire +0x10, 2 lightning +0x1c, 3 magic
  +0x20, 4 cold +0x24 / length +0x30, 5 poison +0x28 / +0x2c, 6..8 life /
  mana / stamina leech, **9 stun +0x44 = damage + length**, 11 burn, 12
  freeze, 10 random of four). After the damage, physical += calc2 × 256;
  then the self state `aurastate` (+0x80), made with no length
  (FUN_006251f0 kind 4), its aurastats set by FUN_005c6cc0.
- Conversion (FUN_0057b7d0, last in the build): pct % of the physical
  (FUN_00483360) leaves it and joins the element (poison: an eighth, 50
  ticks at least; cold: 50 ticks at least).
- Stun (FUN_0057c6c0 → FUN_0057aae0): record +0x44 (or, when 0, gear stat 66
  × SrcDam / 128). Monsters flagged special (FUN_005a0180) shrug it off 90 %
  of the time; a MonStats +0x0c flag (mask 0x6ce280) or MonStats +0x32 = 0
  makes them immune; ids 0x10f / 0x152 / 0x167 / 0x230–0x231 cap it at 13
  frames; others at 250. State 21 (stunned) for that many frames, refreshed
  by a new stun.
- Concentrate: aurastate `concentrate`, aurastat1 skill_armor_percent =
  ln34 (100 + 10 per level); calc4 = Berserk's level, % to magic.

### Other starts resolved by srvdofunc 2 (srvstfunc table 0x732140)
- **6 Power Strike** (FUN_005da940): hit check FUN_0057ec10 with **0** to-hit
  bonus (the column's ToHit / LevToHit don't reach it); on a hit ED =
  calc1, calc4 conversion when EType, the element (lightning, EMin..EMax
  + synergy) via FUN_0056e0c0; no ResultFlags, no calc2 add; SrcDam
  passed as is.
- **39 Berserk** (FUN_005d97f0): Bash's build (toht, ResultFlags, hit
  class, ED calc1, calc4 = 100 → all magic, the element) without calc2's
  add; then, hit or not, the self state `aurastate` for **calc2** ticks
  (10 when that's ≤ 0), refreshed if present: aurastat1 damageresist
  (par5 = 0), aurastat2 armor_override_percent −100 (defense 0).
- **35 Vengeance** (FUN_005cfe10): toht; the physical from FUN_0057b420
  (the weapon's, one additive %), ResultFlags, flags | 1; then fire (+0x10)
  = physical × (calc1 [+ stat 329 fire mastery]) / 100, cold (+0x24) ×
  calc2 [+ 331], lightning (+0x1c) × calc3 [+ 330], cold length (+0x30)
  += ELen; an overlay cycling fire / cold / lightning (FUN_006444a0 /
  FUN_00644560); SrcDam 128.
- **58 Fire Claws** (FUN_005c7e00): the shape-shifted Druid's
  (FUN_0056e680 builds the hit); waits for shapeshifting.

### Zeal, Sacrifice, Smite (srvdofunc table 0x7322b0)
- **Zeal**, start 37 (FUN_005daf40): the unit's skill data
  (FUN_00620250) gets the hit count = **calc1** (`min(par5 + lvl − 1,
  par6)`), the target (or one found by FUN_0056bd10, flags 0x20003).
  Do 13 (FUN_005dbc60, shared with Fend), each action frame: the stored
  target (or a new search), hit check with toht; on a hit ED = **calc2**,
  calc4 conversion, the element; SrcDam; FUN_0057dbf0; count − 1, and
  with hits left a new target from FUN_0056bd10 (handed the last one's id)
  and the next hit (FUN_0056e210). No ResultFlags.
- **Sacrifice**, start 29 (FUN_005ce790): hit check with toht; on a hit
  the physical from FUN_0057b420 with ED = **calc1**, calc4, the element,
  ResultFlags, flags | 1 (the physical is built: FUN_0057b7d0 skips its
  weapon roll but still does crit, gear elements, leech). Do 64
  (FUN_005ce8e0): the hit's physical (record +8, after the hit), capped at
  the target's life (stat 6), × **calc2** (par3 = 8) / 100 as damage to the
  player (record flags 0x1000, FUN_0057c6c0).
- **Smite**, no start, do 150 (FUN_005ce9f0), players: the shield's damage
  (the item record's +0xfe / +0xff: armor.txt mindam / maxdam, << 8), plus
  Holy Shield's own damage (FUN_00647bc0 / 00647d00) while state 0x65
  (holyshield) is on, into FUN_0057b420 on the shield with ED = **calc1**,
  SrcDam 128; stun = **calc2** (`min(250, ln12)`, record +0x44), the
  element; hit check FUN_0057ec10 with no bonus, then flags | 1: **always
  hits** (a block still stops it: FUN_0057dbf0 needs flags & 0x8380 clear);
  record flags 2, so FUN_0057dbf0 **skips FUN_0057b7d0** (no crit, gear
  elements, leech, conversion); hit class 0x65. Monsters' Smite
  (FUN_005a4f50) differs.
- Record flags seen: 1 = the physical is built (FUN_0057b7d0 doesn't roll
  the weapon), 2 = don't run FUN_0057b7d0 at all, 0x1000 = self damage;
  FUN_0057b7d0 marks 0x20.

### Sequences (anim SQ)
- `seqnum` is record byte **+0x13** (the loader's field table at 0x616300:
  monanim +0x11, seqnum +0x13, seqinput after). FUN_00643d00 returns it
  for players (monsters: MonStats' own). FUN_00663310 finds the frames:
  players `[0x7483b8 + seqnum × 4]` (23 sequences) + weapon index × 12;
  monsters FUN_00659e30.
- Weapon index (FUN_006632c0 → FUN_0064f380's weapon class, through the
  pairs at 0x748418): hth, 1ht, 2ht, 1hs, 2hs, bow, xbw, stf, 1js, 1jt,
  1ss, 1st, ht1, ht2. An entry is `{frames, count, count}`; null: the
  skill has no sequence with that weapon.
- A frame is 6 bytes `{u16 0, mode, frame, direction 0, event}`: the player
  shows `frame` of `mode`'s animation; event **1 is a hit** (the skill's
  srvdofunc runs), 3 comes before some of Jab's and Charge's hits.
- Generated into `components/rules/sequences.hpp` by
  `tools/ghidra/gen/sequences.py` (3450 frames, 207 entries). Which skills
  use them: 1 Jab, 2 Sacrifice, 4 Charge, 5 Conviction, 6 Inferno, 8
  Impale, 9 Fend, 10 Whirlwind, 11 Double Swing / Frenzy, 12 Lightning /
  Chain Lightning, 13 Leap, 14 Leap Attack, 15 Double Throw, 16 Fists of
  Fire / Dragon Claw / Claws of Thunder / Blades of Ice, 18 Arctic Blast,
  19 Dragon Talon, 21 Dragon Flight, 22 Werewolf / Werebear, 23 Blade
  Fury. E.g. Jab with a spear: 21 frames, three hits; the claws with two
  claws: A2 hit then S4 hit; Frenzy: A1 hit then S3 hit.
- Per hit: **Jab** do 7 (FUN_005db2d0): toht, ED calc1, the element.
  **Dragon Claw** do 46 (FUN_005d6340 → FUN_005d6200): toht + stat 325,
  the charges' damage (FUN_005d3ac0 / 005d3ba0), ED calc1, the element,
  FUN_0057b7d0, then **release** (FUN_005d5220): each claw hit finishes.
  **Frenzy** do 9 (FUN_005d8e00): the odd (second-hand) frame looks for
  another target (FUN_0056bd10); FUN_005d8c70: when the last hit landed,
  the frenzy state's counter (stat 0xa9) + 1 up to the skill level, for
  auralencalc ticks, its aurastats (velocitypercent dm34, attackrate
  dm56) at that counter; FUN_005d8b10: toht, ResultFlags, ED calc1, the
  element. **Double Swing** do 70 (FUN_005d8470): the odd frame retargets,
  then Bash's build (FUN_005d7ea0).

### Charge-ups (srvstfunc 23 / srvdofunc 34, 35)
- The hit (FUN_005d3490; [35] FUN_005d35d0 is the same after a dual-claw
  check): a plain melee hit at the skill's to-hit. On a hit, FUN_005d3320
  adds a charge: the state `aurastate` (Tiger: progressive_damage, Cobra:
  progressive_steal, Fists / Claws / Blades: progressive_fire / _lightning
  / _cold) for `auralencalc` ticks (par3 = 375, 15 s); stats 0x15e / 0x15f
  hold the skill id and level, `aurastat1` the count (capped at 3),
  `aurastat2` (progressive_tohit) = `aurastatcalc2` (par4 = 50).
- A finishing hit's damage (FUN_005d3ba0 / FUN_005d3ac0, per progressive
  state, level = max(stored, current)), by `prgdam` (+0x44):
  - 1 Tiger Strike (FUN_005d3680): record ED += calc1 × charges.
  - 2 Cobra Strike (FUN_005d3790): FUN_004e6ca0 (ln12, par1 40 + par2 5 per
    level) added to life leech (damage +0x38) at 1 charge, life and mana
    leech (+0x3c) at 2, both doubled at 3.
  - 3 (FUN_005d3880): elemental damage (FUN_0056e0c0), cold freezing with
    2–3 charges; no 1.14d skill uses it.
  - 4 Fists of Fire, Claws of Thunder, Blades of Ice (FUN_005d3970): the
    skill's elemental damage (FUN_0056e0c0); cold with 3 charges adds
    freeze length = cold length / FUN_004e6c70; if calc1 > 0 (Fists of
    Fire: `lvl*3`) part of the physical moves to the element
    (FUN_00483360 / FUN_0056c8e0), not fully read.
- The release (FUN_005d5220), after Attack's srvdofunc and the finishers',
  on a hit (damage flags & 1): for each progressive state, with n = its
  count (1..3), `srvprgfunc1..n-1` for the earlier charges and
  `srvprgfunc{n}` last (indices into srvdofunc: Fists of Fire's fire
  bursts, Claws of Thunder's novas, Blades of Ice's, Royal Strike's meteor
  / chain lightning / ice), the release overlay (FUN_00571aa0), then the
  state is removed.

### Dragon Tail (srvstfunc 27 / srvdofunc 50)
- Start (FUN_005d7090): a kick at toht + stat 325 (FUN_0057ec10); on a
  hit, the kick damage (FUN_005d54b0) with SrcDam, stored.
- Action (FUN_005d7180): release the charges (FUN_005d5220), then fire =
  the kick's physical × (**calc1** + stat 329 fire mastery) / 100
  (`ln12`: 50 + 10 per level), result flags 9, over an area of
  `aurarangecalc` (par3 = 6 subtiles) around the target (FUN_0056bad0).

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

0. ~~Combat corrections~~ — done (combat.md, "Corrections applied").
1. ~~**Skill data + selection + calcs**~~ — done: `skills.hpp` + `test_skills`
   (565 of 566 calcs compile; Bone Wall's `par34` is a data typo),
   `skillbar.hpp` (buttons at game.exe's positions, picker, F1–F8, the
   save's skills), skill levels with item bonuses. Using any skill but
   Attack still swings a plain attack (logged once). Not done here: mana
   is spent by skills from phase 2 on; the picker's layout isn't traced.
   Was: Skills.txt records; a calc compiler
   (Skills.txt text → the same ops) and evaluator over skillcalc.txt's
   operands; the save's left/right skills and hotkeys (F1–F8); the HUD
   buttons and picker; mana cost. Attack (srvdofunc 1) as today's swing.
   Files: `components/rules/skills.hpp` + `tests/test_skills.cpp` (records,
   calc compiler/VM, level brackets, mana cost); `load.hpp` (Skills.txt,
   skillcalc.txt); `apps/d2d/skillbar.hpp` (buttons, picker, hotkeys, from
   the save's +0x38..0x87); `fight.hpp` (casting through `Fight`).
2. ~~**Weapon skills**~~ — started: the Bash family (32 / 2: Bash, Stun,
   Concentrate) and Dragon Talon (24 / 42) are built (`fight.hpp`
   `start_swing` / `swing`, `combat.hpp` `Swing`); mana is paid per swing,
   AttackNoMana skills fall back to a plain attack. The charge-ups (23 /
   34, 35) and Dragon Tail (27 / 50) too: charges on the player
   (`Fight::charges`, `rules::charge_bonus`), released by Attack, Talon or
   Tail; Tail's fire splash. Not in them yet: the srvprgfunc release
   missiles (logged), progressive_tohit, prgdam 4's freeze and calc1
   share, fire mastery on Tail, the SQ animation of Fists / Claws /
   Blades (they swing A1). Stun (stands the monster, `Monster::stun_until`)
   and Concentrate (defense while swinging, calc4 conversion) too; not in
   them: the stun guards (special monsters, immunity, the boss cap), gear
   stun length, when the self state really ends. Power Strike, Berserk
   (all-magic, defense 0 for calc2 ticks) and Vengeance (elements as % of
   the physical) too; not in them: the masteries on Vengeance. Zeal
   (chained hits, the next target the nearest), Sacrifice (the life
   price) and Smite (S1, the shield, sure hit, stun) too; not in them:
   FUN_0056bd10's target pick, Holy Shield's damage on Smite. SQ
   sequences play from the traced table (`sequences.hpp`), a hit on each
   event frame: Jab, Dragon Claw, Frenzy (its speed state), Double Swing,
   and the claws' charge-ups; not in them: the sequence's own rate (taken
   as the class's A1 through attack_ticks), seqinput / seqtrans, a skill
   refused for a weapon with no sequence (it swings once), attackrate
   taken as IAS. Not yet: Impale, Whirlwind, Leap Attack, Charge, Double
   Throw, Fend, the other start
   functions (Fend, Charge, Holy Shield, Leap Attack, ...). Was:
   **Weapon skills** (srvstfunc builds the record, srvdofunc[2] resolves it:
   Bash, Jab, Sacrifice, the Assassin kicks): calc1 damage %, calc2
   post-damage add, toht, ResultFlags (knockback, stun), SrcDam,
   `UseAttackRate` timing, all through `rules::player_blow`. Files: the
   skill's damage % / flat / to-hit into `Fighter` in `combat.hpp`; the
   srvstfunc/srvdofunc dispatch as a table in `fight.hpp`.
3. **Passives and masteries** (`passivestat*`): Claw Mastery, Weapon Block,
   Critical Strike, Dodge / Avoid / Evade: they feed `Fighter`
   (`skills.hpp` computes the stats, `make_fighter` adds them).
4. **Missile spells** (Fire Bolt, Magic Arrow, Poison Dagger): EMin/EMax by
   level, synergies, cast rate (FCR), the missile system that already flies
   quill spikes (`ai.hpp` Missile; friendly missiles like the merc's).
5. **Auras, summons, charge-ups, sentries**: auras as states on units in
   range, summons as monsters on the player's side (the merc code), the
   Assassin charges (srvprgfunc through srvdofunc), traps.
