# UNIT/Player.cpp trace inventory

The gap map at `962ffce255f97c35cdacdb0bcbb8457c73a01822` assigns 34
function entries to `UNIT/Player.cpp`, `004609f0` through `00462c20`. The
assignment follows `game-srcfiles.tsv` (asserts at `004609f0`, `00461180`,
`00462560`, `004625b0`, `00462ba0` and `00462c20`) and the map's inference
between matching assertions. The unassigned run below `004609f0` and the
`UNIT/?` run from `00462ca0` up to the `UNIT/CUnit.cpp` assert at `00464310`
are not counted here.

| Status at that base | Entries |
|---|---:|
| Verified | 13 |
| Ported | 4 |
| Cited | 10 |
| Untouched | 7 |

The 17 cited or untouched entries are read in
[the client player trace](unit-player-trace.md), each with a row there and a
unique name in `names.tsv`; `00461010` keeps its existing name. Numeric names
keep fields whose meaning was not established. The 17 verified or ported
entries already carry their evidence in [collision.md](collision.md),
[cursor-units.md](cursor-units.md), [skills.md](skills.md),
[automap.md](automap.md) and `tools/emu`, and were not re-read.

All 17 reads are static, from the instruction listing; none has an oracle,
and a name does not promote an entry to ported or verified. No d2d behaviour
changed.

Odd things the reads turned up, none a claim of a d2d defect:

- `FUN_00462560` builds the Skills row inline and reads byte `+5` with no
  null check, so an out-of-range skill id would read address 5.
- `FUN_00461b40` asserts (0x548) when `FUN_00461ac0` answers 0 without
  writing its outs, which a null skill node or record does.
- `FUN_00461890` null-checks `FUN_00640e90`'s result, which can be 0 only
  with a null Objects table pointer and index 0.
- `FUN_00460bf0` stores `FUN_0061ab00`'s raw result in `0x7a5260`, where
  `FUN_00460e70` stores the same flag as 0/1.
- `FUN_00461a70` has no caller in `game-calls.tsv`.
