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
