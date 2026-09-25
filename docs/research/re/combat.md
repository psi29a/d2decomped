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

## Approximations still in the port (marked `ponytail:` in code)

- Hit chance: 200·AR/(AR+DR)·alvl/(alvl+dlvl), clamped 5..95. Not traced.
- Player damage: flat damage added before the strength/dexterity bonus.
  Not traced.
- Crushing blow: a quarter of current life less physical resistance, with
  no boss or difficulty divisors.
- Monster hit recovery: at an eighth of max life. Player: a twelfth.
- Block: (shield + BlockFactor + item) × (dex − 15) / (clvl × 2), at most
  75, a third while moving. FHR and FBR act as animation-rate bonuses, not
  the per-class breakpoint tables.
- Damage reduced %: capped at 50, applied before the flat reduction, which
  can reach 0.
- Monster elemental damage: El1..3 MinD/MaxD taken as MonLvl DM
  percentages, like the physical damage.
- Mana regeneration: all of max mana in 120 s, times (100 + bonus) %.
- Open wounds: 1.10's per-level table for 8 s. Leech: × MonStats Drain.
- Not in yet: skills (skills.md), monster life regeneration (MonStats
  DamageRegen), set bonuses in the stat sums, the weapon swap, cold slowing
  the player, poison length reduction, champions/uniques.
