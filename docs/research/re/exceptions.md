# Fog's stack crawl (`.\Src\Exceptions.cpp`, 0x40b3f0 .. 0x40d7d0) — game.exe 1.14d

The code that writes a stack dump into the error log: it loads
`dbghelp.dll` (or `imagehlp.dll`), lists the process's modules, loads their
symbols and walks the stack. `.\Src\Exceptions.cpp` is the file name these
functions pass to the debug allocator (`0x6cd628`, with `FUN_0040b380` /
`FUN_0040b3c0`); `game-srcfiles.tsv` lists the same file for them. Read from
the Ghidra decompile (`python3 tools/ghidra/fn.py <addr>`). The register
arguments were checked in the asm. No oracle ran, so all of it is
**cited**, none proven.

d2d ports none of it. Its crash reports are Sentry's (off unless turned on
in the launcher), and it writes no stack dump of its own
(`git grep -n -i "stack crawl\|dbghelp" -- apps components` finds nothing).

The log lines go to the log id `DAT_006cc837` unless a function below
names another (`FUN_00410660`, the
`log_printf_to` of src-frontier.md; `FUN_00410740` and `FUN_004106b0` write
without a line end).

## How it is reached

- **`FUN_0040d5b0`** (cited in runtime-support.md as a call of the error
  system's start-up `FUN_00408ce0`) runs once (`DAT_0075d014` set means
  done). It initialises the critical section `0x0075cfbc` and installs
  **`FUN_0040d540`** with `FUN_00684af3`. That CRT function swaps the
  `_translator` field of the per-thread data (`__getptd`), so this is
  `_set_se_translator`: `FUN_0040d540` becomes the structured-exception
  translator of the thread that runs the start-up.
- It then loads `.\dbghelp.dll`, else `dbghelp.dll`, else `imagehlp.dll`
  (none: `"Missing imagehlp.dll.  Can't generate better stack crawls!"`,
  return 0) into `DAT_0075cfd8`. It looks up 14 functions:
  - `0x75cfdc` SymCleanup, `0x75cfe0` SymFunctionTableAccess,
    `0x75cfe4` SymGetLineFromAddr and `0x75cfe8` SymGetModuleBase;
  - `0x75cfec` SymGetModuleInfo, `0x75cff0` SymGetOptions,
    `0x75cff4` SymGetSymFromAddr and `0x75cff8` SymInitialize;
  - `0x75d004` SymSetOptions, `0x75cffc` SymLoadModule,
    `0x75d000` SymUnloadModule and `0x75d008` StackWalk;
  - `0x75d00c` UnDecorateSymbolName and `0x75d010` MiniDumpWriteDump.

  All but SymGetLineFromAddr and MiniDumpWriteDump must be found. Then
  `DAT_0075d014` = 1 and the log says `"imagehlp.dll loaded for better
  stack crawls."`. `FUN_0040b500` undoes it: it clears the flag, frees the
  library and deletes the critical section.
- **`FUN_0040d540`** (the translator: an exception code and an
  `EXCEPTION_POINTERS` on the stack) logs `"ExceptionHandler: imagehlp.dll
  not loaded"` when the flag is clear. Otherwise it duplicates the current
  thread's pseudo-handle into a real one (`DuplicateHandle`, options 2 =
  same access) and calls `FUN_0040cc10` with ECX = that thread handle and
  EDX = the exception's context record (`[arg2 + 4]`). It closes the handle
  after.

## The stack crawl (`FUN_0040cc10`)

ECX = a thread handle, EDX = a `CONTEXT`. It returns 1 after a dump, 0
when it stops early.

- The registry value `Diablo II` / `NoStackDump` (default 0, read with
  `FUN_00414f10`) non-zero: it logs `"sShowStack: Stack dump turned off in
  registry"` and returns 0. The flag `DAT_0075d014` clear: it logs
  `"sShowStack: imagehlp.dll not loaded"` and returns 0.
