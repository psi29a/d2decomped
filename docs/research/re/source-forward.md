# SOURCE forward — the Storm init forwarder

Trace date: 2026-11-24. A small `SOURCE` function
(`build/gaps/functions.tsv`, module `SOURCE/?`) read from the local Ghidra
decompile and checked against instruction bytes in game.exe 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The persistent oracle is `tools/emu/storm_forward.py`:

```sh
tools/emu/.venv/bin/python tools/emu/storm_forward.py --exe /path/to/Game.exe --mutation-check
```

The oracle validates the executable hash, requires EIP at the return sentinel
and a clean stack after the call, and exercises the entry directly with a
random 32-bit ECX argument. It hooks `storm_init` (`FUN_0041bee0`) as a no-op
so only the forwarding is checked (the init itself is the separate item
`storm_init`, `FUN_0041bee0`). It runs game.exe's own code; no MPQs or files are used.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0041c0c0` | `storm_init_forward` | One-argument fastcall forwarder to `storm_init` (`FUN_0041bee0`): saves the ECX argument in ESI, calls the init, and returns the saved argument unchanged in EAX (`PUSH ESI / MOV ESI,ECX / CALL 0x0041bee0 / MOV EAX,ESI / POP ESI / RET`, 12 bytes). No callers found in the image (XrefsTo: 0). | no counterpart |

`FUN_0041c0c0` is a 12-byte forwarder. It does not store its argument; it
only calls `storm_init` (`FUN_0041bee0`), which builds the critical sections
and the power-of-10 table) and hands the argument back to the caller. The
observable state is the returned value in EAX and a clean stack, since the
single fastcall argument is pushed and popped around the call. The oracle
hooks `storm_init` so the test covers the forwarding on its own; the init's
own behaviour is traced by its own item.
