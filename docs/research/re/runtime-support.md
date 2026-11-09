# Runtime support routines (0x4059d0 .. 0x409490) — game.exe 1.14d

Small helpers every other module calls: the internal-error path, the error
box, the logging start-up and the pool-block list. Read from the Ghidra
decompile (`python3 tools/ghidra/fn.py <addr>`); the asm was not read, so
every register argument is the decompiler's (`unaff_` / `in_` names), and
no oracle ran: all of it is **cited**, none proven. d2d ports none of it
(it has no Windows error box, no pool list, no Save-folder lookup).

## The internal-error path (`FUN_00408090`)

- **`FUN_00408090`** is 4 bytes and the decompile returns `unaff_retaddr`:
  it hands back its caller's return address. The sites that pass a constant
  (`FUN_00408090(0x44)`, `(199)`, `(0x261)`) pass it as an argument the
  decompiled body does not use, so the number is a source line for the reader of
  the asm, not a value. Every site then runs the same pair:
  `FUN_00408a60(&DAT_006cc837, <that address>)` and `FUN_00681e09(-1)`
  (exit; the decompile marks it as not returning). The other docs write it
  as "assert line N" (collision.md "Building a path", items.md, chat.md);
  this is why: the number is the line only in that sense, and the value
  reported at run time is a code address.
- Callers in this work list: `FUN_004059d0` (lines 0x133 / 0x134),
  `FUN_00406d30` (0x44), `FUN_00409370` (0x261), `FUN_00409490` (0x2b4 /
  0x2bf), `FUN_00648120` (199), `FUN_00649970` (no argument).
- `FUN_00408b20` and `FUN_00408ce0` use it as the argument of a different
  report, `"Unrecoverable internal error %08x"` through `FUN_00408900`
  (lines 0x8de, 0x576, 0x577), see below.
- d2d: not ported (a d2d assert is C++'s own). Open question, not traced:
  the text of `DAT_006cc837` (the strings TSV is not in this checkout), so
  it is not claimed here to be a file name.

## The mode run (`FUN_004059d0`)

- **`FUN_004059d0`** (no arguments; its only caller is `FUN_00405c30`, which
  stores the application mode at `0x0074c704`, switches.md "The
  application mode"). With `m = DAT_0074c704`: a mode outside 1 .. 5
  (`modstate0` 0, or past `expand`) returns 0. Else it reads the pair
  `{make, done}` at `0x706554 + 8 * m` / `0x706558 + 8 * m`:
  - `make` null: the internal error with line 0x133; `done` null: line
    0x134 (`FUN_00408090`, above).
  - Else `0x0074c700 = make()`; `result = (*(*0x0074c700)[0])()` (the first
    virtual method of the object `make` returned, called with no
    arguments in the decompile); `done()`; return `result`.
- So each of the five modes `client`, `server`, `multiplayer`, `launcher`
  and `expand` has its own object and cleanup in that table; which
  functions the five pairs hold was not read (the table's bytes were not
  read here), and so what each mode runs is untraced.
- d2d: not ported; it has no application mode (switches.md).