- It duplicates the current process's pseudo-handle into a real one. Then
  `SymInitialize(process, path, 0)` with the search path of `FUN_0040c290`
  (below) built in a 0x1000-byte stack buffer. A failure logs
  `"SymInitialize(): gle = %lu"`, runs SymCleanup, closes the handle and
  returns 0.
- `SymSetOptions(SymGetOptions() | 0x10)`; `FUN_0040c0f0` (process id,
  process handle) lists the modules and loads their symbols;
  `FUN_0040bcf0` writes the annotated stack dump; `FUN_0040b660` frees the
  list; `FUN_0040f960` flushes the logs.
- `"Stack Crawl:"` and the header `"Address  Frame    Module       Return
  Stack"`. The `STACKFRAME` starts from the context's Eip (+0xb8), Ebp
  (+0xb4) and Esp (+0xc4), all three in mode 3. Up to 0x40 frames, each a
  `StackWalk(0x14c, process, thread, &frame, context, 0,
  SymFunctionTableAccess, SymGetModuleBase, 0)`. A failed step logs
  `"Stack walk error: %d"` and ends the walk; so does the third frame
  whose symbol is not found (`SymGetSymFromAddr`, into a symbol record with
  room for a 0x100-byte name).
- Per frame, one line: the address, the frame pointer and the module file
  name after its last `\` (`FUN_0040c520`), as `"%08x %08x %-12s "`; then
  `"%s!"` with the module name when `SymGetModuleInfo` works; then
  `"%s+%04X "`, the symbol (`"unknown"` when not found) and its
  displacement.
  - `"[omap] "` when the symbol's flags (+0xc) have bit 0 or 1.
  - The frame's FPO record (`FuncTableEntry`) when there is one. Its frame
    type (the top two bits of the word at +0xe) 0: `"(FPO: [SEH])"` with
    bit 0x800 of that word, else `" (FPO:"`, `" [EBP 0x%08x]"` with bit
    0x1000, and `" [%d, %d, %d])"` (the word at +0xc, the dword at +8 and
    the byte at +0xf, low 3 bits).
  - Frame type 3: `"(FPO: Non-FPO [%d, %d, %d])"` with the same fields.
    Any other type: `"(UNKNOWN FPO TYPE)"`.
  - The source file after its last `\` and the line, `"%*s %s, %d"`, when
    `FUN_0040b600` found a line.
- `"%d frames dumped"`, a flush, SymCleanup and the process handle closed.

### The module list (`FUN_0040b690`, `FUN_0040b920`, `FUN_0040bc50`)

A list node is 0x210 bytes from the debug allocator: +0 the path (0x104),
+0x104 the module name, +0x204 the base address, +0x208 the size and +0x20c
the next node. Both builders keep the list sorted by base address (each new
node walks down past every node of a lower base). Ghidra types both builders
`void`; the list head comes back in EAX (`FUN_0040bc50` and `FUN_0040c0f0`
use it).

- **`FUN_0040b690`** (process id on the stack): tries `kernel32.dll`, then
  `tlhelp32.dll`, for `CreateToolhelp32Snapshot`, `Module32First` and
  `Module32Next`. Neither has all three: `"Unable to load toolhelp library"`.
  A snapshot of kind 8 (modules, by the documented value of
  `TH32CS_SNAPMODULE`) that fails: `"Unable to get toolhelp
  snapshot"`. Else `"Enumerate modules..."` and a node for each module
  entry (its path, name, base and size).
- **`FUN_0040b920`** (process handle on the stack): `psapi.dll` with
  `EnumProcessModules`, `GetModuleFileNameExA`, `GetModuleBaseNameA` and
  `GetModuleInformation`. A missing one: `"Unable to load psapi library"`.
  More than 0x100 modules: `"PSAPI EnumProcessModules returned invalid number
  of modules"`. Each call that fails logs its own `ERROR: PSAPI ...` line
  with `GetLastError`. On any of these failures it frees the library and
  the list built so far (`FUN_0040b660`), and returns no list.
- **`FUN_0040bc50`** (EAX = process id, stack = process handle): the
  toolhelp list, or the psapi list when that one is empty. It logs each
  node as `"    Base:%08lXh  Size:%7lXh  Name:%-15.15s  Path:%s"` and returns
  the list.
