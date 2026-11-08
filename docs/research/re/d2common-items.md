# D2COMMON map entries: CmnItems and inventory UI

The current gap map assigns 17 routines to `D2COMMON` through
`D2Common/CmnItems.h` source evidence. This map bucket does not mean the 17
entries export from one standalone library: several routines call or are
called by `UI/inv.cpp`, and the group includes client inventory input and
drawing. The stock image checked here is game.exe 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.

## Mapped routines

| Address | Stock behavior observed in Ghidra | d2d/re-check |
|---|---|---|
| `FUN_00483c50` | Placement highlight for the cell under the cursor, called by `FUN_00483ff0` with the unit in EDI, the colour in EBX, the grid index in EAX and the inventory and grid coordinates on the stack. Nothing when the colour's low byte is 0xff, there is no unit, or no cursor target (0x7bcbe4); then the item's width and height; then nothing if the target cell (0x721e4c, 0x721e50) is negative or the mouse is over the bottom bar (y at or below screen height − 0x27; on page 1 also left of half the screen width, on page 0x13 the same test on the second look). `FUN_0063b9d0` (collision) true draws the cell solid in mode 1; else `FUN_0063bb20` (occupancy, with the collided unit) decides: no collision draws mode 0 unless the collided unit is the Horadric Cube (item code `box `, checked through `FUN_006335f0`) or the call said so, in which case it draws the cube's own cells in mode 3. | `tools/emu/d2common_inventory_ui.py`, with the callees hooked. |
| `FUN_00483e10` | Returns true only for interaction modes 0, 1, 2, 3, and 4. | 20 direct stock cases in `tools/emu/d2common_item_ui.py`. |
| `FUN_00483e40` | Draws the socket-count markers beside a hovered, identified, socketed item. It walks socket children and spaces the markers using the item's height and the selected panel slot. | Called by inventory, weapon-switch, and hireling item drawing. |
| `FUN_00483f80` | Holds back three quest consumables: `ass` until Radament flag 9/bit 5, `xyz` until Golden Bird flag 0x14/bit 5, and `tr2` until Prison of Ice flag 0x25/bit 8 is set and bit 7 is clear. Other codes pass. | Existing check: `game::quest_item_held_back`; `tests/game/test_quest_item_held_back.cpp`. |
| `FUN_00483ff0` | Draws the inventory grid. It walks its item list, chooses tint from identification, cursor selection, socket and usability state, and draws socket markers for the hovered item. In interaction modes 0xb–0xe it dispatches the corresponding cell hit test and placement preview. | Current d2d panel drawing is in `apps/d2d/panels.cpp`; see [char-panel.md](char-panel.md). |
| `FUN_004844a0` | Body-slot highlight (called by `FUN_004845a0`; EAX = inventory). Nothing while the mouse is left of centre on page 1 or at/below screen height − 0x27; left of centre otherwise it zeroes the slot (0x7bcc20); with no item in hand or no pending flag (0x7bcbe8) nothing. Else: the item's size, then body-location fit (`FUN_0062ed50`) and equip requirements (`FUN_0062eaf0`) both true → valid; else the item in that slot (`FUN_0063bde0`) and the socket test (`FUN_004843e0`) decide. It draws the slot's 20-byte record at `0x7bcc58 + 20 * slot` with the verdict in EDI (`FUN_00483a70`). | `tools/emu/d2common_inventory_ui.py`, with the callees hooked. |
| `FUN_004845a0` | Draws the player inventory/equipment slots 0–10, including weapon-switch offsets, item cells and hover treatments, then runs the inventory click/transfer helper `FUN_004844a0`. | Current weapon-set display is in [weapon-switch.md](weapon-switch.md); inventory panel drawing is in `apps/d2d/panels.cpp`. |
| `FUN_00486bd0` | Converts mouse pixels to the top-left inventory cell for an item footprint. It divides by cell size, centers multi-cell items under the pointer, clamps negative top-left coordinates to zero, and uses the grid midpoint when the footprint matches a grid dimension. It does not reject an item extending past the far edge. Even item dimensions use a second viewport-coordinate sample before division. | Eight stock cases cover odd/even footprints, grid-width case and lower-edge clamp in `tools/emu/d2common_item_ui.py`. Item dimensions and viewport adjustment are controlled hooks. |
| `FUN_00486d10` | Action throttle. Game types 0 and 1 (single player, uncapped) pass without the time check. Otherwise a throttled trade (`FUN_004b9180`) is cancelled (`FUN_004b90b0`) and the action refused; else it passes only when more than 250 ms (unsigned) have passed since the tick stored at 0x7bca48, which it then updates (a second `GetTickCount`). | `tools/emu/d2common_inventory_ui.py`, with the callees hooked. |
| `FUN_00486da0` | The gold button hit test (EDI = x, ESI = y): true only with nothing in the cursor hand (`FUN_0063c1e0` on the player's inventory) and the point inside the inclusive rectangle `x = [screenW − panelX − 0xed, screenW − panelX − 0xd9]`, `y = [panelY + screenH − 0x57, panelY + screenH − 0x45]`. | `tools/emu/d2common_inventory_ui.py`. |
| `FUN_00486e10` | Tests the inclusive rectangle `x=[screenW-panelW-0x12e, screenW-panelW-0x10e]`, `y=[panelTop-0x60+screenTop, panelTop-0x40+screenTop]`. | Nine stock cases exercise the rectangle's four edges, adjacent outside pixels and an interior point in `tools/emu/d2common_item_ui.py`. |
| `FUN_00486e60` | Drop-on-ground click (EDI = x, ESI = mouse message, stack = y). Only at resolution mode 2, not on a belt slot (`FUN_00498dc0`), and not below screen height − 0x30; skipped when the border type is 1 and x is left of centre. A cursor item of type 6 is dropped (`FUN_00453ec0`). Skipped again if the border type is now 3 on page 0 with x left of centre. Otherwise it marks the message handled (+0x18 = 1), clears 0x7bcbe4 and 0x7bcbe8 and breaks the handler chain (`FUN_00420290`). (An earlier version of this row described a different function.) | `tools/emu/d2common_inventory_ui.py`. |
| `FUN_00486ef0` | Right-click in the inventory (stdcall message, x at +0xc, y at +0xe, both words). Clears 0x7bce90 and 0x7bce94; off a belt slot, a click in `x = [screenW − panelX − 0x12e, screenW − panelX − 0x10e]`, `y = [panelY + screenH − 0x60, panelY + screenH − 0x40]` sets UI flag 1 (`FUN_00455f20(1, 1, 0)`) and returns; else it reads the hand item. With the gold flag (0x7bce30) set on page 0 it clears it, and if the cursor is over the gold button (`FUN_00486da0`) and the dialog flag (0x7bce2c) is clear opens the gold dialog (`FUN_00454150(1)`). It clears queued clicks, and for a click inside the rectangle at 0x7bcaa0..0x7bcaac, off a belt slot, marks the message handled (+0x1c = 0, +0x18 = 1) and breaks the handler chain. (An earlier version of this row had the rectangles of `FUN_00486da0` and this function mixed up.) | `tools/emu/d2common_inventory_ui.py`; `FUN_00486da0` runs for real inside it. |
| `FUN_00487000` | Item under the cursor (EAX = grid layout: +0 columns, +1 rows, +4 left, +0xc top, +0x14 and +0x15 cell size; stack: unit, inventory, cursor item, mouse x, y, page). Without a cursor item it asks `FUN_0063bd10` for the unit at the cell, clears 0x7bcbe4, 0x7bcbf4 and 0x7bcbf8 when there is none, else sets 0x7bcbe4 and, for a unit other than the one in 0x7bcbf8, stores its pixel box (0x721e3c..0x721e48, the width centred in the cell) and the unit in 0x7bcbf4 and 0x7bcbf8. With a cursor item it centres the footprint on the pointer (an even width or height uses the overlay size, `FUN_004dbea0`, quartered), uses half the grid when the footprint is as wide or tall as the grid, steps back half the footprint, floors at 0, and gives up (writes nothing) if it would pass the far edge; otherwise it stores the cell (0x721e4c, 0x721e50) and clears the hover. The edge test is a signed 32-bit add, so a pointer far left of the grid, whose unsigned cell wraps to about 0x7ffffffd, passes it and is stored. | `tools/emu/d2common_inventory_ui.py`, with the callees hooked. |
| `FUN_0048b3f0` | In the hireling equipment panel, hit-tests each slot while an item is held and tints an eligible target differently from an ineligible one. | Existing behavior summary in [hireling.md](hireling.md). |
| `FUN_0048b540` | Draws hireling equipment items centered in the head, torso and arm boxes; a hovered identified socketed item also draws its socket markers. | Existing behavior summary in [hireling.md](hireling.md). |
| `FUN_0065f590` | For an item unit and item record, computes socket count from the possible footprint, caps it by six and the item's socket count, then resolves the requested count from the current/rolled value or base item record. A successful result sets socket flag 0x800 and stat 0xc2. | Existing drop trace in [drops.md](drops.md); d2d's generation rule is in `components/rules/rules.hpp`. |

## Executable checks and limits

`tools/emu/d2common_inventory_ui.py` runs seven of the routines above (`FUN_00483c50`, `FUN_004844a0`, `FUN_00486d10`, `FUN_00486da0`, `FUN_00486e60`, `FUN_00486ef0`, `FUN_00487000`) in game.exe against a model of each on random globals, messages and layouts, with the routines they call hooked (their arguments and order are checked, not their bodies; `FUN_00486da0` runs for real inside `FUN_00486ef0`). It does not draw anything or move a real cursor.

```sh
cd tools/emu
uv run python d2common_inventory_ui.py
```

`tools/emu/d2common_item_ui.py` runs the stock mode predicate, panel rectangle,
and grid-coordinate helper on bounded synthetic inputs. It hooks only the item
dimension and viewport-adjustment helpers in the grid-coordinate cases. The
other routines above are static traces from the hash-checked Ghidra analysis
and their cited d2d callers/tests. Mouse event sequences, rendering, timing,
and equipment-panel state combinations are not modeled beyond the cases in the table.

```sh
cd tools/emu
uv run python d2common_item_ui.py
```
