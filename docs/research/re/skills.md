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
  The weapon-type gate is `FUN_00643f80` (from the usability chain
  `FUN_00647960`, status 2 when it fails): a skill with no itypea1 (+0x18)
  and no etypea1 (+0x24) passes. Else, with h4 / h5 the right / left
  hand items (`FUN_0063bde0(inv, 4 / 5)`; for Left Hand Throw / Swing
  (ids 4 / 5) the active hand's item is dropped, `FUN_0063bef0`), it
  passes when one hand fits set a (itypea1..3 / etypea1..2) and the other
  set b (itypeb / etypeb), either way round:
  `(D_a(h4, h5) && D_b(h5, h4)) || (D_b(h4, h5) && D_a(h5, h4))` (EAX the
  set, 0x644009 / 0x644013). `FUN_00643d90` D(item, other): no item:
  true when the set has no itype and no etype, or, whichever set is asked,
  set a's itype1 (+0x18) is weap 0x2d / mele 0x2e / 0x43, set b's itype1
  (+0x1e) is empty and the other hand isn't a weapon (`FUN_00629bb0(..,
  0x2d)`, 0x643dc2..0x643df9; so Double Swing, mele / mele, refuses bare
  hands); an item of an etype: false; no itype: true;
  an item of none of the itypes: false; of one: true, except the ammo
  tests (a bow / crossbow needs its quiver in the other hand with stat 70
  above 0, unless stat 0x9d; flags 0x4000 / 0x100 items only for skill 0).
  So Holy Shield and Smite (itypea1 shld, no set b) need a shield in
  either hand: the other hand passes set b's empty test.
  d2d: `weapon_gate` / `hand_fits` (fight.hpp, through `skill_refused`;
  `test_weapon_gate`, Left Hand Throw / Swing's dropped hand too); not
  kept: the ammo tests, the 0x4000 / 0x100 items; Throw keeps its own gate.
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
  that (MulDiv, FUN_00483360), plus the elemental mastery (stats
  0x149..0x14c, FUN_00644c90) when asked. The min adds its synergy only
  when it's over one point (0x100) or the row has an EMinLev1 (0x644db3);
  the max always: Lightning's 1-x stays 1 (emu-checked: `skill_damage.py`).
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

The function table (0x745774, 7 entries with arity): 0x6436e0 `min(a, b)`
and 0x6436f0 `max(a, b)` (decompiled 2026-09-29), 0x643700 `rand(a, b)`,
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
text itself (see below). 1.14d also ships the compiled .bin.
d2d compiles the Skills.txt text to the same ops with the same
operand and function lists.

### The calc-text compiler (2026-09-29)

Entry, per-calc-string field on the Skills.txt loader (`FUN_00613f80`):

```
FUN_00611bd0(text in ECX, record in EDX; stack: offset, size, row, column)
  buf[1024]; len = FUN_006c1ae0(text, buf, 1024, LAB_00611930, LAB_006119e0, FUN_006119f0)
  if (len > 0) *((u32*)(record + offset)) = FUN_006118b0(&0x96bc70 buffer, &0x96bc74 size, &0x96bc78 capacity, buf, len)
  else                                           ... = 0xffffffff
```

`FUN_006118b0` copies the bytecode into a shared growing buffer (pointer `DAT_0096bc70`, size `0x96bc74`, capacity `0x96bc78`) and returns its offset; that's what a Skills.txt record's `calc1..4` field ends up holding. The mirror wrapper `FUN_00611c70` has the same shape and the same three callbacks (not a misscalc lookup); it fills the skill description buffer (`0x96bc7c` / `0x96bc80` / `0x96bc84`). Both run in `tools/emu/datatbls_finish.py` (datatbls.md "Fourteen more functions").

**Compiler**: `FUN_006c1ae0(text, out_buf, buf_size, name→fn_tag_cb, fn_tag→arity_cb, ident→operand_cb)`. It's a **shunting-yard** with an operator stack of 64 slots (`local_151[]` for token classes, `auStack_114[]` for values). The tokenizer is `FUN_006c11c0`; the byte emitter is `FUN_006c18a0` (bounds-check + write); the arity-at-pop lookup is `FUN_006c1820`.

Token class → emitted VM op (matches the VM table at line 100+):

| token | meaning | emits |
|---|---|---|
| 0 | operand (identifier / number) | 4 / 5 / 6 (via the operand callback, then push mark `0x02` on op stack) |
| 1 | close paren / end sub-expr | pop until matching `0x02` mark (or `0x01` call marker) |
| 3 | `+` | 0x10 |
| 4 | `-` | 0x11 (binary) or 0x15 (unary, when no operand precedes) |
| 5 | `*` | 0x12 |
| 6 | `/` | 0x13 |
| 7 | `?` (top-level ternary hook) | 0x16 |
| 8 | (paired with 7 at pop) | 0x17 |
| 9 | `^` (power) | 0x14 |
| 10..15 | `<= < > >= == !=` | 0x0c 0x0a 0x0b 0x0d 0x0e 0x0f |
| 16 | `?` (call-site ternary) | routes through `FUN_006c19c0` |
| 17 | `:` | routes through `FUN_006c1a50` |
| 18 | function-call opener | push `0x01` + fn tag onto op stack |

After the token loop, the compiler runs **the VM itself (`FUN_006c0bc0`) with no operand/function callbacks** over the emitted bytecode, then trims and re-terminates: this is **constant folding at compile time**. Purely-literal subexpressions collapse to a single `push const` op.

**Function-name → tag** (`LAB_00611930`, string equality, `FUN_00413590`):

| name | tag | runtime entry (0x745774[]) | arity | shape |
|---|---:|---|---:|---|
| `min` | 0 | 0x6436e0 | 2 | `min(a, b)` |
| `max` | 1 | 0x6436f0 | 2 | `max(a, b)` |
| `rand` | 2 | 0x643700 | 2 | `rand(a, b)` |
| `skill` | 3 | 0x646c00 | 2 | skill(id_via_'Name', field) |
| `miss` | 4 | 0x643740 | 2 | missile field (`FUN_0064b340`) |
| `stat` | 5 | 0x643770 | 2 | `stat(id, base/accr/mod)` |
| `sklvl` | 6 | 0x646c60 | 3 | `skill('Name', 'blvl', ctx)` (arity 3 via `LAB_006119e0`: `(tag==6) ? 3 : 2`) |

This **resolves the "which of 0x6436e0 / 0x6436f0 is min vs max" question**: table[0] is `min`, table[1] is `max`.

**Identifier → operand** (`FUN_006119f0` for skillcalc, `FUN_00661880` for misscalc): packs the first four chars of the identifier as a little-endian `uint32` (padded with `' '` if shorter) and looks it up in `FUN_006bd130(skillcalc[], key, 1)`. Identifiers longer than 4 chars are effectively truncated at 4 for lookup — which matches how skillcalc.txt keys `ln12`, `par1`, `edmn` etc. all fit in 4 bytes. `*param_2` returns 1 for "found in the special keyword tables" (`skills`, `events`, `states`, `hitclass`, `colors`), else 0 for "operand".

Operand-emit is via `FUN_006c19c0` / `FUN_006c1a50`: small values emit op 4 (1-byte operand id), larger ones 5 (2-byte) or 6 (4-byte). The operand id is the skillcalc.txt row number (matches d2d's compiler and the runtime table already documented in **The calc language** above).

**Traced in full (2026-10-02)**, from FUN_006c11c0 / FUN_006c18a0 /
FUN_006119f0 and checked black-box: tools/emu can run the compiler itself
(set `DAT_0096c8b4` = 1 before `FUN_00619300` so the .txt link tables are
built, then call `FUN_006c1ae0(text, buf, 0x400, 0x611930, 0x6119e0,
0x6119f0)`). `components/rules/skills.hpp` `compile_calc` is a port, and
its postfix matches game.exe's on all 1650 calc cells of Skills.txt and
SkillDesc.txt and 4000 random strings, except the two `miss()` tooltip
lines (missile names aren't passed in). What it does:

- **Tokens**: whitespace and `"` are skipped anywhere. Digits: a constant
  (32-bit wrap). A name (alnum run) followed, after spaces, by `(` and
  one of `min max rand skill miss stat sklvl` (any case) opens that
  function and eats the `(`. Any other name, a `'quoted name'` and a
  `.name` go through `FUN_006119f0`, keyed on the function whose `(` is
  on top of the operator stack: inside `skill(` / `sklvl(` a skill name
  (any case) gives its id, inside `stat(` an ItemStatCost name gives its
  id, else `base` 1, `mod` 2, anything else 0 (as an *operand* id);
  otherwise the first **four** characters, space padded, are looked up
  in skillcalc.txt (case matters). A plain or quoted name that's a
  table id is a constant, an operand is an operand, unknown is constant
  0. A `.name` is always a constant, and an unknown one ends the text.
  `=` or `!` without `=`, and any other character, end the text.
- **Bone Wall's `par34`** therefore reads `par3` (`04 0a 00`): calc2 is
  Param3 = 8 at every level (bugs.md #2).
- **Precedence** (`FUN_006c18a0`): pop while the in-stack precedence
  (0x6fc874, by op byte: comparisons 15, `+ -` 17, `* /` 19, `^` 20,
  unary minus 21, `?` 22) is at least the incoming op's own byte (0x0a..
  0x16). So comparisons are one left-associative level, `?` is only
  popped by a later operator: `lvl<4?1:2` is `lvl < (4 ? 1 : 2)`, and
  `a?b:c+d` is `(a?b:c)+d`. `:` does nothing; `,` pops down to the
  nearest `(`. `-` is unary unless an operand or a function's `(` came
  last; `,` `?` `(` `)` leave that flag alone, so `min(lvl,-1)` and
  `max(-1,2)` fail.
- **Power** (`^`, VM 0x14): 1 when the exponent is below 1, else repeated
  multiplies.
- **The end**: pops down to a function left open (its call is dropped).
  A `(` left open is written as its mark byte `02`, where the VM stops,
  so `1+(2` is 2, but it still counts as a value for the ops under it.
- **Failure** (0xffffffff, no calc): an op with too few values on the
  stack, `)` with no `(`, nothing at all, 64 pending ops or 1024 bytes.
