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
  game seed (game +0xd0), its unit seed `{low, 666}`.
- A critter is never made: FUN_0054e490 returns at once when
  FUN_004638a0(row, 0xd), MonStats2's flag 13 (`critter`), is set. The
  camp's chickens (MonPreset act 1 id 3) cost nothing; its cows do.

So the game seed after the town is the seed stepped once per object and
non-critter monster of the rooms up. d2d: `populate` (gamedata.cpp) for
a level without generated rooms; `load_npcs` gives each unit its room.
Checked in the emulator: `LEVELS=1,2,8,4,9 uv run python diff_drlg.py
1-20 0 game`, the game and object seeds through the camp and every level
after it.

Not matched: with the camp up first, a Blood Moor room on its edge
shares edge tiles with the camp's rooms (FUN_0066e580's near list takes
the level next door), so its room1 seed differs from d2d's, which shares
only within a level. No roll lands on those rooms (FUN_0054ebc0 leaves a
room by a town unpopulated), so seeds 1-20 come out the same apart from
those seeds.
