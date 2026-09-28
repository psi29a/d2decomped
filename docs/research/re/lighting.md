# Lighting and time of day — game.exe 1.14d

d2d: `components/rules/light.hpp` (`Day`, `LightGrid`, test_light),
`frame_light` (town.hpp) and `Lighting` / `blit_dt1_tile_lit` (world.hpp).

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
| +0x1c / +0x24 | the sun's direction (−cos, sin; sin halved at night) |
| +0x28 | scale: frames per degree, 128 (0x7443e4; 4 in the eclipse) |
| +0x30 | eclipse |

- **Phases:** each is {start degree, type, RGB}, 12 bytes. Normal (0x7443f0):

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
  `FUN_0061bfc0`): time + 1 a frame. Night phases (type 2) add one more;
  Act 4 adds 15 and Act 3 at night 9. The time wraps at 360 × scale. When
  the time passes the next phase's start × scale, the phase moves on and
  the time snaps to that start.
- **The day is 340° long** (bugs.md #9): phase 2 starts at 0°, so "0 <
  time" is true the first frame of phase 1, which moves straight on to
  sunrise. With each phase running a frame past its end, a day is 35 846
  frames, about 24 minutes.
- **Intensity** (`FUN_0061bb80`): angle a = time / scale degrees;
  intensity = (int)(sin(a) × 128 + 128 + 0.5), the sine halved for
  a ≥ 180°, clamped to 0 and to 255 (170 in Act 5). Sunrise 128, noon
  255 (256 clamped), midnight 64. Level 120 is fixed at 200. Act 4 levels
  103..106 step toward 0x80 / 0x40 / 0x38 / 0x30. The eclipse drops by 8
  down to 32.
- **Colour** (`FUN_0061bce0`): blended from the phase's RGB toward the
  next phase's by how far the time is between them. Level 120 is fixed at
  (245, 240, 255).
- **The level's own light:** `FUN_00619d70` gives the level's Levels.txt
  Intensity / Red / Green / Blue. When any is non-zero, its Intensity
  replaces the day's (`FUN_00474550`). Act 1's caves (8..): 0, (255, 255,
  255): dark but for the lights.
- **Ambient sound:** day while the phase index is 1..3 (`FUN_004e42e0`
  via `FUN_0061c220`), so 0° to 180°; else night.

## The light grid (client, 0x4744b0 .. 0x475b20)

- **The grid:** 48 × 48 subtiles at 0x7b0e68, 8 bytes each: {u32 blocks
  light, intensity, R, G, B}. `FUN_00475800` centres it on the player
  each frame (origin 0x7b0a54 / 0x7b0a58 = the player's subtile − 24),
  fills it with the ambient (`FUN_00474610` → `FUN_004744b0`), marks
  subtiles whose collision has 0x22 (`FUN_004756d0`), then updates each
  light in the list at 0x7b5668 (`FUN_004755a0`).
- **A light** (`FUN_00474160(unit, type, radius, intensity, r, g, b)`,
  0x44 bytes): type, unit id, position in eighths of a subtile (the
  unit's 16.16 position >> 13, + 4), radius × 8 (capped at 18 subtiles),
  the radius it eases to (8 a frame), intensity, RGB.
  - the player: radius 13 + the light radius bonus (`FUN_00460930`), 255,
    white;
  - monsters: MonStats2 Light / light-r/g/b (a unit's own light-radius
    stat if higher);
  - objects: objects.txt Lit<mode> / Red / Green / Blue, 255;
  - missiles: Missiles.txt Light / Red / Green / Blue.
- **Stamping** (`FUN_004748d0`): over the light's square, each subtile
  corner takes (r − d) × (intensity << 16) / r >> 16, d the distance
  `FUN_004740d0` (0.96 · the longer side + 0.4 · the shorter, in
  1/1024ths). `FUN_004747c0` adds it in, capped at 255; with coloured light
  on, the RGB is the intensity-weighted mean. At quality 0 other players'
  lights are capped at radius 16, type 1 at 8, type 3 skipped.
- **Shadows** (quality 2, `FUN_00475780` by frame rate): the player's
  light (type 0) is stamped by `FUN_00474d70`, static ones (type 2) by
  `FUN_004750f0`. Both first fill a 64 × 64 table from the blocked
  subtiles, spread it outward ring by ring (`FUN_00474b50` /
  `FUN_00474c00`: each subtile takes a blend of the two nearer to the
  light), and scale the light by (8 − blocked / 2) / 8.
- **Sampling:** `FUN_00475aa0(x, y)` reads the entry at (x >> 3, y >> 3),
  clamped to 0..47.

## Drawing with it (DirectDraw, video mode 3)

- **Tiles:** a floor's 6 × 6 subtile corners take the grid's light
  (`FUN_004de260`); a wall's columns take it along its base
  (`FUN_004dedf0`, count by orientation from 0x6db9d8). The driver
  (0x72f6d0 +0x9c / +0xa0: 0x5131b0 / 0x5130a0) draws each 32-pixel
  block with its corner lights through the game's callbacks (0x72da60).
- **A block** (`FUN_004f8120`, `FUN_004f84f0`): nearly even corners
  draw flat at level light >> 3 (`FUN_004f8050`), level 31 unlit
  (`FUN_004f7ea0`); otherwise a 32 × 32 table of levels bilinear between
  the corners (`FUN_004f71a0`), each pixel through the light table.
- **The light table** (0x7d2348): 32 levels × 256, the act's PL2 +0x400.
  Level 31 is the identity, 0 black.
- **Colour** isn't drawn here: the software path reads the intensity
  only. The RGB is for Glide / Direct3D (coloured light is on by default,
  0x712b8c, a debug key toggles it).

## Shadows

- **Options:** Video Options "Blended Shadows" (registry, default on;
  `FUN_004f5200` sets 0x72da5c) and "Light Quality" (the grid's shadows).
- **The alpha tables:** PL2 +0x3500, three 256 × 256 tables (D2WinPalette
  copies them to 0x7ee468.. and hands the driver pointers in that order,
  `FUN_004fb010`). Table k mixes T[a][b] = a · (1 − w) + b · w with
  w = 0xbf, 0x7f, 0x3f / 255 (`FUN_00605880`), nearest palette colour.
- **Tile shadows** (orientation 13): driver +0xa4 (0x512f90) draws each
  block through callback `FUN_004f82d0` at alpha 0xc0 → table 0 as
  T[tile][ground]: a quarter of the tile over three quarters of the ground,
  unlit. With Blended Shadows off, opaque.
- **Unit shadows:** driver +0x90 (0x5122e0 → `FUN_00601730` →
  `FUN_00608d60`) draws the frame from its bottom row up, every other row,
  each output row one up and one left: half as tall, leaning up-left from
  (x + xoff + yoff / 2, y + yoff / 2). Each pixel becomes T0[ground][0]:
  a quarter of the ground (black when not blended). MonStats2 Shadow says
  which monsters cast one (all of them in 1.14d).

## d2d

- The grid is sized to the view plus the widest light, not 48 (bugs.md
  #11, deviations.md #1).
- `Day` steps in the World a tick at a time and goes to the client in the
  View; `frame_light` fills a `LightGrid` round the player with the
  level's light or the day's, and stamps the player (13), objects (Lit by
  mode), monsters and missiles.
- `render_world` draws floors and walls through `blit_dt1_tile_lit`: each
  pixel bilinear between the subtile corners round the ground it's over,
  found from its screen position (floor tiles are 128 tall and reach past
  their own cell); a wall's column where it crosses the cell's middle. One
  level for a tile when all the corners it reaches share it. Units take the level at their
  feet; additive missiles and overlays aren't lit.

- Tile shadows blend at a quarter (`blit_dt1_shadow`); players', NPCs'
  and monsters' composites cast shadows after the floor pass
  (`shadow_composite`), each pixel once however many layers cover it.

- Walls shadow the player's and objects' lights (`stamp_shadowed`, as
  `FUN_00474d70`: the rings, the blend toward the light, 16 behind a
  blocked subtile); monsters' and missiles' lights (type 1) aren't.
  Light Quality is taken as high.
- Light types from their callers of `FUN_00474160`: the player
  (0x460cf0) and objects (0x4ae2ee) type 0; missiles (0x470555) and
  MonStats2 lights (0x4c56d7, 0x4cdac5) type 1; two static ones type 2
  (0x4bc5bc, 0x4d6f85: cached shadows, `FUN_004750f0`).

- The player's radius is 13 + item_lightradius (stat 89) summed over what's
  worn, charms and sockets (the View's `light_bonus`), capped at 18.

- The Den of Evil's light once cleared: quests.md "The Den lights up".

ponytail, not yet: type 2's cached shadows, lights easing to a new radius,
Act 3 / 4 days.

## The unit under the cursor

`FUN_00471ec0` (the unit draw): when the unit is the one under the cursor
(`FUN_00467a10`), its light is doubled and held to 0x40..0xff before it's
drawn, so a hovered monster stands out in the dark. Objects then also
draw in mode 7 instead of 5 (3 / 4 by their draw type); mode 7's table
isn't traced. d2d: `Unit::highlight`, last frame's hovered unit.

Found on the way: `FUN_00476ea0` loads `Data\Global\Monsters\RandTransforms.dat`
(0x1e00: 30 colour tables for monsters) and `GreenBlood.dat` (256, the
censored blood, when a unit's +0x1a2 is set and a setting file says '1').
