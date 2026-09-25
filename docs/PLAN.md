# D2Decomp — Plan

Reverse-engineer Diablo II (2000, LoD 2001) into a cross-platform, modern
C++26 engine. Ghidra drives decompilation; reference implementations
(OpenD2, OpenDiablo2, AbyssEngine) guide file-format work.

## Non-goals

- Ship a game with Blizzard assets. Users bring their own ISOs.
- Multiplayer server (Battle.net). Out of scope until singleplayer runs.
- Pixel-perfect renderer. Match behavior, not implementation.

## Phases

Status as of 2026-09-24.

1. **Bootstrap** — repo skeleton, CMake root, docs seeded. *Done.*
2. **Launcher (install path)** — one Qt6 app (`apps/launcher/`, target
   `d2d-launcher`) that reads the 4 D2/LoD ISOs directly (single-header
   `iso9660.hpp`) and copies files into a per-user data dir. *Done.* The
   1.14d patch data is read straight from `LODPatch_114d.exe` (d2d.cfg
   `patch = …`), so there is no separate patch step.
3. **Asset formats** — parsers for MPQ (StormLib), DC6, DCC, DS1, DT1,
   COF, TBL, TXT, plus D2S saves, fonts and palettes, tested against real
   assets (`components/`, `tests/`). *Done;* extended as game code needs.
4. **Ghidra pass** — 1.14d `game.exe`, the Blizzard-provided static PE
   (the old D2Common/D2Game/D2Client/… DLLs are linked into it; no CD
   checks; most modding work targets it). *Ongoing, driven by phase 5:*
   notes per subsystem under `docs/research/re/`.

   The launcher's install step drops the PEs into `<dest>/bin/` when it
   sees them in a source (GOG install → v1.14d; disc `crack/` → v1.00), and
   the `Add patch binaries…` / `Fetch patch…` buttons extract them on
   demand from a Blizzard MPQ-appended installer.

   **`bin/` is dev-only.** Nothing our engine ships against Blizzard's PEs;
   they exist so contributors can open them in Ghidra. Once this phase is
   done, the `bin/` import will be dropped from the launcher — redistributing
   Blizzard's binaries is not permitted, so no release will include them.
5. **Core loop** — main menu, character select, load act 1 rogue camp,
   render tiles. No combat yet. *In progress:* frontend, town, NPCs,
   panels, trade and waypoints work (see README "Status").
6. **Combat + AI** — actor state machine, packet-equivalent events,
   monster AI from game.exe.
7. **Cross-platform polish** — Linux/macOS/Windows CI, controller,
   high-DPI, rebindable input.

## Directory layout

```
apps/           end-user apps: launcher (Qt6 installer), d2d (the game)
components/     shared libraries (one subdir per lib, own CMakeLists.txt)
tests/          all tests. one test_*.cpp per subject, ctest-hooked
tools/          decompilation helpers, format converters, Ghidra scripts
docs/           plan, notes
docs/research/  file format specs, RE notes, references
cmake/          shared CMake modules
```

## Toolchain

- C++26 (`-std=c++2c` / `/std:c++latest`), CMake ≥ 3.28, Ninja.
- System packages: SDL3 (window, input, WAV decode), OpenAL (openal-soft),
  FFmpeg (Bink cinematics), zlib, Qt6 ≥ 6.5 (launcher only).
- `FetchContent`: StormLib (MPQ), CLI11.
- License: GPL-3.0-or-later (matches OpenD2). Compatible with StormLib
  (MIT), SDL3 (Zlib), openal-soft (LGPL), FFmpeg (LGPL), Qt6 (LGPL).

## Reference projects (in `../`)

- **OpenD2** — C, closest to original engine layout. Best for file-format
  code (DCC, DC6, DS1, DT1, COF). Reads like a decompilation.
- **OpenDiablo2** — Go. Best for high-level game logic and asset docs.
- **AbyssEngine** — C, successor to OpenDiablo2 by same author. Sabbat-era
  design; uses Lua for game code.

Inspiration only. We do not copy code — we read, understand, cite in
`docs/research/`, and write our own.

## Ghidra workflow

