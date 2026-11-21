# DT1 block coordinates

`game.exe` 1.14d preserves signed DT1 block coordinates and adds them directly
to screen coordinates when drawing. d2d's `y_shift = max(0, -min(block.y))`
is therefore an **internal pixel-buffer packing rule**, not an offset computed
by game.exe.

## Loading and block fixup

- `FUN_00600790` caches a DT1 and calls `FUN_0060a390` to validate its header.
- `FUN_005fdea0` reads the tile's 20-byte block records on demand.
- `FUN_0060a4c0` rebases each block's encoded-data pointer at record `+0x10`;
  it does not alter the signed X or Y at `+0x00` and `+0x02`.

The wall draw routines then use those signed values directly:

- `FUN_005130a0`, at `0x00513114–0x00513122`, sign-extends `block.y` and adds
  the screen-Y argument, and sign-extends `block.x` and adds screen X.
- `FUN_005131b0` repeats the same operations at `0x00513228–0x00513236`.
- `FUN_00512f90` does the same for tile shadows.
- Wrappers `FUN_004f6920`, `FUN_004f6950`, and `FUN_004f6980` forward the
  cell's screen coordinates without a tile-block minimum or Y adjustment.

No minimum-Y pass occurs in the loader, block fixup, wrappers, or draw loops.

## The rest of `SRC/Tilecmp.cpp`

The untouched functions bracketed by `SRC/Tilecmp.cpp` in
`game-srcfiles.tsv` (entries `0x6094b0` .. `0x60a910`). They assert and
allocate with `.\SRC\Tilecmp.cpp` (`0x6e5f30`). Read from the decompile
and the disassembly; no oracle was run.

The DT1 as these functions use it: version 7 at `+0x00`, flags at
`+0x04` (bit 0 built in memory, bit 2 one flat block, bit 3 refused by
the free), a name copied to `+0x08` (`FUN_0060a290`), the tile count at
`+0x10c` and the tiles at `+0x110`, after a 0x114-byte header. A tile is
0x60 bytes: its block offset at `+0x48`, block count `+0x50`, blocks
`+0x54`. A block is 0x14 bytes: x `+0x00`, y `+0x02`, format `+0x08`,
data length (u16) `+0x0a`, data `+0x10`.

### Reached

| Address | What it does |
|---|---|
| `FUN_00609e90` | (file, a2): returns at once when a2 is set; a null file is fatal (line 0x25a). `FUN_005fe080`, then: with flag bit 2, one free (line 0x262). Bit 3 is fatal (0x267). Otherwise it frees each block's data (0x274) and clears its length and pointer, each tile's block array (0x27b), the tile array (0x281) and the file (0x285). Caller `FUN_006006b0` (`0x6006ce`), which passes each cached DT1's `+0x104` with a2 = 0 |

### Not reached

A DT1 builder: it makes tiles from 24-bit images (3 bytes a pixel), and
copies and flattens tile libraries. Nothing calls or points to the first
group (byte scan of `0x401000` .. `0x7d0000` for the address, and of
`.text` for an E8 / E9 to it). The second group is called only by the
first.

No caller:

| Address | What it does |
|---|---|
| `FUN_00609620` | (a, b): a 0x10-byte record (line 0x6d; null fatal 0x6e) holding a, b and two zeros |
| `FUN_00609670` | (library): null is fatal (0x80); frees each tile on its `+0xc` chain (next at `+0x5c`, `FUN_006095a0`), then the library (0x8c) |
| `FUN_006096d0` | (library, a2, a3, a4 .. a8, 25 bytes): null is fatal (0xa2). A new tile (`FUN_00609550`, 0xa6 when it fails) pushed on the library's chain (count `+8` + 1): u16 a2 at `+0x16`, a3 at `+0x14`, a4 .. a7 at `+0x24` .. `+0x30`, a8 at `+0x10`, the library's `+4` at `+0x34`, `+0x51` = 2, and the 25 bytes at `+0x38` |
| `FUN_00609810` | (tile, a2, pixels, x, y): unless `FUN_00609770` finds the 32 × 32 block empty, adds a block (`FUN_006094b0`) at (x, y) (each below 0x10000) with a 0xc00-byte buffer (line 0x119), format 0x1001, filled by `FUN_005179b3` (rows of 0x60 bytes, 0x20 rows); widens the tile's box to it |
| `FUN_00609b90` | (tile, pixels): null tile (0x1c3) or pixels (0x1c4) are fatal. Builds the floor's blocks (`FUN_00609aa0`) and sets the box to 0, 0, 0xa0, −0x80 |
| `FUN_00609d60` | (library, out): with no tiles, `*out` = 0 and returns 0x85200004. Else a 0x114-byte DT1 (line 0x22c) with version 7, flag bit 0 from the library's `+0`, the tile count, and a tile array (line 0x238) filled from the chain (`FUN_00609c30`); `*out` = it, 0 |
| `FUN_0060a0f0` | (file, out, size): null arguments (0x2d9 .. 0x2db) or no tiles (0x2dc) are fatal. A flat copy (size from `FUN_00609fd0`, 0x2e0 when 0; line 0x2e3): header, tiles, then each tile's blocks and data (`FUN_0060a050`) with file-relative offsets. Sets bit 1 on the source and bits 1 and 2 on the copy |
| `FUN_0060a290` | (file, out, name): not version 7 is fatal (0x348). With flag bit 0 or 1: copies the name to `+0x08`, makes the tile and block offsets pointers, clears bit 1, sets bit 2; `*out` = the file |
| `FUN_0060a570` | (file): null is fatal (0x3ea); needs bit 0 (0x3eb). `FUN_0060a520` for each block of each tile |
| `FUN_0060a750` | (file, out): null (0x44f) or without bit 0 (0x450) is fatal. A copy (lines 0x453, 0x45e, 0x472) of the header, tiles and blocks, with each block re-encoded (`FUN_0060a650`); `*out` = the copy |

