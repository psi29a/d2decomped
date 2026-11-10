# Bnclient support modules — game.exe 1.14d

The Battle.net client's helpers around the BNCS protocol in
[../../design/battlenet.md](../../design/battlenet.md): the local cache file,
the news list, BNFTP downloads and patching, the message queue, the
BnSend leftovers and the Warden client. d2d ports none of it (Battle.net is
not built); this is the trace. Source files come from the assert strings
(`game-srcfiles.tsv`). Re-check any row with
`tools/ghidra/fn.py <address>`; fastcall arguments were read from the asm
(`Disasm`).

The Bnclient API table at `0x72faa0` (returned by `FUN_00518c10`) is what
the launcher and Warden call; entries named below are its offsets.

## CACHE.CPP: bncache.dat

An MPQ-style archive at `%ALLUSERSPROFILE%` (CSIDL `0x23`, common app
data) `\Blizzard Entertainment\Diablo II\bncache.dat` (`FUN_00519220`).
One critical section (`0x881894`) guards it.

**Header** (0x2c bytes, `DAT_00881880`): `+0` magic `BN3\x1a`
(`0x1a334e42`; `BN2\x1a` is the old format and is rebuilt), `+4` the header
size 0x2c (written, never checked), `+8` file end,
`+0x10` hash table offset, `+0x14` block table offset, `+0x18` hash count,
`+0x1c` block count, `+0x20` CRC-32 of the header with this field zeroed,
`+0x24` / `+0x28` CRC-32 of the hash / block table. A new file gets 0x400
entries in each table (hash at 0x2c, block at 0x402c).

**Tables:** 16-byte entries, both encrypted as MPQ tables are. Hash entry:
name hash A, name hash B, locale (0), block index (`-1` empty, `-2`
deleted). Block entry: file offset, allocated size, data size, flags
(`0xc0000000` in use). Each file starts with a 0x144-byte entry header:
`+0` its size (0x144), `+0xc` CRC-32 of the header, `+0x10` CRC-32 of the
data, then FILETIMEs: `+0x18` last read, `+0x20` the caller's, `+0x28`
written, `+0x30` reusable after (written + the first lifetime), `+0x38`
expires (written + the second lifetime); the name at `+0x40` (260 bytes).
Lifetimes are seconds; -1 never runs out in practice.

| Address | What |
|---|---|
| `FUN_00518de0` | CRC-32 (table `0x72fb80`, init and final `~`); EAX buffer, ECX length; null buffer with a length is fatal |
| `FUN_00518e60` | MPQ block decrypt (seed `0xeeeeeeee`, crypt table `+0x1000`) |
| `FUN_00518ec0` | MPQ block encrypt, same key schedule |
| `FUN_00518f30` | Now as FILETIME plus ESI seconds; cached for one tick count |
| `FUN_00518fa0` | MPQ name hash: `toupper`, seed `0x7fed7fed`, table `type * 0x100` |
| `FUN_00519000` | Builds the 0x500-entry MPQ crypt table (seed `0x100001`, `* 0x7d + 3 % 0x2aaaab`) |
| `FUN_00519110` | Hash lookup by name (types 0/1/2), linear probe; returns a matching locale or the locale-0 slot, else -1 |
| `FUN_005191d0` | Deletes every entry for a name: block cleared, hash slot `-2` |
| `FUN_00519220` | Builds the cache path (creates both folders) |
| `FUN_005192a0` | Closes and deletes the file |
| `FUN_00519300` | Closes: frees tables, header, crypt table; handle back to -1 |
| `FUN_005193c0` | API `+0xc8`: frees a buffer the cache returned (ECX) |
| `FUN_005193e0` | Opens or creates the cache. Checks magic, header CRC, table offsets, sizes (file ≤ 10 MB) and table CRCs; any failure logs "WARNING- cache file is invalid. Replacing with new cache file" with a code 1-12 and starts a new file |
| `FUN_005198c0` | Rebuild: close, delete, `FUN_005193e0` |
| `FUN_00519920` | API `+0xcc`, read: ECX name, EDX FILETIME out, stack data out, size out. Checks the entry header (size, CRC, name, not expired, data ≤ 16 MB, data CRC) and rewrites its access time. A bad entry logs "File: '%s' in bncache.dat is corrupt (%d) - deleting from cache"; a bad file "bncache.dat is corrupt (%d) - rebuilding archive" |
| `FUN_00519db0` | API `+0xd0`, write: ECX name, EDX data, stack size (≤ 16 MB), FILETIME*, two lifetimes in seconds (the second non-zero; news and Warden pass -1, BNFTP 15 and 30 days). Deletes the old entry, reuses a free block that fits (or the first big enough one past its reusable time), else appends (block count < 0x400), then rewrites both tables, their CRCs and the header |

