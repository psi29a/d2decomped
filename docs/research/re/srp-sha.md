# SRP and SHA source markers — game.exe 1.14d

Evidence is from stock `Game.exe` 1.14d (SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`),
opened in Ghidra through REA. The decompiler output is a guide to the
machine code, not recovered source. The source marker and function ranges
are recorded in `game-srcfiles.tsv` and `game-functions.tsv` at D2Decomped
revision `b2b1977c43c5925cdbddbed068af63f46b420c6a`.

## SRP state allocation: `FUN_00528500` (`.\\SRP.cpp:0x157`)

The constructor asks `FUN_00413020` for `0x4c` bytes with the source marker
`.\\SRP.cpp`, line `0x157`, and flags zero. If allocation succeeds, it
initializes the block through `FUN_00527dc0` and stores the returned state
pointer through its first argument. If allocation fails, it writes null to
that output. The initializer clears dwords at offsets `0` and `0x48`.
`tools/emu/srp_sha.py` checks both allocation outcomes against the stock
binary.

## Interleaved SHA-1: `FUN_0052a120` (`.\\SHA.cpp:0xc8`)

The function takes a byte count on the stack, an input pointer in EDX, and
an output pointer in ECX. It removes leading zero bytes, drops one more
leading byte when the remaining length is odd, then hashes the even and
odd byte lanes independently with SHA-1. The 20-byte digests are
interleaved into a 40-byte result (`even[0], odd[0], even[1], odd[1], ...`).
The implementation uses an internal SHA context and the standard SHA-1
round constants. Its temporary lane buffer is allocated through
`FUN_00413020` with `.\\SHA.cpp:0xc8` and released through
`FUN_00412650` with `.\\SHA.cpp:0xef`. If allocation fails, it returns
without writing the output.

`FUN_00528760` calls this helper while constructing the SRP client proof;
the proof handler at `FUN_005220b0` and password-change path also reach that
code. `docs/design/battlenet.md` describes the surrounding NLS SRP exchange.

The executable oracle in `tools/emu/srp_sha.py` compares the output with
Python's SHA-1 implementation over boundary lengths and zero-prefix cases.
This establishes the helper behavior on this binary; it does not establish
SRP session compatibility or network-interaction parity.