Called only by the ones above:

| Address | What it does | Caller |
|---|---|---|
| `FUN_006094b0` | a zeroed 0x14-byte block (line 0x23; null fatal 0x24) | `FUN_00609810` |
| `FUN_00609550` | a zeroed 0x60-byte tile (line 0x45; null fatal 0x46) | `FUN_006096d0` |
| `FUN_006095a0` | EBX = tile: null is fatal (0x54); frees each block on its `+0x58` chain (next `+0x10`) and its data (0x35, 0x38), then the tile (0x60) | `FUN_00609670` |
| `FUN_00609770` | EAX = tile, ECX = pixels, stack pitch: 1 when a 32 × 32 block matches the tile's colour `+0x34` (with `+0x51` bit 1) and is 0 in the second plane (with bit 0) | `FUN_00609810` |
| `FUN_00609940` | EAX = pixels, stack colour: 0 when 0x200 pixels in a row are that colour, else 1 | `FUN_00609980` |
| `FUN_00609980` | a 0x600-byte block (line 0x154) filled with the colour, then 15 rows of the floor diamond copied from the image; kept as format 0x2001 when `FUN_00609940` finds it is not one colour, else freed (0x186) | `FUN_00609aa0` |
| `FUN_00609aa0` | ESI = tile, EDI = pixels: the 25 floor blocks by the table at `0x743500`, each kept (line 0x1a7) when it has pixels | `FUN_00609b90` |
| `FUN_00609c30` | EBX = the new tile, stack = the built one: copies its fields and box, and its blocks into an array (line 0x201) | `FUN_00609d60` |
| `FUN_00609fd0` | EDX = file: the flat size, 0x114 + tiles × 0x60 + blocks × 0x14 + each block's data; null is fatal (0x293) | `FUN_0060a0f0` |
| `FUN_0060a050` | EAX = out, EBX = tile: copies its blocks then their data, setting each block's offset; returns the bytes | `FUN_0060a0f0` |
| `FUN_0060a520` | ECX = pixels, EAX = bytes, EBX = callback: calls it with each 3-byte pixel | `FUN_0060a570` |
| `FUN_0060a650` | ESI = block: with no data, clears it. Format 0x1000 re-encodes through `FUN_0060d5b0` / `FUN_0060d430` (0x434, 0x435 above 0xffff bytes); 0x2000 through `FUN_0060db20`; else fatal (0x43b). Data of zeros is freed (0x429) and cleared | `FUN_0060a750` |

## d2d representation

`components/dt1/dt1.hpp` decodes all blocks into one nonnegative,
`width × abs(height)` pixel array per tile. It first computes

```text
min_y = min(0, every signed block.y)
y_shift = -min_y = max(0, -min(block.y))
```

It stores each block position as `(block.x, block.y + y_shift)` and uses the
same shift in `put_pixel`. This is a suitable representation for d2d's packed
buffer, provided the renderer accounts for the shift when choosing the
buffer's screen position. It should not be cited as a game.exe formula.

Traced 2026-10-30 through `FUN_005fdea0`, `FUN_0060a4c0`, `FUN_005130a0`,
`FUN_005131b0`, and `FUN_00512f90`.
