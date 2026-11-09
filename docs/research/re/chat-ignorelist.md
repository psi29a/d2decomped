# Chat/Ignorelist.cpp and the allocator it uses

Traced from game.exe 1.14d (cited; the sections "Account mapping" and "Node teardown" run in `tools/emu/ignorelist.py`, and the remaining five entries in `tools/emu/ignorelist_remaining.py`). d2d has no ignore list and
no Storm allocator port: the functions below are `not ported`.

## IgnoreList singleton (`.\Chat\Ignorelist.cpp`)

- `FUN_00611560` (130 bytes) is the lazy getter. If `DAT_0096bc20` is 0 it
  calls `FUN_00413020(0x188, ".\\Chat\\Ignorelist.cpp", 0x48, 0)`, runs the
  constructor `FUN_00611200` on the block, stores the result in
  `DAT_0096bc20` and returns it. Object size 0x188; allocation line 0x48.
  The assert string and `game-srcfiles.tsv` (`00611560 Chat/Ignorelist.cpp`)
  tie it to the file.
- Callers: `004423b0`, `00448370`, `004486f0`, `00448920`, `00448e30`,
  `0047bdf0` (and `0051b1d0`, not in the function table). `0047bdf0`
  (BattleNet chat receive) calls vtable slot +8 with the sender name and slot
  +0x14 with a second string; a non-zero result drops the message.
