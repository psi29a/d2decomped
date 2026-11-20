# The ring past the frontier — game.exe 1.14d

200 untouched functions on 2026-11-20, traced in two hundreds.

**The first hundred** (`python3 tools/gaps.py` at `d6eb7bc7`), in two
groups:

- **Reached from ported code:** the two `frontier = true` rows of
  `build/gaps/functions.tsv`, and 17 untouched functions whose address a
  verified, ported or oracle function takes (a callback, a column loader's
  parser, a window procedure). `game-calls.tsv` lists those as `data`, so
  the gap map doesn't count them as frontier.
- **Called by cited code:** 81 untouched functions that a cited function
  calls, from the item, collision, skills, act-end, unit-sound, automap,
  lighting, missile, party, drop and inventory notes. Of the 119 such
  functions there, the 19 largest (135 bytes and up) are left for later.

**The second hundred** (the gap map after the first): untouched
functions that cited code calls or takes the address of, where the citing
note is this file or one of the cursor, collision, lighting, skills, DRLG,
cels, COF draw order, player, character panel, item name, hireling,
packet, menu and UI notes; the 100 smallest of the 112 there up to 270
bytes, leaving out the Battle.net chat dialog (`0x43c000..0x44a000`).

Rows are in address order within each table. A reading says what the
decompile and, where noted, the disassembly show; callers are named with
the note that cites them. **Run** marks the 80 functions that
`tools/emu/frontier_ring.py` runs in stock game.exe and checks against the
reading (`cd tools/emu && uv run python frontier_ring.py`). Everything else
is read, not run: re-check with `tools/ghidra/fn.py <address>` and, for
the callers, `tools/ghidra/calls.py <caller> --to <address>`. The **d2d**
column is as in [game-frontier.md](game-frontier.md).

Conventions used below: `FUN_0040b3c0(line, 0)` frees a block (ECX) with
its source line; `FUN_00408a60` / `FUN_00681e09` is the assert-and-exit
pair, and "asserts line 0x..." names the line it reports; the data tables
block is `PTR_DAT_00744304`, with MonStats rows at `+0xa78` (0x1a8 bytes
each, count `+0xa80`) and Skills rows at `+0xb98` (0x23c bytes, count
`+0xba0`). A server game keeps its units in 128 hash chains per type,
chained through unit `+0xe4`: players at game `+0x1120`, monsters
`+0x1320`, objects `+0x1520`, items `+0x1720`, missiles `+0x1920` (the
table at `0x6e10e0`, read from game.exe), tiles `+0x1b20`. A D2Win
control's first dword is its type; `FUN_004f95c0` destroys one through a
per-type function, most ending in the shared `FUN_004f9310`. "The run's
settings" is the switch struct of switches.md, as `FUN_00405c30` (the
caller of `app_mode_run`) reads it.

