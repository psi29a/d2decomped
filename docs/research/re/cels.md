# Cel placement (DC6 and DCC frames)

Where game.exe 1.14d puts a cel on screen, given an anchor (x, y).

## The cel header

A decoded cel is the DC6 frame header as game.exe keeps it
(`SRC/CelCmp.cpp` getters):

| Offset | Field | Getter |
|---|---|---|
| +0x04 | width | `FUN_006018c0` |
| +0x08 | height | `FUN_006018f0` |
| +0x0c | offset_x | `FUN_00601920` |
| +0x10 | offset_y | `FUN_00601950` |

DCC frames end up in the same header. The DCC direction decoder
`FUN_0060bff0` reads each frame's fields with the bit-width table at
0x6e5f70 into 0x2c-byte records at 0x969020 (variable0, width, height,
x_offset, y_offset, ...). It copies the first five into the output cel
unchanged (the `puVar9[0..4]` copy before `FUN_0060bdb0`). So a DCC
frame's y_offset is the cel's +0x10, with no box arithmetic in between.

## The anchor

The GDI and DirectDraw cel draws (GDI `FUN_006c84b0` / `FUN_006c85a0`,
DirectDraw `FUN_00511fb0` / `FUN_005120a0`: slots 0x84 / 0x88 of the
renderer table picked from 0x72da80) cull, then call `FUN_006014c0`
(asserts `SRC/CelCmp.cpp`). The Glide and Direct3D tables (0x72f178,
0x74bf28) aren't read here.

    left   = x + offset_x
    bottom = y + offset_y          // the cel's last row
    rows   = height
    if bottom - height < 0: rows = bottom + 1          // clipped at the top
    if bottom >= screen_h:                             // clipped at the bottom
        skipped = bottom - screen_h + 1; rows -= skipped; bottom = screen_h - 1
    if rows <= 0: draw nothing
    dest = surface + bottom * pitch + left             // EDX into the row writer

It then calls the row writer for the cel file's kind (table 0x6e3688;
kind 0 is `FUN_00608540`) with the left column, skipped and rows. The
writer draws upward from `dest`. The cel covers rows
`y + offset_y - height + 1` .. `y + offset_y`, which is
`blit_at_anchor`'s rule (apps/d2d/common.cpp). The cull in the renderer
slot (`y + offset_y >= 0` and `y + offset_y - height < screen_h`) is the
same box.

Not traced here: when arguments 4 and 5 are both zero, `FUN_006014c0`
also skips a cel whose left column is negative instead of clipping it.
The GDI path passes 0x98a168 / 0x98a16c there, and what sets those
isn't traced.

## Re-check it

```sh
cd tools/emu && uv run python cel_anchor.py | diff - ../../tests/cel_anchor_cases.inc
cmake --build build --target test_cel_anchor && build/tests/test_cel_anchor
```

`cel_anchor.py` runs `FUN_006014c0` on 399 anchors, offsets and heights,
including cels clipped at the top and bottom of an 800x600 screen. It
hooks only the row writer. `test_cel_anchor` draws the same cels with
`blit_at_anchor` and checks the left column and the first and last rows.

## DCC cell decode and DC6 RLE pack (`SRC/Codec.cpp`)

`FUN_0060bff0` decodes a DCC direction using the 16-entry bit-width table
at `0x6e5f70` (`0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 26, 28, 30, 32`,
matching `components/dcc/dcc.hpp`), the LSB bit-mask table at `0x6e6030`,
and two helpers:

- **`FUN_0060b7b0`** (ESI = dst cell `{pb_entry, pixels, pitch, w, h}`,
  ECX = `&pb_ptr`, EDX = equal-cells bit reader or 0, stack =
  `(pixel_reader, prev_cell)`, `ret 8`): when the equal-cells reader is
  present, `prev_cell[0] != 0`, and its next bit is 1, copies `prev_cell`
  into `dst` through a 25-byte stack buffer (overlap-safe, no-op when
  `dst == src`) if `(w, h)` match, or zeroes `dst` if dimensions differ.
  Otherwise computes `bits_per_pixel` (`0` if `pb[1] == pb[0]`, `1` if
  `pb[2] == pb[1]`, else `2`), fills with `pb[0]` for 0-bit or reads
  `bits_per_pixel` LSB-first per pixel via `FUN_0060b200`, and advances
  `*pb_ptr += 4`.
- **`FUN_0060bdb0`** (ECX = frame record, EDX = cel header, stack =
  `out_buf`, `ret 4`): packs the decoded frame (`+4` width, `+8` height,
  `+0x20` pixels, `+0x24` pitch, `+0x28` bottom-up) into DC6-style RLE
  (flipping rows bottom-to-top when `bottom_up == 0`, transparent runs
  `0x80 | min(len, 0x7f)`, opaque runs `min(len, 0x7f)` + pixel bytes,
  row terminator `0x80`). Returns 1 if the end pointer equals
  `cel_hdr + 0x20 + *(cel_hdr + 0x1c)`, else logs `"Sprite Decompression
  Error"` (`0x6e6138`) via `FUN_00410610` and returns 0.

Re-check: `cd tools/emu && uv run python dcc_decode_helpers.py --exe /path/to/Game.exe --mutation-check` (13 synthetic cases, 6 mutations; `FUN_0060b200`, `memcpy` and `memset` run for real; `FUN_00410610` hooked).
