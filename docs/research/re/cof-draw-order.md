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

- A frame past the COF's own (an object's FrameCnt running on, bugs.md
  #30): `FUN_004db110` still indexes the priority run at that frame, so
  the bytes come from the next row or past the table. Each is matched
  against the COF's layer records (`FUN_004db7b0`, `*rec == type` while
  `< 0x10`): a byte naming no layer draws nothing, one naming a layer
  gets that layer's DCC past its frames, no cell. Every such object's
  DCCs end by the COF's last frame (`qo` NU: COF 1, DCC 1; `5h` NU: 13,
  13 for both layers; FrameCnt 13 / 20), so nothing draws whatever the
  bytes are: d2d's `past_cof` blanks the unit outright, the same.

d2d: `composite_frames` (apps/d2d/world_view.hpp) does both.

## A layer's file

`FUN_005fe610` builds the path of one layer's file into the caller's buffer
(register arguments: the stem from `FUN_005fe2b0`, the layer record in
ESI; the record's type is the dword at +8). An empty stem gives an empty
path and 0. Per type:

| Type (+8) | Path | Extension |
|---|---|---|
| 0 char, 1 monster, 2 object | `DATA\GLOBAL\CHARS` / `MONSTERS` / `OBJECTS` `\<token>\<layer>\<stem>` (token +0x18, layer +0x1c; a space byte becomes NUL) | `.dcc`, `.dc6` in the cases below |
| 3 missile | `DATA\GLOBAL\MISSILES\<name>` (name = the string at +0x2c; none gives empty and 0) | `.dcc`, `.dc6` when `CompressedData` is 0 |
| 4 item | `DATA\GLOBAL\items\<name>` (+0x2c; only when +0x10 is 3 or 4, else empty and 0) | always `.dc6` |
| 6 overlay | `DATA\GLOBAL\OVERLAYS\<name>` (+0x2c; needs bit 1 of +4, else empty and 0) | `.dc6` for row +0x14 = 0x7a, else as a missile |

Every `.dcc` it picks sets bit 0 of the record's +0x44 (`FUN_005fe990`
reads that bit: set = a DCC). The `.dc6` cases for types 0 to 2:

- `DAT_00741efc` is 0. That is the registry value `CompressedData` under
  `Diablo II` (`FUN_005fe280`; the default passed is 1). With it 0 every
  layer is a `.dc6`.
- Monster rows (+0xc) 0xf2, 0xfb, 0x16f, 0x209 and 0x2c0, whatever else.
- Monster rows 0xf3, 0x11c, 0x14d, 0x220, 0x22f, 0x23a, 0x2c1 and 0x2c5 when
  record +0x10 is 0.
- Object rows 0x156 and 0x233.
- A stem equal (`__strnicmp` through `FUN_00413590`) to `OYTRlitTNhth`,
  for a char, monster or object: a `.dc6`, and bit 0 of +0x44 cleared.
  `FUN_005fe2b0` has no such test. No file of that stem is known here.

`FUN_005fe2b0` makes the same `.dc6` decisions (it returns before setting
bit 0 for them), so the two agree apart from that last stem. This
replaces the list in earlier notes, which left out 0x209, the objects and
the +0x10 test, and put 0x2c0 with the second group. Overlay row 0x7a
(`mephisto`) is a `.dc6` too.
`FUN_005fe990` loads it, and a file that isn't there is fatal
(error 0x413). d2d tries `.dcc` then `.dc6` (the same files) and draws
the other layers when neither is there, so it needs none of these rules;
`Scene::overlay_sprite` (apps/d2d/scene/render.cpp) reads row 0x7a's
`.dc6` through that fallback.

Re-check: `python3 tools/ghidra/fn.py 005fe610` (the two switches on
`*(esi+0xc)`, the roots and the format strings) and
`python3 tools/ghidra/fn.py 005fe280` (the registry value).


## Loading a layer (SpriteCache.cpp)

The chain from a layer record to its decoded cells, in call order. The
record (ESI in most of these) has the type at +8, row id +0xc, +0x10, the
five name dwords +0x18 token, +0x1c layer, +0x20 piece, +0x24 mode, +0x28
weapon, a name string pointer at +0x2c, flags at +4 and +0x44.

