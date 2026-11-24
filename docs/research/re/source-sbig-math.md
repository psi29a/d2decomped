# SOURCE: SBig arithmetic wrappers — game.exe 1.14d

`SOURCE` rows (Storm's big numbers, statically linked) of
`build/gaps/functions.tsv` that were `untouched` on 2026-11-24: stdcall
wrappers after the cited `sbig_delete` (`FUN_00424e40`) of
[source-frontier.md](source-frontier.md). Each takes numbers made by
`SBigNew` (`FUN_00423950`); the first is the result, and its temporaries'
pool (`+0x14`, top `+0x154`) serves the internal routine. Read from the
disassembly; each row is run by `tools/emu/sbig_math.py` (its `gaps-runs`
line) on random values up to 320 bits against Python's integers, with
`--mutation-check`. Re-check with `tools/ghidra/fn.py <address>`. The
**d2d** column is as in [game-frontier.md](game-frontier.md): d2d has no
SRP or Warden big numbers, so no row has a counterpart.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00424e70` | `sbig_div` | dst, a, b (`RET 0xc`): takes a pool temporary (top + 1), `EAX` = a, `FUN_00423dc0(dst, temporary, b, pool)`, gives it back; dst = a ÷ b, rounded down (run for b ≠ 0) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00424eb0` | `sbig_gcd` | dst, a, b (`RET 0xc`): `EDI` = dst, `EAX` = b, `FUN_00424180(a, pool)`; dst = gcd(a, b), gcd(a, 0) = a | no counterpart (as `sbig_pow_mod`) |
| `FUN_00424ed0` | `sbig_mod` | dst, a, b (`RET 0xc`): as `sbig_div` with the two outputs swapped, `FUN_00423dc0(temporary, dst, b, pool)`; dst = a mod b (run for b ≠ 0) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00424f10` | `sbig_mul` | dst, a, b (`RET 0xc`): `FUN_004242c0(dst, a, b, pool)`; dst = a × b | no counterpart (as `sbig_pow_mod`) |
