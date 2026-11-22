# The bit buffer — game.exe 1.14d

game.exe packs packets and save items into bit streams, LSB first, with one
small set of functions at 0x410d90..0x411205. No source-file assert names
the module. Every function here is stdcall with its arguments on the stack.
d2d's port is `d2d::net::d2gs::BitReader` / `BitWriter`
(components/net/d2gs/bit_buffer.cpp).

Verified: `tools/emu/bit_buffer.py` runs game.exe's own functions on 20,000
generated cases (reads, signed reads, writes, aligns, bytes used, save
checksums; sizes 0..63, counts -3..40 and the 32-bit ends).
`tests/test_bit_buffer.cpp` answers the same cases with d2d's port and
`d2s::save_checksum`, and the diff is empty. 600 of the cases are its
fixture. `coverage` reaches every instruction of the eight bodies except
`FUN_00410e40`'s null-reader assert.

## The state (`FUN_00410e40`)

`FUN_00410e40(state, buffer, bytes)` fills five dwords. A null state asserts
(`FUN_00408090(0x13c)`, `FUN_00408a60`, `FUN_00681e09`).

| Offset | Field |
|---|---|
| +0x00 | the byte pointer: where stores and loads go |
| +0x04 | the length in bits, `bytes * 8` (`add edx, edx` three times, 0x410e73) |
| +0x08 | the byte index |
| +0x0c | the bit in that byte, 0..7 |
| +0x10 | the overflow flag |

The pointer and the index move together, except after a write's overflow
(below). `FUN_00410e90` gives the bytes used: the index, plus one when the
bit is not 0.

## Reading (`FUN_00410f60`, `FUN_00411020`, `FUN_00411030`)

`FUN_00411020` is a `jmp` to `FUN_00410f60(state, count)`:

1. `past = index * 8 - bits + count + bit`, floored at 0 (`jns`, 0x410f81).
   A nonzero `past` sets the flag, and only `count - past` bits are read:
   a read past the end returns the bits left, not 0.
2. A count of 0 or less after that returns 0 and moves nothing.
3. Otherwise it takes up to a byte's worth at a time,
   `(byte & table_6ce38c[bit]) >> bit & table_6ce368[chunk]`. The first
   table is the bytes `0xff << bit`, the second the dwords `(1 << n) - 1`
   for n 0..8, both read in game.exe. Each chunk is **added** in at
   `shl` by the count read so far (0x410fd3). x86 masks that shift to 5
   bits, so a count over 32 folds its high bits back onto the low ones.
4. A byte used up moves the pointer and the index on.

All the sums are 32-bit. A count of `0x80000000` on an 8-byte buffer
wraps `past` to `0x7fffffc0` and reads all 64 bits, folded, with the flag
set; on an empty buffer it reads nothing and leaves the flag clear
(cases 95 and 1013 of `gen 20000 5`).

`FUN_00411030(state, count)` reads the same, then, when `count < 32`
(`jge`, signed) and bit `count - 1` is set, ORs in `~((1 << count) - 1)`:
a sign extension. `FUN_0045f190` (S->C 0xac) reads a 16-bit field of the
boss and the signed stats with it.

## Writing (`FUN_00410eb0`, `FUN_00411070`)

`FUN_00410eb0(state, value, count)`:

1. If `bit + index * 8 + count > bits` (`jle`, 0x410ecb): the flag is set,
   the index jumps to `bits / 8`, the bit to 0, and nothing is written.
   **The pointer stays where it was.**
2. At bit 0 it zeroes the byte under the pointer (0x410ef3), even for a
   count of 0 or less.
3. Each chunk ORs `(table_6ce368[chunk] & low byte of value) << bit` into
   that byte (an 8-bit `shl bl, cl`). A filled byte moves the pointer and
   the index on and zeroes the next byte while `index * 8 < bits`. Then
   `sar` shifts the value right by the chunk, so a count over 32 writes
   the value's sign.

Two edges follow from the pointer and the 0x410ef3 store:
- After an overflow, a write of 0 bits zeroes the byte the pointer was left
  at, not the one at the index.
- With every bit used and the pointer at the end, a write of 0 bits or
  less stores its zero one byte past the buffer. d2d skips that store.

A count of `0x7fffffff` with bits already used wraps the test negative,
and game.exe then writes past the buffer. The oracle gives that count only
at bit 0. d2d also tests the true (64-bit) end of a positive count, and
takes such a write as an overflow instead.

`FUN_00411070(state)`: mid-byte, the pointer and the index move to the
next byte, the bit goes to 0, and that byte is zeroed while
`index * 8 < bits`. Its one caller is `FUN_006312b0`.

## The save checksum (`FUN_00411130`)

Ghidra's decompile shows an empty loop. The disassembly (0x411144..0x411159)
is `sum = byte + (sum < 0) + sum * 2` over `count` bytes (signed; 0 or less
gives 0): the sum rotated left by one, plus the byte. Callers: `FUN_0043c6b0`
and `FUN_0043caa0` (char-select), `FUN_00569ad0` (the save writer) and
`FUN_0056a090` (the join's header check, net-join.md "Join act"), each with
the field at +0xc zeroed first. d2d's `d2s::save_checksum` is the same sum,
and zeroes bytes 0xc..0xf itself. The oracle's `c` cases keep them zero and
match.

## Two bit counts (`FUN_00411170`, `FUN_004111a0`)

- `FUN_00411170(bytes, count)`: the sum of the dword table at 0x6ce398
  over the bytes, unsigned compares (`jc`). The table is each byte's set
  bit count, so this is the set bits in the run. Its only caller is
  `FUN_0056a620`.
- `FUN_004111a0(value)`: the index of the lowest set bit, -1 for 0. It
  tests the bytes low to high (`test al, al`, then `ah`, then bytes
  +2, +3) and looks the first nonzero one up in the dword table at
  0x6ce798 (its entry 0 is -1), plus 8, 0x10 or 0x18. Callers:
  `FUN_00570e30`, `FUN_005711d0`, `FUN_005d6410`.

d2d calls neither. `bit_buffer.py model` runs both against Python's own
counts: 557 runs of the first, the 256 single bytes among them, and
6,021 values of the second, every byte in each of the four places among
them. All match.

## No caller

`FUN_00410d90` (XOR of two buffers into a third over `(bits + 7) / 8`
bytes, dwords then the 1..3 left), `FUN_004110b0` (writes entry n of a
{value, count} table through `FUN_00410eb0`) and `FUN_004110d0` (walks a
tree of dwords a bit at a time until a non-negative entry, with no end
check) have no call, jump or pointer. The byte scan found none. stubs.md
lists them.