### CACHE.CPP: verified

All 16 functions `FUN_00518de0` ... `FUN_00519db0` (the five CACHE.CPP
entries of `game-srcfiles.tsv` and the helpers around them) run in the
emulator against an independent model of the file (`tools/emu/bncache.py`,
1165 cases, 11 model defects planted and caught). The model builds the
expected file image; after every open, write and read the oracle compares the
whole file byte for byte, the in-memory header fields and both tables, the
logged messages, the critical-section balance and the live Storm allocations.
Hooked: the Storm allocator and free (`FUN_00413020`, `FUN_00412650`), the
log writer `FUN_00518d40` (format and code recorded), the fatal report, and
the Win32 file, folder, clock and critical-section imports (a dictionary file
system with exclusive opens). The CRT's `toupper`, `memset`, `memcpy`,
`_stricmp` and `FUN_00413800` run for real. Not covered: a real file system,
thread contention, the logger's formatting, and the callers (news, Warden,
BNFTP).

- **Open** (`FUN_005193e0`) opens with `OPEN_ALWAYS`, share mode 0 (a second
  game, or a second open, fails and leaves no cache). A file of 0 bytes, over
  `0xa00000` bytes, or with the `BN2` magic is replaced with no log line. The
  rest log "WARNING- cache file is invalid" and the code: 1 under 0x2c bytes,
  2 short header read, 3 bad magic, 4 header CRC, 5 hash offset or count 0,
  6 block offset or count 0, 7 hash table past the end, 8 short hash read,
  9 hash CRC, 10 block table past the end, 11 short block read, 12 block CRC.
  A replacement deletes the file, creates it empty and builds the 0x400-entry
  tables in memory only (nothing is written until the first write); the
  header's `+4` is 0x2c and the file end starts at 0x802c.
- **Hash entries**: the slot is name hash type 0 `& (count - 1)`; the entry
  holds the type 1 hash, the type 2 hash, locale 0 and the block index.
  `FUN_00519110` returns the exact-locale slot, else the last locale-0 slot of
  the probe, else -1; a `-2` slot is skipped, a `-1` slot ends the probe.
  The tables are encrypted with the type 3 hash of `(hash table)` and
  `(block table)`; the cipher uses the type 4 block of the crypt table.
- **Write** (`FUN_00519db0`) refuses a null or empty name, null data or
  FILETIME, a size of 0 or over 0x1000000 and a zero second lifetime, taking
  no lock. Otherwise it deletes the name, then picks a block: the first free
  block (flags 0) with allocation >= size + 0x144, else the first block with
  that allocation whose entry header is unreadable, not 0x144 bytes, or whose
  reusable time (`+0x30`) is before now, else a new block at the file end
  (block count under 0x400). A reused block keeps its offset and allocation
  and every hash entry that pointed at it becomes `-2`. The entry header
  stamps `+0x18` and `+0x28` with now, `+0x30` / `+0x38` with now plus each
  lifetime (a 64-bit product, so -1 is 136 years), then both tables, their
  CRCs and the header are rewritten. Any I/O failure or a full table logs
  "bncache.dat is corrupt (N) - rebuilding archive" with N = 0xb (no block),
  0xc (no hash slot), 0xd to 0x11 (short writes: entry header, data, hash
  table, block table, header) and replaces the whole cache; the call returns 0.
