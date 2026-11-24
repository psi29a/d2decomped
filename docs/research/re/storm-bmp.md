# Storm's image files (SOURCE/SBMP.CPP)

Traced 2026-11-24 by reading game.exe 1.14d, not run: the decompile, the
assembly for register arguments and return values, `game-calls.tsv` for
callers, and a byte scan of the whole exe for pointers and `E8`/`E9`
calls to each entry point. The module is `0x6c50d0..0x6c7025`: the
`.\SOURCE\SBMP.CPP` entries in `game-srcfiles.tsv` and the functions
between them, up to the save wrapper `FUN_006c7000` (the function after
it counts UTF-8 lengths, not images). Its source file name (`0x6fcc58`) appears at
lines 0x116, 0x11d, 0x120, 0x1ff, 0x20b, 0x294, 0x29c, 0x388, 0x389,
0x3a4, 0x3a5, 0x3e8, 0x3ee, 0x46b, 0x474, 0x4eb, 0x4f2, 0x5b6 and 0x5f3.

```sh
python3 tools/ghidra/fn.py 006c6b70 006c54d0 006c5f70 006c6790 006c6e70
python3 tools/ghidra/fn.py 006c6cb0 006c5ee0 006c5b60 006c5b10 006c61c0
python3 tools/ghidra/fn.py 006c6620 006c6470 006c6370 006c6330 006c5a90 006c5a60 006c7000
```

The decompile hides most of these functions' register arguments; the
argument lists below are read from the assembly at each call.

