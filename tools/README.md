# Tools and apps: start here

Every tool in the repo, one line each. Each folder has a README with
the command to run first.

## Apps (what players or testers run)

| Folder | What it is | First command |
|---|---|---|
| [apps/d2d](../apps/d2d/README.md) | The game | `build/apps/d2d/d2d --data ~/Workspace/private/diablo2` |
| [apps/launcher](../apps/launcher/README.md) | Finds or installs the game data, then starts d2d | `open build/apps/launcher/d2d-launcher.app` (macOS) |
| [apps/whisper](../apps/whisper/README.md) | A remote control inside the real game.exe | `./whisper.py 192.168.50.68 TOKEN state` |
| [apps/d2proxy](../apps/d2proxy/README.md) | Sits between a client and a game.exe host and logs every packet | `build/apps/d2proxy/d2proxy --host 192.168.50.68` |
| [assets/icon](../assets/icon/README.md) | The icon and the launcher's banner | nothing to run |
| [assets/flatpak](../assets/flatpak/README.md) | Linux / Steam Deck packaging: desktop entry and app info | built by CI, nothing to run |

## Tools (for comparing d2d against game.exe)

| Folder | What it is | First command |
|---|---|---|
| [tools/emu](emu/README.md) | Runs game.exe's own code in an emulator, as the ground truth | `cd tools/emu && uv sync` |
| [tools/ghidra](ghidra/README.md) | Decompiles game.exe | `tools/ghidra/import.sh` |
| [tools/drlg-dump](drlg-dump/README.md) | Prints d2d's level layout as text, to diff against game.exe's | `build/tools/drlg-dump/drlg-dump ~/Workspace/private/diablo2 3` |
| [tools/drop-dump](drop-dump/README.md) | Prints d2d's item drops as text, to diff against game.exe's | `cd tools/emu && uv run python drops.py 1-1000` |
| tools/missile-dir-dump | Prints d2d's missile frame directions, to diff against game.exe's (`tools/emu/missile_dirs.py`) | `build/tools/missile-dir-dump/missile-dir-dump table` |
| [tools/mpq-cat](mpq-cat/README.md) | Prints a file from the game's archives | `build/tools/mpq-cat/mpq-cat ~/Workspace/private/diablo2 'data\global\excel\Levels.txt'` |
| [tools/net-join](net-join/README.md) | Joins a game.exe host, logs the packets, leaves | `build/tools/net-join/net-join save.d2s 192.168.50.68` |
| [tools/iso-dump](iso-dump/README.md) | Lists or extracts a Diablo II disc image | `build/tools/iso-dump/iso-dump list disc.iso` |

## Scripts

| File | What it is | Run it |
|---|---|---|
| `validate-linux.sh` | CI's Linux build, clang-tidy and tests, in Docker (`validate-linux.Dockerfile`; its own `build-linux/`). Run before every push | `tools/validate-linux.sh` (exit status 0 = good) |
| `tidy-pr-select.py` | CI only: picks the files a PR needs clang-tidy on; checks in `test_tidy_pr_select.py` | `python3 -m unittest tools/test_tidy_pr_select.py` |
| `sound_coverage.py` | Lists the base game's sounds d2d can't play yet, by the table column or game.exe code that plays them (needs a build, for mpq-cat) | `tools/sound_coverage.py ~/Workspace/private/diablo2` |
| `anim_coverage.py` | Lists the base game's animations (player, monster and object COFs, missiles, overlays, item drops) d2d hasn't drawn in the runs given, by kind and mode. d2d logs what reaches the screen when `D2D_ANIM_LOG=<file>` is set (needs a build, for mpq-cat). `--audit`: the MPQs' own gaps instead, LoD too: COF layers with no DCC, DCCs short of their mode's frames or directions, sprites the data names that no MPQ holds (6 s) | `D2D_ANIM_LOG=$PWD/build/anims.log ctest --test-dir build -R 'quests_act\|_d2d'`, then `tools/anim_coverage.py ~/Workspace/private/diablo2 build/anims.log` |
| `d2d_smoke.py` | Starts a no-save headless game, takes a screenshot and closes its process | `tools/d2d_smoke.py --data /path/to/mpqs --wait 1` |
| `coverity.py` | Lists Coverity Scan issues with file:line, shows one, marks them False Positive / Intentional / Bug (auth: `--help`) | `tools/coverity.py list` |

## Before any of them

1. Build once: `cmake -S . -B build -G Ninja && cmake --build build`
2. For the tools that read game data (d2d, mpq-cat, the dumps, emu,
   net-join, d2proxy's game.exe tables): your own Diablo II + LoD files
   (the MPQs). The examples use `~/Workspace/private/diablo2`; put your
   folder there instead.
3. For the 1.14d patch data, set
   `D2_PATCH_INSTALLER=/path/to/LODPatch_114d.exe`.

## Rules for new tools

- Python tools are uv projects with their own `.venv` (`uv sync`, then
  `uv run`), never system pip.
- Anything longer than ~100 lines gets its own folder and a README in
  this shape: what it is, the command to run first, why it exists, more
  uses, when it breaks. Then add its line to the tables above.
