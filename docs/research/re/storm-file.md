# Storm file handles, message text and window records (SOURCE)

Trace date: 2026-11-16. Fifteen functions that the gap map listed as `cited`
(an oracle only hooked their addresses) now run from stock game.exe 1.14d
(SHA-256 `631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`)
in `tools/emu/source_storm.py`:

```sh
tools/emu/.venv/bin/python tools/emu/source_storm.py --exe /path/to/Game.exe --mutation-check
python3 tools/ghidra/fn.py 00416230 00416370 004165a0 004192f0 004198d0 00419980 0041aad0
python3 tools/ghidra/fn.py 0041c170 0041c1a0 0041c1e0 0041c360 0041c430 004208e0 00420970 00420a70
```

461 calls, 10 negative controls. Source file names in the binary:
`.\SOURCE\SFILE.CPP` (lines 0x14b..0x14e, 0x1231, 0x123c), `SSTR.CPP` (0x1f2),
`SEVT.CPP`. d2d has no port of any of them: it reads archives through
StormLib (`components/mpq`), so there is nothing to diff and no d2d bug to fix.

## What the oracle runs, and what it scripts

Stock code: the 15 entries, plus the handle-slot helper `FUN_00415950`, name
copy `FUN_004135d0`, CRT copy/fill, record constructor `FUN_00418f20` and
list linker `FUN_00417b80`, handle teardown `FUN_00419070`, loose-file read
`FUN_00415560` / `FUN_004154b0`, crypt-table build `FUN_00415390` and name
hash `FUN_00415330`, string duplicate `FUN_00414310`, message-facility
registration `FUN_0041c0f0` / `FUN_0041c140`, window lookup `FUN_004209e0`,
and the two static list constructors (`0x6ca810`, `0x6ca8e0`).

Scripted (hooked and logged): the archive search `FUN_00417f10`, the sector
reader `FUN_00417040`, the request queue `FUN_00419790` / `FUN_0041a880` /
`FUN_0041a020`, checksum step `FUN_0041d810`, archive close `FUN_00418a50`,
error reporter `FUN_0041caf0`, event table `FUN_00426420`, allocation and
the Win32 imports. Not traced: the archive search itself, MPQ sector
decompression, the request queue's worker, the checksum arithmetic.

## Conventions read from the assembly

- `FUN_00415950` and `FUN_00415930` are `__fastcall` and store ECX into the
  slot EAX points at, then return 1. The handle accessors pass their own
  handle argument slot, so the "is this handle valid" branch that follows is
  not reachable, since the helper returns 1 each time. Ghidra's decompile of the accessors is
  misleading for this reason (the handle appears as `param_2`).
- Critical section `0x779068` guards the file layer; `0x7790cc` guards the
  message-facility list.
- Intrusive lists (`TSList`): the list object holds the link offset at +0 and
  a head link at +4 (`prev`, `next`). A link is two dwords: previous link
  address and next item address; at the end of a list `next` is `~head`. The
  head link's `next` is the first item. The static constructors at `0x6ca810`
  (file list `0x70836c`, head `0x708370`) and `0x6ca8e0` (window list
  `0x708860`, head `0x708864`) set `next = ~head`; `tools/emu`'s CRT init skips
  them, so the oracle runs them. `FUN_00417b80` with mode 1 inserts after the
  head (newest first); `FUN_004208e0` inserts at the tail.

## File handle record (`.?AUFILEREC@@`, 0x158 bytes)

| Offset | Meaning (as the traced code uses it) |
|---|---|
| +0 / +4 | list link |
| +8 | name the caller passed (0x104 bytes) |
| +0x10c | loose files: duplicate of the disk path |
| +0x110 | OS file handle, or -1 for an archive member |
| +0x114 | archive record; +0x118 block entry (+8 file size, +0x18 expected checksum, +0xc flags, +0 offset) |
| +0x11c | file key; +0x120 read position |
| +0x124 / +0x130 of the archive | sector size, header offset used by the fixed-key adjustment |
| +0x128 | sector count; +0x12c sector offset table (only when entry flags & 0xff00) |
| +0x138 | 0x1000-byte cache buffer; +0x13c cursor, +0x140 valid length |
| +0x144 | reference count |
| +0x148 / +0x14c / +0x150 / +0x154 | running checksum, checksum enabled, state (1, 2, 4), bytes summed |
| archive +0x148 | archive reference count |

## Function findings

