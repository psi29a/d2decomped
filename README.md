# D2Decomp

D2Decomp is a cross-platform, open-source reimplementation of the Diablo II:
Lord of Destruction game engine. It aims to reproduce the original game's
behavior, using version 1.14d as its reference. The long-term goal is a
drop-in replacement, with optional features beyond the original game.

## What to expect

D2Decomp is an unfinished, single-player project—not the original game or a
complete replacement. Its current playable slice covers character setup, the
Rogue Encampment, the Blood Moor, and the Den of Evil.

| Area | Progress |
|---|---:|
| [Full roadmap](docs/PLAN.md) | Phases, known gaps, and next steps |
| Current playable slice | ✅ 100% |
| Launcher and asset formats | ✅ 100% |
| Act I levels | ~7% (2 of about 30 built) |
| Act I quests | ~17% (1 of 6 implemented) |

## Bring your own game

You need your own Diablo II + Lord of Destruction game data. D2Decomp does not
bundle Blizzard's game files or assets; it reads data from your installation or
media. The project is not affiliated with or endorsed by Blizzard
Entertainment.

## How it works

[Ghidra](https://github.com/NationalSecurityAgency/ghidra) and
[Unicorn](https://www.unicorn-engine.org/) are the tools that make the reverse
engineering possible. Ghidra helps us understand the original game's code;
Unicorn runs original game functions so their behavior can be checked against
this reimplementation.

## Beyond the original game

**Available now:** Linux support alongside Windows and macOS, plus an optional
headless mode and local control channel for scripted play and testing.

**Planned:** finish Act I and continue through the rest of the campaign; add
multiplayer and dedicated-server play; and support controllers, high-DPI
displays, and remappable controls. These features are not implemented yet.

## Build and run

You'll need a C++26 compiler, CMake 3.28+, Ninja, SDL3, OpenAL, FFmpeg, zlib,
StormLib, and Qt 6.5+ for the launcher.

```sh
cmake -S . -B build -G Ninja
cmake --build build
build/apps/d2d/d2d --data /path/to/your/MPQ-folder
```

## License

D2Decomp is licensed under the GNU General Public License, version 3 or later
(GPL-3.0-or-later).
