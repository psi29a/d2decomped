# SOURCE: Storm bit reader and writer — game.exe 1.14d

`SOURCE` rows (Storm, statically linked) of `build/gaps/functions.tsv`
that were `untouched` on 2026-11-24: thiscall helpers on two small records,
a bit reader (`+0` the next 16-bit word, `+4` the bits held, `+8` their
count) and a bit writer (`+0` the start, `+4` the room left in bytes, `+8`
the next byte, `+0xc` the bits held, `+0x10` their count). The reader's
only `init` caller is `FUN_0041f9e0`, the writer's `FUN_0041f980`; the
writer's `put` is called by `FUN_0041d9e0` and `FUN_0041f850`
(XrefsTo, 2026-11-24). Read from the disassembly; each row is run by
`tools/emu/storm_bits.py` (its `gaps-runs` line) against a model, with
`--mutation-check`. Re-check with `tools/ghidra/fn.py <address>`. The
**d2d** column is as in [game-frontier.md](game-frontier.md); d2d has no
such Storm bit streams.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0041d880` | `storm_bits_read_init` | `ECX` reader, source (`RET 4`): the bits `+4` = the source's first dword, the pointer `+0` = source + 4, the count `+8` = 0x20 | no counterpart |
| `FUN_0041d8d0` | `storm_bits_peek` | `ECX` reader, n, mask (`RET 8`): when the count `+8` is at most n (unsigned), the next 16-bit word goes in above the held bits (`SHL` by the count, mod 32), the count grows by 16 and the pointer by 2; returns the held bits AND mask, nothing consumed | no counterpart |
| `FUN_0041d900` | `storm_bits_skip` | `ECX` reader, n (`RET 4`): the held bits shift right by n (mod 32), the count `+8` goes down by n | no counterpart |
