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
2. `FUN_00411910` validates the pointer; 0 returns 0 (a null pointer is
   therefore not an error to free, it is reported only through 00411910).
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

