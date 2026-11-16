# ITEMS: remaining ItemMode routines from 0x0055f000

Static inspection of the local stock 1.14d Ghidra export, with assembly
cross-checks for register arguments, return values and RNG. This covers the 27
functions classified as cited or untouched in this address range by the gap
map at source revision `1265c32d`. It establishes inspected behavior, not a
runtime match or that every original branch has been ported.

In the table, item mode 0 is stored, 1 equipped, 2 belted and 4 on the
cursor. An inventory pointer is unit `+0x60`; item page is item data `+0x45`.
`FUN_0063cc70` queues a changed item's ID in its inventory; `FUN_00621000`
marks the owning unit for update. Persistent flags set with `FUN_006280d0`
are distinct from the transient masks set with `FUN_00628170`.

| Address | Static trace |
|---|---|
| `FUN_0055f2a0` | Inventory in ECX, body slot on the stack: map paired slots 4↔5, 6↔7 and 11↔12 through `FUN_0055f240`, then look up the item in that slot through `FUN_0063bde0`. An unpaired slot returns the incoming EDX value: assembly shows the mapping helper changes EAX only, then this wrapper copies EDX to EAX. Do not assume the fallback is zero. |
| `FUN_0055f4f0` | Empty function: a `RET 4` consumes one stack argument without modifying game state. Its existence does not imply a missing item operation. |
| `FUN_0055f500` | For a nonnull owner with inventory, save its resource/selected-skill snapshot through `FUN_0055d9d0`, walk all inventory items, and for each item whose `FUN_00625820` reports an attached stat list, detach and relink its contribution through `FUN_006277f0` and `FUN_00627910`, preserving the active/passive result. Restore eligible skill selections with `FUN_0055da70` after the walk. |
| `FUN_0055f5d0` | Compare two items, one on the stack and one in EDI: all three 16-bit magic prefix IDs and suffix IDs must match, followed by the 16-bit automagic ID. Return false at the first mismatch and true only after all seven comparisons. This is affix equality, not whole-item equality. |
| `FUN_0055f6e0` | Add the supplied amount to item stat 0x46 (quantity), read back the result and send its stat update; then apply the same amount to the owner's associated scroll/tome skill count through `FUN_0055c070`. No capacity clamp occurs in this helper. |
| `FUN_0055f740` | Only handles an incoming item of type 0x16. Scan the owner's inventory for a type-0x12 item on page 0 with matching affixes (`FUN_0055f5d0`) and enough capacity (`FUN_006295b0`) for the incoming quantity, treating a quantity below 1 as 1. Return the first fitting item, or zero. A nonzero final argument is a probe; otherwise increase its quantity, send the stat update and increase its skill count. |
| `FUN_0055f9c0` | Resolve the item's scroll/tome skill through `FUN_0055bff0`; if found, look up the owner's permanent skill entry, set its count to zero and send skill-count packet 0x22 with the owner's ID (or -1 for no owner). The quantity and previous count are read but not used to preserve a count. |
| `FUN_0055fa40` | First inventory pass resets the associated scroll/tome skill counts for items in mode 0 and counts those on page 0. If any exist, a second pass calls `FUN_0055c110` with its add argument 1 for exactly those page-0 stored items, rebuilding their aggregate skill counts. Iteration saves the next node before processing. |
| `FUN_0055fb10` | Require owner and item, aborting through the assertion/error path otherwise. Set item mode to cursor (4), set transient item mask 0x100000, queue its ID in the owner's inventory and mark the owner for update; return 1. |
| `FUN_0055fb80` | Select an item code from two counted arrays in the record at EDX: count bytes at +0 and +0x7c, arrays at +4 and +0x80. Advance the supplied unit's RNG at +0x20 once for a positive combined count, using mask or modulo selection; zero total count selects index zero without advancing. Resolve the code and assert its record's 16-bit version +0xf6 does not exceed the saved ECX context's +0x78. If upgrade code +0x88 is neither zero nor `0x20202030`, use two sequential rolls below 100 against the difficulty record selected by context +0x6d: thresholds +8/+0x20, or +0xc/+0x24 when the last argument is nonzero. A first success chooses +0x88 and a second success chooses +0x8c. Return the selected code. |
| `FUN_0055fcc0` | For each inventory item with inventory-node kind 3, or kind 1 and a successful `FUN_0062ff70` equipment-contribution check, call `FUN_0062a360` (empty), remove its wield contribution, and remove it from the inventory, asserting the removed pointer matches. Kind 3 additionally obtains a value through `FUN_0063c110` and passes it to `FUN_0063be30`, which is empty in stock; no additional state change follows from that call. Rebuild set/equipment contributions and finish by resetting inventory linkage through `FUN_0063e0b0`. A missing item or failed removal is fatal. |
| `FUN_0055fde0` | Recompute equipment eligibility/contributions through `FUN_0055dbc0` with its final argument zero; mark the owner for update only when the caller's final argument is nonzero. |
| `FUN_0055fe00` | Set persistent item flag 0x10 (identified). If the item is stored (mode 0) and `FUN_0062ff70` says it contributes to the owner, refresh equipped effects through `FUN_0055c2c0`, re-evaluate equipment through `FUN_0055dbc0` and mark the owner for update. |
| `FUN_0055fe60` | Wrapper around `FUN_0055c2c0` with the final argument zero; no additional branch or state change. |
| `FUN_00560420` | Remove a stored item to the cursor, using an optional inventory override. Reject an occupied cursor unless the final override argument allows it. Missing item, inaccessible player page when `FUN_00535060` is false, or wrong mode sets the caller's error output to 1; success requires item type 4/mode 0 and exact-pointer inventory removal. Update set/equipment effects, obtain grid geometry and call `FUN_0063bcf0` (empty in stock), copy the old page to item data +0x47 and set current page to 0xff. Establish cursor linkage except the player-page-1 case, and set mode 4. When the notification argument equals 1, optionally set flag 0x20, invoke the page-2 callback, set transient mask 4, mark an item with socket children through flag 1, clear flag 0x4000, queue the item and owner updates. Return 1 on success, 0 for ordinary rejection; malformed context/inventory/removal takes the fatal path. |
| `FUN_00561b00` | Swap the mode-4 cursor item with a mode-0 inventory item. Reject page 2 with error 1 and reject putting the cube itself into page 3; cube class ID is lazily cached from code `box `. Remove the target with exact-pointer validation, set its page to 0xff, establish cursor linkage and mode 4, preserve its old page, set transient mask 0x40000 and queue updates. Place the old cursor item at the supplied coordinates/page, requiring both grid insertion and inventory linkage; insertion failure sets error 1. Give it mode 0 and the same transient mask, clear flag 0x4000 and refresh equipment effects as applicable. Success returns 1. This sequence mutates the target before testing insertion, so rejection is not a general transactional rollback. |
| `FUN_00561e70` | Build an outgoing flag mask containing 0x20, send the item action through `FUN_0053eed0`, then call `FUN_0055ed30`; that helper clears unit mask 2, asserts that `FUN_0063ad90` removes and returns exactly the item, calls empty stock helper `FUN_0063c550` with the item X coordinate, and calls `FUN_00557fd0`. The empty helper does not itself update belt bookkeeping. |
| `FUN_00562250` | Move a belted item (mode 2) to an empty cursor. Missing item or wrong mode sets error 1; non-item and occupied-cursor checks return 0. On success remove it through `FUN_0063ad90`, call empty stock helper `FUN_0063c550` with its belt index, establish cursor linkage, update scroll/tome count through `FUN_0055c6e0`, clear unit mask 2, refresh set contributions, set mode 4 and transient mask 0x800, queue updates, then compact the affected belt column through `FUN_0055edc0`. Null game/owner/inventory takes the fatal path. |
| `FUN_00562590` | Do nothing for an already identified item or missing owner inventory. Otherwise assert transient mask 0x100000 is clear, identify via `FUN_0055fe00`, set persistent flag 1, clear flag 0x4000, queue item/owner updates and call `FUN_00553380` to finish owner notification. |
| `FUN_005628c0` | Move an item into an owned, stored cube whose code is `box `. A restricted nonzero cube page causes owner notification and return 0. Require source item type 4 in mode 3 or 4; set its page to 3 and delegate placement to `FUN_00560200` with automatic coordinates. Return 1 after delegation; malformed/missing item/cube paths set error 1, while some type/permission rejections simply return 0. |
| `FUN_00562c40` | Replenishment worker with custom arguments: EDI=item, EAX=rate-stat ID, EBX=value-stat ID, stack=(game, maximum). A zero rate returns 0. Otherwise increment the value only when the incremented value is at most the maximum, send an owner stat update if its owner ID resolves and a client exists, and invoke `FUN_0055f900` if the item was broken and the new value is positive. Schedule event 3 at game frame + max(125, signed 2500/rate + 1) only after a successful increment. Return 1 for every nonzero rate, including an already full item with no scheduled successor. |
| `FUN_00562d30` | First try durability replenishment for an unbroken item with durability: stat 0xfc is the rate, stat 0x48 the value and `FUN_00625e00` the maximum. If that worker returns 1, stop even when it did not increment. Otherwise, for an item accepted by `FUN_006289f0`, replenish quantity with rate 0xfd, value 0x46 and maximum from `FUN_006295b0`. |
| `FUN_00562da0` | Dispatch through the function-pointer table at 0x006e117c indexed by the argument. `IsBadCodePtr` failure takes the assertion/error-exit path. There is no local index bounds check; the callback's input contract is inherited from callers. |
| `FUN_00563350` | If item-record byte +0x137 is zero, return 1 immediately. Otherwise require an inventory on the source item; duplicate each contained item using `FUN_0055a2a0`, set the copy to mode 4, preserve its x/y and pass the copy's ID to `FUN_00562660` to insert it into the destination socket context. Stop with 0 if insertion fails; an empty source inventory succeeds. Earlier copies are not undone on a later failure. Assembly confirms destination context remains in the saved EDX argument and that the duplicated item is the return value from `FUN_0055a2a0`, despite its void decompilation. |
| `FUN_00563470` | Given game, owner and item with owner inventory, choose by current item mode: mode 0 saves the current page into item data +0x47, sends a player-only item action with mask 0x20, then calls `FUN_0055df10` to remove it; mode 1 gets its equipped slot through `FUN_0063c150` and delegates unequipping to `FUN_00560cd0`; mode 4 delegates cursor removal to `FUN_0055eea0`. Other modes and invalid inputs do nothing. |
| `FUN_00563840` | Special handling when the removed item's item-record code is `box `: iterate the owner's inventory and evacuate every page-3 item. Send an item action with mask 0x20, require exact removal through `FUN_0063ad90`, call `FUN_0063bcf0` (empty in stock) with cube grid arguments, and set mode 4/page 0, then try automatic placement in page 0. Successful insertion/linking sets mode 0, transient mask 2 and queues owner/item changes, including equipment updates and any page callback. Failed placement restores page 0xff and searches for a nearby drop location through `FUN_00555da0`; if found, `FUN_00558aa0` drops the item there. Missing placement does not have an explicit cleanup success branch. The owner arrives in EBX, which is absent from the generated signature. |
| `FUN_00563fe0` | For a stored item whose record byte +0x139 is nonzero, resolve the replacement code at record +0xc8, build a creation descriptor preserving source level/position/seed-related fields, remove the original through `FUN_0055e000` and create the replacement through `FUN_00558d90`. If both quantity bytes +0x13a and +0x13b are nonzero, advance the owner's RNG with bound (second minus first), adding the first; otherwise quantity is zero. Put the result on page 0, first trying the descriptor's coordinates and then automatic placement, finally delegating to `FUN_00563c00` if both fail. Assembly returns 1 after creation and placement attempts, 0 for a disabled/nonstored source; invalid inputs, code lookup or creation failure is fatal. A full inventory therefore does not make the function return 0 after creation. |