1. Extract 1.14d PEs (Game.exe + D2*.dll) into a work dir outside the repo.
2. Ghidra project shared under `tools/ghidra/` as scripts + `.gpr` metadata
   (never the raw binaries).
3. Symbol names, structs, enums live in Ghidra Data Type archives (`.gdt`)
   committed under `tools/ghidra/gdt/`.
4. One markdown per function group in `docs/research/re/` with:
   address, signature, callers/callees, prose walkthrough, our C++ port.

## Milestone: "camera pans the rogue camp"

Success = launch our binary, log in with an imported save, camera moves
around a rendered act 1 town. No NPCs interactive. *Reached,* and passed:
NPCs talk and trade. The next milestone is leaving town (phase 5's end).

## Open questions

- Launch page: the plan was for `d2d-launcher` to grow one; today it's two
  binaries (`d2d-launcher` installs, `d2d` runs). Keep it that way unless
  a launch page earns its place.
- Save format: keep 1.14d-compatible or greenfield? Compatible unless it
  hurts.
- Networking: leave off until singleplayer runs.

## Known issues / to investigate

Noted while playing the dev build (2026-09-24):

- **Collision.** Now built like game.exe's room grid (DT1 subtile rows
  were read upside down) with the plus-shaped unit test, and click-to-walk
  paths round obstacles (docs/research/re/collision.md). Left: the tile
  entry flag bits, units blocking each other, doors, and D2's own
  pathing in place of our A*.
- **NPC menu entries.** Not every menu item does something yet:
  - trade/repair, gamble, healing, hire and identify work (see Next up
    for what's approximate);
  - quest topics and the extra entries (Kashya's hire, Warriv's "go
    east") come from the game server (docs/research/re/npc-talk.md).

## Wilderness plan (from 2026-09-25)

Goal: walk out of the Rogue Encampment into a Blood Moor laid out the way
game.exe lays it out, then fill it with monsters.

A. **Act layout** — done: `components/drlg` `act1_layout`, `test_drlg`;
   the town DS1 comes from it (`--seed`, default 3 = townE1).
B. **Outdoor DRLG** — done for the Blood Moor: `components/drlg/outdoor.hpp`
   (borders, border substitution, river/bridge, Den of Evil, roads,
   shrines markers, fills, plain rooms with LvlSub stamps, preset rooms),
   `test_outdoor` over 60 seeds. Gaps, logged as "not implemented" at
   load: preset units (FUN_00667620, so later room seeds drift from
   game.exe), stamp objects (shrines, waypoints), CheckAll stamps, cliff
   caves / cliff styles / waypoints (other act 1 levels), rarity tile
   picks (first DT1 match instead of FUN_0066d820).
C. **Levels from stamps** — done: the generator builds one `ds1::Map`
   for the level; `finish_level` gives it the town's lookup + collision.
D. **Leaving camp** — done: levels know their neighbours (`Level::near`),
   so collision, pathing and drawing carry on across the edge; walk over
   the town's bridge and `Town::cross_level` hands the player (path,
   merc, automap, music) to the Blood Moor. Walking toward a level that
   isn't built (Cold Plains) logs "not implemented". ponytail: the
   neighbour's NPCs aren't drawn across the edge.
E. **Monsters** — spawning done: game.exe's monster region and room
   population (docs/research/re/monsters.md, `components/rules/monsters.hpp`,
   `test_monsters`); the Blood Moor's monsters draw with rolled components
   and wander (MonWndr). Next: AI, combat, death, experience, drops.

## Next up (as of 2026-09-24)

1. Testable game rules: store, prices, item cursor, stat/skill points,
   repair, item generation, gambling and pathing are in
   `components/rules` with `test_rules`. NPC patrol can follow once it's
   off `Scene`.
2. Trade leftovers: magic stock, the real stock roll, charge recharging.
3. NPC menu leftovers: Warriv's "go east", resurrecting a dead merc, the
   hire list's own list widget and offer count, Cain's spawn spot in a
   game where he's already rescued.
4. Waypoint travel and leaving town (Act 1 wilderness DRLG), once more
   fundamentals are in place.
