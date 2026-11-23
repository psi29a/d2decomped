# SOURCE: SBig API wrappers — game.exe 1.14d

`SOURCE` rows (Storm's big numbers, statically linked) of
`build/gaps/functions.tsv` that were `untouched` on 2026-11-23: the
stdcall wrappers between the cited `SBigFromBinary` (`FUN_00423850`),
`SBigNew` (`FUN_00423950`) and `SBigToBinaryBuffer` (`FUN_00423b30`) of
[source-frontier.md](source-frontier.md). Each takes numbers made by
`SBigNew`, puts the first in `ESI` or `EDI` and calls an internal
routine; the result goes to the first number. Read from the disassembly;
each row is run by `tools/emu/sbig_api.py` (its `gaps-runs` line) on
random values up to 320 bits against Python's integers, with
`--mutation-check`. Re-check with `tools/ghidra/fn.py <address>`. The
**d2d** column is as in [game-frontier.md](game-frontier.md): d2d has no
SRP or Warden big numbers, so no row has a counterpart.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00423790` | `sbig_add` | dst, a, b (`RET 0xc`): `ESI` = dst, then `FUN_00422300(a, b)`; dst = a + b | no counterpart (as `sbig_pow_mod`) |