- **Read** (`FUN_00519920`) zeroes its outputs first. A hash hit whose block
  index is not below the block count (this includes `-1` and `-2`) rebuilds
  (code 1), a short first read of the header 2, a header size field other than 0x144 3, a short rest of the header 4, a block data size over 0x1000000 5,
  a short header write-back 6, a short data read 7. Entry faults (header CRC 1,
  name not equal ignoring case 2, expired 3, data CRC 4) log the "File: ..."
  line and delete the entry in memory only; the file is not rewritten until
  the next write. A block with flags 0 reads as a name mismatch. The caller's
  FILETIME is returned before the data CRC is checked, so a data CRC failure
  leaves it set. On success `+0x18` (last read) is rewritten with a new header
  CRC.
- **Time** (`FUN_00518f30`) caches one static FILETIME keyed by (seconds,
  tick count); a different seconds value or tick re-reads the clock.
- **Name hash** (`FUN_00518fa0`) indexes the crypt table by `type * 256 +
  (signed char)`, so a character of 0x80 or more reads the previous type's
  block; for type 0 that is memory before the table (bugs.md 88).
- **Close** (`FUN_00519300`) frees the block table, hash table, header and
  crypt table (CACHE.CPP lines 0x155, 0x159, 0x15d, 0x165), closes the handle
  and clears the open flag; closing twice is harmless. `FUN_005193c0` frees a
  returned buffer (line 0x16f) and returns 1, or 0 for null.

Faults found (bugs.md 84 to 89): an unbounded name copy in the write, a
double free when the replacement file cannot be created, the 10 MB open limit
against 16 MB entries, unchecked table counts, name hash reads below the table,
and a full block table discarding the whole cache.

## BNNews.cpp: the news list

`SID_NEWS_INFO` (0x46). The list is a growable array of 0x408-byte items
(`"AUItem_NewsTable"`): `+0` flag, `+4` timestamp, `+8` text (0x400),
sorted by timestamp. Globals: capacity `0x881c30`, count `0x881c34`, data
`0x881c38`, first unread index `0x881c40`, newest timestamp `0x881c44`.
Cached in bncache.dat as `BNNews-D2DV` (pointer at `0x72ff80`).

| Address | What |
|---|---|
| `FUN_0051d370` | Saves the unread items to the cache: newest, count (fatal over 0xff), then timestamp + string each; texts ≥ 0x400 abort |
| `FUN_0051d510` | Calls the header callback (`0x881c2c`) once, then the item callback (`0x881c28`) once per unread item |
| `FUN_0051d590` | Array deleting destructor; orphan (stubs.md) |
| `FUN_0051d5d0` | Array resize to N items (Storm realloc, else alloc + copy) |
| `FUN_0051d6a0` | Array clear; orphan (stubs.md) |
| `FUN_0051d6d0` | Array reserve for N more, rounded up to the grow step; orphan (stubs.md) |
| `FUN_0051d7a0` | Array append one item; orphan (stubs.md) |
| `FUN_0051d7f0` | Array reserve exact; orphan (stubs.md) |
| `FUN_0051d810` | Array set count (grows rounded up) |
| `FUN_0051d860` | The 0x46 reply (handler `0x521210` is a `JMP` here): u8 count (fatal over 0x100), u32 at `+5` oldest, u32 at `+9` newest, then u32 timestamp + string each. Timestamp 0 is the MOTD: it calls the item callback and isn't stored. Others replace an item with the same timestamp or insert in order. Then marks the first item at or after the oldest, saves (`FUN_0051d370`) and calls back (`FUN_0051d510`) |
| `FUN_0051da30` | Loads the cache copy; keeps it only if it has ≤ 0x100 items in timestamp order, else clears the list |

## BnSend.cpp leftovers

