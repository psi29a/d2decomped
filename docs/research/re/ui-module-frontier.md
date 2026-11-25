# UI module frontier — game.exe 1.14d

The 68 `UI` rows of `build/gaps/functions.tsv` with `frontier = true` on
2026-11-18: untouched functions that a function d2d ports calls. Read
from the decompile and, where it matters, the disassembly and the data;
no oracle was run for these rows unless one is named. The decompiler
drops many fastcall arguments here (`FUN_004538d0(n)` prints as
`FUN_004538d0()`), so a row says only what the code shows. Re-check with
`tools/ghidra/fn.py <address>` and `tools/ghidra/calls.py <caller> --to
<address>`. The **d2d** column is as in [game-frontier.md](game-frontier.md):
*matches*, *differs*, *not ported*, *no counterpart* (with why), *not
compared*.

Common callees: `FUN_00524a30` a string by id, `FUN_00526700` appends a
wide string, `FUN_004fb180(r, g, b)` the palette's nearest index,
`FUN_004788b0` loads a DC6 (mpq-archives.md), `FUN_00478a00` frees one,
`FUN_00451db0` / `FUN_00451e10` with `d2d_window_handle_get(n)` (calls
with and without a flag on window control `n`; not followed).

## A monster's immunities (`FUN_004526c0`)

The hover bar's text (`FUN_00454ad0`) ends with `FUN_004526c0`, `EBX` the
monster. Unless its class (`+4`) is 0x2c0..0x2c5, it walks six 10-byte
rows at `0x6d6418` {dword stat, word string, dword colour} and, for each
stat over 99 (`d2d_unit_stat_value`), adds that string as a line (a line
break between two): damageresist (stat 36) string 0x2765 colour 4,
magicresist (37) 0x2766 colour 8, fireresist (39) 0x2761 colour 1,
lightresist (41) 0x2763 colour 9, coldresist (43) 0x2762 colour 3,
poisonresist (45) 0x2764 colour 2. Then `DAT_007a2c78` = 4. The row
bytes were read from game.exe (`tools/emu`: `emu.Emu().read(0x6d6418,
60)`).

d2d matches: its bar (apps/d2d/session/view.cpp, `draw_monster_bar`) draws
a line per immunity under the label (`rules::immunity_lines`), in the row's
colour. The rows are checked against game.exe: `cd tools/emu && uv run
python immunity_table.py` (frozen as tests/immunity_table_cases.inc,
test_immunity_lines); the strings read from game.exe's tables are "Immune
to Physical / Magic / Fire / Lightning / Cold / Poison". The resists come
from MonStats and the boss mods: a curse that lowers one isn't counted
(a `ponytail:` note there).

## The UI start (`FUN_00456970`)

