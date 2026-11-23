# The Arcane Sanctuary's stars

The Arcane Sanctuary (level 74) floats in a field of drifting stars. They
are not tiles or units: the world draw (`FUN_00476bc0`) calls
`FUN_00476290` when the player's level (`FUN_00463dd0` → `FUN_00620bb0`
→ `FUN_0061a1b0`) is 0x4a, after `FUN_004ded20` and before the lower
walls (`FUN_004df480`), so everything else the world draws covers them.
(Level 0x78, the Arreat Summit, calls `FUN_00476460` at the same point;
not traced.)

## Made once (`FUN_00476290`'s head)

Behind a flag nothing else writes (`DAT_007b955c`: its only references are
the read at 0x476296 and the write at 0x4763d3), so once a run:

1. The star seed (`*0x712c4c`, the 64-bit LCG `low × 0x6ac690c5 + high`):
   `FUN_00650e30` sets {1, 0x29a}, then `FUN_00650e40` sets the low word to
   `FUN_00650de0`'s mix of `time()`, `GetTickCount()` and ECX
   (`DAT_007b8d10`), the high to 0x29a. d2d: `rules::Rng(seed)`.
2. Eight colours (`DAT_007b8d24`), shade i = 0..7: base = i × 0x80 / 7 +
   0x80 (128 to 256), then red, green and blue each base + (roll & 0x3f) −
   0x20, capped at 0xff, one roll each in that order; the colour is the
   palette's nearest (`FUN_004fb180` → `FUN_00605210` on the palette at
   0x81e668).
3. The 256 stars placed (`FUN_00476190(i, 0)`).

## A star (`FUN_00476190(index, again)`)

- x (`DAT_007b8d30`): with `again` 0, rand(width) (`FUN_00476190` →
  `FUN_0045c390`: 0 for a bound under 1, a mask for a power of two, else
  the low word mod the bound); with `again` 1, width − 1 + (roll & 7).
  Width is the screen's (`DAT_007a5220`).
- y (`DAT_007b57e8`): rand(height) the same way, no roll for a height
  under 1 (`DAT_007a521c`).
- speed (`DAT_007b9130`): −(roll mod 5) − 1, so 1 to 5 px left a step.
- colour (`DAT_007b5688`): the colours[roll & 7].

## Each call

- Each star is drawn as a line from (x, y) to itself in its colour, alpha
  0xff (`FUN_004f6380`, the driver's +0xc0; the software modes' line is a
  solid palette index, weather.md).
- Then, when `GetTickCount()` is more than 0x28 (40) ms past the last step
  (`DAT_007b57e0`, which only a step updates), each star's x moves by its
  speed and one now left of 0 is placed again (`again` 1): back in at the
  right edge, on a new row, speed and colour.

d2d: `rules::Stars` (components/rules/quests/stars.hpp), drawn in `render_world`
(world_view.cpp) before the lower walls. `tools/emu/arcane_stars.py` runs
`FUN_00476290` for real (only `FUN_00650de0`, `GetTickCount` and the line
draw hooked) on made-up palettes and screen sizes;
tests/test_arcane_stars.cpp checks d2d's draws against it. Not traced:
whether the driver moves the stars when a side panel shifts the view.
