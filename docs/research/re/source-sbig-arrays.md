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
| `FUN_004211a0` | `round_up_multiple_2` | Value, step (`RET 8`): as `round_up_multiple` (`FUN_004210e0`), byte for byte: rounded up to a multiple of the step, a step of 0 faults | no counterpart (as `sbig_grow_step`) |
| `FUN_004211c0` | `sbig_bytes_resize` | `ECX` array, n (`RET 4`): `+0` = n; the buffer becomes `SMemReAlloc(buffer, n, ".E", -2, 0x10)`; when that gives 0, a new `SMemAlloc(n, ".E", -2, 0)` block, and an old buffer has its first min(n, `+4`) bytes copied over and is freed (`SMemFree`, `0x42122d`). Callers `sbig_bytes_reserve` and six more | no counterpart (as `sbig_grow_step`) |
| `FUN_00421240` | `sbig_bytes_grow_step_2` | `ECX` array, n (`RET 4`): the same code as `sbig_bytes_grow_step` (`FUN_00421160`). Two callers | no counterpart (as `sbig_grow_step`) |
| `FUN_00421280` | `round_up_multiple_3` | Value, step (`RET 8`): the same code as `round_up_multiple` (`FUN_004210e0`) | no counterpart (as `sbig_grow_step`) |
| `FUN_004212d0` | `sbig_digit` | `ECX` number, i (`RET 4`): the digit at `+0x10` + i when that index is below the used count `+4` (unsigned), else 0. Callers the bit scans `FUN_004213b0` and `FUN_00421470`, and two more | no counterpart (as `sbig_grow_step`) |
| `FUN_004212f0` | `sbig_compare` | Two numbers (`RET 8`): digit by digit from each one's first digit `+0x10` while either has digits left (0 past the used count); the last pair that differs, so the most significant, decides (`0x42137e`): 1 when the first is bigger, -1 when smaller, 0 when equal. `tools/gaps.py` counts five callers; `FUN_00421530` jumps to it | no counterpart (as `sbig_grow_step`) |
| `FUN_004213b0` | `sbig_top_bit` | Number (`RET 4`): from its top digit down, the first non-zero one's highest set bit, as digit index (from `+0x10`) × 32 + bit (`0x42142e`, through `sbig_digit`); 0 for a number of 0, as for bit 0 | no counterpart (as `sbig_grow_step`) |
| `FUN_00421440` | `sbig_is_even` | `EDX` number (`ECX` unused): 1 when it has no digits (`+4` = `+0x10`) or its low digit (0 past the used count) is even, else 0. Callers `sbig_is_even_stdcall` and one more | no counterpart (as `sbig_grow_step`) |
| `FUN_00421470` | `sbig_low_bit` | Number (`RET 4`): from its first digit up, the first non-zero one's lowest set bit, as digit index (from `+0x10`) × 32 + bit (`0x4214f1`, through `sbig_digit`); 0 for a number of 0 | no counterpart (as `sbig_grow_step`) |
| `FUN_00421510` | `sbig_low_digit` | `EAX` number, `EDX` out pointer: out = the digit at `+0x10` when it is below the used count `+4`, else 0. Only caller `sbig_low_digit_stdcall` | no counterpart (as `sbig_grow_step`) |
| `FUN_00421540` | `sbig_is_even_stdcall` | Number (`RET 4`): `sbig_is_even` with it in `EDX` | no counterpart (as `sbig_grow_step`) |
| `FUN_00421550` | `sbig_low_digit_stdcall` | Number, out pointer (`RET 8`): `sbig_low_digit` with them in `EAX` and `EDX` | no counterpart (as `sbig_grow_step`) |
| `FUN_004215c0` | `sbig_set_digits` | `ECX` number, n, source (`RET 8`): `+0` = n; unless the buffer and n are both 0, the buffer becomes `SMemReAlloc(buffer, n × 4, ".I", -2, 0)`; n dwords copied from the source; the used count `+4` = n. Callers the assignments `sbig_assign`, `sbig_assign_with_step` and thirteen more | no counterpart (as `sbig_grow_step`) |
| `FUN_00421630` | `sbig_bytes_reserve` | `ECX` array, n, round flag (`RET 8`): when the entry count `+0` is below the used count `+4` + n, that size, rounded up (when the flag is set) to a multiple of the grow step `+0xc` (else `sbig_bytes_grow_step` of it), goes to `sbig_bytes_resize` | no counterpart (as `sbig_grow_step`) |
| `FUN_00421680` | `sbig_bytes_set_count_e` | `ECX` array, n (`RET 4`): `+0` = n; unless the buffer and n are both 0, the buffer becomes `SMemReAlloc(buffer, n, ".E", -2, 0)`: `sbig_set_count` for bytes | no counterpart (as `sbig_grow_step`) |
| `FUN_004216b0` | `sbig_bytes_set_count_d` | `ECX` array, n (`RET 4`): as `sbig_bytes_set_count_e` with the tag `.D` (`0x70888c`) | no counterpart (as `sbig_grow_step`) |
| `FUN_00421870` | `sbig_assign` | `ECX` number, source (`RET 4`): unless they are the same, `sbig_set_digits` with the source's used count `+4` and buffer `+8`; returns the number. The grow step `+0xc` stays | no counterpart (as `sbig_grow_step`) |
| `FUN_00421ba0` | `sbig_assign_with_step` | `ECX` number, source (`RET 4`): as `sbig_assign`, then the grow step `+0xc` copied too (also for itself); returns the number | no counterpart (as `sbig_grow_step`) |
