# Where d2d differs from game.exe on purpose

d2d aims to behave like game.exe 1.14d. This file lists where it doesn't,
knowingly, and why, so each difference is a decision rather than an
accident. Its companion is bugs.md: game.exe's own mistakes, most of which
d2d copies.

**Kind:**
- *improvement*: d2d does better than game.exe on purpose; game.exe's
  behaviour is documented, and a switch could bring it back if a test or a
  player needs it.
- *approximation*: d2d gets close but not exact, for now. It should go once
  the real thing is built (the code carries a `ponytail:` note).
- *addition*: something game.exe doesn't have (dev tooling, CLI switches),
  with no effect on play.

**Visible:** can a player tell?

## Improvements

| # | What | game.exe | d2d | Why | Visible | Where |
|---|---|---|---|---|---|---|
| 1 | The light grid's size | 48 × 48 subtiles round the player (±24): a light further off adds nothing, and at 800 × 600 the view's corners read the grid's clamped edge (bugs.md #11) | Sized to the view's corners plus the widest light (18 subtiles) | Lights stay lit wherever they are on screen, at any resolution | yes: torches glow from the screen's edges | light.hpp `LightGrid`, town.hpp `frame_light`, lighting.md |
| 2 | A save's map id of 0 | Reused as the seed (a map from seed 0) | Treated as "none": the run's random seed | d2d's early saves wrote 0 | only for those saves | main.cpp `game_seed`, drlg.md |
| 3 | A wrapped tint byte (Transform 8) | Read back as Transform 8 (`FUN_005038d0`) | The same | — | no | matched since ab51cf0: not a deviation after all (bugs.md #12) |
| 4 | Roofs over the player | Drawn whole: a player under a roof or behind a tent top is hidden (walls in front fade to half, roofs never do; walls.md) | A soft circle round the player's body (radius 70 px) shows through roofs: a quarter of the roof in the inner half, back to solid at the rim. `--vanilla-roofs` or `roof_cutout = 0` in d2d.cfg turns it off | Keep the player, and loot on a hut's floor, in sight | yes | world.hpp `Hole`, `g_roof_cutout` |

## Approximations

| # | What | game.exe | d2d now | Visible | Where |
|---|---|---|---|---|---|
| 1 | Palette blends (additive and multiply missiles, blended shadows, half see-through rain) | PL2 tables: the nearest palette colour of the blend | The blend in RGB | slightly: colours off the palette | world.hpp `blit_dcc_frame`, `blit_dt1_shadow`, ingame.hpp `draw_rain` |
| 2 | Lit tiles | 32-pixel blocks, each bilinear between its four corner lights | Each pixel bilinear between the subtile corners round the ground under it | hardly | world.hpp `blit_dt1_tile_lit` |
| 3 | Unit shadows | Each layer drawn through the alpha table (untraced whether overlaps darken twice) | A composite darkens each pixel once | hardly | world.hpp `shadow_composite` |
| 4 | Light Quality | Drops to 0 or 1 by frame rate and the Video Options setting (fewer shadows, capped radii) | Always high (2) | only on slow machines | town.hpp `frame_light` |
| 5 | Static lights' shadows (type 2) | Cached from the collision map (`FUN_004750f0`) | Not built (no such light in the Blood Moor) | not yet | lighting.md |
| 6 | Ambience at dusk and dawn | Fades across (`FUN_004e42e0`) | Switches at once | yes, briefly | town.hpp |
| 7 | Rain's random rolls | The player unit's seed | A client seed of its own | no (same odds) | weather.hpp `Rain::rng` |
| 8 | Where a town portal opens by its caster | Nearest free spot from the caster, collision 0x3e01, size 3 | Nearest free spot 0.6 cells south of the player | slightly | server.hpp `open_portal` |
| 9 | COF draw effects 0..2 | Driver alpha modes (which of 25 / 50 / 75 % each is, untraced) | Read as a quarter, half, three quarters | only on layers that use them | world.hpp `layer_trans` |

About 200 smaller shortcuts are marked `ponytail:` in the code; this table
holds the ones a player could notice or a test could trip on.

## Additions

| What | Why |
|---|---|
| `--seed`, `--headless`, `--devctl`, `--no-save`, `--no-video`, `--start-*` | Scripted tests and development; game.exe's own switches are kept (dropin_goal) |
| The devctl channel and its `debug` verbs (docs/control_channel.md) | Driving the game in tests |

## How to add one

Add a row when d2d does something differently from game.exe on purpose:
what, game.exe's behaviour, d2d's, why, whether it's visible, and where.
An approximation leaves when the real thing lands; keep its row as
"matched since <commit>" once or twice so the history is findable.