The client's UI reset at a game's start (`FUN_00456970`; menu.md "The
mini-panel's start") calls eight functions the gap map listed as frontier
on 2026-11-20. Their teardown partners are in `FUN_00456d80` (rows below
and in unnamed-frontier.md). Each runs in stock game.exe, with its
loaders and registry calls hooked: `cd tools/emu && uv run python
ui_start_frontier.py --mutation-check` prints `ok: 10 mini-panel, 6 text,
2 waypoint/dialogs, 64 store cels, 1 quest log, 4 controls, 4 keys`, then
eight caught negative controls. The rows go in call order.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0047f0c0` | `minipanel_cels_load` | Game type 0 (single player, `FUN_0044db30` = `0x7a0610`) loads `Panel\minipanel_s` into `0x7bc960` and sets `0x7bc978` = 1; any other type loads `Panel\minipanel`, flag 0 (`d2d_ui_dc6_load`, strings at `0x6d7488` / `0x6d749c`). Both load `Panel\minipanelbtn` into `0x7bc964`, then clear 7 (flag 1) or 8 button-down dwords at `0x7bc8f8`. The flag picks the bar everywhere: `FUN_0047f710` draws it at W/2 − 0x4a − 3 (flag 1) or W/2 − 0x54 − 3, and W/2 + 0x38 − 2 or + 0x23 − 2 beside a left panel; `FUN_0047e8b0` lays out 7 buttons (ids 0, 1, 2, 4..7) or 8 (ids 0..7, 3 the Party screen: `FUN_0047ec50` case 3 opens it when the game type isn't 0), and the press and release (`FUN_0047ef30`, `FUN_0047ed90`) walk 7 or 8 | matches in a joined game (type 9) and single player: apps/d2d/panels/mini_panel.hpp picks the form by `net_game`, its layout checked against game.exe (tools/emu/minipanel_layout.py, test_mini_panel); d2d's own `--host` / `--connect` games keep the single-player bar (menu.md "Mini-panel") |
| `FUN_0049d440` | `text_state_init` | Clears 0x9f bytes at `0x7bf1b0`, then `0x7bf1f6` = the registry's `Diablo II\Text Display Beta` dword (`FUN_00414f10`), or 1 when the read fails. `FUN_0049d570` writes it back. The NPC speech scroll `FUN_0049d5a0` draws its box (`FUN_00452e50`) and lines only while `0x7bf1f6` or `0x7bf1fa` is set; the scroll's timing runs either way | matches the default (1): d2d draws the speech box whenever a speech is up (apps/d2d/render/ingame.cpp:467); the setting itself has no counterpart |
| `FUN_0049c6a0` | `waypoint_ui_init` | `0x7bf081` = `FUN_00660f30(0)` (a 16-byte waypoint record from WayPoint\Waypoint.cpp, word 0 = 0x102, then word `[0x746428]` (1) or'd with `[0x74642a]` (1)), byte `0x7bf085` = 0. The waypoint panel (`FUN_0049cf90`, S->C 0x63) and its rows (`FUN_0049c7f0`) read the record | not compared |
| `FUN_004b7b40` | `dialogs_init` | `DAT_007c0e58` = 0, the flag `dialogs_free` tests | no counterpart (as `ui_cels_free_7beeec`) |
| `FUN_00489220` | `store_cels_load` | Each slot still empty is loaded (`FUN_004788b0`, flag 0): `DATA\GLOBAL\ui\panel\buyselltabs` (`0x7bcc24`), `miniconvert` (`0x7bcc28`), and `invchar6tab` (`0x7bcc38`) only when d2exp.mpq was found (`FUN_00408f20`) and the game is an expansion one (`FUN_0044dcc0`) | matches in an expansion game: d2d loads all three (apps/d2d/load.cpp:146, 262, 305); its weapon-switch tabs in a classic game not compared |
| `FUN_004a3410` | `quest_log_init` | Writes the registry's `Diablo II\GAMEOVER` = 0 (`FUN_004150e0`; the string's only reference in game.exe, so nothing in game.exe reads it), then resets the quest log: state `0x7bf298` and dwords `0x7bf2d1`, `2d5`, `2d9`, `2dd`, `34d`, `351`, `2a4`, `2ac`, `0x7c0259` to 0, bytes `0x7bf2e1..0x7bf34c` and 0x29 each at `0x7bf356` and `0x7bf380` to 0, bytes `0x7bf2b0..2b4` and `0x7c025d..0x7c0260` to 0, dwords `0x7bf2b5`, `2bd`, `2c1`, `2c5`, `2c9`, `2cd` to −1 | no counterpart for `GAMEOVER`; the quest log's reset not compared |
| `FUN_0046ac70` | `keys_start` | `FUN_0046aae0` (the key bindings' load, controls.md), `DAT_007a7418` = 1; the first time (`0x7a7428` was 0) sets `0x7a7428` and installs the four 12-byte handler rows at `0x712944` on the window `[0x7c8cbc]` (`FUN_00451db0`, count 4): WM_KEYDOWN and WM_SYSKEYDOWN → `FUN_0046a840`, WM_KEYUP and WM_SYSKEYUP → `FUN_0046a940`. Those run a bound key's action (table `0x712698`); with `0x7a7418` = 2 only the actions whose third word is set. Undone by `FUN_0046acb0` | no counterpart: d2d reads SDL key events and keeps its keys in d2d.cfg (unnamed-frontier.md `keyconfig_file_path`) |
| `FUN_004a5190` | `controls_init` | `FUN_004a43e0`: an expansion game takes the rows table `0x724468`, 0x3e rows, 0x2f, 5 thumb rows (`0x7c0278`); a classic one `0x724268`, 0x33, 0x24, 6. Then `DATA\GLOBAL\UI\Menu\TextSlid` into `0x7c02bc`, the buttons' boxes (`FUN_004a5060`), and scroll `0x7c02ac` and selection `0x7c02b0` = 0 | matches for the thumb: 5 rows in an expansion game, 6 in classic (apps/d2d/panels/controls.cpp:165); the rows tables are d2d's `keys::rows(expansion)`, not compared row by row |

## Rows in address order

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_004526c0` | `hover_immunities` | See "A monster's immunities" | matches (see above; tools/emu/immunity_table.py) |
| `FUN_004545c0` | `npc_dialog_window_flag` | Fastcall flag: while `DAT_007a27e8`, window control 5 through `FUN_00451db0` (flag set) or `FUN_00451e10` (clear). Callers the dialog open `FUN_004b7cd0`, `FUN_004b7c00`, `FUN_004b7eb0`, `FUN_004b8020` | no counterpart: d2d has no Win32 window controls |
| `FUN_00454790` | `set_loading_act` | Fastcall: `DAT_007a288c` = ECX, read by `FUN_004547b0`'s `FUN_00600c00`. Only caller `FUN_0044e100`, before the loading screen | no counterpart: d2d draws no loading screen |
| `FUN_004547b0` | `loading_screen_end` | When the loading DC6 `DAT_007a2888` is set: frees it, then `FUN_004fb480`, `FUN_00600c00(DAT_007a288c)` and the colour setups (`FUN_0045a620`, `FUN_00499e30`, `FUN_004972b0` and seven more); clears `0x7a2884`, `0x7a2888`, `0x7a2868`. Callers `FUN_0044c990`, `FUN_0044e380`, `FUN_00456300` | no counterpart (as `set_loading_act`) |
| `FUN_00454880` | `text_area_span` | Fastcall `int[4]`: x from 0 to the screen width, narrowed to the right half when a left panel is open (`0x7a2834`, `0x7a2818`, `0x7a27c8`, `0x7a27fc`, `0x7a2850`, `0x7a2854`, `0x7a2810`) and to the left half for a right panel (`0x7a27c4`, `0x7a27d0`); false with any of `0x7a281c`, `0x7a2824`, `0x7a2828`, `0x7a27f0`, `0x7a2830`, `0x7a282c`, or when the span is empty. Callers `FUN_00454970`, `FUN_004549f0`; `FUN_00454ad0` keeps the monster bar at its middle less half the name's width, y 0x20 (0x454d79..0x454d96) | ported, read: `rules::monster_bar_span` / `monster_bar_box`, `test_hover_box` |
| `FUN_00454980` | `window_controls_set` | `FUN_00451db0` on window controls 0xd and 1. Callers `FUN_00455720`, `FUN_004a5a40`, `FUN_004a5d70` | no counterpart (as `npc_dialog_window_flag`) |
| `FUN_004549c0` | `ui_flag_7a27a0` | `DAT_007a27a0 != 0`. Only caller `FUN_004b8730`, gating a click in its box | not compared |
| `FUN_00454a70` | `hover_lines_draw` | When the hovered unit (`FUN_00467a10`) is a monster: line `DAT_007a2f30` in colour 6 at the text height + 0x27, then line `DAT_007a2c78` below it (one row lower when either `0x7a2d30` or `0x7a2f38` holds text). Only caller the panel draw `d2d_panel_draw_order` | not compared: d2d lays out the label and the immunity lines itself (apps/d2d/session/view.cpp) |
| `FUN_004554b0` | `hover_monster_label` | For a hovered monster with a label: colour 3 for one type test, else 4 for another, else 0, at the text height + 0x27 (`FUN_004549f0(6, y, colour)`). Only caller `d2d_panel_draw_order` | not compared |
| `FUN_00456550` | `loading_screen_begin` | `loading_screen_end`, `FUN_00454740`, then loads `DATA\GLOBAL\UI\Loading\Loadingscreen` (or `DATA\LOCAL\UI\LoadingScreen` when `FUN_00525150`) into `DAT_007a2888`, frame 0, `0x7a2868` = 1, the bar reset (`FUN_00478130`), `0x7a27b8` = `DAT_007a0624`. Callers `FUN_0044e200`, `FUN_004565e0` | no counterpart (as `set_loading_act`) |
| `FUN_0045a620` | `automap_colours_init` | Eleven automap colours `0x7a51b0..0x7a51ba` from the palette: (0,0,255), (255,0,0), (255,0,255), (0,255,0), (0x44,0x70,0x74), (0x48,0xa0,0x34), (0xf4,0xf4,0xf4), (0x80,0x80,0x80), (0xf4,0xf4,0), (0,0,255), (0,255,0). Callers `FUN_0044e100`, `FUN_00454740` | matches: the pet teal (0x44, 0x70, 0x74) at `0x7a51b4` (automap.md) |
| `FUN_00484c90` | `item_text_ear_name` | `ESI` item with flag 0x1000000 (an ear, line 0x6ea): unless `FUN_00627e70` is 5..9, its name (`FUN_00628340`) formatted with `FUN_005272b0` into `EBX` (0x200 characters); returns 1, else 0. Only caller the item name `FUN_0048c060` | not compared |
| `FUN_00484d30` | `item_text_ear_name2` | As `item_text_ear_name` without the 5..9 test (line 0x70b), into the second argument. Only caller `FUN_0048c060` | not compared |
| `FUN_00484db0` | `item_text_quantity_line` | `EBX` item with stat 0x46 (quantity) above 0: a line of two strings and the count (`%d` at `0x6d9be8`). Only caller the tooltip `FUN_0048dd90` | not compared |
| `FUN_0048db80` | `item_tip_transmogrify` | For the hovered item `DAT_007bcbf4` with the transmogrify flag (`d2d_item_transmogrify_flag`): the Items.txt row of its class (`FUN_006335f0`, 0x1a8 bytes a row), then the row of its TMogType code (`+0xc8`, `d2d_item_row_by_code`); text from strings and that item's name (`FUN_0048c060`), with `FUN_0048cea0`'s lines when `DAT_007bcbf0` is 1..9; drawn at the tooltip spot kept on the screen (`FUN_00502520`, `FUN_00502280`). Only caller the tooltip `FUN_0048dd90` | not compared |
| `FUN_00493b00` | `portrait_name_draw` | `EAX` unit: its name (`FUN_00464a60`, 0x20 characters) drawn under the portrait (`FUN_00492fa0`). Only caller the merc portrait `FUN_00493ce0` | not compared |
| `FUN_00494ee0` | `ui_cels_free_7beeec` | Frees the DC6s at `0x7beeec`, `0x7beef0`, `0x7beef4`. Only caller the client UI teardown `FUN_00456d80` | no counterpart: d2d's sprites are owned values |
| `FUN_00495180` | `minipanel_button_draw` | Unless one of six UI states (`FUN_004538d0`) is up, or `FUN_004942d0`: two cels at (width − 0x4b, height − 0xa0) and (width − 0x48, height − 0xa4), the second's frame `DAT_007bef04 != 0`; else `FUN_00455f20(0)`. Sets `DAT_007bef08`. Only caller `d2d_panel_draw_order` while `DAT_007a2848` | not compared |
| `FUN_00496990` | `ui_cels_free_7bef0c` | Frees 20 DC6s in `0x7bef0c..0x7bef80`, zeroes `0x7bef30` (0x30 bytes). Only caller `FUN_00456d80` | no counterpart (as `ui_cels_free_7beeec`) |
| `FUN_00496cf0` | `skill_buttons_draw` | Left skill (line 0x289) and right skill (line 0x294): one at level under 1 is unset (`FUN_006470f0`, `FUN_00643bc0` / `FUN_00643c50`); then each button (`FUN_00496be0`) at x 0x75 and width − 0xa5. Only caller the HUD `FUN_00499450` | not compared |
| `FUN_004972b0` | `hud_colours_init` | `0x7bef6c..0x7bef6f` = the palette's (0x80,0,0), (0,0x80,0), (0,0,0x80), (0x80,0x80,0). Callers `d2d_ui_dc6_load`, `FUN_00454740`, `FUN_004977c0`, `d2d_belt_key_labels` | not compared |
| `FUN_00497920` | `belt_hover_item` | `EDI` inventory: with a cursor item and `DAT_007bef94`, the belt box under it (`DAT_0072235c`): an empty box takes a beltable item, else `d2d_inv_belt_slot_for`; each path highlights the box (`FUN_004978d0`). Only caller `d2d_belt_key_labels` | not compared |
| `FUN_00497fe0` | `durability_alerts_draw` | Walks the body slots at `0x7223e8`: when any worn item with durability is under 20 % (stat 0x48 × 100 / max), draws the alert (`FUN_00497cf0`) for each slot that holds an item, and for slots 4 and 5 when empty too. Only caller `d2d_panel_draw_order` with no panel up | not compared |
| `FUN_00498340` | `minipanel_hover_tip` | With the mouse in (width / 2 − 8 .. + 5, height − 0x27 .. − 0xd): a string at y height − 0x27 (`FUN_00502280`). Only caller `FUN_00499450` | not compared |
| `FUN_00498930` | `belt_hover_set` | `EAX` mouse: the belt box under it (`FUN_004987e0`), unless the cursor state (`FUN_00468830`) is 6 or 8: with a cursor item, that box; else the box's item, its tooltip anchor (`0x722360`, `0x722364`) when it changed. Only caller `d2d_belt_popup_on_strip` | not compared |
| `FUN_00498a20` | `item_use_check` | Fastcall (_, Items row): a row with `+0x129` or `+0x12a` set selects the item text (returns 0); else a row without `+0x11d` plays the "impossible" voice and returns 0; else 1. Only caller `FUN_00498a90` | not compared |
| `FUN_00499e30` | `black_index_init` | `DAT_007bf034` = the palette's (0,0,0). Callers `d2d_ui_dc6_load`, `FUN_00454740`, `FUN_00494eb0`, `FUN_004a7ae0` | not compared |
| `FUN_00499f40` | `party_row_buttons_draw` | Two cels of `DAT_007bf028` (frames 0, 1) at panel x + 0xc and + 0x10b, y `EAX` − the panel y. Only caller `FUN_0049a9b0` | not compared |
| `FUN_00499fc0` | `party_cel_draw` | One cel (first argument, frame the third) at panel x + the second argument, y `EAX` − the panel y. Only caller `FUN_0049a510` | not compared |
| `FUN_0049a3d0` | `party_column_text` | Column 0..6: a string drawn at y `EAX` − the panel y (`d2d_common_glyph_top`); other columns nothing. Only caller `FUN_0049c270` | not compared |
| `FUN_0049aaa0` | `text_centre_x` | Fastcall (_, x): x less half the string's width (`FUN_00501820`). Callers `FUN_0049aaf0`, `FUN_0049b330` | not compared |
| `FUN_0049abd0` | `party_hover_tips` | The Party screen's tooltips: the button at panel x + 0xe3..0x118, y 0x18..0x23 (string 0x1018); then each roster row's button (0x35 wide, 0x14 high) by its state: 0 strings 0x1004, 2 0x1006, 4 0x1008, two lines at y 0x32 and 0x41. Only caller `FUN_0049c3d0` | not compared |
| `FUN_0049af60` | `party_tip_text` | One tip box (`FUN_0049aaf0`) with one or two lines, by whether the short is 0. Only caller `FUN_0049b330` | not compared |
| `FUN_0049c170` | `party_level_text` | A label, two strings and the level (`%2u`) drawn at y `EAX` − the panel y. Only caller `FUN_0049c270` | not compared |
| `FUN_0049c700` | `waypoint_ui_end` | Once (`DAT_007bf085`), with the player in a room, `FUN_004786a0(0)`; frees the waypoint record `DAT_007bf081` (`FUN_00660fd0`); `FUN_0049c650` frees the waypoint panel's cels (`waygatebackground`, `waygatetabs` or `expwaygatetabs`, `waygateicons`, loaded by `FUN_0049c580`). Only caller `FUN_00456d80` | not compared |
| `FUN_0049db10` | `text_lines_copy` | Fastcall source: a 0x9d0-byte record of up to the source's `+0x18` lines, each a 0x9d4-byte copy (`FUN_0040b380`); copies `+0x18` and `+0x1a`. Only caller `FUN_0049dbc0` (ui-text-frontier.md) | not compared |
| `FUN_0049f3d0` | `unit_text_free` | Fastcall unit: its overhead text (`FUN_00661200`) freed (`FUN_0049d370`, line 0x68e) and cleared, then `FUN_006611a0(0, unit)`. Only caller the client unit free `FUN_00465870` | no counterpart: d2d's units own their text |
| `FUN_004a51e0` | `ui_cel_free_7c02bc` | Frees the DC6 at `0x7c02bc`. Only caller `FUN_00456d80` | no counterpart (as `ui_cels_free_7beeec`) |
| `FUN_004a5d00` | `keyconfig_open_windows` | `DAT_007c02b8` = 1, `FUN_00455ae0`, `FUN_00451db0` on controls 6, 1, 1, 1. Only caller `FUN_004a5e60` | no counterpart (as `npc_dialog_window_flag`) |
| `FUN_004a5d70` | `keyconfig_close_windows` | `FUN_00451e10` on controls 6, 1, 1, 1; with the argument, `window_controls_set` and `DAT_007c02b8` = 0. Callers `FUN_004a6000`, `FUN_004a63b0` | no counterpart (as `npc_dialog_window_flag`) |
| `FUN_004a6480` | `ui_cels_free_7c02dc` | Frees the DC6s at `0x7c02dc`, `0x7c02e0`. Only caller `FUN_00456d80` | no counterpart (as `ui_cels_free_7beeec`) |
| `FUN_004a76c0` | `inv_click_left_half` | `ESI` click: at 800 × 600 (`d2d_renderer_resolution` 2), no `FUN_00498dc0`, x at most width / 2, y above height − 0x2f: cursor state 6 runs `FUN_00453ec0`; marks the click handled (`+0x18`), `FUN_00420290`, 1. Callers `FUN_004a7720`, `FUN_004a78c0`, for clicks outside the panel box | not compared |
| `FUN_004a7a90` | `set_flag_7c02ec` | `DAT_007c02ec` = 1. Only caller `FUN_00454150`, case 0 | not compared |
| `FUN_004a8ce0` | `set_7c07fc` | Fastcall: `DAT_007c07fc` = ECX. Only caller `FUN_004996a0`, after `FUN_00455f20(0)` | not compared |
| `FUN_004b3470` | `npc_talk_active` | 1 when `DAT_007c0d29`, the talked-to unit exists (`d2d_client_unit_find`) and `DAT_007c0c6b` is 1. Callers `FUN_004a66e0`, `FUN_004a6790` | not compared |
| `FUN_004b34a0` | `npc_menu_any_open` | 1 when any of eight dwords `0x7c0d5f..0x7c0d7b` (4 apart) is set. Callers `FUN_00488e70`, the tooltip `FUN_0048dd90` | not compared |
| `FUN_004b3de0` | `npc_talk_end` | `DAT_007c0c6b` = 0, `FUN_00487990`, `FUN_004b3c20`, `FUN_00455f20(0)`. Callers `FUN_004b3f10`, `FUN_004b4fd0`, the greeting `FUN_004b66b0`, `FUN_004b6a30` | not compared |
| `FUN_004b5c20` | `hire_list_close` | The hire list's close callback: when `DAT_007c0d77`, `FUN_004b8020`, the box's virtual call (1), both cleared; `FUN_004b4830`; 1. Passed by `FUN_004b5c60` to `FUN_004b7eb0` | not compared |
| `FUN_004b7970` | `dialog_key_is_ok` | `EAX` dialog, `ESI` key: with `+0x20` clear, 1 when the key is either of two bound keys (`d2d_keyconfig_key_of`). Callers `FUN_004b79d0`, `FUN_004b7a90` | not compared |
| `FUN_004b7b50` | `dialogs_free` | When `DAT_007c0e58`, `FUN_00451e10` on controls 0xe and 1, `0x7c0e4c` − 1; frees the dialog list `DAT_007c0e54` (next `+0xb0c`, line 0x333). Only caller `FUN_00456d80` | no counterpart (as `ui_cels_free_7beeec`) |
| `FUN_004b7c60` | `dialog_keys_cache` | Two bound keys (`d2d_keyconfig_key_of`, each below 0xe0) into `0x7273f4` and `0x727400`. Callers `FUN_004b7cd0`, `FUN_004b7eb0` | not compared |
| `FUN_004b7cd0` | `dialog_open` | Fastcall: `npc_dialog_window_flag`, `FUN_004898d0`, mouse reset; refuses a null callback or a positive fifth argument; a 0xb10-byte dialog (line 0x396) pushed on `DAT_007c0e54`: tick count, the background `DATA\GLOBAL\ui\menu\<name>` (`0x7274ac[n]`), its size, position; `dialog_keys_cache`, controls 0xe and 1 set, count `0x7c0e5c`. Only caller `FUN_00454150` | not compared |
| `FUN_004b85e0` | `dialog_count` | Returns `DAT_007c0e5c`. Callers `FUN_00453910` (a dialog up refuses UI 5) and `FUN_004b8cb0` | not compared |
| `FUN_004b9060` | `text_buffer_7c0e84` | Returns `&DAT_007c0e84`. Only caller `FUN_0048edf0` | not compared |
| `FUN_004b93f0` | `sound_slot_alloc` | The first free 0x59-byte slot of `0x7c0ed0..0x7c5458`: zeroed, in use, `FUN_00516950`, a new serial (`DAT_007c5460` + 1) at `+8`. Only caller the sound start `FUN_004b9a00` | not compared |
| `FUN_004b94e0` | `sound_slot_free` | `EDI` slot: `FUN_00516980`, each listed callback (`d2d_unit_sound_list_call`), `FUN_00516a10`, `FUN_004df7b0` when `+0x30` is 1; frees it. Callers `FUN_004b9d20`, `FUN_004ba020` | not compared |
| `FUN_004b9890` | `sound_start_volume` | 0.5 or 0: a sound with a row whose `+0x47` is 4 gets 0; row `+0x60` 0xca checks `FUN_00481920`'s `+0x18`; else `FUN_00622aa0(player, sound, 2)` decides. Only caller `FUN_004b9a00` | not compared |
| `FUN_004b9c60` | `sound_playing_4657_4698` | 1 when a playing sound (list `DAT_007c5458`) not in state 2 has an id 0x1231..0x125a. Only caller `FUN_004dcaa0` | not compared |
| `FUN_004b9d50` | `sound_of_unit` | Fastcall unit: the first playing sound with that unit at `+8`, through `FUN_004df900`; 0 for none. Only caller `FUN_004dcaa0` | not compared |
| `FUN_004ba950` | `sounds_stop_52_71` | Fastcall (unit, unit): each playing sound of id 0x34..0x47 not from either unit is flagged `+0x3d`, and its row's `+0x40` fade (`FUN_004b9ef0`) when in state 1. Only caller `FUN_004e42e0` | not compared |
| `FUN_004ba9d0` | `sounds_stop_72_201` | As `sounds_stop_52_71` for ids 0x48..0xc9. Only caller `FUN_004e42e0` | not compared |
| `FUN_004bbbb0` | `editbox_ctor` | `EditBoxImplementation`: the slider `DATA\GLOBAL\ui\menu\textslid`, two text buffers of (max + 1) words (lines 0x5d, 0x5e), position, width (else 10 digits' width × (max + 2) / 10), lines (else 1, one-line mode). Only caller `FUN_004bbd80` | not compared |
| `FUN_004c0100` | `ui_cels_free_7c5494` | `FUN_004c0090` when `DAT_007c5474`; frees the DC6s at `0x7c5494`, `0x7c5498`. Only caller `FUN_00456d80` | no counterpart (as `ui_cels_free_7beeec`) |
| `FUN_004eca00` | `skilldesc_line_40` | SkillDesc line kind 0x40 (`d2d_skill_text_desc_line`): unless string 0x1506, three strings, `FUN_004e73e0`'s value, a string, the level (`%d`), three more. Returns 1 | matches: run inside `tools/emu/skill_tips.py`, every SkillDesc line, against `test_game --skill-tips` (components/game/skill_text.hpp:155) |
| `FUN_004ecb10` | `skilldesc_line_41` | Kind 0x41: unless either string is 0x1506, five strings | matches (as `skilldesc_line_40`) |
| `FUN_004ecb80` | `skilldesc_line_45` | Kind 0x45: `FUN_004e73e0`'s value, five strings, one more when the third argument is set, then one | matches (as `skilldesc_line_40`) |
| `FUN_004ecc80` | `skilldesc_line_42` | Kind 0x42: a string formatted (`FUN_005269d0`, 0x100 characters) with the argument, then two | matches (as `skilldesc_line_40`) |
| `FUN_004ed0d0` | `skilldesc_line_2d` | Kind 0x2d: for a value above 1, `FUN_004e7c30(0x10d5, 0)` and two strings; 1 | matches (as `skilldesc_line_40`) |
