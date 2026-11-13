# SOURCE: the 21 cited functions

Trace date: 2026-11-13. The starting local `build/gaps/functions.tsv` has
exactly 21 rows with module `SOURCE` and status `cited`. The table below
preserves that scope, in address order. Module membership is the gap map's
source-file attribution, including its `SOURCE/?` inferred ranges; it is
not proof that every function came from the same original source file.

Read from the local Ghidra decompile and checked against instruction bytes
in game.exe 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The persistent oracle is `tools/emu/source_cited.py`:

```sh
tools/emu/.venv/bin/python tools/emu/source_cited.py --exe /path/to/Game.exe --mutation-check
python3 tools/ghidra/fn.py 0041b680 0041caf0 0041d2a0 0041d2c0 0041d300
python3 tools/ghidra/fn.py 00420320 00420360 00420a10 00420ab0 00420b00 00420bf0 00420c10 00420c30 00420c50
python3 tools/ghidra/fn.py 00423850 00423950 00423b30 00425330 00426210 00426370 006c6a80
```

The oracle uses the already-installed emulator environment, requires no
MPQs, and does not download dependencies. It validates the executable hash,
checks instruction-budget completion and stack cleanup, and exercises all
21 entries directly: **1,446 calls**, plus eight negative controls that
require deliberately wrong expectations to fail. Counts exclude calls
made transitively by another tested function and the negative-control runs.

These are scoped stock-code checks against Python expectations. They do
not establish d2d parity or complete branch coverage. An address reference
in `tools/emu/` makes the gap map say `verified` even for intercepted callees;
that automatic label must not be read as stronger evidence than this table.

## Findings and verification boundaries

`stack N` means N caller arguments, removed by the callee. Register arguments
below come from the assembly, which recovers arguments Ghidra omitted.

