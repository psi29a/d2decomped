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
  slot EAX points at, then return 1. The handle accessors pass a stack
  slot and read the handle back from it: their own handle argument's
  (`FUN_00415f10`, `FUN_00416160`, `FUN_004161c0`, `FUN_004181f0`,
  `FUN_004182d0`), or the next argument's once its out pointer is in a
  register (`FUN_00415fa0`, `FUN_00416030`, `FUN_004160f0`, `FUN_004162a0`,
  `FUN_00416320`, `FUN_00416400`). The "is this handle valid" branch that
  follows is not reachable, since the helper returns 1 each time. In the
  second kind Ghidra's decompile shows the handle as `param_2`.
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

## SEVT.CPP: the handler register `FUN_00426420` (`LAB_0042644a` is inside it)

Traced 2026-11-25 from the disassembly and run by
`tools/emu/storm_event_register.py` (`gaps-runs: FUN_00426420 FUN_0042644a`):

```sh
tools/emu/.venv/bin/python tools/emu/storm_event_register.py --mutation-check
python3 tools/ghidra/fn.py 00426420 0042644a 00426270
```

**`0x42644a` is not a function entry.** `FUN_00426420` is one function, 0x426420
to 0x42670e (`RET 0x14`). Its first 42 bytes check the arguments; the
`jne` at 0x426439 jumps to 0x42644a, which pushes `ebx`/`esi`/`edi` and
carries on in the same stack frame (the decompile of "FUN_0042644a" reads the
caller's arguments through `unaff_EBP`). It is the `LAB_0042644a` of
`FUN_00426420`, listed as a function in `game-functions.tsv` only because Ghidra
split the body; `game-srcfiles.tsv` gives it `SEVT.CPP`. Its one "caller" in
`game-calls.tsv` is that jump.

Signature: `(tag, window, code, flags, handler)`, `RET 0x14`; callers:
`FUN_00420a70` (and through it `FUN_00420bf0`, `FUN_00420c10`, `FUN_00420c30`,
`FUN_00420c50`). Callees: `FUN_0041c340` (stores the error in `DAT_007790f4`
and calls `SetLastError`), `FUN_00426270` (node lookup, tag in ESI, window in
EDX), `FUN_00413020` (SMemAlloc) and `FUN_00412650` (SMemFree), all with the
file `.\SOURCE\SEVT.CPP`; the critical section `0x77930c`.

1. **Guard.** It goes on only when `handler != 0` and `flags == 0`. Otherwise
   `FUN_0041c340(0x57)` and 0, before the section is entered.
2. **Node.** `FUN_00426270` looks (tag, window) up in the node hash: buckets
   `DAT_00779300`, size `DAT_00779304`, count `DAT_00779308`, bucket
   `(tag ^ window) & (size - 1)`, chain through node `+0x10`. A node is 0x14
   bytes: `+0` tag, `+4` window, `+8` the handler counter, `+0xc` its handler
   table, `+0x10` next. Missing: if `size >> 1 <= count` (always true at 0)
   the hash is rebuilt first: the new size is the smallest power of two above
   `2 * count + 2` (4 when empty; 0x143), the nodes are moved bucket by
   bucket, each to the *head* of its new chain (so chains reverse), and the
   old array is freed (0x154). Then the node (0x15d) and its 0x10-byte handler
   table (0x160) are allocated, the count rises, the node goes at the head of
   its chain.
3. **Handler table.** `+0` buckets, `+4` size, `+8` count. If `size >> 1 <=
   count` (true when empty) it is rebuilt: size the smallest power of two above
   `2 * size + 2` (4 when empty); a bucket array (0x16e) and an array of tail
   pointers (0x16f); the old records are walked bucket by bucket, chain by
   chain, and each is appended at the *tail* of bucket `code & (newsize - 1)`
   (record `+0xc` zeroed first), so their order is kept; the tail array is
   freed (0x182) and the old bucket array when there is one (0x184).
4. **The record** (0x10 bytes, 0x18d): `+0` code, `+4` the node's counter
   after incrementing it (a sequence number), `+8` handler, `+0xc` next. It
   goes at the *head* of bucket `(size - 1) & code`, the table's count rises,
   `DAT_007792f8` is set to 1 (`FUN_00426f90` re-reads the table while it is set, and clears it after a
   lookup when `DAT_007792fc` is 1), the section is left, return 1.

The dispatcher takes the newest record of a code first, which is what the
WM_CHAR oracle above shows. Oracle: 60 runs of up to 600 random calls (some
with a non-zero `flags` or a null handler, tags and windows from a handful of
pairs up to 40, codes from a few common to random 32-bit values) compare every
return value, the SMemAlloc / SMemFree log (size, line, flags, which block a
free takes back), `DAT_007790f4`, `DAT_007792f8` and both tables chain by
chain with a model; five planted defects (ignored flags, a handler table grown
one record late, a node rehash that keeps order, a zero sequence number,
a record added at the tail) are all caught.

d2d: no counterpart. It has no Storm event table; `rules::enter_takers` models
only the dispatch result, already checked by `test_storm_char_dispatch`.
Not traced: the size-overflow cases (a count of 0x7fffffff or more, which
cannot be reached), allocation failure (SMemAlloc is hooked and never returns
null; the function does not test for it), and a second thread in the section.

## More SFILE.CPP code, read from the decompile and the assembly

Traced 2026-11-20 by reading, not run: the rest of the functions between
the `SFILE.CPP` source-file entries in `game-srcfiles.tsv`. Callers are from
`game-calls.tsv`. Register arguments and `ret n` are from the assembly.

**Its lists.** Five static destructors (`0x6cb0c0`, `0x6cb120`, `0x6cb180`,
`0x6cb1e0`, `0x6cb240`) each empty one list with ECX = the list. That names
the lists by the records their code links:

| List | Head link | Records | First item read at |
|---|---|---|---|
| `0x708354` | `0x708358` | archives, `.?AUARCHIVEREC@SFile@Storm@@` (0x25c bytes plus extra) | `0x70835c` |
| `0x708360` | `0x708364` | requests, `.?AUREQUEST@@` (0x44 plus extra) | `0x708368` |
| `0x70836c` | `0x708370` | files (above) | `0x708370` |
| `0x708378` | `0x70837c` | events, `.?AUEVENTREC@@` (0xc plus extra) | `0x708380` |
| `0x708384` | `0x708388` | audio streams, `.?AUAUDIOSTREAM@@` (0x50 plus extra) | `0x70838c` |

### Reached code

| Function | What it does |
|---|---|
| `FUN_00415240` | (dst, src, src size; `ret 0xc`) Under critical section `0x779080`, allocates a 0x3134-byte work buffer once (line 0x2c9, kept at `0x77903c`). Then calls `FUN_006b01b0(FUN_004151d0, FUN_00415210, work, &{src, 0, dst, 0, size})` and returns 1. Called by `FUN_00416ad0` for a sector whose entry flags & 0xff00 are 0x100 |
| `FUN_004152d0` | EAX = byte count, ECX = key, EDX = buffer. Decrypts count / 4 dwords in place with the table at `[0x779048] + 0x1000`. The seed starts at 0xeeeeeeee. Each dword: seed += table[key & 0xff]; the dword ^= seed + key; key = (key >> 11) \| ((~key << 21) + 0x11111111); seed = seed × 33 + 3 + the decrypted dword. Callers `FUN_00416ad0`, `FUN_0041b6b0` |
| `FUN_004155b0` | ECX = archive, EAX = hash slot: returns archive +0x13c + 0x2c × the u32 at +0xc of the slot's 16-byte entry (table at archive +0x140), the slot's block entry. Callers `FUN_004172f0`, `FUN_00417f10` |
| `FUN_004155d0` | EAX = name, ECX = archive, stack u16 a0, u8 a1 (`ret 8`). Hashes the name with `FUN_00415330` types 0, 1 and 2. Starts at type 0 & (the count at `[archive+0x138]+0x18` − 1) and walks the 16-byte entries at +0x140, wrapping, until one has +0xc == −1 or the walk is back at the start. Entries with +0xc == −2 are skipped. An entry with both name hashes (+0, +4), u16 +8 == a0 and u8 +0xa == a1 returns at once. Otherwise the last entry whose +8 is 0 or a0 and whose +0xa is 0 or a1 is returned, else −1. Callers `FUN_004172f0` (name `(signature)`, `0x6cf18c`, a0 = a1 = 0), `FUN_00417f10` |
| `FUN_004156a0` | ECX = array, stack count (`ret 4`): widens count 16-byte entries to 0x2c bytes in place, last first. Each entry's 16 bytes move to the start of its 0x2c slot and the other 0x1c bytes are zeroed. Caller `FUN_0041b6b0` |
| `FUN_004157c0` | ESI = request (from `FUN_0041a550`). Under `0x779068`, locks (slot +0x2c) the buffer object at +0x28 at offset +0x2c for `[0x779038]` bytes. Copies up to request +0x3c bytes from `[0x779050]` and fills the rest with the stream's byte +0x48 (`FUN_0041d2c0`), then unlocks (+0x4c). It sets the stream's (+0x30) +0x30 from the bytes written, and +0x1c by how far that passes +0x24. A stream in state 1 (+0x18) with enough buffered (the least of 0x10000, +0x24 and +0x40) is played (+0x30, flags 1) and goes to state 2. The slots are IDirectSoundBuffer's Lock / Unlock / Play in the DirectSound header's order |
| `FUN_004158e0` | `GetModuleFileNameA(GetModuleHandleA(0))` into `0x778f28` (0x104 bytes), cuts it at the last `\` (`FUN_004134d0`), then `FUN_00413800(0x778f28, "\", 0x104)`: the exe's folder with a trailing backslash. Callers `FUN_00415a80`, `FUN_004160a0` |
| `FUN_00415a80` | ECX = name, EDI = out, stack strip (`ret 4`). Fills `0x778f28` first if empty. With strip set it keeps the name after its last `\`. Then `wsprintfA("%s%s", folder, name)` and `_fullpath(out, that, 0x104)`. Callers `FUN_00415b00`, `FUN_0041ba60` |
| `FUN_00415b00` | EAX = out, ESI = name, stack flags (`ret 4`). Flags & 3 == 0: returns 0. Flag 2: the exe folder plus the name's last part; returns 1 if that is an existing file (`FUN_00415940`: `GetFileAttributesA`, not a directory). Flag 1: a name starting with `\`, or containing `:\` or `\\` (`strstr`, `FUN_00686900`), is copied as is (0x104), otherwise the exe folder is prefixed; returns whether that is an existing file. Callers `FUN_00417f10`, `FUN_0041bca0` |
| `FUN_00415bb0` | (archive, out, a2, a3, key, key size; `ret 0x18`). With a2 and key both 0, the key is the 4 bytes at `0x6cf078` (`01 00 01 00`). Zeroes `*out`. Builds an `SSignature.cpp` context (`FUN_0041d3b0`). Under `0x779068`, it saves the archive file's position (+0x10c), seeks to the header offset +0x130 and feeds the archive size (`[+0x138]+8`) plus the context's tail length (`FUN_0041d410`) in 0x10000-byte reads (buffer line 0xb33) to `FUN_0041d420`, then restores the position. `FUN_0041d6d0` checks it with the key (it needs the context full and its buffer starting with the dword 0x5349474e). `*out` = 5 when that passes, else 1; returns out == 5. Caller `FUN_004172f0` |
| `FUN_00415ee0` | (value; `ret 4`): 0 gives error 0x57 and 0; else stores it at `0x778f20` and returns 1 (`FUN_00419f30` clears it). Caller `FUN_00514780` |
| `FUN_00416710` | (buffer; `ret 4`): frees it (`FUN_00412650`, line 0x1407) and returns 1; null gives error 0x57 and 0. Callers `FUN_00523480`, `FUN_005234d0`, `FUN_005239c0` |
| `FUN_00416ad0` | (file, position, dst, bytes; `ret 0x10`), the sector reader. A position that is not sector-aligned (archive +0x124) gives error 0x57 and 0; 0 bytes return 0. The first read of a file whose entry flags & 0xff00 are set loads its sector offset table (count + 1 dwords, at +0x12c) through `FUN_004154b0`, decrypting it (`FUN_004152d0`) when flags & 0x10000. A table whose offsets go down returns 0. Then +0x130 = 1. It reads the range (a compressed file reads its sectors' span into a buffer, line 0x382) and works out how many bytes arrived. Encrypted files (0x10000) decrypt sector by sector. Compressed files then unpack each sector: one stored smaller than its full size goes to `FUN_00415240` for 0x100 or `FUN_0041e720` for 0x200 (a failure reports 0x85100083 through `FUN_0041caf0`); one already full size is copied. Returns the bytes read. Callers `FUN_00416fb0`, `FUN_00417040` |
| `FUN_00416fb0` | ESI = file, EDX = position: a one-sector cache in the archive. When archive +0x11c is this file and +0x120 this position, returns +0x12c. Otherwise it reads a sector (cut to the file's end for an uncompressed file) into archive +0x128 with `FUN_00416ad0`, records file, position and result, and returns the result. Caller `FUN_00417040` |
| `FUN_004172f0` | (archive, out; `ret 8`). Returns 1 when `FUN_00415bb0` passes. Otherwise it looks up `(signature)` (`FUN_004155d0`); none sets `*out` = 1, error 0x4dc and returns 0. It loads `advapi32.dll` and nine Crypt* entry points (table line 0xa74) and opens the `Blizzard_Storm` context of the Microsoft Base Cryptographic Provider v1.0 (type 1; flags 0, then 8). It maps the archive file and requires the `(signature)` entry's flags & 0x1ff00 to be 0, its size above 8 and offset nonzero (else `*out` = 2). The entry's first dword must be 0 (else 3). The key is resource `BLIZZARDKEY` of type `#256`. It hashes (algorithm id 0x8003) the archive from its header offset to the entry, then a fresh buffer of the entry's size (allocated with flag 8, line 0xaca), then the rest of the archive, and verifies the signature at entry +8 (size − 8): `*out` = 5 and returns 1 on success, else `*out` = 2. Error 0x4dc on failure. Callers `FUN_0051e6d0`, `FUN_0051ed00`, `FUN_0051fab0` |
| `FUN_00417a70`, `FUN_00417c70`, `FUN_00417d80`, `FUN_004184b0` | the list insert, one per list (ECX = list, stack link, mode, where; `ret 0xc`). It unlinks the link if it is linked; mode 1 links it after `where` (the head when null), any other mode before. Callers: the request, event, audio stream and archive constructors `FUN_00419630`, `FUN_00418f90`, `FUN_00419700`, `FUN_00418ec0` |
| `FUN_00418390` | (a0, name, flags; `ret 0xc`): null or empty name gives error 0x57 and 0; under `0x779068`, `FUN_00415b90` then returns `FUN_00417f10`'s answer for (name, flags). Its only caller, `FUN_00418b80`, is an orphan (below) |
| `FUN_00418900`, `FUN_00418940`, `FUN_00418980`, `FUN_004189c0`, `FUN_00418a00` | ECX = list: unlinks the first item until the head's +8 is not positive. Called by the static destructors for `0x708354`, `0x708360`, `0x70836c`, `0x708378`, `0x708384` in that order, and by the orphans `FUN_00418c60` .. `FUN_00418da0` |
| `FUN_00418be0`, `FUN_00418c20` | ECX = a link: unlinks it. Reached only by a `jmp` from the unwind handlers `0x6c9e90` / `0x6c9ec0` (ECX = `[ebp-0x10]`) |
| `FUN_00419690` | ECX = event list, stack record (`ret 4`): unlinks and frees it (`.?AUEVENTREC@@`), returns the next record or 0. Caller `FUN_0041a550` |
| `FUN_00419700` | (list, extra, flags; `ret 0xc`): allocates 0x50 + extra bytes tagged `.?AUAUDIOSTREAM@@` (flags \| 8), zeroes the link, and links it into the list when one is given (`FUN_00417d80`). Caller `FUN_0041b280` |
| `FUN_004199a0` | ECX = stream, the destructor. Drops the reference of the file at +8 (+0x144; at 0 `FUN_00419070` and free). Releases (slot +8) the buffer object at +0x3c when +0x44 is set. Unlinks the stream. Callers `FUN_00419a50`, `FUN_00419b90`, `FUN_00419f30`, `FUN_0041a020`, `FUN_0041a990` |
| `FUN_00419c00` | Audio stream pump (nothing while `0x779044` is set), per stream from `0x70838c`, by state +0x18. A stream in state 3, or in state 2 once its file position is past its end, is handled first. If +0x14 is set it rewinds: file position = +0x20, +0x30 = +0x1c = 0. Otherwise, when `FUN_00415760` answers (not −1): while +0x2c is below +0x24 it pads the buffer with the +0x48 byte and goes to state 3; else it stops it (slot +0x48) and goes to state 4. In state 2 it queues the next read (`FUN_00419790`) when the position `FUN_00415760` returns leaves a chunk of room. In state 0 it queues the first buffer's worth (+0x40 bytes) of reads and goes to state 1. `0x7790a4` set aligns reads to sectors. Caller `FUN_0041a550` |
| `FUN_00419f30` | Sets `0x779044`. While the worker `0x779034` exists and a request is running (`0x77902c`) or queued (`0x708368` > 0), it waits on it 10 ms at a time. Then it unlinks every stream, drops its count +0x4c (destroying and freeing at 0), clears `0x778f20` and `0x779044`, and returns 1. Caller `FUN_005147b0` |
| `FUN_0041a450` | (archive, position, &next, &timed, &delay; `ret 0x14`). Walks the requests from `0x708368`, skipping one whose +0x24 is nonzero and equals the following request's. When `0x708350` is 0 or the request's +0x30 is set, it becomes `next` (best 0). Otherwise it does when its distance is at most the best so far, and the distance becomes the best. The distance is +0x14 minus (the position minus the sector size of the request file's archive, when that is positive); a smaller +0x14 adds 0x8000000 instead, and a request in another archive is 0xfffffffe. `timed` is the request with the smallest +0x18 − now (at most 1000 when +0x1c is set), `delay` that value. Caller `FUN_0041a550` |
| `FUN_0041a550` | The request worker. Its address is pushed at `0x41a8a1` in `FUN_0041a880`. Each loop it sets every event (`0x708380`, `FUN_00419690` steps), pumps the streams and picks a request. With nothing due it waits on `0x779054` (250 ms, or until the bytes-per-second pace allows). Otherwise it unlinks the request into `0x77902c` and reads it: archive member `FUN_00417040`, loose file `FUN_00415560`, into `[0x779050]` for an audio request. A file being checksummed whose bytes summed reach its size finishes the checksum (`FUN_0041d810` mode 4, state 4). An audio request fills its stream (`FUN_004157c0`), or stops it (slot +0x48, state 4) when the read gave nothing. Then it calls the callback at +0x40 (a mismatched finished checksum first reports 0x85100083), frees the request when +0x38 is set, and sets its event (+8). It returns 0 when `0x778f24` is set, clearing it |

### No caller (orphans)

These 58 have no caller in `game-calls.tsv`, no raw pointer to them in the
image (0x401000..0x7d0000), and no E8 / E9 rel32 call to them in `.text`
(byte-scanned). They are Storm entry points and template members D2 does not
use (stubs.md).

| Function | What it does |
|---|---|
| `FUN_00415d50` | (path; `ret 4`): the checksum of a disk file. It opens the file (`CreateFileA`, read, share read) and feeds it in 0x1000-byte reads (line 0xb83) to `FUN_0041d810` (modes 1, 2, 4), returning the result or 0 on a short read. A null or empty path gives error 0x57 |
| `FUN_00415ec0` | (callback, context; `ret 8`): stores them at `0x779060` / `0x779064`, the open callback `FUN_004192f0` calls |
| `FUN_00415f10` | (archive, flag; `ret 8`): archive +0x258 += 1 when the flag is 0, else −1; returns 1 |
| `FUN_00415fa0` | (archive, out1, out2; `ret 0xc`): `*out1` = archive +0x118, `*out2` = (archive +0x110 == 3), each when non-null; returns 1 |
| `FUN_00416030` | (archive, buffer, size; `ret 0xc`): copies the archive's name (+8) with `FUN_004135d0`; a null buffer gives error 0x57 |
| `FUN_004160a0` | (buffer, size; `ret 8`): copies the exe folder (`0x778f28`, filled first if empty); returns 1 |
| `FUN_004160f0` | (file, out; `ret 8`): `*out` = file +0x114 (its archive); a null out gives error 0x57 |
| `FUN_00416160` | (file; `ret 4`): an archive member's block entry +0x18 (its expected checksum), else 0 |
| `FUN_004161c0` | (file, out; `ret 8`): an archive member's block entry +0x1c..+0x2b copied to out (16 bytes), returns 1; else 0 |
| `FUN_004162a0` | (file, buffer, size; `ret 0xc`): copies the disk path (+0x10c) when set, else the name (+8); a null buffer gives error 0x57 |
| `FUN_00416320` | (file, out; `ret 8`): zeroes `*out` and returns an archive member's block entry +4, else 0 |
| `FUN_00416400` | (file, FILETIME *; `ret 8`): a loose file answers `GetFileTime(handle, 0, 0, out)` (after leaving the lock); an archive member copies its block entry +0x10 / +0x14; a null out gives error 0x57 |
| `FUN_004166d0`, `FUN_00416700` | (value; `ret 4`): store a u16 at `0x77905c` / a byte at `0x77905e` |
| `FUN_00416770`, `FUN_004168c0`, `FUN_00416930`, `FUN_00416970`, `FUN_004169b0` | ECX = link, stack offset (`ret 4`): link +4 when negative gives its complement; else link +4 plus the offset (a negative offset is worked out from the link). Byte-identical |
| `FUN_00417a10`, `FUN_00417a50`, `FUN_00417b60`, `FUN_00417c50`, `FUN_00417d60` | ECX = list, stack link (`ret 4`): the link, or list +4 when null. Byte-identical |
| `FUN_00417b20`, `FUN_00417d20`, `FUN_00417e30` | the same choice, then its +4, or 0 when that is not positive. Byte-identical |
| `FUN_00417e90`, `FUN_00418560`, `FUN_004185f0`, `FUN_00418660`, `FUN_00418710` | the same choice, then unlinks it. Byte-identical |
| `FUN_004181f0` | (file, &a, &b; `ret 0xc`): finds the stream whose +8 is the file and fills +0x34 / +0x38 from slots +0x18 / +0x1c of its buffer object when they hold 0x7fffffff, then copies them out. No stream gives error 0x85100072. The slots are IDirectSoundBuffer's GetVolume / GetPan in the header's order |
| `FUN_004182d0` | (file, a, b; `ret 0xc`): the same lookup; a value other than 0x7fffffff that differs from +0x34 / +0x38 is stored and passed to slot +0x3c / +0x40 (SetVolume / SetPan in that order) |
| `FUN_004185d0`, `FUN_004186d0`, `FUN_004186f0` | ECX = list, stack item (`ret 4`): the item's +4, or list +8 when null. Byte-identical |
| `FUN_00418b20`, `FUN_00419150`, `FUN_00419a50`, `FUN_0041a0e0` | ECX = record, stack flags (`ret 4`): run the archive close `FUN_00418a50`, file teardown `FUN_00419070`, stream destructor `FUN_004199a0` or request destructor `FUN_0041a020`, then free the record (tag `delete`) when flags bit 0 is set |
| `FUN_00418b80` | (name; `ret 4`): `FUN_00418390(0, name, FUN_00418b50())` |
| `FUN_00418c60`, `FUN_00418cc0`, `FUN_00418d00`, `FUN_00418d40`, `FUN_00418da0` | ECX = list: empty it (`FUN_00418900`, `FUN_00418940`, `FUN_00418980`, `FUN_004189c0`, `FUN_00418a00`), then unlink its head link. The three middle ones are byte-identical |
| `FUN_00418e90` | ECX = archive: lowers +0x148; at 0 closes it (`FUN_00418a50`) and frees it |
| `FUN_00418ec0`, `FUN_00418f90`, `FUN_00419630` | (list, extra, flags; `ret 0xc`): the archive (0x25c), event (0xc) and request (0x44) constructors, as `FUN_00419700` |
| `FUN_00419000` | ECX = link, stack flags (`ret 4`): unlinks it and frees it when flags bit 0 is set |
| `FUN_00419180` | (archive, name, priority, a3, out; `ret 0x14`). Makes a second archive record (0x25c bytes) on an open archive. It copies the archive's name (+8) and file handle (+0x10c), +0x150 = the archive, +0x118 = priority, +0x258 = 0, the name at +0x154, +0x148 = 1. It raises the archive's +0x148 and links the record before the first archive whose +0x118 is not above its own. A null out, null or empty name or null archive gives error 0x57 |
| `FUN_00419b90`, `FUN_0041a2c0` | ECX = list, stack record (`ret 4`): destroy (`FUN_004199a0` / `FUN_0041a020`) and free a stream / request; returns the next item or 0 |
| `FUN_0041a960` | (handle; `ret 4`): `FUN_0041a300(handle, 0)`; a null handle gives error 0x57 |
| `FUN_0041b080` | (6 words; `ret 0x18`): `FUN_0041aad0(a0, a1, a2, a3, a4, 0, a5)` |
| `FUN_0041ba30` | (3 words; `ret 0xc`): `FUN_0041b280(a0, a1, a2, 0, 0x7fffffff, 0x7fffffff, 0)` |

## Remaining gaps

The archive search, sector decompression, request-queue worker, checksum
arithmetic, the I/O-error retry modes 0 and 1 of `FUN_004154b0`, the
allocation-failure paths of `FUN_004208e0` and the interlocked races of
`FUN_0041c1a0` are not traced or not run.
