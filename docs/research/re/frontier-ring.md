# The ring past the frontier — game.exe 1.14d

100 untouched functions on 2026-11-20 (`python3 tools/gaps.py` at
`d6eb7bc7`), in two groups:

- **Reached from ported code:** the two `frontier = true` rows of
  `build/gaps/functions.tsv`, and 17 untouched functions whose address a
  verified, ported or oracle function takes (a callback, a column loader's
  parser, a window procedure). `game-calls.tsv` lists those as `data`, so
  the gap map doesn't count them as frontier.
- **Called by cited code:** 81 untouched functions that a cited function
  calls, from the item, collision, skills, act-end, unit-sound, automap,
  lighting, missile, party, drop and inventory notes. Of the 119 such
  functions there, the 19 largest (135 bytes and up) are left for later.

Rows are in address order within each table. A reading says what the
decompile and, where noted, the disassembly show; callers are named with
the note that cites them. **Run** marks the 47 functions that
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
table at `0x6e10e0`, read from game.exe), tiles `+0x1b20`.

## Reached from ported code

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

## Called by cited code: 0x407000..0x4bdfff

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

## Called by cited code: 0x4c0000..0x50ffff

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_004c5bd0` | `room_reach_test` | ECX room, EDX x, EAX y, EDI radius; the room's rect from `FUN_00619730`. Returns 0 only when x + r < left **and** x − r > left + width, or y + r < top **and** y − r > top + height, so for a radius ≥ 0 it always returns 1 (bugs.md 124). Caller the radius scan `FUN_004c5c40` (collision.md), which tests each unit's distance after it. Run | n/a (bugs.md 124) |
| `FUN_004c76d0` | `scale_down_percent` | ECX record, EDX percent: `+0x44` (256ths) becomes ((100 − percent) × (`+0x44` >> 8) / 100) << 8. Callers the client skill code `FUN_004f2160`, `FUN_004f4a30`, `FUN_004f4c20` (collision.md). Run | not compared |
| `FUN_004c7bd0` | `unit_size_plus4` | `FUN_00622870(ECX)` + 4. Callers `FUN_004f42e0`, `FUN_004f4450`, `FUN_004f4c20`. Run | not compared |
| `FUN_004c8350` | `skill_usable_here` | EAX skill id: 0 for no Skills row, no room (`d2d_unit_room`) or no room at the point (`FUN_00463740`); 0 when the row's byte `+5` bit 0 is clear and `FUN_0061ab00(room)` holds; else 1. Caller `FUN_004c83c0` (SkillsBar.cpp, skills.md) | not compared |
| `FUN_004c9d70` | `sequence_first_byte5` | ECX unit: 7 without a sequence (`FUN_006633b0`) or with a count (`FUN_006633f0`) of 0; otherwise its loop tests the first record's byte `+5` on every pass (the pointer never moves, read from the disassembly): 0 gives −1, nonzero gives 7 (bugs.md 125). Caller `FUN_004c9db0` (skills.md). Run | n/a (bugs.md 125) |
| `FUN_004fb130` | `palette_fade_start` | ECX 0: `DAT_00841b68` and `DAT_0072de50` = 0. Else both 1, the start tick in `DAT_00841b6c` and `DAT_007d5638` = 2. Both then `d2d_renderer_set_palette(0x81e668)`. Callers `FUN_0044c5e0`, `FUN_00460480` (3 calls), `FUN_004f90a0`, `FUN_004f9f20` | not compared |

## Called by cited code: 0x520000..0x5fffff (server)

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

## Called by cited code: 0x600000..

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