| Address | What |
|---|---|
| `FUN_0051ac20` | Frees the reply copy at `0x8818c8` (Storm free, line 0x1ef); called on close by `FUN_0051c470` |
| `FUN_0051ad70` | Byte-stuffs a buffer in place: `00` → `FF 01`, `FF` → `FF FF`; fails at 0xff bytes. Orphan (stubs.md) |
| `FUN_0051b1f0` | API `+0x8c`: the 0x1C STARTADVEX3 reply: ECX out = `0x8822a8[0x1c]`, returns `0x882128[0x1c]` (the per-id reply arrays) |
| `FUN_0051b210` | Frees the reply copy at `0x8818d0`; called on close |
| `FUN_0051b820` | The 0x26 READUSERDATA reply (handler `0x521200` jumps here): keeps a copy at `0x8818d8` |
| `FUN_0051b960` | API `+0x7c`: reads the 0x26 READUSERDATA reply from `0x8818d8`: only when it has 1 account, ECX keys and EDX request id; copies each string into the caller's buffers |
| `FUN_0051bd60` | The 0x40 QUERYREALMS2 reply (handler `0x5211d0` jumps here): keeps a copy at `0x8818cc` |
| `FUN_0051bdc0` | The 0x0B GETCHANNELLIST reply (handler `0x5210c0` jumps here): keeps a copy at `0x8818c8` |
| `FUN_0051be20` | The 0x09 GETADVLISTEX reply (handler `0x5210d0` jumps here): keeps a copy at `0x8818d0` |

The four reply copiers are reached only by a `JMP` from their handler,
which Ghidra doesn't list as a reference.

## BnSend.cpp: the senders (verified)

The 28 BnSend functions of the gap map run in the emulator against a model of the packet
each should send (`tools/emu/bnsend.py`, 2016 cases, 11 model defects
planted and caught). The send `FUN_0051c5c0`, the reply wait `FUN_00520b70`,
the expansion test `FUN_0051bec0` (a `JMP` to `FUN_00408f20`, which the
decompiler prints as `thunk_FUN_00408f20`), the Storm allocator, `time()`,
the hash `FUN_005209d0` and the crash-dump reader `FUN_0040d290` are hooked
and logged; the string copy `FUN_004135d0` and `FUN_00413750` (strlen) run
for real. The decompiler gets several of these packets wrong (stack
offsets, the id in `CL`); the asm and the oracle are the reference.

Common to the senders: the strings of the game and chat requests are copied
with `FUN_004135d0(dst, src, 0x100)`, so each is cut at 0xFF bytes and
NUL-terminated; a null source copies nothing. (The account strings 0x59 to
0x5B are `memcpy`ed after a length check, and the user data ones are not cut.) A request that waits (`GETCHANNELLIST`, `GETADVLISTEX`,
`LOGONREALMEX`) first clears that id's slots in the two reply arrays
(`0x882128`, `0x8822a8`) with `FUN_00520c00`, then sends, then waits.
"Product" below is the dword `0x44324456` (`VD2D`), plus `0x13fa` when
`d2exp.mpq` is present (`0x44325850`).

