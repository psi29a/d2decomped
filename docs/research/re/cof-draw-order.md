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


## Draw effects

A COF layer record is 9 bytes from +0x1c: type, shadow, selectable,
transparent, draw effect, weapon class. `FUN_00470ec0` reads transparent
with `FUN_004db050` (byte +3); when it is set, the layer's draw mode is
its draw effect (`FUN_004db140`, byte +4, 5 when the layer isn't there).
Otherwise the mode is 5. The mode goes to the renderer's cel draw (slot
0x84, `FUN_004f6480`), whose `FUN_006c8250` (GDI; `FUN_00511d70`
DirectDraw) picks a blend table from the array `FUN_004fb010` builds out
of the PL2 (`FUN_006c8000` keeps it at 0x98a040):

| Effect | Table | PL2 | Blend |
|---|---|---|---|
| 0 | [2] 0x80e468 | +0x23500 | a quarter of the layer, three quarters of the screen |
| 1 | [1] 0x7fe468 | +0x13500 | half and half |
| 2 | [0] 0x7ee468 | +0x3500 | three quarters of the layer |
| 3 | [0x41] 0x7de468 | +0x33500 | additive |
| 4 | [0x42] 0x821b68 | +0x43500 | multiply |
| 5 | none | — | opaque |
| 6 | [0x43] 0x831b68 | +0x5b500 | max-component |

The row writer (`FUN_00608110`, through `FUN_00608540`) reads
`T[screen * 256 + sprite]`. Fitted against the installed act 1 palette,
table 0's screen share is a quarter, table 1's a half, table 2's three
quarters. The shares are from the data, not from `FUN_00605880`'s weights.

In the 1,064 COFs of the listfile, transparent layers use effect 1
(monster tokens LG and E9, layer 1), 3 (239 layers) and 6 (layer 8 of
monster token EG's COFs and object token PY's). d2d's `draw_mode`
(world_view.hpp) matches 0..5; it draws effect 6 opaque (deviations.md
approximation 9).

Re-check:

```sh
cd tools/emu && uv run python cof_effects.py | diff - ../../tests/cof_effect_cases.inc
cmake --build build --target test_cof_effects && build/tests/test_cof_effects
```
