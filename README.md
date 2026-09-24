# D2Decomp

Cross-platform, GPL-3.0 re-implementation of Diablo II + LoD in C++26,
driven by a Ghidra decompilation of the 1.14d `game.exe`.

**Not affiliated with Blizzard.** You bring your own game data.

See [`docs/PLAN.md`](docs/PLAN.md) for the roadmap, known issues and
what's next; RE notes live in [`docs/research/re/`](docs/research/re/).

## Status

Phase 5 (core loop) is in progress. You can:

- **Frontend**: startup cinematics, title, credits, Cinematics menu,
  character select with your real `.d2s` saves, character create.
- **Rogue Encampment**: rendered town with objects and patrolling NPCs,
  walk/run animations, town music and ambience, automap (Tab).
- **Panels**: inventory, character stats (spend stat points), skill
  tree (spend skill points), belt, stash, Horadric Cube, with item
  tooltips (names, affixes, sets, uniques, runewords, durability).
- **Items**: pick up, move, swap and equip across inventory, stash,
  cube, belt and body slots (class, two-hander and requirement checks).
- **NPCs**: hover names, menus, talk/introduction/gossip speech with
  voice.
- **Trade**: vendor stock, prices, buying and selling with gold;
  repair at Charsi; gambling at Gheed (real magic/rare/set/unique rolls);
  Akara heals; Kashya hires rogues; Deckard Cain identifies (once
  rescued).
- **Mercenary**: your save's merc (or a newly hired one) follows you.
- **Movement**: collision as game.exe builds it; clicks path round
  obstacles.
- **Waypoint**: the panel, act tabs and activation (no travel yet).

It's all single-player and in memory: saves are read, never written.
No combat, no leaving town yet.

## Build

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Needs:

- A C++26 compiler (Clang ≥ 18, GCC ≥ 14, MSVC 17.10+), CMake ≥ 3.28,
  Ninja.
- SDL3, OpenAL (openal-soft), FFmpeg (libavformat, libavcodec,
  libavutil, libswscale, libswresample), zlib, and Qt6 ≥ 6.5 for the
  launcher.
- StormLib and CLI11 come in through FetchContent.

macOS: `brew install sdl3 openal-soft ffmpeg qt`.

## Run

1. Install the game data with `d2d-launcher` (reads the four D2/LoD CD
   ISOs), or point at an existing install's MPQ folder.
2. Start the game: `build/apps/d2d/d2d --data <mpq dir>`.

The MPQ dir is found in this order:

1. `--data`
2. `$D2_MPQ_DIR`
3. `data = …` in `d2d.cfg`
4. the launcher's saved path
5. `~/Workspace/private/diablo2`

`d2d.cfg` lives in the user dir: `~/Library/Preferences/d2d/` on macOS,
`~/.config/d2d/` on Linux. Saves go in `save/` under the same dir. The
1.14d patch data is read straight from Blizzard's installer:

```
patch = /path/to/LODPatch_114d.exe
```

Useful flags:

- `--scale N` sets the window scale; the game renders at 800×600.
- `--no-video` skips the startup cinematics.
- `--start-screen ingame` jumps straight into the game.
- `--headless --devctl <socket>` runs without a window, driven over a
  Unix socket ([`docs/control_channel.md`](docs/control_channel.md)).

Keys in town:

| Key | Action |
|-----|--------|
| I | inventory |
| C | character stats |
| T | skill tree |
| Tab | automap |
| ` | belt |
| R | run toggle |
| Esc | close panels / back to the roster |

## Tests

`ctest` runs the format and unit tests. Tests that need game data skip
when it's missing. Set these to run them:

- `D2_MPQ_DIR` points at the MPQ dir;
- `D2_PATCH_INSTALLER` points at `LODPatch_114d.exe` (1.14d tables and
  town objects);
- `D2_SAVES_DIR` points at real `.d2s` saves (parser checks against them).

`tests/smoke_d2d.py <d2d>` drives a headless game end to end:
character select, stash, Warriv's speech, the waypoint, camera pan.

## Layout

```
apps/        d2d (the game), launcher (Qt6 installer)
components/  format libraries: mpq, dc6, dcc, cof, ds1, dt1, tbl, txt,
             d2s, font, palette, iso9660, devctl, userdir, ...
tests/       one test per subject, plus the smoke test
tools/       Ghidra scripts and project, ISO dump helper
docs/        plan, control channel, research/re notes
```
