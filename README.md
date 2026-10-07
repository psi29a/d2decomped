# D2Decomp

D2Decomp is a cross-platform, open-source reimplementation of the Diablo II:
Lord of Destruction game engine. It aims to reproduce the original game's
behavior, using version 1.14d as its reference. The long-term goal is a
drop-in replacement, with optional features beyond the original game.

## Why

I want to play Diablo II with my daughter, natively on Linux. No Wine, no
emulation layer: a game engine that runs on our machines, reads the game
data we own, and lets us adventure together.

## What to expect

D2Decomp is an unfinished project—not the original game or a
complete replacement. All of Act I plays through: character setup, the Rogue
Encampment, every area from the Blood Moor to Andariel's lair, and all six
quests. Act II isn't built yet, so Warriv's caravan goes nowhere. d2d can also join
an Act I game hosted by the original game over TCP/IP.

| Area | Progress |
|---|---:|
| [Full roadmap](docs/PLAN.md) | Phases, known gaps, and next steps |
| Launcher and asset formats | ✅ 100% |
| Act I levels | ✅ 100% (38 of 38; maps match the original's) |
| Act I quests | ✅ 100% (6 of 6) |
| Act I matching the original exactly | Maps, item drops, monster and object placement ✅; a few details left |
| Acts II–V | Not started |
| Single player, softcore and hardcore | ✅ Runs as a one-player client/server, like the original |
| Multiplayer over TCP/IP (a d2d host) | Not started |
| Joining a game hosted by the original | 🚧 Act I together: party, trade, portals, waypoints, chat; live checks pending |
| Dedicated server | Not started |

## Bring your own game

You need your own Diablo II + Lord of Destruction game data. D2Decomp does not
bundle Blizzard's game files or assets; it reads data from your installation or
media. The project is not affiliated with or endorsed by Blizzard
Entertainment. [Legal notes](LEGAL.md) explain how EU law allows
reverse engineering for interoperability, and how the project stays within it.

## How it works

[Ghidra](https://github.com/NationalSecurityAgency/ghidra) and
[Unicorn](https://www.unicorn-engine.org/) are the tools that make the reverse
engineering possible. Ghidra helps us understand the original game's code;
Unicorn runs original game functions so their behavior can be checked against
this reimplementation.

## Beyond the original game

**Available now:** Linux support alongside Windows and macOS, plus an optional
headless mode and local control channel for scripted play and testing.
Small comforts, each switchable off with `--toggle`: gold is picked up by
walking over it, and roofs turn see-through around your character.

**Planned:** continue through the rest of the campaign; let d2d host multiplayer
games and run a dedicated server; and support controllers, high-DPI displays, and remappable controls. These features are not implemented yet.

## Build and run

You'll need a C++26 compiler, CMake 3.28+, Ninja, SDL3, OpenAL, FFmpeg, zlib,
StormLib, and Qt 6.5+ for the launcher.

```sh
cmake -S . -B build -G Ninja
cmake --build build
build/apps/d2d/d2d --data /path/to/your/MPQ-folder
build/apps/d2d/d2d --join 192.168.1.10   # an original game's TCP/IP host, your LAN only
```

You can also join from the menus: Other Multiplayer → TCP/IP Game → Join
Game, type the host's address, then pick a character. d2d remembers the
address as `last_tcp_ip` in `d2d.cfg`. Alt+Enter switches between a window
and borderless fullscreen; `fullscreen = 1` in `d2d.cfg` starts that way.

d2d looks for the game data in this order, and the first hit wins:
1. `--data` (or `$D2_MPQ_DIR`).
2. A `d2data.mpq` beside the binary or in the working directory.
3. `data =` in `d2d.cfg`.

If the first hit is wrong, d2d stops with an error and doesn't try the
next one. The launcher (`d2d-launcher`) finds an installed Diablo II and writes `data =`
(and `patch =` for a non-1.14d install, pointing at `LODPatch_114d.exe`)
into `d2d.cfg`. That file lives in `~/Library/Preferences/d2d` on macOS,
`~/.config/d2d` on Linux, and `Documents\My Games\d2d` on Windows.
Your install is read in place and never changed. Its Fullscreen box sets
`fullscreen =`. Launch saves the settings, starts d2d and closes the launcher.

## License

D2Decomp is licensed under the GNU General Public License, version 3 or later
(GPL-3.0-or-later); the full text is in [LICENSE](LICENSE).
