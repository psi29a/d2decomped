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

### The call into the slot (`FUN_004f68e0`)

`FUN_004f68e0` (`tools/emu/disasm.py 4f68e0`) is a stdcall thunk of nine
stack arguments (`ret 0x24`) with no logic of its own. It loads argument 1
into ECX and argument 2 into EDX, pushes arguments 3 to 9 (seven dwords, so
that argument 3 is the callee's `[ebp + 8]`) and calls `[DAT_007c8cc0 + 0x7c]`.
Both drivers' entries (`FUN_006c95d0` and `FUN_005132c0`) end `ret 0x1c`, seven
dwords, which matches. Read from their disassembly (the same in both):

| Thunk argument | Callee | Used as |
|---|---|---|
| 1 | ECX | the tile, passed on to `FUN_005fdea0` and to callback `+0xc` |
| 2 | EDX | passed to the callback as its stack argument beside the y |
| 3 | `[ebp + 8]` | x, less 0x50 (and the panel shift below) before the callback |
| 4 | `[ebp + 0xc]` | y, handed to the callback unchanged |
| 5, 6, 7 | `[ebp + 0x10..0x18]` | not read by either driver |
| 8 | `[ebp + 0x1c]` | 1: x moves by −`DAT_007c9138` / 4; 2: by +`DAT_007c9138` / 4; else 0 |
| 9 | `[ebp + 0x20]` | not read by either driver |

The only caller with a body to read here, `FUN_004de410`, passes (tile
record +0x18, the light table, x, y, subtile x, subtile y, 0xff, its state
+0x20, its third argument). Its state +0x20 is `FUN_0045ae90`'s value
(`FUN_004de730` stores it there), which fits the panel shift that
menu.md "The view with panels open" describes: 1 and 2 are the two panel
sides. That last link is read from `FUN_004de730` and the drivers' compare
with 1 and 2, not run.

d2d: `not ported` as a function (it is a table call). The x −0x50 is
bugs.md #10's; the panel shift is menu.md's, not re-checked here. Cited, not
proven.

## The floor draw's helpers (`FUN_004de410` and what it calls)

- **`FUN_00472280`** (`tools/emu/disasm.py 472280`) is the seeded roll the
  splash and bubble spawns use: ECX the seed pair {low at +0, high at +4},
  EDX a base, one stack argument n (`ret 4`; the decompile shows only n).
  For n < 1 it returns EDX and leaves the seed alone. Otherwise the seed
  becomes low × 0x6ac690c5 + high (64-bit: new low from the low word, new
  high from the carry word) and the result is EDX + (new low & (n − 1)) when
  n is a power of two, else EDX + new low % n. The spawns call it with EDX 0
  (`xor edx, edx` at 0x472e0b in `FUN_00472da0`), so it is a plain roll
  there. d2d reads the same, cited from the decompile, no oracle run:
  `rules::Rng::operator()` (rules.hpp, the same step,
  mask for powers of two, `%` otherwise, 0 below 1). rules.hpp cites
  `FUN_0045c370` for the step; that function was not compared here. Cited.
- **`FUN_00477730`** returns `DAT_00712ccc`, the driver option that
  lighting.md "Drawing with it" splits at 4: `FUN_004de410` takes the
  software light table (`FUN_004ddef0`) when it is below 4 and the 3D
  vertex fill (`FUN_004de070`, `FUN_004de260`) otherwise; `FUN_004def80`
  tests `3 <` it. Its four callers are `FUN_004de410`, `FUN_004dea70`,
  `FUN_004def80` and `FUN_004df1c0` (`game-calls.tsv`). Read from the
  six-byte body (`tools/ghidra/fn.py 00477730`). d2d: not ported as a
  function; lighting.md already carries the `< 4` rule and d2d copies the
  software path. Cited.

- **`FUN_004ddef0`** (`tools/emu/disasm.py 4ddef0`) fills the software modes'
  8 × 8 light table: EAX the output (64 entries of 12 bytes, the caller's
  768-byte buffer), three stack arguments (`ret 0xc`) x, y and a flag.
  - Flag 0 (the floor draw's case): entry (col, row) calls `FUN_00475aa0` with
    ECX = 8·x − 8 + 8·col and EDX = 8·y − 8 + 8·row, the four bytes of the
    entry as its output pointers (`FUN_00475aa0` shifts both right by 3, so this
    is the grid at (x − 1 + col, y − 1 + row), lighting.md). Byte 0 is the
    grid's intensity; bytes 1 to 3 are written only when `DAT_00712b8c` is
    non-zero (lighting.md's colour toggle), else they keep what the caller's
    buffer held; `FUN_004de410`'s buffer is uninitialised stack.
  - Flag non-zero (a roof with a height, lighting.md): every entry is
    `FUN_0061c0e0`'s intensity in byte 0 and `FUN_0061c0b0`'s red, green, blue
    in bytes 1, 2, 3 (the call order of its three pointers); x and y unused.
  d2d reads the same on the table and the rule, cited from the decompile, no
  oracle run (`tile_blit.hpp`'s `blit_dt1_tile_lit`
  cites it; lighting.md has the grid rule); the stale colour bytes with the
  toggle off are not modelled and nothing in the software path was found
  reading them (`FUN_004f8b80`'s reads were not re-checked). Cited.

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

### Open (`FUN_006c9710`, slot `+0x40`)

Arguments as the decompile reads them: `ECX` the file name, `EDX` the record,
the stack argument the kind. It stores the kind in `record + 4`, then:

- **`'p'`, a still picture:** allocates the buffer `DAT_0098a278` (0x6db0
  bytes, zeroed), loads the file (`FUN_0041b680`), decodes it (`FUN_006c6a80`
  with type 2, an image decoder returning width, height and bits per pixel;
  the file is freed with its assert at `.\Src\gdiSmack.cpp` line 0x78) and
  accepts it only if it is 0x1d4 × 0x3c at 8 bits (0x1d4 × 0x3c = 0x6db0).
  It then fills the 256-byte map `DAT_0098a178` from the picture's 256 RGBA
  palette entries through `FUN_00605210` (the game's nearest-colour lookup in
  `DAT_007c8cc8`, as the video frame does).
- **`'s'`, a Smacker:** only when the record has none yet: `_SmackOpen_12(name,
  0xfe000, -1)`; on success allocates a width × height buffer, zeroes it,
  points the Smacker at it (`_SmackToBuffer_28`) and clears
  `DAT_0098a27c` (the palette-changed flag).
- Any other kind returns; any failure (the open, the load, the size test)
  falls to the cleanup, which frees the buffer, closes the Smacker and zeroes
  the kind, as `FUN_006c9640` does.

The allocation's and free's literal arguments (0x51 / 0x43 here, 0x53 / 0x42
in DirectDraw's `FUN_00513400`, 0xf0 for the free) read like source line
numbers of SMemAlloc / SMemFree-shaped calls, the way the assert's 0x78 is;
that is a reading of the pattern, not traced. Apart from those, the two
drivers' open functions are the same text, with the failure path of the GDI
one also zeroing the kind.

### Frame (`FUN_006c9900`, slot `+0x44`)

- **`'p'`:** when `DAT_007c9144` is 0, blits the still (`FUN_006c9690`, width
  0x1d4).
- **`'s'` with a Smacker:** `_SmackWait_4` says whether a frame is due. If it
  is: returns at once when `DAT_007c9140` is 0; otherwise, when the frame
  carries a palette (Smacker `+0x68`), rebuilds the map `DAT_0098a178` from
  its entries (`+0x6c..`, three bytes each) through `FUN_00605210` and sets
  `DAT_0098a27c`; decodes the frame (`_SmackDoFrame_4`); then, if the frame
  number (Smacker `+0x374`) equals the frame count (`+0xc`), closes the video
  (`FUN_006c9640`), else `_SmackNextFrame_4`. After that, with
  `DAT_007c9144` 0 and `DAT_0098a27c` set, it blits with the Smacker's width
  (`+4`).
- DirectDraw's `FUN_005135f0` is the same text (its map `DAT_008815b0`, flag
  `DAT_008816b4`, close `FUN_00513330`, blit `FUN_00513380`).

**The close-then-blit order** (bugs.md #95): on the frame that closes the
video, the GDI function goes on to the blit with the same flag still set,
reading the width from the Smacker it just closed, and `FUN_006c9640` has
zeroed `DAT_0098a278`; `FUN_006c9690` then reads its source from that null
pointer. DirectDraw's `FUN_00513380` opens with `if (DAT_008816b0 != 0)`,
so the same sequence there blits nothing. Not run: whether a frame number
reaches the frame count under this check, and whether the palette flag is
set by then, is not read from the Smacker library.
