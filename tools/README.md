# tools/

Scripts, converters, and RE helpers used during decompilation.

| dir | what | run |
|---|---|---|
| `ghidra/` | Headless Ghidra import, decompile, disasm and xref scripts. Findings go to `docs/research/re/`. See its README. | `tools/ghidra/import.sh` |
| `emu/` | game.exe under unicorn: call its functions and diff them against the port. Includes the DRLG and drop oracles. See its README. | `cd tools/emu && uv run python drlg.py 3` |
| `mpq-cat/` | Writes files from the game's MPQ stack (1.14d patch first) to stdout. | `build/tools/mpq-cat/mpq-cat <mpq dir> 'data\global\excel\Levels.txt'` |
| `drlg-dump/` | Our level generator's output in `emu/drlg.py`'s text form. Trailing `tiles` / `units` / `seeds` / `objgroups` add each per room (the last matches `emu/objgroups.py`'s dump for diffing). | `build/tools/drlg-dump/drlg-dump <mpq dir> <seed> [level] [mode]` |
| `drop-dump/` | Our drop rolls (jobs on stdin) in `emu/drops.py`'s text form; `tables` prints each class's entries. `emu/drops.py` runs it. | `build/tools/drop-dump/drop-dump <mpq dir> [tables] < jobs` |
| `iso-dump/` | Lists or extracts the install ISO. | `build/tools/iso-dump/iso-dump` |
| `validate-linux.sh` | The CI Linux job (configure + build + clang-tidy + ctest) in a docker image with deps baked in (see `validate-linux.Dockerfile`) against the current tree; catches checks the mac clang doesn't run (identifier-length, include-cleaner) before a push. Uses its own `build-linux/`; the deps image is (re)built only when the Dockerfile changes. | `tools/validate-linux.sh` |

Python tools are uv projects with their own `.venv` (`uv sync`, then
`uv run`), never system pip.

Anything longer than ~100 lines gets its own subdirectory with a
README naming inputs, outputs, and how to run it.