- **Folding**: when no operand and no function token was seen, the VM
  runs once and its result replaces the code. d2d skips it (same value).

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
  of the time (the attacker's seed); MonStats boss (+0x0c & 0x40: mask
  0x6ce280 of the bit table at 0x6ce268) or Velocity (+0x32, the loader's
  field at 0x651614) 0 makes them immune; the hirelings (FUN_0063ee90: ids
  0x10f RogueHireling, 0x152 Guard, 0x167 Iron Wolf, 0x230 / 0x231 the act 5
  ones) cap it at 13 frames; others at 250. State 21 (stunned) for that many
  frames, refreshed by a new stun. Special is FUN_005a0180's EDX 8: monster
  data +0x16 & 8, set by FUN_005a0320 on every unique, champion and
  superunique placed (FUN_005a09e0, FUN_005a48c0, FUN_005a4940), not on
  minions. d2d: `Fight::land`.
  Read again whole (cited, decompile only; stack `length`, the struck unit
  in EDI, the attacker as the first argument): the order is **length < 1
  returns; for a monster target: the special test and its 90 % draw first,
  then the boss test, then Velocity (+0x32) 0, then the hireling cap**. So a
  special monster that is also immune (a boss-column unique, say a
  superunique) still takes a step of the attacker's seed (`FUN_005a0180`
  true, seed +0x20 / +0x24 stepped, `% 100 < 90` returns) before the boss
  test returns. The length is capped at 250 for everything but a hireling
  (`FUN_0063ee90`), whose length over 12 becomes 13; a non-monster target
  has only the 250 cap and no immunity test. The end frame is the game's
  frame (+0xa8) plus the length; with state 0x15 already on, its stat list is
  moved to the new end (`FUN_00625310`: replaced, not kept at the longer),
  event 0xc is set for it (`FUN_005417d0`); with none, a list is made
  (`FUN_006251f0`, kind 2, owner the attacker), `FUN_0056e900` is its end
  callback and state 0x15 goes on. d2d (`strike.cpp:511`): reads the same,
  cited from the decompile, no oracle run, on the
  special 90 %, the boss and Velocity tests, the 250 cap and the
  replacement. DISAGREES in the order: d2d tests boss / Velocity first and
  draws only for a stunnable special, so an immune special costs game.exe a
  seed step and d2d none (and d2d's `rng(100)` is not shown to be the
  attacker's unit seed). Re-check: `python3 tools/ghidra/fn.py 0057aae0
  005a0180 0063e9f0 0063ee90`.
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
  elements, leech, conversion); hit class 0x65. A monster's Smite:
  FUN_005a4f50 mode 5 (A2's damage, elements and to-hit), stun calc2,
  FUN_0057ec10's hit roll (not sure to hit); on a hit (result & 1) the
  skill's ResultFlags (+0x12e, Smite's 8: knockback) into the result and
  HitFlags (+0x130, none) into the flags; hit class 0x65; FUN_0057dbf0
  (EDX 0x80), FUN_0057d4f0.
- **Knockback** (result flag 8, FUN_0057cee0): on a player mode 0x13 (KB,
  FUN_00580a70 -> FUN_0057f190 -> FUN_0057f090): path type 8, 5 subtiles
  on from the attacker (FUN_00648cf0 / FUN_00648e40); no path, NU
  (FUN_0057f020). On a monster with no KB mode (MonStats2 bit 13) it's
  turned into a get-hit (flag 4); else mode 13 (FUN_005a7e60 /
  FUN_005a7c20; start FUN_005a77d0: GH and KB modes, alive): path type 8,
  5 subtiles, 10 for class 0x4e (sandleaper1); each frame along the path
  (FUN_005a8630 -> FUN_00554ca0) till the mode ends, then FUN_005a8520:
  GH (mode 3), a Sand Leaper instead thinking 15 frames on (45 stunned).
  The player's KB is drawn as GH (PlrMode.txt). d2d: `Fight::land` and
  `monster_update`'s KB (a monster), `Fight::knock` (the player), an even
  slide over the mode (path type 8 adds no pace, 0x6eb648[8] = 0).
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
  srvdofunc runs), 3 comes before some of Jab's and Charge's hits: Jab's
  client do (cltdofunc 16) plays its dosound there (unit-sounds.md).
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

### Target search, Fend, Impale, Holy Shield
- **FUN_0056b7e0** walks the units in the rooms around the caster within
  a radius (squared subtiles, FUN_006492a0) that pass a filter
  (FUN_0056b3e0, flags | 0xa783), calling a callback. The melee skills
  pass 0x20003 and FUN_0056e510's radius: the weapon's range + 4
  (FUN_00622870: the hand's weapon, weapons.txt rangeadder at item record
  +0x104, checked for all 306 weapons in tools/emu); 0x20000 is in melee
  (FUN_00622c40: FUN_00641530 within the range + 1 and a clear line, mask
  0x804; under 1 always; classes 0x102 / 0x105 within the range + 8),
  0x200 the bare line from the caster (FUN_0064e260, mask 4), 0x8000
  hostile (FUN_00554200), 0x100 / 0x2000 out of town. Chain Lightning
  (hit 12, FUN_005aa730) passes 0x88583 round the missile's spot, its
  radius sHitPar1 (else the skill's), handed the struck unit's id; when the
  pick is that unit it doesn't hop. d2d: `Fight::in_reach`,
  `rules::ring_pick`. **FUN_0056bc80** counts
  them (callback 0x56bc70). **FUN_0056bd10** picks one (callback 0x56bcd0),
  handed the last target's unit id: the unit with the **next higher id**,
  else the **lowest id** (round the ring). So Zeal's and Fend's hits (do
  13) and Frenzy's / Double Swing's second hand go round the enemies in
  reach in id order.
- **Fend**, start 9 (FUN_005dae30): the hit count = min(enemies in reach
  (FUN_0056bc80), **calc1** = 12), the first target the clicked one (or the
  search's); then do 13 as Zeal: ED calc2 (ln34).
- **Impale**, start 7 (FUN_005dab40; sequence 8, one hit): toht; the
  physical (FUN_0057b420) with ED calc1, the element, flags 1; on a hit
  FUN_005daa40: **calc2** % (par6 − dm34) of the time the weapon loses
  **calc3** durability (stat 72; at 0 FUN_0055f850 breaks it), or, for a
  throwing weapon (FUN_006289f0), one of its quantity (stat 70,
  FUN_0056c3f0).
- **Holy Shield**, start 36, do 18 (FUN_005c9480; anim SC): the holyshield
  state (0x65) for auralencalc (ln12) ticks, its aurastats (toblock =
  dm56) via FUN_005c6cc0, FUN_005c6dc0's part, the skill and level in
  stats 0x15e / 0x15f, its aura events (+0x84, up to 3, FUN_0056e740).
  Smite adds its MinDam..MaxDam while it's up. FUN_005c6dc0 adds
  passivestat1..5 (+0x98) at passivecalc1..5 (+0xa4, FUN_00646ca0) to the
  state's stats (stat 0x44 velocitypercent also as 0x45); in 1.14d Holy
  Shield's row has no passivestat and no auraevent, so neither adds
  anything (its aurastatcalc2 has no aurastat2 either).

### Skills that move: Whirlwind, Leap Attack, Charge
- **Whirlwind**, start 38 (FUN_005d8f50): a path to the clicked point
  (FUN_0064ea90) at velocity FUN_0056e5b0 = the class's **walk velocity**
  (CharStats +0x40 << 8; monsters MonStats +0x32), sequence mode (0x12),
  the whirlwind state. Do 76 (FUN_005d9580) on each event frame: if the
  hit timer allows (FUN_005d9320: players every 4 / 6 / 8 / 10 / 12 / 14 /
  16 frames as FUN_0062a710's attack frames are < 12 / 15 / 18 / 20 / 23 /
  ≤ 25 / more; 10 without a weapon; monsters every other event), one hit,
  **two with two weapons** (FUN_005d92d0), each on FUN_0056bd10's next
  target within **5** (round by id): toht, ResultFlags, ED calc1, the
  element. It ends at the point.
- **Leap Attack**, start 41 (FUN_005da540): the leap's path to the target
  (FUN_00645ca0 finds the landing spot), flags 0x1080. Do 78 (FUN_005da7e0)
  per event, by flags: take off (0x100: FUN_005da120 / 005da490), land,
  strike (0x200: FUN_005da660: toht, **knockback** (flags | 8), flags 0x20,
  ED calc1 (ln34 + Leap's level × par8), the element; a stun on the target
  ends). Sequence 14: S1 frames (events at takeoff and landing), then A1
  (the strike).
- **Charge**, start 31 (FUN_005cf6b0): velocity = **run velocity**
  (CharStats +0x41 << 8) × (max(velocitypercent (67), 50) + **par1** (150))
  / 100; players at a distance only (in reach it's a plain attack,
  FUN_0056e230). Do 67 (FUN_005cf900) on each event (every run frame of
  sequence 4 is one): the target in reach jumps the sequence to its attack
  frames (FUN_00553dc0); the attack's event hits (toht, **knockback**,
  ED calc1, the element, hit class 0x70); run frames out, the run starts
  over.

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

### Dragon Flight (srvdofunc 52)
- Do (FUN_005d7850) on each action event, by the event count (unit
  +0x38 >> 8, bit 0): the first teleports to the skill's spot
  (FUN_0056d2c0, FUN_00554ea0; not into collision 0x804 where the room
  asks); the next kicks the unit's target (FUN_00553540): to hit with toht
  + stat 325 (FUN_0057ec10), on a hit the kick damage (FUN_005d54b0) with
  ED `ln12` (FUN_004e6ca0), HitClass +0x1a5 (0x80 when 0); then the
  charges are released (FUN_005d5220), hit or not. No knockback.
- d2d: both on the one action frame (`Fight::spot`, and `shadow_cast` for
  a Shadow beside its target).

### Choosing skills
- The save holds them: header +0x38 sixteen hotkeys (u32 skill id,
  0xffff = none, 0x8000 = assigned to the left button), +0x78 left skill,
  +0x7c right skill, +0x80 / +0x84 the weapon-swap pair. Erza: left Dragon
  Talon, right Death Sentry, swap Attack / Shadow Master.
- The client's skill buttons are drawn by SkillsBar.cpp (FUN_004c83c0 ..
  LAB_004c9280), the skill picker by SkillsPal.cpp (FUN_004c9b40); icons
  are `Spells\<Cl>Skillicon` (already loaded for the tree).

### Can the skill go now (FUN_00647960, traced 2026-11-07)
`FUN_00647960(unit, entry)` (stdcall, SKILLS/Skills.cpp) answers 0 or the
first reason it can't, in this order (the Skills.txt flag bits are the
loader's, `FUN_00613f80`: dword +4 bit n, n from 0x20 on in dword +8):
1. no row (entry +0), or the row's InGame flag clear (bit 0xa, +5 & 4): 3.
2. level 0 (`FUN_006442a0(unit, entry, 1)`): 7.
3. aura (bit 5, +4 & 0x20, `0x6ce27c`): 6. passive (bit 4, +4 & 0x10): 5.
4. `FUN_00647640` (scrolls and tomes: the entry's count +0x30 above 0 with
   an inventory; Throw, Left Hand Throw / Swing: their hands; else 1),
   then the weapon-type gate `FUN_00643f80` for every skill: 2.
5. mana (`FUN_00647540`): 1. An item's entry (+0x34 not -1) asks only for
   its charges (+0x38 above 0); blood_mana (state 0x72) asks life (stat 6)
   instead of mana (stat 8); srvdofunc 116 passes when `FUN_0063a400`.
6. restrict (`FUN_00644060`, +0x228): 0 refuses a unit in a state with
   States.txt restrict (wolf, bear, delerium; `FUN_0063a510`), 2 asks
   one of State1..3 (+0x22a), 1 and others pass: 4.
7. startmana (`FUN_006440f0`, a player only): 1 when +0x184 (startmana,
   not minmana +0x186) is above 0, mana is below startmana x 256 and the
   unit isn't in state inferno (0xc). Only Inferno (6), Arctic Blast and
   Blade Fury have one: Inferno needs 6 mana to start, its 36 << 2 price
   alone 0.56 (cases inferno_start_5 / _6 / inferno_burning_5).
8. an item's entry (+0x34 not -1) whose item is in the inventory with no
   charges left, its stat 0xcc (layer skill << 6 | level) 0 in the low byte
   (`FUN_00647840`): 2. An item not found passes (cases item_held_charges /
   item_held_no_charges, item_charges).
9. a player in state skilldelay (0x79) with the skill's delay calc
   (+0x190, `FUN_00646ca0`) above 0 (`FUN_006478f0`): 8; else 0.

So an aura or passive at level 0 is 7, a passive with no weapon 5, a
skill with no weapon and no mana 2. Oracle: `tools/emu/skill_status.py`
(31 cases, the real rows and helpers; `ok`).

The client asks before it sends a skill (`FUN_004610c0`: EDI the unit,
EBX the entry's slot in the input record +0x1c; `FUN_004d9fc0` wraps the
status). On status 1, 2 or 4 a skill with AttackNoMana (bit 0x1c, +7 &
0x10) has Attack's entry (`FUN_006439f0(unit, 0)`) put in the slot and is
asked again (LAB_00461101). Then 0 or 5 goes; anything else sends nothing
and voices 0x711ddc[status] (`FUN_00461070`): 1 needmana (0x15), 2..4
impossible (0x13), 6..8 none. So Jab with no spear, Smite with no shield
or Bash out of mana swings a plain attack, with no voice. Oracle:
`tools/emu/skill_status.py client` (18 cases; `ok`).

Every click path that sends a skill reaches it (traced 2026-11-08). A click
goes to `FUN_004625b0`, which first picks the target through `FUN_004621d0`
-> `FUN_00467880`. That one only asks whether the status is 0, to let the
skill's own search (Skills.txt +6 bits 0x40 / 0x20 / 0x80) pick a target;
it never refuses. Then `FUN_004625b0` asks `FUN_004610c0` itself (shift
held, a spot skill, or a cast at the point: three calls), or a unit in
reach asks through `FUN_00461c70`. A unit out of reach is walked to, and
the walk's end asks again: `FUN_00461250` commands 0x15 / 0x16 ->
`FUN_00461180` -> `FUN_004610c0`, Attack's entry kept in the slot when it
fell back. d2d: `Fight::skill_status` / `Fight::ask`, asked at the click
for a spot or self cast (world/commands.cpp) and in `start_swing` for a
unit; `tests/game/test_skill_ready.cpp`. Not kept: the fallback's Attack
at a spot (`FUN_00461700(0)`), and a joined game asks only the weapon (the
host asks the rest).

Skill delay. At the end of a skill's do (`FUN_0056f7f0`, from
LAB_0056fa93) the skill's delay calc (+0x190, Skills.txt `delay`, in
frames) above 0 puts a player in state skilldelay (0x79) until the game
frame (+0xa8) plus the delay (`FUN_0056ef90`: the state made or kept,
its end event 0xc through `FUN_005417d0`); an SQ skill (anim +0x10 0x12)
only when unit +0x38 & 0xffffff00 is 0. Shapeshift sets it through
`FUN_0056f020`. While it's on, every skill with a delay above 0 is status
8 (`FUN_006478f0`), the others are not: Meteor (30 frames) blocks Meteor,
Blizzard (45), Fire Wall (35), Hydra (40), Frozen Orb (25), Fist of the
Heavens (25), Valkyrie (150), Poison (15) / Plague Javelin (100),
Immolation Arrow (25), but not Fire Bolt. The server's skill packet asks
too (the call at 0x549b0a in `FUN_00549ad0`). The client keeps its own,
for its player only: `FUN_004c6680` sets 0x7a04fc (`FUN_0044ce40`) to its
frame clock 0x7a0498 plus min(delay, 12), and `FUN_004d9fc0` turns a 0
into 8 before then. Status 8 voices nothing; the buttons draw it as not
usable. Cases delay_meteor, delay_meteor_off, delay_fire_bolt,
delay_meteor_no_mana. d2d: `Fight::start_delay` at each do's action
frame, `Hero::skill_delay`; startmana's state inferno is `Hero::burning`
(`FUN_005c8e30`: 20 frames from the start, 6 past each later flame).

### Item charges (traced 2026-10-28)
- Stat 0xcc (204, item_charged_skill): param skill << 6 | level, value
  max << 8 | left.
- Entries: the server's stat hook adds a skill entry per item and skill
  (SKILLS_AddOrRemoveItemSkill, entry +0x34 the item's unit id) while the
  item's stats reach its owner (worn in the active weapon set) and charges
  are left; at 0, or taken off, the entry goes, and a hand that held it
  goes back to Attack (left) or skill 0 (right). `game::charged_skills`.
- Level: the entry's own; +skills don't reach it.
- Cost (FUN_0056bfe0, after the start or do function where mana's paid):
  an entry with an item pays one charge and no mana (FUN_0056bec0: the
  stat on the item, else in its sockets, FUN_0065c820; left - 1, at least
  0; stat packet 0x3e). FUN_005531c0 there is the unit's client, not a
  sound. `Fight::pay`.
- Usable (FUN_00647840): the entry's item still in the inventory with the
  stat nonzero, checked when the cast is paid for: an entry gone between
  the click and the swing (used up, unequipped, the weapons swapped) casts
  nothing (`Fight::cast_level`). A queued attack keeps its own entry
  (`Hero::attack_charge`). The client starts nothing from an entry with 0 charges and
  plays the skill's ItemCastSound for an entry's cast (FUN_004c6140,
  Skills.txt +0x122), as for a scroll.
- Picker (SPELLSEL_DrawSkillGrid): rows 0..4 by SkillDesc ListRow; row 4
  holds only the scrolls and tomes (identify, town portal), and the entries
  join them there; the charges left on the icon and the button
  (SPELLSEL_DrawSkillLevel: x + 4, the icon's bottom - 1, colour 3).
  Hotkeys keep the entry's item. d2d: `SkillBar::Pick`, `UseSkill::item`.
- A skill that can't be used now (FUN_004d9fc0 -> FUN_00647960, not 0):
  the picker still lists it in draw colour 1, 4 for status 6
  (FUN_004aa3a0's filters are the left flag, Skills +4 & 0x6ce278 and
  FUN_006446a0 & 8, not the status); the buttons too (FUN_004a8d30). A
  click picks it (FUN_004a9bd0 refuses only a charged entry at 0). The world click casts only at status 0 (FUN_00467880); the
  client's check (FUN_004610c0) voices 0x711ddc[status] (0, 0x15, 0x13,
  0x13, 0x13): 0x13 the class table's +8, `<class>_impossible_1`
  (amazon 0xb89). After an item change FUN_004c1350 -> FUN_004c1200 puts
  a remembered hand skill back unless its status is 2 or 7. d2d:
  `skill_refused` (the weapon-type gate, Throw's own), `Fight::impossible`;
  "Can the skill go now" has the statuses.
  ponytail: the remembered hands (FUN_004c1200) aren't kept.
- Tooltip: the button's and the picker's (FUN_004a8d50 -> FUN_004ef990:
  unit, skill, level, calc, draw mode, x, y) is the plain skill tooltip at
  the entry's level, "%d" its only number. No charges line: the charges
  show only on the icon.
- Joined game: the hand's select (C->S 0x3c) carries the entry's item as
  the host knows it, -1 for the character's own skill.
- Local co-op: each player's hands and hotkeys keep their entries while
  another player has the screen (`Town::hand_items`).
- Load: the save keeps skill ids only and a loaded game selects the
  character's own entry (SelectSkill(id, -1)), so a saved charged hand
  falls back.
- Repair: the missing charges are priced (FUN_00628d30 with 10000, after
  the npc's rep mults; none on an ethereal item, FUN_0062a8d0) and
  refilled (FUN_0055fe80, from FUN_005761c0 and the cube's flag 0x400).
  Skills.txt cost mult +0x234, cost add +0x238, reqlevel +0x174.

### Passives and masteries (phase 3)
- Record: +0x94 `passivestate`, +0x96 `passiveitype`, +0x98 passivestat1..5
  (shorts), +0xa4 passivecalc1..5.
- **FUN_00646d60**(unit, skill): the skill's level with item bonuses
  (FUN_006442a0); unless the state at +0x80 is on the unit, it (re)fills
  the passivestate's stat list: each passivestat (up to the first empty
  one) = its passivecalc, **on the layer passiveitype** (0 when none); the
  skill and level go in 0x15e / 0x15f (the refresh is skipped while 0x15f
  equals the level). **FUN_00646f20** runs it for every skill that has a
  passivestate.
- Masteries, **FUN_00645830**(unit, weapon, skill data, 0 to-hit / 1
  damage / 2 crit): the **best** value of stat 342 / 343 / 344 over the
  layers whose item type the weapon is (FUN_00629bb0); a thrown weapon
  (FUN_00645720) reads 345 / 346 / 347 instead. Used by the attack rating
  (LAB_0057da4c: joins stat 119), the damage % (LAB_0057b591: joins
  stat 25) and FUN_0057b7d0, where the mastery crit rolls **first**, then
  critical strike (337), then deadly strike (141); any doubles.
- Weapon Block, **FUN_0057dca0**: the best stat 348 over the layers either
  hand's item is (layer 0 any); FUN_0057dd60 rolls it when the player
  stands (walking / running rolls evade, 340, instead) and the weapon class
  is 0xd (HT2, two claws), before dodge (338, swings) / avoid (339,
  missiles).
- Item elemental damage takes its mastery as a % (FUN_0057b7d0 →
  FUN_0057a8e0): fire 48/49 + 329, lightning 50/51 + 330, cold 54/55 +
  331, magic 52/53 + 357, poison 57/58 + 332.
- Built: `rules::passive_stats` (skills.hpp), `Fight::player_fighter`
  (the layer check, best value), `make_fighter` (342..344, 348, the
  elemental masteries), `monster_blow` (Weapon Block), `panel_stats` (Iron
  Skin's 171 % of defense, Natural Resistance). Increased Speed's
  velocitypercent (67) is taken as FRW. Not yet: the pierces (333..336,
  Cold Mastery) and Pierce (328) wait for spells and missiles (phase 4),
  the throw masteries for thrown weapons, Summon Resist for summons,
  Increased Stamina (no stamina yet), the +0x80 state check.

### Shots and ammo
- **The plain attack** (do 1, `FUN_0056f070`): with a bow or crossbow
  (the unit's weapon class 1 / 7, `FUN_0064f460`) it fires the weapon's
  missile (`FUN_00645f00`): magicarrow (Id 27) with stat 157, explodingarrow
  (41) with stat 158, else arrow (0) / bolt (31). It spends ammo unless
  the missile is magicarrow.
- **Throw** (do 3, `FUN_0056f460`): the throwable in hand (ItemTypes
  Throwable, `FUN_0062ba80`, or stat 125) flies as its weapons.txt
  missiletype, spending one; `FUN_0056c600` then adds the skill's to-hit
  (stat 19) and damage % (stat 25).
- **A skill's do** (`FUN_0056f7f0`) spends one when Skills.txt
  `decquant` is set; srvstfunc 4 (`FUN_005da8b0`) refuses the skill
  without ammo (`FUN_0056c4e0`).
- **The ammo** (`FUN_0056c4e0` / `FUN_0056c3f0`, players only): with a
  bow / crossbow (ItemTypes 27 / 35) the ammo or throwable in hand 4,
  else hand 5; with any other weapon, the weapon itself. `FUN_0056c310`
  takes 1 off its quantity (stat 70); at 0 there's none.
- **Out of ammo** (`FUN_00580310`, after a spend and when srvstfunc 4
  refuses): for hand 4, then hand 5, a stackable (`FUN_006289f0`, ItemsTxt
  +0x132), throwable or stat-125 item under 1 goes to `FUN_00580030`: a
  magic or better weapon (type 0x2d, `FUN_0062a0f0`) breaks and stays
  (`FUN_0055f850`); anything else leaves, and the inventory's first of its
  code (`FUN_0063cd80`), not broken (0x100), takes the empty slot through
  `FUN_00562a30`, which refuses one `FUN_0062eaf0` (requirements) won't
  wear. Only that first one is tried. srvstfunc 4 also runs
  `FUN_00580380` (a stackable weapon at 0 durability breaks; not ported).
  d2d: `Fight::out_of_ammo`, `cast_missile`; test_game checks both.

### Missile skills (phase 4)
- **FUN_0056f7f0** (a skill's do): runs srvdofunc (+0x2e), then, when the
  skill has a **srvmissile** (+0x46), launches it — FUN_0056ecb0 (flags
  0x21) or FUN_0056ee90 (0x420), by a +0x04 flag — through
  **FUN_0059fa30** (missile create) from the caster toward FUN_0056d2c0's
  target point; then pays the mana (FUN_0056bfe0) unless srvstfunc did,
  and sets the delay (+400, FUN_0056ef90).
- The missile's damage is built when it's made (FUN_0059fa30 →
  FUN_0059f900 → **FUN_0064b860**): with the row's **Skill** (+0x194) set,
  the skill's physical (FUN_00647bc0 / 00647d00) and elemental damage
  (FUN_00644d50 / 00644e40 with flag 1) and length (FUN_00644f20); the
  weapon's damage only when the skill's SrcDam (+0x1a5, as written) is
  non-zero, × SrcDam / 128, with its mastery, strength / dexterity bonus
  and stat 25 (arrows). Without Skill, the row's own damage columns.
- Elemental damage with flag 1 (FUN_00644c90): + the element's mastery %
  of the damage after the synergy: fire 329, lightning 330, cold (and
  freeze) 331, poison 332; none for magic.
- Missile record: +0x0c pSrvDoFunc (table 0x73c768), **+0x0e pSrvHitFunc
  (table 0x73c840)**, +0x10 pSrvDmgFunc (0x73c960); +0x38.. Param1..5,
  +0x4c.. sHitPar1..3 (FUN_0064b340's operand order).
- Hit function 1 (**FUN_005a9a70**, Fire Ball): radius = sHitPar1, else
  the skill's calc1; FUN_0056bad0 → FUN_0056b7e0 hits every unit whose
  squared subtile distance is within radius². The record is rolled ONCE
  (FUN_005a8c70) and a copy handed to each unit found; each resists its
  own copy. Hit function 13 (Glacial Spike) the same. d2d: `MissileRoll`
  (combat.hpp), filled by the first blow of `Fight::area`. Confidence: high
  (game.exe's own code under the emulator: one record per burst in 400 of 400
  trials for each skill; d2d 400 of 400 equal after this change, 2 of 400 before).
- A standing missile (no velocity, e.g. a Blizzard shard, Size 2, or a Fire
  Wall flame, Size 1) strikes from its own subtile each frame (FUN_005ae1f0
  -> FUN_00641cb0): the footprint of its Size against the unit's, 5 / 13 / 21
  subtiles for a size 1 / 2 / 3 unit by a shard and 1 / 5 / 9 by a flame
  (`standing_struck`, ai.hpp; `missile_touches` as a moving one). Confidence:
  high for the footprints (game.exe's own hit test against d2d, 6 standing
  rows, 3,726 class x offset cases: 0 differ after this change, 476 before).
- Built: `missile_skill` / `cast_missile` / `fire` / `skill_missile_hits`
  in fight.hpp, `rules::missile_damage` (skills.hpp),
  `rules::missile_blow` (combat.hpp). Magic Arrow, Fire Bolt, Ice Bolt,
  Ice Blast, Lightning, Bone Spear, Fire Ball: cast on a monster (left or
  right) or on open ground (right); SC with FCR as a rate bonus, A1 for the
  bow skills; missiles fly as game.exe (missiles.md: mask 0x184, 0x04 stops)
  rather than walk collision; CollideKill 0 flies through (Lightning, Bone
  Spear), Pierce rows fly on at stat 328 %; ToHit rows and weapon-share
  skills roll the attack rating. Pierce stats (333..336, Cold Mastery)
  cut the monster's resistance unless it's immune, to −100 at the least
  (the published rule; not traced). Not yet: srvdofunc missile skills
  (Charged Bolt 17, Teeth 8, Multiple Shot, Guided Arrow, ...), srvstfunc 4
  arrows (Fire / Cold Arrow), the other hit functions (Glacial Spike 13,
  Holy Bolt 7, Frozen Orb), explosions on walls, the delay, FCR
  breakpoints, SQ casting (Lightning casts as SC), the missile's light.
- Part 2: srvstfunc 4 (**FUN_005da8b0**) only checks the ammo
  (FUN_0056c4e0), so the Amazon's arrows and javelins take the same path
  (Fire / Cold / Ice Arrow, Poison Javelin, Lightning Bolt). Do 8
  (**FUN_005db410**, Teeth, Multiple Shot): calc1 srvmissilea missiles
  (srvmissileb for monsters) at points a step apart across the line to
  the target, centred on it — the step is the line's vector turned a
  right angle and halved until its squared length is at most 3 subtiles
  (FUN_0056d3b0; FUN_0056d370 scales short vectors up first); calc3 of
  them in the middle, the rest flagged 0x10000. Do 17 (**FUN_005c9300**,
  Charged Bolt): calc1 missiles at the target, each with FUN_005c9290
  steering it. Do 22 (**FUN_005c9b50** → FUN_0056d400, the novas): 64
  missiles toward the offsets in 0x6e1288 / 0x6e1388, lasting
  FUN_00663270 (the row's range) + calc1. A row with no Skill (Multiple
  Shot's) carries the weapon's damage at its SrcDamage (96).
  Built as `skill_missile` / `fire` (fight.hpp); a nova's missiles share
  their struck list, so a monster takes one hit from it. Not yet: the
  0x10000 flag's meaning (Charged Bolt's wander: missiles.md), the
  other do functions
  (Guided Arrow 10, Strafe 12, Chain Lightning 26, Meteor / Blizzard 28,
  Fire Wall, Blaze, Inferno, Bone Spirit, Blessed Hammer, ...).

### Auras (phase 5, part 1)
- An aura skill's do runs on its pulse: **65** (FUN_005cf010, friendly:
  Might, Prayer, the resists, Thorns, Defiance, Blessed Aim, Cleansing,
  Concentration, Vigor, Meditation, Fanaticism, Salvation) puts aurastate
  (+0x80) with aurastats (+0x54 ids, +0x68 calcs) on the caster
  (FUN_0056b740 → FUN_005cedc0) and, with auratargetstate (+0x82), on
  every ally within aurarangecalc (+0x64) subtiles by aurafilter (+0x50)
  (FUN_0056b7e0). **66** (FUN_005cf3a0: Holy Fire, Holy Shock, Sanctuary,
  Conviction) and **81** (FUN_005d0920, Holy Freeze): the caster's
  aurastate gets the skill's **passive** stats (+0x98 / +0xa4) — Holy
  Fire's weapon fire — and every enemy in range gets the target state
  with the aurastats (Conviction's resistances and defense) and a hit of
  the skill's element (FUN_0056e0c0 with the mastery; flags 0xd,
  HitClass +0x134, ResultFlags / HitFlags). **82** Redemption.
- FUN_00646d60 holds an aura's passive state off while the aura's state
  (+0x80) is on, so the passive stats hold either way.
- Operands 49..53 (enma, exma, edma, enms, exms) are the elemental damage
  / length with the mastery flag (FUN_00646460 cases 0x31..0x35).
- Built: `Fight::aura` (the right skill when it's an aura), its
  aurastats in `update_fighters` (resists added over the panel, to 95),
  `aura_pulse` (Prayer's hitpoints heal; 66 / 81 strike monsters in
  range), `target_of` (the target state's resistances / defense while in
  range), thorns_percent (131) on melee hits. Not yet: perdelay's reader
  (taken as ticks), aurafilter's unit-flag, town and sight bits (below),
  the merc in the aura, Sanctuary / Redemption's callbacks, Holy Freeze's
  slow (its cold chills), mana (FUN_00644b10), the aura overlays.
- **aurafilter** (+0x50): dos 65, 66 and 81 pass it to FUN_0056b7e0
  (SkilPal.cpp lines 0x1f6, 0x2ba, 0x6ba), which runs the callback only on
  a unit within range that FUN_0056b3e0 takes (a filter of 0 is 0x583).
  The bits on a monster, as read from the decompile (no oracle):
  - 0x2: monsters at all;
  - 0x1000: set, a monster in mode 0xc (dead); clear, one in neither 0xc
    nor 0;
  - 0x4: undead (FUN_0063e990: MonStats +0xd & 8 or & 0x10);
  - 0x4000: no MonStats boss (FUN_0063e9f0, +0xc & 0x40);
  - 0x40000: no prime evil (FUN_0063edc0, +0xc & 0x80);
  - 0x8000: an enemy (FUN_00554200: FUN_00650d70 is 0);
  - 0x10000: an ally (FUN_00554de0: a monster stands for its owner,
    FUN_0058f0d0, then FUN_00650d70).

  For a monster, 0x8000 and 0x10000 take FUN_00650d70's opposite answers,
  so one an enemy aura strikes fails a friendly one's 0x10000. The 1.14d
  rows:
  - Might, Prayer, the resists, Thorns, Defiance, Blessed Aim, Cleansing,
    Concentration, Vigor, Fanaticism, Salvation 0x12003, Meditation 0x12001
    (no monster bit), the totems 0x10103: allies only;
  - Holy Fire, Holy Shock 0xa783 and Conviction 0xa583: living enemies;
  - Sanctuary 0xe786: undead that aren't bosses;
  - Holy Freeze 0x4a283: no prime evil.

  d2d: `rules::aura_filter_takes` (`Fight::aura_takes`) in `enemy_pulse`
  and `in_aura`, so a friendly aura's auratargetstate isn't drawn on a
  monster. Not read: 0x80 / 0x400 (unit flags +0xc4 & 4 / & 8), 0x100 and
  FUN_0056b7e0's 0x2000 (town rooms), 0x200 (line of sight,
  FUN_0064e260), 0x20000 (FUN_00622c40), 0x80000 (state 0x56).
  Re-check: `python3 tools/ghidra/fn.py 0056b3e0` (and 0056b7e0, 005cf010,
  005cf3a0, 005d0920); the rows from Skills.txt's aurafilter column.
- **FUN_0056b3e0 whole: the other unit types and the order** (cited,
  decompile only; ECX the caster, EDX the unit tested, stack `flags`). A
  null target fails. By the target's type, in this order:
  - type 0 (player): needs 0x1; with 0x1000 only mode 0x11 (dead) passes,
    else mode 0x11 and mode 0 fail;
  - type 1 (monster): as above (0x2, then 0x1000 / mode 0xc and 0, then
    0x4, 0x4000, 0x40000);
  - type 2 (object): needs 0x10, nothing more; type 4 (item): needs 0x20;
  - type 3 (missile): needs 0x8 and its Missiles.txt row
    (`FUN_0046ace0`, id in ECX, stride 0x1a4 from `[0x744304]+0xb64`) must
    exist with row byte +4 bit 1 (mask 2) **clear**; which column that is is
    not read;
  - any other type fails.
  Then, for each of types 0 to 4 that got past its own branch (they all fall into the same tail): 0x80 passes only a target with unit flags +0xc4
  bit 2, 0x400 only one with bit 3; 0x100 fails a target whose room's area
  is a town (`FUN_0061ab00` of `FUN_00620bb0`, the same test `FUN_0057c6c0`
  uses); 0x10000 (ally, `FUN_00554de0`) and 0x8000 (enemy, `FUN_00554200`)
  and 0x20000 (`FUN_00622c40(caster, target, 0)`) each first replace a
  **missile caster** (type 3) by its owner (`FUN_00552fd0`; no owner
  fails), and 0x10000 also fails a monster whose MonStats +0xd bit 0 is
  set; 0x80000 fails a target in state 0x56; 0x200 passes only when both
  units have a room and `FUN_0064e260` over their two positions
  (`FUN_0045adf0` / `FUN_0045ae20`, mask 4) returns 0 (a missing room
  fails); no 0x200 → pass. d2d's
  `aura_filter_takes` agrees on the monster bits it lists, with these
  differences: it has no player / object / item / missile branches (an aura
  only ever strikes a `Monster` here), no mode-0 exclusion for a monster
  (`Fight::aura_takes` passes `dead = !alive()`; game.exe tells mode 0 and
  0xc apart and passes only 0xc when 0x1000 is set), and 0x10000 does not
  fail the MonStats +0xd bit 0 monsters. Those two are the DISAGREES (not
  played out: no aura in the 1.14d rows above sets 0x1000, and which
  monsters carry +0xd bit 0 is not read here).
- A monster's curse or enemy-aura lowering of a resistance whose base is
  100 or more counts a fifth, truncated toward 0 (FUN_005c3540, from the
  callbacks of do 30, 65 and 66): `apply_target_stats` takes the base.

### Charge-up releases (phase 5, part 2)
- **FUN_005d5220** (a finisher's hit): per charge state held, n = its
  count (1..3), level = max(the state's 0x15f, the skill's now); runs
  srvdofunc[srvprgfunc n] — with **prgstack** (Fists of Fire, Claws of
  Thunder, Blades of Ice) srvprgfunc 1..n−1 first, the count set to each
  in turn; then the state goes.
- The count picks the calc (**FUN_005d3da0**: prgcalc n, +0x34 + n×4;
  prgcalc1 without a charge state) and the missile (**FUN_005d3cf0**: 1
  srvmissilea, 2 srvmissileb, 3 srvmissilec).
- **38** (FUN_005d3e80): the skill's physical (FUN_0056e170) and element
  (FUN_0056e0c0) on everything within prgcalc n subtiles of the target
  (FUN_0056b7e0, aurafilter). **36** (FUN_005d4db0): a nova of the
  count's missile round the target (FUN_0056d400), range FUN_00663270 +
  calc1. **39** (FUN_005d3f90): prgcalc n² tries at random points within
  prgcalc n subtiles of the target, a missile at each that lands on the
  map. **37** (FUN_005d4e70 → FUN_005d4150), **143** (FUN_005d4f40 →
  FUN_005d4870), **40** (FUN_005d5010 → FUN_0056ede0), **41**
  (FUN_005d5080: random offsets ±20) aren't built.
- A missile row with no Skill carries its own element by level
  (FUN_0064b100 / 0064b1d0 / 0064b2a0: EMin + MinELev brackets <<
  HitShift, ELen + ELevLen brackets): `rules::row_damage`.
- Built: `Fight::release` / `prg`; devctl `debug charges`, `debug
  release`.

### Summons (phase 5, part 3)
- Record: +0xbc `summon` (MonStats row), +0xbe `pettype`, +0xc0 `petmax`
  (calc), +0xc4 sumskill1..5 with +0xd0 sumsk*calc, +0xe4 `sumumod`,
  +0xe6 `sumoverlay`. Do functions: 56 (FUN_005c5100, the golems), 57
  (Iron Golem), 31 (FUN_005c4b00, Raise Skeleton / Skeletal Mage, from a
  corpse), 16 (FUN_005dc1e0, Valkyrie), 119 (FUN_005c7390, the Druid's
  wolves, bear, spirits), 114 (Raven), 15 (Decoy), 44 / 45 (the
  Assassin's traps), 49 (Shadow Warrior / Master).
- **FUN_0056d940** spawns the row at the target (FUN_005b2f20, flags
  0x42, radius -1: the target's subtile alone, three draws of the room's
  seed, else none; the push at 0x56d9a3, the radius slot as FUN_005e0160's
  `push 2`), marks it a pet (+0xc4 |= 0x20000) and ties it to its owner.
  Hydra's three (hydra1..3 are SizeX 0) share the subtile.
- **FUN_005c49e0**(skill, owner, pet, level, bonus): the pet's level
  (stat 12) = min(owner's level, owner's × 3 / 4 + bonus) — bonus calc2
  for 119 / 114, 0 else — then its defense (31) and attack rating (19)
  straight from MonLvl at that level (+0xb70 rows of 0x78: difficulty
  and expansion columns).
- **FUN_005c4470**(pet, owner, skill, level, bonus): the skill's passive
  stats (+0x98 / +0xa4) and aurastats (+0x54 / +0x68, in a state with
  aurastate +0x80) onto the pet, life and max life + calc1 % (stats 6 /
  7), sumskill n at sumsk n calc levels (FUN_0056deb0), the aura events,
  sumumod (FUN_005a4850), sumoverlay, then FUN_005d6b60 with the skill
  level: the pet's MonEquip gear (below).
- **FUN_005d6b60**(game, owner, pet, skill, level, ilvl, oninit): MonEquip
  gear. The pet's MonStats row (0x1a8 bytes, tables +0xa78) gives a
  MonEquip index at +0x2a (none when negative; rows of 0x1c bytes at
  tables +0xbe0, `FUN_005d30a0`). With `oninit` set (a monster being
  made) the first row must have its oninit byte (+4) set. A pet without an
  inventory (+0x60) gets nothing. Rows for this monster are skipped while
  their level (+2) is above `level`; from there every following row of the
  same monster runs: count the item slots (+0x14 loc bytes, 1..10, up to
  3), roll one with the unit's seed (a power of two masks, else modulo),
  and if that body slot is empty equip the slot's item (+8, a code; four
  spaces take the owner's item in the same slot, skipped if the owner has
  none) at item level `ilvl` and quality from +0x17 (outside 0..7: 0),
  through `FUN_00573b20`. So a summoned Valkyrie takes the rows from her
  skill level on: the higher rows give better gear.
- Built: `Fight::pets` (a Monster each), `summon_skill` / `cast_summon` /
  `summon` / `pets_turn` / `pet_foe`; monsters take pets as foes; Raise
  Skeleton uses a Blood Moor corpse (`Monster::corpse_used`).
  `Monsters::row` ignores case (Skills.txt says ClayGolem). Not yet:
  FUN_005c4470's stats, sumskills (the skeletal mage's bolt, Fire Golem's
  Holy Fire, Valkyrie's), sumumod, Skeleton / Golem Mastery
  (FUN_005d6b60), pets' own think (NecroPet's since traced: pet-ai.md),
  pets leaving the Blood Moor, Decoy, Shadow Warrior, the Druid's spirits
  and vines, Raven's hit count.

### Traps (phase 5, part 4)
- Do 45 (**FUN_005d6170** → **FUN_005d5e10**): the summon path —
  FUN_0056e620 (the pet slot), FUN_0056d940 at the target, FUN_005c49e0 /
  FUN_005c4470 (level, sumskills at their calcs) — then FUN_005a7e60 /
  FUN_005a7c20 hand the trap its mode. The trap monsters (MonStats AI
  AssassinSentry: aidel 15, aip1 100, aip2 10, aip3 15, aip4 13..25;
  DeathSentry) shoot Skill1, a monster skill ('sentry lightning',
  'BoltSentry', 'death sentry ltng', ...) whose missile row names the
  player's skill (sentrylightningbolt → Lightning Sentry), so the damage
  is the Assassin's skill at the trap's sumskill level.
- Built: traps as pets (`Pet::shot_skill / shot_level / shots`,
  `trap_shot`, `trap_turn`): every aidel ticks at the nearest monster
  within aip4 subtiles; Charged Bolt Sentry's do 17 spread; spent after
  Param1 shots (calc4 for Charged Bolt Sentry), untargetable. Not traced:
  the AssassinSentry / DeathSentry AI functions (aip4 as range, shot
  counts as published); not built: Wake of Fire / Inferno (do 125 / 95),
  Death Sentry's corpse blast (do 55), Blade Sentinel / Fury / Shield,
  Fire Blast / Shock Web (thrown traps).

### Do-function missiles (phase 6, part 1)
- Tables: srvdofunc 0x7322b0, missile pSrvDoFunc 0x73c768, pSrvHitFunc
  0x73c840, pSrvDmgFunc 0x73c960 (4-byte entries, index = the column).
- **Do 10** (FUN_005db6d0: Guided Arrow, Bone Spirit): srvmissilea at the
  target unit (flags 0x20; 0x420 with none); calc1 through the callback
  **0x5db6a0**, which adds it to the missile's stat 25 (damage %). Missile
  do **7** (FUN_005ae780) re-aims at the target every Param1 (0: 5) frames
  while it's 4..24 off (FUN_006416d0), a live hostile unit, the owner alive
  (else the missile ends) and its room not a town's; hit
  **10** (FUN_005aa650) lets a guided one pass all but its target.
- **St 8** (FUN_005dacd0, Strafe): n = the monsters within aurarange,
  clamped to [min(calc3, calc1), calc1]; **do 12** (FUN_005dba40) shoots one
  at the next (FUN_0056bd10) per call, calc2 as its damage % (0x5db6a0).
- **Do 26** (FUN_005ca1b0, Chain Lightning): srvmissilea with calc1 hops
  (FUN_0064a710). Hit **12** (FUN_005aa730): with hops > 1, a new one from
  the hit point at another unit within sHitPar1 (else aurarange) subtiles,
  filter 0x88583, not the one just hit, a hop fewer.
- **Missile do 14** (FUN_005aef70, Grim Ward's grimwardsmall / medium /
  large): every Param1 frames (6; FUN_0064a3b0 the missile's age) it runs
  skill srvdofunc Param2 (30, the curse; FUN_0056d810 -> 0x7322b0) at the
  missile's skill and level (FUN_0064a280 / FUN_0064a210), the missile its
  caster. With no target unit the point is its own spot (FUN_0056d2c0), so
  Terror (Grim Ward's auratargetstate) lands round it: aurarangecalc ln12,
  auralencalc par6 (60) / AiCurseDivisor, the AI swap as a Necromancer's
  Terror (monster-ai.md "Curse thinks"). d2d: the missile update runs
  `spot` with the skill as do 30.
- **Do 28** (FUN_005ca3e0 -> FUN_0056ede0: Meteor, Blizzard, Eruption):
  srvmissilea standing at the target point (within 100). Blizzard's centre
  (missile do **10**, FUN_005aea60) and Eruption's (**25**, FUN_005af880)
  call FUN_005a9820: every calc2 frames SubMissile1 at a random point
  rand(2r) - r on each axis, r = calc1 - 1 (-6..5 subtiles at calc1 7). Meteor's centre hits (**14**, FUN_005aabb0) when
  its Range runs out (FUN_005adf10 with no unit): the skill's damage within
  sHitPar1 (else aurarange) subtiles (FUN_0056bad0), then FUN_005aaa90
  puts HitSubMissile1 at 18 offsets (x 0x6e2550, y 0x6e2598; every
  sHitPar2th) with flags 0x8001: its Range set to FUN_004cc7c0 = Param3 +
  (level - 1) x Param4 frames (FUN_0059fa30: 0x8000 takes the Range from
  the create struct). The fire burns its own row (meteorfire: EMin <<
  HitShift a frame, + Inferno x 3 %, FUN_0064b100).
- **Do 24** (FUN_005c9ea0, Fire Wall): from the target point two
  srvmissilea makers out across the caster's line, srvmissileb on the
  point. The maker (missile do **6**, FUN_005ae680) leaves SubMissile1
  where it is each frame.
- **Do 19** (FUN_005c8ca0, Inferno, Arctic Blast; st 11 FUN_005c8fa0
  checks the mana): a flame per call lasting calc1 frames (flags 0x8020).
- Missile do **5** (FUN_005ae520: the flames of Fire Wall, Meteor, Blaze)
  stands and collides every frame; damage func **3** (FUN_005ad220) only
  rolls a hit flag (0x4000) against a percentage (the missile's seed & 0x7f
  under dParam1, +0x78: immolationfire 19 / 128): the damage is the
  skill's per frame, in 256ths (Fire Wall 1: 15 << 4 = 240 = 23 a second).
  A row with no Skill (meteorfire, immolationfire) burns its own row:
  FUN_0064b100 = (EMin + the level's ELev part, FUN_00644b70) plus
  EDmgSymPerCalc % (+0x118) << HitShift, the calc run by FUN_0064b7c0, the
  Missiles.txt calc evaluator (missile, owner, calc offset, row, level). The
  same evaluator gives Immolation Arrow's fire its length: FUN_005aa250 runs
  SHitCalc1 (+0x88) through it and FUN_005a9530 hands that to each fire.
- Built: `Fight::fire` / `missile_tick` / `burn` / `launch` (fight.hpp),
  `Missile::target / hops / ed_pct / fixed` (ai.hpp), MissileInfo
  srv_do / param1..2 / hit_par2 / sub / hit_sub (loaded to a fixed point
  of SubMissile1 / HitSubMissile1). Not traced: FUN_0056bd10's pick (the
  nearest), DamageRate's use, Charged Bolt's and Strafe's timing (arrows
  3 ticks apart), Inferno's held channel (it lasts the cast).

### Pets (phase 6, part 2)
- **FUN_005c4470** is how the masteries reach pets: the summoning skill's
  passive stats (+0x98 / +0xa4, set as base stats) and aurastats (+0x54 /
  +0x68, in its aurastate) go on the pet, their calcs run with the
  owner's skills — Raise Skeleton's maxhp is `skill('Skeleton
  Mastery'.lvl) * par1 * 256`, Clay Golem's tohit `skill('Golem
  Mastery'.ln56) + ...`, the golems' synergies likewise. Then life (with
  that maxhp) + calc1 %, sumskills at their sumsk calcs (FUN_0056deb0),
  the aura events, sumumod, sumoverlay. **FUN_005d6b60** isn't a
  mastery: it's the pet's MonEquip.txt gear.
- A pet's life and damage are MonStats' own columns (a skeleton 21 life,
  1-2 damage in Normal, 30 / 42 in Nightmare / Hell), not MonLvl-scaled.
  The Druid's and Raven's rows have no damage: the skill's physical
  (MinDam + MinLevDam) is theirs. (Neither traced: the published values.)
- Do 149 (**FUN_005ce0b0**, NecromageMissile): srvmissilea + the mage's
  byte +0xf (necromage1..4: poison, cold, fire, lightning).
- Do 144 (**FUN_005ca910**, Hydra): three at the target, sumskills at
  level (HydraMissile, whose row 'hydra' carries the Hydra skill).
- Do 58 (**FUN_005c56c0**, Revive): the corpse's monster rises with its
  own stats (FUN_006538a0), brought down to the owner's level and life
  when above it, then FUN_005c4470 (200 % life, Revive's speed and
  damage %); the pet's time is calc2 frames.
- Do 15 (**FUN_005dc000**, Decoy): the owner's look (FUN_00621ce0), life =
  owner's max life x calc3 %, the owner's level, gone in calc2 frames.
- Do 49 (**FUN_005d6e70**, Shadow Warrior / Master): the owner's level
  (stat 12), skills (FUN_005d6cf0) and look; gear by FUN_005d6b60.
- Do 114 / 119 (FUN_005c6910 / FUN_005c7390): FUN_005c49e0 with calc2 as
  the bonus level, then FUN_005c4470.
- Built: `Fight::summon_one` (the stats, sumskills: a missile it shoots or
  an aura it runs), `Pet::ranged / aura / res / thorns / fire / until /
  variant / hits / idle / mirror / where`, `pet_aura` (Fire Golem's Holy
  Fire round it), the totems' do-65 auras on the player in range
  (`update_fighters`), `pets_cross` (pets follow the player over a level
  edge; traps stay), devctl `debug pets`. Not traced / not built: the pet
  AI functions but NecroPet's (DruidWolf, Totem, ...; pet-ai.md), the pets' MonEquip gear, sumumod / sumoverlay, Valkyrie's Dodge /
  Avoid / Evade, Clay Golem's slow, Fire Golem's fire absorb, Decoy's and the
  Shadow's look (drawn as their rows), Raven's hit count (Param5).

### Missile hit functions and spot spells (phase 6, part 3)
- A missile's hit function runs when it strikes a unit and, with no unit,
  where its Range runs out (FUN_005ae1f0 -> FUN_005adf10(0, 1)): return
  bit 2 damages the unit, bit 1 ends the missile, 4 passes by. So Fire
  Ball, the exploding arrows and Glacial Spike go off at the end of their
  flight as well.
- **2** (FUN_005a9d80, Plague Javelin), **4** (FUN_005b07a0, Exploding /
  Freezing Arrow), **36** (FUN_005abf70, Fire Blast / Shock Web in the
  air: passes units, lands at its target), **51** (volcano debris):
  HitSubMissile1 there. **3** (bomb on ground): the skill's damage within
  aurarange. **7** (FUN_005a9fb0, Holy Bolt): sHitPar2 1 strikes only the
  undead (FUN_0063e990), 2 demons (FUN_0063e940); with sHitPar1 it heals
  allies by calc1. **9** (FUN_005aa250, Immolation Arrow): the damage
  within sHitPar1 (else calc1), fire over that ground (FUN_005a9530) for
  the arrow row's SHitCalc1 (+0x88) frames (0x8000), burning its own row
  (immolationfire: + Fire Arrow x 5 %).
  **13** (FUN_005aa8b0, Glacial Spike): the damage within sHitPar1 (else
  aurarange), frozen sHitPar2 (else auralen) frames (FUN_005a8f20). **20**
  (FUN_005ab370, Lightning Fury): HitSubMissile1 at up to sHitPar2 (else
  calc1) units within sHitPar1 (else aurarange). **22** (FUN_005add20,
  Fist of the Heavens): the damage within aurarange of its target, then
  holy bolts at the undead in it (FUN_005ab630). **29** (FUN_005abb00,
  Frozen Orb): HitSubMissile1 toward every sHitPar1th of 64 directions
  (0x6e2738 / 0x6e2638: a circle of radius 30). **47** (FUN_005ac550,
  Molten Boulder): the damage within sHitPar1 (else aurarange), fire at
  0x6e2550's offsets; it rolls on. **48** (FUN_005ac6d0): HitSubMissile1
  on toward the target.
- **19** (FUN_005ab110, FingerMageSpider): the missile's skill's
  auratargetstate (+0x82) on the unit struck for auralen (+0x60) frames,
  at least 5, with its aurastats (FUN_005c6cc0); a new hit renews the
  length. Returns 3 (damage, end). d2d: `Fight::marks`, read by the knight's
  and finger mage's thinks through `Foe::states`.
- Missile do **15** (FUN_005af030, Frozen Orb): every Param1 frames
  SubMissile1 toward direction n of 64 (0x6e2b78 / 0x6e2a78), n += Param2.
  **23** (FUN_005af790, Firestorm): SubMissile1 where it is. **27**
  (FUN_005afa30, Tornado): every Param1 (else calc4) frames its damage
  within Param2 (else aurarange). **28** (FUN_005afb80, Volcano): every
  Param1 (else calc4) frames, between frames Param3 and Param4,
  SubMissile1 thrown within Param2 (else aurarange).
- NextHit rows put state 0x56 on the unit they struck (FUN_005adb00); every
  NextHit missile passes it by until frame + NextDelay has run, so it is
  struck again at frame + NextDelay + 1 (Twister, Tornado, Shock Field, the
  war cries).
- Skill dos: **11** (FUN_005db850, Charged Strike): calc1 of its missile
  from the unit struck, away from the caster, wandering: Charged Bolt's
  setup (flags 0x21, FUN_005c9290 with each one's index) aimed at the
  subtile twice the struck unit's less the caster's; none at calc1 < 1
  (Fight::strike_bolts, test_game_missiles). **14**
  (FUN_005dbe50, Lightning Strike): from the unit struck at another within
  calc1 (FUN_0056bd10), calc2 hops. **73** (FUN_005d0040, Blessed Hammer):
  path type 14 (FUN_00648cf0), +50 % magic damage (stats 52 / 53) against
  the undead (FUN_006461d0). **80** (FUN_005d0670, Fist of the Heavens):
  its delay missile on the target. **117** (FUN_005c7160, Firestorm):
  calc1 makers, all but the first wandering. **118** (FUN_005c72f0,
  Twister / Tornado): calc1 of them (FUN_005c7040). **123** (FUN_005c8080,
  Volcano): at the point. **43** (FUN_005d5d70 -> FUN_005d5bf0, Shock
  Web): prgcalc of them scattered within aurarange. **48** (FUN_005d68a0,
  Blade Fury): one per attack frame while it's held. **33**
  (FUN_005d3140, Psychic Hammer): the skill's damage on the unit, knockback
  at calc1 % (calc2 champion, calc3 unique, calc4 boss). **51**
  (FUN_005d76e0, Mind Blast): physical and stun within aurarange (or
  prgcalc), conversion through FUN_005d7680. **20** (FUN_005c9800 ->
  FUN_005c96a0, Static Field): calc1 % of life within aurarange. **55**
  (FUN_005c4df0, Corpse Explosion): the corpse's max life (FUN_006538a0) x
  calc1..calc2 %, half fire half physical, within aurarange / 2. **63**
  (FUN_005c5e60, Poison Explosion): srvmissilea clouds on the corpse. **27**
  (FUN_005ca360, Teleport): to the point if it's open and not in town.
  **74** (FUN_005d88b0, Double Throw): both weapons' missiles (flag 1)
  with the skill's toht (stat 19) and calc1 damage % (stat 25).
- Built: `Fight::burst / strike / area / spot / spot_skill /
  strike_bolts / land_range`, `Missile::ox / oy / turn / hit_at`,
  MonType undead / demon. Not traced / not built: Blessed Hammer's path
  (a spiral out at a fifth of its speed), Holy Bolt's healing, Static
  Field's floor, Mind Blast's conversion, Plague's cloud count (one), Volcano's frame window,
  Double Throw (the throwing weapons' missiles), explosions on walls.

### Buffs, curses, war cries (phase 6, part 4)
- Do **18** (FUN_005c9480: Holy Shield, the Sorceress's armors, Bone /
  Cyclone Armor, Burst of Speed, Fade, Venom), **23** (Energy Shield,
  Blaze), **25** (FUN_005ca030, Enchant: on the ally targeted, else the
  caster), **29** (FUN_005ca4d0, Thunder Storm), **47** (FUN_005d6630,
  Cloak of Shadows): the aurastate (+0x80) with its aurastats for auralen
  frames, the aura events (+0x84.., FUN_0056e740 with their function ids
  +0x8a), the passive part (FUN_005c6dc0). Cloak then puts auratargetstate
  cloaked on the monsters within aurarange (FUN_005d6520).
- Curses: do **30** (FUN_005c37c0) and **61** (FUN_005c3f20, Confuse):
  auratargetstate (+0x82) with its aurastats for auralen frames on every
  unit within aurarange of the target (FUN_0056e780 -> FUN_005c35c0 /
  FUN_005c3de0). Dim Vision and Terror swap the monster's AI (special 10 / 11)
  and land only where FUN_0056e2f0 lets (monster-ai.md "Curse thinks"). **59** (FUN_005c3b90, Attract) on the one unit. **6**
  (FUN_005db1c0, Inner Sight / Slow Missiles) round the caster. **71**
  (FUN_005d8570, Taunt) on the monster (or the nearest within 20).
- War cries: do **68** (FUN_005d83e0): a nova of srvmissilea (FUN_0056d400)
  and the state on the caster (FUN_005d8290). Hit **18** (FUN_005ab0b0):
  the state on an ally it reaches. **21** (FUN_005ab500, Battle Cry): the
  auratargetstate on an enemy. **17** (FUN_005aafb0, Howl, do 22's nova):
  a monster below the caster's level + Param2 (FUN_004efc20, skill
  +0x14c) + the level runs (FUN_005ddd00) for Param5 + Param6 × (level −
  1) frames (FUN_004efcb0, its 4th stack arg, the state block's +0x10);
  FUN_004cc7c0's Param3 + Param4 × (level − 1), its 3rd, is never read
  (bugs.md 41).
- Do **69** (FUN_005d81c0, Find Potion) / **72** (FUN_005d8780, Find Item):
  a corpse without state 0x76 (then marked), at calc1 %: a potion
  (FUN_005d8100) / a treasure drop of a tier by Param1..4 (FUN_005a8000).
  FUN_005d8100: the caster's level id (FUN_00620bb0 → FUN_0061a1b0) to an
  act by 0x6eb2f0's starts {1, 40, 75, 103, 109} (FUN_006427f0), the row act
  + difficulty × 5 of 0x741b58 (15 rows of {healing, mana, rejuvenation}:
  hp2 mp2 rvs; hp3 mp3 rvs ×2; hp4 mp4 rvl ×3; hp4 mp5 rvl; hp5 mp5 rvl ×8);
  the caster's rand(100) below Param3 (+0x150, 30) the mana one, below
  Param3 + Param4 (+0x154, 10) the rejuvenation, else the healing one.
  d2d: `rules::find_potion`.
- Curse lengths (FUN_005c37a0): auralen / DifficultyLevels AiCurseDivisor
  (+0x1c; 1, 2, 4) for Confuse (FUN_005c3f20), Attract (FUN_005c3bcc), and
  do 30's curses whose auratargetstate is 23 dimvision or 56 terror
  (FUN_005c3400); the other curses keep auralen.
- Aura event functions (FUN_0056e740 -> table 0x7325b0 by auraeventfunc,
  registered with FUN_005c0ad0; ECX the wearer, then the other unit, the
  hit, skill, level):
  - **1** Chilling Armor (FUN_005cab40, hitbymissile): the missile's owner
    (FUN_00552fd0: unit +0x98 when +0xc8 bit 10), hostile (FUN_00554200),
    and the missile's row ReturnFire (flag bit 9, +5 & 2; checked against
    Missiles.txt): the skill's srvmissilea (+0x48) from the wearer at the
    owner's spot (FUN_0059fa30, flags 0x20).
  - **2** Frozen Armor (FUN_005cac50, damagedinmelee): a player or monster
    whose hit did damage (+8 over 0) gets a hit with flag 0x20 and calc1
    (+0x138) frames' freeze (+0x34; FUN_0057c6c0), the skill's overlay
    (+0x110): resisted by cold res less pierce, then FUN_0057b230 (a special
    is chilled; others frozen at coldeffect < 0, / MonsterFreezeDivisor).
  - **3** Shiver Armor (FUN_005cad40, attackedinmelee): the skill's
    element (FUN_0056e0c0) as a hit (result 0x4021, flags 0xd;
    FUN_0057c6c0, FUN_0057cee0), the overlay.
  - **4** Iron Maiden (FUN_005c5f50) / **5** Life Tap (FUN_005c61e0): on
    the cursed unit, calc1 % of the damage back / as life to the attacker
    (Iron Maiden's calc2 / calc3 when the wearer is a player or hireling).
    **Iron Maiden's function, read (FUN_005c5f50; cited, decompile and
    disassembly, no oracle run).** This one is fastcall
    with ECX the *game* (FUN_00552f60 takes it as its first argument at
    0x5c6016, and 0x5c60fb reads game +0x6d, the difficulty), not the
    wearer; `ret 0x14` takes five stack arguments: A (the cursed wearer,
    which takes the hit), B (the unit it struck), C (the hit it dealt: +8 its
    damage), the skill id, the level. Table slot 4 of 0x7325b0 is 0x7325c0,
    the only reference to it. Steps:
    1. 0 when B or A is missing, A is not type 0 / 1, A is dead
       (FUN_005541b0), or C missing or C +8 under 1.
    2. The skill row (FUN_0045c4b0, `skill_row`); a level under 0 becomes 1.
       Row +0x82 (auratargetstate) must be in range and A must carry it
       (FUN_00639df0), else 0. The state's stat list (FUN_006256b0) names
       the curse's caster (owner type FUN_00625250, id FUN_006252b0), found
       with FUN_00552f60 (it may be 0).
    3. The percent: A a monster that is not a hireling (FUN_0063ee90 0) takes
       calc1 (+0x138); A a player or a hireling takes calc2 (+0x13c) when B is
       a player, else calc3 (+0x140). It is evaluated by FUN_00646ca0 with the
       caster, the skill and the level.
    4. A zeroed 0x70-byte hit record: damage (+8) = C +8 x percent / 100
       (FUN_00483360); result flags (+4) |= row +0x12e | 0x20, hit flags (+0)
       |= row +0x130, hit class (+0x60) = row +0x134. It hits A with B as the
       source: FUN_0057c6c0 (game, B, A, 1, record) then FUN_0057cee0
       (game, B, A, record). So the reflected damage is the row's ResultFlags
       / HitFlags / HitClass, not a thorns number.
    5. A leech tail, only when B is a monster of class 0x122 (the `cmp eax,
       eax` at 0x5c60e3 compares a register with itself, so its jne falls through on this path): B's MonStats
       record (FUN_0055b7e0) byte +0xa0 + difficulty (Drain) must be over 0,
       and B must be evil (alignment FUN_006259b0 0). The reflected damage
       (record +8) is scaled by that Drain percent unless it is 100. Then,
       with A alive and A's owner (FUN_0058f0d0) alive or none, and the
       scaled damage over 0, leech = 20 % of it (`lea edx,[eax+0x14]` with
       eax = 0 from the alive test at 0x5c6152: the constant 0x14). With an
       owner, the owner is
       healed 50 % of it first (FUN_005c5f10, `heal_unit_life`: EAX amount in
       256ths, EBX unit; adds to stat 6 at most up to the unit's max life and
       returns what fitted) and the rest is reduced by it; A is healed
       the rest, the owner whatever A could not take; overlay 0x97 (151,
       FUN_00621e40) plays on A and on the owner. Returns 1 from step 4 on.
    d2d: reads the same, cited from the decompile, no oracle run, on the
    reflected share and the calc pick for a monster
    wearer (`Monster::reflect_pct`, calc1 of "ironmaiden", states.cpp:364;
    ai.cpp:3073 as thorns) and for a player wearer's picked row
    (tick.cpp:550); the wearer rule is "hireling" (FUN_0063ee90 classes
    0x10f, 0x152, 0x167, 0x230, 0x231), not "any pet": a skeleton or a
    golem is a monster, so calc1. Not ported: the leech tail for class 0x122,
    and the reflected hit sent as a hit record (its flags and hit class; resistances not traced).
  - **22** Bone Armor (FUN_005c6370, absorbdamage): its pool stat
    (record +0x54) takes the hit's physical (+8); at 0 the state goes.
  - **24** Energy Shield (FUN_005ca6c0): calc1 % of each damage type of
    DAT_006e30e0 (+8, +0x10, +0x1c, +0x24, +0x20; the flagged +0x38..+0x40
    only from a monster), mana (stat 8) taken 16 / calc2 a point; at 0 the
    state goes. Each field loses min(field x calc1 / 100, mana x 16 /
    calc2) (FUN_00483360 at 0x5ca7e1 / 0x5ca7f3): no cap on calc1 in the
    function; Skills.txt's calc1 is `min(edmn,95)`, so it never passes 95.
  - **25** Cyclone Armor (FUN_005c87a0): the pool takes fire, lightning
    and cold (+0x10, +0x1c, +0x24; FUN_005c8780 each). In that order:
    fire (+0x10), cold (+0x24), lightning (+0x1c), each only when above
    0; FUN_005c8780 (EAX the pool, in 256ths as the stat at skill +0x54
    holds it): a field at least the pool loses the pool (pool 0), else the
    field goes to 0 and the pool keeps the rest. It runs in event 0xb, so
    on the unresisted 256ths fields, before FUN_0057c1e0's flat MDR and
    resistances. Pool 0 after: the state goes (FUN_00639db0 / FUN_006277e0);
    else stat 0x54 (the client's shown share, MulDiv of skill +0x56's stat)
    is resent when it moves by 5 or more.
    d2d: `rules::absorb_blows` on each part's raw field (`cyclone_take`,
    then `elemental_fine`); V: `tools/emu/cyclone_armor.py`, 3000 records
    in `tests/cyclone_armor_cases.inc` (`test_cyclone_armor`).
  d2d: `buff_events` (Foe::fired_by, damaged_by), `absorb` (Taken::physical
  / elemental).
- Built: `self_cast` (all of the above), `start_state`, `state_of`,
  `absorb`, `buff_events`, `buff_tick` (Blaze, Thunder Storm),
  `Monster::curse / cry` with `monster_states` (damage %, pace %, Iron
  Maiden, Terror, blindness) and their aurastats in `target_of`, Life Tap
  in `land`, Battle Orders' maxima in `update_fighters`, Redemption in
  `aura_pulse`, Conversion (st 32 / do 79), Leap / Dragon Flight /
  Telekinesis as spot skills, Grim Ward's totem, `Loot::put`; devctl
  `debug states`.
- **Thunder Storm** (do 29, FUN_005ca4d0): the cast puts the state on with
  the skill and level in stats 0x15e / 0x15f, a periodic skill run every
  perdelay frames while it lasts; each run strikes (srvmissilea, hit at once,
  FUN_005adf10) the monster within Param7 subtiles (FUN_005c8bc0, record
  +0x160; 17) whose unit id is next after the last struck (FUN_006444a0 /
  FUN_00644560), round to the lowest (FUN_0056bd10 with callback 0x56bcd0).
- **Battle Command**'s aurastat1 item_allskills (1) adds to every skill
  while its state is on (World's `skill_level`).
- **War cries on allies** (hit 18, FUN_005ab0b0): the nova's missile
  reaching a unit FUN_00554de0 calls an ally gives it the state too
  (FUN_005d8290: the caster's auralen and level, aurastats FUN_005c6cc0).
  d2d: `merc_cry` (Shout's armor %, Battle Orders' life % on the merc),
  a pet's buffs (`Pet::bo_life`, its armor %), those within the missile's
  reach as it's cast.
- **Leap** (start 40, FUN_005d9d50; do 77, FUN_005da370): the point pulled
  back to aurarangecalc subtiles (FUN_005d9a10, FUN_006417f0), a free spot
  (mask 0x1c09, three tries) reached in a line (FUN_00645910, 0x804) within
  reach, state 0x12; at its first event it flies there (FUN_005d9f70: path
  type 9 at the class's run velocity, CharStats +0x41 << 8; a monster's
  MonStats Run), at the next it lands (FUN_005da120) and knocks back each
  monster within calc1 subtiles (result 9, FUN_0056bad0). d2d:
  `Fight::leap`. FUN_005d9a10 (EAX the skill) in subtiles: range =
  aurarangecalc; d = FUN_006417f0(caster, point) = (short + 2 × long) / 2,
  at least 1; over range the point becomes caster + (point − caster) ×
  range / d (C division, each axis). sx = point y − caster y, sy = caster
  x − point x, each clamped as −2 below −1, 2 above 1 (−1..1 kept). Three
  tries: the point, then (x + sx, y + sy), then (x − sx, y − sy) (from the
  pulled point); each: the nearest free spot to it (FUN_0064e7b0, the
  caster's size, 0x1c09), that spot free (FUN_0064d910, 0x1c09), the line
  from the caster's subtile to it clear (FUN_00645910 -> FUN_0064e260,
  0x804, no sizes) and FUN_006417f0 to it ≤ range. The first that passes
  is the landing; none, no leap.
- **Dragon Flight** (start 12, FUN_005c9030: the target within
  aurarangecalc, neither in town; do 52, FUN_005d7850): the flight is at
  once, to the free spot nearest the target (FUN_00554ea0), unless the
  level's Teleport (Levels.txt, FUN_0061db70 +4) is 0, or 2 without a clear
  line (FUN_006229f0, 0x804: FUN_00622920 with the caster's size,
  FUN_00620510's 2 for a player, and 2 at the point, 0x622a80); then the kick. `Level::teleport` holds the
  column (Acts 1–2: 1, Duriel's Lair 2); Teleport obeys it too.

### The rest (phase 6, part 5)
- Royal Strike's releases: **40** (FUN_005d5010: the count's missile
  standing at the target, its meteor), **143** (FUN_005d4f40 ->
  FUN_005d4870) and **37** (FUN_005d4e70 -> FUN_005d4150, Claws of
  Thunder's third): a 64-direction scan (FUN_005d4680 / FUN_005d40f0) for
  units within prgcalc (else aurarange), a missile at each with
  FUN_004efc20 + 1 hops; **41** (FUN_005d5080): prgcalc of them toward
  random points round the target.
- Do **60** (FUN_005c58b0, Bone Wall): a bonewall on the point, then two
  srvmissilea makers carrying calc2 / 2 (missile do 13 lays the rest).
  With P the first piece's spot (where FUN_0056d940 put it) and (dx, dy)
  = caster - P in subtiles ((1, 0) when both are 0): outside town and
  only when count = calc2 / 2 (C division) is above 1, one maker from P
  toward (P.x - dy, P.y + dx), the other toward (P.x + dy, P.y - dx)
  (0x5c5b04..0x5c5b96), each carrying the first piece's id
  (FUN_0064a710) and the count (FUN_0064a760). Missile do 13
  (FUN_005aeda0, table 0x73c768 + 13 x 4): while its count is above 0,
  on a frame whose move entered a new subtile (path +0x34 & 8, set by
  FUN_00650150 when it crossed one or more), with the first piece still
  there (FUN_00552f60), a piece at the maker's spot (FUN_0056d940,
  joined to the first one's group FUN_0058f030 / FUN_0058f100, its
  stats FUN_005c4470); only a piece made spends one of the count. Then
  it flies on (FUN_005ae1f0): Vel 12, Range 7 + 2 a level, it stops on
  collision (CollideType 8).
  d2d: `rules::bone_wall_makers` (V: `tools/emu/bone_wall.py`, 2000
  casts in `tests/bone_wall_cases.inc`, `test_bone_wall`) and
  `Fight::missile_tick`'s do 13 (`test_bone_wall_lay`).
  **62** (FUN_005c5d00, Bone Prison): twelve round the point (0x6e304c /
  0x6e307c: (-1,-4) (1,-4) (3,-3) (4,-1) (4,1) (3,3) (-1,4) (1,4) (-3,3)
  (-4,-1) (-4,1) (-3,-3)).
- Do **115** (FUN_005c6a80, the vines): the summon at level calc2, state
  0x96. **116** (FUN_005c6ec0, Werewolf / Werebear): the aurastate for
  auralen frames (1000 + Shape Shifting), a second cast shifts back:
  FUN_0056c740(unit, state, 1) takes off every state of its group (3:
  wolf, bear, maul, feralrage, monsterset, delerium) the unit has, and
  with any off nothing goes on and FUN_0056f020 starts the delay; the do
  gives 0 (no srvmissile), so it pays nothing. Werebear on the wolf is a
  shift back too. d2d: `Fight::shift_back` in the SC cast;
  `test_skill_ready`. **120** (FUN_005c77c0, Feral Rage / Maul):
  the state, its count (stat 0xa9) up by one to calc2. **122**
  (FUN_005c7f10, Hunger): the hit with calc1 damage % and calc2 / calc3
  stolen. st **56** (FUN_005c7690) / **58** (FUN_005c7e00): the
  werebeast hits.
- Do **124** (FUN_005c8190, Armageddon / Hurricane): the state and a timer
  event (FUN_005417d0(5, FUN_004efc80 + frame): Skills +0x154, Param4).
  Event 5 (FUN_0056d790) runs the state's srvactivefunc (States.txt, the
  table 0x7322b0): 145 Hurricane (FUN_005c8380) strikes within aurarange,
  146 Armageddon (FUN_005c8520) lays its control row on a free spot within
  it (5 tries, 0x805); each run files the next Param4 frames on (8 / 20),
  and one in town takes the state off. **44** (FUN_005d6020,
  Blade Sentinel): a trap monster (FUN_005d5e10) sent to the point and
  back (FUN_00554ea0). **54** (FUN_005d7e10 -> FUN_005d7ce0, Blade
  Shield): a periodic skill, struck every perdelay (par3: 25) frames while
  the state lasts: every unit within aurarange (par4) passing aurafilter
  (FUN_0056b7e0, each by FUN_005d7c40) takes a hit at the skill's to-hit
  (FUN_006449f0, unless HitFlags bit 1), its MinDam..MaxDam (FUN_0056e170)
  and element (FUN_0056e0c0), plus the weapon's at SrcDam (FUN_0057b7d0,
  32 / 128), HitClass +0x1a5. **125** (FUN_005d1170, Wake
  of Fire's shot): its maker row toward the target. **95**
  (FUN_005cc4e0, Inferno Sentry's shot): one flame a frame, its reach
  from the monster's own bytes (+0x9a / +0x9b), for calc3 frames.
- Built: those as `prg` 37 / 40 / 41 / 143, `summon` walls / vines /
  spirits, self casts 116 / 124 / 54 (`buff_tick`: Armageddon, Hurricane,
  Blade Shield), the werebeast melee (st 56 / 57 / 58, do 120 / 121 /
  122), Blade Sentinel (its row back and forth, missile do 20), Double
  Throw (weapons.txt missiletype, `Scene::thrown`), Wake of Fire's maker
  (missile do 31) and Inferno Sentry's flames on traps, Death Sentry's
  corpse blast (`corpse_blast`), pets in the crowd (walls block), devctl
  `debug unbuilt` (lists none). Not traced / not built: the shapeshifted
  look (the Druid keeps his) and the werebeast skills' form requirement,
  Rabies' spread, the walls' time (24 s published) and makers' pace,
  Armageddon's 5 spot tries, Blade Sentinel as a
  monster, Double Throw's toht, Vine Attack / the cyclers (the vines'
  published behaviour).

### Cast rate (FCR)

The "breakpoints" everyone quotes are emergent: game.exe stores a formula
and per-class base frames, not a table. What's stored:

- **Per-class base frames** in `data\global\animdata.d2` (570 304 bytes, 256
  hash blocks of `u32 count` then `count` × 160-byte records; each record is
  `char cofname[8]` + `u32 frames_per_dir` + `u32 anim_speed` +
  `u32 flag_count` + 144 bytes of flags). Read via
  `build/tools/mpq-cat/mpq-cat <mpq> 'data\global\animdata.d2'`. The SC
  (spellcast) frames for the 1.14d ship data, all HTH weapon-mode:

  | Class | cof | base frames |
  |---|---|---:|
  | Amazon (AM) | AMSCHTH | 20 |
  | Assassin (AI) | AISCHTH | 17 |
  | Barbarian (BA) | BASCHTH | 14 |
  | Druid (DZ) | DZSCHTH | 15 |
  | Necromancer (NE) | NESCHTH | 16 |
  | Paladin (PA) | PASCHTH | 16 |
  | Sorceress (SO) | SOSCHTH | 14 |

  The Sorceress "lightning family" cast (Charged Bolt, Nova, Lightning,
  Chain Lightning, Thunder Storm) is anim S1 not SC. `SOS1HTH` in the same
  file is also 14 frames — what matters for the port is the animation the
  skill's `anim` column names.

- **The formula.** `EFCR = FCR × 120 / (120 + FCR)` (integer truncated), the
  same diminishing return as `rules::effective_speed` in
  `components/rules/combat.hpp:164`. Then

  ```
  frames_shown = ceil(base_frames × 256 / (256 + floor(256 × EFCR / 100)))
  ```

- **Traced in game.exe 1.14d** (2026-10-30, `tools/emu/cast_rates.py` runs it):
  - `FUN_00623f50` sets the unit's per-tick rate (+0x4c). Its cast branch
    (0x623fcb..0x624012) runs for a player's SC (mode 10), for a player's SQ
    whose skill has seqtrans SC, and for a monster's SC (`FUN_006216e0` is the
    test). It adds 100 to `FUN_00621740(unit, 2)`, caps the sum at 175, and sets
    the rate to the AnimData record's speed (+0xc) x that / 100, truncated, at
    most 0x7fff.
  - `FUN_00621740` reads the row at 0x6e8e3c (stat 105 faster cast rate, flag 1,
    factor 120; the neighbouring rows are IAS 93, FHR 99, FBR 102, FRW 96 with
    150): with the stat value v it returns 120 x v / (120 + v), truncated, when
    v is not 0. So the effective FCR stops mattering at 75: FCR 200 already
    reaches the 175 cap (speed 448 of 256).
  - A player mode is stepped by the client tick `FUN_00463390`: `FUN_006217c0`
    asks whether it is over, `FUN_00623e00` takes a step if not, and
    `FUN_006217c0` asks again; yes ends the mode. For a plain animation (+0x30
    empty) it is over when current + speed >= +0x48, which `FUN_00624390` set to
    the AnimData frame count x 256 (0x624473); current starts at 0 for SC (the
    start-frame function `FUN_00623b10` returns 0 for it). So the last step is
    skipped: a plain cast lasts ceil(256 x frames / speed) - 1 ticks, at
    least one. A sequence is over when its countdown (+0x48) is <= 0
    (`FUN_00621260` sets it to the sequence's step count x 256, 0x62129a):
    ceil(256 x steps / speed) ticks, no minus one (see the Lightning notes).
  - Checked: `tools/emu/cast_rates.py` runs the cast branch and the end test on
    game.exe for FCR 0..400 at AnimData speeds 208 and 256 and the SC frame
    counts below; `test_combat --cast-rates` prints d2d's in the same form and
    the two match. The tick counts are the published tables' (Sorc 13/12/11/10/9/8/7
    at 0/9/20/37/63/105/200; Amazon 19/18 at 0/7; Assassin 16/15 at 0/8, 9 at 174).
  - Not traced: the code that picks the SC animation for each skill (Skills.txt
    `anim`, +0x10), the client's other tick path for a monster's cast, and
    negative FCR (the formula as written gives a negative effective value; d2d keeps v).
  - D2MOO's `Units.cpp` (MIT; its functions carry these 1.14d addresses) reads the
    same: the 120-factor table, the 175 cap, the speed product and the two end tests.

- **Verification.** Running the formula against `SOSCHTH` base = 14 sweeps
  through breakpoints `[0, 9, 20, 37, 63, 105, 200]` matching the community
  Sorc SC numbers exactly — that's what proves the formula is right (the
  breakpoints are 1-frame drops of `frames_shown` across FCR 0..300).
  Per-class emergent breakpoints for reference (FCR value → resulting
  frame count):

  | Class | Base | Breakpoints (FCR → frames) |
  |---|---:|---|
  | Amazon | 20 | 0→20 7→19 14→18 22→17 32→16 48→15 68→14 99→13 152→12 |
  | Assassin | 17 | 0→17 8→16 16→15 27→14 42→13 65→12 102→11 174→10 |
  | Barbarian | 14 | 0→14 9→13 20→12 37→11 63→10 105→9 200→8 |
  | Druid | 15 | 0→15 9→14 19→13 32→12 54→11 86→10 152→9 |
  | Necromancer | 16 | 0→16 9→15 18→14 30→13 48→12 75→11 125→10 232→9 |
  | Paladin | 16 | 0→16 9→15 18→14 30→13 48→12 75→11 125→10 232→9 |
  | Sorceress | 14 | 0→14 9→13 20→12 37→11 63→10 105→9 200→8 |

- **Port.** Nothing lives as a "breakpoint table": `rules::cast_ticks`
  (combat.hpp) times a plain cast by FCR, `rules::speed_frames` shortens GH /
  BL by FHR / FBR. The table above counts `frames_shown` of the older formula, which
  assumes AnimData speed 256: there it is one more than the ticks a cast takes (the
  end test above). The Druid's SC runs at speed 208, so its row does not hold:
  `cast_ticks(15, 208, 0)` is 18 ticks at 0 FCR, not 14.

### Skill accessors and the passive refresh (work package 00643690)
Cited from the decompile (no oracle run); re-check with
`python3 tools/ghidra/fn.py <address>`.

- **FUN_00643690**(skill id) -> the skill's `passivestate` (record +0x94,
  short): 0 when the id is outside `[0, [0x744304]+0xba0)`, the record
  pointer (`+0xb98 + id*0x23c`) is 0, or the short is below 1. Callers:
  FUN_0056dfa0, FUN_00570080, FUN_00646fd0, FUN_00647110 (each then sets
  that state on the unit with FUN_00639db0).
- **FUN_00639db0**(unit, state, on): does nothing unless `0 <= state <
  [0x744304]+0xc4` (the States.txt count); then FUN_00625a70 (the state bit
  in the unit's state words, plus its dirty bit) and FUN_0064c040 (once per
  unit, flag 0x2000 at unit +0xc4: links it through unit +0xe0 into the list
  head FUN_00619f90 returns; asserts on a null unit). Callers:
  0045d705/19/2d, 0046df40, 004c2180 and the four skill functions below.
  d2d: reads the same, cited from the decompile, no oracle run. `ai.hpp:413` and `ai.cpp:1570` cite the call for
  state 0x76 / 0xc, and `FUN_00625a70`'s bit is cited by
  `tools/emu/freeze_rules.py:249`. d2d has no one function for the
  bounded set; each caller writes its flag.
- **FUN_00643910**(list) and **FUN_006438f0**(node): both are `p ? p[+4] : 0`
  (byte-identical bodies). A skill list header's +4 is the first node
  (FUN_00647110 reads `list+4` too), a node's +4 the next one. FUN_0056dfa0
  walks `for (n = FUN_00643910(*(unit+0xa8)); n; n = FUN_006438f0(n))`
  (the header pointer at unit `+0xa8`, tested non-null first). Callers
  of 006438f0: 004a9a90..004a9df0 (five), of 00643910: 004c7990, 0053c5d0,
  0056dfa0, 00575900, 005eb970. d2d: **not ported** (the shadow oracle only
  stubs them, `tools/emu/shadow_master.py:350`).
- **FUN_00643ce0**(node) -> `*(short *)node[0]`: the skill id, i.e. the
  first short of the node's record pointer (record +0), 0 for a null node
  (a node with a null record would fault: no check). The body reads only
  its one parameter; the file-name / line arguments Ghidra shows at call
  sites (`".\PLAYER\PlrMsg.cpp"`) are not used by it. Callers include 00461180,
  00462560..00462c20, FUN_0054c0e0 (PlrMsg.cpp lines 0x1209 / 0x120e) and
  FUN_0056dfa0. d2d: **agrees**, combat.md:452 already reads it as the id.
- **FUN_00646d60**(unit, skill): the outline is in "Passives and masteries"
  above. Guards: skill in `[0, +0xba0)`, record non-null, `passivestate`
  (+0x94) in `[1, +0xc4)`, and FUN_00643810 (unit in EAX, skill in ESI: the
  unit's +0xa8 list entry with that id, flag +0x3c clear, preferring the base
  entry +0x34 == -1, else the higher +0x28 level) non-zero. If the record's
  +0x80 is below 1 or that state is **not** on the unit (FUN_00639df0):
  level = FUN_006442a0(unit, entry, 1); FUN_00643620(level) (unit and state
  in EDI / EBX; argument 0 removes the passivestate's stat list, non-zero
  finds or creates it) returns 0 for level 0, and then the function returns;
  if stat 0x15f already equals the level -> return.
  Else for up to 5 slots, stop at a passivestat short outside
  `[0, [0x744304]+0xbd4)`: value = FUN_00646ca0 (calc +0xa4+4k, skill,
  level), layer = passiveitype (+0x96), or 0 when that is below 1;
  FUN_00627150 sets it in the list. Then stats 0x15e = skill, 0x15f = level
  and FUN_00639e30(unit, passivestate, 1). If the +0x80 state **is** on, the
  tail FUN_006256b0 -> FUN_006277e0 + FUN_00626cd0 removes the passivestate's
  list. d2d: **agrees** with skills.hpp:765-774 and skills.md:674; this adds
  the invalid-stat break, level 0 removing the list, and the removal while the
  aura state is on.
- **FUN_0056de40**(unit in ECX) (37 bytes): refreshes that unit's passives,
  then, when it is a player, rebuilds pet limits. The asm (0056de40..0056de64)
  saves ECX and calls FUN_00646f20 with the unit on the stack (the callee
  pops it: both exits pop once and return). A null unit, or a type dword
  (`+0`) other than 0, returns after that refresh. A player calls
  FUN_00554010 (ECX = the unit; it returns the dword at unit `+0x80`, and
  asserts line 0xa29 on a null) and tail-jumps to FUN_00575900 with that
  dword in ECX and the unit in EDX. FUN_00575900's decompiled body does not
  read ECX. It walks the player's skills and sets each PetType row's max
  (pet-ai.md). FUN_005d8290 calls it at 0x5d83c7 with the unit that just
  received the aura state: its EDX, stored at `[ebp-4]`, loaded into EBX,
  then into ECX. Eight other call instructions: 0x532f15, 0x534622,
  0x54be5a, 0x56a7be, 0x583be1, 0x583c5a, 0x5c9468, 0x5d8f38. d2d: **not
  ported**. The aura-state cites (ai.cpp, fight.hpp, missiles.cpp) stop at
  FUN_005d8290. Passives are derived on read (`rules::passive_stats`).
- **FUN_0056dfa0**(unit, in ECX): for every node of the list at unit +0xa8
  (FUN_00643910 / FUN_006438f0): id = FUN_00643ce0(node); if
  FUN_00643690(id) > 0, FUN_00639db0(unit, passivestate, 1) then
  FUN_00646d60(unit, id). So for each node whose FUN_00643690 value is
  above 0 it turns that passivestate on and refreshes that skill, with no
  check of the level (the only loop in the function; nodes with 0 are skipped). Called from FUN_0054c0e0 (its
  unit `+0x10 == 0x11` branch, after FUN_0053fdf0 and before the max life /
  mana / stamina stats are written back and state 0x36 is set then cleared;
  `tick.cpp:572` cites it) and FUN_0055b800. d2d: **not ported**: d2d
  derives passives on read (`rules::passive_stats`, skills.hpp:765) and
  does not replay this refresh; whether that differs in play is untested.
- **FUN_00647110**(unit, skill) -> list entry or 0: 0 for a null unit, a
  skill outside `[0, +0xba0)` or a null record. Sets the passivestate on
  (FUN_00643690 + FUN_00639db0). Walks the unit's list (FUN_00620150): an
  entry for that record with +0x34 == -1 (a base entry) gets level +0x28
  += 1 only while the level is below FUN_004aa8b0() or the unit is a
  monster (`unit[0] == 1`), then FUN_00646d60 and returns it. Else, when the
  unit has a list header (+0xa8): a new 0x40-byte zeroed node from
  `".\SKILLS\Skills.cpp"` line 0x539: +0 record, +8 mode = record +0x11 for
  a monster, 0x10 for a player-type unit (`unit[0] == 0`) of class 6 given
  skill 5, else record +0x10; +0x28 level 1, +0x34 -1, +4 next 0; appended
  at the tail (or as the first node when `header+4` is 0), then
  FUN_00646d60. No header: 0. d2d: reads the same, cited from the decompile,
  no oracle run, on the new node's level 1, mode and tail append (`pets.cpp:942`, pet-ai.md:269, throw.md:30). Not in
  d2d: the +1 on an existing base entry (capped for a player) and the
  class 6 / skill 5 mode 0x10 case; `shadow_give` sets the level directly.
- **FUN_00646fd0**(skill, drop_level): the removing twin of FUN_00647110.
  The unit comes in EBX (Ghidra shows `unaff_EBX`; the asm was not read, so
  the register use is cited, not proven). With a list at unit +0xa8 and a
  first node: passivestate (FUN_00643690) off (FUN_00639db0(unit, ps, 0)).
  The list header holds three selected-skill pointers: +8 (cleared with
  FUN_00643bc0(unit, 0, -1)), +0xc (FUN_00643c50(unit, 0, -1)) and +0x10
  (zeroed); each is cleared if it is the base entry (+0x34 == -1) of this
  skill, **whether or not the level then reaches 0**. Then it finds the base
  entry in the chain: with `drop_level` 0, or when `+0x28 -= 1` leaves less
  than 1, the node is unlinked and freed (FUN_0040b480, Skills.cpp line
  0x4ac); finally FUN_00646d60 refreshes the passive. d2d: **not ported**.
  Callers: 006470f0, 00647280.
- **FUN_00570080**(player, skill) -> 1 when learned: spends skill points.
  Guards as above (skill in range, record non-null). Cost 1, or when record
  +0x170 is not -1 the calc at that offset (FUN_00646ca0; level from
  FUN_006442a0 on the current entry, 0 when there is none; +0x170 is likely
  `skpoints`, unconfirmed). If stat 5 (the unspent skill points) is at least
  the cost: stat 5 -= cost (FUN_006272b0), FUN_00647110 grants the level,
  FUN_00646f20 refreshes all passives, and for a player-type unit
  (`unit[0] == 0`) FUN_00554010 and FUN_00575900 run. On success it calls
  FUN_005531c0(0xd73) and FUN_0053c4a0, then sets the passivestate on and
  FUN_00646d60, and returns 1. When FUN_00647110 returns 0 the stat 5 cost is
  given back and the result is 0. d2d: **not ported** (grep of
  `components/` and `apps/` for 00570080 finds nothing).

### Stat-list helpers and skill starts (work package 0056e520, cited)

- **FUN_00627150**(list, stat, value, layer) (32 bytes): `if (list != 0)
  FUN_006270b0(list, stat, value, layer)`. A null-safe SetStat on a stat
  list; FUN_006270b0 finds the stat's entry (FUN_00624b90) or, for a
  non-zero value, creates it (FUN_00624c80), stores `value` at entry +4
  (value 0 deletes the entry, FUN_00624d10), and returns 0 when nothing
  changed. Used by 80+ callers (game-calls.tsv `grep 00627150`). d2d:
  agrees with the "set, not add" reading in drops.md:193 and
  objects.md:81 (no code port needed).
- **FUN_0056e520**(unit in ECX, value in EDX) (83 bytes,
  `.\SKILLS\Skills.cpp`): makes a stat list with FUN_006251f0(owner 0,
  kind 4, no length, the unit's type and id; type 6 / id -1 for a null
  unit), links it with `d2d_rules_socket_props(unit, list, 1)`,
  sets **stat 0x44 (attackrate) = value** (FUN_00627150) and calls
  FUN_00623f50 (the rate recompute, whose body asserts `.\UNITS\Units.cpp`)
  with the Skills.cpp file / line 0x12ab at the call site (the decompile
  shows three arguments; FUN_00623f50's body reads only the unit, so the
  extra two are not confirmed from asm). Callers: Dragon Tail's kick
  start FUN_005d7090 and Bash's start FUN_005d7ea0, both just before the
  to-hit roll (FUN_0057ec10). d2d: **not ported** as such; the nearest is
  the SIAS term (combat.hpp:68), which the kick/bash paths do not add.
  This function does not free the list (kind 4, no length):
  **unverified** whether anything removes it later; not claimed.
- **FUN_004c7be0** (83 bytes) is the same body with line 0x9e5; callers
  FUN_004c7cf0 and two sites at 0x4f26f3 / 0x4f2706. d2d: not ported.
- **FUN_0056ff10**(unit in EDI, skill in the stack arg; only caller
  FUN_005701b0) (354 bytes): returns at once unless the Skills record
  (stride 0x23c at tables +0xb98) has flag +4 & 0x20 and a valid
  `aurastate` short (+0x80, below the States count at +0xc4), and the
  unit has a level (FUN_006439f0 entry, FUN_006442a0(unit, entry, 1) > 0).
  If record +5 & 0x80 is clear: state +0x80 on (FUN_00639db0), find or
  create the state's list (FUN_006256b0 / FUN_006251f0 kind 0), tag it
  with the state (FUN_006252d0), skill (FUN_006260d0) and level
  (FUN_006260f0), link it (`d2d_rules_socket_props`) and set **stats 0x15e
  = skill, 0x15f = level**. If +5 & 0x80 is set it calls
  `d2d_ai_castable(skill, level, 1, 0, 0)` instead. Both end in
  FUN_0056cda0(skill, level, 1). d2d: **not ported**; the 0x15e / 0x15f
  pair agrees with the passive refresh above (FUN_00646d60).
- **FUN_005a2d80** (124 bytes, no caller in game-calls.tsv, so reached
  through a table or pointer): reads stats 0x15 and 0x16; when 0x16 > 0
  and the unit's class id (+4) is not 0x10f, sets stat 0x15 = max(0, s15
  - 1 + s17) and 0x16 = max(0, s16 - 1 + s18) in the unit's list for flag 1
  (FUN_00625760). Stat names by ItemStatCost row (21..24 = min / max /
  secondary min / max damage) are not checked here. d2d: not ported.
