# Item creation, replenishment and expiry

Static observations of stock 1.14d, at source baseline
`1265c32d6903e856257733878bb04d9085f9af30`. These nine functions complete
the remaining server `ITEMS/Items.cpp` entries below the common item helpers.
The cached Ghidra export was read alongside assembly: omitted register
arguments and inferred `void` return types are not reliable signatures.

| Address | Static finding |
|---|---|
| `FUN_00558270` | ECX game, EDX output, stack item: clear a 0x84-byte creation block. Fill game, item level (at least 1), class, mode, position, quality, item version, flags, seed pointer/initial seed, file index and durability fields from the source. Category 4 copies gold stat 14 to block `+0x34`; otherwise a stackable item copies quantity stat 70 there and asserts if negative. Set word `+0x28=1` and dword `+0x2c=0`. Category 7 copies name and ear class; otherwise a personalized item (flag `0x1000000`) copies its name. Assumes a real item: the partial null defaults do not make later dereferences safe. |
| `FUN_00558530` | ECX game, EDX item: read stat `0xfc` (replenish durability). Zero does nothing. Otherwise look up event 3 for that game/item; an existing event prevents duplication. If absent, queue event 3 at game frame plus signed `2500 / rate + 1`, with two zero trailing arguments. The division truncates toward zero; there is no positive-rate clamp. |
| `FUN_00558580` | Same event gate, argument convention and formula as the preceding helper, using stat `0xfd` (replenish quantity). Both helpers look for event kind 3; this trace does not establish how the real queue shares their work. |
| `FUN_00558a10` | ECX game, EDX item: a nonzero quest-item predicate returns deadline 0. Otherwise quality 4 returns current frame +30000; qualities 5 through 9 return +45000. Remaining category-4 items use +45000 only when gold stat 14 is strictly greater than 10000, otherwise +15000. Other items matching type `0x35` use +30000, the rest +15000. Results are absolute frames with 32-bit wrap, not elapsed durations. |
| `FUN_00558b90` | ECX game, EDX act index: if game act slot `+0xbc+4*index` exists, walk its rooms and units. Cache the next unit before deleting the current one. For type-4 units whose item data `+0x24` expiry is nonzero and signed-less-than-or-equal to the captured game frame, remove room membership/path/collision when a room exists, clear unit flag 2, run `FUN_005538d0(game,FUN_00557fa0,item)` and destroy the unit through `FUN_00555600(game,item)`. No local act-index bounds check. |
| `FUN_00558c50` | EDX item: null/nonitem or an item without flag `0x4000000` does nothing. Otherwise clear that runeword flag, zero magic-prefix slot 0, find stat list state `0xab` with flags `0x40`, and if present detach then free it. Other prefix slots and unrelated item flags are preserved. |
| `FUN_00558cb0` | ECX game, EDX room, six stack arguments: reject null game, then require the descriptor's class in the Items table's range. Create a type-4 unit from descriptor coordinates/mode and call the item reader `FUN_0062e430`; store its result in the output argument. Nonzero reader result plus zero reader error succeeds: set item flag `0x80000`, clear `0x2000`, schedule both replenishment helpers, return item. Reader failure destroys the new unit and returns 0. Invalid class writes output 0; null game/allocation failure return 0 without that output write. `RET 0x18`. |
| `FUN_00559130` | ECX game, EDX room, stack coordinate pair and optional output creation block. Derive area monster level from level/difficulty/expansion, subtract 1 only above 1, then cap at 64. Pick a class with `FUN_00556240` using the room seed at room `+0x6c` when nonnull. Try coordinates `(x+2,y+3)` if `FUN_00463740` accepts them, else the original point; resolve placement through `FUN_0064e810` with masks `0x3e01/0x801`. On success build a 0x84-byte ground-mode-3 block with level, game, seed pointer, version and placement room; call `FUN_00558d90(game,&block,0)`, optionally copy the completed block to output. Placement failure leaves output untouched. |
| `FUN_00559830` | ECX game, EDX room, stack coordinates, two class-selection arguments and optional context. Use area level minus 1 above 1 only for a nonnull nontown level, otherwise 1; unlike `FUN_00559130`, no level-64 cap. Pass that level and a boolean for context first dword ==1 to `FUN_005560f0`, using the room seed. Apply the same offset-coordinate and placement search, then create a ground-mode-3 item through `FUN_00558d90`, with context in block `+0`. Placement failure returns without creation. |

## Original-code observations

Run with the existing emulator environment, from its directory:

```sh
D2_MPQ_DIR=/path/to/game-data python item_lifecycle.py
```

`tools/emu/item_lifecycle.py` runs the two scheduling functions on 48
synthetic cases, expiry on 288, and runeword cleanup on 8. Rates include
zero, negative, nondividing positive, and values at/above 2500; frames
include wraparound. Expiry covers quest precedence, quality boundaries,
both sides of the 10000-gold threshold, and type `0x35`. Cleanup checks
null/nonitem/unflagged cases, absent/present lists, prefix preservation
and detach-before-free order. All 344 cases passed on the binary below.

The scheduling oracle replaces stat lookup, event lookup and event queueing
with argument-recording hooks. The expiry oracle replaces quest, quality,
category, ancestry and stat queries with synthetic values, but executes
`FUN_0062beb0` for real and checks that it forwards the item and type `0x35`
to the ancestry predicate. The cleanup oracle
runs the real item flag and prefix accessors but hooks stat-list lookup,
detach and free. It checks original control flow and recorded calls, not
the real queue, tables, stat-list effects, gameplay, or d2d parity. Creation
and expiry deletion remain static traces. The runner rejects instruction
budget exhaustion rather than accepting a partial emulator result.

Artifact identity (local game data is never included):

- Stock executable SHA-256: `631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
- Cached Ghidra export SHA-256: `3ecaa0c989f37641ccc23bb57c6f45d1f08965fd13ad7329c7e88c9749542ccc`.
- Assembly listing SHA-256: `39aa9fe25018092a6b735e9d7f969adc68452c95a4345c0522ed2355a8cc91f5`.
