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
(`0x1a334e42`; `BN2\x1a` is the old format and is rebuilt), `+8` file end,
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
