# Pal.PL2 — palette + colormaps

`game.exe` 1.14d uses 443,175-byte (`0x6c327`) PL2 files. The layout below
comes from `FUN_00606930` and its table generators, with the installed-table
addresses confirmed in `FUN_004fb1e0`. It replaces the earlier layout inferred
from OpenDiablo2 and data probing.

## File layout

All lookup tables contain palette indices. A 256 × 256 table occupies
`0x10000` bytes; a one-dimensional 256-entry table occupies `0x100` bytes.

| Offset | Size | Contents | Generator / evidence |
|---|---:|---|---|
| `0x000000` | `0x400` | 256 base R, G, B, unused entries | `FUN_00606930` copies the input palette; `FUN_00605210` establishes channel order |
| `0x000400` | `0x2000` | 32 light-level variations | `FUN_00605de0` |
| `0x002400` | `0x1000` | 16 inventory colour variations | `FUN_00605e60` |
| `0x003400` | `0x100` | Selected-unit colour shift | `FUN_00606890` |
| `0x003500` | `0x10000` | 75/25 alpha blend | `FUN_00605880`, weight `0xbf` |
| `0x013500` | `0x10000` | **50/50 alpha blend** | `FUN_00605880`, weight `0x7f` |
| `0x023500` | `0x10000` | 25/75 alpha blend | `FUN_00605880`, weight `0x3f` |
| `0x033500` | `0x10000` | Per-channel additive blend, clamped to 255 | `FUN_006059b0` |
| `0x043500` | `0x10000` | Per-channel multiplicative blend | `FUN_00605aa0` |
| `0x053500` | `0x1800` | 24 hue rotations | `FUN_00605f00` |
| `0x054D00` | `0x1800` | 24 hue rotations with reduced lightness | `FUN_00605f00` |
| `0x056500` | `0x1800` | 24 hue rotations with increased lightness | `FUN_00605f00` |
| `0x057D00` | `0x100` | Saturation-zero shift | `FUN_00605f00` |
| `0x057E00` | `0x100` | Raised-lightness shift | `FUN_00605f00` |
| `0x057F00` | `0x1800` | 24 hue-range-restricted rotations | `FUN_00605f00` |
| `0x059700` | `0x100` | Additional shift; semantics not yet named | Contiguous loader block |
| `0x059800` | `0xC00` | 12 saturated hue variations | `FUN_00605f00` |
| `0x05A400` | `0x100` | Red tone ramps | `FUN_00605f00` |
| `0x05A500` | `0x100` | Green tone ramps | `FUN_00605f00` |
| `0x05A600` | `0x100` | Blue tone ramps | `FUN_00605f00` |
| `0x05A700` | `0xE00` | 14 additional shifts; semantics not yet named | Contiguous loader block |
| `0x05B500` | `0x10000` | Max-component-weighted blend, **not** 50/50 alpha | `FUN_00605b70` |
| `0x06B500` | `0x100` | Darkened colour shift, each channel scaled to two thirds | `FUN_00605b70` |
| `0x06B600` | `0x27` | 13 text colours, three bytes each | `FUN_00606930` copies 39 bytes |
| `0x06B627` | `0xD00` | 13 text-colour index shifts | `FUN_00605d20` builds the last 12; loader copies all 13 |
| `0x06C327` | — | End of file | Exact file size |

`FUN_004fb1e0` installs these tables directly: base to `0x81e668`, the
`0x400..0x3500` block to `0x81ea68`, alpha tables to `0x7ee468`, additive to
`0x7de468`, multiply to `0x821b68`, the `0x53500..0x5b500` shifts to
`0x7d6468`, max-component blend to `0x831b68`, darkened shift to
`0x81e468`, and text colours/shifts to `0x7d563c` / `0x7d5668`.

For the alpha tables, `FUN_00605880` computes each output colour as
`column * (255 - weight) / 255 + row * weight / 255`, then finds the nearest
base-palette entry with `FUN_00605210`. Thus the three weights `0xbf`,
`0x7f`, and `0x3f` select approximately 75%, 50%, and 25% row contribution.

## d2d comparison

`Pl2::additive()` uses the correct `0x33500` table. An emulator dump of
`game.exe`'s installed additive table at `0x7de468` matches d2d in all
65,536 Act 1 cells.

`Pl2::blend50()` uses the 50/50 alpha table at `0x13500` (installed at
`0x7fe468`) and matches it in all 65,536 Act 1 cells. It read `0x5b500`, the
max-component-weighted table, until 2026-11-03 (51,800 Act 1 cells off).
The method has no runtime caller yet; its diagonal-identity test passes for
either table, so only the oracle identifies it.

Reproduce the Act 1 oracle comparison:

```sh
cmake --build build --target mpq-cat pl2-dump
for table in additive blend50; do
  (cd tools/emu && uv run python pl2.py "$table" 1) > ".cache/pl2-oracle-${table}.txt"
  build/tools/mpq-cat/mpq-cat "$D2_MPQ_DIR" 'data\global\palette\act1\pal.pl2' \
    | build/tools/pl2-dump/pl2-dump "$table" > ".cache/pl2-d2d-${table}.txt"
  diff -u ".cache/pl2-oracle-${table}.txt" ".cache/pl2-d2d-${table}.txt"
done
```

No diff is expected for either table.
