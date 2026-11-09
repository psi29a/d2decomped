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
