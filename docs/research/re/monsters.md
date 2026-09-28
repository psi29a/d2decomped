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
flag bit 0 (`+0xc`, the isSpawn column) is set as `{row, rarity}`
(0x34-byte entries from `+0x14`, count `+0x12`, rarity sum `+0x11`). With
Levels rangedspawn (level def `+0x31`; Act 5 only) the first draw is
redrawn up to 20 times until it's a rangedtype (flag `+0xf` & 0x10).

Every level's region is made at game start, levels 1 up in Levels order,
on one seed: `{one step of the game seed, 666}` (FUN_00547d20 →
FUN_005479c0). With a fixed seed (-seed) the game seed is `{map seed, 666}`
(FUN_0052c280; otherwise QueryPerformanceCounter, and the map seed is
drawn from it), and nothing in game creation draws from it before.

After the draws, each type rolls up to 3 component sets on the same seed
(FUN_005bdb20, `+3` count, 16 bytes each from `+4`): a set is a pick per
layer among MonStats2's HDv..S8v (their list lengths are MonStats2
`+0x15`). The first rolls every layer with 2+ choices; the next ones copy
it and reroll layers a and b — `i = rand(n)` over the n varied layers,
a = L[i], L[i] = L[n-1], b = L[rand(n-1)] (a = b when n = 1) — up to 3
tries not to repeat a set. The cap is `1 << MonStats2 +0x25`: 1 set when no
layer varies, 2 when the only one has 2 choices, else 3 (true of all 609
rows).

