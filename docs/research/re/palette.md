# Palette loading — `.\Src\D2WinPalette.cpp`

`game.exe` 1.14d loads a 768-byte `pal.dat` alongside the act's `pal.pl2`.
The DAT is 256 three-byte entries in **B, G, R** order. d2d's conversion to
R, G, B matches the active palette produced by game.exe for every entry in
Acts 1–5.

## Function map

| Address | Role |
|---|---|
| `FUN_004fb1e0` | Load both files, install the PL2 tables, and build the derived palette lookups. Its source string is `.\Src\D2WinPalette.cpp` at `0x006dc828`. |
| `FUN_004fb480` | Select an act palette: `CL + 1`, clamped to 1–5; build the DAT and PL2 paths and call `FUN_004fb1e0`. |
| `FUN_004fb500` | Load explicit DAT/PL2 paths, defaulting to Act 1 when the DAT path is null. |
| `FUN_00517079` | Archive file reader: `ECX` is the archive selector (zero here), `EDX` the path; it returns an allocated file buffer and cleans three stack arguments. |
| `FUN_00605210` | Find the nearest palette entry to an R, G, B triple, with the first entry winning a tie. |

`FUN_004fb500` has three callers: `FUN_0042f2e0`, `FUN_0044e770`, and
`FUN_00454740`. `FUN_004fb480` is the usual act-selection entry point.

## DAT and active-palette layout

At `0x004fb1f7–0x004fb201`, `FUN_004fb1e0` reads the DAT path passed at
`EBP+0x08`. Its loop at `0x004fb204–0x004fb22a` advances the DAT pointer by
three bytes per entry while writing a zero fourth byte into a local
four-byte-stride array. That local array is not consumed later by the
function.

The active palette is instead the PL2 base table: `0x004fb255–0x004fb266`
copies `0x400` bytes from PL2 offset zero to `0x0081e668`, and
`0x004fb342–0x004fb35e` writes 5 to each entry's fourth byte. Consequently,
the oracle below compares two representations of the same palette; it does
not claim that game.exe's active palette is read directly from DAT.

`FUN_00605210` establishes the active entry's channel order. At
`0x00605230–0x00605253` it subtracts its R, G, and B arguments from bytes
`+0`, `+1`, and `+2` of each four-byte entry, then sums the squared
differences. The PL2 base is therefore R, G, B, unused.

For each act, interpreting DAT byte triples as B, G, R reproduces those
first three PL2 bytes exactly; interpreting them as R, G, B does not. This
covers all 256 entries in each of Acts 1–5.

## The PL2 builder's colour helpers, and an unused matcher

The untouched functions between the nearest-colour lookup `FUN_00605210`
and the PL2 builder `FUN_00606930` (pl2.md). Read from the decompile; no
oracle was run. Rows marked *dead* have no pointer, call or jump from
anything outside the dead ones (byte scan of game.exe).

