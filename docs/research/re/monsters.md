# Monsters — spawning (1.14d game.exe)

Our port: `components/game/objgroups.cpp` (room population), `components/rules/monsters.hpp`
(`monster_region`, `monster_stats`), loaded by `load_monsters` (components/game/gamedata_load.cpp), tested by
`tests/test_monsters.cpp`. How they notice the player and when they think:
monster-ai.md.

## Tables

- MonStats rows are 0x1a8 bytes (`[0x744304]+0xa78`, count `+0xa80`). Offsets
  used here: `+0x02` BaseId, `+0x0c..0x0f` flag bytes, `+0x20` spawn,
  `+0x26/+0x28` minion1/2, `+0x2c/+0x2d` PartyMin/Max, `+0x2e` Rarity,
  `+0x2f/+0x30` MinGrp/MaxGrp, `+0x31` sparsePopulate.
- MonStats and MonStats2 are **not** row-aligned (734 vs 609 rows in 1.14d):
  MonStats `MonStatsEx` names the MonStats2 row.
- 1.14d stats are percentages of MonLvl.txt: HP = MonLvl HP × minHP..maxHP / 100
  (zombie1 level 1: 7 × 101..181 % = 7..12). We read the LoD `L-*` columns.
- **Stat init** (`FUN_00573cb0`, Monster.cpp): level = MonStats Level
  (`+0xaa` + difficulty; Nightmare / Hell in an expansion game take the
  area's level, `FUN_0061dca0`, unless noRatio or flag `+0xc & 0x40`);
  resistances from `+0x144..`; then `FUN_006538a0`(row, L- columns,
  difficulty, level, mask, out): the MonLvl row (`[0x744304]+0xb70`, 0x78
  bytes: AC, TH, HP, DM, XP groups of 6 ints, classic then L-, by
  difficulty; level at most the last row) × the MonStats % / 100
  (`FUN_00483360`), or the MonStats value as written for a noRatio row
  (`+0xc & 4`, `DAT_006ce270`). Life = min + rand(max − min + 1) on the
  unit seed (+0x20), + the /players bonus % (`FUN_00573930` →
  `FUN_005738f0`: 0, 0, 50, 100 .. per player count; experience
  `FUN_00573910` the same), at most 0x7fffff, stats 6 / 7 in 256ths;
  defense stat 31, experience stat 13. Per mode (`FUN_005a4f50`) A1 / A2
  / S1 damage and to-hit, and El1..3 MinD / MaxD as DM % the same way
  (length as written). Checked: `tools/emu/monstats.py` (every row ×
  difficulty, 2202/2202).
- **MonProp** (the end of `FUN_00573cb0`): `FUN_00573470` finds the row
  by MonStats `+0x1a` (MonProp; the table at `[0x744304]+0xc30`, count
  `+0xc34`, 0x138 bytes a row, loaded as `monprop` by `FUN_00658720` into
  `0x96c860`). The row is +0 Id, then per difficulty (`+4 + 0x60 ×
  difficulty`, the column `FUN_00573cb0` uses for the level) six
  {prop, par, min, max} ints, then the chances, a byte each at `+0x124 +
  6 × difficulty + slot`. The slots run in order up to the first prop
  below 0. A slot with chance 0 always applies; any other steps the unit
  seed (+0x20, × 0x6ac690c5 + carry) and applies when the seed % 100 is
  below the chance. Applying is the item-property path,
  `FUN_0065fd70(0, 0, unit, 0, 0, 0, slot, 0, 0x40, 0)`. In the stock
  rows every chance is 0. Read from game.exe's own tables in the
  emulator: Mephisto (MonStats 242, 704) props 86 cast2 and 56 swing2,
  15 / 30; Blunderbore (186..189, 618, 619) 108 crush, 15 / 25; the
  Council (345..347) 244 extra-fire, 40 / 100; baalhighpriest (557) 60 /
  120; Blood Raven (267) 60 knock in Hell only; quillrat6..8 (605..607)
  54 thorns 15..20 / 30..40; baboon6 (295) 262 fade 1 / 2 / 2; druidhawk
  (419) 91 stupidity −1. No MonStats row names row 1 (irongolem, fade 16),
  and nothing else reads the table, so the Iron Golem's row never applies
  in 1.14d.
  - What they do (`rules::monster_props`, `game::anim_rate_pct`,
    `game::monprop_hit`): swing2 / cast2 are IAS 93 / FCR 105 on its A1 /
    A2 and SC rates (`FUN_00623f50` → `FUN_00621740`, 120 × v / (120 +
    v)). Thorns (78, damagedinmelee func 6, `FUN_005bf670`) puts its flat
    value on a melee striker (unit +0xc4 bit 2) as physical damage through
    `FUN_0057c6c0` → `FUN_0057c1e0`, so its damage reduction applies: the
    player's swings and the merc's (`Fight::merc_strike`), only off a blow
    that landed (`Fight::land`: none on a block, a miss, or a converted
    monster, which takes no blow, `FUN_00554200`). Knockback (81,
    func 7, `FUN_005bf960`) is a seed draw's low 7 bits under a bound,
    result flag 8: 0x40 against a player or a plain monster, 0x20 for a
    MonStats2 `large` one, 0x80 (always) a `small` one (byte +5 & 8 / & 4,
    masks [0x6ce274] / [0x6ce270]; the column list at 0x6580a5 puts large
    at flags bit 11, small at 10). Crushing blow (136, func 16,
    `FUN_005bffc0`) is a seed draw % 100 under the value: current life
    (stat 6) / a divisor, after the hit. The divisor: 10 for a player or a
    hireling (`FUN_0063ee90`: classes 0x10f, 0x152, 0x167, 0x230, 0x231);
    for another monster (a pet, or one made a foe by Confuse or Attract) 4,
    8 for a MonStats boss (+0xc & 0x40, `FUN_0063e9f0`) or a unique (data
    +0x16 & 2, `FUN_005a0180`), plus its monster_playercount share (stat
    0x64, at least 1, through `FUN_005738f0`: the table at 0x6e1590, 0, 0,
    50, 100 … %, then (n − 2) × 50); doubled for the missile event (6).
    The share it takes off is the raw damageresist (stat 0x24), 100 at
    most and none at 0 or less (`FUN_00483360`, × dr / 100), not a hit's
    50 % cap. Life is set to the rest (0 at most: result flag 2), state
    0x93 on when it took any. d2d: `Foe::knock_under` / `crush_divisor`
    (`game::monster_foe_props`), `game::crushing_blow`; a monster foe
    takes its crushes and knockback (`Fight::knock_back`) after the hits.
    Stupidity (`FUN_005bfaa0`) acts on monster targets only; fade is
    drawn by the client; fire mastery reaches only missiles the monster
    owns, and the Council's and baalhighpriest's fire comes from their
    Hydra's heads, so none of those three acts.

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
  list (act +0x10, next +0x7c) and the act marked (+0x54). Depth 3 loads
  the DT1 list and preset units (FUN_0061b320); depth 2 only sets the
  status (FUN_0061bb10). The walk, freeing and return: drlg.md "A room's
  life on the server".
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
camp's rooms are 8×8 splits row by row, the level's list newest first
(town-start.md "The camp populating"), and closeness stands in for the
border flags.

## Room population — FUN_0054ec90

Per area of the room's list (FUN_0061ad50: id +0x28 set, +0x20 clear, rect
not empty), `(h·5/3)·(w·5/3)` rolls of the **game** seed: `seed % 100000 <=
density` (density capped at 10000) spawns. A LvlPrest row with Logicals gets
its areas by walls (FUN_0066d110: flood FUN_0066c580 / FUN_0066c3d0, split
FUN_0066ca50, newest first); any other room one whole-room area
(FUN_0066ccb0, w × h tiles). Spots are drawn in the area (FUN_0054dac0) and
must carry its id (FUN_0061b130). Diffed bit-exact on every Logicals room of
act 1 (against the emulator's FUN_0061ad50 lists, seeds 1-3).
Each object an object group makes before (FUN_00552560) steps the game seed
once (FUN_00555230 → FUN_00552df0): the loop's start moves with them.

1. FUN_005bde80: `room_seed(total rarity) + 1`, walk the entries subtracting
   rarity until < 1. (MonStats `spawn` may replace the pick 80 % of the
   time for flagged monsters; no act 1 wilderness monster has one.)
2. FUN_005be020 (room seed): unique/champion if under MonUMin (chance rising
   with rooms done) or under MonUMax (6 %); else a group. Its 1/2 results
   both become "group". MonUMin/Max come from Levels +0x28 / +0x2b + difficulty
   (FUN_005479c0); normal Cold Plains has 1 / 1.
3. Group size MinGrp..MaxGrp, but 1..1 for BaseId 19 (fallen1) and 91
   (scarab1) — FUN_0054ec40. sparsePopulate: `game_seed % 100 > sparse` skips.
4. FUN_0054df80 → FUN_0054dc40: up to 20 random spots in the room rect
   shrunk one subtile at the top left (room seed), skipping any within
   WarpDist² (Levels.txt, 2025) of the level's entrances (FUN_0054db50),
   testing placement (FUN_005b2a00 with flag 1 = test only). The entrances
   (LAB_00642480): rooms with flags & 0x30000 or a warp slot that placed a
   warp, then the level's type 0xb tile: the waypoint's, or a Levels
   Position level's own tile (the Canyon of the Magi, WarpDist 3800; not
   its waypoint's too). d2d stands the waypoint's room in for the 0x30000
   rooms; every acts 1–3 level matches (`sweep.sh ... monsters`).
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
  into 2: a group. Normal Cold Plains has MonUMin / MonUMax 1 / 1; the
  Blood Moor has 4 / 5 in nightmare, 7 / 9 in hell; the Den of Evil none.
- **The unique** (FUN_0054ec90 → FUN_005a43e0(game, room, 1, 0, 0, 1)): a
  type picked again (FUN_005bde80 unique flag: nightmare / hell from the
  region's rarity list; normal from Levels.txt umon1..), placed like a
  group's leader (FUN_005a09e0 → FUN_0054dc40), counted (FUN_005a0320).
  FUN_005a0320 counts a unit as it first sets flag 8, so a superunique that
  is also an act boss (FUN_005b1cf0: the Smith, Coldworm) counts once.
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
  - the champion kinds (one of 16 / 36..39 by cpick, each weight 1):
    36 ghostly (FUN_005a1080): monster data +0x16 |= 0x40 (drawn
    see-through — see `### Ghostly blend (client flag 0x40)` below),
    damageresist (stat 0x24) set to 80, FUN_005a0e80 with speed −33 %,
    then cold (stats 0x36 / 0x37 += MonLvl DM, L-DM in an expansion game,
    for its level × constants rows 22 / 25 + difficulty %; 0x38 length
    += 150);
    37 fanatic (FUN_005a11f0): item_armor_percent (0x10) set to −70,
    FUN_005a0e80 with speed as `fast` (2048 / Velocity − 128, 10..100);
    38 possessed (FUN_005a1230): +0x16 |= 0x20, life +100 %
    (FUN_005a0d20, EDI = the %), FUN_005a0e80 (+20 %);
    39 berserker (FUN_005a1280): life −75 %, then damage % (0x19) and
    to-hit % (0x77) + 300 × DifficultyLevels +0x34 / 100 (the damage part
    halved when FUN_00463900 gives 0x76); no FUN_005a0e80: no level,
    experience or speed change. FUN_005a0e80's speed needs MonStats
    Velocity > 0. d2d: `boss_stats` (test_monsters).
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
| 23 poison hit | init `FUN_005a1e00` | poisonmindam / maxdam (stats 57 / 58) = MonLvl damage × MonUMod constants rows 0x10 / 0x13 + difficulty (0x1c / 0x1f with the unique flag) / 100, poisonlength (59) = (mlvl × 5 + 150) × 2, then `FUN_005a1370` (+75 poison resist); `FUN_0057b7d0` rolls 57..59 into every hit it lands. The Maggot Queen's (`FUN_005b1cf0`, BaseId 0x11c: mods 23, 22) |
| 20 scarab | 1 `FUN_005a2eb0`, 2 `FUN_005a2ee0` | into GH or DT: event 7 two frames on, then 8 missiles 225 (buglightning), 4 ways (0x6e21a8 / 0x6e2198) × 2 at its level (stat 12), steered by `FUN_005c9290`; no 10-frame limit |
| 10 poisondead | 1 `FUN_005a3800`, 2 `FUN_005a2c20` | on death, 4 frames on: missile 155 (corpsepoisoncloud) at its own spot, level = its level (at least 1), through `FUN_0056ecb0` |
| 24 thief | 3 `FUN_005a30e0` | on a hit; never reached: MonUMod disables it and `FUN_005a49b0` skips it in a superunique's list (Lord De Seis has it) |
| 13 rage, 11 durieldead | none | no init (0x73c008) and no hooks (0x73c0b8) |
| 33 suicideminion | 1 `FUN_005a3e70`, 2 `FUN_005a3ef0` | GH puts it straight into death; dead, 4 frames on, its A1 roll as physical plus aip1's element, within aip4 (monster-ai.md "Act 5's monster skills") |
| 14 / 15 / 21 / 31 / 32 / 34 / 35 / 40 .. 42 | 0x73c0b8 | not traced (traps, bone prisons, Act 5's deaths) |
| 25 mana burn | (spawn) `FUN_005a1f90` | manadrain min / max (stats 62 / 63) = MonLvl damage × `FUN_005a00f0`'s % |
| 26 teleport | spawn `FUN_005a1600`: skill 184 MonTeleport level 1, AI-control flag 0x20 (`FUN_005dd250`); AI `FUN_005b11f0` | at a think: rand(100) < 40 (its seed), then — life under 30 % (`FUN_00621f20`), or not flagged by `FUN_00457490(class, 1)` (monsters 10 / 0x159 / 0x22d count as flagged) and AI param +0x14 < 10 — rand(100) < 15: a free spot in its room (`FUN_0054dc40`), not in town; under 30 %, rand(100) < 25 and no state 0x34: life + mlvl (stat 6 + mlvl × 256); then cast 184 there |
| 30 aura | spawn `FUN_005a1650` | the table at 0x73bf68 {min mlvl, add, mul, div, skill}: Might 98 /6, Holy Fire 102 /6, Blessed Aim 108 /5, Holy Freeze 114 /7, Conviction 123 /8, Fanaticism 122 /8, Holy Shock 118 /8 from mlvl 20; pick = rand(rows reached) on {name seed (monster data +0x14), 666}; superunique 37 → Fanaticism; class 0x2c0 → Conviction 20; level = (add + mlvl) × mul / div, 1..99 |

- DifficultyLevels +0x34 / +0x30 (ChampionDamageBonus / UniqueDamageBonus,
  90 / 75 / 66) scale the champion and strong damage and to-hit bonuses.

d2d: `components/rules/uniques.hpp` (`roll_boss`, `boss_stats`,
`fire_blast`, `kSpectralElement`, `kBossBonus`), objgroups.cpp's `unique()`
with a `Population`; `make_boss` (ai.hpp) applies the stats and mana burn when a
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
- **Family mods** (`FUN_005b21b0`, from the spawn): by MonStats BaseId,
  every monster of the family, boss or not (`FUN_005a4850` with 0: not
  counted): brute2 13, scarab1 20, mummy1 10, Duriel 11 + 22, the traps
  (0x146..0x14a, 0x162) 14, the bone prisons (0x154..0x157) 15, Act 5's
  0x1b4 34, 0x1cd 33, 0x1f5 35, 0x21c..0x21e 22. d2d:
  `rules::family_mods`; the scarabs' bolts and the mummies' cloud run in
  `Fight::boss_events` (checked 2026-10-18 in a headless game: Dung
  Soldiers in the Rocky Waste, a Decayed in the Halls of the Dead).
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
| 0x04 | place_rogue_warner | MonStats 0x10a (Flavie) at the marker (radius −1 only); an NPC, but made here, and she blocks later spots |
| 0x05 | place_bloodraven | MonStats 0x10b (Blood Raven) |
| 0x08 | place_tightspotboss | MonStats 0x11c at the marker (radius −1 only), flag 8 |
| 0x0a / 0x0b | place_tentacle_ns / _ew | FUN_0054da60: MonStats 0x105's chain by the level (act 3) |
| 0x11, 0x12 | place_fallen, place_fallenshaman | one monster at the marker: base 0x13 (fallen1) / 0x3a (fallenshaman1) → the level's own (FUN_0063ec70) → FUN_0054e2a0 |
| 0x16, 0x17, 0x19, 0x1b..0x20 | fetish, fetishshaman, imp, minion, bloodlord, deadminion / imp / barb, reanimateddead | the same with bases 0x8d, 0x116, 0x1c5, 0x211 / 0x1ec, 0x20a, 0x1b6 (other acts); 0x1d..0x20 (bases 0x1c5, 0x1ec — 0x211 in level 0x6e —, 0x20a, 0x1b6; 0x54e9a0..) try radius −1 then 4 in mode 12 (dead), unit +0xc4 \|= 0x2000000; 0x1b6 then rolls rand(50) on its own seed for a FUN_005417d0 event |
| 0x18, 0x1a | place_impgroup, place_miniongroup | normal only: the level's imp (0x1ec, 0x211 in level 0x6e) / minion (0x1c5) by FUN_0063ec70, then FUN_0054e090: MinGrp..MaxGrp (+0x2f / +0x30) set, the leader through FUN_005b3040 (radius −1, flags 0: its party), then `rand(max − min + 1) + min − 1` on its seed more at radius 3 round it (FUN_005b2f70, flags 0) |
| 0x00, 0x06, 0x07, 0x09, 0x0c..0x10, 0x13..0x15, 0x21..0x24 | nothing, the river monsters, amphibian, fallennest, fetishnest, talking / dumb guards, maggots, mosquitonest, **group25..group100** | **nothing** (the default; group ids 0x21..0x24 are past the table: `index − 2 > 0x1e`) |

- **The level's own monster** (FUN_0063ec70(room, base)): the Levels.bin
  record's monster list (+0x33 count, +0x36 u16 MonStats rows): the first
  whose family base (MonStats +2) is the base's wins. Else the family's
  chain (MonStats +4, next in class) is walked while the next one's level
  (MonStats +0xaa) stays within the level's +0x16 + 1.
- **Preset swaps** (FUN_0054e600 on a MonStats row): in the Bloody
  Foothills (0x6e) with quest 0x1f's record open (`FUN_005444b0`: +9, set
  for every quest as the records are made, FUN_00545d80, and cleared only
  by `FUN_00544410` at the first join for a quest the player has done)
  0x1f2 / 0x205 become 0x1f3 / 0x206; past normal 0x1c5 / 0x211 there
  aren't made. FUN_0054e490: the prison door (0x1b2) a subtile west, in
  mode 12 once quest 0x20's record is shut; the barricade doors 0x1b0 /
  0x1b1 (MonType bit 0x12) each make object 571 / 572 on their spot. An
  evil hut leader (0x210, FUN_0054df80) makes object 562 (Dummy: a 5x5
  block in mode 0) after its party. A cage (object 473, InitFn 62) makes
  a5q2's five prisoners as it's made (monster-ai.md).
- **A superunique's company** (FUN_005a49b0 by its hcIdx, each through
  FUN_005b23c0 in its group): Radament (10), Shenk (0x2a: 20 minion1s
  within 20), Nihlathak (0x3c: 20 of the level's minion within 10), Baal
  Subject 2 (0x3e: 10 skmage_cold3s within 20).
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

## Nightmare and Hell (2026-10-17)

Every difficulty populates through objgroups.cpp's port (game.exe's to the
subtile; `sweep.sh` monsters, acts 1–4, `$DIFFICULTY` 0..2). What differs
from normal:

- **Mazes grow by difficulty**: LvlMaze.txt `Rooms` / `Rooms(N)` /
  `Rooms(H)`. `GameData::difficulty` (set with the map seed by
  `set_map_seed`) picks the column.
- **Density and MonUMin/Max** by difficulty (Levels `MonDen(N)`,
  `MonUMin(N)` ...), so NM / hell rooms roll uniques and champions where
  normal has none (the Blood Moor: 4 / 5 in NM).
- **The unique's type** (FUN_005bde80 with its unique flag set, from
  FUN_0054ec90 at 0x54ef0b and FUN_0054e600's preset codes 2 / 3 at
  0x54e873): normal picks one of Levels umon1..; NM / hell pick by rarity
  off the region like a group, with the placespawn swap at threshold 0
  (`rand(100) > 0`, a group's call passes 20): a crow nest unique is a
  foul crow.
- **Mods**: a unique's `rand(1) + 1 + difficulty` (FUN_005a0760); a
  superunique's `difficulty` more unique mods (FUN_005a0600) on its own
  seed before its minions, and MinGrp / MaxGrp each + difficulty when both
  are set (FUN_005a0c00).
- A boss's draws are all on its own unit seed (+0x20) after the ones it
  was made with (FUN_005a0c00 at 0x5a0c4a: ECX = unit + 0x20; FUN_0054e1e0
  likewise), and FUN_005a0320 counts every boss once (flag 8 marks it
  counted, champions too).
- A placed monster blocks the next one's plus-shaped test only on its own
  subtile (FUN_005b2a00 with pattern 2): a diagonal neighbour fits.

Not yet: tiles still come up in list order at load (drlg.md); in game.exe
they come up with the room1 as the player walks, so edge-shared tiles (and
the room1 seeds after them) can differ off that order.

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

### Ghostly blend (client flag 0x40)

Champion mod 36 (Ghostly, `FUN_005a1080`) sets a bit on the monster:
`pMonData[0x16] |= 0x40`. Client-side, this is the marker that says
"draw this unit see-through with an extra bright pass". No `Trans*.dat`
palette is involved — it's a runtime alpha, not a colour LUT.

Client sprite draw dispatch (`FUN_00470ec0` → `FUN_00471ec0`, source
string `.\ENGINE\Gfx.cpp` line 0x52b): picks a per-layer draw type
`local_c` and a `param_2` low byte (alpha), then calls
`FUN_004f6480(local_b4, x, y, alpha_and_flags, draw_type, palette)`.
Draw types seen in the branches: **5** normal alpha, **6** overlay, **7**
"bright alpha" (`FUN_004f6480` -> vtable slot at
`(*DAT_007c8cc0)+0x84`).

The type-7 branch has the doubled-alpha formula (`unit_draw.c:193-203`):

```c
if (local_28 == 0 && local_54 != 0) {           // bright pass gate
    uVar8 = (param_2 & 0xff) * 2;               // double the alpha
    local_c = 7;                                // draw type = 7
    if (uVar8 < 0x41)  uVar8 = 0x40;            // floor at 0x40
    if (uVar8 > 0xfe)  uVar8 = 0xff;            // ceiling at 0xff
    param_2 = (param_2 & 0xffffff00) | uVar8;
    FUN_004f6480(local_b4, x, y, param_2, 7, palette);
}
```

- `local_54 = FUN_00464370()` — cursor hover check ("is this the unit
  under the cursor"), which lighting.md confirms doubles a hovered unit's
  light. Not the ghostly gate.
- `local_28 = FUN_004db360(&local_c, local_40)` in
  `.\ENGINE\GfxUtil.cpp` — a state-driven draw-type override reading
  stat 0xb5 and the layer's state at `+0x12d`. This IS the state /
  ghostly path in game.exe, but the +0x16 & 0x40 bit itself isn't read
  here — the ghostly mod's link is through the stat / state pipeline
  (untraced this pass).

**Ponytail note:** the formula above (double the alpha, clamp
[0x40, 0xff], draw type 7) is the same shape D2 uses for both the
cursor highlight and the ghostly effect — the difference is which gate
sets `local_c = 7`. Wiring it in d2d for cursor hover is the smaller,
verified port; the +0x16 → 0x40 → type 7 hookup for champion-Ghostly
monsters needs a second trace of `FUN_004db360` and stat 0xb5's
setter (a state or a stat FUN_005a1080 sets after `|= 0x40`).

Concrete examples of monsters that carry `|= 0x40`: any champion mob
rolled with cpick 36 (Blood Moor fallen1 champions, Cold Plains
zombies etc.). No unique monster is Ghostly by default in vanilla —
it's rolled per-pack on champion mods only.

## Act 3's jungle (2026-10-13)

Checked: `diff_drlg.py 76|77|78 monsters` (rooms and seeds), `LEVELS=75,76,77,78
diff_drlg.py game`.

- **Water spawns.** FUN_005b2a00 hands a spawnCol 1 type (but rows
  0x102..0x107, the tentacles, and 0x99) to FUN_005b2700 instead of its
  ring walk: the room's floor list (room1 +8: entries +8, count +0xc;
  0x30-byte `{.., +8 x, +0xc y, .., +0x18 tile}`), from `rand(count)` on
  the room seed (0 taken as 1) round to the one before the start
  (never tried), a tile with DT1 material bit 2 (water, tile +6) whose
  middle subtile (x·5 + 3, y·5 + 3) has no 0x100 and a plus clear of
  0x1c09 at one of (0,−3) (3,0) (0,3) (−3,0) (0x6e2d50). The asked spot
  is ignored. FUN_0054dc40's 20 tries each check the area (FUN_0061b130)
  and WarpDist, then that scan. The Great Marsh's Swamp Dwellers.
- **Tentacles.** A leader of base 0x105 (FUN_005b2830 → FUN_005b2570):
  PartyMin..Max on its seed (FUN_004cc790), then a step `% 6`; each
  minion1 at its spot only (radius −1, flags 0x40) at 0x6e2cf0[6k + r],
  r going (r + 5) % 6; k 0 for a head placed with flags 4 (MonPlace 10,
  north-south), else 1.
- **MonPlace in FUN_0054e600:** 0x16 / 0x17 the level's 0x8d / 0x116
  (fetish, fetish shaman) as the Fallen are; 10 / 11 a tentacle head
  (0x105 down NextInClass by level: 0x54eb60 — 76–78 one, 92–93 two,
  else none), its spot only; 9, 0x0c, 0x0d, 0x13–0x15, 0x21–0x24 make
  nothing there.
- **The Dark Wanderer** (object 368, InitFn 43, FUN_005bd1f0, a3q7):
  MonStats 0x170 at the marker's x + 7, its spot only, once a game
  (a3q7's init, FUN_005bd300, sets the record's "due" byte; record 0x1c,
  word 0x20: data +1 due, +4 / +8 the spot). His think is MonAI 91,
  DarkWanderer (FUN_005ea130, search type 1). His spot (FUN_005bd0d0,
  worked out once, +2): 20 subtiles north, else 11 north, 2 east or west
  of that, else 8 north, else 3 north, the first clear of 0x3c01
  (FUN_006229f0). AI control +0x14 0 -> 1. A foe 20 or more off (the
  think's AI distance): stand 40. Under 20, state 1: walk there
  (FUN_005ded90), state 2. State 2: within 2 of the spot (FUN_005dc5c0),
  or after 3 more walks (+0x18, FUN_005de190(0, 0) first), unit flag
  0x20000, gone (FUN_005ddfc0), and the record (FUN_005bd4a0: +0xd, +0x10
  his id) two ticks on (FUN_005bd390) gives every player in act 3 word
  0x20 bit 0 (FUN_005bd260) and leaves 7 or 8 objects 0x83 ("vile dog
  afterglow", InitFn 59: lit, mode 1, gone 27 frames on) round his spot
  at (±3, ±3) (0x741538), from offset `seed & 1` on, each at a free spot
  near it (FUN_00545340, 0x3f11). The spot is where he went (his unit,
  FUN_00552f60 by the record's +0x10 id); the offsets (0x741538, index 7
  repeats index 2: bugs.md #20) start at the quest seed's next step & 1.
  d2d: ai.cpp's DarkWanderer, `gone`; world.cpp's afterglow (`fires`,
  object 131 in OP, 27 ticks). `tests/quests_d2d.py` walks him off (map seed 3).
- **Unkillable monsters** (MonStats killable 0: the Dark Wanderer, the
  compelling orb, chickens, bats, the camp's cow, traps) are made without
  unit flag 0x4 (game.exe's Dark Wanderer: +0xc4 0x8200a, a plain monster
  0x8200e), so FUN_005dca70 and the merc's search pass them over and no
  hit lands. d2d: `Monster::killable` in `targetable()`.
- The collision grid (FUN_0064c900) takes the shadow layer's DT1
  subtile flags too (act 2's desert's prickly shadows; Act 1's shadows
  carry none).

## Kurast and Travincal (2026-10-13)

Checked: `diff_drlg.py 79..83 monsters` 5/5 (446 + 97 rooms), `LEVELS=75,..,83
diff_drlg.py game`. Kurast needed nothing new. Travincal:

- **The compelling orb** (object 404, InitFn 60, FUN_005bbba0, quest
  0x13's record): unless the record's +0x28 says it's made, MonStats
  0x16e on the object's spot (FUN_005b3090 → FUN_005b2a00, radius −1:
  the spot only, mask by spawnCol); then +0x28 = 1, +0x2c its id; with
  +0xc set the object goes to mode 2. The InitFn (FUN_00555230 at
  0x5553fe) runs before the object's mode and footprint (0x55545d), so
  the object doesn't block its own orb.
- **The orb broken**: record 0x13's join (FUN_005bb480) sets data +0xc
  when a player's word 0x12 (Khalim's Will) has bit 0; striking the orb
  with the Flail (FUN_005bb980) sets it too. InitFn 60 then puts object
  404 in mode 2 (one frame, no light, no collision, not selectable) and
  still makes the orb monster (+0x28 is per game; checked in the
  emulator: mode 2, then 0x16e made). The records' "made" bytes (the
  orb's +0x28, Hratli's 0xe +0 / +1, Natalya's 0x14 +0x20) only stop a
  second unit when a room comes up again; d2d populates each room once.
- **FUN_005b1cf0** for base 0x16e: FUN_005436b0(0x13), unit flag
  0x20000, MonUMod 22. The whole switch, by BaseId (an uber by its own
  row): Andariel 22 + quest 6, uber (0x2c3) 23, 6, 29; Duriel quests 0xd,
  9, uber (0x2c4) 6, 18; Radament (0xe5) quest 8; Mephisto 22 + quest
  0x14, uber (0x2c0) 22, 30, 17, 8, 6; Diablo 22 + quest 0x17, uber
  (0x2c1) 22, 8, 6; the Summoner (0xfa) quest 0xc, flag 0x800,
  FUN_00573570; Izual 22 + quest 0x16, uber (0x2c2) 6, 18; Blood Raven 12,
  22, quest 2, stat 0x76; the Maggot Queen 23, 22; the Fire Golem
  FUN_005a4850(0x1f, 0); the bone prisons (0x154..0x157) flag 0x20000;
  the Smith 22; fetish11 (0x197) quest 0x11; Hephasto (0x199) quest 0x18;
  the prison door (0x1b2) quest 0x20; Nihlathak (0x20e) 22 + quest 0x22;
  the ancients (0x21c) FUN_005b1c50; Baal (0x220) 22 + quest 0x24, uber
  (0x2c5) 22, 18, 8, 6. d2d `rules::act_boss` has every mod list (read
  from the code; only Act 1's, the Maggot Queen's and the orb's diffed),
  not the ancients' or the Fire Golem's.
- The oracle needs `objgroups.QUEST_STUBS` here: the council's
  superunique specials read quest flags (FUN_00544760).

**The act in one game** (`LEVELS=75,..,83 diff_drlg.py game`, rooms up in
a shuffled order) needed three things the one-level diffs never hit:

- A room's floor list (FUN_00619660: room1 +8 → +8 entries, +0xc count)
  and its Logicals areas (FUN_0066d110, from the near rooms up as it comes
  up) depend on the order rooms come up, so both follow each relevel, as
  the tiles and room1 seeds did.
- FUN_005b2700 searches the room1 it's handed, on that room1's seed
  (+0x6c). Round a point (a pack's or a party's spot) that's the room
  holding the point, and a water spot can be on a room's shared edge row,
  the next room's: its floors, its seed stepped, even before it populates.
- Tiles shared across a level's edge (Travincal's bottom row on the
  Causeway's top) come up with the other level's rooms already up, as the
  camp's did for the Blood Moor.

**Area ids** (+0x28) carry across rooms. A room without Logicals
(FUN_0066ccb0) is one area of id 1 and sets the level's counter (+0x1dc)
to 1. A Logicals room (FUN_0066d110) labels from the counter (0 taken as
1; the first label one past it; after, one past the last), then
FUN_0066d040 joins it to each near room of its level that's up and
touches it (FUN_0066b860 gap 1): along its border, top and bottom rows
then left and right columns, edges included (FUN_0066cf60), a cell on
the other room too with both ids set, different and alike in +0x20 gives
this room's id the other's (FUN_0066c770: every area of the room with
that id, and, if any changed, the same in its near rooms of the level,
whole rooms aside). Lookups read the room's grid of records over its
(w+1)x(h+1) cells, the rects unclipped (+0x00), not the clipped +0x10.
So most open floor is id 1, level-wide.

FUN_0061b130(room1, x, y) is the id at a point: the room holding it, the
given one or one of its near list, any level (FUN_00463740); 0 when none.
Population compares it inside one room; mode 5's search (FUN_005dca70)
and the pet and merc thinks (FUN_005e45d0, FUN_005e52d0, the follow's
candidate spots) compare two points, so two outdoor levels' whole rooms
(both 1) count as one area, and a Logicals room's walled-off part as
another. d2d: room_tiles.hpp `logic_areas` / `merge_areas`,
`game::area_id_at`; `tools/emu/area_ids.py` calls FUN_0061b130 at every
tile as each room populates.

## Act 4's population (2026-10-16)

- **The knights' variant** (FUN_00573cb0, after the look, before the life
  roll): MonStats rows 0x137 (doomknight2, Abyss Knight) and 0x138
  (doomknight3, Oblivion Knight) roll `rand(4)` on the unit seed into
  monster data +0xe (0x138 also +0xf): the row DoomKnightMissile adds to its
  missile (FUN_005cdfb0: EDX = srvmissilea + that byte, undeadmissile1..4). One more unit-seed step, so their groups' counts come after it.
  d2d: `rules::monster_variant`, `Monster::variant`.
- **Trapped souls** (InitFn 46, FUN_005506d0, object 369 "trapped soul
  placeholder", from ObjGrp 46's scatter): on the object seed,
  ((w·h) >> 7)·30 >> 8 souls for the room; up to 12 tries of a spot
  rand(w − 4), rand(h − 4) in that FUN_00550220 takes (3x3: the room's
  margins, no 0xc01 in 10x10, no 0x3f11 in 3x3); a hit makes a soul (odd
  step 0x194 trappedsoul2, else 0x193) at the placeholder (FUN_005b2f20,
  mode 1, that spot only; made: the object to mode 2), then hops on from
  the random spot: hop k (from 1) goes on while rand(k >> 1) is 0, up to
  max(left, 4)·3 steps of dir = step & 7, (step % 5 + 5)·2·0x731b7c[dir]
  then the same for y (0x731b9c), until FUN_00550220 takes one (a soul
  there); none: rand(k >> 1) once more, and the try ends. d2d:
  objgroups.cpp `trapped_souls`; `monsters.py` counts each soul alone.
