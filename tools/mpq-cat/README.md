# mpq-cat

Prints a file from Diablo II's archives (MPQs) to the terminal.

## Run it

```sh
build/tools/mpq-cat/mpq-cat ~/Workspace/private/diablo2 'data\global\excel\Levels.txt' > /tmp/levels.txt
```

Use single quotes: the paths use backslashes.

## Why it exists

- Reading the game's tables (Levels.txt, MonStats.txt, ...) while
  reverse-engineering, without unpacking anything.
- It reads the archives in d2d's order (the 1.14d patch first), so you
  see the same bytes d2d does.
- tools/emu uses it to hand files to game.exe's code.

## More uses

| You want | Command |
|---|---|
| Several files in a row | `mpq-cat <dir> 'file1' 'file2' > out` |
| Many files, each to its own file (heads only) | `mpq-cat -d out/ -n 64 <dir> 'file1' 'file2'` (missing ones skipped) |
| One column of a table | `mpq-cat <dir> 'data\global\excel\Levels.txt' \| cut -f1,2` |
| A sprite or sound for another tool | `mpq-cat <dir> 'data\global\ui\cursor\hand.dc6' > hand.dc6` |

## When it breaks

| You see | Fix |
|---|---|
| `mpq-cat: ... not found` | Check the path's spelling; it's case-sensitive inside the archive |
| 1.14d rows missing | Set `D2_PATCH_INSTALLER=/path/to/LODPatch_114d.exe`, or put `patch_d2.mpq` beside the MPQs |
| `usage: ...` | It needs the MPQ folder first, then at least one file |