| Id | Function | Packet (after the id) |
|---|---|---|
| 0B | `FUN_0051ac50` GETCHANNELLIST (ECX flag) | u32: 1 when expansion and flag is 0, else 0; waits; returns the copy at `0x8818c8` |
| 0A | `FUN_0051ae10` ENTERCHAT (ECX user, EDX statstring, one unused stack word) | **First a 0x0B packet** with the product dword (result ignored), then user NUL statstring NUL; returns 1 when the 0x0A send is >= 0 |
| 0C | `FUN_0051aca0` JOINCHANNEL (ECX name, EDX force, stack: no-first-join) | u32 flags then the name. A name: flags 2 when EDX is non-zero, else 0. No name: channel "Diablo II", flags 1, plus 4 when expansion and the stack flag is 0 |
| 10 | `FUN_0051af20` LEAVECHAT | empty; returns 1 when the send is >= 0 |
| 0E | `FUN_0051af40` CHATCOMMAND (ECX text) | text read as cp1252, converted to UTF-16 then UTF-8, NUL included. The five holes of cp1252 (81 8D 8F 90 9D) become U+FFFF (`EF BF BF`). Each conversion takes at most 0x1FF units including the NUL: 510 characters, or 510 UTF-8 bytes, pass; one more sends nothing, silently. Null and empty send nothing |
| 15 | `FUN_0051b010` CHECKAD | 16 bytes: `0x49583836` ("IX86"), product, banner id (`0x88252c`), `time(0)`; returns 1 |
| 21 | `FUN_0051b060` DISPLAYAD | 14 bytes: `0x49583836`, product, banner id, two zero bytes; returns 1 |
| 16 | `FUN_0051b6c0` CLICKAD (ECX ad id, EDX flag) | u32 id, u32 (EDX != 0); returns 1 |
| 1C | `FUN_0051b0e0` STARTADVEX3 (ECX name, EDX password, stack: description, field) | u32 (password non-empty), 0, field, 0, 0, then name, password, description as NUL strings. A null string is sent as empty. Waits for nothing; returns 1 |
| 09 | `FUN_0051b240` GETADVLISTEX (ECX name, EDX password, two unused stack words) | u32 0, 0, 0, 0x19, then name, password and an empty string; waits; returns the copy at `0x8818d0` |
| 22 | `FUN_0051b320` NOTIFYJOIN (ECX game, EDX user) | product, 0x0E, game NUL user NUL |
| 3E | `FUN_0051b460` LOGONREALMEX (ECX realm title, EDX password) | Null title: nothing, returns 0. Else counter `0x8818c4` + 1 (stored back), then the 20-byte hash of the 28 bytes (counter, token `0x8818bc`, hash of the password), then the title NUL. Waits; returns the copy at `0x8818d4`; a send below 0 returns 0 without waiting. The hash is `FUN_005209d0` (the old broken SHA-1), not checked here |
| 12 | `FUN_0051b540` LOCALEINFO | 0x20 bytes: FILETIME of `GetSystemTime`, FILETIME of `GetLocalTime`, the time zone Bias (i32), system LCID, user LCID, user LangID (u16 widened); then `GetLocaleInfoA(0x400, t, buf, 0x40)` for t = 3, 5, 7, 0x1002, each NUL-terminated in turn. Only the first byte of the string area is cleared, so a failing `GetLocaleInfoA` leaves stack residue in its string |
| 27 | `FUN_0051b6f0` WRITEUSERDATA (ECX account, EDX count, stack: keys, values) | u32 1, u32 count, account, keys, values (NUL strings). Clears id 0x27, builds the packet in a Storm block of exactly its size (line 0x4c4, freed at line 0x4e0), returns 1 when the send is >= 0 |
| 26 | `FUN_0051b870` READUSERDATA (ECX account, EDX count, stack: request id, keys) | u32 1, count, request id, account, keys; block of exactly its size, line 0x506, freed at 0x521 |
| 59 | `FUN_0051ba00` SETEMAIL (ECX email) | email NUL. Null: fatal, line 1373 (0x55d). Over 0xFF bytes: fatal, line 1381 (0x565) |
| 5A | `FUN_0051baa0` RESETPASSWORD (ECX account, EDX email) | account NUL email NUL. Null first: fatal 1400; empty first: fatal 1401; a null or empty second returns 0 quietly (before the length checks); a first over 0xFF bytes: fatal 1409; a second: fatal 1416 |
| 5B | `FUN_0051bb90` CHANGEEMAIL (ECX account, EDX old, stack: new) | three NUL strings. Null first: fatal 1433; empty first: fatal 1434; any null or empty later one returns 0 quietly; then over-long first, second, third: fatal 1444, 1452, 1460 |
| 5D | `FUN_0051bcf0` (crash report) | asks `FUN_0040d290` (the "Crashdump" file reader) for up to 0x5dc bytes; sends them only when it reports data. The reader is not traced here |

The fatal path is `FUN_00408a60(&"", return address, line)` then `exit(-1)`;
the lines above are its third argument.

**Reply copiers and readers** (the handler jumps straight to them):

| Function | Keeps | Slot | Free line | Alloc line |
|---|---|---|---|---|
| `FUN_0051b3e0` | 0x3E realm logon (battlenet.md: MCP IP at +0x10, port at +0x14 of the reply) | `0x8818d4` | 0x3b2 | 0x3bb |
| `FUN_0051b820` | 0x26 user data | `0x8818d8` | 0x4ec | 0x4ed |
| `FUN_0051bd60` | 0x40 QUERYREALMS2 reply | `0x8818cc` | 0x19a | 0x16b |
| `FUN_0051bdc0` | 0x0B channel list | `0x8818c8` | 0x1ef | 0x1e6 |
| `FUN_0051be20` | 0x09 game list | `0x8818d0` | 0x36a | 0x326 |

