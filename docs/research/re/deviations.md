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
`light_grid` (improvement 1), `trans_roof` (improvement 4), `autoloot` (improvement 5),
`autoparty` (improvement 7), `pad_controls` (improvement 11), `coop_lobby` (improvement 12).
`--toggle scalemode=pixelart|nearest|linear` picks the upscale filter (Additions).

## Improvements

| # | What | game.exe | d2d | Why | Visible | Where |
|---|---|---|---|---|---|---|
| 1 | The light grid's size | 48 × 48 subtiles round the player (±24): a light further off adds nothing, and at 800 × 600 the view's corners read the grid's clamped edge (bugs.md #11) | Sized to the view's corners plus the widest light (18 subtiles) | Lights stay lit wherever they are on screen, at any resolution | yes: torches glow from the screen's edges; `--toggle light_grid=off` for game.exe's | light.hpp `LightGrid`, town.hpp `frame_light`, lighting.md |
| 2 | A save's map id of 0 | Reused as the seed (a map from seed 0) | Treated as "none": the run's random seed | d2d's early saves wrote 0 | only for those saves | main.cpp `game_seed`, drlg.md |
| 3 | A wrapped tint byte (Transform 8) | Read back as Transform 8 (`FUN_005038d0`) | The same | — | no | matched since ab51cf0: not a deviation after all (bugs.md #12) |
| 4 | Roofs over the player | Drawn whole: a player under a roof or behind a tent top is hidden (walls in front fade to half, roofs never do; walls.md) | A soft circle round the player's body (radius 70 px) shows through roofs: a quarter of the roof in the inner half, back to solid at the rim. `--toggle trans_roof=off` turns it off | Keep the player, and loot on a hut's floor, in sight | yes | tile_blit.hpp `Hole`, world_view.hpp `g_roof_cutout` |
| 5 | Gold on the ground | Picked up only by clicking it | Walking within half a cell of gold puts it in the purse (on by default). `--toggle autoloot=off` turns it off | Less clicking for coins | yes | world.cpp `autoloot_gold`, main.cpp `g_autoloot` |
| 6 | A joined game's save-back (B3) | Written as it came: magic and size checked (`FUN_0045c520`) | Written only when it parses as the same character with a good +0xc checksum; a .d2s.bak the first time; temp file + rename | A broken or foreign save never replaces the player's own | no | character_store.hpp `save_bytes` |
| 7 | Party in a joined game | The party button: the player invites (0x5e 6) and accepts an invite (8) by hand | d2d invites each other player once and accepts their invites itself (on by default; d2d has no party screen). `--toggle autoparty=off` turns it off | Shared experience without a party UI | yes | netgame.cpp 0x8b, main.cpp `g_autoparty` |
| 8 | When the world loads | Built after char-select's OK, behind the loading screen | Built behind the title: game tables, saves, sprites, then the camp and the levels next to it (overlapping it, its warps, its walk links; in Act 1 that's the Blood Moor) for the newest save's map seed (the likely pick); a save left in a later act also queues that act's town on the builder thread. Picking another character rebuilds for its seed (`set_map_seed`). No toggle | The usual pick is ready the moment OK is clicked | yes: no wait after OK; the log shows those levels building on the title | load.cpp `finish_scene`, main.cpp `set_map_seed` |
| 9 | A joined host that goes silent | Untraced (unverified: game.exe's own drop timeout) | 20 s with no packet from the host (it answers our 0x6d every 5 s) and d2d leaves for character select. No toggle | A host that vanishes (its PC off or asleep) left d2d in a dead game for 18 min, until TCP gave up (live 2026-10-18) | yes: back to character select | netgame.cpp `kSilentMs`, main.cpp |
| 10 | d2d's player far from the host's | Untraced (unverified: game.exe corrects by the host's 0x15 only) | 15+ subtiles apart for 2 s and d2d puts its player where the host has it. No toggle | d2d walked through doors only it had open and on floor the host's path won't take; the host is the truth | yes: a jump back | town.cpp `net_drift_ms` |
| 11 | Configure Controls' buttons | Cancel, Default, Accept, each a third of the panel (FUN_004a5060) | A fourth, Gamepad / Keyboard, turns the list into the pad's page: its settings (left-handed sticks, swapped confirm, dead zone, cursor speed), two layouts, each action's two buttons; edits work as a key's do (a button leaves the action it clashed with), Accept keeps them in d2d.cfg `pad_*`. The four buttons take a quarter each. `--toggle pad_controls=off` turns it off | game.exe has no pad (#33) | yes | controls.cpp, gamepad_map.hpp `Bindings` |
| 12 | The title's SINGLE PLAYER | Single player only; a joined game's players pick on their own machines | "SINGLE PLAYER / COOP": with two or more external pads character select is local co-op's lobby, where each pad past the first picks its character before player 1 (alone) starts. `--toggle coop_lobby=off` turns it off | Couch co-op without picking characters mid-game | yes | lobby.cpp, frontend.cpp `title_ui`, local-coop.md "The lobby" |

## Approximations

| # | What | game.exe | d2d now | Visible | Where |
|---|---|---|---|---|---|
| 1 | Palette blends (additive and multiply missiles, blended shadows, half see-through rain) | PL2 tables: the nearest palette colour of the blend | The blend in RGB | slightly: colours off the palette | world_view.hpp `blit_dcc_frame`, tile_blit.hpp `blit_dt1_shadow`, ingame.hpp `draw_rain` |
| 2 | Lit tiles | 32-pixel blocks, each bilinear between its four corner lights | Each pixel bilinear between the subtile corners round the ground under it | hardly | tile_blit.hpp `blit_dt1_tile_lit` |
| 3 | Unit shadows | Each layer drawn through the alpha table (untraced whether overlaps darken twice) | A composite darkens each pixel once | hardly | world_view.hpp `shadow_composite` |
| 4 | Light Quality | Drops to 0 or 1 by frame rate and the Video Options setting (fewer shadows, capped radii) | Always high (2) | only on slow machines | town.hpp `frame_light` |
| 5 | Static lights' shadows (type 2) | Cached from the collision map (`FUN_004750f0`) | Not built (no such light in the Blood Moor) | not yet | lighting.md |
| 6 | Ambience at dusk and dawn | Cross-fades over 250 sound ticks (`FUN_004e42e0`) | matched since this commit (`Audio::crossfade_ambience`) | — | audio.hpp |
| 7 | Rain's random rolls | The player unit's seed | A client seed of its own | no (same odds) | weather.hpp `Rain::rng` |
| 8 | Where a town portal opens by its caster | Nearest free spot from the caster, collision 0x3e01, size 3 | matched since this commit (`World::portal_spot`) | — | world.cpp `portal_spot` |
| 9 | COF draw effect 6 | The PL2 max-component table (+0x5b500, `FUN_006c8250` mode 6) | Opaque: not ported yet, `blit_dcc_frame` has no max-component mode | on the layer 8 of monster token EG's COFs and object token PY's | world_view.hpp `draw_mode` |
| 10 | JOIN GAME's address check | Connects once to test the host, and shows "Cannot Connect to Server" there (`FUN_00434790`) | Goes straight to char-select; picking a character finds out, with the same popup and OK back to TCP/IP | yes: the error comes one screen later | main.cpp `Screen::TcpIp`, tcpip-menu.md |
| 11 | A host's refusal (B4) | Its message in Font42 over the game screen, since the join runs in the game loop (`FUN_0044cb60`) | The same message in the OK popup on char-select | yes | main.cpp `join_error_text`, tcpip-menu.md |
| 12 | TCP/IP Options' own address | Read again every 5 s (record 0x10b) | Read once as the screen opens | only if the network changes while it's up | main.cpp `Screen::TcpIp` |
| 13 | World sounds' pan, and a loop back in range | Pans by turning the far speaker down to (255 − pan) / 127 (`FUN_00515890`); a Tracking loop stopped past its far range waits, not freed, and starts again back in range (`FUN_004ba020`) | matched since this commit: each world sound is two sources, one per speaker (`AL_SOFT_direct_channels`, `Audio::start_split`); without the extension OpenAL pans a mono source by its angle | only without the extension | audio.hpp `apply_mix` |
| 14 | Where a free-spot search may look (`FUN_0064e7b0` / `FUN_0064e810`) | Each spot's room found from the hint room's near list (`FUN_00463740`): a spot in a room past them counts as none, so the Vulture searches by its target's room, then its own | The whole level's grid (and its neighbours'), so one search; units are the Crowd's | hardly: only a search running past the rooms next to its start | ai.hpp `free_spot_near`, gamedata.hpp `nearest_spot` |

425 smaller shortcuts are marked `ponytail:` in components/ and apps/
(2026-10-31; `git grep -n ponytail: -- components apps | wc -l`); this table holds
the ones a player could notice or a test could trip on.

## Additions

| What | Why |
|---|---|
| `--seed`, `--headless`, `--devctl`, `--no-save`, `--no-video`, `--start-*`, `--toggle` | Scripted tests and development; game.exe's own switches are kept (dropin_goal) |
| The devctl channel and its `debug` verbs (docs/control_channel.md) | Driving the game in tests |
| `D2D_ANIM_LOG=<file>`: each animation's first draw appended to the file (`Scene::note_drawn`) | Animation coverage (tools/anim_coverage.py); nothing is written when it isn't set |
| Borderless fullscreen: Alt+Enter, `fullscreen = 1` in d2d.cfg, the launcher's Fullscreen box | game.exe's own display modes and `-w` aren't traced; borderless doesn't change the desktop's resolution
| The window's upscale filter: `scalemode = pixelart` (default; nearest with blended edges, SDL 3.4's `SDL_SCALEMODE_PIXELART`, plain nearest on SDL 3.2), `nearest` or `linear` in d2d.cfg, the launcher's Scaling dropdown, `--toggle scalemode=` | game.exe draws 800×600 unscaled; d2d's bigger windows and fullscreen stretch it, and pixel art stays sharp where linear blurred it (Thomas, 2026-10-28) |
| Widescreen (`--widescreen`, `widescreen = 1`, `--view-width`): the in-game world draws wider than 800 pixels, at the window's aspect (at most 1400); the HUD and panels stay 800×600, centred. Off by default | Modern displays. A wider view shows more of the map than game.exe ever did (it is a client view only: monsters update 30 cells out, the host replicates 28, the view is at most 13 across); the UI isn't re-laid-out yet (#39) |
| Local co-op: up to 4 players in one game on one screen (docs/design/local-coop.md). They are one party: experience, NoDrop and gold go by game.exe's party rules (party.md). One screen keeps them on one level: a warp, a portal, a level's edge or a respawn takes the whole party. Players walk through each other, and a step that would spread the party past one screen is undone (`World::leash`). Death with a partner alive makes a ghost (gear kept, D2's penalty) that a partner's Resurrect channel raises, or a town; everyone dead respawns in town as in single player. Quest credit is one rule for every quest (`World::credit_party`: what the acting player gained, a partner who hasn't done that quest gains), where game.exe has a callback per quest | Friends on one Steam Deck; game.exe has no shared-screen play
| Gamepads (SDL3; apps/d2d/README.md, docs/design/controller-plan.md): stick walking, soft targeting with the target hovered and an "A  Talk" prompt, the right stick aiming in the field, d-pad hops through every panel, shoulders through a menu's buttons, rebindable in d2d.cfg `pad_*` and Configure Controls' pad page | Couch and Steam Deck play; game.exe has no pad
| An NPC's windows (store, menu, speech) shut once the player is more than 2 cells (10 subtiles) from the NPC, as Esc (`Town::update`) | game.exe's two panels cover its whole 800-pixel world, so it can't walk away with them open; a widescreen view or a click past them can. 2 cells is d2d's (game.exe talks within 6 subtiles)
| A join that fails for reasons game.exe can't hit (a name a host won't take, a host that never finishes the join, a desync) shows d2d's own words in the OK popup | Say why, instead of only logging it |

## How to add one

Add a row when d2d does something differently from game.exe on purpose:
what, game.exe's behaviour, d2d's, why, whether it's visible, and where.
An approximation leaves when the real thing lands; keep its row as
"matched since <commit>" once or twice so the history is findable.
