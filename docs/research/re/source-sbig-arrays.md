# SOURCE: SBig array helpers — game.exe 1.14d

`SOURCE` rows (Storm's big numbers, statically linked) of
`build/gaps/functions.tsv` that were `untouched` on 2026-11-23: the
growable-array code next to the cited `sbig_*` rows of
[source-frontier.md](source-frontier.md). An array record keeps its entry
count at `+0`, its used count at `+4`, its buffer at `+8`, its grow step
at `+0xc` and, for a number, its first digit's index at `+0x10`; the
buffers are tagged `.I` (`0x708874`, dwords), `.E` (`0x708880`) and `.D`
(`0x70888c`, bytes) for `FUN_004133d0` (`SMemReAlloc`), `FUN_00413020`
(`SMemAlloc`) and `FUN_00412650` (`SMemFree`). Read from the disassembly;
each row is run by `tools/emu/sbig_arrays.py` (its `gaps-runs` line)
against a model, with `--mutation-check`. Re-check with
`tools/ghidra/fn.py <address>`. The **d2d** column is as in
[game-frontier.md](game-frontier.md): d2d has no SRP or Warden big
numbers, so no row has a counterpart.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00420f80` | `sbig_read_count255` | `EBX` bytes, `EDI` their size, `EAX` and `ESI` out pointers (position, value), no stack arguments: both 0, then while the position is below the size a byte is read and the position moves on; 0xff ends the count, any other byte gives value × 255 + byte (`0x420fa5`); the value is then cut to the bytes left (size − position). Only caller `FUN_004234f0` (`0x423503`) | no counterpart (as `sbig_grow_step`) |
| `FUN_00421160` | `sbig_bytes_grow_step` | `ECX` array, n (`RET 4`): below 0x100 the highest power of two not above n (0 gives 1); from 0x100 on, 0x100, kept at `+0xc` (`0x42118b`). `sbig_grow_step` with a 0x100 cap. Callers `sbig_bytes_reserve` and six more | no counterpart (as `sbig_grow_step`) |