- **`FUN_0040b660`** (ECX = list) frees every node (debug free, line
  0x1f7).
- **`FUN_0040bcb0`** (ECX = address, EDX = list) returns the node whose
  `[base, base + size)` holds the address, or 0.

### Symbols (`FUN_0040c0f0`, `FUN_0040c290`, `FUN_0040c620`, `FUN_0040b600`)

- **`FUN_0040c0f0`** (stack = process id, EBX = process handle): builds the
  list (`FUN_0040bc50`) and logs `"Load symbols..."`. It calls SymLoadModule
  for each node (with the node's path and name copied to stack buffers).
  When `DAT_0075cfb8` is 0 it logs `"Symbols Loaded for '%s'"` or `"Error
  (%d) loading symbols for '%s'"`. Returns the list.
- **`FUN_0040c290`** (EBX = the output buffer) builds a `;`-separated
  symbol search path. The order: the exe's folder (`FUN_0040c220`: the
  module path cut at its last `\`, `/` or `:`), the current directory
  (left out when it equals the exe's folder, `_stricmp`), then
  `_NT_SYMBOL_PATH`, `_NT_ALTERNATE_SYMBOL_PATH` and `SYSTEMROOT` when set.
  Each piece is appended with no length check; the buffer is 0x1000 bytes
  at both callers, and each piece at most 0x200.
  - The last `;` is dropped and the log says `"User defined symbols path:
    %s"`. An empty path logs `"Unable to generate symbols search path"`.
  - It returns the buffer, or 0 for an empty path (`neg`/`sbb`/`and` at
    0x40c508), so SymInitialize then uses its default path.
- **`FUN_0040c620`** is a counted SymInitialize under the critical section.
  With the flag `DAT_0075d014` set it returns 1, else 0. Only the first
  holder (`DAT_0075cfd4` from 0) does the work:
  - it builds the path into `0x75bfb8` (0x1000 bytes, kept) once;
  - `SymInitialize(GetCurrentProcess(), 0x75bfb8, 0)`; a failure logs
    `"s_DoSymInitialize(): err = %lu"`, runs SymCleanup, resets the count
    and returns 0;
  - `SymSetOptions(options | 0x10)`, then the module list with symbols
    for the current process id, freed straight away (line 0x1f7).

  `FUN_0040c720` is its release (cited, its caller is the quick walk below).
- **`FUN_0040b600`** (process, address on the stack; EBX = an
  `IMAGEHLP_LINE`, EDI = the displacement out): SymGetLineFromAddr at the
  address, then at each of the 99 bytes below it until one works. The
  displacement is set to how far back it went. Returns 1 when a line was
  found, 0 when SymGetLineFromAddr is missing or none of the 100 works.
- **`FUN_0040c520`** (`__thiscall`: ECX = an address; EAX = the buffer size;
  stack = the name buffer, a section-number out and an offset out): the
  file name of the module that holds the address. It uses `VirtualQuery`'s
  allocation base, or the exe's handle when that is 0, then
  `GetModuleFileNameA`. `"unknown"` when either fails. When the base is a
  PE image (`MZ`, then `PE` at `e_lfanew`), it finds the section whose
  `[VirtualAddress, VirtualAddress + max(VirtualSize, SizeOfRawData)]`
  holds the address. The out values are its 1-based number and the offset
  in it; both are 0 when no section matches.

### The annotated stack dump (`FUN_0040bcf0`)

ECX = the process handle, EDX = the context, stack = the module list.

- An empty line, then `"Annotated stack dump"`. The line `"// EIP = 0x%08X -
  %s - %s+%04X"` gives Eip, the module holding it (`FUN_0040bcb0`, or
  `"unknown"`) and its symbol.
- From Esp it dumps up to 0x1000 bytes and at most 400 lines. Each line is
  `"              %08X:"` (the address), up to 16 bytes as dwords
  (`" %02X%02X%02X%02X"`, in memory order), padding, and the bytes as
  text: a byte whose low 7 bits are below 0x20 shows as `.`.
- An unreadable line (`IsBadReadPtr`) ends the dump.
- A dword that falls inside a listed module ends the line right after it.
  A comment follows, `"// 0x%08X - %s - %s+%04X"`: the value, the module
  and its symbol. That counts as three more lines. When that dword is in
  no module on the second look, it is internal error line 0x3a5 (fatal).
- The comment's other form: a symbol named `` `string' `` with
  displacement 0 prints `%s='%s'`, with the bytes at the value as escaped
  text, up to 0x50 bytes (`FUN_004113e0`).
- **`FUN_004113e0`** (ECX = output, EDX = text, stack = size, a quote byte):
  copies the text with each byte below 0x20 written as `\r`, `\n`, `\t` or
  `\xHH`. A null text gives `"<NULL>"`. A non-zero quote byte goes before
  and after it. A null output, or a result that does not fit, is a fatal
  internal error.

## No caller

These four have no call, jump or pointer to them in game.exe (a byte scan
for the address as a dword and as an `E8`/`E9` target). stubs.md lists them.

- **`FUN_0040d7d0`** (ECX = a log id): fills a `CONTEXT` (flags 0x10007)
  with its own return point (`call $+5` / `pop`), Esp and Ebp. It then
  calls **`FUN_0040c7a0`** with ECX = that log id, EDX = the current
  thread and the context. That is a "quick" walk: it runs once at a time
  (`DAT_0075d018`) and takes the counted SymInitialize (`FUN_0040c620`).
  - It walks up to 0x10 frames, skips the first, and stops after 3 symbol
    misses. Each frame logs `"    %08X  %s!%s+%04X"`, adding `"%*s%s,%d"`
    (the file and line) when `FUN_0040b600` finds one.
  - Then `FUN_0040c720` and `"QuickStackWalk: %d entries,
    %I64d+%I64d+%I64d+%I64d cycles, %I64d avg"`. Its four cycle counters
    only ever add the same local (`local_3a0`, set to 0 and never written
    again), so they stay 0.
- **`FUN_0040b540`** (ECX = a thread): `GetThreadContext` (flags 0x10007),
  then `"Stack bytes:"` with 0x1000 bytes at Esp and `"Code bytes:"` with
  0x20 bytes at Eip, both through `FUN_00410040` with the log `"ERR"`
  (`0x6cd614`).
- **`FUN_0040b3f0`** (ECX, EDX; stack: file, line and a third dword it
  never reads, `ret 0xc`): the growing allocator `0x40b4b0` (datatbls.md)
  without a pool. It calls `FUN_0040a1f0` with ECX 0, EDX = its ECX and its
  EDX, file and line on the stack, where `0x40b4b0` passes its pool (when
  `DAT_00708000` is set), its EDX and its three stack dwords. On a 0 result
  both log `FUN_00408900(2, file, line, "")` and return 0.
- **`FUN_0040d1c0`** (ECX, EDX, stack: a dword, a count n, an array): adds
  a record to `<GameLogs>\Crashdump` (`FUN_0040f000` + `"Crashdump"`,
  `fopen` `"ab"`). The record is ECX, EDX, the dword, n, then n dwords from
  the array. The dwords are copied through a 0x20-byte stack buffer
  (`FUN_0041d2a0`) with no bound, so more than 8 run past it.
  - The reader of that file is `FUN_0040d290`, the source of the
    Battle.net crash report (bnclient.md, message 0x5D). It reads these
    same records, folds equal ones together and deletes the file.
  - With the writer an orphan, game.exe 1.14d never makes the file: the
    report has data only when a `Crashdump` file is already in the GameLogs
    folder.

## Re-check

```sh
python3 tools/ghidra/fn.py 0040d5b0 0040d540 0040cc10 0040bcf0 0040c290 0040d1c0 0040d290
uv run --project tools/emu python tools/emu/disasm.py 40d540 40   # ECX thread, EDX = [arg2 + 4]
uv run --project tools/emu python tools/emu/disasm.py 40c508 8    # the path or 0
```