| Function | Traced behaviour |
|---|---|
| `FUN_00416230` | SFileGetFileName(handle, buffer, size). Null buffer: error 0x57, return 0, no lock. Otherwise sets `buffer[0] = 0`, copies the record name with `FUN_004135d0` (truncated to `size - 1` plus NUL; size 0 copies nothing), returns 1. |
| `FUN_00416370` | SFileGetFileSize(handle, high). Zeroes `*high` if non-null. Loose file: `GetFileSize(os handle, high)`. Archive member: block entry +8, high word stays 0. |
| `FUN_004165a0` | SFileSetFilePointer(handle, offset, high, method). A non-null `high` pointing at non-zero: error 0x57, return -1, no lock. Loose: `SetFilePointer(os, offset, NULL, method)`. Archive: method 0 sets, 1 adds, 2 is end-relative; a negative move past zero (method 1) or past the start from the end (method 2) clamps to 0; other methods leave the position. Clears +0x13c / +0x140. Returns the position. |
| `FUN_004192f0` | SFileOpenFileEx(archive, name, flags, out handle). Clears `*out`; null out pointer, null-name pointer test and empty name give error 0x57 and 0 (a null name pointer is read before the test and faults). Calls `FUN_00417f10(archive, name, flags, &archive, &path)`: 0 = not found, 2 = loose file, other = archive member. Loose: `CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, SEQUENTIAL_SCAN, 0)`; flag 4 returns the raw handle, otherwise builds a record with the path duplicate. Archive with flag 4: reopens the archive file and seeks to the block offset, storing the handle (-1 on failure, error 2). Archive otherwise: base name after the last `:` then last `\`, hashed with MPQ type 3; if entry flags & 0x20000 the key is `(key + offset - archive +0x130) ^ size`; sector count is size / sector size rounded up; sector table allocated (`SFILE.CPP` 0x1231) only when flags & 0xff00; cache buffer (0x123c); checksum flag copies global `0x7790b0` only when the entry has a checksum; archive reference count rises. Returns 0, 1 or 2 and calls the open callback (`0x779060`, context `0x779064`) with the name on success. |
| `FUN_004198d0` | SFileCloseFile. Unlinks the record, decrements +0x144, and at zero runs `FUN_00419070` (closes the OS handle, frees the sector table, buffer and drops the archive reference, closing the archive at zero) and frees the record with tag `delete`. A null handle operates on the default list head link `0x708370` and clears it (both dwords become 0): an original quirk, observed, confidence high. |
| `FUN_00419980` | SFileOpenFile(name, out): `FUN_004192f0(0, name, flags, out)` with flags from `FUN_00418b50` (`0x779058` bits 0 and 1, plus bit 0 when `0x70835c <= 0` and `0x779058 == 0`). |
| `FUN_0041aad0` | SFileReadFileEx2(handle, buffer, bytes, *read, overlapped, unused, mask). Null buffer: error 0x57. Zero bytes: signals the overlapped event. Loose file without overlapped: `ReadFile`; a failed `ReadFile` is reported through `FUN_0041caf0` and retried (stock policy `0x70834c` = 2). Archive: bytes left in the cache are copied first; a read shorter than the sector remainder fills the cache (up to min(remainder, 0x1000)); larger reads read directly. Synchronous archives call `FUN_00417040`; queued archives split the read into `0x708348`-byte (0x20000) requests through `FUN_00419790` and wait on an event for the last. Position advances only without overlapped. Short result: error 0x26, return 0. Checksummed files (+0x14c) finish the checksum when the whole file was read and report `0x85100083` on mismatch. |
| `FUN_0041c170` | ESI = index: `LoadStringA(exe, 0x5100 + index, 0x779100, 0x100)`; on failure returns the built-in string table entry `0x6cf2f4[index]` (entry 0 is `ERROR #%u (0x%08x)`). |
| `FUN_0041c1a0` / `FUN_0041c1e0` | Lock / unlock the message critical section. The lock increments `0x70841c` (initially -1): a zero result initialises the section, otherwise the increment is undone. |
| `FUN_0041c360` | Stores its argument at `0x7790f0`, the flag that makes the error reporter return at once. |
| `FUN_0041c430` | FormatMessage wrapper (code, buffer, size). Null buffer or zero size: error 0x57, return 0. Registers facilities 0x510, 0x876, 0x878 (executable module) once, finds the facility `(code >> 16) & 0xfff`, and calls `FormatMessageA` with the module table (0x800) when found, else system text (0x1000), language 0x400. Clears the first byte first. Returns whether text was produced. |
| `FUN_004208e0` | Allocates a 12-byte `.?AUWNDREC@@` record and links it at the tail of the window list; stores the window at +8. No guard for allocation failure (the failure path writes through a null pointer; not run). |
| `FUN_00420970` | ECX = window list, stack = record: unlinks it, frees it (tag `.?AUWNDREC@@`), returns the following record or 0. A null record faults (not run). |
| `FUN_00420a70` | ESI = handler, EDI = window, stack (tag, code): a null handler gives error 0x57 and 0; an unknown window gets a record via `FUN_004208e0`; then `FUN_00426420(tag, window, code, 0, handler)` and its result is returned. |

## Remaining gaps

The archive search, sector decompression, request-queue worker, checksum
arithmetic, the I/O-error retry modes 0 and 1 of `FUN_004154b0`, the
allocation-failure paths of `FUN_004208e0` and the interlocked races of
`FUN_0041c1a0` are not traced or not run.
