# Chat/Ignorelist.cpp and the allocator it uses

Traced from game.exe 1.14d (cited, no oracle run). d2d has no ignore list and
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