| Address | Role |
|---|---|
| `FUN_00604df0` | Fastcall (out, palette): each of the 256 four-byte entries to hue, saturation and lightness as three doubles. Channels are byte / 255; lightness (max + min) / 2; saturation (max − min) / (max + min), or / (2 − max − min) above 0.5 lightness; hue in degrees, 0 for a grey. A lightness outside 0..1 is fatal (line 0x3f), as are a saturation outside 0..1 (0x4d) and a hue below 0 or from 360 up (0x5c); byte channels never get there. Caller `FUN_00606930` |
| `FUN_00604fe0` | (m1, m2, hue): the HSL-to-RGB channel helper, hue wrapped into 0..360: below 60 m1 + (m2 − m1) × hue / 60, below 180 m2, from 240 m1, else m1 + (m2 − m1) × (240 − hue) / 60. Caller `FUN_00605080` |
| `FUN_00605080` | ECX 256 HSL triples, EAX out: back to four-byte entries, each channel rounded from × 255; grey (saturation 0) is the lightness in all three. The channels come from `FUN_00604fe0` at hue + 120, hue and hue − 120. Callers `FUN_00605f00` (the hue rotations), `FUN_00606890` (the selected-unit shift) |
| `FUN_006051f0` | `VirtualFree(p, 0, 0x8000)` through `FUN_005179a4` when p is not null. Dead |
| `FUN_00605280` | (palette, count, r, g, b): the index of the entry *farthest* from (r, g, b) by squared distance (a strict `>` keeps the first), 0 for an empty palette. Dead |
| `FUN_00605320` | `DAT_008e9d38` = the argument's three low bytes reversed (B, G, R to R, G, B), the colour `FUN_006056d0` skips. Dead |
| `FUN_006054f0` | (path, first, last): sets up the matcher below. Fatal for a null path (line 0x3b6), first above last (0x3b7), first below 0 (0x3b8) or last above 0xff (0x3b9). Keeps last in `DAT_008e902c`, first = 1 in `DAT_008e9930`, a squares table (−0x100..0xff) at `0x8e9030`. With an empty path (`FUN_00413750`, the strlen, gives 0) the matcher's palette is the grey ramp; otherwise a 0x300-byte file is read as 256 R, G, B triples into four-byte entries at `0x8e9938`, stored B, G, R. Any other size goes to `FUN_00605340`, which takes the palette from a 256-colour PCX (header bytes 0x0a, 5 and 8 bits; the 0x0c-marked 768 bytes at the end). It logs "Unable to open " plus the path with `OutputDebugStringA` and returns 0 when the file can't be opened. Dead |
| `FUN_006056c0` | `DAT_008e9028` = the argument, the flag that makes `FUN_006056d0` skip the colour at `DAT_008e9d38`. Dead |
| `FUN_006056d0` | (r, g, b): the nearest entry by squared distance over entries `DAT_008e9930`..`DAT_008e902c`, those masked off by the 256-byte table at `0x8e9830` skipped (when `DAT_008e9d40` is set); 0 when the colour is the skipped one. Its address is `FUN_006057a0`'s default (`0x6057bd`), so it is dead too |
| `FUN_006057a0` | (rgb, out, count, matcher): maps count R, G, B triples to indices through the matcher (default `FUN_006056d0`), only once a palette is loaded (`DAT_008e9934`); with `DAT_008e9d3c` set, a colour with all three channels under 11 is first turned into (4, 0, 0). Dead |
| `FUN_00605840` | `DAT_008e9d3c` = the argument (that near-black rule). Dead |
| `FUN_00605850` | Copies a 256-byte mask to `0x8e9830` and sets `DAT_008e9d40`. Dead |
| `FUN_00605a90` | `FUN_006059b0(argument)`. Dead |
| `FUN_006069f0` | (palette, &size): `*size` = 0x6c327, the PL2 file's size, then `FUN_00606930(palette)`. Dead |

## Oracle comparison

`tools/emu/palette.py` runs `FUN_004fb1e0` with the real act DAT and PL2,
stubbing only file I/O, memory copying, the graphics driver, and allocation
cleanup. It prints the active palette at `0x0081e668` as `index R G B`.
`tools/palette-dump` prints d2d's DAT decoding in the same format.

```sh
cmake --build build --target mpq-cat palette-dump
for act in 1 2 3 4 5; do
  (cd tools/emu && uv run python palette.py "$act") > ".cache/palette-oracle-act${act}.txt"
  build/tools/mpq-cat/mpq-cat "$D2_MPQ_DIR" "data\\global\\palette\\act${act}\\pal.dat" \
    | build/tools/palette-dump/palette-dump > ".cache/palette-d2d-act${act}.txt"
  diff -u ".cache/palette-oracle-act${act}.txt" ".cache/palette-d2d-act${act}.txt"
done
```

Traced 2026-10-30: no differences in 1,280 RGB entries (five acts × 256).
Alpha is intentionally outside the comparison: game.exe sets the active
palette's fourth byte to 5, whereas d2d applies its sprite transparency
convention.
