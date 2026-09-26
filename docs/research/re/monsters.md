# Monsters — spawning (1.14d game.exe)

Our port: `components/rules/monsters.hpp` (`monster_region`, `populate_room`,
`monster_stats`), loaded by `load_monsters` (apps/d2d/load.hpp), tested by
`tests/test_monsters.cpp`.

## Tables

- MonStats rows are 0x1a8 bytes (`[0x744304]+0xa78`, count `+0xa80`). Offsets
  used here: `+0x02` BaseId, `+0x0c..0x0f` flag bytes, `+0x20` spawn,
  `+0x26/+0x28` minion1/2, `+0x2c/+0x2d` PartyMin/Max, `+0x2e` Rarity,
  `+0x2f/+0x30` MinGrp/MaxGrp, `+0x31` sparsePopulate.
- MonStats and MonStats2 are **not** row-aligned (734 vs 609 rows in 1.14d):
  MonStats `MonStatsEx` names the MonStats2 row.
- 1.14d stats are percentages of MonLvl.txt: HP = MonLvl HP × minHP..maxHP / 100
  (zombie1 level 1: 7 × 101..181 % = 7..12). We read the LoD `L-*` columns.

## Monster region — FUN_005479c0 / FUN_005475e0

One region per level (0x2e4 bytes, `MonsterRegion.cpp:0x87`): density
(`+0x2b8`, Levels MonDen for the difficulty), MonUMin/Max (`+700/+0x2bd`),
MonWndr (`+0x2be`), monster level (`+0x2dc`, MonLvl or MonLvlEx).
FUN_005475e0 draws `min(NumMon, 13, list size)` types without replacement
from mon1.. (normal) or nmon1.. (NM/hell), keeping each whose MonStats
flag (`+0xc`) allows it as `{row, rarity}` (0x34-byte entries from `+0x14`,
count `+0x12`, rarity sum `+0x11`). The seed is one step of the game seed
(FUN_00547d20).

## Room population — FUN_0054ec90

Per room rect (tiles × 5 = subtiles), `(h/3)·(w/3)` rolls of the **game**
seed: `seed % 100000 <= density` (density capped at 10000) spawns:

1. FUN_005bde80: `room_seed(total rarity) + 1`, walk the entries subtracting
   rarity until < 1. (MonStats `spawn` may replace the pick 80 % of the
   time for flagged monsters; no act 1 wilderness monster has one.)
2. FUN_005be020 (room seed): unique/champion if under MonUMin (chance rising
   with rooms done) or under MonUMax (6 %); else a group. Its 1/2 results
   both become "group". Normal act 1 has MonUMin/Max 0 — one roll, a group.
3. Group size MinGrp..MaxGrp, but 1..1 for BaseId 19 (fallen1) and 91
   (scarab1) — FUN_0054ec40. sparsePopulate: `game_seed % 100 > sparse` skips.
4. FUN_0054df80 → FUN_0054dc40: up to 20 random spots in the room rect
   shrunk one subtile at the top left (room seed), skipping any within
   WarpDist² (Levels.txt, 2025) of the level's entrances (FUN_0054db50),
   testing placement (FUN_005b2a00 with flag 1 = test only).
5. The leader at that spot; FUN_005b2830 adds PartyMin..PartyMax minions
   (minion1/minion2 alternating, radius 4) — how Fallen come in packs;
   then `rand(max-min+1) + min - 1` more of the type (radius 3).

FUN_005b2a00 places by rings of 3 subtiles out to 3 × radius (radius −1:
the point only), each walked from a random point round the square (room
seed: parity bit picks the axis, `rand(c)` the offset, two sign bits), the
first spot inside the room whose collision (FUN_0064d9b0, MonStats2
spawnCol → mask 0x3c01 / 0x1c0 / 0x3f11 / 0) is clear.

## Champions and uniques

- **The roll** (FUN_005be020, room seed) per spawn hit: while the level has
  made fewer uniques than MonUMin (region +0x2c8 vs +700), a unique when
  `rand(100) < rooms_done·100 / rooms_in_level` (+4, counted by
  FUN_0054ebc0 each room; +0xc); else while under MonUMax (+0x2bd), a unique
  when `rand(100) < 6`; else one more roll, 1 or 2, and FUN_0054ec90 turns 1
  into 2: a group. Normal act 1 has MonUMin / MonUMax 0; the Blood Moor
  has 4 / 5 in nightmare, 7 / 9 in hell; the Den of Evil none.
