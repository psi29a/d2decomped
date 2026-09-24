# Automap — game.exe 1.14d (`UI\automap.cpp`)

## AutoMap.txt (`FUN_0061fcf0`)

Each row:

    LevelName, TileName, Style, StartSequence, EndSequence, Type1..4 / Cel1..4

- `LevelName` indexes game.exe's level-type names at `0x6e7d50`
  (16-byte strings): 0 "None", 1 "1 Town", 2 "1 Wilderness", …,
  35 "5 Lava". These match LvlTypes ids; `FUN_0061fc10` does the lookup.
- `TileName` indexes the orientation names at `0x6e7f90` (8-byte
  strings): 0 fl, 1 wl, 2 wr, 3 wtlr, 4 wtll, 5 wtr, 6 wbl, 7 wbr,
  8 wld, 9 wrd, 10 wle, 11 wre, 12 co, 13 sh, 14 tr, 15 rf, 16 ld,
  17 rd, 18 fd, 19 fi (`FUN_0061fc80`).
- Records are 32 bytes:

      int level_type | int orientation | u8 main (0xff any) | u8 sub_lo | u8 sub_hi | u8 | int cel[4] | int n_cels

## Adding cells

`FUN_0061fff0(level_type, orientation, main, sub)` matches a record and
returns `cel[rand % n_cels]` from the game's RNG (`FUN_0045c3e0`).

`FUN_00457cf0` adds one cell per tile, once (tile flag 0x40000):
- The tile's level coordinates go through `FUN_00643310`:
  `x = (tx - ty) * 80`, `y = (tx + ty) * 40` (world pixels).
- Both are divided by 10.
- Lower walls (orientation > 15) get y + 24.
- The cell stores `{cel, x, y}`.

Cel sets: act 1 uses `UI\AutoMap\MaxiMap` (1499 cels of 16×32); act 2
`Act2Map`; act 4 `Act4Map`; LoD town `ExTnMap`. The `…S` variants are
used when the automap-size option is "small" (`DAT_007a5150` = 1,
divisor 20 instead of 10).

## Drawing (`FUN_00459700` → `FUN_00459440`, four passes)

- `scroll = player world px / div - screen / 2 + (40, 15)`.
  - With a side panel open, x shifts by ∓W/4.
- `screen = cell * 10 / div - scroll`, drawn via `FUN_004f6510` (DC6,
  bottom-left anchoring), clipped to the screen (or to the small map's
  box).
- Cells within ~150 px of the centre use fade draw modes 0/1/2 (the
  "AutoMapFade" option).

## Unit marks (`FUN_0045a860` → `FUN_0045a7f0`)

The mark is a 13-point polyline at `0x6d6638`:

    (0,-1) (2,-2) (4,-1) (2,0) (4,1) (2,2) (0,1) (-2,2) (-4,1) (-2,0) (-4,-1) (-2,-2) (0,-1)

It is drawn doubled, as lines at `(px/div - scroll_x + 8, py/div -
scroll_y - 8)`.

Colours come from `FUN_004fb180(a, b, c)`, a nearest-palette search
against BGR palette bytes, so the arguments are (B, G, R):

| who | global | colour |
|---|---|---|
| you | `0x7a51b0` | red |
| party | `0x7a51b3` | green |
| other players | `0x7a51b1` | blue |

`FUN_00459bc0` picks the colour and decides whether the unit shows.
Your own mark sits next to the campfire and stash miniatures in the
town map, which confirms the (40, 15) scroll offsets.

## d2d

- Tab toggles the automap.
- Tiles within 12 of the player are revealed as you walk; D2 reveals by
  room.
- Cel choice is a tile hash, not the game's RNG.
- Your own mark is drawn.
- Not yet: the fade, NPC/party marks, the small mode, panel shift.
- devctl `debug automap` reveals the whole level.
