# D2Decomp — Plan

Reverse-engineer Diablo II (2000, LoD 2001) into a cross-platform, modern
C++26 engine. Ghidra drives decompilation; reference implementations
(OpenD2, OpenDiablo2, AbyssEngine) guide file-format work.

## Non-goals

- Ship a game with Blizzard assets. Users bring their own ISOs.
- Multiplayer server (Battle.net). Out of scope until singleplayer runs.
- Pixel-perfect renderer. Match behavior, not implementation.

## Phases

1. **Bootstrap** — repo skeleton, CMake root, docs seeded. *(this commit)*
2. **Launcher (install path)** — one Qt6 app (`apps/launcher/`, target
   `d2d-launcher`) that reads the 4 D2/LoD ISOs directly (single-header
   `iso9660.hpp`) and copies files into a per-user data dir; later applies
   the 1.14d patch. Same binary grows a launch page in phase 5. *(next)*
3. **Asset formats** — parsers for MPQ, DC6, DCC, DS1, DT1, COF, TBL, TXT.
   Reuse Storm/StormLib for MPQ. Write tests against real assets.
4. **Ghidra pass** — decompile `Game.exe`, `D2Common.dll`, `D2Game.dll`,
   `D2Client.dll`, `D2Gfx.dll`, `D2Win.dll`, `D2Sound.dll`. Version = 1.14d
   (the Blizzard-provided static PE — no CD checks, most modding work
   targets it). Document one DLL per doc under `docs/research/re/`.

   The launcher's install step drops these PEs into `<dest>/bin/` when it
   sees them in a source (GOG install → v1.14d; disc `crack/` → v1.00), and
   the `Add patch binaries…` / `Fetch patch…` buttons in the launcher extract
   them on demand from a Blizzard MPQ-appended installer (StormLib handles
   the appended-MPQ format). Any future patch drops into the same flow with
   no code change.

   **`bin/` is dev-only.** Nothing our engine ships against Blizzard's PEs;
   they exist so contributors can open them in Ghidra. Once this phase is
   done, the `bin/` import will be dropped from the launcher — redistributing
   Blizzard's binaries is not permitted, so no release will include them.
5. **Core loop** — main menu, character select, load act 1 rogue camp,
   render tiles. No combat yet.
6. **Combat + AI** — actor state machine, packet-equivalent events,
   monster AI from Game.exe.
7. **Cross-platform polish** — Linux/macOS/Windows CI, controller,
   high-DPI, rebindable input.

## Directory layout

```
apps/           end-user apps: launcher (install + launch), the game itself
components/     shared libraries (one subdir per lib, own CMakeLists.txt)
tests/          all tests. one test_*.cpp per subject, ctest-hooked
tools/          decompilation helpers, format converters, Ghidra scripts
docs/           plan, notes
docs/research/  file format specs, RE notes, references
cmake/          shared CMake modules
```

## Toolchain

- C++26 (`-std=c++2c` / `/std:c++latest`), CMake ≥ 3.28, Ninja.
- Libraries via `FetchContent`. First adds, when needed:
  - SDL3 (windowing, input, audio)
  - StormLib (MPQ) — LGPL, fine to link.
  - fmt, spdlog, cli11 as needed.
- No Qt for now — one dep, huge. ImGui + SDL3 covers the installer GUI.
- License: GPL-3.0-or-later (matches OpenD2). Compatible with StormLib
  (MIT), SDL3 (Zlib), ImGui (MIT).

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
around a rendered act 1 town. No NPCs interactive. That is the north star
for phases 2–5.

## Open questions

- Ship as one monolith or `d2d-installer` / `d2d-launcher` / `d2d` binaries?
  Leaning three binaries so the installer stays boring.
- Save format: keep 1.14d-compatible or greenfield? Compatible unless it
  hurts.
- Networking: leave off until singleplayer runs.
