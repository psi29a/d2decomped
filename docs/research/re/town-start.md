# Town start and arrival spots — game.exe 1.14d

Where a player appears in town comes from DS1 **special walls**
(orientation 10 or 11), not from preset objects or Levels.txt.

## DS1 special tiles → the level's spawn list

While a room's tiles are built (code around `0x667d09`), each special
wall's packed tile dword gives main index (bits 20..25) and sub index
(bits 8..15). Main 30..33 append an entry `{x, y, index}` to the level
(list at level+0x2c, stride 12, count at +0x1d8), in level tile
coordinates:

| main | spawn index      |
|------|------------------|
| 30   | sub (0..4)       |
| 31   | sub + 5 (5..9)   |
| 32   | 10               |
| 33   | 11               |

This is why a search for `CMP x, 0x1e` finds nothing: the code does
`main - 0x1e`, a range check, and a jump table at `0x667eb8`.

Table `0x6eed88` (8-byte `{any_of_group, group}` records by index) makes
index 0 mean "any of 0..4" and index 5 mean "any of 5..9". Indices 10–13
are groups 2–5 on their own.

## Picking a spot

- `FUN_0061b060(act, level, index, &x, &y, size)`, the old
  `DUNGEON_FindActSpawnLocation`, calls `FUN_0066b2b0`:
  - In a town, index 13 means the waypoint: `FUN_0066ad80` finds the
    preset object whose objects.txt flag at +0x167 has bit 0x40.
  - Any other index goes to `FUN_0066ac40`, which counts the matching
    entries and picks one at random (the level's seed at +0x1c4).
- The tile is converted to subtiles (`* 5`, `FUN_00643560`), then `+3`
  on both axes.
- `FUN_0064e7b0` then finds the nearest free spot for the unit size
  (collision mask 0x1c09).

## Callers

| index | caller |
|---|---|
| 0 | game join / act entry (`FUN_005394a0`, `0x584a79`, ...) |
| 11 | portal code (`0x56cf6e`, `0x56d033`), next to TownPortal object 0x3b: the town-portal arrival spot |
| 12 | a Lut Gholein caller (`0x59e009`, level 0x28) |

Every Act 1 town variant (townE1/N1/S1/W1) has exactly one 30/0, one
32/0 and one 33/0. townE1: 30/0 at tile 30,13 (next to the stash and
campfire), 32/0 at 32,13, 33/0 at 34,20.

## The camp populating

The camp is one preset (Act 1 def 1, townE1-4, kind 2) split into 8×8
rooms (FUN_00666680), row by row, each put at the head of the level's
list (level +0x10): 35 for the 56×40 town (the DS1 is 57×41, its last
column and row an edge). Each room populates as it comes up
(FUN_0052d0f0) like any other, but nothing in it rolls:

- FUN_0054f060 steps the room's seed; no object groups (FUN_00552610)
  and no monster population (FUN_0054ec90) come of it, so the object
  seed (game +0x10f0) stays put through the town.
- FUN_005559a0 makes the room's preset units: the objects first (type
  2), then the monsters (type 1), each pass in the room's preset list
  order, the DS1's reversed; a unit flagged +0x1c & 1 is left out.
- Each unit made (FUN_00555230 → FUN_00552df0) takes one step of the
  game seed (game +0xd0), its unit seed `{low, 666}`. A monster's init
  then draws twice on that (the look, the life; a guard rogue up to 11):
  the town NPCs' thinks start two steps on (town-npcs.md).
- A critter is never made: FUN_0054e490 returns at once when
  FUN_004638a0(row, 0xd), MonStats2's flag 13 (`critter`), is set. The
  camp's chickens (MonPreset act 1 id 3) cost nothing; its cows do.

So the game seed after the town is the seed stepped once per object and
non-critter monster of the rooms up. d2d: `populate` (gamedata.cpp) for
a level without generated rooms; `load_npcs` gives each unit its room.
Checked in the emulator: `LEVELS=1,2,8,4,9 uv run python diff_drlg.py
1-20 0 game`, the game and object seeds through the camp and every level
after it.

With the camp up first, a Blood Moor room on its edge shares edge tiles
with the camp's rooms: the near list (FUN_0066c370 → FUN_0066be80) takes
the close rooms up in the levels next door too, appended and the list
sorted again, so FUN_0066e940 finds the camp's edge tiles and the room
rolls fewer picks on its seed (its room1 seed moves). d2d: `camp_rooms`
(gamedata.cpp) lays the camp's rooms up on its preset (LvlPrest def 1)
and hands them to `level_room_tiles` as `outside`; no DT1s, so the
camp's tiles are found, never picked. Checked: the same diff, every
room1 and room seed of seeds 1-20.

The camp's own rooms' room1 seeds (room1 +0x6c, FUN_006422a0: the room's
tile seed, the DRLG room's, stepped once more after its tile picks): d2d's
`keep_camp_seeds` keeps those for the first roll on a camp room (the hireling's
teleport spot, Cain), from `preset_room1_seeds` (`level_room_tiles` on the camp's
preset with LvlPrest def 1's DT1 list, the DRLG room seeds from
`generate_preset`, the rooms up so far first). They depend on the order the
rooms came up in (a shared edge's re-pick rolls on the later room's seed). Checked:
`uv run python camp_room1.py 1-4 --mutation-check` (game.exe's rooms up in list,
reverse and shuffled order against `drlg-dump ... 1 <dir> camp`). Not done: the
seeds are taken once, with the rooms up at the first roll (`keep_camp_seeds`'
ponytail), and the client's critters (apps/d2d/session/ambient.cpp) still use the
DRLG room's seed.

The camp's tiles: each cell's tile is the rarity pick (FUN_0066d820) on its
room's DRLG seed, the picks of `level_room_tiles` on the camp's preset (LvlPrest def 1,
DT1 list by its Dt1Mask, the three always-lists after). d2d's `lay_camp_picks`
(gamedata.cpp) lays them once, rooms in list order, into `GameData::town`'s
`Level::picks` through `lay_tiles` and re-runs `finish_level`, so drawing and the walk
grid read the picked tiles where load_town's first match in the DT1s stood. Checked:
`uv run python camp_tiles.py 1-8 --mutation-check` (every camp room brought up in game.exe
against `drlg-dump ... 1 <dir> camptiles`, the tile sets equal). Not done: the order the
rooms come up in as a player walks (shared edges re-pick by it; relevel doesn't run on the camp).