| Function | Traced behaviour | Oracle coverage / limits |
|---|---|---|
| `FUN_0041b680` | Stack 7: calls `FUN_0041af60` with the seven arguments unchanged and an eighth zero; propagates EAX. | 3 forwarding cases; actual file loading is intercepted. Earlier gdi-driver.md's file-load description is consistent with this wrapper, but this probe does not verify file contents or ownership. |
| `FUN_0041caf0` | Stack 6: error code, source, line/kind, detail, allow-continue, exit code. Returns zero immediately if either `0x7790f0` or `0x7790e4` is set. Otherwise marks busy, formats a report, calls reporters/UI, and returns one only for allow-continue plus result 7. Other tested results request termination with the caller's exit code, return zero, and clear busy. | 7 cases: both suppression flags and their combination; allow-continue 0/1 crossed with UI results 1/7. Formatting, localization and OS effects are scripted. Debug-break result 2, reporter callbacks, negative source kinds and legacy-Windows exit-status branches are read but not executed. |
| `FUN_0041d2a0` | Stack 3: destination, source, byte count; forwards to the CRT copy. | 48 non-overlapping copies with lengths 0–257 and four destination alignments; surrounding bytes checked. No overlap guarantee asserted. |
| `FUN_0041d2c0` | Stack 3: destination, count, value; fills with the low byte of value. The count precedes the value, unlike CRT memset. | 240 cases, including values 256 and `0xfedcba98`, zero length and guard bytes. Original CRT runs. |
| `FUN_0041d300` | Stack 2: destination, count; clears that many bytes. | 48 cases with alignments, zero length and guards; original CRT runs. |
| `FUN_00420320` | Stack 1: window; calls `FUN_004269c0` for tags `0x534d5348`, `0x534d5349`, `0x534d534a`, `0x534d5347`, in that order, and returns one. | 3 windows including zero; the pop-level callee is intercepted. Its errors do not change the wrapper's return. |
| `FUN_00420360` | Stack 1: window; same tag order, calling `FUN_00426370`, then returns one. | 3 windows with push-level callee intercepted; actual push-level behaviour is separately exercised below. |
| `FUN_00420a10` | ESI = window, no stack arguments. Clears current window `0x7792f4` if equal; looks up the window with ECX; if found clears tags G, H, J, I and removes its record through `FUN_00420970` with ECX `0x708860`. | 4 combinations of current/noncurrent and found/missing; lookup, clear and erase calls logged. |
| `FUN_00420ab0` | ESI = window; stack 3 = tag, code, handler. Looks up window, creates it if missing, then calls `FUN_00426720(tag, window, code, handler)`. | 2 lookup outcomes, ordered calls and propagated result; callees intercepted. |
| `FUN_00420b00` | Stack 6: window, message, wParam, lParam, optional result pointers. Builds a message record; always dispatches G by message. WM_COMMAND (`0x111`) additionally dispatches H by low word of wParam, with high word in record +0x10. Key down/up (`0x100`/`0x101`) dispatch I/J by full wParam. Current-window messages replay with dispatch window zero while record +0 retains the original window. WM_NCDESTROY (`0x82`) tears down the original window after dispatch. Returns one and copies record +0x18/+0x1c to optional outputs. | 20 cases across five messages, current/noncurrent and optional/null outputs. Dispatch is scripted to change result fields; teardown body executes with its lookup intercepted. Handled-result fields do not short-circuit this wrapper's second dispatch. |
| `FUN_00420bf0` | Stack 3: window, code, handler. Calls `FUN_00420a70` with EDI window, ESI handler, stack H tag and code. | 3 argument sets, including null and all-ones; registration callee intercepted. Ghidra's two-argument prototype is incomplete. |
| `FUN_00420c10` | Same three-argument registration interface, tag I. | 3 forwarding cases. |
| `FUN_00420c30` | Same three-argument registration interface, tag J. | 3 forwarding cases. |
| `FUN_00420c50` | Same three-argument registration interface, tag G. | 3 forwarding cases. |
| `FUN_00423850` | Stack 3: bignum handle, bytes, count. Passes handle in ESI to `FUN_00423380`, importing little-endian bytes; a zero-length import clears the previous value. | 114 imports, including zero-length replacement, values up to 2048 bits, and arithmetic inputs; original import code runs. |
| `FUN_00423950` | Stack 1: output handle pointer. Allocates 0x168 bytes at SBig.cpp line 0x521 and constructs the object; allocation failure writes zero to the output. | 108 calls including one allocation failure; constructor runs, allocator is scripted. |
| `FUN_00423b30` | Stack 4: handle, destination, capacity, optional count pointer. Materializes little-endian bytes, copies min(capacity, available length), reports bytes actually copied. Zero exports zero bytes. | 81 exports: capacities 0, 1, 3, 4, 32, 257, null count, zero, multiword and modular-power results; destination guards checked. Original materialization and copy run. |
| `FUN_00425330` | Stack 4: output, base, exponent, modulus. Calls modular exponentiation with EAX = output +0x14 as scratch state. | 25 powers with 8–512-bit odd moduli and exponents 0, 1, 2, 17, 65537, compared with Python pow. Actual arithmetic runs; alloc/realloc/free intercepted. Zero/even moduli and aliased operands are not covered. |
| `FUN_00426210` | EDI = level, no stack arguments. Walks level's bucket array, detaches each handler through handler +0xc before freeing it; frees the array (SEVT.CPP line 0x58), then the level. | 3 layouts: zero buckets, empty bucket, mixed chains. Free order, source metadata and cleared heads checked; frees logged rather than reclaiming memory. |
| `FUN_00426370` | Stack 2: tag, window. Locks `0x77930c`; searches table `0x779300`, count `0x779304`, bucket `(tag XOR window) AND (count−1)`, collision link +0x10. A hit allocates a 0x10-byte level, deep-copies the previous level through `FUN_00426180`, links the old level at +0xc, installs the new level and sets `0x7792f8` to one. Returns zero on miss, one on hit; unlocks both paths. | 5 cases: missing table, zero count, empty bucket, collision miss/hit. Real deep copy, distinct allocations, payload and prior-level link checked. Allocator failure and concurrent mutation are not covered. |
| `FUN_006c6a80` | Stack 9: kind, data, size, palette/output argument, destination, capacity, three optional metadata outputs. Clears non-null metadata first. Missing data/size, or non-null destination with zero capacity, sets error 0x57 and returns zero. Kind zero with at least two bytes detects BM → 1, `0x050a` → 2, otherwise 3. Kinds 1/2/3 dispatch respectively to `FUN_006c60f0`, `FUN_006c6990`, `FUN_006c5730`; other kinds return zero without changing last error. | 720 routing cases cross kinds, signatures, sizes, data presence, destination/capacity and optional outputs. Actual detector runs; all three decoders are intercepted. Exact register and stack arguments checked; no image decoding or rendered-pixel claim. |

## Details that change how earlier citations should be read

The event push in net-join.md is more than an empty handler level: its call
to `FUN_00426180` deep-copies the existing bucket array and handler chains.
Each level has buckets at +0, count at +4, a copied dword at +8, and prior
level at +0xc. Each hash node has tag +0, window +4, current level +0xc,
and collision link +0x10. `FUN_00426210`, previously explicitly unread in
net-join.md, releases one such level's handlers and storage; it does not
walk the prior-level link itself.

The bitmap dispatcher is not a complete decoder. At the kind-1 call,
EDI is the data pointer and EAX/ECX/EDX are the three optional outputs;
kind 2 has data in ECX and its first optional output in EAX; kind 3 keeps
data in ESI and passes the outputs on the stack. Reading only Ghidra's
prototypes loses these arguments. The four event-registration wrappers
likewise end in `ret 0xc`, not `ret 8`.

The error reporter's Ghidra prototype says void, but the assembly returns
zero on suppression and one on the continue path. The oracle records the
termination request in Python; it never terminates a native process.

## Tool review

Before using the decompile extractor, its missing-address behaviour was
changed to exit nonzero (it previously printed a comment and succeeded).
A missing address and a valid extraction were checked locally. The emulator
was reviewed for import dispatch, hooks, memory allocation and its call
limit: a reached instruction limit alone does not raise. The new oracle
therefore independently requires EIP at its return sentinel and the expected
ESP after every top-level call. Reallocation rejects untracked pointers
rather than guessing the old allocation size.
