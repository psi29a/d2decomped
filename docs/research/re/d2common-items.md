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
| `FUN_00483c50` | Handles the inventory cursor's placement preview. It requires an active cursor target and valid grid coordinates, suppresses the action in two mode/screen combinations, queries the candidate cell, and changes the cell tint. Its occupied-cell exception compares the item code with `box ` (the Horadric Cube). | The call is from `FUN_00483ff0`. Item code lookup is covered in [items.md](items.md). |
| `FUN_00483e10` | Returns true only for interaction modes 0, 1, 2, 3, and 4. | 20 direct stock cases in `tools/emu/d2common_item_ui.py`. |
| `FUN_00483e40` | Draws the socket-count markers beside a hovered, identified, socketed item. It walks socket children and spaces the markers using the item's height and the selected panel slot. | Called by inventory, weapon-switch, and hireling item drawing. |
| `FUN_00483f80` | Holds back three quest consumables: `ass` until Radament flag 9/bit 5, `xyz` until Golden Bird flag 0x14/bit 5, and `tr2` until Prison of Ice flag 0x25/bit 8 is set and bit 7 is clear. Other codes pass. | Existing check: `game::quest_item_held_back`; `tests/game/test_quest_item_held_back.cpp`. |
| `FUN_00483ff0` | Draws the inventory grid. It walks its item list, chooses tint from identification, cursor selection, socket and usability state, and draws socket markers for the hovered item. In interaction modes 0xb–0xe it dispatches the corresponding cell hit test and placement preview. | Current d2d panel drawing is in `apps/d2d/panels.cpp`; see [char-panel.md](char-panel.md). |
| `FUN_004844a0` | Checks panel bounds and cursor state, tests the selected cell and transfer conditions, then updates the inventory/cursor and redraws the cell or panel. The decompilation does not establish the user-facing name of each internal mode. | Called by `FUN_004845a0` in the recorded direct-call table. |
| `FUN_004845a0` | Draws the player inventory/equipment slots 0–10, including weapon-switch offsets, item cells and hover treatments, then runs the inventory click/transfer helper `FUN_004844a0`. | Current weapon-set display is in [weapon-switch.md](weapon-switch.md); inventory panel drawing is in `apps/d2d/panels.cpp`. |
| `FUN_00486bd0` | Converts mouse pixels to the top-left inventory cell for an item footprint. It divides by cell size, centers multi-cell items under the pointer, clamps negative top-left coordinates to zero, and uses the grid midpoint when the footprint matches a grid dimension. It does not reject an item extending past the far edge. Even item dimensions use a second viewport-coordinate sample before division. | Eight stock cases cover odd/even footprints, grid-width case and lower-edge clamp in `tools/emu/d2common_item_ui.py`. Item dimensions and viewport adjustment are controlled hooks. |
| `FUN_00486d10` | Allows the input path immediately in modes 0/1; otherwise a modal-state test can cancel it, or the function admits one action after more than 250 ms and records the current tick. | Uses `GetTickCount`; this document records static behavior only. |
| `FUN_00486da0` | Returns true only when the player inventory lookup at the current cursor is empty and the mouse lies in the small inclusive rectangle `x=[screenW-panelW-0x57, screenW-panelW-0x45]`, `y=[panelTop-0x57+screenTop, panelTop-0x45+screenTop]`. | Static trace; its input is the caller's ESI/EDI coordinates. |
| `FUN_00486e10` | Tests the inclusive rectangle `x=[screenW-panelW-0x12e, screenW-panelW-0x10e]`, `y=[panelTop-0x60+screenTop, panelTop-0x40+screenTop]`. | Nine stock cases exercise the rectangle's four edges, adjacent outside pixels and an interior point in `tools/emu/d2common_item_ui.py`. |
| `FUN_00486e60` | Under its panel, modal, y-position and input-mode guards, clears cursor-selection globals, marks the UI action active, and refreshes the interface. One mode also invokes a secondary action first. | Static trace; direct callers include `FUN_004912a0`, `FUN_00492510` and `FUN_004927c0`. |
| `FUN_00486ef0` | Processes a mouse event: clears two event accumulators, handles a special rectangle/modal case, updates a pending item-panel action when the pointer is in the tested panel region, and refreshes the interface. | No direct caller is present in the recovered call table; it may be reached indirectly. |
| `FUN_00487000` | Updates the item cursor from mouse movement. With no selected item it hit-tests and records the selected unit and screen bounds; with an item selected it converts the pointer to grid coordinates, centers and lower-clamps the footprint, rejects dimensions that exceed the grid, and updates the target cell. | Static trace; three direct callers are recorded at `FUN_004873a0`, `FUN_004889d0`, and `FUN_00488f10`. |
| `FUN_0048b3f0` | In the hireling equipment panel, hit-tests each slot while an item is held and tints an eligible target differently from an ineligible one. | Existing behavior summary in [hireling.md](hireling.md). |
| `FUN_0048b540` | Draws hireling equipment items centered in the head, torso and arm boxes; a hovered identified socketed item also draws its socket markers. | Existing behavior summary in [hireling.md](hireling.md). |
| `FUN_0065f590` | For an item unit and item record, computes socket count from the possible footprint, caps it by six and the item's socket count, then resolves the requested count from the current/rolled value or base item record. A successful result sets socket flag 0x800 and stat 0xc2. | Existing drop trace in [drops.md](drops.md); d2d's generation rule is in `components/rules/rules.hpp`. |

## Executable checks and limits

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
