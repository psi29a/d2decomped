# Walls and roofs as the player moves

## Walls in front fade (FUN_004dd060, FUN_004dd180)

Each frame the client sorts a room's tiles into lists (`FUN_004dd7c0`):
- group bits 14..16 of the tile's flags 0 (floors): plain;
- orientation 15 (roofs): the roof list (`FUN_004dd460`), by group;
- orientations 16..19 (lower walls): their own list (`FUN_004dd350`);
- every other wall: `FUN_004dd180`, which fades it.

Every wall has a non-zero group: the DRLG sets (tile word >> 18 & 3) + 1
(`FUN_0066dc50`). `FUN_004dd060` has two modes, picked by `DAT_0072a968`.
In 1.14d it is a static 1 that nothing writes (read live under Wine:
1), so the room mode is the only one that runs:

- **Room mode (1.14d's):** a wall's tile +0x10 is its area record
  (`FUN_0066c9c0`: the record at the wall's cell in the room's grid,
  room +0x64 +0x1c; 0 in a room without one). A wall is in front when
  that record is set, its id (+0x28) isn't the player's (`DAT_007c8a0c`:
  `FUN_0061b130` → `FUN_0066ce30`, the record at the player's cell),
  and the player's cell is before the record's top-left in x or in y
  (`DAT_007c8a08 < rec[0]` or `DAT_007c8a10 < rec[1]`). Only a preset
  with LvlPrest Logicals has records (`FUN_00666ac0` → `FUN_0066d110`,
  the areas by walls; drlg.md); every other room is one area
  (`FUN_0066ccb0`) and its walls never fade. Seed 574303463: towns 1,
  40, 75, 109 have none; the Barracks (28) all 48 rooms, the Kurast
  Bazaar (80) 48 of 80, the Pandemonium Fortress (103) 12 of 12, a
  Blood Moor or Cold Plains house a room or two.
- **Cell mode (dead in 1.14d):** a wall 1..3 cells past the player's
  cell (`DAT_007c8a08` / `DAT_007c8a10`, the player's subtile / 5) in x
  for orientations 1 4 5 7 8 10 12, in y for 2 3 6 7 9 11 12.

A wall in front fades from alpha 0xff to 0x80 at 0x7f per 500 ms, one
that no longer is fades back; the tile's alpha byte (+0x28) is drawn
through `FUN_004f6950` when it isn't 0xff. Tiles with flag 4 (word bit
3, DT1 material bit 0) never fade (`FUN_004dd180`).

d2d: `wall_alpha` in `render_world`, over `Level::room_areas`
(drlg `logic_areas`).

## Roofs

Roofs never fade and aren't hidden by where the player stands: the roof
draw (`FUN_004dea70`, after the walls and units) skips only tiles flagged 0x400 (alpha 0) or 8
(hidden in the DS1: tile word bit 31); its timer that would set 8 (tile
+0x24 bit 2) is never armed in 1.14d. A player under a roof stays under it.

On screen, walls (upper and lower) stand 16 px higher than a floor's
frame would put them: measured live against game.exe in Lut Gholein
(floors, raised roofs, units and walls each on game.exe's pixels), not
traced to the entry y (`FUN_0066dc50`); lower walls follow the upper
ones untested.

d2d cuts a see-through circle in roofs round the player by default
(deviations.md improvement 4; `--toggle trans_roof=off` for game.exe's).

## Hidden tiles

A floor or wall word with bit 31 (the DS1's hidden bit; FillBlanks
fillers and style-30 sequence 0/1 floors get it too, FUN_0066e9b0) still
becomes a tile of the room, flagged 8 (FUN_0066dc50 walls, FUN_0066dde0
floors; shadows don't take it, FUN_0066df40). Collision stamps every tile
of the room whatever its flags (FUN_0064c790), so a hidden tile blocks;
the client's tile and wall draws skip `flags & 0x408` (FUN_004de410,
FUN_004dea70), so it isn't drawn. The camp's river edge is such walls.
d2d: `Level::Pick::hidden`, and the DS1 path stamps hidden cells.
