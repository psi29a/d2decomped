# Cel files and the cel list cache (`SRC/CelCmp.cpp`) — game.exe 1.14d

The untouched functions bracketed by `SRC/CelCmp.cpp` in
`game-srcfiles.tsv` (`0x600f10` .. `0x6039c0`). Two kinds of code sit
there:

- cel file helpers, asserting with `.\SRC\CelCmp.cpp` (`0x6e5dd0`);
- Storm's `TSHashTable<CELLIST, HASHKEY_NONE>` template code (RTTI at
  the vtable `0x6e5de8`'s locator `0x700434`), with its lists of
  `CELLIST` records (tag `.?AUCELLIST@@`, `0x741f2c`) and their
  `TCELENTRY` records (tag `.?AUTCELENTRY@@`, `0x741f14`).

Read from the decompile and the disassembly; no oracle was run. A
function is **reached** when a pointer to it is in the image or a call
or jump to it comes from code outside the dead set below. The dead set
is the functions with no pointer and no call or jump from anything but
another dead one (byte scan of `0x401000` .. `0x7d0000` for the address,
and of `.text` for an E8 / E9 to it, repeated until nothing changes).
The list code follows storm-file.md's conventions ("Intrusive lists").
Where a function is byte-identical to one storm-file.md describes, its
row says so.

## The cel file

What these functions read of a cel file (the DC6 file as loaded,
`FUN_00601340`; frame header in [cels.md](cels.md)):

| Offset | Field | Evidence |
|---|---|---|
| `+0x00` | version: 6; 5 is the older layout `FUN_00600fb0` converts | `FUN_00601340`, `FUN_00600fb0`, `FUN_006019b0` |
| `+0x04` | flags: bit 0 loaded, bit 1 frame offsets made pointers (`FUN_00601340` sets it), bit 2 three bytes a pixel (`FUN_006015d0` copies width × height × 3) | the functions named |
| `+0x08` | kind: the index into the tables at `0x6e3680`, `0x6e3688` (the row writers, cels.md), `0x6e3690` and `0x6e3698`. Each holds two entries; kind 1's are 0. Kind 0: `FUN_00608ee0`, `FUN_00608540`, `FUN_00606b50`, `0x606b00` (not a function entry in game-functions.tsv) | the tables' bytes |
| `+0x10`, `+0x14` | directions, frames per direction | `FUN_006019b0`, `FUN_00601340` |
| `+0x18` | the frames, directions × frames, each an offset made a pointer | `FUN_00601340` |

A frame's `+0x18` is a 0x2c-byte record `FUN_00601340` gives each frame
(one block, line 0x3a6). Its `+0x20` is the frame's data
(`FUN_00601980`).

## The hash table

`TSHashTable<CELLIST, HASHKEY_NONE>`, as the reached code uses it. One
instance is global at `0x8e8cc0` (its critical section `0x8e8d10`, used
by `FUN_00603860`, `FUN_006039c0` and `FUN_00603a70`).

| Offset | Field |
|---|---|
| `+0x00` | vtable `0x6e5de8`: slot 0 `FUN_006036d0`, 1 `FUN_00603610`, 2 `FUN_006037b0`, 3 `FUN_00603780` |
| `+0x04` | the record list (link offset, head link `+0x08` / `+0x0c`) |
| `+0x10` | zeroed by `FUN_00602800` and `FUN_00603780` |
| `+0x14` | the buckets, a growable array: capacity `+0x14`, count `+0x18`, data `+0x1c` (12-byte lists), growth step `+0x20` |
| `+0x24` | the mask: a key's bucket is key & mask |
| `+0x48`, `+0x4c` | the last key handed out; set once it has wrapped (`FUN_00603280`) |
| `+0x50` | a critical section |

A `CELLIST` record is 0x38 bytes plus extra (`FUN_00603610`): the key at
`+0x00`, links at `+0x04` and `+0x0c`, flags at `+0x18` (bit 2: three
bytes a pixel, `FUN_00603860`), a count of entries at `+0x24`, and its
`TCELENTRY` list at `+0x2c` (head link `+0x30` / `+0x34`). A
`TCELENTRY` is 0x24 bytes plus extra (`FUN_006029b0`): its link at
`+0x00`, then six fields and a buffer at `+0x20` (`FUN_00603860`).

## Reached code

