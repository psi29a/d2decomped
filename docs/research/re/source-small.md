# SOURCE small — small Storm functions

Trace date: 2026-11-24. These are the small `SOURCE` functions
(`build/gaps/functions.tsv`, module `SOURCE/?`) that are a single store of an
argument into a global. Read from the local Ghidra decompile and checked
against instruction bytes in game.exe 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The persistent oracle is `tools/emu/storm_small.py`:

```sh
tools/emu/.venv/bin/python tools/emu/storm_small.py --exe /path/to/Game.exe --mutation-check
```

The oracle validates the executable hash, requires EIP at the return sentinel
and a clean stack after the call, and exercises each entry directly with
random 32-bit arguments. It runs game.exe's own code; no MPQs or files are used.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0041c0e0` | `serr_set_blizzard_error` | `DAT_007790f8` = the ECX argument (a function pointer), no return value. Only caller `FUN_00408ce0` (`error_system_init`, `runtime-support.md`) | no counterpart |

`FUN_0041c0e0` is two instructions: `MOV dword ptr [0x007790f8], ECX` then
`RET`. It installs the global error-report callback that the error path
(`FUN_0041caf0`) calls as `(*DAT_007790f8)(0, 0, code, 0)` when a non-zero
callback is set; when it is zero the path falls back to a `MessageBoxA`.