- **FUN_005a23d0** (339 bytes; caller FUN_005a2530, the Cursed boss mod):
  per enemy (FUN_00554200) in the area, with Skills record 0x42 (Amplify
  Damage, stride 0x23c, so the record at +0x9378; its state short at
  +0x82 and its five passivestat-style stat shorts from +0x56 are
  range-checked): two FUN_00646ca0 calls evaluate the record's
  calcs (+0x60, +0x68) for the level (results feed the put-state's
  length / level in registers), `d2d_states_put_state` puts the curse,
  and each of 5 stat slots (short +0x56+2k, calc +0x6c+4k) with a valid
  stat and a non-zero calc is **set** into the new list
  (FUN_00627150). d2d: **agrees** with monster-ai.md "Boss mods in the
  hit"; the set-not-add per slot and the five-slot cap are the details
  this adds.
- **FUN_005c3de0** (320 bytes; caller FUN_005c3f20, Confuse): after the
  FUN_005c3d50 test, `d2d_states_put_state` puts the curse from the skill
  descriptor (param_2: stat ids at +0x20..+0x30, five slots, each valid
  stat set to FUN_005c3540(+0x38+4k) when non-zero); if descriptor +0xc is
  non-zero it re-rates the unit with FUN_00623f50 (call site asserts
  `.\SKILLS\SkillNec.cpp` line 0x3d7); then FUN_005543b0(1) (alignment 1),
  FUN_005b1990(0, 9) (list 9), FUN_00573090(0) (kind 3, per
  monster-ai.md) and event 10 at `now + length` (FUN_005417d0).
  d2d: **agrees** with monster-ai.md "Confuse and Attract"; the
  FUN_00623f50 re-rate is the new detail (d2d does not model it).