| Address | What it does |
|---|---|
| `FUN_00601650` | (13 stack arguments, `ret 0x34`): draws a band of a cel's rows. Asserts: the layer's cel file (`+0x34`) is version 6 (line 0x4c6) without bit 2 (0x4c7), argument 6 is set (0x4ca), the layer has a frame at `+0x3c` (0x4cd). The cel's last row y + offset_y less argument 11 is the band's bottom, less argument 12 its top; clipped to rows 0 .. argument 7 − 1, and nothing for an empty band. Then the kind's row writer (`0x6e3688`, line 0x4e9 when 0) at x + offset_x. A null `+0x34` skips both checks and faults reading its kind (`0x6016e6`); whether a caller passes one is not traced. Callers `FUN_005121a0`, `FUN_00512250` (DirectDraw), `FUN_006c86a0`, `FUN_006c8750` (GDI) |
| `FUN_00601a30` | `DAT_008e8cb8` = the argument (`ret 4`): the hook `FUN_00601a50` calls for each frame with a record. Callers `FUN_0050b7b0` (`glSprite.cpp`), `FUN_006b7110` (`d3dSprite.cpp`) |
| `FUN_00601a40` | returns `DAT_008e8cb8`. Caller `FUN_0060a9d0` (`GfxHash.cpp`) |
| `FUN_00601b10` | a frame's `+0x18` record; a null frame is fatal (line 0x62a). Callers `FUN_0050a450`, `FUN_006b5d40` |
| `FUN_00601e20` | ECX = array, stack count (`ret 4`): the growth step. Below 0x15, the count's highest set bit (1 for 0); else `+0xc` = 0x15 and 0x15 |
| `FUN_00602430` | ECX = record: unlinks its `+0x0c` link, then its `+0x04` link |
| `FUN_006025e0` | ECX = list: byte-identical to storm-file.md's `FUN_00418900` (unlinks the first item until there is none) |
| `FUN_00602640`, `FUN_00602690` | ECX = list: the same loop. Byte-identical to each other |
| `FUN_00602800` | ECX = table, stack flag (`ret 4`): `+0x10` = 0, empties the record list, then each bucket: flag 0 deletes each record (slot 0), else only unlinks it. Callers `FUN_00603460`, `FUN_00603780`, `FUN_00603fc0`, `FUN_006046e0` |
| `FUN_00602aa0` | ECX = list: byte-identical to storm-file.md's `FUN_00418c60` (empty it, unlink its head). Reached by the unwind handler `0x6ca270` (ECX = table + 4) |
| `FUN_00602bf0` | ECX = an entry list: unlinks and frees each `TCELENTRY` until it is empty. Callers `FUN_006039c0`, `FUN_00604940` |
| `FUN_00602d20` | ECX = a record list: for each first record, empties its entries, unlinks its `+0x30` head and its `+0x0c` and `+0x04` links, and frees it. Callers `FUN_00603e50`, `FUN_00604640`, `FUN_006046e0` |
| `FUN_00602f10` | ECX = the bucket array, stack capacity (`ret 4`): empties the lists past it, then reallocates (`FUN_004133d0`, tag `.?AV?$TSExplicitList@UCELLIST@@@@` `0x741f44`, flag 0x10). When that fails it allocates anew, sets up each kept list in the new block, empties the old one and frees it |
| `FUN_00603060` | ECX = the bucket array: empties each list and unlinks its head, then frees the data |
| `FUN_006030e0` | ECX = table, stack key, a second argument it does not read (`ret 8`): the lookup. Walks bucket key & `+0x24` for the record whose `+0x00` is the key; 0 when none |
| `FUN_00603140` | ECX = an array of 0x38-byte records (count `+4`, data `+8`): empties each one's entries and unlinks it, then frees the data. Caller `FUN_00603b60` |
| `FUN_00603240` | `jmp FUN_00603060`. Reached by the unwind handler `0x6ca27b` (ECX = table + 0x14) |
| `FUN_00603280` | ECX = table: adds 1 to `+0x48`; when that wraps to 0 it sets `+0x4c` and goes on. Once `+0x4c` is set, it goes on until the lookup misses. Returns the key. Callers `FUN_00604840`, `FUN_006048c0` |
| `FUN_00603300` | ECX = the bucket array, stack count (`ret 4`): grows (capacity rounded up to the step) and sets up each new list (link offset `0xdddddddd`, empty head), or empties the lists it drops; count = the argument. Callers `FUN_00603bd0`, `FUN_00603c80` |
| `FUN_00603460` | the table's destructor: its vtable back, `FUN_00602800(1)`, the buckets freed (`FUN_00603060`), the record list emptied and its head unlinked. Callers `FUN_006037b0`, `FUN_00604640` |
| `FUN_00603610` | slot 1 (list, extra, flags; `ret 0xc`): allocates a `CELLIST` of 0x38 + extra bytes (flags \| 8), zeroes its links, sets up its entry list, and links it at the list's tail |
| `FUN_006036d0` | slot 0 (record; `ret 4`): empties its entries, unlinks its head and links, frees it |
| `FUN_00603780` | slot 3 (ECX = table): `FUN_00602800(1)`; `+0x10` and `+0x24` = 0; the buckets freed and `+0x14`, `+0x18`, `+0x1c` = 0 |
| `FUN_006037b0` | slot 2: byte-identical to storm-file.md's `FUN_00418b20` family, with `FUN_00603460` as the destructor |

