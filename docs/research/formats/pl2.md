# Pal.PL2 — palette + colormaps

D2 stores a "palette-plus-lookups" bundle at, e.g.:
- `data\global\palette\Sky\Pal.PL2`
- `data\global\palette\fechar\Pal.PL2`
- `data\global\palette\menu1\Pal.PL2`
- `data\global\palette\ACT{1..5}\Pal.PL2`

File size in 1.14d: **443,175 bytes** (constant across every ship-side PL2).

## What we currently parse

`components/palette/palette.hpp` exposes only the parts we've needed so far.
The rest of the file is not addressed by the parser; adding a transform is
one offset + one `std::uint8_t` slice.

| Offset      | Size          | Field                       | Semantics                                            |
|-------------|---------------|-----------------------------|------------------------------------------------------|
| `0x000000`  | 256 × 4 bytes | Base RGBA palette           | `base_palette()[idx]` — RGB triple + alpha           |
| `0x033500`  | 256 × 256     | MaxComponentBlend (additive)| `additive(fg, bg)` — palette-preserving TRANS_ADDITIVE |
| `0x05B4C0`  | 256 × 256     | Transparency50              | `blend50(fg, bg)` — 50% alpha blend                  |

(210176 = `0x033500`; 374016 = `0x05B4C0`.)

## Table semantics

Each `PL2PaletteTransform` is a 256×256 grid of palette indices. `T[fg][bg]`
returns the palette index Blizzard picked to represent the composite of
foreground index `fg` over background index `bg`. Because the result is
still a palette index, blending is *palette-preserving* — the output never
drifts off the fixed 256-colour set, which is what keeps D2's look
consistent under lighting, VFX and alpha blends.

### MaxComponentBlend (additive)

D2's TRANS_ADDITIVE mode — used for fire, spell glows, shrine sparkle.
Result brightness is always ≥ `max(brightness(fg), brightness(bg))`.

Empirical invariants (verified by the palette test):
- `additive(i, 0) == i` and `additive(0, j) == j` for every index — pure
  identity on both axes, because "add nothing to fg" and "add fg to nothing"
  both return fg.
- **Commutative**: `additive(i, j) == additive(j, i)`.

### Transparency50 (50% alpha)

The most common alpha-blend LUT. Result is roughly the midpoint colour
between `fg` and `bg`, snapped to the nearest palette entry.

Invariant:
- `blend50(i, i) == i` — blending an index with itself is itself
  (diagonal identity).

`blend50(i, 0)` is *not* `i` — blending with palette entry 0 gives the
mid-tone between whatever entry 0 renders as and `i`, not `i` itself.

## Full layout (not yet parsed)

unverified (source: OpenDiablo2's `d2pl2`), with empirical probing of
`menu1/Pal.PL2` (re/unverified.md):

- `0x000000..0x000400`  BasePalette (parsed)
- `0x000400..?`         LightLevelVariations, InvColorVariations, TextColors,
                        TextColorShifts, Transparency25 — offsets not
                        pinned; total ~180 KiB before we hit `0x033500`.
- `0x033500..0x043500`  **MaxComponentBlend / Additive** (parsed)
- `0x043500..0x05B4C0`  Blend25 or Blend75 (only ONE of them lives here;
                        the other, plus HueVariations and the tone tables,
                        occupy the rest of the file). To be nailed when a
                        subsystem asks for it.
- `0x05B4C0..0x06B4C0`  **Transparency50** (parsed)
- `0x06B4C0..EOF`       Remaining transforms — HueVariations[111],
                        RedTones/GreenTones/BlueTones, DarkendColorShift,
                        UnknownVariations[14].

## How the offsets were found

A small brute-force probe swept every 256-aligned offset in the file
looking for 65,536-byte blocks whose `T[i][0]` mapped identically to `i`
for all 256 indices. That returned exactly two hits: **210176** and
**374016**. Disambiguated by:
- `T[0][j] == j` on **both** axes  → additive (210176)
- `T[i][i] == i` (diagonal only)   → 50% alpha (374016)

Additive was double-checked by verifying result brightness ≥ per-channel
max on 65,025 sample cells (100% pass).