- **FUN_005d7ea0** (Bash start, 536 bytes): before the roll it asks
  FUN_0061ab00(FUN_00620bb0(target)) (the town test used by DiabPrison
  above) and returns 0 (no attack) in a town room; it evaluates the
  record's calc3 (+0x140) and then calls FUN_0056e520, so the temporary
  attackrate list of Bash is probably calc3 (the value register is not
  visible in the decompile: likely, not certain). d2d: skills.md
  "Melee skills" and strike.cpp:455 **agree** on the roll, flags, ED
  and the aurastate list (FUN_006251f0 kind 4, FUN_0056e900 callback,
  FUN_005c6cc0); **not ported**: the town refusal and the attackrate list.
- **FUN_0056e970** (`d2d_states_put_state`, 622 bytes; fastcall, ECX = a
  put-state descriptor {unit-of-caster +0, target +4, state +0x1c,
  skill +8, level +0xc, length +0x10, stat +0x14 / value +0x18, callback
  +0x20}): fails (0) for a state outside the States table, a player whose
  data +0xd & 1, or one without FUN_00451fe0 flag +5 & 2. For a state with
  FUN_0063a420 set (the curse states): stat 0x6d (109, curse resistance)
  >= 100 fails; a non-zero one shortens the length by FUN_00483360(100)
  (a random share); state 0x39 (57) on the target fails. It finds the
  existing list (FUN_006256b0, or FUN_00625760 flag 0x20 for a curse): same
  state, same skill and the same level only moves its expiry to `now +
  length` (event 0xc), a higher stored level returns 0, anything else
  removes it (FUN_006277e0, FUN_00626cd0) and makes a new kind list
  (flags 2 when timed, 0x800 / 8 by the state's table bits), sets the
  stat/value pair, links it and sets the removal callback (default
  FUN_0056e900). d2d: **agrees** with `put_state` (states.cpp:57) on the
  extend / keep-higher / replace rule; **not ported**: curse resistance
  (objects.md:82 already notes it), state 57's block and the stat pair.
- **FUN_004c6ac0** (796 bytes, client; caller FUN_0045d5e0): for a Skills
  record whose state short (+0x80) and stat short (+0x54) are valid, finds
  or creates the state's list (kind 0, FUN_006251f0), sets the stat to
  the level, runs the client start function from the table at 0x727ba8
  (indexed by the record's shorts from +0xf6, once per earlier level when
  record +4 & 0x80, setting the stat to each level first), plays the
  record's sound / overlay (+0x100 / +0x108 / +0x10a via FUN_004b9a00 and
  FUN_00470390) and restores the unit's +0x28 and the saved position.
  Cited only: the table's meaning is not read. d2d: not ported.