## Not reached

No pointer, and no call or jump from anything but another function in
this list. The cel file ones assert with `.\SRC\CelCmp.cpp`.

| Address | What it does |
|---|---|
| `FUN_00600f10` | (file, out, a3, a4; `ret 0x10`): needs bit 2 (line 0x2ad). A new version-6 header (line 0x2b1) with the old flags less bits 0..2, the same directions and frames, kind 0; then `0x6e3690[kind]` with ECX = the old file, EDX = the new one. `*out` = the new one; 1 |
| `FUN_00600fb0` | (file, out, size; `ret 0xc`): needs bit 0 (0x2cb) and version 5 (0x2cc). Makes the frame offsets pointers, sets bit 1, then builds a version-6 copy (line 0x2e3) frame by frame; writes it and its size |
| `FUN_00601180` | (file, a2; `ret 8`): needs bit 2 (0x322); `FUN_0060d110` for each frame (ECX = the frame, EDX = a2) |
| `FUN_006011f0` | (file, out, size; `ret 0xc`): a flat copy (line 0x345): header, then each frame with its data; bit 1 cleared, bit 0 set. Frees the first frame's record when there is one (line 0x375) |
| `FUN_00601410` | (pointer; `ret 4`): null is fatal (0x40d); frees it (0x40e) |
| `FUN_00601450` | (file, a2; `ret 8`): null is fatal (0x431); `0x6e3698[kind]` for each frame (ECX = the frame, EDX = a2) |
| `FUN_006015d0` | (file, direction, frame, out; `ret 0x10`): needs version 6 (0x4a0) and bit 2 (0x4a1). The frame at `FUN_00600c90(direction, directions)` × frames + frame; copies width × height × 3 bytes of its data to out; 1 |
| `FUN_00601810` | (layer; `ret 4`): 1 when its `+0x34` is a version-6 file, `+0x40` is below 0x40, and its `+0x00` is at most the frames per direction (`<=`) |
| `FUN_00601980` | a frame's data (`+0x20`); null is fatal (0x5b4) |
| `FUN_006019b0` | a file's directions (`+0x10`); not version 6 is fatal (0x5c2) |
| `FUN_00601b80` | ECX = table, stack out, flag (`ret 8`): enters `+0x50`; `*out` = 1 with the flag, else −1 |
| `FUN_00601bc0`, `FUN_00601c30`, `FUN_00601d30` | byte-identical to storm-file.md's `FUN_00416770` |
| `FUN_00601cb0` | 1 when the argument is 1 |
| `FUN_00601cc0` | ECX = array, stack index: data (`+8`) + index × 12 |
| `FUN_00601d00` | ECX = table, stack key: key & `+0x24` |
| `FUN_00601e60` | (n, m; `ret 8`): n rounded up to a multiple of m |
| `FUN_00601e80` | ECX = list, stack pointer to an offset: the list set up with that link offset and an empty head |
| `FUN_00601ea0` | (file; `ret 4`): null is fatal (0x41b). With bit 0 or 1: bit 1 runs `FUN_00601a50`; then frees it (0x422) |
| `FUN_00601fa0` | byte-identical to storm-file.md's `FUN_00417a10` |
| `FUN_00601fc0` | byte-identical to storm-file.md's `FUN_00417a70` (the insert) |
| `FUN_00602070` | byte-identical to storm-file.md's `FUN_00417b20` |
| `FUN_00602090`, `FUN_00602400` | ECX = table, stack flag: leaves `+0x50` when the flag is set. Byte-identical |
| `FUN_00602110`, `FUN_00602280` | ECX = list, stack item: the item's link (item + the link offset), or list + 4 when null. Byte-identical |
| `FUN_00602190` | byte-identical to storm-file.md's `FUN_00417e90` |
| `FUN_006021e0` | (record; `ret 4`): when `+0x08` is set, unlinks its `+0x04` and `+0x0c` links |
| `FUN_00602260` | ECX = list, stack item: the item's next, or list `+8` when null |
| `FUN_006022a0`, `FUN_00602590` | the link as `FUN_00602110` picks it, then unlinks it. Byte-identical |
| `FUN_006022f0`, `FUN_00602350` | ECX = list, stack offset: set up with that offset and an empty head. Byte-identical |
| `FUN_00602310` | as `FUN_00602260`, but 0 when that is not positive |
| `FUN_006023b0`, `FUN_00602980` | as `FUN_00601e80`, zeroing the head first. Byte-identical |
| `FUN_006023e0` | byte-identical to storm-file.md's `FUN_004185d0` |
| `FUN_006024e0` | ECX = list, stack item, mode, where (`ret 0xc`): the insert, with the item and `where` given as items, not links |
| `FUN_00602620`, `FUN_006027e0` | ECX = table, stack record: unlinks it (`FUN_006021e0`) and deletes it (slot 0). The same code |
| `FUN_006026e0` | ECX = list, stack offset: when the offset differs, empties the list and sets it up with the new one |
| `FUN_006027a0`, `FUN_00602890`, `FUN_006028d0`, `FUN_00602ae0`, `FUN_00602df0` | as `FUN_00602aa0`: empty the list (`FUN_006025e0`, `FUN_00602640` or `FUN_00602690`), unlink its head |
| `FUN_006029b0` | ECX = list, stack mode, extra, flags (`ret 0xc`): allocates a `TCELENTRY` of 0x24 + extra bytes (flags \| 8) and zeroes its link; with a non-zero mode, inserts it by that mode at the head (`FUN_00601fc0`, `0x6029ef`..`0x6029f5`). Returns it |
| `FUN_00602a10` | ECX = table, stack record, flag (`ret 8`): as `FUN_00602620`, then leaves `+0x50` when the flag is set |
| `FUN_00602a40` | byte-identical to storm-file.md's `FUN_00419000` |
| `FUN_00602b20`, `FUN_00602ec0` | ECX = record: empties its entries, unlinks its `+0x30` head and its links. The same code |
| `FUN_00602b70` | ECX = list, stack flags: empties it, unlinks its head, frees it (tag `delete`) with bit 0 |
| `FUN_00602c40` | ECX = list, stack entry: returns the next, unlinks the entry and frees it |
| `FUN_00602cb0` | as `FUN_00602b20`, then frees it (tag `delete`) with bit 0 of its argument |
| `FUN_00602e30` | ECX = list, stack record: returns the next record; empties and unlinks the record and frees it |
| `FUN_006031f0` | ECX = the bucket array, stack n, round (`ret 8`): when count + n is above the capacity, reallocates to it (`FUN_00602f10`), rounded up to the step when `round` is set |
| `FUN_00603250` | ECX = table, stack key: the lookup |
| `FUN_00603400` | ECX = table, stack key, out, flag (`ret 0xc`): enters `+0x50`, `*out` = 1 or −1 by the flag, looks the key up; on a miss leaves `+0x50` (when `*out` is non-zero) and `*out` = 0. Returns the record |
| `FUN_006035a0` | ECX = list, stack mode, extra, flags (`ret 0xc`): allocates a `CELLIST` of 0x38 + extra bytes (flags \| 8), zeroes its links, sets up its entry list; with a non-zero mode, inserts it by that mode at the head (`FUN_006024e0`, `0x6035ec`..`0x6035f5`). Returns it |
| `FUN_00603730` | as `FUN_006037b0`, with `FUN_00603060` |
| `FUN_00603760` | ECX = the bucket array: `FUN_00603060`, then `+0`, `+4`, `+8` = 0 |
| `FUN_006037e0` | ECX = object, stack flags: unlinks its `+0x10` link, frees its record array (`FUN_00603140`), frees it (tag `delete`) with bit 0 |
| `FUN_00603850` | `jmp FUN_00603300` |
| `FUN_00603860` | (key, source, source pitch, rows, width, a6, a7, a8; `ret 0x20`): rows and width must be set (line 0x229) and width at most the pitch (0x22a). Under `0x8e8d10` it finds the key's record in `0x8e8cc0`, links a new 0x24-byte `TCELENTRY` into its entries (count `+0x24` + 1), stores the fields (`+0x08` a8, `+0x0c` width, `+0x10` rows, `+0x14` rows × width, `+0x18` a6, `+0x1c` a7), and allocates a buffer of row bytes × rows (line 0x239; the row bytes are width, × 3 for a record with bit 2) into `+0x20`. Then `FUN_005179b3` with ECX = the buffer, EDX = the source, and (row bytes, rows, pitch, row bytes) (`0x603985`..`0x603994`); what that does is not traced |
| `FUN_006039c0` | (key; `ret 4`): under `0x8e8d10` finds the key's record in `0x8e8cc0`, frees each entry's `+0x20` buffer (line 0x24c, `0x603a10`..`0x603a1f`), frees the entries, unlinks the record and deletes it (slot 0) |
