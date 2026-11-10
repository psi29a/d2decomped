# H/stpl.h: list, arithmetic and forwarding helpers

These 34 stock 1.14d functions execute in `tools/emu/h_lists.py`. It pins
the executable SHA-256 to
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The selection is the H module of the gap map at upstream
`83fc7ba1181f548aa65250fa0b2074a3896902de`, not every Storm template
instantiation in game.exe. Prior static notes are in
[chat-ignorelist.md](chat-ignorelist.md).

## Observed rules

A normal list header stores its node-link offset, then the terminator's
previous-link pointer and next-node value. A nonpositive signed next value
terminates traversal; the negative encoding is the complement of the
terminator address. Each live node's embedded link stores its previous-link
pointer and next-node value. Oracle scenarios encode these fields from
high-level node order, run real stock inserts/removals, and compare the
complete 8192-byte arena, including guard bytes and unrelated fields.

| Address | Observed behavior and checks |
|---|---|
| `FUN_0060e0e0` | Return the pointer at object `+8` plus unsigned index times 12; checked at 0, 1, 7 and -1-as-unsigned. No index bounds check. |
| `FUN_0060e110` | Same bucket-address formula; independently executed. |
| `FUN_0060e170` | Resolve the **next** link address from the current link's second word: complement a negative next value; otherwise add the supplied offset. A negative offset derives the offset from current link minus its predecessor's next-node field. The established `stpl_link_prev` name is a historical misnomer. Checked negative/zero/positive next values and explicit/derived offsets. |
| `FUN_0060e1c0` | Copy of the next-link resolver, independently executed with the same boundaries; historical name `stpl_link_prev_2` is preserved. |
| `FUN_0060e1f0` | Bitwise AND of caller hash and table `+0x24`; zero, high-bit and all-bit masks preserve the actual mask result. |
| `FUN_0060e210` | Same hash-mask operation, independently executed. |
| `FUN_0060e2a0` | For unsigned input below 21, return its highest set bit, using 1 for zero. At or above 21, store 21 at object `+0xc` and return 21. Lower inputs leave that field untouched. |
| `FUN_0060e2e0` | Round an unsigned value up to a multiple of the nonzero second argument, with 32-bit overflow. Checked exact multiples, nonmultiples and wraparound; zero divisor is outside the tested contract. |
| `FUN_0060e300` | Same growth-step rule and write gate as the preceding growth helper; independently executed. |
| `FUN_0060e340` | Same unsigned rounding operation; independently executed. |
| `FUN_0060e370` | Read the offset through the argument pointer and construct an empty list header. |
| `FUN_0060e390` | Same pointer-argument initialization; independently executed. |
| `FUN_0060e450` | Return node+header offset for a nonnull node, or header+4 for a null node. It is the **node argument**, not the stored offset, whose zero selects the terminator. |
| `FUN_0060e470` | Initialize an empty list using the offset argument by value. |
| `FUN_0060e490` | Unlink an already attached node, then insert after the reference only for direction value 1, before it otherwise. Null reference selects the terminator, so after means prepend and before means append. Checked unlinked and foreign-list nodes, first/second/terminator references, directions 0/1/2 and offsets 4/12. |
| `FUN_0060e540` | Read the raw next-node word from the selected node link, or header+8 for a null node. |
| `FUN_0060e560` | Repair predecessor/successor links and zero the removed node's embedded link. Checked sole/head/middle/tail nodes and the empty terminator. Unlinking a nonempty terminator is not a valid-list scenario here. |
| `FUN_0060e5b0` | Empty-list initialization with offset by value; independently executed. |
| `FUN_0060e630` | Node-link/terminator-address getter, independently executed. |
| `FUN_0060e650` | Return raw next only when it is signed-positive; zero, negative and high-bit values return zero. |
| `FUN_0060e6e0` | Two-link node unlink: only when node `+8` is nonzero does it attempt the links at `+4` and `+0xc`. A linked second list is deliberately left unchanged when that outer gate is false. Both linked/unlinked combinations are checked. |
| `FUN_0060e7a0` | Real two-link unlink then table virtual slot 0 with the node argument. The callback checks that both links are zero before recording deletion. An absent first link with a linked second is covered by the unlink helper, not this deleting-wrapper fixture. |
| `FUN_0060e7c0` | Signed-positive next-node getter, independently executed. |
| `FUN_0060e820` | Repeatedly unlink the signed-positive list head. Checked empty and one-through-four-node lists at offsets 4/12; terminator remains valid and removed node links become zero. |
| `FUN_0060e890` | Same insert/unlink operation as the earlier instance, independently executed. |
| `FUN_0060e940` | Raw-next getter, independently executed. |
| `FUN_0060e960` | Single-node unlink, independently executed. |
| `FUN_0060e9b0` | Clears terminator fields then constructs the empty list using a pointer-supplied offset; final bytes match the other initializer. |
| `FUN_0060e9e0` | Same clearing initializer, independently executed. |
| `FUN_0060ea10` | If node `+0x14` is nonzero, free it with source line `0x5e4`. Unlink the `+0xc` embedded link, then the `+4` link, independently of the first link's next-field gate. Unlike the two-link-only helper, it removes a linked second list even with an absent first link. |
| `FUN_0060eaa0` | Table-relative next getter: list header starts at table+4; return the next node only when signed-positive. |
| `FUN_0060eae0` | Copy of string-free/two-link cleanup, independently executed. |
| `FUN_0060f510` | Duplicate the supplied text, hash the original with two zero options, call the table lookup with that hash and an address holding the duplicate, then free a nonnull duplicate. Return the lookup result; null duplicate skips only the free. |
| `FUN_00611170` | Same temporary duplicate/hash lifecycle, forwarding two extra caller arguments to insertion. Both existing wrapper routines now execute directly; an address mentioned solely as a hook in an older oracle would not prove their behavior. |

## Re-check and boundary

```sh
cd tools/emu
D2_MPQ_DIR=/path/to/game-data python h_lists.py --mutation-check
```

The normal run passes 498 cases across 34 original entry points, checking
their return values where defined, `RET` stack cleanup, normal return before
the instruction cap, called argument values/order, and the arena. Four
deliberately wrong expected models are rejected: growth cap, unsigned
rounding, insertion side, and free source line.

The list/arithmetic code runs without replacement. Cleanup hooks only
Storm free and the virtual deletion boundary; it cannot establish allocator
or actual object destruction. Forwarding-wrapper cases hook string duplication,
hashing, the final table lookup/insertion, and free; they establish forwarding
and temporary-string ownership, not the table implementation. The array/hash
oracle checks selected H hash/array routines separately; the account-table
lookup implementation `FUN_0060f1d0` remains beyond these cases.
No live game, real registry/file operations, arbitrary malformed-pointer
graph, exception unwinding, or d2d comparison is claimed. A separate suite
checks string/ignore behavior.
