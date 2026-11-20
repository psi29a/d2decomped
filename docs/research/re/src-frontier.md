# SRC frontier — game.exe 1.14d

The 125 `SRC` rows of `build/gaps/functions.tsv` with `frontier = true` on
2026-11-18: untouched functions that a function d2d ports calls. `SRC`
holds the front end (`Src\MainMenus.cpp`, `Src\CharSel.cpp`), D2Win
(`Src\D2Win*.cpp`, `Src\D2Comp.cpp`), the sound layer
(`Src\D2Sound*.cpp`) and the sprite cache. Read from the decompile and,
where named, the data; no oracle was run for these rows. Re-check with
`tools/ghidra/fn.py <address>` and `tools/ghidra/calls.py <caller> --to
<address>`. The **d2d** column is as in [game-frontier.md](game-frontier.md).

## Memory pools, async data and the front end

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00409ab0` | `pool_free_block` | Fastcall (_, `*ptr`, file, line): the pool block holding the pointer (`FUN_004099d0`); a slot inside its range has its in-use bit cleared (masks `0x6ce268` / `0x6ce2e8`) and the count dropped under the block's lock (`FUN_00409a60`); outside, or no block, the general free `FUN_00409810`. Callers `FUN_0040a1f0` and the frees `FUN_0040b3c0`, `FUN_0040b480` | no counterpart: d2d owns its memory |
| `FUN_00409b80` | `pool_release_empty` | For the global pool (`0x74f104`, a null argument): each sub-pool's blocks with no slots in use are unlinked, removed from the sorted list (`d2d_pool_block_remove`), `VirtualFree`d and freed. Only caller the app start `FUN_0044b8a0` | no counterpart |
| `FUN_00409ff0` | `pool_set_first` | The global pool's first dword = the second argument, under its lock, when it has sub-pools. Callers `FUN_0044b8a0`, `FUN_0044f360` | no counterpart |
| `FUN_0040a4c0` | `async_data_finish` | Fastcall record not yet done (`+0x48` bit 0): its request event reset and released, `FUN_004068f0` on its handle, done flag set; a buffer at `+0x30` freed (line 0xd7), its pointer and size kept at `+0x3c`/`+0x40`, `+0x1c..+0x34` cleared. Callers `FUN_0040a550`, `d2d_async_data_create`, `FUN_0040a960`, `FUN_0040ab00` | no counterpart: d2d reads synchronously |
| `FUN_0040a7f0` | `async_data_done` | Fastcall record: 0 for null; a record whose `+0x48` isn't `0x4173796?` (`Asy`) and not done is finished when its event is signalled (`WaitForSingleObject(…, 0)`); returns the done bit. Callers the COF and sprite loaders (`FUN_005ff2a0`, `FUN_005ff300`, `FUN_006001f0`), `FUN_0040a960`, `FUN_0040ab00`, `FUN_00482b40` | no counterpart (as `async_data_finish`) |
| `FUN_0040dcc0` | `socket_send_locked` | Fastcall (connection, buffer, length): unless closed (`+0x2c`), `send` (ws2 ordinal 19) under the connection's lock; a short send or `SOCKET_ERROR` keeps `WSAGetLastError` (ordinal 111) at `+0x34`, state 4 unless it was WSAEWOULDBLOCK (0x2733). Callers `FUN_0051c5c0`, `FUN_0051ca90` (Battle.net news) | no counterpart: d2d has no Battle.net client |
| `FUN_0040f000` | `gamelogs_path` | `SHGetFolderPathA(CSIDL_PERSONAL)` + `Diablo II\GameLogs\` into the buffer (empty on failure). Callers `FUN_0040c720`, `FUN_0040d290`, `FUN_0040eb10`, `FUN_0040f8a0` | no counterpart |
| `FUN_00430b40` | `realm_count` | Returns `DAT_0077998c`, the realm list's length. Callers `FUN_004369f0`, `FUN_0043a0d0`, the character screen `FUN_0043b080` | no counterpart: d2d has no realms |
| `FUN_00430b50` | `realm_name` | Fastcall index: the first string of realm entry `0x779988[index]` (an entry must exist) | no counterpart (as `realm_count`) |
| `FUN_00431870` | `cinematic_play` | `FUN_0042f480`, `FUN_0042f3f0`, `FUN_004317d0`; with video on (`FUN_004f9050`) and `FUN_0040eb10` 0, `FUN_0042fd10`, the cutscene setup (`d2d_renderer_cutscene_setup`) and `FUN_004f9060(1)`; then `FUN_004317d0` and `FUN_00431600(movie)`. Callers `FUN_00434320`, `FUN_00434560` | not compared: d2d plays the movies itself (apps/d2d/frontend.hpp:168) |
| `FUN_00431e70` | `char_create_class_flags` | `AL` class: the record `DAT_007795d4`'s `+0x1ee` = class, and one of seven bytes `+0x85..+0x8b` set for it (0 `+0x85`, 1 `+0x87`, 2 `+0x88`, 3 `+0x86`, 4 `+0x89`, 5 `+0x8a`, 6 `+0x8b`), the rest cleared. Callers `FUN_00433bf0`, `FUN_00434a00` | not compared |
| `FUN_00433450` | `frontend_back` | `FUN_00433380(1)`. Callers the character screens (`FUN_00439840`, `FUN_0043b080`, `FUN_0043b6b0` and five more) | not compared |
| `FUN_00434060` | `realm_list_load` | From the Battle.net interface (`DAT_0077abf0` +0x40): the realm count at `+4` (more than 0x1ff are refused) and name/description pairs, each copied (lines 0x1192..0x11a4), then sorted (`qsort`, `FUN_00430b00`). Callers `FUN_0043a0d0`, `FUN_0043b080` | no counterpart (as `realm_count`) |
| `FUN_00434960` | `frontend_net_check` | Unless `FUN_00449b30`: game types 1, 2 (`FUN_00432480`) get `FUN_00449ad0` and 0; else `FUN_0042f6e0`, `FUN_0044a7a0`, the next screen (`FUN_004fa590`), 1. Only caller `FUN_0043b6b0` | not compared |
| `FUN_004349b0` | `frontend_mcp_check` | Fastcall (ECX, EDX). Unless `FUN_00449b30` is nonzero: `FUN_00432480(EDX, 0)` of 1 or 2 gets `FUN_00449ad0` (closes the realm connection `DAT_00798fb4` and its two events) and 0. Else `FUN_0042f6e0`, `FUN_0044a810(ECX)`, the next screen (`FUN_004fa590(0x433060)`), then 1 when `mcp_result` (`FUN_00449c00`) is 0, else 0 (`neg`, `sbb`, `add 1` at `0x4349f5`). As `frontend_net_check` with `FUN_0044a810` for `FUN_0044a7a0` and that last test. Only caller `FUN_0043b520`: on 0 it shows result 0x7b's own message or a plain string, then goes back (`frontend_back`) | no counterpart: d2d has no realms |
| `FUN_00435e00` | `frontend_ret1_00435580` | `FUN_00435580`, then 1. Only caller `FUN_00439840` | not compared |
| `FUN_00437a20` | `realm_known` | `EDI` name: 1 when it matches (case-insensitive, `FUN_00413590`) one of the realm names, else 0. Callers `FUN_0043a0d0`, `FUN_0043b080` | no counterpart (as `realm_count`) |
| `FUN_00437d20` | `char_select_strings_init` | Once (`DAT_00779da8`): seven strings converted, then the 0x80-word slots from `0x779df0` to `0x77abf0` filled (`FUN_005264f0`), 1 at the first failure. Only caller `FUN_00438ad0` | not compared |
| `FUN_00437f60` | `char_class_name` | `EAX` class 0..6: that class's string (other values a default) copied out. Callers `FUN_004380f0`, `FUN_00438560`, `FUN_00439210` | not compared |
| `FUN_0043a2b0` | `realm_pick` | For a Battle.net game (`DAT_007795ec` 1): loads the realms; none shows an error (`FUN_0043b9a0`); else the registry's `Diablo II\Preferred Realm` when it's in the list, else the gateway's realm (`BNGatewayAccess::Realm`), else the first, saved back to the registry; 0 only for the error. Only caller `FUN_0043ae30` | no counterpart (as `realm_count`) |
| `FUN_0043b7a0` | `bnclient_attach` | Fastcall screen: when `+0x225` is unset, the Battle.net interface (`FUN_00518c00`) into `DAT_0077abf0` and `+0x225`, its first virtual call, 1. Only caller `d2d_frontend_screen_run` | no counterpart (as `realm_count`) |
| `FUN_00449c00` | `mcp_result` | Returns `DAT_0079929c`, the result of the realm character server connection (`"clt->mcp"`): `FUN_0044a070` sets 0 as it connects and 2 when the connection isn't made; `FUN_00449fb0` sets 0xb when `FUN_0044a1d0` returns -1, and 2 when the connection drops or 60000 ms pass (`GetTickCount`) while it waits; `FUN_00449bf0` sets it to its argument. Callers `FUN_00432400`, `FUN_004349b0`, `FUN_00434a00`, `FUN_00435e10`, `FUN_0043b520` (0x7b has its own message), `FUN_00441500`, `FUN_00441a90` | no counterpart (as `realm_count`) |

## D2Win: controls, fonts, D2Comp

A D2Win control's `+0` is its kind; the setters assert it (an assert
path the rows below don't repeat). Kind 4 is the text list
(tcpip-menu.md "kind 4"), kind 1 the edit box (its text at `+0x5c`,
read by the name checks), kinds 2 and 8 as the rows say. Font calls use
the current font `DAT_0072dffc` (records `0x841db0`, 0x14 bytes; its
line height at `+10`) and the glyph lookup `DAT_00841da0`; `0xff` then a
digit in a string is a colour change.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_004f9740` | `control_set_hidden` | Fastcall (control, flag): `+8` bit 2 set or cleared. Called by 16 screens (title, TCP/IP, character create and select, chat) | not compared: d2d's screens track their own state |
| `FUN_004f9fb0` | `screenshot_save` | The first free `Screenshot%03d.jpg` (counter `DAT_0072dc40`, `FUN_00406960` probes), then the JPEG encoder (ordinals 2, 5 quality 8, 3) on a 0x4e40-byte state. Callers `FUN_004fa0c0`, the key handler `FUN_004fa7a0` | not compared: d2d's Print Screen (apps/d2d/session/update.cpp:1164) |
| `FUN_004fa410` | `d2win_stub_4fa410` | Returns. Callers `FUN_0042f480`, `FUN_0042fab0`, `FUN_00435230` | no counterpart (empty) |
| `FUN_004fa460` | `d2win_get_7d55e0` | Returns `DAT_007d55e0`. Callers `FUN_004ff620`, `FUN_004ffe50`, `FUN_00508370` | not compared |
| `FUN_004fa4b0` | `d2win_get_7d55d4` | Returns `DAT_007d55d4`. Only caller the button draw `FUN_00500db0` | not compared |
| `FUN_004fb590` | `d2win_stub_4fb590` | Returns. Callers `FUN_0043ae30`, `FUN_0043b080` | no counterpart (empty) |
| `FUN_004fb740` | `textlist_set_extra` | Kind 4: `+0xa0`, `+0xa4` = the two values; 1. Callers `FUN_00431c90`, `FUN_00435580`, `FUN_004380f0`, `FUN_00438560` | not compared |
| `FUN_004fbe90` | `textlist_count` | `EAX` kind 4: the number of lines on its list (`+0x50`, next `+0x1c`). Only caller `FUN_004fc9b0` | not compared |
| `FUN_004fbee0` | `text_fit_chars` | `EBX` wide text, width: the characters that fit before `FUN_005017d0`'s width reaches it; the text cut there; that count. Only caller `FUN_004fbf30` | not compared |
| `FUN_004fc6a0` | `textlist_clear` | Fastcall kind 4: `FUN_00507fd0`. Only caller `FUN_0043ae30` | not compared |
| `FUN_004fcf80` | `textlist_add_narrow` | Fastcall narrow string: widened (0x400 characters) and added to the list (`FUN_004fc9b0(4, 0, 0)`). Callers the title, TCP/IP and character screens | not compared |
| `FUN_004fd0b0` | `image_set_frame` | Kind 2: `+0xc`, `+0x10` = the two values; 1. Callers `FUN_00438560`, `FUN_004390a0`, `FUN_00439210` | not compared |
| `FUN_004fd830` | `scrollbar_reset` | Kind 8: `+0x30` = 0. Callers `FUN_0042ffe0`, `FUN_0043a0d0`, `FUN_0043b080` | not compared |
| `FUN_004fda50` | `scrollbar_set_default_cb` | Kind 8: `+0x20` = `FUN_004fd8f0`. Same callers | not compared |
| `FUN_004fdad0` | `editbox_set_filter` | Kind 1: `+0x268` = the value. Callers the TCP/IP, character create and chat screens | not compared |
| `FUN_004fdb00` | `editbox_set_26c` | Kind 1: `+0x26c` = the value. Same family of callers | not compared |
| `FUN_004fdc70` | `editbox_has_selection` | `EAX` kind 1: 0 when `+0x54` is -1 or equals `+0x58`, else 1. Callers `FUN_004fd9d0`, `FUN_004ffe50` | not compared |
| `FUN_004fdd50` | `editbox_set_48` | Kind 1: `+0x48` = the value | not compared |
| `FUN_004fdd90` | `editbox_text` | Kind 1: its text at `+0x5c`. Callers the name checks (`d2d_character_name_character_name_allows`, `FUN_00430620`) and nine screens | matches: d2d's name check reads the typed name (components/d2s/character_name.hpp:8) |
| `FUN_004fe710` | `editbox_click_at` | `*arg` kind 1: `FUN_004fe3b0` with the mouse (`DAT_007d55a4`, `DAT_007d55a8`) less the box's corner and padding. Callers `FUN_004ff620`, `FUN_005001d0` | not compared |
| `FUN_004ffbf0` | `editbox_mouse` | An event on a visible, enabled (`+8` bits 2 and 0) kind-1 box: outside it, dragging scrolls the caret by one; inside, a click within 300 ms of the last on the same box (`DAT_00841b78`) is a double click (`FUN_004fdd00`); else the caret goes to the character under the mouse and, without Shift, the selection starts there; 0x201 (left down) starts a drag. Then the box's callback `+0x34`. Callers `FUN_004ffe50`, `FUN_005001d0` | not compared |
| `FUN_004ffe10` | `editbox_set_narrow` | A narrow string widened (0x100 characters) and set as the text (`FUN_004ff5a0`). Callers the TCP/IP, character and chat screens | not compared |
| `FUN_00501840` | `text_width_widest_line` | `EAX` wide text: the widest line's width (glyph `+3` advances, `0xff` + digit colour codes skipped). Callers `d2d_common_glyph_top`, `FUN_00502520`, `d2d_gamemenu_cels`, `FUN_00503000` | not compared |
| `FUN_00501910` | `text_width_first_line` | `EAX` wide text: the width up to the first line break. Only caller `FUN_00501a80` | not compared |
| `FUN_005019c0` | `text_height` | `ESI` wide text: (line breaks) × the font's line height × `DAT_0072e004` / 10. Callers `FUN_00502520`, `d2d_gamemenu_cels`, `FUN_00503000` | not compared |
| `FUN_00501fe0` | `text_draw_clipped` | Draws wide text glyph by glyph (`d2d_renderer_cel_colour`, mode 5) from x, a line break moving up a line, colour codes switching the colour (a digit above 12 stops the colour), until x passes the limit. Only caller `FUN_004fbf30` | not compared |
| `FUN_00502480` | `hint_box_draw` | A text box kept on screen (clamped to the buffer size, 0x1f from the bottom): a rectangle (`d2d_renderer_rect_mode`) the text's width and one line high, then the text (`FUN_00501a80`). Only caller `FUN_004ffe50` | not compared |
| `FUN_005025c0` | `hint_box_draw_multi` | As `hint_box_draw` for several lines (`text_height`), 4 px in from the given width. Only caller `FUN_004fbf30` | not compared |
| `FUN_005028a0` | `text_list_free` | Fastcall list: each node (next `+4`) and its text freed (lines 0x579, 0x57a). Callers the join error text, the chat input and `FUN_0047b720` | no counterpart: d2d's strings own their memory |
| `FUN_00503220` | `compcode_free_entry` | `EAX` entry: nothing for null; else its buffers freed. Callers `FUN_005038d0`, `FUN_00503ba0` | no counterpart |
| `FUN_005032b0` | `cof_layer_ptr` | `EAX` COF: the layer record at `(+1 × direction + 9 + layer) × +0 + +1 + 0x1c`; null for a null COF. Callers `FUN_005038d0`, `FUN_00503ba0` | not compared |
| `FUN_005035f0` | `composite_find` | `EAX` list head: the node whose record has `+4` = the token and `+0xc` = `ESI`; null if none. Callers `FUN_00503000`, `FUN_005038d0`, `FUN_00503ba0` | not compared |
| `FUN_00503740` | `composite_layer_names` | Fills a layer's names from the unit's: token, mode, weapon class (tables `0x72e050`, `0x72e108`, `0x72e15c`), the piece code (default `lit `), from the COF's layer list (9 bytes a layer, layers above 15 refused), overlay `0x72e0b8`; missing parts become `xxx `. Callers `FUN_00503000`, `FUN_005038d0`, `FUN_00503ba0` | not compared: d2d composes the same names (components/compcode/compcode.hpp) |
| `FUN_00503920` | `comp_layer_byte_test` | `BL` layer under 0x40: false unless the table `0x6dca60` (0x40 a row, by `FUN_00517abb`) gives a byte above 0x40. Only caller `FUN_00503ba0` | not compared |
| `FUN_005042f0` | `composite_set_mode` | Fastcall (composite, mode): the mode's COF in its list (`+0xb8`); with one, `+0x20` mode, `+0xc` frames (`cof+1`), `+8` = 0, `+0x10` speed (`cof+0x18`), `+5` layers (`cof+2`), 1; else 0. Callers `FUN_00438ad0`, `d2d_compcode_weapon_class`, `FUN_005066c0` | not compared |
| `FUN_00504570` | `compcode_items_load` | Reads Weapons, Armor and Misc (`FUN_00504430`: version, alternategfx, spawnable, level, component, wclass, 2handedwclass, 1or2handed, Transform; 0x1a8 bytes a row) into one table (line 0xa34), the spawnable rows' indices (line 0xa4b) and a code lookup (`FUN_006bd040`, `FUN_006bd230`). Only caller `FUN_00506000` | matches: d2d builds the same table from the three files (components/compcode/compcode.hpp:6) |
| `FUN_00504d60` | `compcode_slot_remap` | `EBX` slot 1..4 and `ESI` appearance byte above 3: when its item (table `0x72e1e0`, 12 bytes) is-a type 3, 0x10, 0xf or 0x33 (`FUN_00504a20`), the byte becomes that code's index in `0x87d838`, else `byte % 3 + 1`; slot 0 likewise for type 0x25. Callers `d2d_compcode_weapon_class`, `FUN_005066c0` | not compared |
| `FUN_00505260` | `composite_set_frame` | Frame `+8` = the value (negative as 0) when under frames × 256. Only caller `FUN_00438ad0` | not compared |
| `FUN_005052f0` | `composite_set_18` | `+0x18` = the value (asserts the composite). Only caller `FUN_004380f0` | not compared |
| `FUN_00505360` | `composite_set_0` | `+0` = the value (asserts). Only caller `FUN_004380f0` | not compared |

