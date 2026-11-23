# SOURCE: small SBig helpers — game.exe 1.14d

`SOURCE` rows (Storm's big numbers, statically linked) of
`build/gaps/functions.tsv` that were `untouched` on 2026-11-23: leaf
helpers next to the cited `sbig_*` rows of
[source-frontier.md](source-frontier.md). `tools/gaps.py` counts no
callers for them but one each for `FUN_00420eb0` and `FUN_00420fe0`. A number
keeps its entry count at `+0`, its used count at `+4`, its dword buffer
at `+8`, its grow step at `+0xc` (`sbig_grow_step`); the temporaries'
pool keeps 0x14-byte numbers from its start and their top at `+0x140`
(`sbig_pow_mod`). Read from the disassembly; each row is run by
`tools/emu/sbig_helpers.py` (its `gaps-runs` line) against a model, with
`--mutation-check`. Re-check with `tools/ghidra/fn.py <address>`. The
**d2d** column is as in [game-frontier.md](game-frontier.md): d2d has no
SRP or Warden big numbers, so no row has a counterpart.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00420db0` | `sbig_index_in_use` | `ECX` number, n (`RET 4`): 1 when `+0x10` + n is below the used count `+4`, unsigned (`CMP`, `SBB`, `NEG` at `0x420db9`), else 0 | no counterpart (as `sbig_grow_step`) |
| `FUN_00420dd0` | `sbig_pool_take` | `ECX` pool, counter pointer (`RET 4`): a non-null counter grows by 1; returns the temporary at pool + top × 0x14 (top `+0x140`), the top grows by 1 | no counterpart (as `sbig_grow_step`) |
| `FUN_00420e00` | `sbig_pool_release` | `ECX` pool, n (`RET 4`): the top `+0x140` goes back by n | no counterpart (as `sbig_grow_step`) |
| `FUN_00420e10` | `sbig_pool_take_if` | `ECX` pool, number, flag (`RET 8`): flag 0 returns the number; otherwise as `sbig_pool_take` with no counter | no counterpart (as `sbig_grow_step`) |
| `FUN_00420eb0` | `sbig_add_carry` | `ESI` a 64-bit accumulator, low and high dwords (`RET 8`): the 65-bit sum; returns its low dword and keeps the rest (sum >> 32) in the accumulator, the carry out of the 64-bit add included (`0x420ec2`..`0x420eed`, `__allmul` by 2^32) | no counterpart (as `sbig_grow_step`) |
| `FUN_00420f30` | `sbig_shift_in` | `ESI` a 64-bit accumulator, digit (`RET 4`): accumulator × 2^32 + digit (`__allmul`), so its low dword moves up and the digit comes in below | no counterpart (as `sbig_grow_step`) |
| `FUN_00420f60` | `sbig_make_qword` | `EAX` high dword, digit (`RET 4`): returns `EDX:EAX` = `EAX` × 2^32 + digit | no counterpart (as `sbig_grow_step`) |
| `FUN_00420fd0` | `sbig_entry` | `ECX` number, i (`RET 4`): `&buffer[i]`, buffer `+8` + i × 4 | no counterpart (as `sbig_grow_step`) |
| `FUN_00420fe0` | `sbig_last_entry` | `ECX` number: the address of the last used entry, `+8` + (`+4` − 1) × 4; 0 when the used count `+4` is 0 | no counterpart (as `sbig_grow_step`) |
| `FUN_00421000` | `sbig_byte_at` | `ECX` number, n (`RET 4`): buffer `+8` + n bytes | no counterpart (as `sbig_grow_step`) |
