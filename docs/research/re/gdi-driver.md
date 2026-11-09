# The software GDI video driver — game.exe 1.14d

Video mode 1 (`DAT_007c8cb0`; `-w` runs it, lighting.md "Drawing with it")
draws through a table of function pointers at `0x74c4a8`. Its entries are
copies of the DirectDraw driver's table at `0x72f6d0` (same slot offsets),
most of them byte for byte the same code at other addresses, so the
DirectDraw function is the one to read for what the slot is for. Re-check any
row with `tools/ghidra/fn.py <address>`; register arguments are where the
decompile reads them. `Src/gdiSmack.cpp` (`game-srcfiles.tsv`) is the file
the video slots come from; the DirectDraw twins' file is `Src/ddSmack.cpp`.

Slots read so far (GDI `0x74c4a8 + slot`; DirectDraw `0x72f6d0 + slot`):

| Slot | GDI entry | DirectDraw entry | What |
|---|---|---|---|
| `+0x18` | `FUN_006c7d80` | `FUN_00511720` | Takes the draw buffer: refused when `DAT_007c9144` is set; a fatal error (0x10d / 0x3bf) when the critical section `DAT_007c9134` doesn't exist; enters it |
| `+0x40` | `FUN_006c9710` | `FUN_00513400` | Opens a video |
| `+0x44` | `FUN_006c9900` | `FUN_005135f0` | One video frame |
| `+0x48` | `FUN_006c9640` | `FUN_00513330` | Closes the video |
| `+0x7c` | `FUN_006c95d0` | `FUN_005132c0` | Floor / roof tile draw |
| `+0xa0` | `FUN_006c93a0` | `FUN_005130a0` | Wall tile draw, block by block |

## Tile draw (`+0x7c`, `FUN_006c95d0`)

The renderer's tile draw (`FUN_004f68e0` hands its arguments to renderer
`+0x7c`, cof-draw-order.md / bugs.md #10) lands here in mode 1.

- It asks `FUN_005fdea0` to read the tile's block records on demand (dt1.md;
  its first argument is the tile, it returns 0 when it can't), and only if
  that answers does it call the callback `+0xc` of the table at
  `DAT_007c914c` (the game's own callbacks, lighting.md) with the
  decompile's `param_4` and `param_2`; the other arguments are in registers
  the decompile doesn't show.
- The decompile of `FUN_006c95d0` and of DirectDraw's `FUN_005132c0` is the
  same text, so the two drivers draw a floor tile the same way. The x
  adjustment bugs.md #10 puts in both (0x50 off the top corner) is not in the
  decompile: it passes its arguments in registers the decompile doesn't
  show, so that half of the row rests on the disassembly the row's author
  read, not on a re-read here. Cited, not re-proven.

## Clearing the draw buffer (`FUN_006c9280`, `FUN_006c9220`)

- `FUN_006c9280` is a thunk to `FUN_006c9220`. Its caller is the slot `+0x18`
  function `FUN_006c7d80`, which calls it only when its first argument is
  non-zero, after it has entered the critical section `DAT_007c9134`.
- `FUN_006c9220(flag)` zeroes the draw buffer `DAT_007c9154` (stride and
  width `DAT_007c9138`, height `DAT_007c913c`): all of it when the flag is 0,
  else the first height − 0x2f rows, so the bottom 0x2f rows (the panel strip
  the tile view box leaves out, automap.md) keep their pixels.
- DirectDraw's `+0x18` (`FUN_00511720`) locks the surface and has no such
  clear: the GDI draw buffer is a persistent one.

## Wall tile draw (`+0xa0`, `FUN_006c93a0`)

The same text as DirectDraw's `FUN_005130a0` (read side by side: the same
`FUN_005fdea0` load, view box, per-block test and callback `+0x18` of
`DAT_007c914c`), which d2d ports (automap.md "wall draw", `world_view.cpp`).
So the box it tests blocks against, and the side-panel halves, are the ones
written up there. It returns 1 when it drew a block, 0 otherwise.

## Videos (`Src/gdiSmack.cpp`: slots `+0x40`, `+0x44`, `+0x48`)

The state is a record `{ int smack; char kind; }` (the slot functions take
its pointer; `kind` at `+4` is `'s'` for a Smacker file, `'p'` for a still
picture, 0 for none), a pixel buffer `DAT_0098a278` and a 256-byte palette
map `DAT_0098a178`. DirectDraw's twins (`ddSmack.cpp`: `FUN_00513400`,
`FUN_005135f0`, `FUN_00513330`, `FUN_00513380`) keep theirs at `DAT_008816b0` /
`DAT_008815b0`. d2d doesn't port any of them.

### Close (`FUN_006c9640`, slot `+0x48`)

Frees the buffer (`FUN_0040b3c0`, SMemFree-shaped, mpq-archives.md) and
zeroes `DAT_0098a278` when it is set; closes the Smacker (`_SmackClose_4`) and
zeroes the handle when `kind` is `'s'`; then zeroes `kind`. DirectDraw's
`FUN_00513330` is the same without the last step (its `kind` stays).

### Row blit (`FUN_006c9690`)

Copies a video buffer to the draw buffer through the palette map. Registers
as the decompile reads them: `EAX` is a rectangle record (`+8` the x, `+0xc`
the bottom y), `ECX` the row count, the stack argument the row width. The
first row goes to `(bottom − rows) * W + x + DAT_007c9154` (`W` =
`DAT_007c9138`), each following row one stride lower, every byte of the
source mapped through `DAT_0098a178`. Unlike DirectDraw's `FUN_00513380`, it
does not test `DAT_0098a278` for null first.
