# Inventory.cpp — game.exe 1.14d

The inventory record and its lists, from `.\INVENTORY\Inventory.cpp`
(`game-srcfiles.tsv`). The placement searches, the wielded weapon and the
belt are in [weapon-switch.md](weapon-switch.md), [items.md](items.md) and
`components/rules/rules.hpp` (`free_spot`); this page covers the rest of
the file. Re-check any row with `tools/ghidra/fn.py <address>`; register
arguments (EAX, ESI, EDI, EBX) are where the decompile reads them
(`unaff_` / `in_EAX`). "No caller" rows were byte-scanned for rel32
calls, jumps and raw pointers and are in [stubs.md](stubs.md).

## The record

The inventory (`FUN_0063e370` and the unit's at unit `+0x60`) is 0x40
bytes, `+0` magic `0x1020304` (every entry point checks it):

| Offset | What |
|---|---|
| `+0x04` | the memory pool it was allocated from |
| `+0x08` | the owner unit (`FUN_0063ac40` clears its `+0x60`) |
| `+0x0c` / `+0x10` | first / last item; items link through item data `+0x60` previous, `+0x64` next (`FUN_0063af20` writes the new item to the old tail's `+0x64` and the old tail to its own `+0x60`; `tools/emu/inventory_lists.py`) |
| `+0x14` | the grids: 16 bytes each, `+0` first item, `+4` last, `+8` width, `+9` height, `+0xc` cell array (width × height item pointers) |
| `+0x18` | grid count |
| `+0x1c` | the wielded weapon's id, -1 none ([weapon-switch.md](weapon-switch.md)) |
| `+0x20` | the item in the cursor hand |
| `+0x28` | item count (not counting the cursor item) |
| `+0x2c` / `+0x30` | changed-item list: 8-byte nodes, `+0` the item's unit id, `+4` next |
| `+0x34` / `+0x38` | corpse list: 16-byte nodes, `+0` flag, `+4` the corpse's unit id, `+8` a word, `+0xc` next ([combat.md](combat.md) "Saving") |
| `+0x3c` | count of corpse nodes with the flag set |

Item data (unit `+0x14`) holds its place: `+0x5c` the inventory, `+0x68`
its grid (1-based, 0 none), `+0x69`, `+0x6c` / `+0x70` previous / next in
that grid. The grid descriptors passed to `FUN_0063add0` are `0x74479c`
(body locations: the cells are body locations 0..10, 0..0xf for placing)
and `0x7447b4` (the belt, 16 cells).

## Functions

| Address | What |
|---|---|
| `FUN_00597a20` | InvMode.cpp, fastcall (game, unit): true when unit `+0xc8` bit 0x16 or 0x17 is set, or when any id on the inventory's changed list still finds a unit (`FUN_00552f60`). Callers `FUN_00568060` (cube) and `FUN_005689d0` |
| `FUN_0063a970` | Takes an item (EAX) out of a grid (arg 1): unlinks it (`+0x6c` / `+0x70`, the grid's first / last), clears its cells (arg 2 zero: the one cell at its x, y; else its width × height, `FUN_006286c0`, line 0x199), zeroes `+0x68`..`+0x70` |
| `FUN_0063aaf0` | Removes an item (fastcall) from its inventory: out of its grid (`FUN_0063a970`, the whole footprint unless grid 1), out of the cursor slot or the item list (`FUN_0063a810`, count `+0x28` − 1), clears `+0x5c`, `+0x68`, `+0x69`, item data `+0xc` = -1 (`FUN_00629ed0`), detaches its stats from the owner (`FUN_006277f0`), and clears the wielded id if it was this item |
| `FUN_0063ac40` | Frees an inventory: the cursor item and every item removed (`FUN_0063aaf0`), each grid's cells and the grid array freed, the owner's `+0x60` cleared, both node lists freed, then the record (lines 0x261..0x27f) |
| `FUN_0063af20` | Appends an item (fastcall) to an inventory's item list (ESI): tail `+0x10`, item data `+0x5c` = the inventory |
| `FUN_0063af70` | Appends an item (ECX) to a grid's list (ESI) when the item's inventory is arg 2 |
| `FUN_0063b2e0` | The last item (`+0x10`). No caller |
| `FUN_0063b300` | Does a w × h spot at x, y fit a grid (EAX): inside its width and height, then wholly free (`FUN_0063a8a0`) |
| `FUN_0063b5e0` | Picks the edge search for a grid: a player owner (unit type 0) `FUN_0063b490` (bottom row up), anything else `FUN_0063b540` (from the top) |
| `FUN_0063b790` | Same pick for the free-spot walk: a player `FUN_0063b620` (row by row), else `FUN_0063b6e0` (column by column) |
| `FUN_0063b7d0` | Byte-for-byte the same as `FUN_0063b790` |
| `FUN_0063b810` | Byte-for-byte the same as `FUN_0063b790` |
| `FUN_0063bcc0` | Client: with arg 1 non-null, places an item: `FUN_0063afd0(arg 3, arg 4, page + 2, arg 5)`. Five callers (`FUN_004c2970`, `FUN_004c3e00`, `FUN_004c4740`, `FUN_00560200`, `FUN_00561b00`) |
| `FUN_0063bcf0` | Empty: `ret 0x20` (eight stack arguments, nothing done) |
| `FUN_0063bd00` | Empty: `ret 4` (one stack argument); called at `0x4b381e` and `0x5790f5` |
| `FUN_0063be30` | Empty: `ret 8` (two stack arguments, nothing done). The decompile shows no arguments; the asm pops 8 |
| `FUN_0063be40` | (inventory, n): true when the item at body location 5, else 4, passes `FUN_0062a4e0` and `FUN_00628950(item, n)`. No caller |
| `FUN_0063c050` | (inventory, component): the worn item (body locations 0..10) drawn on that look component (Items `+0x115`, `FUN_00628660`); a weapon (`FUN_00629fe0` 2, 3 or 0xc) answers component 5 only when wielded and 6 only when not |
| `FUN_0063c110` | An item's x when it's 10 or less (its body location), else 0 |
| `FUN_0063c150` | An item's x (its column) |
| `FUN_0063c180` | Sets the cursor item: null removes the current one (`FUN_0063aaf0`); else `+0x20` = it and its `+0x5c` = the inventory |
| `FUN_0063c3b0` | (inventory, template item, after): the first item of the pack grid (grid 2; a player owner's size comes from Inventory.txt, `FUN_00621050` / `FUN_0065c1f0`) whose `FUN_0062b400` is 0x12, whose `FUN_00627f80` matches the template's and whose stat 0x46 (`FUN_00625480`) is below `FUN_006295b0`; it starts at `after` itself when given |
| `FUN_0063c4c0` | Puts an item on body location ECX (0..15): `FUN_0063afd0(loc, 0, 1, 0)` |
| `FUN_0063c550` | Empty: `ret 8` (two stack arguments); eight callers (`0x4c432f`, `0x4c457c`, `0x4c4ffa`, `0x55ec1c`, `0x55edaa`, `0x55ee71`, `0x560a07`, `0x56231b`) |
| `FUN_0063c7f0` | The item in belt slot n (0..15) |
| `FUN_0063c830` | (inventory, item, slot, &out): a beltable 1 × 1 item whose Items bytes `+0x11d` and `+0x130` are both 1 gets the belt slot's item in `out` and true (line 0xbc9) |
| `FUN_0063cab0` | True when the item at body location 3 is of ItemTypes row 3 (`FUN_00629bb0`). No caller |
| `FUN_0063cb70` | Removes every item, not the cursor's |
| `FUN_0063cbd0` | Frees the changed-unit list (line 0xcbb) and clears `+0x2c` / `+0x30` |
| `FUN_0063cc30` | Is this unit id on the changed list |
| `FUN_0063cc70` | Adds an item's id to the changed list unless there (line 0xd09; out of memory is fatal, 0xd0a). 40 callers, the server's item moves; `FUN_00597b00` walks the list |
| `FUN_0063cd80` | (inventory, ItemTypes row, after): the first pack-grid (grid 2, sized as `FUN_0063c3b0`) item of that row (`FUN_00629bb0`); it starts at the item after `after` (`+0x70`) when given |
| `FUN_0063ce60` | (inventory, item type, after): the next worn item (body location below 11) of that ItemTypes row, after `after` when given. No caller |
| `FUN_0063cf30` | Same, by item code (`FUN_00633640`, compared with unit `+4`). No caller |
| `FUN_0063d010` | `FUN_0063cf30`'s search over grid 2 (`mov eax, 2`, not grid 0), but still handed the body grid's size record `0x74479c` (13 × 1), so it finds nothing unless grid 2 is 13 × 1. No caller (`tools/emu/inventory_queries.py`) |
| `FUN_0063d0f0` | (inventory, set row): how many worn set items (quality 5, `FUN_00627e70`) are of that named row (`FUN_00629da0`). No caller |
| `FUN_0063d470` | (inventory, id, word, flag): appends a corpse node (line 0xfea), `+0` flag, `+4` id, `+8` word, `+0x3c` + 1 when flagged |
| `FUN_0063d4e0` | (inventory, id, flag): removes that corpse node, drops the count when flagged (line 0x1034). Callers `FUN_005394a0`, `FUN_0057f690`, `FUN_0057fb70` |
| `FUN_0063d590` | Frees the corpse list (line 0x1071). No caller |
| `FUN_0063d5f0` | The flagged corpse count (`+0x3c`) |
| `FUN_0063d630` | A corpse node's unit id (`+4`) |
| `FUN_0063d650` | A corpse node's `+8` |
| `FUN_0063d670` | A corpse node's flag (`+0`) |
| `FUN_0063db90` | (EAX a, b): b zero: 3 if a else 0; else 5 if a else 1. No caller |
| `FUN_0063e000` | An item's unit id (`+0xc`) |
| `FUN_0063e070` | The item if its inventory is arg 1, else 0 |
| `FUN_0063e0b0` | Empties an inventory: cursor cleared, wielded id -1, every item removed |
| `FUN_0063e100` | (EAX scratch inventory, EBX unit): copies the unit's pack grid (grid 2; its size from Inventory.txt through `FUN_00621050` / `FUN_0065c1f0`, so an empty scratch inventory gets the grid allocated by `FUN_0063add0`) into the scratch one when both are that size |
| `FUN_0063e1c0` | (EAX scratch inventory, stack unit, item): asks the free-spot search (`FUN_0063b850`, the unit selects the Inventory.txt row) and, when the spot is inside the pack grid, fills the item's footprint (line 0x16d5) with -1 |
| `FUN_0063e2e0` | (EAX unit, stack other unit, EBX scratch inventory): every item on the unit's item list (`+0x64` next) that is on page 2 (`FUN_00628250`) gets a spot in the scratch inventory (`FUN_0063e1c0`, with the other unit); false at the first that doesn't |
| `FUN_0063e370` | The trade fit test (PlrTrade.cpp `FUN_00567c70`): (pool, unit A, unit B, `&out`). Two scratch inventories are allocated from the pool and given copies of A's and B's pack grids (`FUN_0063e100`); A's page-2 items are then placed in B's copy, and B's in A's. True when both fit; else `*out` = 0 when A's don't fit in B's, 1 when B's don't fit in A's (left alone when `out` is null). Both scratch inventories are freed (`FUN_0063ac40`) (`tools/emu/inventory_trade.py`) |

## The look's token table (`FUN_0063d710`, `FUN_0063d900`)

[compcode.md](compcode.md) "The look from what's worn" writes a helm,
weapon or shield as "its graphic's index in this table". The table is at
`0x96cc68`, 255 entries of (code, ItemTypes row), built once
(`0x96d464`):

- entries 1..3 are `lit `, `med `, `hvy ` with type 3;
- then each Items row (`0x96ca58`, 0x1a8 bytes a row) of type 0x2d, 3,
  0x33 or 0x25 and not 0x4b (`FUN_00629b50`): its alternategfx (`+0x90`),
  else its code (`+0x80`), unless already listed, at the first entry from
  the next unused one that holds no code and whose type in the static
  table at `0x744ca8` (same layout: `lit `, `med `, `hvy ` type 1, then
  `cap ` 0x25, `skp ` 0x25, ...) doesn't share 0x2d or 0x32 with the
  item's. Past entry 0xfe it starts over at the next unused one.

`FUN_0063d900` (ESI alternategfx, EDI code) returns the first entry
holding either, 0 when none. `FUN_0063da70` (EAX item, unit, look,
tints) writes one item: its component (`FUN_00628660`), 5 for the
wielded hand's weapon and 6 for the other's (`FUN_0063c050`), the index
there (0xff in both arrays when not in the table), `FUN_0063d930`'s bow
fix-up (a crossbow's other hand to component 6, `lit ` for a bow's
quiver on 5), and the tint from `FUN_0062c100` + 1, 0xff when there is
none.