Each frees the previous copy when the slot is set, allocates the size, and
copies; `FUN_0051b3e0` stores the size first and, for a reply of 0x48 bytes
or more (but not exactly 8), byte-swaps the word at `+0x18` (the port, `+0x14` of the reply) with `ntohs`.
`FUN_0051ac20`, `FUN_0051b210` free the channel-list and game-list copies
and the 38-byte routine at `0x51b3b0` (no function entry in Ghidra) the realm-logon copy, clearing
the slot. `FUN_0051b1f0` returns the 0x1C reply slots: ECX out =
`0x8822a8[0x1c]`, EAX = `0x882128[0x1c]`. `FUN_0051b960` parses the 0x26
reply for the caller: it needs `0x882128[0x26]` set, 1 account, the same key
count and request id, then for each key copies the string with `strncpy`
into the caller's buffer (padded with NULs to the caller's size, the byte
after it zeroed); an empty value only zeroes the first byte.

**Byte stuffing** (`FUN_0051ad70`, an orphan): in place on a buffer with at
least 0x100 spare bytes, `00` becomes `FF 01` and `FF` becomes `FF FF` (it
moves the following 0x100 bytes up each time). It checks the output
position against 0xFF at the top of each step, and an escape moves the
position by two, so a `00` or `FF` at position 0xFE steps over the check
and processing goes on past 0xFF; otherwise reaching 0xFF returns 0. Not
used.

**Findings** (bugs.md 66 to 68): STARTADVEX3 and GETADVLISTEX build their
strings in a 0x100-byte local, so name + password + description over 0xFF
bytes (with their NULs, over 0x100) smashes the stack cookie and the game
aborts; a null password crashes LOGONREALMEX and a null string in NOTIFYJOIN
sends stack garbage; CHATCOMMAND silently drops a line over 510 characters.

## BnDownload.cpp: BNFTP and patching

A download runs on its own thread; `0x881d5c` counts them, `0x881c54`
cancels, `0x881c50` is the percent shown (0x65 = done), the critical
section `0x881f64` guards the patch name at `0x881d60`.

| Address | What |
|---|---|
| `FUN_0051e5d0` | API `+0x70`: cancel every download |
| `FUN_0051e5e0` | True while a download thread runs; close waits on it |
| `FUN_0051e5f0` | `recv` until EDI bytes are in (EBX buffer); 0 on error |
| `FUN_0051e640` | Resume size: an existing file's size when its write time is within 4 s (`0x2625a00`) of the server's, else 0 |
| `FUN_0051e920` | Extracts one file from an open patch MPQ to disk |
| `FUN_0051e9d0` | Runs each string of a double-NUL list with `ShellExecuteA`, each only after the signature check `FUN_00516de1` |
| `FUN_0051ea20` | Percent = done × 100 / total. Orphan (stubs.md) |
| `FUN_0051ea40` | Sets the percent (ECX) and clears the patch name |
| `FUN_0051ea80` | Stores the server's mpq file name (`0x881c58`) |
| `FUN_0051eaa0` | True when that name is set |
| `FUN_0051eab0` | Receives a file into memory in 1460-byte chunks; flag 1 also writes it to bncache.dat, flag 2 hands it to `FUN_0051e440` |
| `FUN_0051ebb0` | Receives a file to disk from an offset in 1460-byte chunks, stamping the server's FILETIME |
| `FUN_0051ed00` | Applies downloaded patches: for each MPQ in registry `Patch\Patches`, runs its `Prepatch.lst` (`delete`, `extract`, `execute` lines, paths under the game folder); writes `Patch\Launcher` (Game.exe), `SrcData` (d2exp.mpq when expansion, d2data.mpq, d2sfx.mpq), `DstData` (patch_d2.mpq); runs bnupdate.exe when nothing else is listed. ECX out: 0 nothing, 2 launched, -1 failed |
| `FUN_0051f2d0` | API `+0x48`: `FUN_0051ed00`; returns 1 for 0 or 1; for 2 sets `*ECX = 1` (quit to update) and returns 0 |
| `FUN_0051f310` | The download thread: connects to the Battle.net server on 6112 (from the server list, `FUN_0051c190`) (3 tries, next server each), 15 s receive timeout, sends `02` (BNFTP) and the request, reads the reply header (< 0x100 bytes: `+4` size, `+0x10` FILETIME, `+0x18` name), resumes a partial file, then saves it and calls `FUN_0051fab0` |
| `FUN_00520020` | API `+0x50`: downloads a patch mpq by name: records it in `Patch\Patches`, uses the cache copy if one exists, else builds a BNFTP v1 request (u16 length, u16 0x100, `IX86`, `D2DV`, banner id/ext 0, start 0, FILETIME, name) and starts `FUN_0051f310` |

