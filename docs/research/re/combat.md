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

## Drinking a potion

C→S 0x20 (inventory, FUN_0055e170) and 0x26 (belt, FUN_00562390) both go
through FUN_005bf240, which runs the item's pSpell (Items +0x94) from the
table at 0x741790 (8 bytes an entry, the second the server's): pSpell 3,
FUN_005be3f0, healing / mana; pSpell 5, FUN_005beac0, rejuvenation.
FUN_00562390's shift flag gives a belt potion to the hireling
(FUN_00574ec0(7, 0)), types hpot 0x4c, apot 0x50, wpot 0x51 only.

FUN_005be3f0, for each of the item's stats (Items +0x9e, calc +0xa4):
- `amount = calc`; `hpregen` (0x4a) `<< 8` through FUN_0062a5d0, the
  class's life bonus (amazon, paladin, assassin ×1.5 as `v + (v >> 1)`;
  barbarian ×2; others ×1; a non-player ×2); `manarecovery` (0x1a)
  `<< 8` through FUN_0062a620 (amazon, paladin, assassin ×1.5; sorceress,
  necromancer, druid ×2; others ×1). No HealthPotionPercent in 1.14d.
- if vitality (life) / energy (mana) > 0: `r = rand(stat) >> 1`, and
  `rand(100) < r` doubles it (the unit seed, +0x20).
- `<< ISC ValShift` (0 for both); classic only: FUN_005c6870.
- into the state (Items +0x98) for `len` (calc +0xb0) frames, joining
  what's left of it: `left = end − now`, per frame
  `(old × left + amount) / (left + len)`, lasting `left + len`; it ends
  through FUN_0056e900.

FUN_005beac0: each stat a percent of the max (ISC +0x32's stat) by MulDiv
(FUN_00483360), 100 the max itself, capped; no class bonus, no roll.
d2d: rules::potion_amount / potion_rate, Fight::potion.

## Picking up

C→S 0x16 → FUN_00548b00 (an item: within 5, a clear path); action 0 is
FUN_00563560's auto-place, else to the cursor (FUN_0055cf50).
FUN_00563560:
1. FUN_0055cc90 may it be taken; not: message 0x13 (impossible).
2. gold: FUN_0055c850.
3. stacking, FUN_00560020: a scroll (0x16) into an inventory tome of its
   Books kind with room (FUN_0063c3b0, FUN_0055ffa0 / FUN_0055ef20); a tome
   (0x12) into one (FUN_0055d370): past its maxstack the picked one keeps
   the rest on the ground; a stackable AutoStack type into the inventory's
   matching stacks (FUN_0055d0d0, FUN_0063c200, the match FUN_0062c850:
   class, quality, flags, stats 0x15–0x18, 0x9f, 0xa0; max FUN_006295b0:
   maxstack + stat 254, ≤ 511), the rest placed on.
4. FUN_0055d710: into a free body location it fits.
5. the belt: FUN_0062bad0 (own type Beltable), FUN_00628ba0 (autobelt, or
   a bottom match not isc / tsc: FUN_0063c560), FUN_0063c790 →
   FUN_0063c600: 1×1; columns 0–3 whose bottom item matches (FUN_00628a40:
   the same code, or both in hp1–5 / mp1–5 / rvs, rvl, 0x744660–0x744698)
   take their lowest free box below the belt's boxes; else, autobelt, the
   first free bottom box; FUN_0063afd0 places it.
6. the inventory: FUN_005600a0 → FUN_0063b950; no room: message 0x17.

Messages voice on the client (FUN_004cb9c0): the class's table off
0x72a008 (amazon 0x727eac), 0x13 → +8, 0x14 → +0x18, 0x15 → +0xc,
0x16 → +0x10, 0x17 → +0x14 (cantcarry, amazon sound 0xb76), 0x18 → +0x1c;
the same sound within 0x4b of FUN_0044db00's clock is skipped.
d2d: rules::pick_up, Loot::take. ponytail: no step 4; stacks match by
code; the voice's repeat guard; the doubling rolls d2d's rng; no
shift-click to the hireling.

## Regeneration — who can die to poison

- **Players**: FUN_00580810 (event 3 callback, re-armed every frame) calls
  FUN_00580610: `life += hpregen`, capped at max life; then
  **`if (life < 0x100) life = 0x100`**. Negative regen (poison, burning) can
  never take a player below 1 life. Then mana (FUN_005806f0) and stamina
  (FUN_00580500; char-panel.md, the HUD).
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

## Death

- **As the player dies** (the death mode, `FUN_00580ec0` → `FUN_00535ab0`;
  a monster's kill):
  - experience (`FUN_005359f0`): DifficultyLevels DeathExpPenalty (0 / 5 /
    10) % of the level's span, exp_next[level] − exp_next[level − 1] (the
    Experience.txt thresholds, `FUN_00611800`), never below the level's
    start + 1;
  - gold (`FUN_005357d0`): min(level, 20) % of the purse and the stash
    together; in single player (game type 3) no more than what leaves level
    × 500, and only from the purse; the rest of the purse falls where the
    player dies (`FUN_00535510`, split into piles by `FUN_0055a090`); the
    purse goes to 0 and goldlost (stat 175) holds the loss.
- **When the death has played** (mode 0x11: `FUN_00581020` →
  `FUN_0057fca0` → `FUN_0057f700`, PlrModes.cpp): a corpse unit, the
  player's class in mode 0x11, at most 16 a player. Everything worn (body
  locations 1..12) and the item in hand go onto it; its experience stat is
  75% of what the death took.
- **Resurrecting** (C → S 0x41, `FUN_0054c0e0`): hardcore ends the game;
  otherwise life, mana and stamina full, and the player goes to the act's
  town at its start.
- **Taking one's corpse** (`FUN_0057fb70`): its experience back to the
  player; each item (`FUN_00562f30`) to its body location when that's free
  and its requirements are met, else to the inventory; the corpse goes
  when nothing's left on it.
- **Saving:** the d2s keeps a corpse list after the player's items ("JM",
  count, then 12 bytes and an item list each; `FUN_00533850` makes them
  again on joining).
- **The look:** a death COF has only a TR layer and only LIT pieces (no
  BATRHVYDTHTH); a composite piece missing in a mode is drawn as LIT.

d2d: `World::death_penalty` / `make_corpse` / `take_corpse_items`
(server.hpp), the View's `corpses` (drawn lying, clickable as -3000 - k),
d2s `parse_corpse` / `write_save(..., corpse)`.
ponytail: the gold pile isn't split; requirements aren't checked when
items go back; a saved corpse lies by the camp's start (the save's x / y
aren't read); goldlost isn't kept.

