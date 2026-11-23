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
| `FUN_004237b0` | `sbig_and` | dst, a, b (`RET 0xc`): `ESI` = dst, then `FUN_00422440(a, b)`; dst = a AND b | no counterpart (as `sbig_pow_mod`) |
| `FUN_004237d0` | `sbig_copy` | dst, src (`RET 8`): unless dst is src, the digits via `sbig_set_digits` (`FUN_004215c0`, src `+4`, `+8`); then `+0xc` and `+0x10` copied; dst = src | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423800` | `sbig_dec` | dst, a (`RET 8`): `ESI` = dst, `EAX` = 1, then `FUN_00422fc0(a)`; dst = a − 1 (run for a ≥ 1) | no counterpart (as `sbig_pow_mod`) |
| `FUN_004238b0` | `sbig_from_unsigned` | dst, u (`RET 8`): `ESI` = dst, then `FUN_00423540(u)`; dst = the 32-bit u | no counterpart (as `sbig_pow_mod`) |
| `FUN_004238d0` | `sbig_inc` | dst, a (`RET 8`): `ESI` = dst, `ECX` = 1, then `FUN_004221f0(a)`; dst = a + 1 | no counterpart (as `sbig_pow_mod`) |
| `FUN_004238f0` | `sbig_is_odd` | a (`RET 4`): `ESI` = a, then `FUN_00422710`; returns 1 for an odd a, else 0 | no counterpart (as `sbig_pow_mod`) |
