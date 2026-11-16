# UI/ui.cpp frontier helpers — game.exe 1.14d

## Five consecutive helpers

| Address | Name | Stock behavior |
|---|---|---|
| `FUN_00453aa0` | `ui_pentagram_sprite` | Returns the exact dword at `0x7a2878`; no arguments, branches, calls, or normalization. `FUN_0047dd70` reads its frame count while loading the options menu and `FUN_0047e3d0` draws its two animated selection marks. |
| `FUN_00453ab0` | `ui_npc_menu_marker_sprite` | Returns the exact dword at `0x7a2880`; no arguments, branches, calls, or normalization. `FUN_004b8100` uses it for the two animated markers around a selected NPC-menu line. |
| `FUN_00453ac0` | `ui_cursor_suppress_begin` | Writes zero to both `0x70fa9c` and `0x7a2790`. It touches no other state. The panel-toggle case in `FUN_0047ec50` brackets `FUN_0047e090` with this helper and `FUN_00453ad0`. |
| `FUN_00453ad0` | `ui_cursor_suppress_end` | Writes one to `0x70fa9c`, leaving `0x7a2790` unchanged. It has no calls or branches. |
| `FUN_00453b00` | `ui_mouse_move_refresh` | Calls `FUN_00468740` for the cursor y and keeps its low word as the high half of an LPARAM; calls `FUN_00468730` for x and keeps its low word as the low half; calls `FUN_004f59a0` for the game window; then calls imported `SendMessageA(window, WM_MOUSEMOVE=0x200, 0, MAKELPARAM(x,y))`. The signedness and upper halves of both coordinate results are discarded. It ignores the message result. Its eleven callers are item packet/action handlers (`FUN_004c2ad0` through `FUN_004c4c70`) and use it after inventory/equipment state changes so the unchanged mouse position is reprocessed. |

The entry boundaries are `00453aa0..00453aa5`, `00453ab0..00453ab5`,
`00453ac0..00453acc`, `00453ad0..00453ada`, and
`00453b00..00453b2b`. `tools/emu/ui_frontier_helpers.py` executes all five
entries with controlled globals, coordinate hooks, a window hook, and an
intercepted `SendMessageA`. Its mutation check proves that exact (not boolean)
getters, both reset stores, the enable value, and x/y packing are observed.

### Port assessment and remaining gap

d2d has no one-to-one raw sprite getters, cursor-suppression globals, or
synthetic `WM_MOUSEMOVE` sender. `GameMenu::draw` and the NPC-menu drawing code
use typed scene sprites directly. SDL input and d2d's owned inventory state are
re-evaluated in the frame loop, so the Windows-message refresh primitive is not
a useful standalone port. No contradictory d2d behavior was substantiated and
there is therefore no C++ behavior change or regression test. The precise
meanings of the two cursor globals outside this begin/end bracket remain not
traced.

Re-check from `tools/emu`:

```sh
.venv/bin/python ui_frontier_helpers.py --mutation-check
```