## BnMessQueue.cpp: the message queue

A singly linked list of 0x12c-byte nodes (`+0` id, `+4` data up to 0x120,
`+0x124` length, `+0x128` next), head `0x882020`, tail `0x882024`, guarded by
`0x882004`. The launcher drains it.

| Address | What |
|---|---|
| `FUN_00520210` | Queues ECX id with EDX data, stack length (truncated to 0x120) |
| `FUN_005202f0` | Queues ECX id with EDX string (with its NUL) |
| `FUN_005203b0` | API `+0x24`: pops the head into EDX buffer, ECX id out; returns the length, 0 when empty |
| `FUN_00520440` | Frees every node; called on close |

## WardenClient.cpp

The Warden module manager. It rides D2GS (S→C 0xAE, C→S 0x66), not BNCS.
Globals: saved buffer `0x7bc988` / size `0x7bc98c`, running module
`0x7bc990`, loaded module `0x7bc994`, Bnclient API `0x7bc998`, seed
`0x721c94`. The callback table the module gets is at `0x6d8e68` (pointer
at `0x721c98`).

| Address | What |
|---|---|
| `FUN_0047fc40` | Module deleting destructor (line 0x117). Orphan (stubs.md) |
| `FUN_0047fee0` | Loads a module by its 16-byte name: the built-in one (name `0x6d8768`, data `0x6d74b0`) is used straight; others are read from bncache.dat as 32 hex digits + `.mod` (API `+0xcc`), then `FUN_0047fc70` |
| `FUN_0047ffb0` | Writes a module to the cache (API `+0xd0`, FILETIME now). Orphan (stubs.md) |
| `FUN_00480000` | Callback 0: send; `FUN_00478830` wraps it as C→S `66 u16len data` (≤ 0x1fd bytes) |
| `FUN_00480020` | Callback 1: load module (`FUN_0047fee0`) |
| `FUN_00480040` | Callback 2: cache a module under its hex name + `.mod` |
| `FUN_004800c0` | Callback 3: Storm alloc |
| `FUN_004800e0` | Callback 4: Storm free |
| `FUN_00480100` | Callback 5: keeps a copy of a buffer (one slot) |
| `FUN_00480150` | Callback 6: returns the kept buffer (up to the caller's size) and frees it |
| `FUN_004801d0` | Unloads the running module (its `+4` call, then free) |
| `FUN_00480230` | Starts a loaded module: unloads the old, calls its init (`+0x10`), passes the seed to its `+0` |
| `FUN_004802e0` | S→C 0xAE: `0x45f5f0` (not a Ghidra function) jumps here with ECX = packet + 3, EDX = the u16 at +1, only in game types 2 and 3 (`DAT_007a0610`; net-join.md lists the others). Hands it to the running module's `+8`, then starts any module it loaded |
| `FUN_00480320` | Per game frame (`FUN_0044efa0`): the running module's `+0xc`, then starts any module it loaded |

`FUN_004803c0` (init) takes the seed, takes the Bnclient API and loads the
built-in module; `FUN_00480340` (shutdown) frees everything. game.exe's own
server ignores C→S 0x66 (battlenet.md), so none of this runs in a TCP/IP
game.
