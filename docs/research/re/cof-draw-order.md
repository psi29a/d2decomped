# COF draw order: which priority row a direction reads

A COF's priority table (after the 0x1c-byte header, the 9-byte layer
records and the per-frame events) holds, for each direction and frame,
the composite types in drawing order. Its rows are **not** in the DCC's
direction order.

## game.exe 1.14d

- `FUN_00470ec0` (the unit draw) gets the COF (`FUN_004db040`), the unit's
  64-step direction (`FUN_00620100`), and calls
  `FUN_004db2e0(..., &dir64, &row, ..., cof[2] /* directions */, 1)`.
- `FUN_004db2e0` → `FUN_00600e20(dir64, directions)`: the table at
  **0x6e55a0**, `[log2(directions) + 1][dir64]` dwords: round the compass
  one step at a time (16 directions: 0,0,1,1,1,1,2,2,... from south).
- `FUN_004db110(cof, row, frame)` = `cof + 0x1c + layers * 9 + frames +
  (row * frames + frame) * layers`: the priority run; the draw loop walks
  `cof[0]` (layers) entries of it. At 0x471114: `ECX` = the COF, `EDX` =
  `[EBP-0x58]`, the row `FUN_004db2e0` wrote.
- DCC frames take their direction from **0x6e45a0** (`FUN_00600c90`),
  the interleaved order (16: 4,4,8,8,8,8,0,0,0,0,9,... — 0 SW, 1 NW, 2 NE,
  3 SE, 4 S, 5 W, 6 N, 7 E, 8..15 between).
- d2d holds DCC directions, so the row for DCC direction `d` is
  0x6e55a0[x] for any x with 0x6e45a0[x] = d:
  8 → {1,3,5,7,0,2,4,6}, 16 → {2,6,10,14,0,4,8,12,1,3,5,7,9,11,13,15}
  (checked against both tables in the binary).

d2d: `d2d::cof::Cof::priority_row` (components/cof/cof.hpp), used by
`composite_frames` (apps/d2d/world_view.hpp); `test_cof` checks the
Barbarian's hand axe (BATN1HS) is behind him facing north and in front
facing the viewer.

Seen before tracing: facing north the weapon drew over the torso, facing
the viewer behind it (the row was taken as the DCC direction).

## A layer shorter than its COF

- Direction: `FUN_005ff760` (SpriteCache.cpp) decodes a DCC direction
  picked by `FUN_00600c90(dir64, the DCC's own directions)`, so an
  8-direction piece in a 16-direction COF draws at its nearest of 8.
- Frame: `FUN_005feb80` returns no cell when the frame is at or past the
  DCC's frames per direction (the cache record +0x24): that layer draws
  nothing on those frames, the others do.
- In the data: 29 paladin pieces (tower shield `pashtorwlhth` 8 frames
  in a 10-frame walk; claymore, flail, polearm walks; throws) and the
  Frozen Horror's death (`f0`, 13 of 14): bugs.md #29.
  `tools/anim_coverage.py --audit` lists them.

d2d: `composite_frames` (apps/d2d/world_view.hpp) does both.

## A layer's file

`FUN_005fe610` builds the path `<root>\<token>\<layer>\<token><layer><piece><mode><weapon>`
and picks `.dc6` for a hard-coded list of monster ids (0xf2 Mephisto,
0xfb, 0x16f, 0xf3, 0x11c, 0x14d, 0x220, 0x22f, 0x23a, 0x2c0, 0x2c1,
0x2c5), `.dcc` for the rest; overlay row 0x7a (`mephisto`) is a `.dc6`
too. `FUN_005fe990` loads it, and a file that isn't there is fatal
(error 0x413). d2d tries `.dcc` then `.dc6` (the same files) and draws
the other layers when neither is there.

