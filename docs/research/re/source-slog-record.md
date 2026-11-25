# SOURCE SLog record — the record initialiser forwarder

Trace date: 2026-11-25. A small `SOURCE` function
(`build/gaps/functions.tsv`, module `SOURCE/?`) read from the local Ghidra
decompile and checked against instruction bytes in game.exe 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The persistent oracle is `tools/emu/slog_record_forward.py`:

```sh
tools/emu/.venv/bin/python tools/emu/slog_record_forward.py --exe /path/to/Game.exe --mutation-check
```

The oracle validates the executable hash, requires EIP at the return sentinel
and a clean stack after the call, and exercises the entry directly with a
random 32-bit ECX argument. It hooks the record initialiser `FUN_0041f4a0` as
a no-op so only the forwarding is checked (the initialiser itself is a
separate concern: it builds the record's list fields and calls the CRT
constructor at 0x6869fc). It runs game.exe's own code; no MPQs or files are
used.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0041f920` | `slog_record_forward` | One-argument fastcall forwarder to the SLog record initialiser `FUN_0041f4a0`: saves the ECX argument (a record pointer) in ESI, calls the initialiser, and returns the saved argument unchanged in EAX (`PUSH ESI / MOV ESI,ECX / CALL 0x0041f4a0 / MOV EAX,ESI / POP ESI / RET`, 12 bytes). No callers found in the image (XrefsTo: 0). | no counterpart |

`FUN_0041f920` is a 12-byte forwarder. It does not store its argument; it
only calls the record initialiser `FUN_0041f4a0` (which builds the record's
list fields and calls the CRT constructor at 0x6869fc) and hands the argument
back to the caller. The observable state is the returned value in EAX and a
clean stack, since the single fastcall argument is pushed and popped around the
call. The oracle hooks `FUN_0041f4a0` so the test covers the forwarding on its
own; the initialiser's own behaviour is a separate concern.
