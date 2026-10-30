# ghidra: decompiling game.exe

Turns game.exe (1.14d) back into readable C, so d2d can be written to
do exactly what it does.

## Run it

Decompile one function to a file:

```sh
H=$(command -v ghidra-analyzeHeadless || echo "$(brew --prefix ghidra)/libexec/support/analyzeHeadless")
$H tools/ghidra/project D2Decomp -process game.exe -noanalysis -readOnly \
   -scriptPath tools/ghidra/scripts -postScript DecompileRange.java 00584870 00584a00 /tmp/f.c
cat /tmp/f.c
```

First time ever? Do "One-time" and "Import" below first (~1 minute).

## Why it exists

- game.exe is the ground truth. Every rule in d2d cites the game.exe
  function it came from (`FUN_00584870` and so on).
- Headless scripts make it scriptable: one command, one answer, no GUI.

## The scripts (tools/ghidra/scripts)

| Script | Args | Gives you |
|---|---|---|
| `DecompileRange.java` | `<start> <end> <out.c>` | C for every function in a range |
| `DecompileAt.java` | addresses | C for functions at entry points (creates them if Ghidra missed them) |
| `Disasm.java` | `<start> <end>` | raw instructions (shows register args the decompiler hides) |
| `Xrefs.java` / `XrefsTo.java` | address | who uses this address |
| `CallArgs.java` | targets, range | the arguments at every call to a function |
| `FieldUses.java` | base, field | every use of a struct field |
| `FindPattern.java` | hex bytes | where a byte pattern sits |
| `DumpBytes.java` | range, file | raw bytes as hex |
| `ExportCalls.java` | out dir | `game-calls.tsv` (every caller → callee) and `game-srcfiles.tsv` (the source file each function's asserts name), for `tools/gaps.py` |

Each run takes ~2.5 s; batch several `-postScript` calls in one run.

## When it breaks

| You see | Fix |
|---|---|
| Empty output for an address | Code reached only through a table isn't a function yet: use `DecompileAt.java` or `Disasm.java` |
| `analyzeHeadless` not found | `brew install ghidra`; the `H=` line above finds it under `brew --prefix ghidra` |
| Project missing | Run `tools/ghidra/import.sh` once |

## Reference

### One-time

```
brew install ghidra    # 12.x, ships analyzeHeadless + JDK
```

Run the launcher first so `game.exe` (v1.14d, ~3.5 MB monolith) lands in
`<dataPath>/bin/`. Everything below reads from that.

### Import + auto-analyze

```
tools/ghidra/import.sh
```

Creates `tools/ghidra/project/` (gitignored), imports `game.exe`, runs
default analysis, then runs `ExportOverview.java` and `ExportCalls.java`,
which drop:

- `docs/research/re/game-overview.md`
- `docs/research/re/game-{functions,strings,imports,exports,calls,srcfiles}.tsv`

Rerun any time — `-overwrite` replaces the imported program. TSVs are
grep-friendly and check into git, except `game-strings.tsv`: those are
game.exe's own bytes (LEGAL.md), so it stays local. The Ghidra project
itself never goes in.

### Interactive session

```
ghidraRun
```

Open project: `tools/ghidra/project/D2Decomp.gpr`. Do RE work there.
Findings → `docs/research/re/<subsystem>.md`.

### Data Type archives

Structs/enums we recover live under `gdt/` as `.gdt` files (Ghidra
Data Type archives). Attach in Ghidra: *File → Open File System… →
.gdt* then drag types onto listing.

### Tables copied from game.exe

A constant table copied from game.exe into the code gets an entry in
`tests/test_exe_tables.cpp` (`kTables`: name, address, check). The test
compares it element-wise with the real game.exe 1.14.3.71
(`D2_GAME_EXE`, else `$D2_MPQ_DIR/bin/game.exe`, else
`~/Workspace/private/diablo2/bin/game.exe`; it skips without one). A
table local to a function moves to namespace scope in its header so the
test can see it.

### What NOT to commit

- The Ghidra project directory (`project/`).
- Any Blizzard-owned bytes (the imported program lives inside the
  project — that's why the whole dir is gitignored), including
  `game-strings.tsv`.