- **FUN_005d7090** (Dragon Tail start, srvstfunc 27, 238 bytes, fastcall;
  only reference 0x7321ac, the srvstfunc table): returns 0 for a skill
  outside the Skills table or without a target (FUN_00553540). Otherwise
  FUN_0056e520 (the temporary attackrate list above), to-hit
  = `d2d_skills_skill_tohit` + stat 0x145 (325) into FUN_0057ec10; on a
  hit (result & 1) FUN_005d54b0 builds the kick damage, then
  FUN_0057dbf0 applies it with SrcDam, record +0x1a5 (0x80 when 0;
  HitClass is the separate field at +0x134);
  returns 1 (also on a miss). d2d: **agrees** with skills.md "Dragon Tail"
  and strike.cpp:415 on the kick at toht + stat 325 and the 0x80 default;
  unlike Bash it has no town refusal. **Not ported**: the attackrate list.

### Skill record and skill-list accessors (work package 004c9460, cited)

- **FUN_004c9460**(skill id in ECX) (43 bytes) -> the record's **Param1**
  (`+0x148`, skills.md above: par1..par8 at `+0x148..0x164`): 0 when the id
  is outside `[0, [0x744304]+0xba0)` or the record pointer
  (`+0xb98 + id*0x23c`) is 0. It is the body of skillcalc operand 8
  (`d2d_skills_calc_operand`, FUN_00646460 `case 8`; operands 8..15 are
  par1..par8, the others of them read `+0x160` / `+0x164` inline). Its two other
  callers read it as a speed share: FUN_004c9b40 (SkillsPal.cpp, cltstfunc 25)
  and FUN_005cf6b0 (the Charge start, "SkilPal.cpp" line 0x330 in its own
  spelling) call it after the stat 0x43 read (FUN_00625480) and feed the
  result to FUN_00483360 and FUN_00648690. The decompile shows a file and
  line (0x1ca, 0x330) beside the call; the function itself takes the id only
  (no assert in its body), so the pair is not its. The decompile drops the
  fastcall registers, so what each call passes is inferred. d2d: **agrees**: `skills.hpp:723` returns
  `params[code - 8]` for operand 8 (the table's Param1); the list has no
  separate Par1 reader for the two starts, which are not ported
  (`FUN_004c9b40`: cited only).
- **FUN_00483360**(a in ECX, b in EDX, c on the stack) (122 bytes) -> a × b /
  c, the "MulDiv" behind the percents in the skills code: 0 when c is 0. When a ≤
  0x100000 and b ≤ 0x10000 it multiplies in **32 bits** and divides (the
  product is signed and wraps past 2^31, e.g. a = 0x100000, b = 0x10000 gives
  2^36); else when c ≤ b >> 4 it divides first (`(b / c) * a`, wrapping), and
  for a > 0x100000 when c ≤ a >> 4 `(a / c) * b`; else it widens (`__alldiv`
  of the 64-bit product by c sign-extended). The tests are signed, so a
  negative a or b passes its own bound, but the 32-bit branch needs both
  (a ≤ 0x100000 and b ≤ 0x10000); one over its bound takes the other paths. d2d: **agrees**,
  `rules::mul_div` (`combat.hpp:261`) is the same branch for branch (its
  comment names FUN_00483360); callers in ai.cpp, strike.cpp and
  gamedata.hpp use it. The only wart is the 32-bit wrap (bugs.md #61).
- **FUN_004833e0**(class in ECX) (36 bytes) -> the CharStats record
  (`[0x744304]+0xbc4 + class*0xc4`; count at `+0xbc8`): 0 when the class is
  negative or at least the count. No null test of its own (a missing table gives
  a bad pointer, not 0). Callers: FUN_004c85b0, FUN_004c9120, FUN_004c9b40,
  FUN_004e4d80, FUN_005cf6b0. FUN_005cf6b0 (Charge start) reads its byte
  `+0x41` << 8 (the class's run velocity, as at the Charge row above and
  char-select.md; it goes to FUN_00648690, the movement speed, not a swing
  pace) when the unit is a player (`unit[0] == 0`);
  a monster uses the short at MonStats `+0x34` << 8 instead, and the default is
  0x100. The record walk
  matches drops.md ("walks the class's CharStats row"). d2d: **not ported**
  as a function (CharStats rows are `rules::` tables read by class).
- **FUN_004c7630**(unit in ECX, target in EDX) (102 bytes) -> always 1: builds
  a unit command on its stack (net-packets.md "The unit command", seven
  dwords) `s = {0, -1, type, id, 1, 0, 0}` with type / id the target's
  `[0]` / `[3]` (6 / -1 for a null target), and hands it to FUN_00480c10 with
  flag 1. The command number and unit it passes are in registers the
  decompile drops (inferred: the caller's unit, an attack on a unit). Its only
  caller of note is FUN_004c9b40 (the picker's start) when the target is
  already in reach (FUN_00622c40): the start returns this 1 and does not run
  the rest. Also called by FUN_004c8d90, FUN_004c9120. d2d: **not ported**
  (the client's own click-to-command path is not modelled).
- **FUN_004c7950**(target in ECX) (63 bytes): like FUN_0056e520 it makes a
  stat list with FUN_006251f0 (`+0` 0, `+0x10` **6**, `+0x18` **0x32**, the
  target's type and id in `+8` / `+0xc`, 6 / -1 for a null target), sets the
  list's callback (`+0x38`, FUN_00625ce0) to **FUN_004c7890** and links it
  with `d2d_rules_socket_props(target, list, 1)`; nothing if the list is not
  made. FUN_004c7890 (callback, `.\SKILLS\Skills.cpp` line 0x93d) acts on the
  unit's current skill (FUN_00620250) unless it has state 0x12 (FUN_00639df0)
  or skill-flag bit 2 (FUN_006446a0 `& 2`): by the skill record's `+0x2e` short
  0x43 -> FUN_004ca000, 0x4c -> FUN_004c9420, 0x4d / 0x4e -> FUN_004c8b80.
  Callers: FUN_004c83c0, FUN_004c8d90, FUN_004c9120, FUN_004c9b40 (its last
  call, after the skill-list header is set). What kind 6 or the 0x32 stand
  for is not read here (cited). d2d: **not ported** (a client-side list).
- **FUN_00620250**(unit) (56 bytes) -> the unit's selected (current) skill
  node: FUN_006439a0 returns field `+0x10` of the skill-list header (unit
  `+0xa8`; it takes the header in ECX, fastcall, which the decompile drops;
  checked by `skill_accessors.py`), 0 for
  a null header, and also 0 when the header has no selected skill. A null **unit**
  is fatal: it formats message 0x65c (FUN_00408090 / FUN_00408a60) and calls
  FUN_00681e09(-1) (the exit), so a null unit never returns 0; a unit
  with no header (or no selected skill) gives 0, which FUN_004c9b40 and FUN_005cf6b0 test. Callers include FUN_004611f0, FUN_00463390, FUN_004645b0,
  FUN_00465070, FUN_004654c0. d2d: **agrees** with combat.md "unit skill list
  +0x10" (cited there); the fatal null is not modelled (not checked
  whether any d2d caller can pass a null unit).
- **FUN_00644560**(skill, value) (51 bytes, stdcall `ret 8`; a skill record,
  `D2SkillStrc`, not the list header; `skill_accessors.py`): sets the record's
  `+0x18`; a null record formats message 0xa76 and exits (FUN_00681e09(-1)). Seen as
  the first of the target pair `(+0x18, +0x1c)`: the target's type (FUN_004c9b40,
  FUN_005cf6b0: `*target`, 6 for none) or, for a ground target, the x
  (act1-end.md AndrialSpray), and Thunder Storm's last struck unit id
  (above). `+0x1c` is set by FUN_006445a0. d2d: **agrees** with the Thunder
  Storm use (`fight.hpp:347` `storm_last`); the other uses are not ported.
- **FUN_006445e0**(skill, value) (51 bytes, stdcall `ret 8`, a skill record):
  sets the record's `+0x20`; a null record formats message 0xa88 and exits. FUN_004c9b40 writes 0 to it as its
  last step before FUN_004c7950; other callers FUN_004c83c0, FUN_004c8d90,
  FUN_004c9db0, FUN_004d9260 (not read). d2d: **not ported**.
- **FUN_00644620**(skill, value) (51 bytes, stdcall `ret 8`, a skill record):
  sets the record's `+0x24`; a null record formats message 0xa91 and exits. FUN_004c9b40 and FUN_005cf6b0 set
  it to 7 and then, only for a non-player unit (`*unit != 0`) of class 0xd3
  (Duriel), to 0xf, both before the stat 0x43 read. Other callers FUN_004c6de0,
  FUN_004c83c0, FUN_004c8d90, FUN_00549960. What the numbers mean is not read
  (the unit-mode tables number differently for players and monsters; cited).
  d2d: **not ported**.

### Skill list accessors and the hotkey sweep (work package 006450f0, cited)

Cited, not proven: read from `python3 tools/ghidra/fn.py <address>`; no oracle run.
The assert line numbers are the constant passed to `FUN_00408090` (its body
returns only its return address, so what it makes of the number is not read)
before `FUN_00408a60` and `FUN_00681e09(-1)` (report, then exit). The file
name string was not read, so the "Skills.cpp" in the rows is the nearest
cited module, not a quoted assert.

- **FUN_006450f0**(node) -> node `+0x30`: a skill list node's count, the one
  `FUN_00647640` tests for scrolls and tomes (above). A null node asserts
  with line `0xda4` and exits. Callers: 00496be0, 004a9a90, 004a9ae0,
  004a9b30, 004a9b80, 004a9df0, 004aa3a0, 0055c070 and others (see
  `game-calls.tsv`). d2d: **not ported** (only the always-0 result of the
  count's search is, `skill_text.hpp:389`).
- **FUN_00620150**(unit) -> `FUN_00643910(*(unit +0xa8))`: the header pointer
  stored at unit `+0xa8` is passed, and `FUN_00643910` reads header `+4`, the
  first node of the unit's skill list; a null unit asserts with line `0x638` and exits. d2d:
  **not ported** (it holds the list directly).
- **FUN_00643b00**(node, &item, &skill, &level, &charges) -> 0 when node
  `+0x34` is -1 (a base entry), else 1 with each non-null out written:
  `*item = +0x34`, `*skill = *(short*)node[0]`, `*level = +0x28`,
  `*charges = +0x38`. Null node asserts (line `0x641`). So it reads an
  **item** entry's fields (skills.md "Skill record" above: +0x34 the item's
  unit id, +0x38 charges). Callers: 00496be0, 004a9a00, 004a9bd0, 004a9fc0,
  004aa3a0. d2d: **not ported**; `skillbar.hpp` keeps the same four fields
  in its own entry type.
- **FUN_004a9a90 / FUN_004a9ae0 / FUN_004a9b30** (and FUN_004a9b80, the
  fourth; only 004edea0 calls the first three): fastcall (unit in ECX, skill
  id in EDX). Four byte-identical bodies. A non-null unit: walk the list from
  `FUN_00620150(unit)` with `FUN_006438f0`, stopping at the first node whose
  record (`FUN_00644140`) is non-null, whose `FUN_006450f0` count is at
  least 1 and whose record's first short equals the skill id. **The result is
  never used: every path returns 0.** This confirms bugs.md 24 for all four
  (the 24 row names the family; 004a9ae0 and 004a9b30 are the second and
  third copies). d2d: **agrees** (`skill_text.hpp:389` shows 0, bugs.md 24).
- **FUN_004a9d70**(record, count): the hotkey sweep. For each of the 60
  ints from `0x7c04e0` up to but not including `0x7c05d0` (the picker's skill
  ids; the 60 ints starting at `0x7c05d0` are the item ids beside them, as
  `FUN_004aa0f0` shows: skill `0x7c04e0[i]`, item `0x7c05d0[i]`) equal to `*(short *)record`,
  each of the 16 hotkey slots k with `0x7c06c8[k]` equal to that value is
  cleared when `count < 1`: `0x7c06c8[k]`, `0x7c07b8[k]` and `0x7c0768[k]`
  all set to -1, then `FUN_004786a0(-1)`. The slot loop runs 16 times (its
  counter 0xffff + k * 0x10000 stops at 0x10ffff).
- **FUN_004a9df0**(): with the player (`FUN_00463dd0` = `[0x7a6a70]`) list from
  `FUN_00620150` (an empty list asserts, line `0x47d`), for every node whose
  record has `+8 & 0x10` it calls `FUN_004a9d70(record, FUN_006450f0(node))`.
  So a hotkey bound to a scroll / tome-type skill whose node count is below 1
  is unbound (the count `+0x30`, the real one, unlike the 004a9a90 family).
  Called once, from `FUN_0048ffe0`. d2d: **not ported**. `skillbar.hpp:26`
  says nothing clears `0x7c06c8` for a gone entry: that holds (a gone entry
  is not visited), but a count-0 entry with the record flag does clear it.
  Which skills have record `+8 & 0x10` was not read; `FUN_004aa3a0` below
  pairs the same bit with a count above 0, so it marks the count-based
  (scroll / tome) skills (likely).
- **FUN_004d9fc0**(unit in ECX, skill in EDX) -> usable status: `FUN_00647960`
  (unit, skill); when that is 0 and the unit is the client player
  (`FUN_00463de0` = `[0x7a6a70]`) and the frame clock `FUN_0044da90`
  (`[0x7a0498]`) is below `FUN_0044ce50` (`[0x7a04fc]`, the cast delay's end)
  the result is 8, else the status unchanged. d2d: **agrees** with the
  existing rows (above, "frame clock 0x7a0498 plus min(delay, 12)");
  this adds the exact test: strict `<`, player only. Callers: 004610c0,
  00467880, 004a8d30, 004aa3a0, 004c1200.
- **FUN_004aa3a0**(three args from `FUN_004aa7e0`): the picker's list
  builder. Ghidra shows five params, so the roles are read from the uses,
  not proven. `param_3` (the stack word) is 0 in all of `FUN_004aa7e0`'s
  calls, so its `!= 0` branches (the right-list record test `+7 & 0x40`,
  x step +0x30) are not reached from there; `param_4` selects placing
  (`FUN_004a8d50` / `FUN_004aa1f0`) instead of `FUN_004a9a00` + the icon
  draw, `param_5` skips the icon draw. Per category 0..4, for
  each node of `FUN_00620150(unit)` (next from `FUN_006438f0` read first):
  record non-null; id in `[0, [0x744304]+0xba0)`; Skills row (+0xb98 + id *
  0x23c) non-null; its SkillDesc index (row `+0x194`) in `[0, +0xb94)` with
  the row (+0xb8c + idx * 0x120) non-null; for the right list the record
  `+7 & 0x40`; record `+4 & 0x10` (passive) clear; `FUN_006446a0(node) & 8`
  clear. `FUN_00643b00` gives the item id (and charges); for the left list a
  non-item node with `FUN_006450f0(node) > 0` and record `+8 & 0x10` counts
  as an item entry (the scroll / tome rows). Categories 0..3 take non-item
  nodes whose SkillDesc byte `+5` equals the category; category 4 takes the
  item entries (so five rows, as the HUD notes above say). A SkillDesc byte
  `+6` (1..255) already seen is skipped (a 256-byte `seen` table), so two
  skills sharing it show once. Status colour: `FUN_004d9fc0`: 0 -> 0, 6 -> 4,
  other -> 1. The row's y starts at `[0x711470] - 0x56` and drops 0x30 for each
  new category reached; the y of rows 1..4 is saved at `0x7c070c..0x7c0718`.
  The icon is drawn by `FUN_004a9870`, an item entry by `FUN_004a8d50`
  (level) or `FUN_004aa1f0` (count). d2d: **agrees** with `skillbar.hpp`
  (categories, charged entries, passives and flag 0x6ce278 as the existing
  rows say); the SkillDesc `+6` de-duplication and the count-based
  (`+8 & 0x10`) entry were not found in d2d (`git grep` on skillbar.hpp:
  no mention): **not ported**, untested whether it matters in play.
- **FUN_004aa7e0**(): the picker rebuild. Zeroes `0x7c0300` (0x3c5 bytes),
  sets the 60 ints at `0x7c05d0` to -1, zeroes `0x7c06c0`, then two passes,
  each `FUN_00463dd0(a, b, 0)` and `FUN_004aa3a0(a, b, 0)`. With
  `[0x7c07fc]` zero: (a=0, b=0) then (0, 1); non-zero: (1, 0) then (1, 1).
  Only caller `FUN_00456ee0` (panel draw). d2d: **agrees** in outline with
  `skillbar.hpp:7` (two lists); what a and b select was not compared.
- **FUN_004aa160**(): with the list asserted non-empty (line `0x524`), for each
  of the 60 picker slots whose entry lookup (`FUN_006439f0`, registers not
  read) is non-null and whose record has `+4 & 0x10` clear (not a passive),
  while fewer than 16 were bound, `FUN_004aa0f0` binds it to the next hotkey
  slot (a free `0x7c06c8[k] == -1`; skill from `0x7c04e0[i]`, item from
  `0x7c05d0[i]`, left flag from `[0x7c07fc]`). It looks like the default
  hotkey fill. No caller in `game-calls.tsv` (called through a pointer, or
  dead: not decided). d2d: **not ported**.
- Each skill's own srvstfunc / srvdofunc body beyond Attack, [2] and Bash's
  start (Dragon Talon's kicks, sentries, missiles, auras): trace them per
  phase.
- Which `FUN_` reads stat 105 into the SC animation's `frames_shown` (the
  formula and inputs above are traced; the exact call site isn't). A hunt
  for `imul 0x78` (120) around PlrModes.cpp (FUN_0057eec0..LAB_00581050)
  would land it.
- Per-class animation tables for FHR / FBR (same animdata.d2 file, GH /
  BL modes; the formula is the same as FCR).
- The skill bar and picker layout (SkillsBar.cpp / SkillsPal.cpp).

## Skills.cpp accessors and helpers (SKILLS gap batch)

Forty-three small D2Common routines in `SKILLS\Skills.cpp`, each run for real
in the emulator against a Python model (`skill_accessors.py` 23,
`skill_table_rows.py` 14, `skill_corpse.py` 6; random records incl. null,
sign and width edges, and a `--mutation-check` that must catch each planted
defect). Offsets are the skill record's (Redivivus `D2SkillStrc`) and the
skill list's (`D2SkillListStrc`); the calling conventions below are what the
bytes show (several older notes called the stdcall ones fastcall).

**Skill list** (a unit's `+0xa8`): `+4` first skill, `+8` left skill
(FUN_00643930; a null list asserts, Skills.cpp line 0x3e9), `+0xc` right skill
(FUN_00643960, 0x40e), `+0x10` current skill (fastcall set FUN_00643990 and get
FUN_006439a0: a null list is ignored / answers 0), `+0x14` the light
(FUN_00643a00 swaps it and answers the old one, FUN_00643a30 reads it; 0 with no
unit or list). FUN_00643a60 frees each skill (the free call at 0x40b480 gets
line 0x505 and a pool from FUN_004dc0b0) then the list (line 0x508, the unit's
own pool at `+8`) and clears
the unit's `+0xa8`.

**Skill record** accessors (stdcall, `ret 4` / `ret 8`; a null record asserts
with the line in brackets, the same halt as the rest of D2Common, and exits):
`+0xc` flags get (0 for null, FUN_006446a0) / set (0xa9a, FUN_00644660);
`+0x10` target info get / set (0xa23 / 0xa2c); `+0x14` target type
(0xa35 / 0xa3e); FUN_00644460 clears both (0xa47); `+0x18..+0x24` params 1-4
(get 0xa52 / 0xa5b / 0xa64 / 0xa6d; set 0xa76 / 0xa7f / 0xa88 / 0xa91);
FUN_00645120 sets `+0x30` (0xdad). FUN_00643b70 turns a skill into an item
skill: a skill whose owner (`+0x34`) is -1 answers 0 and is left alone; else
`+0x3c = 1`, `+0x38 = value`, answers 1 (0x657 on null).

**Skills.txt row readers** (sgptDataTable `0x744304`, rows of 0x23c at `+0xb98`,
count `+0xba0`; each tests `0 <= id < count` signed and the row address
for null): FUN_00645fb0 answers byte `+6` & gadwBitMasks[1] (`0x6ce26c`);
FUN_00645080 stores the signed charclass byte `+0xc` through its out pointer
(7 for a bad id or a negative class) and answers `class != 7`, asserting 0xd79 on
a null out pointer; FUN_00645b70 / FUN_00645bc0 (level, id) answer 0 for
level <= 0 or a bad id, else FUN_00645b20(level, par1, par2) / (level, par3,
par4) (diminishing returns); FUN_00644750 answers the signed short `+0x174`
(reqlevel) plus the unit's own level in the skill, `0x7fffffff` for a bad id.
FUN_00644750 has no null-unit check, unlike FUN_006447b0.

**A unit's skill levels**: FUN_006447b0 (fastcall unit, id) the level `+0x28` of
FUN_006439b0(unit, id, -1), 0 for none. FUN_00644300 adds FUN_00644180's
bonus levels (ESI the unit) only when the skill's owner is -1. FUN_00644aa0
takes FUN_00643810's skill (EAX unit, ESI id), adds the bonus the same way,
floors at 0 and caps at the experience table's last level (FUN_00611830(0),
called twice when it caps).

**Other helpers**: FUN_00643d00 (fastcall unit, skill): a player (type 0) gets
the record's seqnum (byte `+0x13`); any other unit type takes the MonStats path:
`aSkSeq` (`+0x188`) at the slot whose `aSkill` (`+0x170`, 8 signed shorts)
equals the skill id; skill 0xc7 and no slot give 0. FUN_00644c20 (ESI unit, stack type):
a player's Strength + Dexterity - 20, any other unit 3 x its level, at least 1,
then /3 (type 0) or /4 (rounded toward zero), x 256, wrapping in 32 bits.
FUN_00646170: a shapeshifted player's attack frames, `(+0x48 & ~0xff) / speed`
with speed from FUN_0062a710 for the weapon FUN_00623990 gives, 19 with no
weapon, 0 when the speed is below 1. FUN_00645160 (EAX mode, stack class)
maps a mode through the table at `0x6eb308`, 12 for (class 6, mode 4); the
index has no bound check (the table is 16 entries; the oracle draws modes up to
23, reading past it into `0x6eb348`).
FUN_00645a70 is (n + 1) * 100 / (n + 8) in 32 bits and has no caller; n = -8
would divide by zero (the emulator faults), so nothing may pass it.

**Corpse tests** (a monster, type 1, in mode 12 = corpse, or mode 0 = dying
where the flag allows; no passive state from FUN_0063a770; the class has
corpse-select, FUN_004638a0 with flag 7): FUN_006455e0 and FUN_006456d0 are two
identical copies answering 1 when the gate passes, else 0; FUN_00645590 and FUN_00645630 are two identical
copies that also need FUN_00621f60 (soft monster); FUN_006454c0 (ESI unit, stack
bCheckDead) and FUN_00645510 (stdcall) also take mode 0 when the flag is set,
and FUN_00645510 then answers the class's Velocity (MonStats word `+0x32`) != 0.
FUN_00645510 reads `+0x32` of a null row for a class outside the table; the
class flag is 0 for such a class, so that path is not reached.

Labels such as "soft monster" (FUN_00621f60), "shapeshifted" (FUN_0063a400),
"the experience table's last level" (FUN_00611830), "Diablo's prison" (skill
0xc7) and the `D2SkillStrc` / `D2SkillListStrc` field names come from
Redivivus names or from callers; those callees are hooked, so the oracle checks
how the code uses their results, not what they mean.

No bug found in this batch; the oracles are the evidence (bugs.md unchanged).

## Plan (phases, each shippable)

0. ~~Combat corrections~~ — done (combat.md, "Corrections applied").
1. ~~**Skill data + selection + calcs**~~ — done: `skills.hpp` + `test_skills`
   (every calc compiles as game.exe's does; Bone Wall's `par34` reads `par3`),
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
2. ~~**Weapon skills**~~ — **done 2026-09-26**: every melee skill game.exe
   resolves with a hit is built, except Double Throw (a missile, phase 4)
   and the Druid's shape-shifted ones (Fire Claws, ...: shapeshifting).
   History: the Bash family (32 / 2: Bash, Stun,
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
   taken as IAS. Fend (round the enemies in reach), Impale (its wear on
   the weapon) and Holy Shield (a right-click self cast: SC, mana, its
   state; block; Smite's extra damage) too; self states' aurastats join
   the player's stats (make_fighter reads toblock, damageresist, ...).
   Zeal and the second hands now go round by unit id (FUN_0056bd10).
   Not in them: a broken weapon, cast rate, Holy Shield's aura events and
   passive part, the shield requirement. Not yet: Whirlwind, Leap Attack,
   Charge (they move), Double Throw (missiles). Then Whirlwind, Leap
   Attack and Charge (moving the player through their sequences); not in
   them: Whirlwind's pathing (a straight line), velocitypercent (its base
   100), Leap Attack's range and its leap's height, Charge's knockback
   distance, Leap (not an attack). Was:
   **Weapon skills** (srvstfunc builds the record, srvdofunc[2] resolves it:
   Bash, Jab, Sacrifice, the Assassin kicks): calc1 damage %, calc2
   post-damage add, toht, ResultFlags (knockback, stun), SrcDam,
   `UseAttackRate` timing, all through `rules::player_blow`. Files: the
   skill's damage % / flat / to-hit into `Fighter` in `combat.hpp`; the
   srvstfunc/srvdofunc dispatch as a table in `fight.hpp`.
3. ~~**Passives and masteries**~~ — **done 2026-09-26** (see "Passives and
   masteries" above). Was: (`passivestat*`): Claw Mastery, Weapon Block,
   Critical Strike, Dodge / Avoid / Evade: they feed `Fighter`
   (`skills.hpp` computes the stats, `make_fighter` adds them).
4. ~~**Missile spells**~~ — **done 2026-09-26** (see "Missile skills"
   above; Poison Dagger: st 16 FUN_005c30a0 builds Bash's way with its
   poison, do 32 FUN_005c4cd0 resolves; melee skill elements take the
   mastery too, FUN_0056e0c0 passes flag 1). Was: (Fire Bolt, Magic Arrow, Poison Dagger): EMin/EMax by
   level, synergies, cast rate (FCR), the missile system that already flies
   quill spikes (`ai.hpp` Missile; friendly missiles like the merc's).
5. ~~**Auras, summons, charge-ups, sentries**~~ — **done 2026-09-26**
   (see "Auras", "Charge-up releases", "Summons", "Traps" above, each with
   what isn't built). Was: auras as states on units in
   range, summons as monsters on the player's side (the merc code), the
   Assassin charges (srvprgfunc through srvdofunc), traps.

### Skill HUD (SkillsBar / SkillsPal)

The `.\\SKILLS\\SkillsBar.cpp` / `.\\SKILLS\\SkillsPal.cpp` asserts sit in
the SERVER-side skill-use path (D2Game, 004c83c0..004c9b40), not the
client HUD. They gate can-cast, pick a slot in world space, and emit the
"cast this" command. Line numbers seen: Bar 0x1ba (FUN_004c83c0), 0x237
(FUN_004c85b0), 0x3a2 (FUN_004c8d90), 0x469 / 0x499 / 0x49f (FUN_004c9120);
Pal 0x176 / 0x1ca (FUN_004c9b40). None of them draws icons.

The client HUD (D2Client) lives at 004a8xxx..004aaxxx. Init page:
FUN_00453d90 calls FUN_004a8a30 (loads `Spells\\Skillicon` into
DAT_007c07f8 — the generic 48-cel file) and FUN_004aa920 (loads
`Spells\\skltree_<class>_back` into DAT_00724bf0 + class*0x22). Per-class
skill sprites are lazy: FUN_004a8c80 loads `Spells\\<CC>Skillicon` on
first use into DAT_00724ac0 + class*0x22 (class table has 7 entries × 0x22
bytes: Am So Ne Pa Ba Dr As); FUN_004a8c00 frees the lot.

Icon draw is FUN_004a9870(short\* sid, ctx, arg3, hover_mode, x, y,
panel_first): class from FUN_00645040(sid) → 0..6 (or generic if ≥7),
IconCel byte from SkillDesc.txt at `[0xb8c] + row*0x120 + 7` where the row
is Skills.txt `+0x194 SkillDesc`, cel then blitted with FUN_004f64b0.
Hit-rect is `[x, x+0x30] × [y-0x30, y]` — **icons are 48×48, anchored
bottom-left**, exactly what our `SkillBar::draw` at
`apps/d2d/skillbar.hpp:126` does.

SkillDesc.txt columns: `iconcel` (string @ 006e77f4) and `ListRow` (@
006e7808) are loaded in FUN_00613f80 (the excel loader). ListRow drives
per-class picker rows; game.exe reads the value from the SkillDesc record
(0x120 bytes) alongside iconcel. Pass to trace the offset next.

Our picker vs game.exe (`apps/d2d/skillbar.hpp`):
- ✅ icon size 48, bottom-left anchor: match FUN_004a9870.
- ✅ generic-vs-class icon fallback: match (via `scene->generic_skill_icons`).
- ✅ ±48 column step: match.
- ❌ `kLeftX = 117`: game.exe uses **80** (`MOV EDX, 0x50` at 0x004aa820).
- ❌ `kRightX = kScreenWidth - 165`: game.exe uses **screen_width - 128**
  (`LEA ESI, [EAX - 0x80]; MOV EDX, ESI` at 0x004aa847 / 0x004aa88d).
- ❌ row categorisation: our `rows(on_left)` groups general first then
  class pages 1..3. Game.exe reads **SkillDesc byte at offset +5** (0..4)
  and iterates 0..4 outer-first (`if (local_c == *(char*)(iVar2 + 5))`
  in FUN_004aa3a0). 0 is general, 1..4 are class categories — five rows
  max, not four.
- ✅ hotkey label (corrected 2026-10-02: it is drawn). FUN_004aa3a0's
  icon draw FUN_004a9870 ends in FUN_004a8ed0 (left) / FUN_004a9300
  (right): for each of the 16 hotkey slots (skill 0x7c06c8 + 4k, item
  0x7c0768 + 4k, left flag 0x7c07b8 + 4k) bound to this icon,
  FUN_004a8df0(ECX = key action 0x0e + k, EDX = x + 0x22, y − 0x23,
  colour 4). That fetches the bound key's name (FUN_00469aa0 /
  FUN_0046a530; none → nothing), cuts it to 40 px, right-aligns it at
  x + 47 when wider than 13 px and draws it (FUN_00502320). FUN_004a9260
  then draws a number at the icon's bottom (an item skill's charges).

### Traced 2026-09-29 (2nd HUD pass)

Picker top-level: **FUN_004aa7e0** (`.\\UI\\spellsel.cpp`) calls picker draw
**FUN_004aa3a0** up to four times per frame — one for each of {left / right
button} × {"current class" / "other" categories}. Argument shape (ECX + EDX
fastcall, rest ignored — Ghidra guesses `param_3..5` from stack junk):

| call | button | initial X (EDX)          | notes                          |
|---:|---|---|---|
| 1  | left  | `0x50 = 80`              | (`MOV EDX, 0x50` @ 0x004aa820) |
| 2  | right | `[0x0071146c] - 0x80`    | screen width − 128             |
| 3  | left  | `0x50`                   | second pass, current-class     |
| 4  | right | screen width − 128       | second pass                    |

Inside **FUN_004aa3a0**:
- `local_8 = DAT_00711470 + -0x56` — row-0 (general) Y anchor is
  `screen_height − 86`. Icons draw bottom-left anchored, so the general
  row occupies `[y − 48, y]`, i.e. `[screen_height − 134, screen_height − 86]`.
- Outer loop `local_c = 0..4` — five categories. On first hit in a category,
  `local_8 -= 0x30` (48). Row N (N≥1) sits at `screen_height − 86 − N * 48`.
- Row selector: `local_c == *(char *)(iVar2 + 5)`, where `iVar2` is the
  SkillDesc record base. **SkillDesc byte @ +5 is `ListRow`**, values 0..4.
- Column X step for the picker is `±48` (via **FUN_004a9a00**: `return
  in_EAX ± 0x30`). Left picker grows rightward from `X = 80`, right picker
  grows leftward from `X = screen_width − 128`.
- Duplicate suppression: `local_134[uVar3]` masked by `*(byte*)(iVar2 + 6)`
  (SkillDesc byte @ +6 — a "listcolumn" / group id). A second skill with
  the same group id is skipped so both hands of a stat-shared pair only
  show once.
- Per-row Y is cached at `DAT_007c070c` (row 1), `_0710` (2), `_0714` (3),
  `_0718` (4) for later hover math.

Picker state block layout (DAT_007c0300..):
- `0x0300` — x-coord table (int32 × 240)
- `0x03f0` — y-coord table (int32 × 240) — but note `+0x3c` offset within
  loop: actually stride is 0xf0 = 240 entries in each of x/y/tooltip/id.
- `0x04e0` — tooltip string handle (FUN_00643ce0 from spellsel.cpp line 573)
- `0x05d0` — skill id table (int32 × 240, initialised to 0xffffffff)
- `0x06c0` — count (byte), `0x06c1` — panel_first flag
- `0x06c8` / `0x0768` / `0x07b8` — per-button (left/right) currently
  hovered slot indices; `0x07fc` — "picker is on second pass" flag.

Hit rect (**FUN_004a9bd0** and **FUN_004a9e60**): `[x, x+0x30] × [y-0x30, y]`.

**SkillDesc.txt record (0x120 bytes each) — offsets seen so far:**
- `+5` — `ListRow` (byte, 0..4)
- `+6` — `ListPos` / column group (byte, dup-suppression key)
- `+7` — `IconCel` (byte, cel index into class Skillicon DC6)

The Assassin's charge indicator lives in a different `.\\SKILLS\\SkillBar.cpp`
(singular, 006e32cc → FUN_005da120 / FUN_005d9f70 / FUN_005d8f50, all in
the 005dxxxx SkillAss range).

### Hit-recovery (FHR) and block-rate (FBR)

Same shape as FCR / IAS: an effective rate on the stat, then the animation
plays that much faster. Formula in `components/rules/combat.hpp:164`
(`effective_speed`, cap 120): `E = 120 * v / (120 + v)`. Applied at
`components/game/fight/` for kModeGH / kModeBL / kModeSC:
`len = base_ms * 100 / (100 + E)`. Stats read at `combat.hpp:142` are 99
(FHR), 102 (FBR), 105 (FCR).

Base frames come from `animdata.d2` (loaded `gamedata_load.cpp:850`),
keyed by `<class token><weapon><mode>` (e.g. `BAHTHGH` = Barb + HTH + GH).
`player_anim(mode)` returns the frame count and rate; the reduction is
applied on top.

Game.exe anchors: PlrModes.cpp source string @ 0x006e17e0 (16 xrefs into
0x0057eec0..0x00581050 handling each mode transition); the FHR / FBR table
lookup is not in these functions — the setmode path calls the same
effective-speed reducer used for IAS / FCR (grep confirms no `0x78` /
`* 120` constant in the PlrModes range).

Second pass (2026-09-29): FHR / FBR do **not** share FCR's formula.
The port's `rules::speed_frames` (which matches Sorc SC exactly) is off
by 1 – 4 frames against community FHR breakpoints:

| Stat | Base | Value | speed_frames | Published |
|---|---:|---:|---:|---:|
| Barb 1H FHR | 9 | 86 | 6 | 4 |
| Sorc FHR | 15 | 117 | 10 | 8 |
| Paladin FHR | 7 | 200 | 4 | 1 |
| Necro FHR | 10 | 152 | 6 | 2 |

Game.exe evidence supports the "not a formula" reading: `imul r, r, 0x78`
(the fingerprint of `120 * v / (120 + v)` inline) has **zero hits** in the
whole `.text` — so FCR is not computed that way either, community's
"emergent from formula" story notwithstanding. But raw byte scans for
per-class breakpoint tables also failed:

- `07 0f 1b 30 56 c8` (Barb 1H breakpoints as u8s) — 0 hits.
- `07 00 00 00 0f 00 00 00 …` (as u32s) — 0 hits.
- `09 08 07 06 05 04 03` (Barb 1H frame counts, descending) — 0 hits.
- `0f 0e 0d 0c 0b 0a 09 08` (Sorc frame counts) — 0 hits.

So neither the FCR-shape formula nor the raw-byte breakpoint tables are
in game.exe as expected. The mechanism is one of:

1. A packed / interleaved table (e.g. per-`(class, mode)` struct at a
   fixed offset), reached indirectly — needs a decompile of the setmode
   path that reads stat 99 / 102.
2. A different formula shape (e.g. `frames_left = base - lookup(E)` with
   a single shared lookup) that FCR's Sorc-SC verification happened to
   satisfy by coincidence.

Follow-on hunt: decompile the call chain from `set_pmode(kModeGH)` (client
FUN_004a???? or server 0x0057???? PlrModes range) all the way to the
frame-count assignment. `LAB_0057d28e` reads stat 0x66 (FBR) and does an
arithmetic `(FBR + sign) >> 3` (FBR / 8), which is a hit-in-the-dark clue:
the 8-way divide might index a packed byte per class.

Port state: `rules::speed_frames` is correct for FCR / Sorc SC (verified),
close but slow for FHR / FBR at high stat values (players get hit for
1 – 4 more frames than in game.exe). Ship as-is until the real mechanism
lands; the ponytail note in `combat.hpp` next to `speed_frames` should
call out the FHR / FBR imprecision.

### Act 4's monster skills (2026-10-14 traced; 2026-10-16 cast in `ai.cpp`)

New srvstfunc / srvdofunc rows Act 4's monsters cast (tables 0x732140 /
0x7322b0; skill record offsets as above: +0x48 srvmissilea, +0x4e its
overlay, +0x12e / +0x130 / +0x134 hit flags, +0x138.. calc refs, +0x148..
the Run sequence's numbers):

| skill (Id) | srvst | srvdo | does |
|---|---|---|---|
| Nest (167, the vile mother's) | 49 FUN_005cbd10 | 91 FUN_005cbe00 | start: the point (FUN_0056e620), a free spot there (mask 0x100) stored in the unit's skill record (+0 class from MonStats spawn, x, y, mode); do: events cleared, a unit of that class at that spot (FUN_005b2f20, 1), flags 0x4020000, MonStats overlay (+0xe6) |
| MegademonInferno (319), DiabLight (193) | 53 FUN_005cc240 | — | the breath's end frame (+ calc +0x13c, at least 1) in the skill record, state 0xc on |
| DiabRun (198) | 54 FUN_005cd2c0 | 103 FUN_005cd380 | start: the target's id / type in the record, path to its spot; do (Params at +0x148..+0x15c): record flag 2 (the run over) → anim length Param1 (8) << 8, frame Param2 (14); at frame Param3 (5) runs (path type 1, velocity max(calc1 << 8, 0x100) × stat 0x43 / 100 via FUN_00483360 into the path's +0x7c, the unit a MonStats Velocity's; calc1 = 20); at frame Param4 (13): length Param5 (16) << 8, back to frame Param6 (6); else, with unit +0x4e 1 (the frame's event, FUN_00623e..: event 1, row 18), one hit with the skill's damage (FUN_0056e680, hit flags +0x130 \| 0x20) on the record's unit when in reach (FUN_00622c40), +0x4e cleared. Its sequence is MonSeq seq_diablorun (Sk5mode): 22 RN rows, event 2 on 5..13, event 1 on 18. d2d: `monster_update`'s DiabRun phases (2026-10-16) |
| DiabLight (193) | — | 152 FUN_005cc690 | a missile (srvmissilea diablight, range from Missiles +0x9a / +0x9b × level / 8) at the target; while the record's end frame is ahead and state 0xc is on, events 0 / 1 cleared and event 0 at frame + calc +0x140; else ends (FUN_005cc3b0) |
| DiabCold (194) | — | 100 FUN_005cce80 | the caster flag 0x40; one hit at the unit (flags +0x12e | 1, +0x130, +0x134), no block when stat 6 is 0; the overlay +0x4e |
| FingerMageSpider (196) | — | 101 FUN_005ccfa0 | one srvmissilea (type 3) from the caster to the target (or its path's end); calc +0x138 the missile's level |
| DiabWall (197) | — | 102 FUN_005cd1c0 | calc +0x138 missiles (diabwallmaker, flags 0x21) from him at the target's spot (FUN_0056d2c0), each with FUN_005cd110 and its index: range capped at 77, its seed {target x + index, 666} (FUN_00650e40), one draw; at 20+ of 100 Charged Bolt's wiggle (path type 10, steps = range), else straight. d2d: Missile::wall_maker (2026-10-16) |
| DiabPrison (199) | — | 104 FUN_005cd5d0 | at the point's unit (or the path's target unit, type 2): FUN_005b34c0 makes the bone prison round it when its room isn't a town (FUN_0061ab00): FUN_005b3270 with 0x73d4f0 {4 pieces; (1, 1) kind 1, (1, −1), (−1, −1), (−1, 1) kind 3: only after a piece stood}, class 0x154 boneprison1 (AI Idle), radius 8 |
| RegurgitatorEat (214) | — | 108 FUN_005cdc10 | the target a monster corpse (mode 12): its corpse removed (FUN_0061a270, FUN_00555600); heals its life (stat 6) by MulDiv(the corpse's max life, calc1 (+0x138), 100) (FUN_00483360: ECX, EDX, the 100 on the stack; 0x5cdcdc), in 256ths, capped at its own max, at least 1 |
| MonCurseCast (212) | — | 112 FUN_005ce2b0 | `rand(5)` on the caster's unit seed picks a curse from 0x6e3228 {Amplify Damage, Weaken, Life Tap, Decrepify, Lower Resist}; its state's stat list built per pick (the decompile's case labels don't line up with the five rows: read the jump table when porting), cast through FUN_0056e780 / FUN_005c35c0 at the target's area (calc +0x64 radius, +0x60 length) |
| DoomKnightMissile (335) | — | 148 FUN_005cdfb0 | srvmissilea + the caster's variant (unit +0x14 +0xe) when that row exists; FUN_0056ee90 when the skill's +4 flag (& 0x6ce26c), else FUN_0056ecb0 |

Already in d2d's table: 22 Nova (Frost Nova, DiabFire), 24 wall
(PrimeFirewall), 18 self state (MonBoneArmor), 10 guided (MonBoneSpirit),
30 curse (Decrepify), 96 Bestow, 95 breath (MegademonInferno's do).

### Act 5's imp skills (2026-10-24, monster-ai.md "The imps' ride and skills")

| skill (Id) | srvst | srvdo | does |
|---|---|---|---|
| Imp Inferno (282) | 59 FUN_005d1280 | 126 FUN_005d1350 | start: end frame + calc1 (rand(par3, par4)), state 0xc; do: a flame (srvmissilea) at the target every 3 frames till the end, its range calc2 + Missiles Param2 |
| Imp Teleport (330) | — | 129 FUN_005d1ab0 | riding: off at the free spot nearest the point (FUN_005d19d0); at an unbound live unit: onto it and mount (FUN_005d18e0, special 16); else MonTeleport (FUN_005ccc80) |
| Imp Fire Missile (299) | — | 132 FUN_005d2090 | caster flag 0x40; srvmissilea + TransLvl (MonStats +0x4b) at the target (FUN_0056ecb0), the imp's A1 damage by SrcDamage |

### The skill target point (FUN_0056d2c0; Blade Sentinel, do 44)
Cited from the decompile (no oracle run); re-check with
`python3 tools/ghidra/fn.py 0056d2c0`.

- **FUN_0056d2c0**(ECX game, EDX caster, out x, out y) -> 1 when both are
  non-zero. FUN_00553540 gives the target unit of the caster's path
  (`+0x2c`; FUN_00648bf0 reads path `+0x58`), or 0 when that unit is the
  caster itself. With a unit: its x / y (unit types 2, 4, 5 read `+0xc` /
  `+0x10` of the unit's `+0x2c` struct; other types the path's x / y
  getters, 0 with no path). Without one: the caster path's target words
  (FUN_00648a00 / FUN_00648a10, `+0x10` / `+0x12`).
  Callers: FUN_0056d940, FUN_0056dbc0, FUN_0056e450, FUN_0056e780,
  FUN_0056ecb0 and others (game-calls.tsv). d2d: reads the same, cited from the
  decompile, no oracle run (ai.cpp:2034, ai.cpp:2811, missiles.cpp:552 and
  strike.cpp:889 cite it); d2d's `to_x` / `to_y` stand for the point.
- **Do 44** (FUN_005d6020, Blade Sentinel), read from the decompile: it
  needs the skill id in range of `[0x744304]+0xba0` and a non-null record;
  FUN_0056e620 non-negative; the caster's `+0xc4 |= 0x40`; FUN_0056d2c0's
  point; then FUN_005d5e10 (the summon do 45 uses) makes a monster, and
  FUN_00554ea0 and FUN_0058ef40 are called with the caster's and the
  point's coordinates; FUN_005417d0(2, `+0xa8` + 1) is called, and the new
  monster's `+0xc4` loses bit 3. d2d runs the missile row instead
  (`missiles.cpp`, `kBladeSentinel`). Not closed: the monster's walk out
  and back and its hits are not traced (the helpers above are named, not
  read) and d2d has no moving trap monster (`summons.cpp` places them at
  rest), so the `ponytail:` stays.
