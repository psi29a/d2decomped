# Collision — game.exe 1.14d

`.\COLLISN\Collisn.cpp`, 0x64c000..0x64f800.

## The grid (FUN_0064c900)

Each room owns a u16 per subtile (5x5 per tile), header `{x, y, w, h,
…}` then the cells (`FUN_00619730` gives the rect; allocated at
Collisn.cpp line 0x185). Building it, for every floor, wall and roof tile
of the room (`FUN_00619660/6a0/6e0`), `FUN_0064c790`:

- ORs the tile's 25 DT1 subtile flag bytes in (`FUN_0064c4c0`): subtile
  row r, column c of the tile takes flag **(4 − r)·5 + c** — the DT1
  stores subtile rows bottom-up;
- adds bits from the tile entry's own flags (+0x14 of a 0x30 record):
  0x02 → 0x10, 0x40 → 0x01, 0x80 → 0x04, over the whole tile
  (`FUN_0064c700`). Not in d2d yet — the DS1 bits behind them aren't
  mapped.

Bits: 0x01 blocks walking, 0x08 blocks the player; walking units test
mask 0x1c09 (walls 0x09 plus door 0x400, monster 0x800, player 0x1000 —
units stamp their own footprints). Outside every room reads 0x27.

## Rects and units

- `FUN_0064d0d0` makes a unit's rect from its position and size:
  `x − size/2 … x − size/2 + size − 1` (same for y), inclusive.
- `FUN_0064cc30` tests a rect against a mask, `FUN_0064cd70` sets bits,
  `FUN_0064cce0` clears them; `FUN_0064ceb0` splits a rect across rooms.
- Point tests by collision pattern (`FUN_0064d870`): 0 one subtile
  (`FUN_0064d450`); 1/3/5 a plus — the subtile and its four neighbours
  (`FUN_0064d100`, edges clipped per room); 2/4 bigger (`FUN_0064d4a0`).
  Players and small units use the plus.
- `FUN_0064e7b0` → `FUN_0064dea0`: nearest free spot (spawns, the town
  start).

## d2d

- `Scene::world_walk` is built like the room grid (flipped rows), one grid
  for the whole DS1; objects and standing NPCs stamp 0x01 over their
  size rect.
- `Scene::unit_blocked` is the plus test with mask 0x09.
- Pathing: `rules::find_path` (8-way A* over subtiles) + string-pulling
  (`walk_path`) for the player, NPC approaches and the merc. D2's own
  pathing (Path.cpp) isn't traced; this stands in for it.
- Not yet: the tile-entry flag bits, units blocking each other (0x800 /
  0x1000), doors.
