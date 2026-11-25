# SOURCE Storm init — the Storm init

Trace date: 2026-11-25. A small `SOURCE` function
(`build/gaps/functions.tsv`, module `SOURCE/?`) read from the local Ghidra
decompile and checked against instruction bytes in game.exe 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The persistent oracle is `tools/emu/storm_init.py`:

```sh
tools/emu/.venv/bin/python tools/emu/storm_init.py --exe /path/to/Game.exe --mutation-check
```

The oracle validates the executable hash, requires EIP at the return sentinel
and a clean stack after the call, and exercises the entry directly. It hooks
the three callees as no-ops and checks only the init's shape: the three
callees are reached, in order, and the function returns normally.
`--mutation-check` plants defects in the expected call sequence and requires
each to be caught. Checks game.exe only: d2d has no counterpart
(source-storm-init.md).

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0041bee0` | `storm_init` | A no-argument void init that calls three functions in a fixed order and then returns: `CALL 0x00411cd0` (memory-protection critical sections, 0x100 of them), `CALL 0x00420040` (SLog critical sections, 4 of them), then `JMP 0x00414360` (power-of-10 table plus `DAT_007788dc = 1`). 15 bytes (`CALL 0x00411cd0 / CALL 0x00420040 / JMP 0x00414360`). Callers `FUN_0041c0c0` (`storm_init_forward`) and a 5-byte `JMP` trampoline at 0x6ca890. | no counterpart |

`FUN_0041bee0` is a 15-byte init with three instructions: two `CALL`s and one
`JMP` tail-call. It takes no arguments and returns nothing. The first call
`FUN_00411cd0` lazily initialises the memory-protection critical sections
(`DAT_007788d0` guard, 0x100 sections at `0x776cc0`), the second `FUN_00420040`
initialises the four SLog sections at `0x779238`, and the third, via the tail
call `FUN_00414360`, builds the power-of-10 table (`FUN_004142a0`) and sets
`DAT_007788dc = 1`. The oracle hooks the three callees so the test covers the
init's call order and clean return on its own; each callee's own behaviour is a
separate concern.
