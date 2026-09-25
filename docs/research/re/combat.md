# Combat — damage, poison, regeneration (1.14d game.exe)

Our port: `components/rules/monsters.hpp` (`Fighter`, `make_fighter`,
`player_blow`, `monster_blow`, `attack_ticks`, `open_wounds_per_sec`),
applied in `apps/d2d/town.hpp` (`land`, `strike`, `monster_dots`,
`apply_regen`) and `apps/d2d/ai.hpp` (`monster_update`, `missiles_update`).
Tests: `tests/test_monsters.cpp`.

## Applying damage — FUN_0057c6c0 (SUnitDmg.cpp)

The server applies one damage record (an int array) to a unit. Fields seen
in this function, by int index:

| index | use here | call |
|---|---|---|
| 5, 6 | burning damage per tick, length | FUN_0057add0 |
| 10, 11 | poison damage per tick, length | FUN_0057ac50 |
| 0xc | cold length | FUN_0057af80 |
| 0xd | freeze length | FUN_0057b230 |
| 0x11 | stun length | FUN_0057aae0 |
| 0x12 | (life-related, FUN_0057a980) | |
| 0x13 | total damage (8.8 fixed) | subtracted from stat 6 (life) |

Life after the hit below 0x100 (under 1 point) is set to 0: the unit is
dead, and result flag 2 is set. FUN_0057aa60 / FUN_0057aaa0 do the same
for mana (stat 8) and stamina (stat 10).

## Poison and burning — FUN_0057ac50 / FUN_0057add0

Poison is **state 2** (`poison`) and burning **state 0x73**, each holding a
stat list with stat 0x4a (74, `hpregen`) set to **minus the per-tick
damage**, lasting `length` ticks (plus the current frame), ending through
FUN_0056e900. The unit's regen event (event 3) is scheduled for the next
frame.

One of each at a time: when the state is already there, the new one
replaces it (value and length) only if it is at least as strong
(`new == old || new > old`); a weaker one is ignored.

Healing potions are also `hpregen` (misc.txt stat1), in their own state, so
potion and poison add up in the same regen tick.

## Regeneration — who can die to poison

- **Players**: FUN_00580810 (event 3 callback, re-armed every frame) calls
  FUN_00580610: `life += hpregen`, capped at max life; then
  **`if (life < 0x100) life = 0x100`**. Negative regen (poison, burning) can
  never take a player below 1 life. Then mana (FUN_00580500) and stamina
  (FUN_005806f0).
- **Monsters**: FUN_005a6920: `life += hpregen` (less a state's own
  contribution when FUN_0063a750 says so), capped at max, clamped at **0**.
  At 0 the monster dies; the kill goes to the owner of its state 2 (poison)
  or 0x3e, and the kill runs through FUN_0057ccb0 / FUN_005c0c30.

So poison kills monsters and credits the poisoner, but never kills a player.
d2d matches both, and the one-poison-at-a-time rule.

## To hit — FUN_0057ec10 → FUN_0057d9b0

- Defense: FUN_006223f0 + stat 33 (vs melee) or 32 (vs missiles).
- Player attack rating (FUN_00622560): stat 19 + (dex − 7) × 5 + CharStats
  ToHitFactor; then × (100 + mastery to-hit + stat 119 + the skill's
  bonus) / 100 (plus vs-monster-type bonuses, stat 0xb3).
- Monster attack rating: stat 19 + dex × 5 + the skill's bonus, × (100 +
  stat 119) / 100.
- `c = AR × 100 / (AR + DEF)`; `c = c × 2 × alvl / (alvl + dlvl)`, clamped
  5..95; hit when the attacker's seed % 100 < c.
- Then the defender's rolls (FUN_0057dfb0 → FUN_0057dd60), each cancelling
  the hit: **block** (the defender's seed % 100 < chance, the chance a third
  while the player moves), then Weapon Block (claws), **dodge** (stat 338,
  melee, standing), **avoid** (339, missiles), **evade** (340, walking or
  running).
- Block chance (FUN_00622720): players need a shield; `(stat 20 +
  CharStats BlockFactor) × (dex − 15) / (clvl × 2)`, clvl at least 1;
  monsters stat 20 when MonStats allows it (or with a shield); at most 75.

## Damage — FUN_0057b7d0, FUN_0057b420

- Physical (FUN_0057b420): `min` / `max` = stats 21 / 22 (23 / 24 for a
  second weapon; 1 / 2 barehanded) + stat 111, in 256ths. The weapon's own
  enhanced damage is already in them (ItemStatCost op 13 applies stats
  17 / 18 to the item's own damage). One percentage `p` = the skill's
  enhanced damage + stat 25 (`damagepercent`) + str × StrBonus / 100 +
  dex × DexBonus / 100 + mastery damage (`madm`), at least −90. Then
  `min' = min + min × (stat 18 + p) / 100`, `max' = max + max × (stat 17 + p)
  / 100`, a roll between them, times SrcDam / 128.
- Deadly strike (stat 141), critical strike (stat 337) and mastery crit
  (`macr`) are **separate** rolls against rand(100); any one doubles the
  physical damage.
- Item elemental damage: fire 48/49, lightning 50/51, cold 54/55, magic
  52/53, each with its mastery (329 / 330 / 331 / 357), × SrcDam / 128.
  Poison (57/58 over 59, with poison mastery 332) is split by stat 326.
  Cold length += stat 56, stun += stat 66.
- Leech: life stat 60, mana 62, stamina 64 (a monster's scaled by SrcDam).
- Conversion (record byte +0x65): a % of physical moved to an element
  (Fists of Fire and friends); cold / poison conversions give at least 50
  ticks.

## Corrections applied (phase 0)

1. Hit chance rounds as game.exe does: percent first, then × 2 × alvl /
   (alvl + dlvl); negative defense adds to the attack rating.
2. Critical strike (337) and deadly strike (141) are separate rolls, either
   doubling; the mastery crit joins them with skills.
3. Enhanced damage counts only from the weapon and what's socketed in it
   (op 13); stat 25 joins the strength/dexterity bonus, stat 111 adds to
   both ends; barehanded gets no stat bonus.
4. Defense vs melee / missiles (33 / 32) apply to swings / spikes.
5. Dodge (standing, swings), avoid (missiles) and evade (moving) roll after
   block; they're 0 until passive skills give them.
Not needed: monster AR's `dex × 5` (MonStats monsters have no dexterity).

## Approximations still in the port (marked `ponytail:` in code)

- Crushing blow: a quarter of current life less physical resistance, with
  no boss or difficulty divisors.
- Monster hit recovery: at an eighth of max life. Player: a twelfth.
- FHR and FBR act as animation-rate bonuses, not the per-class breakpoint
  tables.
- Damage reduced %: capped at 50, applied before the flat reduction, which
  can reach 0.
- Monster elemental damage: El1..3 MinD/MaxD taken as MonLvl DM
  percentages, like the physical damage.
- Mana regeneration: all of max mana in 120 s, times (100 + bonus) %.
- Open wounds: 1.10's per-level table for 8 s. Leech: × MonStats Drain.
- Not in yet: skills (skills.md), monster life regeneration (MonStats
  DamageRegen), set bonuses in the stat sums, the weapon swap, cold slowing
  the player, poison length reduction, champions/uniques.
