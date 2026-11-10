# H: hash-table and bucket-array observations

This checks ten functions assigned to `h/stpl.h` by the gap map at upstream
`83fc7ba1181f548aa65250fa0b2074a3896902de`. Their existing names and first
static descriptions in [the ignore-list research](chat-ignorelist.md) are
retained. The original code and matching assembly were read before constructing
synthetic inputs; the oracle runs original instructions with the boundaries
described below. It does not compare a d2d implementation or establish live
Battle.net/ignore-list parity.

| Address | Observation |
|---|---|
| `FUN_0060f590` | Thiscall `(hash,&string)`, `RET 8`. Select bucket `hash & mask` at table `+0x1c`, with 12-byte records. Follow signed-positive node pointers via each bucket's stored link offset. A node must have exactly equal 32-bit hash and `SStrCmp(node+0x14,*string,0x7fffffff)==0`. Empty/negative first links and an exhausted chain return 0. Hash mismatches skip string comparison. |
| `FUN_0060f610` | ECX bucket-array descriptor. For each of descriptor `+4` records, call the string-list clear helper, unlink and zero the terminator at row `+4/+8`, then free the nonnull array pointer at descriptor `+8` with the StringEntry type tag and line −2. The descriptor's count and freed pointer are not cleared here. |
| `FUN_0060f690` | ECX list. Call the MappingEntry clear helper, unlink its terminator when linked, then zero both terminator words. An already-cleared terminator stays clear. The oracle supplies empty lists and records clear dispatch; it does not claim the hooked clear helper's node traversal. |
| `FUN_0060f6d0` | Thiscall `(additional,round)`, `RET 8`. Unsigned 32-bit target is current count plus additional, with wrap. Resize only above current capacity. Nonzero round chooses the stored nonzero step or the real step helper, then rounds target up to its multiple. A computed step of 21 is cached at descriptor `+0xc`; smaller computed steps are returned but not cached. |
| `FUN_0060f720` | Thiscall `(capacity)`, `RET 4`. Clear/unlink truncated MappingEntry lists first; store requested capacity but leave descriptor count `+4` unchanged. Request realloc for `12*capacity`, type tag, line −2 and flag `0x10`. Nonzero realloc result becomes descriptor `+8`. On zero result, allocate replacement with flag 0, initialize `min(old_count,capacity)` empty list headers using the old offsets, clear/unlink their old counterparts, and free the old buffer. The fallback contains no code transferring old nodes to those new headers; the clear dependency itself is hooked here. Newly grown records beyond that minimum are not initialized by this path. |
| `FUN_0060f8b0` | Thiscall `(name)`, `RET 4`. Duplicate name as a temporary string, hash the original with two zero arguments, look up that hash plus the temporary string, free a nonnull duplicate at stpl.h line `0x5e4`, and return the lookup result. Synthetic duplication failure still passes the null temporary value to the hooked lookup; this is not a claim about the real string allocator or lookup's null safety. |
| `FUN_00610be0` | Thiscall `(hash,&string,arg3,arg4)`, `RET 0x10`. Check table growth, remask hash after a nonzero grow result, and call virtual slot `+4` with the selected bucket and both trailing arguments. Unlink an already-linked returned node, append its all-node link to the table list, assign hash, free any old key at line `0x5e9`, duplicate the supplied key and return the node. The oracle checks empty and self-linked returned nodes and an initially empty all-node list. |
| `FUN_00610ca0` | Thiscall `(bucket)`, `RET 4`. Mask ≥`0x3ff` immediately returns 0 without changing the collision counter. Otherwise subtract 3 from counter when above 3, else zero it; increment while walking signed-positive bucket nodes. Only a counter strictly above 13 resets it to zero, invokes the rehash helper and returns 1; an exhausted chain returns 0 with the counter retained. |
| `FUN_00610d30` | Thiscall `(flags)`, `RET 4`. Invoke the table destructor first even for ECX=0; then free ECX with `delete`/line −1 only when ECX is nonnull and the low flag bit is set. Return ECX. Null tests intercept the destructor, so they do not establish that the real destructor accepts null. |
| `FUN_00610df0` | Same insertion/link/key flow as `FUN_00610be0`, with `FUN_00610ca0` as its growth check rather than the other template instance's check. Both routines are executed independently, including rehash-mask changes and key replacement. |

## Re-check

With the existing emulator dependencies and a local stock executable:

```sh
cd tools/emu
D2_MPQ_DIR=/path/to/game-data python h_hasharrays.py
D2_MPQ_DIR=/path/to/game-data python h_hasharrays.py --mutation-check
```

The normal run checks 477 cases: 31 lookup/wrapper cases, 270 reserve cases,
16 destructor cases, 24 resize cases, 120 growth-counter cases and 16
insertion cases. Each call requires normal return within 100000 instructions
and checks the assembly's stack cleanup. Recorded calls include every supplied
stack argument and the this pointer when used. The checks cover changed
descriptor/list/node words and key-pointer writes, plus selected unchanged
fields. They are not a whole-arena memory-diff test.

The mutation mode changes one original instruction byte in fresh emulator
memory, checks the expected original byte before modifying it, and requires
the corresponding observation to reject it. Six mutations alter hash
selection, reserve's equality boundary, growth threshold, destructor flags,
and each insertion routine's mask refresh. No executable on disk is changed.

## Hook boundary

String comparison and duplication, hashing, Storm alloc/realloc/free,
virtual node construction, list-clear dispatch and the table destructor are
stand-ins. A group's dependency can be exercised for real in a different
group: for example lookup is hooked only in the wrapper cases, and the real
growth counter is checked separately from insertion's hooked growth check.
Insertion's synthetic grow result can change the table mask before node
allocation. Reserve runs the actual step helper but intercepts resize.
The six groups use separate emulator instances, so a hook from one cannot
silently replace the function a later group claims to run.

Resize and destructor inputs have empty/self-linked list terminators; actual
populated-list node cleanup, live realloc movement, allocation failures after
fallback alloc, invalid vtables, exceptions and host persistence are outside
this oracle. The fallback test observes the original empty-header writes and
clear/free call order, not successful migration of real bucket contents.

Pinned executable SHA-256:
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
