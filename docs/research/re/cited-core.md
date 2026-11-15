# Ten cited SOURCE, ENGINE, DUNGEON and UNITS functions

Trace date: 2026-11-15. These functions were the requested cross-module
batch after regenerating the gap map at upstream `77990456`. The executable
was the local game.exe 1.14d with SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
Calling conventions below come from the instruction bytes, not Ghidra's
incomplete inferred prototypes.

Re-check the batch with:

```sh
tools/emu/.venv/bin/python tools/emu/cited_core.py --exe /path/to/Game.exe --mutation-check
python3 tools/ghidra/fn.py 004209e0 00423380 00426180 004706e0
python3 tools/ghidra/fn.py 0061a010 0061ad30 00620f00 00621210 00621bb0 00623c80
```

The oracle makes 90 scoped stock-code calls. Four negative controls require
wrong event-copy, ENGINE selection, DUNGEON forwarding and dual-wield models
to fail. It loads no MPQs and opens no files other than the executable.

## SOURCE

| Function | Traced behavior | Oracle boundary |
|---|---|---|
| `FUN_004209e0` | ECX is a window id. The head is the signed-positive pointer at `0x708868`; nodes hold next at `+4` and id at `+8`. It returns the first match. A null or signed-negative head/link ends the search and returns zero. | 16 empty, negative, hit and miss cases. The list is synthetic; no allocation or locking is part of this function. |
| `FUN_00423380` | ESI is an SBig object; stack arguments are byte pointer and unsigned count, popped by `ret 8`. It resets the used length to the object's dword offset at `+0x10`, grows capacity in the object's growth quantum, and imports bytes little-endian into 32-bit limbs. A partial limb keeps earlier bytes in that limb; newly exposed limbs are zeroed. | Four direct imports from zero through 33 bytes run the real grow/zero/import code and are read back through the real exporter. Offset/nonzero-limb replacement is read but not claimed by the oracle. |
| `FUN_00426180` | Stack destination/source level pointers, `ret 8`. Copies bucket count `+4` and state `+8`; allocates a new `count * 4` bucket array (SEVT.CPP line `0x3e`), then clones each `+0xc`-linked 0x10-byte handler (line `0x45`) in order. New tail pointers are zero. It does not copy or set the destination's prior-level field `+0xc`. | A three-bucket layout with empty, two-node and three-node chains checks the five payloads, order, distinct storage, zero tails and six allocation records. Allocation failure is not handled by this stock function and is not injected. |

The direct tests close the two boundaries left by
[source-module.md](source-module.md): its bignum wrapper ran the importer only
transitively, and its event push called the deep copier without naming it as
an oracle entry point.

## ENGINE

`FUN_004706e0` takes unit in ECX, draw mode in EDX, then width and height
output pointers on the stack (`ret 8`). It obtains the remapped unit type,
class and mode, builds the COF name, and tries the ordinary then preload COF
lookup. If neither returns a header it leaves both outputs untouched.

For a unit with `+0xc8` bit 3 set, state 0x25 present, and an owner returned
by `FUN_004639d0`, the COF equipment pointer comes from owner `+0x60`;
otherwise it comes from the unit. After a COF lookup it finds the remapped
class in the client record's `+0x28` list (next `+0x10`). A missing client,
list, matching class, or match `+8` record with `+0x14 == 0` is a quiet
return. It zeroes a 0x48-byte cel request, builds and parses it, then writes
`FUN_00601920(cell)` and `FUN_00601950(cell)` to the outputs. A successful
parse with a null cell is fatal at Gfx.cpp line `0xd7a`.

Seven oracle scenarios cover unit/owner equipment, both COF lookup routes,
missing client/class, failed request build, failed parse, and success. The
type remap, state/owner lookup, COF loaders, request builder/parser and cell
metric getters are hooked and their arguments logged; no file loading or
pixel decoding is claimed.

## DUNGEON

| Function | Traced behavior | Oracle boundary |
|---|---|---|
| `FUN_0061a010` | ECX room1; returns the collision record at `+0x20`. Null is fatal assert id `0x2c5`. | Direct nonnull getter. The fatal route is established by disassembly; it is not allowed to terminate the emulator run. |
| `FUN_0061ad30` | Stack room1, x, y (`ret 0xc`); calls `FUN_0066ceb0` with ECX = room1 `+0x10`, EDX = x and stack y, and propagates EAX. There are no checks or branches. | Three boundary-value forwarding cases; the area-cell callee is hooked. |

## UNITS

| Function | Traced behavior | Oracle boundary |
|---|---|---|
| `FUN_00620f00` | Four stack arguments: unit, a, b, c (`ret 0x10`). Null unit is fatal id `0x936`. Calls `FUN_0066a9b0` with ECX unit, EDX b, and stack `(c, a, unit+0x60)`, then stores EAX at unit `+0x50`. This is the mode's AnimData.d2 record assignment. | Three forwarding/store cases with the lookup hooked. |
| `FUN_00621210` | ESI unit and one stack old-position value (`ret 4`). Calls `FUN_00663410(unit+0x30, unit+0x38, old, &mode, &frame, ..., &event)`. Stores `frame << 8` at `+0x44`, mode at `+0x40`, event's low byte at `+0x4e`, and returns whether the mode differs from the previous `+0x40`. | Three same/changed mode and truncation cases; sequence-row lookup is hooked and its pointers are exercised. |
| `FUN_00621bb0` | Stack object and value (`ret 8`). Requires a nonnull type-2 unit, then stores the exact dword at object data (`unit+0x14`) `+8`. Null and wrong type are fatal ids `0xe60` and `0xe67`. | Four values including zero and `0xffffffff`. Fatal paths are disassembly evidence only. |
| `FUN_00623c80` | Stack unit and skill (`ret 8`). A non-dual-wielder returns. It resolves the skill-selected weapon, primary wielded weapon (`FUN_0063bef0`), and, when selected equals primary, the other weapon (`FUN_0063bf90`). No selected weapon updates eligible primary as active. Primary selected updates eligible primary active then other inactive. A different selected weapon updates it active then primary inactive. Each eligibility test is `FUN_00625820(weapon, 0)`; updates are `FUN_00627910(unit, weapon, active)`. | 48 combinations cover the non-dual gate, three selection relations, missing weapons, the generated eligibility states, and ordered updates. The six callees are hooked. |

No d2d mismatch was found in this batch. The affected d2d behavior already
uses typed state or table data rather than exposing these stock record-level
helpers, so no otherwise-unused C++ port is warranted.