Checked: `tools/emu/regions.py <seed> <difficulty>` runs FUN_005479c0; d2d
matches it on all 125 levels, 5 seeds, 3 difficulties (test_game keeps
seed 0x1234). A level-list name is the row at its place among MonStats'
distinct Ids (bugs.md #13).

## When a room populates

- **Game creation** (FUN_00530930) steps the game seed four times after
  seeding it (FUN_0052c280): the regions (FUN_00547d20), the object seed
  (FUN_00546c60), sunitproxy (FUN_00536070), quests (FUN_00545d80). Nothing
  else steps it inline; after that only room population (FUN_0054ec90)
  rolls it.
- **A room comes into play** when the player's room changes
  (FUN_0061a110 → FUN_0061b6f0 → FUN_0061b490 / FUN_0061b390): the new
  room's near list (room2 +8, count +0x2c) is walked three deep, depth
  first, each room's counters at +0xc (a u16 per depth) bumped and its
  status (+0x44) moved by the callbacks at 0x744384. Depth 1 (the near
  list itself) runs FUN_0061b2d0: no room1 yet (+0x30) → FUN_0061b190
  builds its tiles (FUN_0066ee40 / FUN_0066ee70) and its room1
  (FUN_006422a0 → FUN_00619890), put at the **head** of the act's room
  list (act +0x10, next +0x7c) and the act marked (+0x54). Depth 2 / 3
  only load tiles and presets (FUN_0061bb10, FUN_0061b320).
- **The near list** (FUN_0066c370): the level's rooms under 6 tiles apart
  on both axes, in the level's list order, itself included, bubble-sorted
  (FUN_0066bbc0); then, for a room flagged for a level next door (+0x28
  bits 0x10..0x800, one per Levels.txt Vis slot), that level's close,
  flagged rooms (FUN_0066be80), or with a warp between them the other
  side's first flagged room (FUN_0066be10); each appended and the list
  sorted again (FUN_0066bda0). So the camp's edge rooms bring up the
  Blood Moor's, and the Den's mouth room brings up the Den's first room.
- **Populating** (FUN_0052d160, each game tick while an act is marked):
  the act's room list from the head (newest first), every room1 without
  flag 1 (+0x34): its presets (FUN_005559a0), FUN_00542b40, FUN_00552610,
  then FUN_0054ec90, and the flag set. A player arriving through a warp
  (FUN_0056cf40) has the landing room made and populated at once
  (FUN_0052d0f0) before the rest.
- **The room seed** population rolls is the room1's (+0x6c, `{s, 666}`):
  FUN_006422a0 steps the room2's seed (+0x14/+0x18) once after its tiles
  were picked and takes the low word. Proven: `diff_drlg.py <range> 2|8
  seeds` (300 seeds each, the Blood Moor and the Den, rooms brought up in
  list order).

- **Every unit made steps the game seed.** FUN_00555230 (the server's
  unit maker, every type but players) calls FUN_00552df0(unit, game
  +0xd0): one step, the low word is the unit's init seed (+0x28) and its
  seed (+0x20) is `{low, 666}`. So a room's preset monsters, objects and
  warp tiles, and each monster population places, take a step between
  the density rolls; so do items, missiles, the merc and the camp's NPCs
  wherever they're made, which puts the stream's absolute state out of
  reach without modelling every unit (d2d steps only for what rooms make).
- **Its look** (FUN_00574250 → FUN_00573cb0 → FUN_005739d0, ECX the
  type's region entry from FUN_00547bc0, EDX the unit) is the first roll
  of the unit seed: `rand(entry +3)`, and that set's 16 bytes go to
  monster data +4. A type the region lacks rolls each of the 16 layers
  with at least one choice (MonStats2 +0x15.., FUN_006647c0), a
  one-choice layer included. Checked in the emulator: 2000 seeds.
  FUN_00573cb0 then rolls the monster's life on the same seed.

d2d: `Spawning` (components/game/gamedata.hpp, `start_spawning`,
`player_moved`), run by `Fight::rooms_up` every tick and on arrival. The
camp's rooms are 8×8 splits row by row (their order isn't traced), and
closeness stands in for the border flags.

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
- **The label under the name** (client FUN_004adea0, from FUN_00452580,
  from FUN_00454ad0): only for uniques (flag 8, FUN_004ae360) and minions
  (0x10, FUN_004ae380). It starts with "Demon" (0x275e, MonStats flag
  byte +0xd) or "Undead" (0x275d, FUN_0063e990), if either. A minion's is
  then "Minion" (0xc95, after 0xf9b). A unique's is then each of its mods
  (up to 9, in order) with a string in the u16 table at 0x725188, by
  MonUMod id: 5 Extra Strong 0xc85, 6 Extra Fast, 7 Cursed, 8 Magic
  Resistant, 9 Fire Enchanted 0xc89, 17 Lightning Enchanted 0xc8b, 18 Cold
  Enchanted 0xc8a, 24 Thief 0xc91, 25 Mana Burn 0xc8c, 26 Teleportation
  0xc8e, 27 Spectral Hit 0xc8d, 28 Stone Skin 0xc8f, 29 Multiple Shots
  0xc90, 30 Aura Enchanted 0xc92; the rest have none. Each word comes
  after the joiner at 0x7c0c58, a space: start-up zeroes it and
  FUN_004ac870 copies L" " into it. The line stops once it passes 480 px.
  Monsters 0x2c0..0x2c5 get no mod words.
- **A champion's name** (client FUN_004ac870, the handler at 0x724d78 for
  mods 1, 12, 16 and 36..39; FUN_004ad020 runs the fixed mods 1..4, then
  the monster's own, and the last write wins): "%0 %1" (0x2b40), with the
  word for its champion mod from the table at 0x6da488 and then its
  MonStats name. The words: 16 "Champion" (0xc94), 36 Ghostly, 37 Fanatic,
  38 Possessed, 39 Berserker (0x2b4c..0x2b4f). The search checks the
  first four keys; no match gives the fifth. So: "Champion Zombie",
  "Possessed Zombie". Champions get no label.
- **The warping shrine** (FUN_00583050 → FUN_005a4940): the nearest
  monster passing FUN_00582750 (a monster, not already a boss, ...)
  goes through FUN_005a0760 with champions allowed and FUN_005a2120:
  a champion or a unique, no pack.

## Boss mods in the fight

The stat mods (above) run once, at spawn. What a mod does in the fight
runs through the server's event table at 0x73c0b8: six hooks a mod
(`[mod * 6 + event]`), run by `FUN_005a4270` for each of the monster's
9 mod bytes, with the monster's flag 8 (unique) passed along. Most
handlers do nothing without it, so a unique's minions don't explode.

| Event | Run from | When |
|---|---|---|
| 0 | `FUN_005a7c20` (`FUN_005a4350`) | a mode change, before |
| 1 | `FUN_005a7c20` (`FUN_005a4360`) | a mode change, after |
| 2 | `FUN_005a4370`, the unit event 7 (0x6e2490[7]) | the event 1 handlers queue it (`FUN_005417d0(7, frame + n)`) |
| 3 | `FUN_0057c6c0` | its hit lands on a target |
| 4 | `FUN_0057cee0` | it takes damage |
| 5 | `FUN_0059fa30` (`FUN_005a43b0`) | it makes a missile |

| Mod | Hooks | What |
|---|---|---|
| 9 fire enchanted | 1 `FUN_005a25f0`, 2 `FUN_005a2620` | on death (mode 0), event 7 four frames on. Then the blast: missile 117 (monstercorpseexplode) for the look, and `FUN_0057e090` hits everyone within difficulty + 4 subtiles. v = max life × MonsterCEDamagePercent (DifficultyLevels +0x3c: 50 / 35 / 20) / 100, then ×3/4, ×2/3 or ×1/8 by difficulty. The roll runs from 60 % of v to v (the monster's seed), and the damage struct takes roll << 6 as both physical (+8) and fire (+0x10) |
| 17 lightning enchanted | 1 `FUN_005a37d0`, 2 / 4 `FUN_005a29a0` via `FUN_005a2ba0` | hit in GH: event 7 two frames on; hit otherwise: at once. Either way at most once every 10 frames (monster data +0x18, flag 0x100). 8 missiles 195 (lightunique), 4 ways (0x6e2188 / 0x6e2178: N, E, S, W) × 2, at level mlvl / 2 (at least 1), steered by `FUN_005c9290` |
| 18 cold enchanted | 1 `FUN_005a3800` (needs flag 8), 2 `FUN_005a2bd0` | on death, 4 frames on: a nova (`FUN_0056d400`) of missile 194 (coldunique) at level mlvl / 2, range from missile 119's |
| 7 cursed | 3 `FUN_005a2530` | on a hit, 3 in 4 (its seed): Amplify Damage (skill 66) at level mlvl / 5 + 1, radius from the skill's calc (1..40), through `FUN_0056dbc0` |
| 27 spectral hit | 0 `FUN_005a3040`, 5 `FUN_005a30b0` | at each mode change, and on a missile: one of five elements (0x6e21b8: fire 48 / 49, lightning 50 / 51, magic 52 / 53, cold 54..56, poison 57..59), min / max = MonLvl damage (DM, L-DM in an expansion game) × MonUMod constants row 28 / 31 % (the normal rows on every difficulty); cold and poison length + 40 |
| 29 multishot | 5 `FUN_005a3610` | a missile it makes: two more, from the same skill and level, aimed a subtile to either side (flag 0x80 while they're made) |
| 23 poison hit, 24 thief, 10 / 20 / 31 .. 42 | 0 / 3 / 1 + 2 | not traced (poison clouds, stealing, act bosses' deaths) |
| 25 mana burn | (spawn) `FUN_005a1f90` | manadrain min / max (stats 62 / 63) = MonLvl damage × `FUN_005a00f0`'s % |
| 26 teleport | spawn `FUN_005a1600`: skill 184 MonTeleport level 1, AI-control flag 0x20 (`FUN_005dd250`); AI `FUN_005b11f0` | at a think: rand(100) < 40 (its seed), then — life under 30 % (`FUN_00621f20`), or not flagged by `FUN_00457490(class, 1)` (monsters 10 / 0x159 / 0x22d count as flagged) and AI param +0x14 < 10 — rand(100) < 15: a free spot in its room (`FUN_0054dc40`), not in town; under 30 %, rand(100) < 25 and no state 0x34: life + mlvl (stat 6 + mlvl × 256); then cast 184 there |
| 30 aura | spawn `FUN_005a1650` | the table at 0x73bf68 {min mlvl, add, mul, div, skill}: Might 98 /6, Holy Fire 102 /6, Blessed Aim 108 /5, Holy Freeze 114 /7, Conviction 123 /8, Fanaticism 122 /8, Holy Shock 118 /8 from mlvl 20; pick = rand(rows reached) on {name seed (monster data +0x14), 666}; superunique 37 → Fanaticism; class 0x2c0 → Conviction 20; level = (add + mlvl) × mul / div, 1..99 |

- DifficultyLevels +0x34 / +0x30 (ChampionDamageBonus / UniqueDamageBonus,
  90 / 75 / 66) scale the champion and strong damage and to-hit bonuses.

d2d: `components/rules/uniques.hpp` (`roll_boss`, `boss_stats`,
`fire_blast`, `kSpectralElement`, `kBossBonus`), `populate_room` with a
`Population`; `make_boss` (ai.hpp) applies the stats and mana burn when a
game's monsters are made; `Fight::boss_events` runs the lightning bolts
and death effects each tick; `attack_starts` (spectral hit) and the
multishot and mana-burn hooks sit in `monster_update`.

- Built: fire, lightning and cold enchanted, spectral hit, multishot, mana
  burn, the difficulty bonus.
- Checked live on seed 3: nightmare's Gut Rend (lightning) let off bolts;
  Ash Shifter (fire) blew up for 95 + 95; Gray Maim burned 24 mana a hit.
- Simplified: a hit in GH fires the bolts at once, not 2 frames on; the
  bolts go straight; mana burn's % taken as the enchanted rows'.
- Built too (2026-09-27): Cursed (Amplify Damage on whoever it hits:
  damage reduced −100 % for its auralen), teleport (fight.hpp / ai.hpp
  `monster_update`), Aura Enchanted (`boss_aura`, `Fight::monster_auras`:
  Might / Blessed Aim / Fanaticism for monsters in range, Conviction on the
  foes, Holy Fire / Shock / Freeze strike every perdelay).
- Not built: thief (MonUMod disabled) and poison hit (not a random pick),
  so neither turns up in Act 1.
- Not proven against game.exe: the monster's own seed isn't the game's
  (unit creation isn't emulated).

## Preset units on the server — FUN_00555910 → FUN_005557d0 → FUN_0054e600

When a room comes up, each unit in its list (the DRLG's, docs/research/re/
drlg.md "Preset units") is spawned at the room's origin plus its subtile
(FUN_00555910 → FUN_005557d0; type 1 → FUN_0054e600, type 2 objects →
FUN_0054f490 above id 0x23d, else FUN_00555230). FUN_0054e600(game, room,
id, x, y, mode) — fastcall, id / x / y / mode on the stack:

- **MonStats row** (id < rows): FUN_0054e490, that monster at (x, y)
  (FUN_005b2f20 → FUN_005b2a00, radius −1 = the point, then radius 4 if
  it's taken, unless FUN_0054e3a0 says no: rows 0xe5, 0x11c..0x120,
  0x188, 0x189 stay put). Flag 8 when MonStats +0xe & [0x6ce270] (not
  traced). A few ids are swapped first: 0x1f2 / 0x205 with quest 0x1f in
  level 0x6e (act 5), 0x1c5 / 0x211 skipped in level 0x6e past normal.
- **Superunique** (id − rows < superunique count): FUN_005a49b0, below.
- **MonPlace** (the rest; MonPlace.txt is only codes, the meaning is this
  switch on `index − 2`, byte table 0x54eb30 into jumps at 0x54eb0c):

| Index | Code | What spawns |
|---|---|---|
| 0x02 | place_unique_pack | a type from the level's unique list (FUN_005bde80 unique flag), then FUN_005a43e0 with (0, 0): a **random spot in the room** (FUN_0054dc40), not the marker; mods and minions as a random unique |
| 0x03 | place_champion | a type from the unique list at the marker, made a champion (FUN_005a48c0 mod 16), then FUN_0054e1e0: `rand(3) + 1` more champions of it, radius 4 (the monster's own seed) |
| 0x04 | place_rogue_warner | MonStats 0x10a (Flavie) |
| 0x05 | place_bloodraven | MonStats 0x10b (Blood Raven) |
| 0x08 | place_tightspotboss | MonStats 0x11c, flag 8 |
| 0x0a / 0x0b | place_tentacle_ns / _ew | FUN_0054da60: MonStats 0x105's chain by the level (act 3) |
| 0x11, 0x12 | place_fallen, place_fallenshaman | one monster at the marker: base 0x13 (fallen1) / 0x3a (fallenshaman1) → the level's own (FUN_0063ec70) → FUN_0054e2a0 |
| 0x16, 0x17, 0x19, 0x1b..0x20 | fetish, fetishshaman, imp, minion, bloodlord, deadminion / imp / barb, reanimateddead | the same with bases 0x8d, 0x116, 0x1c5, 0x211 / 0x1ec, 0x20a, 0x1b6 (other acts); the dead ones spawn in mode 12 (dead) |
| 0x18, 0x1a | place_impgroup, place_miniongroup | FUN_0054e090 (act 5, not traced) |
| 0x00, 0x06, 0x07, 0x09, 0x0c..0x10, 0x13..0x15, 0x21..0x24 | nothing, the river monsters, amphibian, fallennest, fetishnest, talking / dumb guards, maggots, mosquitonest, **group25..group100** | **nothing** (the default; group ids 0x21..0x24 are past the table: `index − 2 > 0x1e`) |

- **The level's own monster** (FUN_0063ec70(room, base)): the Levels.bin
  record's monster list (+0x33 count, +0x36 u16 MonStats rows): the first
  whose family base (MonStats +2) is the base's wins. Else the family's
  chain (MonStats +4, next in class) is walked while the next one's level
  (MonStats +0xaa) stays within the level's +0x16 + 1.
- **FUN_0054e2a0**: fallen1 in level 6 (Black Marsh) → 0x14 (fallen2); in
  7, 12, 16 (Tamoe Highland, the Pit) → 0x15 (fallen3). fallenshaman1 in
  6 / 7 → 0x3b; 12 / 16 → 0x3c.
- **Superuniques** (FUN_005a49b0(game, room, x, y, index)): once a game —
  a bit per superunique at game +0x1d30, unless the record's +0x26 lets
  it come back (a guard returns for difficulty > 2). At the marker, or a
  random spot when the record's +0x24 is set
  (FUN_005a09e0: (0, 0) → FUN_0054dc40). Flagged unique (+0x16 |= 2),
  mods as in "Champions and uniques", then per SuperUniques Class (the
  record's +8) a few specials (the Countess, the Smith, Griswold's …:
  cases 6, 10, 0x1a..0x1d, 0x24..0x27, 0x2a..0x2d, 0x3c, 0x3e — spawn
  their company or set quest state; not traced one by one), and MonUMod 22
  (questcomplete) for all. **Minions** (FUN_005a2120 → FUN_005a0c00):
  `MinGrp + rand(MaxGrp − MinGrp + 1)` of MonStats minion1 (+0x26, else
  its own type), MinGrp and MaxGrp each + difficulty when both are set; a
  random unique's are the fixed 3..6. Each is placed by FUN_005b23c0 →
  FUN_005b2a00 at radius 3 round the leader (rings of 3 subtiles, see Room
  population) and flagged minion (+0x16 |= 0x10).
- **Act 1's presets** use (tools survey of every Act 1 LvlPrest file):
  place_fallen 76, place_fallenshaman 118 (the Blood Moor's Fallen Camp 1 /
  2 and Tree Fill, cave themes, cottages, the ruin), place_champion 16,
  place_unique_pack 27, place_bloodraven 1, place_nothing 8,
  place_fallennest 1, place_group25 / 50 / 75 / 100 (3 / 10 / 2 / 13: the
  Crypt, Jail, Catacombs, Fence Fill 1, Cottages 2); superuniques
  Bishibosh, Rakanishu, Griswold, Treehead, Coldcrow, Bonebreak, Boneash,
  Pitspawn, Corpsefire, the Countess, the Smith, the Cow King; monsters
  gargoyletrap 17, Andariel, chickens, cows, rogues, the town NPCs.

## Not yet traced / approximated

- Tiles still come up in list order at load (drlg.md): in game.exe they
  come up with the room1, as the player walks, so edge-shared tiles (and
  the room1 seeds after them) can differ from ours off that order.
- The seed at `+0x20` used for party and group counts (we use the room's).
- The `spawn` replacement.
- The stat init after the look (FUN_00573cb0: life on the unit seed,
  FUN_006538a0), and the unit seed's later rolls (champion / unique,
  party and group counts: the room's here).
- Random object groups per room (FUN_00552610: Levels ObjGrp0..7 /
  ObjPrb0..7, objgroup.txt, objects.txt PopulateFn through 0x731d00) aren't
  built: the Blood Moor's dead rogues and forest objects, and their steps
  of the game seed.

Seed 3's Blood Moor: 155 monsters (107 fallen1, 24 quillrat1, 24 zombie1).

## Colours (FUN_00466360, FUN_00477530)

The client picks a monster's colour index as it makes the unit:
1. MonStats `TransLvl` + 2 (TransLvl 8 and up: 2);
2. a unique (monster flag 8; superuniques too), unless MonStats2
   `noUniqueShift`: `rand(30) + 9` on the unit's seed (`FUN_00477620`);
3. MonStats2 `Utrans` for the difficulty, when set (0xff: `FUN_004791b0`);
4. a superunique's own SuperUniques `Utrans` for the difficulty, when set;
5. 30 and up: 2.

Drawn (`FUN_00477530`, the palette the unit draw passes): 0 and 1 none
(but for monster types 0x16b / 0x16c); 2..7 the table of the graphic's
`Data\Global\Monsters\<code>\COF\palshift.dat` (8 x 256: tables 0..2
plain, 3..6 Carver, Devilkin, ... colours); 8..29 `RandTransforms.dat`
table - 8 (30 x 256: reds, oranges, greens, teals, blues, purples). A
second set (+0x800) is used when a setting (+0x11c) is 2; untraced.

d2d: `World::monster_colour` (the roll on the unit id: same odds, not
game.exe's colour for that monster), `Scene::monster_map`. A state's
colour shift (states.md) replaces it while it lasts.
