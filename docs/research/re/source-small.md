# SOURCE small — small Storm functions

Trace date: 2026-11-24. These are small `SOURCE` functions
(`build/gaps/functions.tsv`, module `SOURCE/?`). Read from the local Ghidra
decompile and checked against instruction bytes in game.exe 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The persistent oracle is `tools/emu/storm_small.py`:

```sh
tools/emu/.venv/bin/python tools/emu/storm_small.py --exe /path/to/Game.exe --mutation-check
```

The oracle validates the executable hash, requires EIP at the return sentinel
and a clean stack after the call, and exercises each entry directly:
`FUN_0041c0e0` with a random 32-bit ECX, `FUN_0041c0d0` with random
NUL-terminated strings. It runs game.exe's own code; no MPQs or files are used.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0041c0d0` | `storm_strpbrk` | Tail-calls the MSVC CRT `_strpbrk(str, charset)`: returns the pointer to the first character of `str` (NUL-terminated) that is in `charset`, or 0. Two stack args (cdecl); the args remain on the stack after the CRT returns. No callers found in the image (XrefsTo: 0). | no counterpart |
| `FUN_0041c0e0` | `serr_set_blizzard_error` | `DAT_007790f8` = the ECX argument (a function pointer), no return value. Only caller `FUN_00408ce0` (`error_system_init`, `runtime-support.md`) | no counterpart |

`FUN_0041c0d0` sets up a frame (`PUSH EBP; MOV EBP, ESP; POP EBP`) and
tail-calls the MSVC CRT `_strpbrk` (0x686990), passing `str` and `charset`
through. It allocates nothing and touches no global; the observable state is
the returned pointer and the two stack args the CRT leaves behind. XrefsTo
found no callers for it in the image.

`FUN_0041c0e0` is two instructions: `MOV dword ptr [0x007790f8], ECX` then
`RET`. It installs the global error-report callback that the error path
(`FUN_0041caf0`) calls as `(*DAT_007790f8)(0, 0, code, 0)` when a non-zero
callback is set; when it is zero the path falls back to a `MessageBoxA`.