- `FUN_00611200` (288 bytes, constructor) sets the vtable to
  `IgnoreList::vftable`, clears dword 0x1f, sets dword 0x61 to 1, zeroes the
  0x104-byte buffer at dword 0x20, builds `<exe dir>\ignorelist` (strip after
  the last `\` found by `FUN_004134d0(path, 0x5c)`, append `"ignorelist"`
  with `FUN_00413800`, copy with `FUN_004135d0`), then reads registry value
  `"Diablo II"` / `"Persistent Ignore"` (default 1) via `FUN_00414f10` into
  dword 0x61 and calls `FUN_00611050`.

## Storm tracked allocator (`SMemAlloc`)

`FUN_00413020(size, file, line, flags)` is SMemAlloc (gateway-ini.md and
mpq-archives.md already name it from call sites; this is the body).

1. If `DAT_007788d0` is 0 run `FUN_00411cd0`; still 0 is fatal (`FUN_00411620`).
2. `FUN_00411670` hashes the file name and line (`FUN_00413ce0(name, 1, line)`,
   case-sensitive variant, table `DAT_006cee30`; result masked `& 0x7fffffff`,
   0 becomes 1; the last (name, line, hash) triple is cached in
   `DAT_007768a8..b4`, valid while `DAT_00708340` is set).
3. Bucket = hash & 0xff. `DAT_00776cc0 + bucket * 0x18` is that bucket's
   `CRITICAL_SECTION` (256 of them); it is entered.
4. The bucket list head is `DAT_007768b8[bucket]`; blocks link through dword 0
   and are matched on dword 1 == hash. No match: `FUN_004117f0(file, hash,
   bucket, 0x1000, 0x1000, 0x10000)` makes a new block.
5. `FUN_00412e40` allocates from the block, then the section is left. If
   `DAT_007788c8` is set and is not this block, `FUN_00412000` runs.

- `FUN_00411cd0` (97 bytes) one-time init: reads `"Internal"` /
  `"Debug Memory"` into `DAT_007784c4` and `"Protect Memory"` into
  `DAT_00776cbc` (both through `FUN_00414f10`, default 0), runs
  `InitializeCriticalSection` on the 256 sections, sets `DAT_007788d0 = 1`.
- `FUN_004117f0` (277 bytes) new block: `VirtualAlloc(NULL, 0x10000,
  MEM_RESERVE)` then commit 0x1000 `PAGE_READWRITE` at the same address
  (either failing calls `FUN_00411620`). Header fields (dword index):
  0 next (old bucket head), 1 hash, 2 bucket, 3 tag (`'mo'` low half, high
  half of the address), 4 = 1, 7 and 8 = first free byte
  (address + header 100 + file-name length, rounded up to 4), 9 = 0,
  0xa = 0x1000, 0xb = committed size, 0xc = reserved size, 0x17 = the file
  name pointer passed in, 0x18.. = copy of the file name; 0xe..0x16 zeroed.
  The block becomes the new bucket head.
- `FUN_00412000` (125 bytes) sweep: clears `DAT_007788c8`, and for each of
  the 256 buckets whose flag in `DAT_007784c8[]` is set, under that bucket's
  critical section clears the flag and releases (`FUN_00411a80`) each block
  with dword 5 == 0 and hash < 0x80000000.
- `FUN_00411620` (22 bytes) fatal path: `FUN_0041caf0(..., 0, 0, 1)` (the
  error report) then `ExitProcess(1)`. The decompile shows no return path.
- `FUN_00412e40` (236 bytes): hash >= 0x80000000 is fatal; otherwise gets
  memory from the block with `FUN_00412ba0` (flags 0x40 / 0x80 come from
  `flags & 0x4000000 / 0x8000000`); on failure leaves the section and is
  fatal. Flag bit 8 zero-fills the result (`FUN_0041d300`); else, if
  `DAT_007788cc` is set, it fills with 0xee (`FUN_0041d2c0`).
- `FUN_00405410` (23 bytes) is `SMemAlloc(size, DAT_006cc82c, -1, 0)`: the
  file argument is a fixed string at `006cc82c` and the line is -1. Callers
  `00517ad6` and `00517c30`.
- Helpers `FUN_00411670` (107 bytes) and `FUN_00411620` have no d2d mention.

Not traced: `0051b1d0` (size 0, absent from the function table, only a call
to `00611560`).

## FUN_00411670: file/line hash with a one-entry cache

Cited from the decompile; it takes the file name in ESI and the line in EBX
(registers, not stack), and returns the hash in EAX. Callers: `00412710`,
`00413020`.

- Name 0 hashes to 0 (the hash of the line is not taken). Otherwise
  `FUN_00413ce0(name, 1, line)` runs. Flag 1 skips the upper-casing and
  `/` to `\` folding that flag 0 does, so the file name is case-sensitive.
- `FUN_00413ce0` is the Storm string hash: seed = line (0 becomes
  `0x7fed7fed`), second accumulator starts at `0xeeeeeeee`, one step per byte
  with table `DAT_006cee30` (indexed by the byte's high and low nibble); the
  result is 1 if it would be 0.
- Result is masked `& 0x7fffffff`, 0 becomes 1.
- Cache: `DAT_00708340` (valid flag), `DAT_007768b4` (name pointer),
  `DAT_007768b0` (line), `DAT_007768ac` (first dword at the name pointer),
  `DAT_007768a8` (hash). A hit needs the same pointer, same line and same
  first dword; otherwise the flag is cleared and the hash recomputed.
  This function stores the four values but has no write to `DAT_00708340`
  itself (its other writers were not searched), so whether the cache hits is
  not established here.

## FUN_004117a0: Storm memory error report

Cited from the decompile (no oracle run); d2d: not ported. 42 bytes. The
message and code arrive in registers (the decompile shows ESI as an unnamed
input). It calls `FUN_0041c340` (stores the code in `DAT_007790f4` and
`SetLastError`), then, if `DAT_007784c0` is non-zero, `FUN_0041caf0(.., 0, 1,
1)` (the error report; `FUN_00411620` calls the same function with 0, 0, 1 and
then `ExitProcess`). It returns to its caller, so a bad free is reported, not
fatal. Callers: `00411910`, `00411f00`, `004122f0`, `004124b0`, `00412650`
(which passes `"SMemFree()"` and -1).

## FUN_00411910: is this a live Storm block?

Cited; d2d: not ported. 132 bytes, `__fastcall`; the pointer arrives in EAX,
the other two inputs in ECX and EDX (EDX non-zero means "report"). Returns 0
or 1. Fields are read below the user pointer:

| test | result |
|---|---|
| pointer is 0 | report, return 0 |
| `short [ptr-2]` != `0x6f6d` (`'mo'`, the tag in block header dword 3) | report, return 0 |
| `byte [ptr-5] & 2` | report, return 0 |
| `byte [ptr-5] & 1` and `short [ptr + ushort[ptr-8] - byte[ptr-6] - 10]` != `0x12b1` | report if asked, **return 1** |
| otherwise | return 1 |

So a bad tag and the `& 2` bit refuse the free; a bad trailing word at
`0x12b1` is only reported. What bit 2 and the trailing word mean is read from
the code alone (bit 2 looks like "already free", the word like an end marker
written when bit 1 is set); the allocator side that sets them was not traced
here. Callers: `004122f0`, `00412650`, `00412760`, `004127e0`, `00412a60`.

## FUN_00411c20: wipe, then release the chunk

Cited; d2d: not ported. 67 bytes. EAX is the chunk header (user pointer
minus 8: `ushort [0]` total size, `byte [2]` pad, `byte [3]` flags). If
`DAT_007788cc` is set (the same switch SMemAlloc uses to fill with 0xee) and
flag 4 of `byte [3]` is clear, `FUN_0041d2c0` fills `ushort[0] - 2*(flag 1) -
byte[2] - 8` bytes with `0xdd`. Then `FUN_00411aa0` unlinks the chunk.
Callers: `00412650`, `00412a60`, `00412f30`.

## FUN_00412650: SMemFree(ptr, file, line, flags)

Cited; d2d: not ported (mpq-archives.md and gateway-ini.md already name it
from call sites; `tools/emu/text_box.py:42` only stubs it). 179 bytes.

1. If `DAT_007788d0` is 0 run `FUN_00411cd0` (the one-time init above); still
   0: `FUN_004117a0("SMemFree()", -1)` and return 0.
2. `FUN_00411910` validates the pointer; 0 returns 0 and SMemFree returns 0
   without freeing. The report (last error set) happens only if the "report"
   input (EDX) is non-zero; the value SMemFree passes there is not visible in
   the decompile and is not traced.
3. Header = ptr - 8; if `byte [ptr-5] & 8`, the header is instead the pointer
   stored at `ptr - 0xc` (an aligned allocation).
4. Block base = `ushort [header+4] << 16` (blocks are 64 KB, see
   `FUN_004117f0`); bucket = `dword [base+8]`. Enter the `CRITICAL_SECTION` at
   `DAT_00776cc0 + bucket * 0x18`, call `FUN_00411c20`, leave it (skipped when
   bucket is -1). Return 1.

The file, line and flags arguments are not read in this body; they are
the same four-argument shape as SMemAlloc. 192 caller rows in
`game-calls.tsv`; the `delete`
operator passes `"delete"` and -1 (`004053f0`, `0040fe60`, `00426e20`).

## FUN_0060ed30: Ignorelist.cpp free-and-reset

Run in `tools/emu/ignorelist_remaining.py` on unlinked nodes; d2d: not ported
(d2d has no ignore list). 33 bytes, `__fastcall`, no
caller in the table (virtual). If `dword [this+0x18]` is set it frees it with
`SMemFree(ptr, ".\\Chat\\Ignorelist.cpp", 0x11, 0)`, then runs
`FUN_0060ea10` on the same object. The assert string and `game-srcfiles.tsv`
(`0060ed30 Chat/Ignorelist.cpp`) tie it to the file. `FUN_0060ea10` (134
bytes, source `stpl.h`) frees `dword [this+0x14]` with file
`d:\diablo2\Diablo2\3rdparty\storm\h\stpl.h` line `0x5e4`, then unlinks the
two list nodes at `this+4` and `this+0xc` (each: if linked, patch the
previous link and clear both words). So `0060ed30` is the destructor of an
Ignorelist string-list element built on Storm's template list (`stpl.h`).

## SMemFree callers that only delete

Cited; d2d: not ported. These five work-list callers are
delete wrappers or list-node cleanups; `FUN_004101c0` and `FUN_0041e0ed` were
not traced:

- `FUN_004053f0` (27 bytes): `if (p) SMemFree(p, "delete", -1, 0)`.
- `FUN_0040fe60`: runs `FUN_0040faa0` (destructor), then if bit 0 of the flag
  and the object are set, `SMemFree(this, "delete", -1, 0)`; returns this.
- `FUN_00426e20`: unlinks the node (same two-word list unlink as above), and
  with flag bit 0 deletes the object the same way.
- `FUN_00426f20`: unlinks the node, then
  `SMemFree(node, <string at 007088d8>, -2, 0)`; the file argument is a tag
  string, not a source path, and the line is -2 (-1 for `"delete"`).
  `FUN_0040fed0` does the same in a loop with the string at `00708124`
  until `dword [this+8]` is 0. Ghidra labels the two strings
  `s___AUBREAKCMD___` and `s___AUMessage_LogManager___`; their exact bytes
  were not read from `game-strings.tsv`.

## Account mapping (`FUN_00611320`)

Run in `tools/emu/ignorelist.py` (9910 cases with the table lookups and the
Storm allocator hooked). `thiscall`, ECX the list, `(raw name, length)`, `ret 8`,
returns 1 when it stored a mapping and 0 otherwise (the decompile shows void).
d2d: not ported.

- Nothing happens (0) for a null or empty name, or when the persistent
  setting is 0: vtable slot +0x20 of the list object (the constructor reads it
  from the registry value `"Persistent Ignore"`, above).
- It zeroes two 256-byte buffers (account, character) and scans the name for
  index below `length - 1` (unsigned: a length of 0 never stops it): a `(` at
  index + 1 sets `open` to index + 1 and copies the first `open` bytes to
  the account; a `)` at index + 1 copies the `index - open` bytes after the
  `(` (the text between) to the character. So a `(` at index 0 is never seen.
  It stops as soon as both are non-empty, or at the end of the string.
- With both non-empty it looks the account up in the hash table at
  `this + 0x54` (`FUN_0060f510`), creating it when missing (`FUN_00611170`),
  frees the entry's old value at +0x18 (`SMemFree`, line 0x142), and stores
  `SStrDup("*" + character)` there.
- No bound on the copies: a `)` before any `(` copies the string from index 1
  (`open` is 0), so a name over 256 characters would overflow the buffer.
  Battle.net names are far shorter.

## Node teardown (`FUN_0060edf0`, `FUN_00610690`, `FUN_0060ef60`)

Same oracle. All three free the node's value at +0x18 (line 0x11,
`.\Chat\Ignorelist.cpp`, only when set), run the stpl.h cleanup
`FUN_0060ea10` (frees +0x14, line 0x5e4, when set; unlinks the node's two
list links at +4 and +0xc when linked; the oracle covers unlinked nodes),
and free the node:

| Function | Last free | Returns |
|---|---|---|
| `FUN_0060edf0(this, flags)` (`ret 4`) | `"delete"` / -1, only when flags bit 0 | `this` |
| `FUN_0060ef60(list, node)` (`ret 4`) | the node tag / -2 | the node's next link (at node + the list's link offset + 4) when it is positive as a signed value, else 0 |
| `FUN_00610690(table, node)` (`ret 4`) | the node tag at `0x74362c` / -2 | nothing |

`FUN_0060ef30(list, offset)` (`ret 4`) does nothing when the offset is the
same; otherwise it unlinks every node (`FUN_0060ecb0`) and resets the
terminator (prevlink points at itself, next = the complement of its address).
`FUN_0060edb0` and its copy `FUN_0060eef0` unlink every node and then clear
the terminator's two words. The oracle runs these on empty lists only.

## Remaining table and singleton helpers

Run in `tools/emu/ignorelist_remaining.py` with allocation, construction and
virtual deletion hooked. The node cleanup and bucket unlinking run in game.exe.

- `FUN_0060ed30` frees the node's +0x18 value if present, then tail-calls
  `FUN_0060ea10` to free +0x14 and unlink its two list links. The oracle runs
  all four combinations of value and cleanup pointer with unlinked nodes.
- `FUN_0060ed70(table, node)` first reads the node's next link using the offset
  at table +4, or table +0xc for a null node; only a positive signed link is
  returned. It calls `FUN_0060e6e0(node)` and then virtual slot 0 with the node.
  The oracle hooks both calls and checks their order and the link boundary.
- `FUN_0060ee60(table, mode)` clears table +0x10, unlinks the list at +4,
  then walks table +0x18 buckets, each 12 bytes at the array at +0x1c. For
  mode 0 it invokes virtual slot 0 on each bucket node; otherwise it unlinks
  each node without that callback. The oracle checks empty and populated
  buckets in both modes, with one node per populated bucket and the virtual
  callback performing its own unlink. The list at table +4 is empty in these
  cases; its nonempty behavior is cited from the code.
- `FUN_006114e0` eagerly allocates 0x188 bytes (`SMemAlloc`, line 0x41),
  constructs the object and stores the constructor result at `0x96bc20`;
  allocation failure stores 0. It allocates even when a singleton already
  exists. `FUN_00611560` returns the existing pointer, or allocates and
  constructs the same way (line 0x48) if it is null. The oracle checks the
  allocation-failure and existing-singleton paths as well as success.

## The rest of the stpl.h range (`0x60dcb0`–`0x611170`)

Storm's container templates (`d:\diablo2\Diablo2\3rdparty\storm\h\stpl.h`)
instantiated for the ignore list, plus the ignore list's own small
methods that the address range puts in the same module. Several functions
are byte-identical copies (one per template instance); the table names
each once. Read from the code, not run.

**Containers.** A link is {prev link, next node}; a next value ≤ 0 is the
list's terminator (`~address`). A list is {link offset, terminator link};
link offset 0 means "the terminator". A hash table is a vtable, a list of
all nodes, a bucket array (12-byte lists at `+0x1c`, count `+0x18`), mask
`+0x24`. A string is one `SStrDup` pointer, freed at stpl.h line 0x5e4.

| Address | What |
|---|---|
| `FUN_0060dcb0`, `FUN_0060e130` | String assign: frees the old (line 0x5e9), `SStrDup`s the new |
| `FUN_0060dd00` | String from text (`SStrDup`) |
| `FUN_0060dd20` | String equal (`SStrCmp`, length 0x7fffffff) |
| `FUN_0060dd50` | Sets `ChatIgnoreList::vftable` |
| `FUN_0060dd60` | `ChatIgnoreList` deleting destructor |
| `FUN_0060dd90` | String free (line 0x5e4) |
| `FUN_0060ddb0` | Destroys the singleton `DAT_0096bc20` (virtual `+0x2c`, 1) and clears it |
| `FUN_0060ddf0` | Persistent ignore flag (`+0x184`) |
| `FUN_0060de00` | Sets it and writes registry `Diablo II` / `Persistent Ignore` |
| `FUN_0060de30` | The part of a name before `*` (empty when it starts with `*`; at most 17 characters) |
| `FUN_0060dea0` | The part from `*` on, `*` included (at most 17 characters). Its terminating 0 goes at the source index, not the destination's; `FUN_0060dff0` zeroes the buffer first, so it doesn't show |
| `FUN_0060df10` | Appends a name and a line end to the ignore file (`+0x80`, mode `at+`), only when persistent |
| `FUN_0060dff0` | The key for a name: its `*` part when it has one, else the part before |
| `FUN_0060e0e0`, `FUN_0060e110` | Bucket `n` (`+8 + n * 12`) |
| `FUN_0060e170`, `FUN_0060e1c0` | A link's previous link (`~prev` when that is the terminator) |
| `FUN_0060e1f0`, `FUN_0060e210` | Hash & mask (`+0x24`) |
| `FUN_0060e2a0`, `FUN_0060e300` | Grow step: the highest power of two ≤ n (1 for 0); from 21 up, 21 stored at `+0xc` |
| `FUN_0060e2e0`, `FUN_0060e340` | Rounds n up to a multiple of the step |
| `FUN_0060e370`, `FUN_0060e390`, `FUN_0060e470`, `FUN_0060e5b0` | List init: link offset, empty terminator |
| `FUN_0060e450`, `FUN_0060e630` | A node's link (offset 0: the terminator) |
| `FUN_0060e490`, `FUN_0060e890` | Links a node before or after another (1 = after), unlinking it first |
| `FUN_0060e540`, `FUN_0060e940` | A node's raw next value |
| `FUN_0060e560`, `FUN_0060e960` | Unlinks a node |
| `FUN_0060e650`, `FUN_0060e7c0`, `FUN_0060eaa0` | A node's next node, 0 at the terminator |
| `FUN_0060e7a0` | Unlinks a node (`FUN_0060e6e0`), then virtual slot 0 (delete) |
| `FUN_0060e820` | Unlinks every node of a list |
| `FUN_0060e9b0`, `FUN_0060e9e0` | List init as `FUN_0060e370`, clearing the terminator first |
| `FUN_0060eae0` | Copy of `FUN_0060ea10` |
| `FUN_0060f590` | Hash lookup by hash and string (`+0x14`) |
| `FUN_0060f610` | Bucket array destructor (`TSExplicitList<StringEntry>`) |
| `FUN_0060f690` | List destructor: unlink all, unlink the terminator |
| `FUN_0060f6d0` | Bucket array reserve, rounded up to the grow step |
| `FUN_0060f720` | Bucket array resize (`TSExplicitList<MappingEntry>`): Storm realloc, else alloc and relink |
| `FUN_0060f870` | Looks a name up in the account map and copies its value (`+0x18`) out |
| `FUN_0060f8b0` | Looks a name up in the ignore table (hash `FUN_00413ce0`) |
| `FUN_00610be0`, `FUN_00610df0` | Hash insert: grow check, node from virtual `+4`, linked last, hash and key set |
| `FUN_00610ca0` | Grow check: counts the chain (the count at `+0x10` starts 3 lower each time); past 13, while the mask is under 0x3ff, rehashes (`FUN_006108d0`) |
| `FUN_00610d30` | Table deleting destructor |
| `FUN_00610d60` | Inserts a name into the ignore table |
| `FUN_00610eb0` | Ignore by key (`FUN_0060dff0`): new keys are inserted and written to the file |
| `FUN_00610f90` | Ignore a name as given: inserted, and written to the file as `!name` (the loader `FUN_00611050` strips the `!`) |