The original binary routines still have assertions and custom calling
conventions; these descriptions do not authorize calling them with synthetic
invalid pointers. No game process was launched for this static pass. Stock `FUN_0063ad90` removes a matching item via `FUN_0063aaf0`; it is
not merely a membership check. Conversely, the grid/belt-facing helpers
`FUN_0063bcf0`, `FUN_0063c550` and `FUN_0063be30` are empty returns,
so their callers alone establish no additional grid/belt mutation. Packet
layout, the whole callback table, allocation-failure recovery and live
inventory synchronization remain outside these bounded traces.

## Live oracle (ten functions)

`tools/emu/items_mode_late_live.py` runs `FUN_0055fa40`, `FUN_0055fcc0`,
`FUN_00560420`, `FUN_00562250`, `FUN_00562590`, `FUN_005628c0`,
`FUN_00562c40`, `FUN_00563470`, `FUN_00563840` and `FUN_00563fe0` in
game.exe. Item-list helpers `FUN_0063dfd0`/`FUN_0063dfa0` run for real; other
callees are hooked and logged with register and stack arguments, and the
sequences are compared with models (1077 cases, ten negative controls).
Details read from the runs, in addition to the table:

- `FUN_0055fa40` (ECX game, EDX owner, no stack): pass one calls `FUN_0055f9c0`
  (ECX owner, EDX item) for each mode-0 item; pass two calls `FUN_0055c110`
  (EAX item, EDI owner, push 1) once per counted mode-0 page-0 item, in list
  order.
