# Module H: bounded original-code verification

At upstream `83fc7ba1181f548aa65250fa0b2074a3896902de`, the gap map assigns
63 functions to module H (`h/stpl.h`): 59 cited and four verified. The
existing [ignore-list research](chat-ignorelist.md) already describes their
static behavior. This pass reads those functions and their ambiguous
calling conventions against stock 1.14d, then directly executes the 63
entry points on synthetic inputs. It does not add a gameplay change.

The module is inferred from assertion source paths and linker proximity;
some ignore-list class methods between Storm template instances share
the H label. It is not a claim to verify all of Storm or every occurrence
of an `stpl.h` template in the executable.

| Oracle and detailed findings | Original entry points | Cases |
|---|---:|---:|
| [Lists, arithmetic and forwarding](h-lists.md), `tools/emu/h_lists.py` | 34 | 498 |
| [Strings and ignore-list methods](h-strings.md), `tools/emu/h_strings.py` | 19 | 315 |
| [Hash tables and bucket arrays](h-hasharrays.md), `tools/emu/h_hasharrays.py` | 10 | 477 |

The three address tables partition the 63-entry baseline exactly, without
duplicates. Each routine is invoked for real; a callee replaced by a hook
in one scenario is exercised in a fresh machine for its own claim. The
scripts require normal return within an instruction cap and check stack
cleanup. They observe return values, field writes and/or dependency call
arguments and ordering within the documented boundaries. List models
additionally compare the entire arena encoded independently from node order.
The 1290 normal cases pass. Four deliberately wrong list/arithmetic models
and six instruction mutations in fresh hash/array machines are rejected.

Corrections to earlier shorthand are retained in the existing research:

- The `FUN_0060e170`/`FUN_0060e1c0` pair resolves the **next** link address;
  established `stpl_link_prev` symbols are kept, with corrected evidence.
- A null **node argument** selects a list terminator; a zero stored offset
  is not the selection test in `FUN_0060e450` or its sibling.
- `FUN_0060f720`'s fallback initializes empty replacement headers and
  dispatches old-list cleanup. It does not itself migrate bucket nodes.
- The name-suffix helper's misplaced NUL terminator and ignored fwrite
  result are observable under sentinel-filled strings and synthetic I/O;
  the detailed string trace records their consequences without changing d2d.

## Re-check

Use the existing emulator dependencies and a local stock executable:

```sh
cd tools/emu
D2_MPQ_DIR=/path/to/game-data python h_lists.py --mutation-check
D2_MPQ_DIR=/path/to/game-data python h_strings.py
D2_MPQ_DIR=/path/to/game-data python h_hasharrays.py --mutation-check
```

After the new oracle files are tracked, `python3 tools/gaps.py` reports H
as 63 verified, zero cited/ported/untouched. That is a bookkeeping result
of these bounded observations, not whole-game equivalence. The executable
is pinned in each runner to SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
Static inspection used the cached local Ghidra export with SHA-256
`3ecaa0c989f37641ccc23bb57c6f45d1f08965fd13ad7329c7e88c9749542ccc`
and matching assembly listing with SHA-256
`39aa9fe25018092a6b735e9d7f969adc68452c95a4345c0522ed2355a8cc91f5`.
No game bytes, saves or extracted strings are included in this change.

## Remaining verification limits

String allocation/comparison/copy, registry and FILE operations, virtual
construction/deletion, final hash-table dispatch, Storm allocation/reallocation,
and selected list-clear or rehash boundaries are synthetic where each
oracle declares them. The resize/destructor cases use empty or self-linked
terminators and do not establish live populated-bucket migration. The
string cases leave freed synthetic memory readable, so allocator reuse,
self-assignment, exception unwinding and real storage failures are outside
the evidence. Arbitrary corrupt pointers, real Battle.net state and d2d
parity remain unverified. No game process, MPQ, host registry or ignore file
is accessed by these cases.
