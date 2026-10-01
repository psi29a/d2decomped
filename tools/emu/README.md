# emu — game.exe as an oracle

Runs game.exe (1.14d) code under [unicorn](https://www.unicorn-engine.org/)
so a C++ port can be diffed against the real thing, one function at a
time or a whole subsystem. There is no Windows or wine involved. The PE
is mapped at its image base, and the few Win32 imports the code paths
touch are stubbed in Python.

## Setup

```
cd tools/emu
uv sync                      # .venv with unicorn, pefile, capstone
```

Needs the launcher's `bin/game.exe` (found through the launcher's
`game.dataPath` default), the MPQs in that same directory, and a built
`build/tools/mpq-cat/mpq-cat`. The 1.14d patch data comes from
`$D2_PATCH_INSTALLER`, which defaults to
`~/Downloads/Diablo II + LoD/patch/LODPatch_114d.exe`.

## What's there

- `emu.py`: the core `Emu` class.
  - `call(addr, *stack_args, ecx=, edx=)` calls a function. It covers
    stdcall, fastcall and cdecl (the caller pops nothing). It returns EAX.
  - `hook(addr, fn, nstack)` replaces a game.exe function with Python.
  - `alloc`/`put`/`r32`/`s32`/`w32`/`read`/`cstr` handle memory. The heap
    is a bump allocator that never frees.
  - `crt_init()` runs the static CRT's heap and thread-data startup. It
    also hooks the fatal-error handler (0x408a60, which raises
    `SystemExit` with the message) and the string-table lookup
    (0x524d30, which returns `""`).
  - `serve_files(e)` answers Storm's file calls (open, size, read, seek,
    close, name) from the MPQ stack through `mpq-cat`. Files are cached
    in `.cache/`, so the first run is slow and later runs are fast.
  - Win32 stubs are `w_<ImportName>` methods that return
    `(eax, stdcall arg count)`. An unstubbed import stops the run with its
    name and a stack of return addresses, so you add them as you go.
  - `backtrace()` scans the stack for code addresses. It's a heuristic,
    not an unwind.
- `drops.py`: the drop roller (FUN_0055a6d0) as an oracle. It diffs
  `build/tools/drop-dump` over a seed range (`drops.py 1-100000`), or
  every treasure class's entries (`drops.py tables`), made items
  (`drops.py items`), containers opened (`drops.py objects`) and armor
  stands' / weapon racks' items (`drops.py stands`). Item creation is
  hooked, so only the dropper's seed moves. See
  `docs/research/re/drops.md`.
- `drlg.py`: the level generator as an oracle.
  - `boot()` loads every data table (FUN_00619300(0, 1, 1), about 1.4 s
    warm), including the LvlPrest and LvlSub DS1s.
  - `alloc_act(e, act, seed, level)` is FUN_006194a0, server side. It
    builds `level` straight away.
  - `dump()` prints a level in the same text form as
    `build/tools/drlg-dump/drlg-dump`.

```
uv run python drlg.py 3              # the Blood Moor for map seed 3
uv run python drlg.py 1-300 2 out/   # out/<seed>.txt for seeds 1..300
```

Diff it against the port:

```
build/tools/drlg-dump/drlg-dump ~/Workspace/private/diablo2 3 > ours.txt
(cd tools/emu && uv run python drlg.py 3) > game.txt && diff game.txt ours.txt
```

- `objgroups.py`: each room's preset units and random object groups
  (FUN_005559a0, FUN_00552610) as an oracle: the room seed in and out,
  the object seed, and every object made. The regression check for
  `components/game/objgroups.cpp`, which should report every seed matching
  for every Act 1 level:

```
uv run python diff_drlg.py 1-20 2 objgroups     # level 2..39
uv run python diff_drlg.py 1-20 2 drops         # then three items dropped at each group object (FUN_00555da0)
./sweep.sh 1-50 monsters objgroups              # every level 2..39, 8 at a time (JOBS=n)
```

- `sight.py`: a monster's line of sight (FUN_00622920 → FUN_0064e260)
  on random walls in a fake room, against a line-for-line Python copy of
  `rules::sight_blocked`. `uv run python sight.py 20000 3` should print
  `ok`. See `docs/research/re/monster-ai.md`.
- `search.py`: a hostile monster's target search (FUN_005dd7f0) on random
  players with pets in the player lists, against `rules::search_pick` and
  ai.cpp's sight and flag rules. `uv run python search.py 20000 2` should
  print `ok`.
- `collision` (drlg.py `collision_dump`): the level's rooms brought up out
  of list order ($ORDER shuffle, the default, reverse or list), then every
  room's collision grid (room1 +0x20), against `relevel`'s:
  `uv run python diff_drlg.py 1-10 2 collision`, `./sweep.sh 1-10 collision`.
- `game` (objgroups.py `game_dump`): one game, $LEVELS (default 2,8,4,9)
  made in turn, each level's rooms brought up in $ORDER and populated newest
  first, then every container they made opened (drops.py's openers): per
  room the room1 seed, the room seed, the object and game seeds, the objects
  made; per container its drops and the seeds after. The check for the one
  object seed and the room order: `uv run python diff_drlg.py 1-20 0 game`.
- `units` brings game.exe's rooms up in $ORDER first (drlg.py `bring_up`):
  the units and warps come out the same in any order.

## Adding a new oracle

1. Find the entry point and its calling convention with Ghidra.
   `Disasm.java` shows the register args the decompiler drops (see
   `tools/ghidra/README.md`).
2. Call it with `boot()`-style setup, then read the result structs back
   (offsets go in the RE doc).
3. When it faults, the message names the eip and the stack. Stub the
   import, or `hook` the function if it reaches outside what you're testing
   (UI, network, sound).
4. Print the result in a text form that a small C++ tool beside the port
   also prints, then diff.

ponytail: `__cinit` (C++ static constructors) isn't run, because its C
initialisers want locale setup. Add it when a global constructor turns out
to matter.
