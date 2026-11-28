# SOURCE: SBig API wrappers — game.exe 1.14d

`SOURCE` rows (Storm's big numbers, statically linked) of
`build/gaps/functions.tsv` that were `untouched` on 2026-11-23: the
stdcall wrappers between the cited `SBigFromBinary` (`FUN_00423850`),
`SBigNew` (`FUN_00423950`) and `SBigToBinaryBuffer` (`FUN_00423b30`) of
[source-frontier.md](source-frontier.md). Each takes numbers made by
`SBigNew`, puts the first in `ESI` or `EDI` and calls an internal
routine; the result goes to the first number. Read from the disassembly;
each row except `FUN_00422120` is run by `tools/emu/sbig_api.py` (its
`gaps-runs` line) on random values up to 320 bits against Python's
integers, with `--mutation-check`. `FUN_00422120` (`sbig_set_reserved`)
is a thiscall helper next to `sbig_reserve` and is run by
`tools/emu/sbig_helpers.py` with `--mutation-check`. Re-check with
`tools/ghidra/fn.py <address>`. The
**d2d** column is as in [game-frontier.md](game-frontier.md): d2d has no
SRP or Warden big numbers, so no row has a counterpart.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0041d320` | `sbig_new_forward` | thiscall: `SBigNew` (`FUN_00423950`) with the `ECX` output handle as its sole stack arg; ignores the constructor's return, returns the handle in EAX (`RET 0`) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00422120` | `sbig_set_reserved` | `ECX` number, n (`RET 4`): `+0x10` = n; if n != 0, `FUN_00421fe0` (sbig_reserve) with `0xFFFFFFFF`. Run by `tools/emu/sbig_helpers.py` | no counterpart (as `sbig_pow_mod`) |
| `FUN_00422780` | `sbig_is_zero_fast` | `ESI` number (`RET 0`): normalizes (`FUN_00422140`), then 1 when the number is 0, else 0; the fastcall internal behind `sbig_is_zero` | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423790` | `sbig_add` | dst, a, b (`RET 0xc`): `ESI` = dst, then `FUN_00422300(a, b)`; dst = a + b | no counterpart (as `sbig_pow_mod`) |
| `FUN_004237b0` | `sbig_and` | dst, a, b (`RET 0xc`): `ESI` = dst, then `FUN_00422440(a, b)`; dst = a AND b | no counterpart (as `sbig_pow_mod`) |
| `FUN_004237d0` | `sbig_copy` | dst, src (`RET 8`): unless dst is src, the digits via `sbig_set_digits` (`FUN_004215c0`, src `+4`, `+8`); then `+0xc` and `+0x10` copied; dst = src | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423800` | `sbig_dec` | dst, a (`RET 8`): `ESI` = dst, `EAX` = 1, then `FUN_00422fc0(a)`; dst = a − 1 (run for a ≥ 1) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423870` | `sbig_from_str` | dst, text (`RET 8`): `ECX` = dst, `EAX` = text, then `FUN_004234b0`: dst = 0 (`FUN_00422cc0`), then per byte up to the NUL, dst × 10 + (byte − `'0'`); no digit check, so `"1a"` gives 59 (run on bytes `'0'`..`'~'`) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423890` | `sbig_from_bytes` | a0, a1, a2, a3 (`RET 0x10`): forwards to the SBig count-reader/importer `FUN_004234f0` with EAX = a1, ECX = a2, stack[0] = a0, stack[1] = a3; returns the callee's EAX unchanged (run by `tools/emu/sbig_from_bytes.py`) | no counterpart (as `sbig_pow_mod`) |
| `FUN_004238b0` | `sbig_from_unsigned` | dst, u (`RET 8`): `ESI` = dst, then `FUN_00423540(u)`; dst = the 32-bit u | no counterpart (as `sbig_pow_mod`) |
| `FUN_004238d0` | `sbig_inc` | dst, a (`RET 8`): `ESI` = dst, `ECX` = 1, then `FUN_004221f0(a)`; dst = a + 1 | no counterpart (as `sbig_pow_mod`) |
| `FUN_004238f0` | `sbig_is_odd` | a (`RET 4`): `ESI` = a, then `FUN_00422710`; returns 1 for an odd a, else 0 | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423910` | `sbig_is_one` | a (`RET 4`): `ESI` = a, then `FUN_00422750`; returns 1 when a is 1, else 0 | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423930` | `sbig_is_zero` | a (`RET 4`): `ESI` = a, then `FUN_00422780`; returns 1 when a is 0, else 0 | no counterpart (as `sbig_pow_mod`) |
| `FUN_004239e0` | `sbig_not` | dst, a (`RET 8`): `ESI` = dst, then `FUN_004228c0(a)`; dst = a with its dwords complemented, over a's width (at least one dword): a XOR (2^(32·w) − 1) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423a00` | `sbig_or` | dst, a, b (`RET 0xc`): `ESI` = dst, then `FUN_004229d0(a, b)`; dst = a OR b | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423a20` | `sbig_set_pow2` | dst, n (`RET 8`): `ESI` = dst, then `FUN_00422b00(n)`; dst = 2^n (n >> 5 zero dwords, then 1 << (n & 31)) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423a40` | `sbig_set_one` | a (`RET 4`): `EDI` = a, then `FUN_00422c60`; a = 1 (used count `+4` = `+0x10` + 1, entry `+0x10` = 1) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423a60` | `sbig_set_zero` | a (`RET 4`): `EDI` = a, then `FUN_00422cc0`; a = 0 | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423a80` | `sbig_shl` | dst, a, n (`RET 0xc`): `EDI` = dst, `EAX` = n, then `FUN_00422d00(a)`; dst = a << n | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423aa0` | `sbig_shr` | dst, a, n (`RET 0xc`): `ESI` = dst, `EAX` = n, then `FUN_00422e70(a)`; dst = a >> n | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423ac0` | `sbig_sub` | dst, a, b (`RET 0xc`): `ESI` = dst, then `FUN_00423100(a, b)`; dst = a − b (run for a ≥ b) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423b80` | `sbig_to_binary_view` | a, pointer out, count out (`RET 0xc`): `FUN_004235f0` fills the byte array at a `+0x158`; *pointer out = its buffer `+0x160` (owned by a), and a non-null count out gets its count `+0x15c`: a's little-endian bytes with no high zero byte (none for 0) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423c20` | `sbig_to_str` | a, buffer, size (`RET 0xc`): `FUN_00423600` writes a's decimal digits at `+0x160`, then `FUN_004135d0(buffer, text, size)` copies at most size − 1 of them and a NUL (run for sizes 1..120) | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423c60` | `sbig_to_str_view` | a, pointer out (`RET 8`): `FUN_00423600` writes a's decimal digits at `+0x160`; *pointer out = that NUL-terminated text, owned by a | no counterpart (as `sbig_pow_mod`) |
| `FUN_00423d60` | `sbig_xor` | dst, a, b (`RET 0xc`): `ESI` = dst, then `FUN_00423250(a, b)`; dst = a XOR b | no counterpart (as `sbig_pow_mod`) |
