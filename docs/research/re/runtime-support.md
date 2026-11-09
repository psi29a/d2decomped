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

## The per-user folder path (`FUN_00406d30`, with `FUN_004067d0` / `FUN_00406b20`)

- **`FUN_00406d30`** (buffer in ESI, size in EDI; both registers as the
  decompile names them; only caller `FUN_00407050`; `FUN_004067d0` and
  `FUN_00406b20` are fastcall in the same decompile): a size of 0 returns;
  a null buffer raises the internal error, line 0x44 (`FUN_00408090`).
  - `FUN_004067d0(buf, size)` asks `SHGetKnownFolderPath` (looked up in
    `Shell32.dll` with `GetProcAddress`; a null result skips it) for the
    folder id at `0x006cca6c` with flags `0x8000`, and converts the wide
    result to the buffer (`WideCharToMultiByte`, code page 0, flags
    0x400, the used-default flag checked). It returns 1 when that worked
    without a substituted character, 0 otherwise (also for the call
    missing or failing).
  - Worked: `FUN_00406b20` (make sure the buffer ends in `\`; 0 when
    there is no room: the length + 1 equals the size) and then
    `FUN_00413800(buf, "Diablo II", size)` (the string joiner the other
    docs call "builds the path", switches.md; chat-ignorelist.md).
  - Did not: `SHGetFolderPathA(0, 5, 0, 0, ...)` (CSIDL 5, My Documents);
    a non-zero result returns with the buffer as it was; else
    `FUN_00406b20` and `FUN_00413800(buf, "Diablo II\\Save", size)`.
    The decompile gives the local 260-byte array as the folder call's
    output and never reads it afterwards, which cannot be what the code
    does (the call's output is the buffer); the asm was not read, so
    this is the decompiler's register loss, not a finding.
- So the result is a per-user folder plus `Diablo II`, or (fallback)
  `<CSIDL 5>\Diablo II\Save`. Which folder the id at `0x006cca6c` names
  was not read (16 bytes of data); `0x8000` is the create flag of
  `SHGetKnownFolderPath` by its documented value, not read in game.exe.
- Open (switches.md lists it): whether `FUN_00407050` uses this for
  `D2.ini`'s directory. Not traced here.
- d2d: not ported (its settings and saves folder is its own, apps/d2d/README.md).

## The error box and the start-up of the error system (`FUN_00408b20`, `FUN_00408ce0`)

- **`FUN_00408b20`** (fastcall; the decompile names `param_1` the error
  class, `param_2` a with-location flag, `param_3` the file / line
  argument of `FUN_00408000`, `param_4` a printf format, `param_5` its
  `va_list`; the call table lists no caller; how it is reached was not
  traced): a report.
  - With `param_2`: it writes `"--------  FILE: %s    LINE: %d  --------\n\n"`
    to the log (`FUN_00413a40` formats, `FUN_004106b0(&DAT_006cc837,
    &DAT_006ffc58, text)` writes).
  - A null format is itself fatal: `FUN_00408900(6, &DAT_006cc837, 0x8de,
    "Unrecoverable internal error %08x", FUN_00408090())`, then
    `thunk_FUN_0040f8a0` and `FUN_00681e09(-1)`.
  - The message is `__vsnprintf` into 0x1000 bytes. With `param_2` the
    box text is `"%s\nLocation : %s, line #%d\nExpression : %s"` with the
    class name from the pointer table at `0x706654` (its first entry is
    `"No Error"`; `param_1` indexes it, unchecked), else the message alone.
  - The title is `"%s Error"` with `"Diablo II"`; the text goes to the
    log (`FUN_00410740`) and `MessageBoxA(0, text, title, 0x30)` (0x30 =
    `MB_ICONEXCLAMATION`, by its documented value). It returns to the
    caller (the fatal exit is the caller's).
- **`FUN_00408ce0`** (fastcall; `param_2` a string, `param_3` a flag; caller
  `FUN_004064c0`): runs its body once (`DAT_0074f0d0` marks it done);
  later calls only run `FUN_004076b0()` when `param_3` is non-zero.
  - The first call: `FUN_00403f60(0)`; `FUN_00401740()` if `FUN_00406b60()`
    is non-zero; fills the error-report record at `0x0074d868`: the
    texts `"Diablo II"` three times, the version
    `sprintf("D2 %i.%i%c", 1, 0xe, 100)` = **`D2 1.14d`** (stored at
    `0x0074d8c8`), `"Windows"`, `".ClassicErrorReporter"` twice; copies
    `param_2` (up to 0x1000) to `0x0074c848` when given.
  - Two critical sections (`0x0074c830`, `0x0074d848`) are initialised;
    the `"Diablo II"` / `"TraceUnfreedMemory"` value (default 0) is read
    with `FUN_00414f10` into `0x0074f0e0`; `FUN_00409dd0(0, "Global Pool
    System", 0x40000, 0x8000)` makes the first pool.
  - A self-check of two 32-entry dword tables: `0x006ce268[i]` must be
    `1 << i` and `0x006ce2e8[i]` its complement, else the "Unrecoverable
    internal error %08x" report with line 0x576 / 0x577 and exit.
    (`bits_set`, `FUN_00410b10`, indexes the first.)
  - Then `FUN_00407de0`, `FUN_00410540`, `FUN_004076b0()` when `param_3`,
    `_atexit(0x00407f20)`, `FUN_0040d5b0` and
    `SErrSetBlizzardErrorFunction(FUN_00408660)`: Storm's error callback
    is `FUN_00408660`.
- d2d: not ported (no error box, no pool system, no error-reporter
  record; its crash reports are the launcher's own).

## The pool's sorted block list (`FUN_00409370`, `FUN_00409490`)

A pool (ESI in both; `FUN_00409dd0` makes one) keeps its blocks sorted by a
key: +4 a critical section, +0x7a0 the count, +0x7a4 the capacity, +0x7a8
the array of 8-byte entries `{block, key}`, +0x17a8 a running byte total.
A block is `param_1`: `*param_1` is its key (an unsigned dword), `param_1[5]`
points at a record whose +0x1c and +0x20 give its size. Offsets are as the
decompile shows them; the asm was not read.

- **`FUN_00409370`** (block): enters the section. When count >= capacity
  the capacity grows by 0x20 and the array is `_realloc`ed to
  capacity * 8; a null result prints `"(1) Pool Blocks overflowed at %d"`
  (`FUN_00409020`) and raises the internal error, line 0x261. The total
  grows by `*(rec +0x20) + 0x18 + ((*(rec +0x1c) + 0x1f) >> 5) * 4`. A
  binary search finds the first entry whose key is not below the block's
  (a lower bound), the tail moves up one entry (`_memmove`), the new entry
  `{block, *block}` is stored and the count rises. Leaves the section.
- **`FUN_00409490`** (block): the reverse. The same lower-bound search, but
  an entry is taken when its **key equals** the block's key (the block
  pointer is not compared), so with two blocks of one key it removes
  whichever the search lands on. It moves the tail down, drops the count,
  takes the same size sum off the total, and when count + 0x40 < capacity
  shrinks the capacity by 0x20 (`_realloc`; a null result prints the same
  overflow message, line 0x2b4). Leaves the section. A key not found
  leaves the section and raises line 0x2bf.
- The line numbers are the internal-error sites of `FUN_00408090` above.
  The list is only inserted into and removed from here; who reads it was
  not traced.
- d2d: not ported (it uses the C++ allocator).
