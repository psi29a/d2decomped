# Ghidra workflow

## One-time

```
brew install ghidra    # 12.x, ships analyzeHeadless + JDK
```

Run the launcher first so `game.exe` (v1.14d, ~3.5 MB monolith) lands in
`<dataPath>/bin/`. Everything below reads from that.

## Import + auto-analyze

```
tools/ghidra/import.sh
```

Creates `tools/ghidra/project/` (gitignored), imports `game.exe`, runs
default analysis, then runs `ExportOverview.java`, which drops:

- `docs/research/re/game-overview.md`
- `docs/research/re/game-{functions,strings,imports,exports}.tsv`

Rerun any time — `-overwrite` replaces the imported program. TSVs are
grep-friendly and check into git; the Ghidra project itself does not.

## Interactive session

```
ghidraRun
```

Open project: `tools/ghidra/project/D2Decomp.gpr`. Do RE work there.
Findings → `docs/research/re/<subsystem>.md`.

## Data Type archives

Structs/enums we recover live under `gdt/` as `.gdt` files (Ghidra
Data Type archives). Attach in Ghidra: *File → Open File System… →
.gdt* then drag types onto listing.

## Tables copied from game.exe

A constant table copied from game.exe into the code gets an entry in
`tests/test_exe_tables.cpp` (`kTables`: name, address, check). The test
compares it element-wise with the real game.exe 1.14.3.71
(`D2_GAME_EXE`, else `$D2_MPQ_DIR/bin/game.exe`, else
`~/Workspace/private/diablo2/bin/game.exe`; it skips without one). A
table local to a function moves to namespace scope in its header so the
test can see it.

## What NOT to commit

- The Ghidra project directory (`project/`).
- Any Blizzard-owned bytes (the imported program lives inside the
  project — that's why the whole dir is gitignored).