- `FUN_0055fcc0` (ECX game, EDX owner): kind 1 entries first test
  `FUN_0062ff70(item,owner)`; kind 3 entries skip that test. Removal order:
  `FUN_0062a360`, `FUN_0063d2b0(inventory,item)`, `FUN_0063ad90`, kind 3 only
  `FUN_0063c110` then `FUN_0063be30`, `FUN_0055c730(game,item,owner,0,1)`,
  `FUN_0055dbc0(game,owner,0)`. `FUN_0063e0b0(inventory)` runs once after the
  walk. Asserts: `0x19fd` (node without item), `0x1a0f` (unlink null),
  `0x1a10` (unlink returned another item). Null owner or owner without
  inventory does nothing.
- `FUN_00560420` (ECX game, EDX owner; stack id, error output, mode flag,
  flag-0x20 request, inventory override, allow-occupied; `ret 0x18`): asserts
  `0x906`, `0x907`, `0x910` (no inventory), `0x947` (unlink null), `0x948`
  (unlink mismatch). The player-page gate (`FUN_00535060` zero and nonzero
  page) gives output 1. After a successful unlink the cursor is set only for a
  player whose page is not 1; with the mode flag 1 a player page 2 calls
  `FUN_00567400`; `FUN_0055f590` nonzero sets flag 1.
