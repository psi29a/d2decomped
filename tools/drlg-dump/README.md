# drlg-dump

Prints a level that d2d's level generator (DRLG) builds, as text, so it
can be diffed against the same level from game.exe.

## Run it

Diff one level against game.exe in one go (needs [tools/emu](../emu/README.md) set up):

```sh
cd tools/emu
uv run python diff_drlg.py 1-20 2          # map seeds 1..20, level 2 (Blood Moor)
```

It runs drlg-dump and game.exe's generator side by side. Good:
`level 2: 20/20 seeds match game.exe`. A mismatch prints the first
differing lines, game.exe's above d2d's.

## Why it exists

- game.exe is the ground truth. Same seed in, same text out, or d2d
  has a bug.
- Text diffs show exactly which room, tile or object went wrong.

## More uses

| You want | Command |
|---|---|
| Other checks per seed | `diff_drlg.py 1-20 2 tiles` (also `units seeds monsters objgroups drops collision game npcs layout`) |
| One level by hand | `build/tools/drlg-dump/drlg-dump ~/Workspace/private/diablo2 3 2` (seed 3, level 2) |
| Every room's tiles | add `tiles` at the end |
| Monsters per room | add `monsters` (`DIFFICULTY=0..2`) |
| Random object groups | `drlg-dump <dir> <seed> <level> objgroups` |
| Collision grids | `drlg-dump <dir> <seed> <level> collision` (`ORDER=shuffle\|reverse\|list`) |
| Every level's collision checksum, libd2's form | `drlg-dump <dir> 1-200 0 crc` (`DIFFICULTY=0..2`; `tools/emu/libd2_crc.py`) |
| A whole game's seeds and containers | `drlg-dump <dir> <seed> 0 game` (`LEVELS=2,8,4,9`) |
| Many seeds to files | `drlg-dump <dir> 1-300 2 out/` writes `out/<seed>.txt` |
| Every level, all cores | `cd tools/emu && ./sweep.sh 1-50 monsters objgroups` |

The full list of modes is in the comment at the top of `drlg-dump.cpp`.

## When it breaks

| You see | Fix |
|---|---|
| `diff_drlg.py` says a seed differs | Diff that seed's two files by hand; the first differing room is the place to look |
| Can't find data | Pass your MPQ folder as the first argument |
| emu side fails | See [tools/emu](../emu/README.md) "When it breaks" |
