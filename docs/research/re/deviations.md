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

**Toggles:** an improvement that changes what a player sees can be turned
off with `--toggle name=off[,name=off...]` (apps/d2d/main.cpp `kToggles`):
`trans_roof` (improvement 4), `autoloot` (improvement 5), `autoparty` (improvement 7).

## Improvements

| # | What | game.exe | d2d | Why | Visible | Where |
|---|---|---|---|---|---|---|
| 1 | The light grid's size | 48 × 48 subtiles round the player (±24): a light further off adds nothing, and at 800 × 600 the view's corners read the grid's clamped edge (bugs.md #11) | Sized to the view's corners plus the widest light (18 subtiles) | Lights stay lit wherever they are on screen, at any resolution | yes: torches glow from the screen's edges | light.hpp `LightGrid`, town.hpp `frame_light`, lighting.md |
| 2 | A save's map id of 0 | Reused as the seed (a map from seed 0) | Treated as "none": the run's random seed | d2d's early saves wrote 0 | only for those saves | main.cpp `game_seed`, drlg.md |
| 3 | A wrapped tint byte (Transform 8) | Read back as Transform 8 (`FUN_005038d0`) | The same | — | no | matched since ab51cf0: not a deviation after all (bugs.md #12) |
| 4 | Roofs over the player | Drawn whole: a player under a roof or behind a tent top is hidden (walls in front fade to half, roofs never do; walls.md) | A soft circle round the player's body (radius 70 px) shows through roofs: a quarter of the roof in the inner half, back to solid at the rim. `--toggle trans_roof=off` turns it off | Keep the player, and loot on a hut's floor, in sight | yes | world.hpp `Hole`, `g_roof_cutout` |
| 5 | Gold on the ground | Picked up only by clicking it | Walking within half a cell of gold puts it in the purse (on by default). `--toggle autoloot=off` turns it off | Less clicking for coins | yes | world.cpp `autoloot_gold`, main.cpp `g_autoloot` |
| 6 | A joined game's save-back (B3) | Written as it came: magic and size checked (`FUN_0045c520`) | Written only when it parses as the same character with a good +0xc checksum; a .d2s.bak the first time; temp file + rename | A broken or foreign save never replaces the player's own | no | character_store.hpp `save_bytes` |
| 7 | Party in a joined game | The party button: the player invites (0x5e 6) and accepts an invite (8) by hand | d2d invites each other player once and accepts their invites itself (on by default; d2d has no party screen). `--toggle autoparty=off` turns it off | Shared experience without a party UI | yes | netgame.cpp 0x8b, main.cpp `g_autoparty` |
| 8 | When the world loads | Built after char-select's OK, behind the loading screen | Built behind the title: game tables, saves, sprites, then the town and the levels next to it (overlapping it, its warps, its walk links; in Act 1 that's the Blood Moor) for the newest save's map seed (the likely pick). Picking another character rebuilds for its seed (`set_map_seed`). No toggle | The usual pick is ready the moment OK is clicked | yes: no wait after OK; the log shows those levels building on the title | load.cpp `finish_scene`, main.cpp `set_map_seed` |

## Approximations

| # | What | game.exe | d2d now | Visible | Where |
|---|---|---|---|---|---|
| 1 | Palette blends (additive and multiply missiles, blended shadows, half see-through rain) | PL2 tables: the nearest palette colour of the blend | The blend in RGB | slightly: colours off the palette | world.hpp `blit_dcc_frame`, `blit_dt1_shadow`, ingame.hpp `draw_rain` |
| 2 | Lit tiles | 32-pixel blocks, each bilinear between its four corner lights | Each pixel bilinear between the subtile corners round the ground under it | hardly | world.hpp `blit_dt1_tile_lit` |
| 3 | Unit shadows | Each layer drawn through the alpha table (untraced whether overlaps darken twice) | A composite darkens each pixel once | hardly | world.hpp `shadow_composite` |
| 4 | Light Quality | Drops to 0 or 1 by frame rate and the Video Options setting (fewer shadows, capped radii) | Always high (2) | only on slow machines | town.hpp `frame_light` |
| 5 | Static lights' shadows (type 2) | Cached from the collision map (`FUN_004750f0`) | Not built (no such light in the Blood Moor) | not yet | lighting.md |
| 6 | Ambience at dusk and dawn | Cross-fades over 250 sound ticks (`FUN_004e42e0`) | matched since this commit (`Audio::crossfade_ambience`) | — | audio.hpp |
| 7 | Rain's random rolls | The player unit's seed | A client seed of its own | no (same odds) | weather.hpp `Rain::rng` |
| 8 | Where a town portal opens by its caster | Nearest free spot from the caster, collision 0x3e01, size 3 | Nearest free spot 0.6 cells south of the player | slightly | server.hpp `open_portal` |
| 9 | COF draw effects 0..2 | Driver alpha modes (which of 25 / 50 / 75 % each is, untraced) | Read as a quarter, half, three quarters | only on layers that use them | world.hpp `layer_trans` |
| 10 | JOIN GAME's address check | Connects once to test the host, and shows "Cannot Connect to Server" there (`FUN_00434790`) | Goes straight to char-select; picking a character finds out, with the same popup and OK back to TCP/IP | yes: the error comes one screen later | main.cpp `Screen::TcpIp`, tcpip-menu.md |
| 11 | A host's refusal (B4) | Its message in Font42 over the game screen, since the join runs in the game loop (`FUN_0044cb60`) | The same message in the OK popup on char-select | yes | main.cpp `join_error_text`, tcpip-menu.md |
| 12 | TCP/IP Options' own address | Read again every 5 s (record 0x10b) | Read once as the screen opens | only if the network changes while it's up | main.cpp `Screen::TcpIp` |

About 200 smaller shortcuts are marked `ponytail:` in the code; this table
holds the ones a player could notice or a test could trip on.

## Additions

| What | Why |
|---|---|
| `--seed`, `--headless`, `--devctl`, `--no-save`, `--no-video`, `--start-*`, `--toggle` | Scripted tests and development; game.exe's own switches are kept (dropin_goal) |
| The devctl channel and its `debug` verbs (docs/control_channel.md) | Driving the game in tests |
| Borderless fullscreen: Alt+Enter, `fullscreen = 1` in d2d.cfg, the launcher's Fullscreen box | game.exe's own display modes and `-w` aren't traced; borderless doesn't change the desktop's resolution |
| A join that fails for reasons game.exe can't hit (a name a host won't take, a host that never finishes the join, a desync) shows d2d's own words in the OK popup | Say why, instead of only logging it |

## How to add one

Add a row when d2d does something differently from game.exe on purpose:
what, game.exe's behaviour, d2d's, why, whether it's visible, and where.
An approximation leaves when the real thing lands; keep its row as
"matched since <commit>" once or twice so the history is findable.