## D2Win scroll bars, the video error log, the sound layer

Kind 5 below is the scroll bar (`+0x48` its range, `+0x4c` its
position). The sound layer is DirectSound behind `Src\D2SoundSmp.cpp`
and `Src\D2SoundFast.cpp`: a sample record's `+0` is its buffer
interface, `+0xc` its 3D buffer, `+0x48` an open stream, under the lock
`0x88174c`; the music thread is `FUN_00516250` (sound.md "Frontend
music").

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00507ef0` | `scrollbar_range` | Fastcall kind 5: `+0x48`. Only caller `FUN_004fc9b0` | not compared |
| `FUN_00507f20` | `scrollbar_set_range` | Kind 5: `+0x48` = the value (negative as 0); 1 | not compared |
| `FUN_00507f60` | `scrollbar_set_pos` | Kind 5: `+0x4c` = the value, at most the range; 1 | not compared |
| `FUN_00507fa0` | `scrollbar_pos` | Kind 5: `+0x4c` | not compared |
| `FUN_00507fd0` | `scrollbar_set_54` | Kind 5: `+0x54` = the value; 1. Callers `FUN_004fc6a0`, `FUN_004fc7a0` | not compared |
| `FUN_005080c0` | `scrollbar_set_step` | Kind 5: `+0x50` = the value (the page step). Only caller `FUN_004fc7a0` | not compared |
| `FUN_00508120` | `scrollbar_mouse` | A left press (0x201) on a visible, enabled bar with a range: in its column, on the thumb (10..0x14 px below its spot, height − 0x1e of travel) starts a drag; within 0xb px of the top an up arrow, within 10 of the bottom a down arrow; a drag sets the position from the mouse; else a page step (`+0x50`, else travel / 10) toward the mouse. Clamped to 0..range; then `FUN_004fba30` when linked, the bar's callbacks `+0x5c` and `+0x34`. Callers `FUN_00508370`, `FUN_005084f0` | not compared |
| `FUN_005103b0` | `ddraw_error_log` | Logs `***** DirectDraw error: %s - %s (0x%x)` with the name of a DirectDraw result (DD_OK and about 90 DDERR codes), except DDERR_SURFACEBUSY and DDERR_SURFACELOST. Only caller `FUN_005122e0` | no counterpart: d2d has no DirectDraw driver |
| `FUN_00512700` | `ddraw_get_14` | `*(DAT_007c9148 + 0x14)`. Only caller `FUN_005122e0` | no counterpart (as `ddraw_error_log`) |
| `FUN_00513ae0` | `launch_expansion` | Returns `DAT_00881790`, the launch config's expansion flag (sound.md). Callers `FUN_00514860`, `FUN_00514990` | not compared |
| `FUN_00513b60` | `sound_get_88176c` | Returns `DAT_0088176c`. Callers `FUN_004dfc20`, `FUN_004e0050` | not compared |
| `FUN_00513b70` | `sound_get_881770` | Returns `DAT_00881770`. Callers `FUN_004df630`, `FUN_004dfc20`, `FUN_004e0050` | not compared |
| `FUN_00513b90` | `sound_3d_mode` | Returns `DAT_00881774` (1 and 2 pick the 3D paths in `sample_set_pan` / `sample_set_far`). Callers `FUN_005157b0`, the mix `FUN_00515890` | not compared |
| `FUN_00514710` | `frontend_track_path` | `ESI` track: with the two list lengths equal, the LoD list's path (`0x72f8b8`) when the expansion flag is set, else the classic list's (`0x72f878`). Only caller `FUN_00514990` | matches for LoD: d2d plays the `0x72f8b8` list (apps/d2d/main.cpp:1327) |
| `FUN_00514840` | `music_playing` | With music on (`DAT_00881794`): 1 when the music sample has an open stream (`sample_streaming`). Callers `FUN_0044efa0`, `FUN_00516250` | not compared |
| `FUN_00514920` | `playlist_mode` | Returns `DAT_00881798` (sound.md). Only caller `FUN_00516250` | not compared |
| `FUN_00514c90` | `sound_get_8817ac` | Returns `DAT_008817ac`. Five sound callers | not compared |
| `FUN_00514ca0` | `sound_mixer_save` | Fastcall: saves the value as `Diablo II\Sound Mixer` (`FUN_004150e0`) and returns it. Callers `FUN_004dfc20`, `FUN_004e0050` | not compared |
| `FUN_00514cf0` | `music_volume` | Returns `DAT_008817b4`, the music volume (menu.md). Callers `FUN_004ba020`, `FUN_004dcaa0`, `FUN_004dfc20` | matches: d2d scales by music / 100 (apps/d2d/audio.hpp:112) |
| `FUN_00514d20` | `sound_get_8817b8` | Returns `DAT_008817b8`. Only caller `FUN_004dfc20` | not compared |
| `FUN_00514db0` | `ui_sounds_init` | Once sound is up (`FUN_00513b50`) and `DAT_008817fc` unset: its lock, `FUN_00515ce0`, eight samples (`FUN_00515530`) into `0x8817dc`. Only caller `d2d_frontend_screen_run` | not compared |
| `FUN_00514e10` | `ui_sound_load` | Reads a file (`FUN_00517079`, line 0x77); a WAV (`wav_header_parse`) with format 0x5622 (22050 Hz) becomes a 0x85-byte record on the list `DAT_00881800`: data, size, stereo flag, its name. Only caller `d2d_frontend_screen_run` | not compared |
| `FUN_00514ec0` | `ui_sound_play` | Fastcall name: the list's record with that name (case-sensitive), then the first of the eight samples with the same channel count that isn't busy (`FUN_00515710`): reset, loaded, mixed, played. Callers `FUN_00433bf0`, the button click `FUN_00501090` | not compared |
| `FUN_00515180` | `sample_stream_fill` | Fastcall (sample, source offset, buffer offset, bytes): locks the buffer (`+0x2c`) and copies from the sample data, wrapping to the loop start (`+0x44`) when looping (`+0x40`), the rest zeroed; unlocks (`+0x4c`); a finished one-shot sets `+0x2c`. Only caller `FUN_00516250` | no counterpart: d2d streams through OpenAL |
| `FUN_00515300` | `buffer_zero` | Locks the whole buffer, zeroes it, unlocks; 1, or 0 when the lock fails. Only caller `FUN_00516250` | no counterpart (as `sample_stream_fill`) |
| `FUN_005155d0` | `sample_load_wav` | Fastcall (sample, WAV bytes, size): parses it (`wav_header_parse`); data longer than the loop start becomes the sample's (`+0x14`, `+0x18`) and the first buffer fill; `+0x30` the end for a one-shot shorter than the buffer, else -1; 1. Callers `FUN_004e01b0`, `ui_sound_play` | no counterpart (as `sample_stream_fill`) |
| `FUN_005156a0` | `sample_play` | Fastcall: `Play(0, 0, looping)` (`+0x30`), `+0x1c` = 1; 1, or 0 on failure | no counterpart (as `sample_stream_fill`) |
| `FUN_00515950` | `sample_set_position` | With a 3D buffer (`+0x34` bit 2), its position (`+0x4c`) = x, y, z. Only caller `FUN_004dfc20` | no counterpart (as `sample_stream_fill`) |
| `FUN_005159b0` | `stream_samples_left` | Under the lock: the open stream's info (`sfile_handle_info`) shifted right 2, else 0. Only caller `FUN_004df800` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515a30` | `sample_set_loop` | `+0x40` = the value. Only caller `FUN_004e01b0` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515a60` | `sample_set_loop_start` | `+0x44` = the value. Only caller `FUN_004e01b0` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515a90` | `sample_set_far` | `+0x3c` = the value; in 3D mode 2 with a 3D buffer (and its bit 2, or the value 0): 1 − value through `FUN_005165f0`, then the buffer's property 7 (`0x6de470`). Only caller `FUN_004dfc20` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515b40` | `sample_set_pan` | With 3D on (`FUN_00513ba0`) and a 3D buffer: mode 1 sets property 1 (`0x6de490`) to -1 for a nonzero value, 0 else; otherwise properties 5 and 4 (`0x6de470`) from `FUN_005165f0`. Only caller `FUN_004e01b0` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515c30` | `sample_streaming` | Fastcall: `+0x48 != 0`. Only caller `FUN_00514990` | not compared |
| `FUN_00515d60` | `sound_flag_72f9b0` | `DAT_0072f9b0` when sound is up (`FUN_00513b50`), else 0. Only caller `FUN_00514990` | not compared |
| `FUN_00515d70` | `sample_open_stream` | When `FUN_004068e0`: opens the file as a stream (`FUN_0041b280`, flag 0x40000 when asked, from offset × 4); `+0x48`, `+0x1c` = 1, `+0x30` = -1; 1, else 0. Callers `FUN_004e01b0`, `FUN_00514990` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515e40` | `sample_close_stream` | Under the lock: an open stream closed (`FUN_0041a990`), the buffer stopped (`+0x48`), `FUN_004068f0`, `+0x48` and `+0x1c` = 0. Callers `FUN_00515ce0`, `FUN_00516250` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515ee0` | `sample_stop` | Under the lock: closes the stream when open, stops the buffer, `+0x1c` = 0. Only caller `FUN_004df800` | no counterpart (as `sample_stream_fill`) |
| `FUN_00515fd0` | `sample_restore` | Under the lock: when the buffer's status (`+0x24`) says lost (bit 2), `Restore` (`+0x50`) retried every 10 ms while DSERR_BUFFERLOST, then `sample_stop`; 1, or 0 when the status call fails. Only caller `FUN_00516250` | no counterpart (as `sample_stream_fill`) |
| `FUN_00516140` | `sample_reset` | With no open stream and sound up (`FUN_00513bb0` 0): frees its data (`FUN_00516070`, `FUN_005153c0`), clears the sample's fields, rewinds (`+0x34`), the 3D buffer's distances 100.0 and 2.0, volume 0 unless 3D, position 0, far 0, the mix (`FUN_005157b0`). Callers `FUN_004e01b0`, `FUN_00514990` | no counterpart (as `sample_stream_fill`) |
| `FUN_00516760` | `wav_header_parse` | A buffer of 0x20 bytes or more starting `RIFF`…`WAVE`: the `fmt ` chunk (16 bytes or more, `FUN_005166d0`) copied into a WAVEFORMATEX (cbSize 0), then the `data` chunk: its pointer, its size out; 0 otherwise. Only caller `FUN_004df630` (and `ui_sound_load`, `sample_load_wav`) | not compared: d2d decodes WAVs itself (apps/d2d/audio.hpp) |
| `FUN_00516eab` | `file_open_error_name` | A failed open's path for the message: the archive's (`FUN_00416230`, 0x104 characters) unless `FUN_00406930`. Callers `FUN_00516e84`, `FUN_00517079` | no counterpart (StormLib) |
| `FUN_00516f85` | `file_read_error_name` | After a short read (`FUN_00406900` below the size asked): the path as `file_open_error_name`. Callers `FUN_00476d40`, `FUN_00516e84`, `FUN_00517079` | no counterpart (StormLib) |
| `FUN_0052b780` | `c2s_send_af` | Fastcall: the two-byte C->S `[AF xx]` (`FUN_0052b330`). Only caller `FUN_0052a750` | not compared |
| `FUN_0052b810` | `net_stub_52b810` | Returns. Only caller `FUN_0052a750` | no counterpart (empty) |

## The sprite cache, tile getters and the DC6 row writers

The row writers below decode one cel's DC6 rows (a byte with bit 7 set
skips `& 0x7f` pixels, 0x80 ends the row, else that many literal bytes)
upward from the destination, as the cel draw `FUN_00608540` picks them
by draw mode (cels.md). Most come as a full and a clipped variant (the
clipped one takes the left and right edges). "Blend" is
`table[dst × 256 + src]`, the PL2-table path (palette.md).

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_005ff4a0` | `spritecache_take_slot` | Fastcall (cache, slot, prefer): with slots left, a free one off the LRU list (`+0x14`, not locked, bit 2 of `+0xc`), or the older of the two ends (`FUN_005ff0f0`); its size off the total (`+8`, under the lock `+0x30`), its data released (`+0x2c` callback) and freed (line 0x9e8), moved to the used list (`+0x24`/`+0x28`), count `+0xc` − 1. Callers `FUN_005ff760`, `FUN_005fffa0` | no counterpart: d2d keeps decoded sprites in memory |
| `FUN_005ff5b0` | `spritecache_touch` | Fastcall (cache, slot): hit count `+0x4c` + 1; under the lock the slot (unlocked) goes to the LRU tail with the tick count at `+0x10`. Only caller `FUN_006001f0` | no counterpart (as `spritecache_take_slot`) |
| `FUN_005ffa20` | `spritecache_read_dir` | `EAX` file, `ESI` slot, (direction, header, frames): the direction's frame offsets (header `+0x1c`, at most 0x20 frames, each relative to the first) into the slot, its direction count and frame count; unless the file is in memory (`header & 2`), an async read of that span (`d2d_async_data_create`, flag 4 on the slot); the span's size, -1 on a bad offset or failed read. Only caller `FUN_005fffa0` | no counterpart (as `spritecache_take_slot`) |
| `FUN_00604c20` | `tile_get_20` | A tile record's `+0x20` (null asserts). `FUN_0066d440` walks frames by it; `FUN_0066d820` sums it as pick weights | not compared: d2d reads the DT1's rarity (components/drlg/room_tiles.hpp:656) |
| `FUN_00604cb0` | `tile_get_8` | A tile record's `+8`. Only caller the automap `FUN_004dd7c0` | not compared |
| `FUN_00604ce0` | `tile_get_28_ptr` | The tile record + 0x28. Only caller `FUN_0064c4c0` | not compared |
| `FUN_00606d10` | `dc6_skip_rows` | Fastcall (rows data, n): skips n rows | matches: d2d decodes the same tokens (components/dc6/dc6.hpp:123) |
| `FUN_00606d40` | `dc6_rows_copy` | Opaque copy of `param_3` rows, pitch `param_4` | matches (as `dc6_skip_rows`) |
| `FUN_00606da0` | `dc6_rows_copy_clip` | As `dc6_rows_copy`, only columns between the two edges | matches (as `dc6_skip_rows`) |
| `FUN_00606e40` | `dc6_rows_blend_map` | Blend after a colour map: `blend[dst × 256 + map[src]]` | differs: d2d blends in RGB, not through the tables (todo.md 30) |
| `FUN_00607060` | `dc6_rows_blend_map_clip` | Clipped `dc6_rows_blend_map` | differs (todo.md 30) |
| `FUN_006072f0` | `dc6_rows_map2` | Two colour maps: `map_a[map_b[src]]` | not compared |
| `FUN_00607480` | `dc6_rows_map2_clip` | Clipped `dc6_rows_map2` | not compared |
| `FUN_00607680` | `dc6_rows_map` | One colour map: `map[src]` | not compared |
| `FUN_006077c0` | `dc6_rows_map_clip` | Clipped `dc6_rows_map` | not compared |
| `FUN_00607970` | `dc6_rows_blend_map_b` | The same per-pixel formula as `dc6_rows_blend_map`; what tells the two apart wasn't followed | differs (todo.md 30) |
| `FUN_00607b90` | `dc6_rows_blend_map_b_clip` | Clipped `dc6_rows_blend_map_b` | differs (todo.md 30) |
| `FUN_00607e20` | `dc6_rows_map_b` | The same per-pixel formula as `dc6_rows_map` | not compared |
| `FUN_00607f60` | `dc6_rows_map_b_clip` | Clipped `dc6_rows_map_b` | not compared |
| `FUN_006082e0` | `dc6_rows_blend_clip` | Clipped blend with no map: `blend[dst × 256 + src]` | differs (todo.md 30) |