- `FUN_005fe2b0` (stem, buffer in EAX): types 0 to 2 copy the five name
  dwords' first three bytes each, stopping at the first space byte, and
  end with NUL; types 3, 4 and 6 copy the string at +0x2c (type 6 needs bit
  1 of +4; all need a non-null +0x2c). Any other type, or a missing name,
  gives an empty stem and 0. It also sets bit 0 of +0x44 (the DCC flag)
  under the same `CompressedData` and row rules `FUN_005fe610` uses (see
  "A layer's file"; the one difference is that stem test). d2d: not ported.
- `FUN_0040b430(file, line, flag)`: `FUN_0040a080(file, line)`, and when
  that returns 0, `FUN_00408900(2, file, line, "")`; it returns the pointer
  either way. This is the allocator the other docs cite by address (chat.md,
  drlg.md, collision.md); it was untraced itself. d2d: not ported (it
  uses its own allocation).
- `FUN_0040a620` (`.\Src\AsyncData.cpp`; the decompile has nine
  parameters, the first in ECX, and the caller's call shows eight, so the
  numbering below is the decompile's `param_N`): makes a 0x54-byte record (`FUN_0040b430`, line 0x128), zeroed,
  with `0x41737960` in +0x48 (the high 28 bits are a tag; bit 1 = it
  allocated the buffer itself with `FUN_0040a080`; bit 0, read by
  `FUN_0040a9e0`, = the read is done). It keeps `param_1` at +4, `param_7`
  at +0x44, the buffer (`param_5`, or a fresh one) at +0x3c and the size
  (`param_4`, or `FUN_00406930()` when below 1) at +0x40, and copies an
  optional 7-dword callback block (`param_6`) to +0x1c.
  When `param_2` is 0, or `DAT_0075af94` is 0, it reads
  at once (`FUN_00406900`), calls the block's first dword with (buffer, size,
  block) and returns the record. Otherwise it takes a queue entry
  (`FUN_0040a550`) and starts the read there; with no entry it frees what it
  made (`FUN_0040b480`, lines 0x184 and 0x186) and returns 0. If the first
  argument after ECX is non-zero and `FUN_00406940(0, 0)` is -1 the run
  ends with error 0x142 (`param_3`). `FUN_005ff760` calls it twice (with
  `.\SRC\SpriteCache.cpp` lines 0x369 and 0x379). d2d: not ported (it reads
  whole files); `tools/emu/sound_hdr_load.py` hooks it.
- `FUN_0040a9e0` (ECX = the record): the result buffer (+0x3c). Null ends the
  run with error 0x25f, a tag other than `0x41737960` in +0x48 with 0x260.
  If bit 0 of +0x48 is clear it calls `FUN_0040a960` (to finish the read)
  first, and a bit still clear is error 0x266. `FUN_005fec90` calls it.
  d2d: not ported.
- `FUN_0040aa40` (ECX = the record): the size (+0x40), after the same null
  (error 0x271) and tag (0x272) checks; it does not wait for the read.
  `FUN_005ff760` calls it right after the read it queues with the
  `FUN_005ff6e0` callback (line 0x369) and drops the result, so there it is only the tag check. d2d: not ported
  (`tools/emu/sound_hdr_load.py` hooks it).
- `FUN_005fec90(entry, record)` (the layer's sprite-cache record in EDI):
  works only when `record` and EDI are set, bit 0 of `*entry` is clear and
  EDI+0x28 is 0; if any of those fails, or `FUN_0040a9e0` gives 0, it falls
  through to the same fatal error as a bad header below (the error id is not
  visible in the decompile). Otherwise it zero-fills `+0x24 * +0x21 * 0x2c` bytes (`FUN_0040b430`,
  `.\SRC\SpriteCache.cpp` line 0x517: one 0x2c-byte cell record per frame
  of every loaded direction), takes the decoded blob from `FUN_0040a9e0`
  and walks one frame header per cell from the offset EDI+0x2c+(+0x22)*4.
  A header must have 0 in its first dword and +4 and +8 (the frame's
  size) at most 0x100; if not, it builds the layer's path into a 260-byte
  local (`FUN_005fe2b0`, `FUN_005fe610`) and ends the run with an error.
  Each good header's +0x18 gets its cell record, and the next header is
  `0x23 + [+0x1c] + [+0x14]` bytes on. At the end EDI+0x28 = 1. Called by
  `FUN_005ffe90`. d2d: not ported (the DCC decoder sizes its own frames).

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

The unit graphics record, the COF caches and the motion offsets around the
unit draw are in [gfx.md](gfx.md).