## First hundred: reached from ported code

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0044d100` | `beta_screens_step` | Game.cpp; installed by `FUN_0044f360` (leaving the game). Shows the screens `DATA\GLOBAL\ui\betascreens\<name>` (records of 0xd bytes at `0x70f238`: start tick, cel `+4`, name `+9`; count `DAT_0070f024`, index `DAT_007a060d`) as six 256-pixel tiles at x 0 / 0x100 / 0x200, y 0x100 / 0x1e0 (`d2d_gamemenu_cel`), each for 10 s from its load (`GetTickCount`); after the last it frees the cels (`FUN_00478a00`), calls `d2d_set_quit_program` and returns 1 | no counterpart (the classic demo's exit screens) |
| `FUN_00451b30` | `game_wndproc` | The window procedure `FUN_0044b8a0` installs (`d2d_set_7d55d8`): `FUN_004514b0(lParam)`; unless `FUN_00451990()` holds the messages (WM_CLOSE always passes), the message table `FUN_00420b00` runs and its result is returned when it handled the message; else `DefWindowProcA` | no counterpart (SDL owns the window) |
| `FUN_004b2390` | `monster_overlay_clear` | A handler `FUN_004b6dd0` (packet 0x28, net-packets.md) takes the address of: a unit whose MonStats row (class at `+4`) has byte `+0xd` bit 0 gets `d2d_gfx_overlay_remove_row(0x48)` | not compared |
| `FUN_004b4070` | `npc_box_close` | The close callback `FUN_004b5ad0` passes to the box builder `FUN_004b7eb0`. 0 while `DAT_007c0c69`; otherwise, when the box `DAT_007c0d73` is up, destroys it (`FUN_004b8020`), clears it and `DAT_007c0c6b`, then `FUN_00487990`, `FUN_004b3c20`, `FUN_00455f20(0)` (the same steps as `npc_talk_end`, `FUN_004b3de0`); returns 1 | not compared |
| `FUN_004dc000` | `screen_fade_draw` | While `DAT_007c89cc` (set 500 ms ahead by `hold_500ms`, `FUN_004dbfe0`) is in the future, draws `d2d_renderer_rect_alpha(x0, 0, x1, DAT_00711470 − 0x2f, 0, time left × 255 / 500)` with x0 0 and x1 the width `DAT_0071146c`; automap scroll mode 1 (`d2d_automap_scroll_automap_scroll`) takes half the width off x1, mode 2 sets x0 to half the width. Past the deadline it clears `DAT_007c89cc`. Only caller the world draw `FUN_00476bc0` (stars.md) | not compared |
| `FUN_004f65e0` | `renderer_world_flag` | Returns `DAT_0072db44`, the copy `renderer_world_begin` (`FUN_004f65c0`) makes of `DAT_0072db40`. Only caller the world draw `FUN_00476bc0`, which keeps it in `DAT_007b57e4`. Run | not compared |
| `FUN_004fc3f0` | `textbox_list_key` | D2WinTextBox.cpp; the key handler of the text box `FUN_004fc7a0` builds (frontend-text-box.md). Asserts a control (line 0x501) of type 4 (0x502); needs flags `+8` bits 4 and 1, lines `+0x54` > 0, a selection `+0x60` ≠ −1 and `d2d_get_7d55cc` 0. Keys: 0x21 PgUp, 0x22 PgDn, 0x23 End, 0x24 Home, 0x26 Up, 0x28 Down move the selection and the top line `+0x58` within the visible lines (`FUN_004fbab0`); a top change with more lines than fit scrolls (`FUN_004fba30`), a selection change calls `+0x34`. Returns 0 | not compared |
| `FUN_00501690` | `font_glyph_find` | ECX a character code: a binary search of the current font's glyphs (font `DAT_0072dffc`; its entry at `0x841da8` + font × 0x14 holds the header `+8`, word count at header `+8`, and the 0xe-byte glyph records `+0xc`, sorted by their first word), at most 1000 steps; a miss returns the record at `+0x1b2` (record 31). D2WinFont.cpp. `d2d_set_font` (`FUN_00502ef0`) installs it as `DAT_00841da0` when `DAT_00841ec6` is set and the font isn't 0xd; `FUN_00502c60` too. Run | not compared |
| `FUN_00508310` | `scrollbar_clear` | Control slot 10 that the scrollbar builder `FUN_005084f0` (char-select.md) fills. Asserts the control's type is 5 (line 0x1b0); returns 0 when `+0x40`, `+0x44` and `+0x58` are all 0, else zeroes them and returns 1 | not compared |
| `FUN_0056e370` | `corpse_search_blastable` | The filter `FUN_0056e390` (the corpse search, monster-ai.md) passes on: 0 unless `d2d_pets_blast_takes(unit)`, else `FUN_0065aa40(unit, EDX)` | not compared |
| `FUN_005c6c50` | `werebeast_state_end` | SkillDruid.cpp. The end callback `FUN_005c6ec0` (skill 116's aurastate, skills.md) hands to `FUN_00625ce0`: `FUN_0056c740(1)`; with a unit (stack), `FUN_00625d00(unit, 0x15e, 0)`, then when `FUN_006439f0()` gives a record, `FUN_00644340(record, FUN_005c6bf0())` and `d2d_unit_set_state_checked(ECX, EDX, 0)` | not compared |
| `FUN_005d3cb0` | `area_walk_apply_copy` | An area-walk callback (`FUN_0056b7e0`, skills.md) from `FUN_005d3e80` (SkillMonst.cpp) and `FUN_005d7b60`: copies the 0x70 bytes context `+0xc` points to and calls `FUN_0056b9c0(EDX, &copy, 0)`; returns 1 | not compared |
| `FUN_005db150` | `area_walk_put_state` | An area-walk callback from `FUN_005db1c0` (SkillAma.cpp; Inner Sight / Slow Missiles, skills.md): `d2d_states_put_state`, returns 1 | not compared |
| `FUN_005dd020` | `unit_search_first_hit` | The visitor `FUN_005dd050` (`unit_search` kind 3) passes to `FUN_0058f380`: while the result (stack 2) is 0, calls the context's function (`+0`) on the unit with the context's `+0xc` and keeps a nonzero answer | not compared |
| `FUN_00631530` | `items_calc_column` | An Items.txt column parser (the column list `FUN_006315d0`, itemmode.md): an empty text or a failed compile stores −1 at record + field; otherwise the formula compiler `FUN_006c1ae0` (0x400-byte buffer, operand callbacks `LAB_00631430`, `LAB_00631490`, `FUN_006314a0`) and the bytes appended to the pool `0x96bcd0` (`d2d_datatbls_append_buffer`), whose offset is stored | not compared |
| `FUN_00651150` | `monstats_skill_mode_column` | A MonStats column parser (`FUN_00651210`'s list, missiles.md): for index < 8 it clears the mode byte `+0x180` + i and the word `+0x188` + 2i; when skill `+0x170` + 2i is set, a code of up to 3 characters is looked up in `0x96bc5c` (`FUN_006bd130`) for the mode byte; not found or 0xe stores 0xe and the code's index in `0x96c7ac` (`FUN_006bd3c0`) in the word | not compared |
| `FUN_006619a0` | `missiles_calc_column` | A Missiles.txt column parser (`FUN_00661b20`'s list, missiles.md), as `items_calc_column` with the operand callbacks `LAB_00661800`, `FUN_00661880` (the misscalc identifier, skills.md) and the pool `0x96bc98` | not compared |
| `FUN_00669080` | `cubemain_input_column` | A CubeMain column parser (`FUN_00669130`, cube.md): a non-empty text compiles into input slot i (`d2d_cube_compile_cube_input`, record + 0x14 + 8i) | `components/game/cube` reads the inputs its own way |
| `FUN_006690b0` | `cubemain_output_column` | As `cubemain_input_column` for output slot i (`d2d_cube_compile_cube_output`, record + 0x4c + 0x54 i) | as above |

## First hundred: called by cited code, 0x407000..0x4bdfff

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00407360` | `ptr_74d88c_if_set` | `&DAT_0074d88c` when it's nonzero, else 0. Callers `FUN_00457300` (gfx start), `FUN_00510020` (video-drivers.md), `FUN_005ffd40`, `FUN_006b42f0`, `FUN_006b4e80`. Run | not compared |
| `FUN_0043f8c0` | `chatdlg_close_all` | ChatDlg.cpp: `FUN_00443220`, `FUN_00443130`, `FUN_0043db20`, `FUN_0043ea20`, `FUN_00443e10`; `DAT_0077bd00` = 0; frees the controls `0x77bd9c` and `0x77bd90` (`FUN_004f95c0`); `FUN_00444e20` when `DAT_00798cc0` is 1; `DAT_0077bbe4` 3 becomes 0; hides `DAT_0077bc4c` (`d2d_control_set_hidden`). Callers the chat front end (`FUN_0043fe40`, `FUN_0043ffa0`, `FUN_00441b70` and 5 more) | no counterpart (Battle.net chat) |
| `FUN_0043fb50` | `chatdlg_reopen` | ChatDlg.cpp: `chatdlg_close_all`, hides `DAT_0077bc4c` again, four `FUN_005009a0`, makes `DAT_0077bd9c` from `0x70d3c0` (`FUN_004f93c0`), `FUN_004fd060`, keeps EDX in `DAT_0077bda4`; returns 1 | as above |
| `FUN_00449e50` | `set_798f7c` | Stores its stack argument in `DAT_00798f7c` (`RET 4`); its three callers pass 0. Run | not compared |
| `FUN_0044bf30` | `get_7a0650` | Returns `DAT_007a0650`. Caller `FUN_00467000` (collision.md). Run | not compared |
| `FUN_004549b0` | `ui_resize_refresh` | `FUN_004a02f0` then `FUN_004b69e0`. Caller `FUN_0044ba20`, the screen size set (automap.md) | not compared |
| `FUN_00457440` | `nop_457440` | Returns at once. Caller `FUN_0044ba20`. Run | n/a |
| `FUN_0045c460` | `monstats_count` | The MonStats row count (tables `+0xa80`). Caller `FUN_00477170` (unit-sounds.md). Run | not compared |
| `FUN_004723c0` | `client_end_free3` | `FUN_006bca90` three times (its arguments are in registers). Caller `FUN_0044c890`, the client's leave-game teardown (act1-end.md) | not compared |
| `FUN_00472bd0` | `client_cels_free` | Frees (`FUN_00478a00`) and clears each set slot of `0x7a89b0..0x7a89bc` and `0x7a893c..0x7a8948`. Caller `FUN_0044c890` | not compared |
| `FUN_00473cf0` | `nop_473cf0` | Returns at once. Caller `FUN_0044c890`. Run | n/a |
| `FUN_004758d0` | `list_7b5668_free` | Walks the list `DAT_007b5668` (next `+0x28`): frees a node's block `+0x30` when set (line 0x57d) and the node (0x57f); clears the head. Caller `FUN_0044c890` | not compared |
| `FUN_00475fd0` | `view_free` | View.cpp: `FUN_004dcf30()`, then frees a block (line 0x141). Caller `FUN_0044c890` | not compared |
| `FUN_00475ff0` | `flag_set_bit0` | ECX dword `|= 1`. Callers `FUN_00466de0` (the cursor's target, cursor-units.md) and `FUN_00467130` (npc-talk.md). Run | not compared |
| `FUN_00476fb0` | `palshift_free` | PalShift.cpp: frees the list `DAT_007b957c` (next `+0x1004`, line 0x84), then `DAT_007b9578` (0x8a) and `DAT_007bb384` (0x90), clearing each. Caller `FUN_0044c890` | not compared |
| `FUN_00477030` | `palshift_find` | PalShift.cpp: the node of `DAT_007b957c`'s list (next `+0x1004`) whose first dword is ECX, or null. Caller `FUN_00477170`. Run | not compared |
| `FUN_00498d50` | `panel_flag_set` | panel.cpp: ECX < 4 stores DL in byte `0x7befb0` + ECX; 4 and up do nothing. Callers the item handlers `FUN_004c4130`, `FUN_004c42a0`, `FUN_004c4c70` (collision.md, inventory.md). Run | not compared |
| `FUN_00498d60` | `panel_hover_reset` | panel.cpp: `DAT_0072235c`, `DAT_00722360`, `DAT_00722364` = −1 (the first is `belt_hover_item`'s box) and `DAT_007bef94`, `DAT_007befa8`, `DAT_007befac` = 0. Callers `FUN_004c42a0`, `FUN_004c4c70`. Run | not compared |
| `FUN_0049c760` | `waypoint_tab_clamp` | EAX a waypoint tab: one at or past `DAT_007224e4` becomes 0; tabs 1..4 fall back while their quest record isn't done (`FUN_004b32d0(id, 0)` then `FUN_0065c310`; records 0x1c for tab 4 → 3, 0x17 → 2, 0xf → 1, 7 → 0), checked from the tab down; the result goes to `DAT_007bf086`. Callers `FUN_0049cf90` (the panel's open, act1-end.md) and `FUN_0049d160`. Run | not compared |
| `FUN_0049da30` | `hover_unit_set` | UI/text.cpp: ECX unit null clears `DAT_007bf23a`; else `DAT_007bf23b` = its id (`+0xc`) and `DAT_007bf23a` = 1. Callers `FUN_00466de0`, `FUN_00467130`. Run | not compared |
| `FUN_0049da50` | `hover_unit_clear` | `DAT_007bf23a` = 0. Same callers. Run | not compared |
| `FUN_0049e8b0` | `text_hover_unit` | When `DAT_007bf20a`, `d2d_client_unit_find(DAT_007bf202, 1)`; else when `DAT_007bf1c0`, `d2d_client_unit_find(DAT_007bf206, 2)`; else 0 (tail calls, read from the disassembly). Same callers. Run | not compared |
| `FUN_004b2640` | `set_7bcc0c` | `DAT_007bcc0c` = 1 (Ghidra's `thunk_FUN_00487b70`). Caller `FUN_004c3c00` (items.md). Run | not compared |
| `FUN_004b2900` | `npc_menu_state3` | Destroys the box `DAT_007c0d67` when up (`FUN_004b8020`), sets `DAT_007c0c6b` = 3, then `d2d_inv_tab_last_used`, `FUN_00488f10`, `FUN_00466fe0`. Caller `FUN_004b4380` (net-join.md) | not compared |
| `FUN_004b2a00` | `npc_item_watch_a` | When `DAT_007c0d96`, byte `DAT_007c0d89` is 1 and ECX is `DAT_007c0d8e`, tail-calls `FUN_004b2940`. Caller `FUN_004c2c80` (items.md). Run | not compared |
| `FUN_004b2a20` | `npc_item_watch_b` | The same test and call as `npc_item_watch_a` (a second copy). Caller `FUN_004c3380`. Run | not compared |
| `FUN_004b2a40` | `npc_item_watch_c` | As `npc_item_watch_a` with `DAT_007c0d89` 0. Caller `FUN_004c2ad0`. Run | not compared |
| `FUN_004b2a60` | `npc_item_watch_d` | As `npc_item_watch_c`. Caller `FUN_004c4130`. Run | not compared |
| `FUN_004b2a80` | `cursor_item_gone` | ECX an item id: when the pointed-at record (`FUN_00467a10`) is an item (type 4) with that id, `DAT_007c0d29` (the talk target) is set and `d2d_client_unit_find` finds it, `FUN_00466fe0` then `FUN_00487990`. Caller `FUN_004c3c00` | not compared |
| `FUN_004b2e20` | `npc_menu_open` | npcmenu.cpp: `DAT_007c0c6b` ≠ 0. Caller `FUN_004c3c00`. Run | not compared |
| `FUN_004b3c00` | `npc_item_watch_e` | As `npc_item_watch_c`. Caller `FUN_004c2e90` (collision.md). Run | not compared |
| `FUN_004b8a10` | `trade_state_open` | 1 when `DAT_007c0e7c` is 1 or 2. Caller `FUN_00496b10` (party.md). Run | `apps/d2d/netgame.hpp` keeps the same state; not compared |
| `FUN_004b93a0` | `float_clamp` | (value, low, high) floats: max(value, low), then min(that, high), returned on the FPU. Caller `FUN_004b98f0` (unit-sounds.md), which clamps two coordinates to ±2000.0 (`0xc4fa0000`, `0x44fa0000`) | not compared |
| `FUN_004b9be0` | `sound_handle_voiced` | ECX a sound handle: walks `DAT_007c5458`'s nodes (next `+4`) to the one whose data `+8` is the handle; 1 when that data's `+0x28` is set and its `+4` nonzero, else 0 (the same list as `sound_handle_playing`, client-frontier.md). Caller `FUN_004cb220` (unit-sounds.md). Run | not compared |
| `FUN_004bd650` | `object_client_dispatch` | EDI object: its data (`d2d_object_data_8`) byte 0 picks a row of `0x6da8c0` (0x14 bytes, count `DAT_0072779c`); the row's function runs when set. Asserts no data (line 0x37a) or a row out of range (0x37b). Callers `FUN_004bd6b0` (the object's init, objects.md) and `FUN_004bd6d0` | not compared |
| `FUN_004bdfe0` | `unit_name_copy` | Copies at most 0x10 characters from EDX to ECX's data (`+0x14`) `+0x28` (`FUN_004135d0`). Caller `FUN_0045e9d0` (party.md) | not compared |

## First hundred: called by cited code, 0x4c0000..0x50ffff

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_004c5bd0` | `room_reach_test` | ECX room, EDX x, EAX y, EDI radius; the room's rect from `FUN_00619730`. Returns 0 only when x + r < left **and** x − r > left + width, or y + r < top **and** y − r > top + height, so for a radius ≥ 0 it always returns 1 (bugs.md 124). Caller the radius scan `FUN_004c5c40` (collision.md), which tests each unit's distance after it. Run | n/a (bugs.md 124) |
| `FUN_004c76d0` | `scale_down_percent` | ECX record, EDX percent: `+0x44` (256ths) becomes ((100 − percent) × (`+0x44` >> 8) / 100) << 8. Callers the client skill code `FUN_004f2160`, `FUN_004f4a30`, `FUN_004f4c20` (collision.md). Run | not compared |
| `FUN_004c7bd0` | `unit_size_plus4` | `FUN_00622870(ECX)` + 4. Callers `FUN_004f42e0`, `FUN_004f4450`, `FUN_004f4c20`. Run | not compared |
| `FUN_004c8350` | `skill_usable_here` | EAX skill id: 0 for no Skills row, no room (`d2d_unit_room`) or no room at the point (`FUN_00463740`); 0 when the row's byte `+5` bit 0 is clear and `FUN_0061ab00(room)` holds; else 1. Caller `FUN_004c83c0` (SkillsBar.cpp, skills.md) | not compared |
| `FUN_004c9d70` | `sequence_first_byte5` | ECX unit: 7 without a sequence (`FUN_006633b0`) or with a count (`FUN_006633f0`) of 0; otherwise its loop tests the first record's byte `+5` on every pass (the pointer never moves, read from the disassembly): 0 gives −1, nonzero gives 7 (bugs.md 125). Caller `FUN_004c9db0` (skills.md). Run | n/a (bugs.md 125) |
| `FUN_004fb130` | `palette_fade_start` | ECX 0: `DAT_00841b68` and `DAT_0072de50` = 0. Else both 1, the start tick in `DAT_00841b6c` and `DAT_007d5638` = 2. Both then `d2d_renderer_set_palette(0x81e668)`. Callers `FUN_0044c5e0`, `FUN_00460480` (3 calls), `FUN_004f90a0`, `FUN_004f9f20` | not compared |

## First hundred: called by cited code, 0x520000..0x5fffff (server)

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0052df40` | `server_game_unlock` | Game.cpp: `LeaveCriticalSection` on the lock at `DAT_00882d34` `+0x18`; asserts no game (line 0x1063) or no lock (0x1064). Callers `FUN_00534650` (collision.md), `FUN_00534680`, `FUN_00534730` | no counterpart (d2d's host has one thread) |
| `FUN_0052f6e0` | `server_game_current` | Game.cpp: enters and leaves the lock `0x882d18`, then `DAT_00882d34` = `FUN_0052e860()`. Same callers | as above |
| `FUN_00534880` | `player_data_free3` | Player.cpp: with the player's data (`d2d_player_data`), `FUN_00534840`, then for its three dwords at `+0x60`, `FUN_00572620` and zero. Caller `FUN_005349d0` (a unit's removal, combat.md) | not compared |
| `FUN_00534970` | `player_corpses_free` | Player.cpp: EAX player; for each node of its inventory's (`+0x60`) corpse list (`FUN_0063d570` / `FUN_0063d610`), the unit with that id (`d2d_inv_corpse_node_id`, `FUN_00552f60`) leaves its room (`FUN_0061a270`) and is freed (`FUN_00555600`). Caller `FUN_005349d0` | not compared |
| `FUN_00534a50` | `free_all_players` | Player.cpp: ECX game; frees (`FUN_00555600`) every unit of the 128 chains at `+0x1120`. Caller `FreeAllUnits` (`FUN_0053afd0`, collision.md). Run | not compared |
| `FUN_00537730` | `server_locks_delete` | `DeleteCriticalSection` on `0x8846a8` and `0x8846c0`; `DAT_008846d8` = 0. Caller `FUN_0052c030` (collision.md) | no counterpart |
| `FUN_0053ca90` | `scmd_send_5c` | SCmd.cpp: queues S→C 0x5c with the dword EDX (5 bytes, `d2d_scmd_queue`, ECX the client). Caller `FUN_0052c500` (items.md). Run | not compared |
| `FUN_0053dec0` | `scmd_send_8d` | SCmd.cpp: S→C 0x8d: EDX unit's id (`+0xc`, −1 for none) and a word from `FUN_00554630`, sent by `FUN_005538d0`. Callers `FUN_00540320` (adding a unit to a party, party.md) and `FUN_005a5970` | not compared |
| `FUN_0053dfb0` | `scmd_send_8e` | SCmd.cpp: S→C 0x8e, 10 bytes: the stack byte (arg 2), the dword EDX, the stack dword (arg 1); `RET 8`. Caller `FUN_0052c410` (items.md). Run | not compared |
| `FUN_0053ea50` | `scmd_send_inventory_items` | SCmd.cpp: EDX unit, stack flags; 0 when it has no inventory (`d2d_item_has_inventory`), the flags have 0x20 or `+0x60` is null. Otherwise each item of the inventory (`FUN_0063b2c0` / `FUN_0063dfa0` / `FUN_0063dfd0`) goes out as packet 0x9d (`FUN_0053cef0(item, 0x13, flags, 0)`) after `FUN_006287b0(&flags, 8, 1)`; returns 1. Callers `FUN_0053cef0` (net-packets.md) and `FUN_0053eae0` | not compared |
| `FUN_0053f580` | `player_arena_free` | Arena.cpp: asserts the player's data (line 0x52); frees its `+0x34` (`FUN_0040b480`, `.\GAME\Arena.cpp` line 0x57) and clears it. Caller `FUN_005349d0` | not compared |
| `FUN_00547dd0` | `monster_mode_count` | EDX unit (stack old and new modes): for a monster (type 1) not dead (`FUN_005541b0`) and not `FUN_00573540`, the record `ECX[FUN_00573520()]` (unless its byte `+0x2d8`) counts `+0x2cc`: −1 from mode 0 to 1 or 2, +1 to mode 0 from any but 4. Caller `FUN_00554340` (collision.md) | not compared |
| `FUN_00547e50` | `monster_count_2d0` | The same record's `+0x2d0` grows by 1 when the monster isn't `FUN_00573540` and its alignment (`FUN_006259b0`) is 0. Caller `FUN_005a6ff0` (drops.md) | not compared |
| `FUN_00552a40` | `free_all_objects` | As `free_all_players` for the chains at `+0x1520`. Caller `FUN_0053afd0`. Run | not compared |
| `FUN_00552db0` | `unit_hash_next` | ECX unit's `+0xe4`; asserts a null unit (line 0xde). 8 callers. Run | not compared |
| `FUN_005530f0` | `unit_hash_unlink` | ESI unit, EDX game: unlinks the unit from its chain (type 5 at `+0x1b20`, types 0..4 at `0x6e10e0`[type] + (id & 0x7f) × 4). Caller `FUN_00555580` (collision.md). Run | not compared |
| `FUN_00557f10` | `free_all_items` | As `free_all_players` for the chains at `+0x1720`, passing over items whose `+0x10` is 6. Caller `FUN_0053afd0`. Run | not compared |
| `FUN_005672d0` | `trade_items_takeable` | PlrTrade.cpp: EAX unit; 0 when an item of its inventory is on page 2 (`FUN_00628250`) and may not be taken (`FUN_0055cc90`), else 1. Caller `FUN_00567c70` (the trade fit test, inventory.md), twice | not compared |
| `FUN_0056dde0` | `skill_missile_args` | Skills.cpp: `FUN_0056dcc0(EDX, s3, s4, s1, s2, 0, s5, 0, s6)` (sN the stack arguments). Caller `FUN_005b0940` (missiles.md) | not compared |
| `FUN_00573640` | `free_all_monsters` | As `free_all_players` for the chains at `+0x1320`, writing each next back to the chain head. Caller `FUN_0053afd0`. Run | not compared |
| `FUN_00581ad0` | `unit_flags_send` | EDX unit, by its flags `+0xc4`: bit 0 `FUN_00581a20`, bit 10 `FUN_00571740`, bit 8 `d2d_unit_overhead_send`; `+0xc8` bit 0 `FUN_00597890(stack, 0)`; then `FUN_00571cd0` always. Caller `FUN_0053a500` (items.md) | not compared |
| `FUN_0059f840` | `free_all_missiles` | As `free_all_players` for the chains at `+0x1920`. Caller `FUN_0053afd0`. Run | not compared |
| `FUN_005a5ff0` | `party_packet_send` | `FUN_005538d0(EDX)`. Callers `FUN_00540180` (party.md), `FUN_005407a0` | not compared |
| `FUN_005fd9c0` | `skill_corpse_target` | `FUN_00553540(ECX, EDX)`; a corpse from the search `FUN_0056e390(stack 1, stack 2)`: none returns 0; else `FUN_00620c10(EDX, corpse)` and, when the out pointers (stack 3, 4) are given, its type and id; returns 1. Caller `FUN_005fda80` (skills.md) | not compared |
| `FUN_005fe070` | `client_end_6093e0` | `FUN_006093e0()`. Caller `FUN_0044c890` | not compared |
| `FUN_005ff3a0` | `spritecache_async_cancel` | SpriteCache.cpp: for each node of `DAT_0089db7c` (next `+0x14`) whose object's `+0xb0` is set while its async read isn't done (`d2d_async_data_done`), `FUN_0040ab00` and clear `+0xb0`. Caller `FUN_0044c890` | not compared |
| `FUN_005ffe30` | `spritecache_reset` | Arg nonzero and `DAT_0089db8c`: `FUN_005ffbe0`; zeroes `0x89db60` (0x50 bytes); arg nonzero: `FUN_006093e0`; zeroes `0x88db20` (0x34); `FUN_0060cd50`. Callers `FUN_00457420` (gfx.md), `FUN_004f9030`, `FUN_004f9060`, `FUN_004f90a0` | not compared |

## First hundred: called by cited code, 0x600000..

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00627540` | `stat_lists_drop_unstated` | A unit's stat block (`+0x5c`) with flag bit 31: walks its lists from `+0x3c` (next `+0x2c`); a list not of kind 4 (`+8`), without flags 0x181 and whose state (`+0x14`) the unit no longer has (`d2d_state_flag_test`) is removed (`FUN_00626c00`) unless it has bit 31, and the walk restarts. Callers `FUN_0057f330` (net-join.md), `FUN_005a6520` (drops.md) | not compared |
| `FUN_0063e8d0` | `monster_base_is_78` | (unit, flag), `RET 8`: a monster's MonStats base id (row `+2`; other units −1) compared with 78 when MonStats has more than 78 rows, else with −1; 1 when equal and the flag is 0. Caller `FUN_004af400` (the get-hit row, combat.md). Run | not compared |
| `FUN_0064a4e0` | `missile_word4_set` | A missile (type 3) with data (`+0x14`): word `+4` of it := arg 2. Callers `FUN_004e26c0`, `FUN_004f2790`, `FUN_004f3530` (lighting.md). Run | not compared |
| `FUN_0064a510` | `missile_word4_get` | That word, −1 for anything else. Caller `FUN_004d44b0` (missiles.md). Run | not compared |
| `FUN_0064a540` | `missile_word6_set` | As `missile_word4_set` for word `+6`. Same callers. Run | not compared |
| `FUN_0064a570` | `missile_word6_get` | Word `+6`, sign-extended, 0 for anything else. Caller `FUN_004d44b0`. Run | not compared |
| `FUN_0064c260` | `room_has_unit` | (room, unit), `RET 8`: asserts the unit (line 0x154); 0 for no room; else 1 when the unit is on the room's list (`FUN_00619f60`, next `+0xe8`). Caller `FUN_004c1a70` (items.md). Run | not compared |
| `FUN_0064fe10` | `aim_keep_last` | Calls the aim `FUN_0064fc60` (`d2d_missiles_aim_direction`) with its four arguments and pointers to the first and last (ECX, EDX), then returns the last as the aim left it; `RET 0x10`. Caller `FUN_00649ef0` (collision.md). Run | not compared |
| `FUN_006505e0` | `path_follow_unit` | (unit, target): sets path flag `+0x34` bit 3 unless both paths (`+0x2c`) share the subtile words `+2` and `+6` (then clears it), bit 0 when their `+0x1c` differ, then `FUN_0064fb90(target path +0, +4, +0x1c)`. Asserts lines 0x54e / 0x54f. Callers the client missile code `FUN_004d2610`, `FUN_004d3070`, `FUN_004d55c0` (missiles.md), `FUN_004f0ef0`, `FUN_005d2a10` | not compared |
| `FUN_006617b0` | `code4_pad` | Copies a code (ECX) into 4 bytes at EAX, padding with spaces after its end; longer codes keep 4. Caller the misscalc identifier `FUN_00661880` (skills.md). Run | not compared |
| `FUN_006b1a30` | `throw_hresult` | `__CxxThrowException(&hr, 0x7020b4)`; both callers (`FUN_006b1b20`, `FUN_006b2f60`, lighting.md) pass 0x80004005 | no counterpart (library) |
| `FUN_006c8a50` | `ddraw_get_14_b` | `*(DAT_007c9148 + 0x14)`, the same read as `ddraw_get_14` (`FUN_00512700`). Callers `FUN_006c87e0`, `FUN_006c9290` (lighting.md). Run | not compared |

## Second hundred: 0x405000..0x4fffff

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00406870` | `storm_options_setup` | `FUN_004164b0(0x6cca68)`, `FUN_00415f70(ECX)`, `FUN_004166b0(0, 0)`, `FUN_00415f90(EDX)`; keeps ECX in `DAT_0074c82c` (what `get_74c82c` returns, unnamed-frontier.md); then `FUN_0041bfc0` option 2 = 0x8000, options 1 and 3 = 1. Caller `FUN_00405c30` before the mode loop | not compared |
| `FUN_00407970` | `crash_app` | Writes 0 to address 0 (`XOR EAX,EAX; MOV [EAX],EAX`). `FUN_00405c30` calls it when the run's settings +4 (`-crash`, DEBUG/CRASHAPP) is 1. Run | n/a (Blizzard debug switch) |
| `FUN_004088c0` | `error_reentry_log` | ErrorManager.cpp: while `DAT_0074f0c8` (Crash.txt being written, `FUN_00407ff0`) is set, logs `***** ENCOUNTERED ERROR DURING ERROR HANDLING!!!` with `FUN_00408000(EDX, stack)` and returns 1; else 0. Caller `FUN_00408900` (the pool's log call, chat.md) | no counterpart |
| `FUN_004091b0` | `pool_size_class` | EAX a size: up to 0x10 gives 0, else the bit length of size − 1, less 4 (`BSR`); its `< 3` branch can't be reached. Caller the pool allocator `FUN_0040a080` (chat.md). Run | no counterpart (the C++ allocator) |
| `FUN_00409700` | `pool_block_take` | ESI pool: with no current block (`+0x28`), a new one (`FUN_004095e0`) becomes `+0x28` and `+0x2c`; with one whose count (`+8`) is under `+0x1c`, `FUN_00409240` tries it and a slot ≥ 0 returns with the block in the out pointer; otherwise a new block goes in front (its `+0x10` the old, the old's `+0xc` it). Caller `FUN_0040a080` | as above |
| `FUN_00409770` | `pool_big_alloc` | ECX pool, EDX size: `malloc(size + 4)`; a failure logs `(3) Pool Blocks overflowed at %d` (`+0x7a0`) and asserts line 0x388. Under the pool's lock (`+4`) the block goes at the head of chain (address % 0x3ff) of the table at `+0x7ac`; returns the block + 4. Caller `FUN_0040a080`, twice | as above |
| `FUN_0040a440` | `async_handles_close` | AsyncData.cpp: closes each handle of `DAT_0075af9c` (`DAT_0075af98` records of 0xc bytes), frees the array (line 0xae), zeroes both and `DAT_0075af94`. Callers `FUN_00405c30` (at exit) and `FUN_00407de0` (runtime-support.md) | no counterpart |
| `FUN_0040b320` | `nop_40b320` | Returns at once. Caller `FUN_00405c30` | n/a |
| `FUN_0041a930` | `sfile_close_checked` | SFILE.CPP: a null handle sets Storm's last error 0x57 (`FUN_0041c340`); else `FUN_0041a300(handle, 0)`. Caller `FUN_0040ab00` (from `spritecache_async_cancel`) with the record's `+0x3c` | no counterpart (Storm) |
| `FUN_0041aa80` | `sfile_async_signal` | SFILE.CPP: null sets 0x57; else `FUN_0041a8f0()` and, for a second argument > 0, `SetEvent(DAT_00779054)`. Caller `FUN_0040a960` with (`+0x3c`, 1) | as above |
| `FUN_0041b050` | `sfile_read5` | SFILE.CPP: `FUN_0041aad0` with its five arguments and two zeros. Callers `FUN_0041b0b0`, `FUN_0041b210`, `d2d_patch_extract`, `d2d_patch_apply` (bnclient.md) and one more | as above |
| `FUN_0042e5d0` | `words_byteswap` | ECX words, EDX count: swaps the two bytes of each word (`ROL 8`). Callers `FUN_00430cd0`, `FUN_00433810`. Run | not compared |
| `FUN_0044b220` | `set_799308` | Stores ECX in `DAT_00799308`. Caller `FUN_00441b70` (the chat front end). Run | not compared |
| `FUN_0044bba0` | `get_7a0460` | Returns `DAT_007a0460`; the client's game start `FUN_0044e200` sets `DAT_007a0620` to whether it's nonzero. Run | not compared |
| `FUN_0044dbe0` | `game_flag_file_check` | Game.cpp: once (`DAT_007a05fc` 0), when `FUN_00516e46(local, 1)` opens, reads one byte (`d2d_file_read_error_name(&c, 1)`), closes (`FUN_00516e84`), and a `'1'` sets `DAT_007a05fc`. The name in the local buffer isn't set here (not traced). Caller `FUN_0044e200` | not compared |
| `FUN_00451450` | `input_record_push` | Record.cpp: with more than 499 records (`PTR_DAT_0070f7a0` +8000) it flushes them (`FUN_00451400`) first; then appends 0x10 bytes: `FUN_0044da90()` and its three arguments. Caller `FUN_004514b0`, the first call of `game_wndproc` | not compared |
| `FUN_00466dd0` | `get_711f94` | Returns `DAT_00711f94`. Callers `FUN_004a7180`, `FUN_004a74a0` (char-panel.md). Run | not compared |
| `FUN_00467250` | `cursor_not_between` | ESI unit, stack unit: 0 when ESI's `+0x10` is set; else, on y when ESI's `FUN_00621b30` record has byte `+0x13c` = 1 and on x otherwise, 1 unless ESI's position lies strictly between the client player's and the stack unit's (`d2d_unit_pos`). Caller the cursor's unit pick `FUN_00467ac0` (cursor-units.md) | not compared |
| `FUN_00467310` | `cursor_between` | The reverse of `cursor_not_between` with the units' roles swapped: 1 when the stack unit's position lies strictly between the player's and ESI's. Caller `FUN_00467ac0` | not compared |
| `FUN_004673c0` | `unit_distance_sq` | EAX and EBX units: (Δx)² + (Δy)² of `FUN_00620650` / `FUN_006206b0` (the path's screen x, y; collision.md). Caller `FUN_00467ac0`. Run | not compared |
| `FUN_0046d750` | `clear_7a7470` | Zeroes the eight dwords `0x7a7470..0x7a748c`. Caller `FUN_0044e200`. Run | not compared |
| `FUN_00473ce0` | `nop_473ce0` | Returns at once. Caller `FUN_0044e200`. Run | n/a |
| `FUN_00474790` | `client_start_room` | With the client player, `d2d_unit_room` (the result unused); then `FUN_004744b0(0)`. Caller `FUN_0044e200` | not compared |
| `FUN_00476b10` | `view_create` | View.cpp: asserts `DAT_007b953c` (line 0x119), `DAT_007b9540` (0x11a) and `DAT_007b9544` (0x11b); allocates a block (0x11d) with an empty rect at `+4`, the three at `+0x2c..+0x34`, runs `FUN_004dddf0` and `FUN_004dcf10`, sets flag bit 0, clears `DAT_007b9530` and returns it. Caller `FUN_0044e200`, into `DAT_007a0640` when that is null | not compared |
| `FUN_0047f1d0` | `resolution_apply` | Reads `Diablo II\Resolution` (`FUN_00414f10`; missing gives 1, which isn't used), then the screen size set `FUN_0044ba20` (automap.md). Caller `FUN_0044e200` | not compared |
| `FUN_0047f210` | `minipanel_row_hit` | 1 when `FUN_004538d0(0x14)` and `FUN_004538d0(0x15)` hold and the cursor (`FUN_00468730`, `FUN_00468740`) is strictly inside the mini-panel's button row: x from W/2 + 0x38 (single player, `DAT_007bc978`) or W/2 + 0x23 over 7 or 8 buttons of 0x14, y between H − 0x4c and H − 0x32 (W, H `DAT_0071146c`, `DAT_00711470`). Callers `FUN_0049d010` (act1-end.md), `FUN_0049d160`. Run | apps/d2d draws the mini-panel (ui-module-frontier.md); not compared |
| `FUN_00481840` | `set_7bc9cc` | Stores ECX in `DAT_007bc9cc`. Caller `FUN_00457300` (gfx start). Run | not compared |
| `FUN_00488e10` | `tabs_select_by_id` | EAX a value: each of the dwords `0x7bc9e4`, `0x7bc9f8`, `0x7bca0c`, `0x7bca20` := whether the word 0xa bytes after it equals EAX. Caller `FUN_00488f10` (char-panel.md). Run | not compared |
| `FUN_00489da0` | `object_10b_note_id` | ECX an object of class 0x10b with a room in the client player's act (`d2d_level_act_index`): its id goes to `DAT_00721e54`. Taken by the panel opener `FUN_00489e00` (collision.md) | not compared |
| `FUN_0048a680` | `clear_7bcaf0` | Zeroes `DAT_007bcaf0`. Caller the open handler `FUN_00455720` (menu.md). Run | not compared |
| `FUN_004a70c0` | `stat_15c_max_held` | EAX unit: the largest value among its stat 0x15c layers (`FUN_006261d0(unit, 0x15c, list, 0x20)`) whose layer is 0 or names a skill one of its two hands' items holds (`d2d_rules_hand_index` 4 and 5, `FUN_00629bb0`). Caller `FUN_004a7180` (char-panel.md) | not compared |
| `FUN_004aa8f0` | `clear_7c0c3c` | Zeroes `DAT_007c0c3c`. Caller the S→C 0x21 handler `FUN_0045dcd0` (net-packets.md). Run | not compared |
| `FUN_004b8920` | `trade_reset` | `DAT_007c0e78` = 0, `DAT_007c0e68` = 0, byte `DAT_007c0e80` = 1. Caller `FUN_00455720`. Run | not compared |
| `FUN_004c5e30` | `radius_scan_first` | The radius scan `FUN_004c5c40` with the filter (stack 3) | 0xa783, the callback `LAB_004c5e20` and a result slot, which it returns. Callers the client skill code `FUN_004f4170` (filter 3) and `FUN_004f42e0` (0x20003) | not compared |
| `FUN_004e8fe0` | `charpanel_line_cb` | A callback the character panel's rows (`FUN_004e94e0`, `FUN_004e9510`, `FUN_004e95c0`; char-panel.md) give `FUN_004e93a0`: `FUN_004e8ec0(EDX, s1, s4, s5)` (sN the stack arguments) | not compared |
| `FUN_004e9610` | `charpanel_attack_cb` | The callback the attack rows (`FUN_004ea170`, `FUN_004ea240`, `FUN_004ea310`; char-panel.md) give `FUN_004e99f0`: `FUN_004e86f0(EDX, s1, s2, s4, s5, s6, s7, 0)`, then `FUN_004e89a0(ECX, EDX, s1, s2, s5)` | not compared |
| `FUN_004f5270` | `video_vsync_on` | `DAT_0072da58` = 1. `FUN_00405c30` calls it for the run's settings +0x14 (`-vsync`); `FUN_0044d760` too. Run | not compared |
| `FUN_004f5280` | `video_aspect_free` | `DAT_0072da7c` = 0. `FUN_00405c30` calls it for +9 (`-nofixaspect`) or a `Fixed Aspect Ratio` registry value other than 1. Run | not compared |
| `FUN_004f54f0` | `video_quality_toggle` | `DAT_0072da50` := !`DAT_0072da50`. `FUN_00405c30` calls it for +0xf (`-lq`). Run | not compared |
| `FUN_004f5990` | `set_7c933c` | Stores its stack argument in `DAT_007c933c`. `FUN_004f9f20` calls it with 1, then 0. Run | not compared |
| `FUN_004f6080` | `renderer_after_cutscene` | Asserts `DAT_007c8cc0` (line 0x2b9); `d2d_renderer_activate`; with `DAT_007c9340` 0 it changes the resolution to its argument (failing: `Failed to resize window after cutscenes... this is fatal!` and exit), else keeps it in `DAT_007c8cb8`; then `d2d_renderer_set_palette(0)`. Caller `FUN_004f9060` | not compared |
| `FUN_004fc960` | `textbox_destroy` | D2WinTextBox.cpp: asserts a control (line 0xcb) of type 4 (0xcc); destroys its scrollbar `+0x90` when set (`FUN_00507ee0`), `FUN_004fb6a0`, then `FUN_004f9310`. Caller `FUN_004f95c0` | not compared |
| `FUN_004fd0f0` | `control_destroy_4fd0f0` | `FUN_004f9310(ECX)`. Caller `FUN_004f95c0` | not compared |
| `FUN_004fd100` | `control_destroy_4fd100` | `FUN_004f9310(ECX)`. Caller `FUN_004f95c0` | not compared |
| `FUN_004fd820` | `control_destroy_4fd820` | `FUN_004f9310(ECX)`. Caller `FUN_004f95c0` | not compared |
| `FUN_004fdaa0` | `control_destroy_type1` | Asserts a control (line 0x6e) of type 1 (0x70), then `FUN_004f9310`. Caller `FUN_004f95c0` | not compared |

## Second hundred: 0x500000..0x51ffff

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00500300` | `control_destroy_500300` | `FUN_004f9310(ECX)`. Caller `FUN_004f95c0` | not compared |
| `FUN_00500930` | `control_destroy_type6` | Asserts type 6 (line 0x59), then `FUN_004f9310`. Caller `FUN_004f95c0` | not compared |
| `FUN_00500b20` | `control6_set_5c` | Asserts type 6 (line 0xd6); `+0x5c` := EDX. Callers the chat front end (`FUN_0043f630`, `FUN_0043fc20`, `FUN_00441b70`, `FUN_00445ff0`). Run | not compared |
| `FUN_00505100` | `comp_list_free` | D2Comp.cpp: each node of `DAT_0087e838` (next `+0xc4`) gets `FUN_005041b0(node, 0)` and is freed (line 0xd38); clears the head. Caller `FUN_00441b70` | not compared |
| `FUN_00506a40` | `control_destroy_type9` | Asserts type 9 (line 0x8c); `FUN_00506990`, then `FUN_004f9310`. Caller `FUN_004f95c0` | not compared |
| `FUN_00506e60` | `list_destroy` | D2WinList.cpp: asserts type 7 (line 0x80); frees its rows (`+0x64`, next `+0x220`; line 0x85), then `FUN_004f9310`. Caller `FUN_004f95c0` | not compared |
| `FUN_005072f0` | `list_click` | D2WinList.cpp, slot 9 of the list builder `FUN_005075e0`. Asserts type 7 (line 0x154). The row under the mouse (`DAT_007d55a4`, `DAT_007d55a8` strictly inside x `+0x208..+0x20c`, y `+0x210..+0x214`): the last row clicked (`+0x68`) again within 300 ms of `DAT_0087e970` runs its `+0x204`; else it becomes `+0x68`. Then keeps the tick and runs the control's `+0x34` when set | not compared |
| `FUN_005073b0` | `list_key` | D2WinList.cpp, slot 0xb: asserts type 7 (line 0x179); runs the first row (next `+0x220`) whose byte `+0x218` is the event's key (`+8`) and that has a `+0x204`; else 0 | not compared |
| `FUN_00507af0` | `account_list_destroy` | D2WinAccountList.cpp: asserts type 0xc (line 0x50); `FUN_00507850`, then `FUN_004f9310`. Caller `FUN_004f95c0` | no counterpart (Battle.net) |
| `FUN_00507db0` | `control_destroy_507db0` | `FUN_004f9310(ECX)`. Caller `FUN_004f95c0` | not compared |
| `FUN_00507ee0` | `control_destroy_507ee0` | `FUN_004f9310(ECX)`. Callers `FUN_004f95c0` and `textbox_destroy` | not compared |
| `FUN_00514060` | `shutdown_881764` | `DAT_00881764` = 0; `FUN_00515ce0`, `FUN_005147b0`, `FUN_00515120`, `FUN_00513af0`; releases the objects `DAT_00881748` and `DAT_00881768` (vtable `+8`) and clears them; deletes the lock `0x88174c` once (`DAT_0088177c`). Callers `FUN_00405c30` (twice) and `FUN_005140d0` | not compared |
| `FUN_00514d00` | `music_volume_set` | Writes `Diablo II\Music Volume` (`FUN_004150e0`) and `DAT_008817b4` from ECX; returns ECX. Caller `FUN_004f9f20`. Run | d2d keeps its volumes in its own settings |
| `FUN_0051a480` | `nop_51a480` | Returns at once. The Battle.net server list code (`FUN_0051bee0`, `FUN_0051ca90`) calls it with printf-style text (`Original servlist:`, `%d --> %s`): a log left empty. Run | n/a |

## Second hundred: 0x530000..0x5fffff (server and caches)

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_005316d0` | `plrsave_item_read` | PlrSave.cpp: 0 for no unit or one with `+0xc8` bit 15; with (s4, s5, s6) = (0, nonzero, 0) it needs `FUN_00564ca0()`; then `FUN_006313e0(unit, buffer, length, 1, 1, 0)`, asserted (line 0x75c). Callers `FUN_005317b0` (three) and `FUN_00569a00` (hireling.md) | not compared |
| `FUN_00533bf0` | `merc_exp_for_level` | PlrSave.cpp; ECX player, EDX (word) hireling, stack experience, `RET 4`. Counts the level up from 1 while the experience step (`FUN_00533a70`, ×0x12 first, then × (L + 1) × L × 3) is at most the experience and L < 99; then, for the hireling row (`FUN_006562f0(+0x70, hireling, level)`), returns `FUN_00663790(level, row +0x20)` when that is above the experience, else the experience. Caller `FUN_00533c70` (hireling.md) | not compared |
| `FUN_0053b470` | `scmd_send_12` | SCmd.cpp: queues 12 bytes (`d2d_scmd_queue`) laid out byte, byte, dword, byte, byte, dword from its arguments. Caller `FUN_00581a20` (from `unit_flags_send`) | not compared |
| `FUN_0053cc40` | `scmd_owned_monster` | SCmd.cpp: EAX a monster (type 1) with data whose `+0x40` isn't EDI and whose owner (`d2d_monster_owner_unit`) is the stack unit: `FUN_0053cc10()`; else 0. Caller `FUN_0053cd80` | not compared |
| `FUN_0053ddf0` | `scmd_send_0b` | SCmd.cpp: `FUN_005531c0(0xbae)`, then queues 0xb bytes. Taken by `FUN_0053de20` (collision.md) | not compared |
| `FUN_0053de90` | `scmd_send_07` | SCmd.cpp: `FUN_005531c0(0xbd8)`, then queues 7 bytes. Taken by `scmd_send_8d` (`FUN_0053dec0`) | not compared |
| `FUN_00547f70` | `unit_stop_send` | ECX unit: `d2d_scmd_unit_stop(0, id, 0x13, x, y, byte +0xb0, FUN_00621f20(unit))`; x, y from the path (`+0x2c`): `+0xc` / `+0x10` for types 2, 4 and 5, `d2d_path_get_x` / `_y` for the others. Caller `FUN_00580860` | not compared |
| `FUN_00548010` | `unit_pkt_11_send` | ECX unit, stack value: `d2d_scmd_pkt_11(type, id, x, y, value)`, x and y as in `unit_stop_send`. Callers the C→S 0x5f handler `FUN_0054cd50` (network.md, with 0) and `FUN_00580860` | not compared |
| `FUN_00549490` | `skill_point_check` | PlrMsg.cpp; ESI skill id, EDI unit: 2 for an id outside the Skills rows; 0 when `FUN_0056c700()`, `FUN_006447d0(unit, id)` and `FUN_00644920(unit, id)` all hold; else 3. Caller the C→S 0x3b handler `FUN_0054bd90` (add skill point, network.md) | not compared |
| `FUN_00566c80` | `trade_partner_send` | PlrTrade.cpp: when EDX isn't the stack unit, `FUN_005531c0(0x83b)` and `FUN_0053b3d0(0, the stack unit's id or −1)`. Taken by `d2d_trade_restore` (`FUN_005678f0`) | not compared |
| `FUN_005675e0` | `trade_saved_copy` | PlrTrade.cpp: the player data's `+0x5c` record (size, bytes) is copied to EDX when it fits the stack length, returning the size; else 0. Caller `FUN_00531bb0` (the save's `JM` items, hireling.md) | not compared |
| `FUN_0056af31` | `inv_nth_item_id_tail` | PlrSave2.cpp. Not a function's start: Ghidra's split of `d2d_inv_nth_item_id` (`FUN_0056af20`) at 0x56af31. Walks the inventory (EAX) to item n (the caller's EBP + 8, from 1) and returns its id, −1 past the end | not compared |
| `FUN_0056c2d0` | `skill_target_flag` | Skills.cpp: EDX unit's flags `+0xc4` |= 0x40; `FUN_00553540(ECX, EDX)` none gives 0; else `FUN_0057d4f0(record, 0)` and 1. Callers `FUN_0056f070`, `FUN_0056f115` (combat.md) | not compared |
| `FUN_00573ae0` | `monster_data_any` | ECX: 1 when a byte of the monster data's `+4..+0x13` is nonzero, else 0. Anything but a monster with data reads address 0 (`XOR EAX,EAX`, then `CMP byte [EAX]`). Caller the S→C 0xac builder `FUN_0053e2e0` (net-packets.md). Run | not compared |
| `FUN_005a0380` | `monster_data_byte1c` | ECX: a monster's data byte `+0x1c` ≠ 0; false for anything else. Caller `FUN_0053e2e0`. Run | not compared |
| `FUN_005a5900` | `party_relation_send` | PartyScreen.cpp; ESI and EDI units (id or −1) differ: when `FUN_00554630()` is −1, `FUN_0055b3a0(2)` if `FUN_0055b3f0()` is 4, else `FUN_0055b3a0(0)` and `(1)`; otherwise `FUN_0055b3a0(1)` twice. Caller `FUN_005a5970` (twice) | not compared |
| `FUN_005a5cb0` | `party_relation_send2` | PartyScreen.cpp: EDX the stack unit: `FUN_0055b3a0(1)`, `FUN_0053da90()`; else `FUN_0055b3a0(0)`, `FUN_0055b3a0(FUN_00554630() ≠ −1)` and `FUN_0053da90()` twice. Taken by `party_packet_send` (`FUN_005a5ff0`) | not compared |
| `FUN_005fde10` | `lru_cache_end` | When `DAT_0088cb14`: a nonzero argument runs `FUN_006093e0`; zeroes `0x88cae0` (0x34 bytes) and `DAT_0088cb18`. Then `FUN_006006b0`. Caller `FUN_00457420` (gfx end) | not compared |
| `FUN_005fe120` | `lru_cache_start` | `DAT_0088cb18` = stack 2; zeroes `0x88cae0` and sets up the LRU cache (`lru_init`) with limit stack 2, the evict callback `LAB_005fde50` and 0x1000 entries; `DAT_0088cb14` = 1. Caller `FUN_00457300` (gfx start, 0x600000) | not compared |
| `FUN_005ff3f0` | `spritecache_node_take` | SpriteCache.cpp; EDI cache, under its lock (`+0x30`): when the count (`+0xc`) isn't 0x1400 and the bytes (`+8`) plus the stack size fit the limit (`+4`), it unlinks the free list's head (`+0x24`; next `+0x14`, prev `+0x18`, tail `+0x28`) unless that has flag `+0xc` bit 2, runs `FUN_005ff060(stack 2)`, asserts a node (line 0x993) and counts it and its size. Caller `FUN_005ffc40` (cof-draw-order.md) | not compared |

## Second hundred: 0x600000..

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00600b80` | `free8_6009c0` | `FUN_006009c0` eight times (its arguments are in registers). Caller `FUN_0044e200` | not compared |
| `FUN_00609020` | `lru_init` | LRUCache.cpp; ECX cache: `+0` EDX, `+4` stack 1 (the byte limit), `+0x28` stack 2 (the evict callback), `+0x14` stack 3 entries of 0x14 bytes, allocated (`FUN_0040b430`, line 0x30), zeroed and chained (next `+0xc`, prev `+0x10`) from `+0x20` to `+0x24`; `+8`, `+0xc`, `+0x18`, `+0x1c`, `+0x2c`, `+0x30` = 0. Callers `lru_cache_start`, `FUN_005ffd40`. Run | not compared |
| `FUN_006092d0` | `lru_evict` | LRUCache.cpp; ECX cache, EDX entry or null (then the used list's head `+0x18`): unlinks it (`FUN_00609180` for a given entry), takes its size (`+4`) off `+8`, runs the evict callback (`+0x28`) when its `+8` is set, frees its block (`+0`, line 200) and puts it at the free list's tail (`+0x24`); the count `+0xc` drops by 1. Callers `FUN_005fdf50`, `FUN_005fe080`, `FUN_006093e0`, `FUN_00609420` | not compared |
| `FUN_0060aaf0` | `gfxhash_clear` | GfxHash.cpp: zeroes 0x10000 bytes at ECX. Caller `FUN_005ffd40`. Run | not compared |
| `FUN_0060ac60` | `gfxhash_key` | GfxHash.cpp; ECX record: `FUN_0060ab80(EAX)` is stored at EDI; kinds (`+8`) 3 and 4, and 6 with `+4` bit 2, add the hash of the name `+0x2c` (`FUN_00413ce0(name, 1, 0)`; asserts it, line 0x2c), the others add `+0x18..+0x28`; the sum v folds to ((v >> 8 ^ v) >> 16 ^ v >> 8 ^ v) & 0x1fff. Callers `FUN_0060ace0`, `FUN_0060adc0` (cof-draw-order.md). Run | not compared |
| `FUN_0060aee0` | `celdatahash_clear` | CelDataHash.cpp: zeroes 0x1000 bytes at ECX. Caller `FUN_005ffd40`. Run | not compared |
| `FUN_0060b200` | `bits_read_lsb` | EDI a bit count (0 gives 0), EDX a reader (`+4` dword pointer, `+0xc` bit position): the next bits LSB first, masked by `0x6e6030` ((1 << n) − 1), across a dword boundary. Callers the DCC decoders `FUN_0060b280`, `FUN_0060b7b0`, `FUN_0060b9a0`, `FUN_0060bff0` (cels.md). Run | not compared |
| `FUN_0060b4b0` | `dcc_cell_grid` | ESI direction: `+0x28` = (width `+0x10` + 3) / 4, `+0x24` = that × ((height `+0x14` + 3) / 4); with `+0x1c` set, counts in `+0x20` the calls to `FUN_0060b2b0` before it returns 0. Caller `FUN_0060bff0` | not compared |
| `FUN_0060b530` | `dcc_frame_bounds` | ECX header, EAX frames (0x2c bytes, count header `+0xc`), EDI out, stack value: out = header, frames, min x, min y, width, height, the stack value, the count; x spans `+0xc` to `+0xc` + width (`+4`), y from `+0x10` − height (`+8`) (or `+0x10` when `+0x28`, bottom-up) for height. Caller `FUN_0060bff0`. Run | not compared |
| `FUN_0060cd30` | `findtiles_lock_init` | `InterlockedIncrement(0x8f03e4)`; the first one initialises the lock `0x8f03cc`. Caller `FUN_005ffd40` | not compared |
| `FUN_0060cd70` | `tile_hash_bucket` | ECX style, EDX sequence, stack orientation: returns (2·style − orientation + sequence) & 0x7f in EAX (the decompile shows void), drlg.md's tile hash bucket; its assert (line 0x1e) can't fire. `RET 4`. Callers `FUN_0060cfa0`, `FUN_0060d040` (drlg.md). Run | not compared |
| `FUN_0060cdb0` | `tile_hash_alloc` | FindTiles.cpp: allocates (line 0x2a; asserts 0x2b) and zeroes 0x200 bytes, the 128 buckets. Caller `FUN_0060a440` (drlg.md) | not compared |
| `FUN_0060cea0` | `tile_hash_note` | FindTiles.cpp; ESI record, EDI value: unless the value is on ESI `+0xc`'s list (next `+4`), a new node (line 0x6e; asserts 0x6f) holding it goes in front. Caller `FUN_0060cfa0` | not compared |
| `FUN_0060cf00` | `tile_hash_add` | FindTiles.cpp; EDI tile, EBX list head: pushes a 0x14-byte node (line 0x82; asserts 0x83) of the tile's `+0x18`, `+0x1c`, `+0x14`, a second node (line 0x8e; asserts 0x8f) holding the tile at `+0xc`, and the old head at `+0x10`. Caller `FUN_0060cfa0` | not compared |
| `FUN_006274f0` | `stats_apply_list` | (unit, list): for each 8-byte entry of the list's `+0x24` (count word `+0x28`), `FUN_00627030(unit, word +2, dword +4, word +0)`. Caller `FUN_004e60a0` (item-names.md) | not compared |
| `FUN_00647b20` | `skill_bonus_add` | (unit, skill, delta): the node of the unit's skill list (`+0xa8` → `+4`, next `+4`) for the skill with `+0x34` = −1; none and a delta ≥ 1 grants it (`d2d_skill_grant`, `FUN_006439b0(−1)` `+0x28` = 0, `d2d_skill_passive_refresh`). Then `+0x2c` += delta, held at ≥ 0. Caller `FUN_004c7990` (skills.md) | not compared |
| `FUN_00661210` | `set_field_0c` | (record, value): `+0xc` := value. Caller `d2d_unit_text_free` with 0. Run | not compared |
| `FUN_0067fb90` | `outdoor_bridge_cell` | EAX level: walks cells 1 to (`+0x14` → `+0x5c`) − 2 through `FUN_0067c570` for one that is 0x1c with bits 16..19 = 1; found: EBX and the index into the two out pointers; else −1, −1. Callers `FUN_0067fc20` (the river's bridge, drlg.md), `FUN_0067fc50` | not compared |
| `FUN_006809c0` | `outdoor_neighbours` | EAX index, ESI out: nine `FUN_0067c570` reads at EAX − 1, EAX and EAX + 1 (its other coordinate in registers the decompile drops) into ESI[0..8]. Caller `FUN_00680b10` (the neighbour bits, drlg.md) | not compared |
| `FUN_006b1b60` | `display_mode_matches` | (record, flags): when the record's `+4`, `+0xc`, `+0x10`, `+0x14`, `+0x18`, `+0x1c` equal `0x74c028`, `0x74c030..0x74c040`, the flags get 0x200; returns 1. Taken by `FUN_006b42f0` (chat.md). Run | not compared |