- `FUN_00562250` asserts `0x1249`, `0x124a`, `0x124f`; a missing unit sets the
  error output, a non-item returns 0 with output 0, wrong mode sets output 1.
  Success order: `FUN_0063ad90`, `FUN_0063c550(inventory, item X)`, cursor set,
  `FUN_0055c6e0`, `FUN_0055c730`, mode 4, transient `0x800`, `FUN_00621000`,
  inventory queue, `FUN_0055edc0(owner, X)`.
- `FUN_00562590` asserts `0x1383` when transient `0x100000` is set. Success:
  `FUN_0055fe00(game, owner, item)`, flag 1, optional clear of `0x4000`,
  inventory queue, `FUN_00621000`, `FUN_00553380(owner, 6, owner)`.
- `FUN_005628c0` asserts `0x14a7`/`0x14a8`; error output 1 for a missing cube,
  a cube not in mode 0, a non-`box ` code, a missing source or a source whose
  mode is not 3 or 4; a restricted cube sends message `0x13` via
  `FUN_00553380` and returns 0; success sets page 3 and calls `FUN_00560200`
  with the source id, coordinates 0, 0, and returns 1.
- `FUN_00562c40` (EAX rate stat, EBX value stat, EDI item, stack game, max):
  zero rate returns 0 with no calls. An increment above the maximum does not
  write, notify or schedule but still returns 1. Otherwise: set stat, owner
  lookup (id -1 stops), client, `FUN_0053d130`, a broken item with a positive
  value calls `FUN_0055f900(game, owner unit, item)`, event 3 at
  frame + max(125, 2500/rate + 1) (C division, truncating toward zero).
- `FUN_00563470` ignores modes 2 and 3; mode 0 saves the page via
  `FUN_00628320`, sends the player-only action when `FUN_005531c0` gives a
  client, then `FUN_0055df10(game, owner, item, 0)`; mode 1 calls
  `FUN_00560cd0` with the slot from `FUN_0063c150` and flag 1; mode 4 calls
  `FUN_0055eea0`.
- `FUN_00563840` (stack game, item; EBX owner; `ret 8`): asserts `0x7ff` on a
  missing Items row and `0x80f`/`0x810` when unlink fails or returns another
  item. For each page-3 inventory item the page byte is set to 0xff on
  failure, and a ground-location search through `FUN_00555da0` is attempted;
  only when it finds a location is the item dropped through `FUN_00558aa0`
  (otherwise the sequence ends at `FUN_00555da0`); a successful placement through `FUN_0063b950` and
  `FUN_0063b210` clears the cursor unless the owner is a player and the page
  is 1, and calls `FUN_00568770` only for a player page 2.
- `FUN_00563fe0` (ECX game, EDX owner, stack item): asserts `0x1619`, `0x161a`,
  `0x161b`, `0x161c`, `0x1622` (no row), `0x1627` (replacement code lookup),
  `0x162d` (creation failed). Rows with byte `+0x139` zero or items not in
  mode 0 do nothing. Quantity is stat 0x46: 0 unless both `+0x13a` and
  `+0x13b` are nonzero, else `FUN_0045c3e0` (range `+0x13b - +0x13a`) plus
  `+0x13a`. Placement tries the descriptor coordinates, then (0,0) with the
  automatic flag, then `FUN_00563c00`.

d2d has no one-for-one counterpart that cites these ten; d2d is not
compared. Not traced: the bodies of the hooked callees and the grid, packet
and event subsystems they reach.