- **The unique** (FUN_0054ec90 → FUN_005a43e0(game, room, 1, 0, 0, 1)): a
  type picked again (FUN_005bde80 unique flag: nightmare / hell from the
  region's rarity list; normal from Levels.txt umon1..), placed like a
  group's leader (FUN_005a09e0 → FUN_0054dc40), counted (FUN_005a0320).
- **Champion or unique** (FUN_005a0760, the monster's own seed at unit
  +0x20): `rand(100) <` MonUMod row 0's `constants` (20) → champion (flags
  |= 4) with one champion mod (FUN_005a0500: MonUMod rows with `champion`
  set, `cpick` weights for the difficulty); else `rand(1) + 1 + difficulty`
  unique mods, at most 8 in all (FUN_005a0600: rows without `champion`,
  `upick` weights, none twice). A row is allowed (FUN_005a03e0) when
  enabled, not an LoD row in a classic game, the monster isn't of an
  `exclude1/2` MonType, and fPick 2 (multishot) isn't on a melee monster
  (fPick 1 / 3 test FUN_0046c140).
- **Its company**: a champion (FUN_0054e1e0) brings `rand(3) + 1` more of
  its type at radius 4, each a champion with mod 16 (FUN_005a48c0); a
  unique (FUN_005a0c00 from FUN_005a2120) brings `rand(4) + 3` of MonStats
  minion1 (else its own type) at radius 3, flagged minions (0x10).
- **What the mods do** (FUN_005a2120: fixed mods 1..4 for uniques, 0x6e2168,
  then its own, through the table at 0x73c008), with MonUMod's
  `constants` column (row: 0 champion chance, 1–3 minion +hp%, 4–6
  champion +hp%, 7–9 unique +hp%, 10 champion +tohit%, 11 champion +dmg%,
  12 minion +tohit%, 13 unique +tohit%, 14 minion +dmg% (strong), 15
  unique +dmg% (strong), 16–21 minion elemental min / max %, 22–27
  champion's, 28–33 unique's; each triple by difficulty):
  - 1 rndname: the name seed (monster data +0x14) from the monster seed;
  - 2 hpmultiply (FUN_005a0dc0 → FUN_005a0d20): life += life × (+hp% row);
  - 4 leveladd (FUN_005a0e40): level + 3, experience × 5;
  - 16 champion (FUN_005a0e80): level − 1 (net + 2), experience − 2/5 (×
    3), +dmg% (stat 0x19) and +tohit% (0x77) from rows 11 / 10 scaled by
    the difficulty's +0x34 percentage, velocity + 20 %;
  - 5 strong (FUN_005a17e0): rows 15 / 13 (unique) or 14 / 12 (minion);
  - 6 fast (FUN_005a1910): velocity % = 2048 / MonStats Velocity − 128,
    10..100;
  - 9 / 17 / 18 fire / lightning / cold enchanted (FUN_005a1990 & co):
    element min / max = MonLvl damage for its level × (elemental rows) %,
    then FUN_005a1370;
  - FUN_005a1370, unless the monster already has two immunities: 8 magic
    resistant +40 cold, fire, lightning (each under 100); 9 / 18 / 17 / 23
    +75 fire / cold / lightning / poison; 25 mana burn +20 magic; 27
    spectral hit +20 cold, fire, lightning (each under 75); 28 stone skin
    defence × 2, damage reduction + 50.
- **Superuniques** (FUN_005a49b0): SuperUniques Mod1..3 (mod 24 skipped),
  then `difficulty` more unique mods (FUN_005a0600, none twice), then
  FUN_005a2120 as for any unique (mod 30 aura also runs FUN_005a1650).
- **Random unique names** (client, FUN_004ac870): on `{name seed, 666}`
  (the seed mod 1 wrote from a step of the monster's seed, low 16 bits),
  a UniqueSuffix then a UniquePrefix into string 0x6b9 ("%0 %1"); then
  `rand(100) < 50` builds it again from an appellation, a suffix and a
  prefix into 0x6ba ("%0 %1 %2"): "Bane Poison the Hunter".
- Loot: champions MonStats TreasureClass2, uniques TreasureClass3,
  superuniques their SuperUniques TC (by difficulty).

d2d: `components/rules/uniques.hpp` (`roll_boss`, `boss_stats`),
`populate_room` with a `Population`; the app populates each difficulty at
load and applies the stats when a game's monsters are made. Not proven
against game.exe: the monster's own seed isn't the game's (unit creation
isn't emulated), and the behaviour mods (auras, enchanted death
explosions and charged bolts, spectral hit's damage, teleport, mana
burn, multishot, curses, ghostly / fanatic / possessed / berserk) aren't
traced or built: their code is in the combat and AI paths, not the mod
table (which only sets stats). A champion's label isn't traced either.

## Not yet traced / approximated

- Room activation order (game.exe populates when a room first becomes
  active; we do every room at load in cell order) and the game seed itself
  (we use the map seed).
- The seed at `+0x20` used for party and group counts (we use the room's).
- The `spawn` replacement, level def
  `+0x31`'s first-pick retry.
- The monster's component roll and stat init.

Seed 3's Blood Moor: 155 monsters (107 fallen1, 24 quillrat1, 24 zombie1).
