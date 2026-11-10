# H: string and ignore-list entry points

These 19 entries belong to the gap map's `H` module (`h/stpl.h`) at
`83fc7ba1181f548aa65250fa0b2074a3896902de`. Some are ignore-list methods
between template instances; the inferred module label does not establish
that they were originally declared in the same header.

The cached stock 1.14d decompilation and assembly were read, and
`tools/emu/h_strings.py` runs the original entry points on 315 synthetic
cases. It requires executable SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
Each call must reach the emulator's return sentinel within 100000
instructions and pop exactly its supplied arguments. The corpus verifies
observable returns, call arguments/order and selected destination memory;
it does not compare a d2d implementation or exercise real storage.

| Address | Traced and checked behavior |
|---|---|
| `FUN_0060dcb0` | Thiscall assignment from a one-pointer string wrapper: free the old nonnull pointer with stpl.h line `0x5e9`, duplicate the source pointer, store the result and return `this`. Duplication failure is stored as zero; there is no rollback. |
| `FUN_0060dd00` | Duplicate the supplied text pointer, store the result at `this`, return `this`. |
| `FUN_0060dd20` | Compare the two wrapped pointers through SStrCmp with limit `0x7fffffff`; return full EAX 1 only for compare result zero, otherwise 0, including negative compare results. |
| `FUN_0060dd50` | Store the base ChatIgnoreList vtable pointer `0x6e6248` at `this`; no independent return-value claim. |
| `FUN_0060dd60` | Restore that base vtable, free `this` with tag `delete`/line −1 when the low flag byte has bit 0, and return `this`. Other flag bits do not request freeing. |
| `FUN_0060dd90` | Free the nonnull wrapped string using stpl.h line `0x5e4`; the wrapper's pointer remains unchanged. |
| `FUN_0060ddb0` | For nonnull global singleton at `0x96bc20`, invoke its virtual slot `+0x2c` with `this=singleton` and stack argument 1. Clear the global after the callback; a null singleton only clears the global. |
| `FUN_0060ddf0` | Read the full persistent-ignore dword at `this+0x184`; values are not normalized to a boolean. |
| `FUN_0060de00` | Store the supplied full dword at `+0x184` and call the registry writer with `Diablo II`, `Persistent Ignore`, zero and that value. The registry implementation is hooked. |
| `FUN_0060de30` | Copy the prefix before the first `*`, limited to 17 bytes and unsigned `capacity−1`, then NUL-terminate at the copied count. A leading star writes an empty result. Capacity zero wraps the unsigned bound, so it is not an empty-buffer guard. |
| `FUN_0060dea0` | Find the first star, copy at most 17 suffix bytes, and limit copying against the **source** index and unsigned `capacity−1`. Write the terminator at the final source index, not the copied count. Empty input or an input without a star leaves the destination untouched. Sentinel-filled output cases check this misplaced terminator and the untouched gap. |
| `FUN_0060df10` | Null/empty names or a false virtual persistence gate return 0 without opening. Otherwise copy/append the line ending to a bounded local buffer, open `this+0x80` in `at+`, store the handle at `+0x7c`, then fwrite and fclose when opened. Assembly returns 1 when fclose returns zero, otherwise 0; fwrite's return is ignored. The corpus supplies fwrite failure with successful close to check that distinction. A failed open stores a null handle and returns 0. |
| `FUN_0060dff0` | Pre-clear destination, zero a 256-byte temporary, try the star suffix, fall back to the prefix when the temporary's first byte is zero, then copy with the caller's capacity. Its zeroed temporary conceals the suffix helper's misplaced terminator in normal results. A null output is a fatal assertion path, not tested here. |
| `FUN_0060e130` | Separate assignment instance with the same free/duplicate/store sequence and return as `FUN_0060dcb0`; both entries execute independently in the oracle. |
| `FUN_0060f870` | Lookup an account name in the table at `this+0x54`; not found returns 0 without touching output. Found reads entry value `+0x18`, copies it with the supplied capacity and returns 1. Hash lookup and bounded copy are hooked. |
| `FUN_00610d60` | Duplicate the supplied name into a temporary one-pointer wrapper, hash the original name with two zero arguments, call table insertion on the original `this` with the hash/wrapper/two remaining arguments, free the nonnull duplicate with line `0x5e4`, and return insertion's EAX. A failed duplicate still reaches insertion with a null wrapper value. The corpus verifies temporary ownership and call order without claiming exception unwinding. |
| `FUN_00610eb0` | With a nonempty name and true persistence gate, derive its key through the real suffix/prefix helpers, lookup in `this+4`, and only for an absent key insert and request file persistence. Return 1 for both present and newly inserted nonempty keys; return 0 for skipped inputs/gates. This return is present in assembly despite a void decompilation. |
| `FUN_00610f90` | With a nonempty name and true gate, lookup the name unchanged in `this+0x2c`; if absent insert it and request persistence as `!name`. Return 1 when the permitted nonempty name was already present or insertion was attempted; return 0 for skipped cases. |
| `FUN_00611050` | Open `this+0x80` in `rt`; on success read 256-byte fgets chunks, truncate at the first CR/LF and insert. A leading `!` with another character routes the rest to `this+0x2c`; a lone `!`, ordinary text and empty stripped lines route unchanged to `this+4`. Close after EOF. Open failure does neither read nor close. Cases include empty files, blank lines, CR inside a line, lone `!` and a 255-byte chunk. |

## Re-check

From `tools/emu`, using the existing dependency environment and a local copy:

```sh
D2_MPQ_DIR=/path/to/game-data python h_strings.py
```

Expected output: `PASS H strings: 19 functions, 315 cases; normal return and RET cleanup checked`.

The oracle independently executes each of the 19 entry points; a coverage
set rejects missing or extra entries. Direct splitting uses 11 input
patterns and capacities 0, 1, 2, 17, 18 and 256, checking the entire
320-byte sentinel-filled output buffer. Object cases cover allocation
failure, null/non-null old strings, positive/negative comparison results,
destructor flag bits and nonboolean persistence values. File stand-ins
cover skip, open failure, close failure, truncated payloads and loader EOF.

## Hook boundary

SStrDup, SMemFree, SStrCmp, bounded copy/append, string length, hashing,
registry writing, virtual persistence/deletion, account lookup, table
insertion, ignore-table lookup, requested persistence and FILE routines are
replaced in the relevant case group. Each group uses a fresh mapped image,
so an entry hooked as a callee in one group executes for real in its own
group. Real name splitting and key selection execute together in the ignore
dispatch cases; actual hash-table insertion and filesystem behavior do not.

The synthetic string routines implement a declared NUL-terminated bounded
contract; their correctness is outside these claims. Freed synthetic memory
remains readable, so allocator reuse, self-assignment hazards and exception
cleanup are not established. The file cases use in-memory stream handles;
no registry, user ignore file, MPQ, game process or network is accessed.
This is bounded original-code evidence, not full game or d2d parity.
