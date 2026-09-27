# Lighting and time of day — game.exe 1.14d

Status: research in progress (2026-09-27). Nothing is built yet.

## The day (ENVIRONMENT\Env.cpp)

The environment struct is 0x38 bytes. `FUN_0061be40` makes it, and
`FUN_0061aa60(game)` returns it.

| offset | meaning |
|---|---|
| +0 | phase 0..5 |
| +4 | the phase's type (0 day, 1 dusk, 2 night, 3 dawn) |
| +8 | time (frames) |
| +0xc | intensity 0..255 |
| +0x18..0x1a | ambient R, G, B |
| +0x1c / +0x24 | the sun's direction (−cos, sin) |
| +0x28 | scale: frames per degree, 128 (0x7443e4; 4 in the eclipse) |
| +0x30 | eclipse |

- **Phases:** each is {start degree, type, RGB}. Normal (0x7443f0):

  | phase | start | type | RGB |
  |---|---|---|---|
  | 0 | 320° | dawn | (125, 144, 243) |
  | 1 | 340° | dawn | (208, 184, 131) |
  | 2 | 0° | day | (255, 255, 255) |
  | 3 | 160° | dusk | (255, 255, 255) |
  | 4 | 180° | dusk | (194, 152, 193) |
  | 5 | 200° | night | (125, 144, 243) |

  Act 4 uses 0x744438 and the eclipse 0x744480.
- **A new game** starts in phase 2 at time 0, which is sunrise.
- **The step** (`FUN_0061bee0`, from the client frame `FUN_0044c790` via
  `FUN_0061bfc0`): time + 1 a frame. Night phases add one more; Act 4 adds
  15 and Act 3 at night 9. The time wraps at 360 × scale, so a day is
  46080 frames, about 31 minutes. At the next phase's start × scale, the
  phase advances.
- **Intensity** (`FUN_0061bb80`): angle a = time / scale degrees.
  Intensity = round(sin(a) × 128 + 0.5), halved for a ≥ 180°, clamped to
  0 and to 255 (170 in Act 5). Level 120 is fixed at 200. Act 4 levels
  103..106 step toward 0x80 / 0x40 / 0x38 / 0x30. The eclipse drops by 8
  down to 32.
- **Colour** (`FUN_0061bce0`): blended from the phase's RGB toward the
  next phase's by how far the time is between them. Level 120 is fixed at
  (245, 240, 255).
- **The level's own light:** `FUN_00619d70` gives the room's level
  intensity and RGB (Levels.txt Intensity / Red / Green / Blue). When any
  is non-zero it replaces the day's (`FUN_004ded20`, `FUN_00474550`).
- **Ambient sound:** day = phase types 1..3 (`FUN_004e42e0`, sound.md).

## The light grid (client, 0x4744b0 .. 0x475b20)

- **The grid:** 48 × 48 entries at 0x7b0e68, 8 bytes each: {…, intensity,
  R, G, B}. Its origin is (0x7b0a54, 0x7b0a58), in 8-pixel units.
- **Filling it:**
  - `FUN_004744b0` fills a range with an ambient;
  - `FUN_00474610` fills the ambient for each room near the player;
  - `FUN_004747c0` (x, y, radius, R, G, B) stamps a light.
- **Sampling:** `FUN_00475aa0(x, y)` reads the entry at (x >> 3, y >> 3),
  clamped to 0..47. Tiles and units take it at their vertices
  (`FUN_004dedf0` → `FUN_004f6920` / `FUN_004f6950`, the renderer's
  lit draw).

To trace next:
- the light stamp's falloff (`FUN_004747c0` .. `FUN_00475800`);
- the lights' sources: the player's light radius, and objects'
  LightDiameter and RGB;
- how the DirectDraw renderer turns a vertex light into the PL2 light
  tables (32 levels at PL2 +0x400).
