# Walls and roofs as the player moves

## Walls in front fade (FUN_004dd060, FUN_004dd180)

Each frame the client sorts a room's tiles into lists (`FUN_004dd7c0`):
- group bits 14..16 of the tile's flags 0 (floors): plain;
- orientation 15 (roofs): the roof list (`FUN_004dd460`), by group;
- orientations 16..19 (lower walls): their own list (`FUN_004dd350`);
- every other wall: `FUN_004dd180`, which fades it.

Every wall has a non-zero group: the DRLG sets (tile word >> 18 & 3) + 1
(`FUN_0066dc50`). A wall is "in front" (`FUN_004dd060`, the default
mode) when its cell is 1..3 cells past the player's cell (`DAT_007c8a08`
/ `DAT_007c8a10`, the player's subtile / 5):
- in x, for orientations 1 4 5 7 8 10 12;
- in y, for orientations 2 3 6 7 9 11 12.

The other coordinate isn't checked. A wall in front fades from alpha 0xff
to 0x80 at 0x7f per 500 ms, one that no longer is fades back; the tile's
alpha byte (+0x28) is drawn through `FUN_004f6950` when it isn't 0xff.
A second mode (`DAT_0072a968` set) compares the tile's group with the
player's (`FUN_0061b130`); not built.

d2d: `wall_alpha` in `render_world`.

## Roofs

Roofs never fade and aren't hidden by where the player stands: the roof
draw (`FUN_004dedf0`) skips only tiles flagged 0x400 (alpha 0) or 8
(hidden in the DS1: tile word bit 31); its timer that would set 8 (tile
+0x24 bit 2) is never armed in 1.14d. A player under a roof stays under it.