What the game uses is the in-memory decoder `FUN_006c6a80` (TGA, BMP or
PCX bytes already read; its routing is in
[source-module.md](source-module.md), the PCX picture load in
[gdi-driver.md](gdi-driver.md)). It has four callers outside this module
in `game-calls.tsv`, and the loader below. Everything that opens or
writes an image file by name is unreachable: the three entry points have
no caller and no pointer anywhere in the exe, and the rest is called only
from them ([The functions](#the-functions)). d2d decodes the game's
images itself and saves no screenshots this way, so nothing here has a
counterpart.

Helpers: `FUN_00419980` SFileOpenFile, `FUN_004198d0` SFileCloseFile,
`FUN_00416370` SFileGetFileSize, `FUN_004165a0` SFileSetFilePointer
([storm-file.md](storm-file.md)), `FUN_0041b050` the read, `FUN_00413020`
and `FUN_00412650` SMemAlloc and SMemFree
([chat-ignorelist.md](chat-ignorelist.md)), `FUN_0041c340` set last
error (0x57, invalid parameter). A palette is always 256 four-byte
entries in R, G, B, 0 order.

## Loading a file

`FUN_006c6b70(name, palette, buffer, size, width*, height*, bits*)`,
stdcall (`ret 0x1c`):

- Zeroes the three non-null outputs. An empty or null name, or a buffer
  with a size of 0, sets 0x57 and returns 0.
- Opens the name through SFile, reads 2 bytes and steps back 2. The kind
  comes from those bytes as `FUN_006c6a80` gets it (`FUN_006c5f40`: `BM`
  1, 0x50a 2, else 3). A failed read leaves kind 0, which returns 0.
- Kind 1 goes to the BMP reader, 2 to the PCX reader, 3 to the TGA
  reader, each with the open file. The file is closed and the reader's
  result returned.

Each reader fills only what it is given: the palette, the buffer, and
the three outputs, each optional. All return 1, or 0 on a failed read.

- **TGA, `FUN_006c54d0`** (file `EDI`, buffer `ECX`, width* `EDX`; stack
  palette, size, height*, bits*; `ret 0x10`):
  - Reads the 18-byte header and checks it with `FUN_006c50d0`.
  - Fails unless the file holds the header, the id field, the colour map
    and the pixels (width × height × bytes a pixel, or 1 byte for a
    run-length type 9 or 10).
  - Skips the id field. With a palette and a colour map, reads the map
    and converts it with `FUN_006c5150`; a size mismatch there returns 0.
  - With a buffer, reads the rest of the file into a block (line 0x116)
    and decodes it with `FUN_006c5210` (size in `EAX`).
- **BMP, `FUN_006c5f70`** (file `EBX`, palette `ECX`, width* `EAX`; stack
  buffer, size, height*, bits*; `ret 0x10`):
  - Reads the 14-byte file header and a 40-byte info header; width
    `+4`, height `+8`, bits `+0xe`. The `BM` is checked only by the kind
    test.
  - At 8 bits or fewer, with a palette: reads 0x400 bytes and swaps each
    entry's B and R. Above 8 bits the palette is ignored.
  - With a buffer: seeks from 0x36 (or 0x436 after a palette) to the
    pixel offset `+0xa`, reads the info header's image size `+0x14`
    (line 0x1ff) and copies the rows bottom-up into the buffer with
    `FUN_006c59c0`: row width × bits ÷ 8, source rows padded to 4. A short
    read returns 0.
  - Compression, a negative (top-down) height and a longer info header
    aren't looked at: the seek counts from a 40-byte header.
- **PCX, `FUN_006c6790`** (file `EDI`, buffer `ECX`, height* `EDX`; stack
  palette, size, width*, bits*; `ret 0x10`):
  - Reads the 128-byte header. Width is x max − x min + 1, height y max −
    y min + 1, bits planes `+0x41` × bits a plane `+3`.
  - A palette is read only at 8 bits.
  - With a buffer: the pixel data is the file less the header, and less
    0x300 at 8 bits. It is decoded with `FUN_006c5cf0` (bytes a line
    `+0x42`, planes `+0x41`), one byte short at 8 bits, so the palette's
    `0x0c` marker isn't decoded. The marker itself is never checked.
  - The palette is the file's last 0x300 bytes (reached by seeking 0x300
    back from the end when there is no buffer), R G B copied as they are.

`FUN_006c6e70(name, palette, buffer**, width*, height*, bits*, wanted
bits, alloc)`, stdcall (`ret 0x20`):

- Zeroes the outputs. An empty or null name, or no `buffer**`, sets
  0x57 and returns 0.
- Reads the whole file into a block (line 0x5b6) and asks `FUN_006c6a80`
  (kind 0, no destination) for width, height and bits.
- A wanted bits of 0 takes the file's; any other value must match, else
  it returns 0.
- Allocates width × height × bits ÷ 8: by `alloc(size)` when given, else
  SMemAlloc tagged with the file name. Decodes into it with
  `FUN_006c6a80` and the palette, then fills the outputs.
- Frees the file block (line 0x5f3) and returns 1, or 0.

## Saving a file

`FUN_006c7000(name, palette, pixels, width, height, bits)` (`ret 0x18`) calls
`FUN_006c6cb0` with a pitch of 0.

`FUN_006c6cb0(name, palette, pixels, width, height, bits, pitch)`,
stdcall (`ret 0x1c`):

- Sets 0x57 and returns 0 for an empty or null name, no pixels, 8 bits
  with no palette, or a width or height below 1.
- `FUN_006c5ee0` (name in `EAX`) finds the extension: the last `.`
  after the last `\`, or the name after the last `\` when there is none.
- `.tga` takes 8, 24 or 32 bits; every other name takes only 8. Anything
  else returns 0.
- A pitch of 0 becomes width × bytes a pixel.
- Creates the file (`CreateFileA`, write, no sharing, `CREATE_ALWAYS`),
  0 if that fails, then picks the writer by `_stricmp` of the extension:
  `.pcx` (`0x6fcc74`), `.gif` (`0x6fcc6c`), `.tga` (`0x6fcc7c`), else BMP.
  It closes the handle and returns the writer's result.
- Only the TGA writer `FUN_006c5830` gets the pitch. The others read the
  pixels as width-byte rows with no gaps.

The PCX, GIF and BMP writers return 1 and check no `WriteFile` result.

- **PCX, `FUN_006c5b60`** (height `ECX`, width `EDX`; stack handle,
  palette, pixels; `ret 0xc`):
  - The header: 0x0a, version 5, run-length, 8 bits, x max width − 1, y
    max height − 1, 72 × 72 dpi, 1 plane, bytes a line = the width (odd
    widths too).
  - Each row is packed by `FUN_006c5b10` (count `EAX`, `EDI` → source
    pointer, `EDX` → output pointer, both advanced) into a block of
    width × height × 2 (line 0x4eb): a run of 2 to 63 equal bytes is
    0xc0 | count then the byte, and a lone byte of 0xc0 or above gets a
    count of 1 (0xc1).
  - Then `0x0c` and the palette as 256 R G B.
- **GIF, `FUN_006c6620`** (width `ECX`, file `ESI`; stack palette,
  pixels, height; `ret 0xc`):
  - `GIF87a`, the screen width × height, flags 0xf7 (a 256-entry global
    table), background 0, aspect 0, the palette as 256 R G B.
  - An image descriptor `,` at 0, 0, width × height, no flags, then LZW
    minimum code size 8.
  - The data, from `FUN_006c6470` into a width × height × 2 block (line
    0x3e8), then a 0 block and `;`.
- **TGA, `FUN_006c5830`** (pixels `ECX`, file `EBX`; stack palette,
  width, height, bits, pitch; `ret 0x14`): only its arguments are read
  here; it is cited in [source-frontier.md](source-frontier.md) as a
  caller of `FUN_006c59c0`.
- **BMP, `FUN_006c61c0`** (palette `EAX`, width `EDI`; stack handle,
  pixels, height; `ret 0xc`):
  - `BM`, file size width × height + 0x436, pixel offset 0x436.
  - A 40-byte info header: width, height (positive: bottom-up), 1 plane,
    8 bits, no compression, image size width × height, 256 colours used
    and important.
  - The palette with B and R swapped, then the rows bottom-up, padded to
    4 (`FUN_006c59c0` into a block, line 0x294).
  - The file size and image size leave out the padding (bugs.md #135).

### The GIF encoder

`FUN_006c6470(output)` (count `ECX`, pixels `EDX`; `ret 4`) returns the
bytes written. It is the classic hashed LZW encoder: codes of 9 to 12
bits, clear 0x100, end 0x101, first free code 0x102. The pixels are
always 8-bit. Its state is on the stack, after the output pointer:

| Offset | Field |
|---|---|
| `+0x00` | code width (9) |
| `+0x04` | largest code at this width (0x1ff) |
| `+0x08` | starting width (9) |
| `+0x0c` | bit buffer |
| `+0x10` | bits in it |
| `+0x14` | current prefix |
| `+0x18` | first pixel to come (1) |
| `+0x1c` | clear code (0x100) |
| `+0x20` | end code (0x101) |
| `+0x24` | next free code (0x102) |
| `+0x28` | hash table: codes (0x4e2c bytes = 5003 entries, line 0x388, zeroed) |
| `+0x2c` | hash table: prefix << 8 \| pixel (line 0x389) |
| `+0x30` | bytes in the current block |
| `+0x34` | the block: its length byte, then up to 255 bytes |

- The encoder starts with a clear code, then sends each pixel to
  `FUN_006c6370`. At the end it sends the last prefix (when any pixel
  came), the end code, the last partial byte, and the last block. It
  frees both tables (lines 0x3a4, 0x3a5).
- `FUN_006c6370(output*, pixel)` (state `EAX`; `ret 8`): the first pixel
  only becomes the prefix. Otherwise it hashes (pixel << 4) + prefix,
  less 5003 when larger than 5002, and probes down by 5003 − hash (1 at
  0). On a hit the code becomes the prefix. On a miss it sends the
  prefix. It stores the pair at the next free code while that is below
  0x1000, else clears the table (`FUN_006c6330`). Then the pixel becomes
  the prefix.
- `FUN_006c6330(output*)` (state `ESI`; `ret 4`) zeroes the code table,
  sets the next free code to clear + 2, sends the clear code and returns
  to the starting width.
- `FUN_006c5a90` (code `EAX`, output* `ECX`, state `EDX`) ORs the code in
  above the buffered bits and moves out whole bytes into the block,
  flushing it once 255 bytes are there. When the next free code is past
  the largest, it widens the code by 1: largest (1 << width) − 1, or
  0x1000 at 12 bits, so the width stops there.
- `FUN_006c5a60` (state `ESI`, output* `EDI`) writes a non-empty block's
  length into `+0x34`, copies length + 1 bytes to the output, advances
  it and empties the block.

## The functions

Callers from `game-calls.tsv`, and the byte scan for the three with none.
"orphan" means no caller and no pointer anywhere in the exe. "orphan's
callee" means reached only from orphans. The scan's one hit, for
`FUN_006c7000` at `0x6cfb2d` in `.rdata`, is the bytes `00 70 6c 00`
unaligned between zeros, not a pointer.

| Address | Reading | Callers |
|---|---|---|
| `FUN_006c54d0` | TGA file reader | orphan's callee (`FUN_006c6b70`) |
| `FUN_006c5a60` | GIF: flush the block | orphan's callee (`FUN_006c5a90`, `FUN_006c6470`) |
| `FUN_006c5a90` | GIF: send a code | orphan's callee (`FUN_006c6330`, `FUN_006c6370`, `FUN_006c6470`) |
| `FUN_006c5b10` | PCX: pack a row | orphan's callee (`FUN_006c5b60`) |
| `FUN_006c5b60` | PCX writer | orphan's callee (`FUN_006c6cb0`) |
| `FUN_006c5ee0` | A file name's extension | orphan's callee (`FUN_006c6cb0`) |
| `FUN_006c5f70` | BMP file reader | orphan's callee (`FUN_006c6b70`) |
| `FUN_006c61c0` | BMP writer | orphan's callee (`FUN_006c6cb0`) |
| `FUN_006c6330` | GIF: clear the table | orphan's callee (`FUN_006c6370`) |
| `FUN_006c6370` | GIF: one pixel | orphan's callee (`FUN_006c6470`) |
| `FUN_006c6470` | GIF: the LZW encoder | orphan's callee (`FUN_006c6620`) |
| `FUN_006c6620` | GIF writer | orphan's callee (`FUN_006c6cb0`) |
| `FUN_006c6790` | PCX file reader | orphan's callee (`FUN_006c6b70`) |
| `FUN_006c6b70` | Load an image file into a buffer | orphan |
| `FUN_006c6cb0` | Save an image file by extension | orphan's callee (`FUN_006c7000`) |
| `FUN_006c6e70` | Load an image file into a new buffer | orphan |
| `FUN_006c7000` | Save, pitch 0 | orphan |

`FUN_006c5830`, the TGA writer, is also reached only from
`FUN_006c6cb0`.
