# Unnamed frontier — game.exe 1.14d

The 375 rows of `build/gaps/functions.tsv` with `frontier = true` and no
module on 2026-11-18: untouched functions that a function d2d ports
calls, with no source-file assert to name their module. Rows are in
address order, in sections by address range. Read from the decompile
(and, where a row says so, game.exe's data); no oracle was run for these
rows, and a row says only what the code shows. Re-check with
`tools/ghidra/fn.py <address>` and `tools/ghidra/calls.py <caller> --to
<address>`. The **d2d** column is as in [game-frontier.md](game-frontier.md).
`FUN_0041c340` is Storm's set-last-error (0x57 invalid parameter);
`FUN_00524a30` is a string by id, `FUN_00526700` appends a wide string.
A caller list gives the count and the callers d2d ports.

## 0x400000..0x46ffff: runtime, Storm helpers, front end, client

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00406ba0` | `install_path_get` | The registry's `Diablo II\InstallPath` (`FUN_00414e50`), else the module's folder (`GetModuleFileNameA`), checked with `GetFileAttributesA`, saved back (`FUN_00415070`). 6 callers, ported: `FUN_00408f20`, `FUN_00469780`, `d2d_keyconfig_defaults`, `FUN_00482ef0` | no counterpart: d2d takes the install from its launcher settings |
| `FUN_00406d20` | `get_74c82c` | Returns `DAT_0074c82c`. 7 callers (the MPQ opens `FUN_004fab90`, `FUN_004fac90`, `FUN_004fad10`, `FUN_004faec0`; `FUN_0050fdf0`) | not compared |
| `FUN_0040f960` | `log_shutdown` | A thunk to `FUN_0040f8a0`: waits up to 1 s (10 ms sleeps) for the log queue `DAT_0075e870` to drain, then closes the open log files (`0x75e790..`) under their lock; asserts `DAT_0075e2ec` (line 0x1c8). Called by the error paths (`d2d_error_box`, `d2d_error_system_init` and others) | no counterpart: d2d logs through `d2d::log` |
| `FUN_00410610` | `log_printf` | Asserts the format (line 0x12b), then `FUN_00410340(1, format, va)`. 36 callers | no counterpart (as `log_shutdown`) |
| `FUN_00410660` | `log_printf_to` | As `log_printf` with a log id first (asserts the format, line 0x13c). 26 callers, among them `FUN_00539760` | no counterpart (as `log_shutdown`) |
| `FUN_00410f60` | `bits_read` | (reader, n): reads n bits LSB-first (masks `0x6ce38c`, `0x6ce368`), byte by byte; reading past the end (`+8` bytes × 8 vs `+4` bits) sets the overflow flag `+0x10` and returns what was there. Callers `FUN_00411020`, `FUN_00411030` | not compared |
| `FUN_00411070` | `bits_align_byte` | A reader mid-byte moves to the next byte (`+0xc` = 0) and, while in range, zeroes it. Only caller `FUN_006312b0` | not compared |
| `FUN_00411170` | `checksum_bytes` | The sum of the table `0x6ce398` (256 dwords) over the bytes. Only caller `FUN_0056a620` | not compared |
| `FUN_004111a0` | `lowest_set_bit_index` | The index of the lowest set bit through the byte table `0x6ce798` (+8, +0x10, +0x18 by byte), -1 for 0. Callers `FUN_00570e30`, `FUN_005711d0`, `FUN_005d6410` | not compared |
| `FUN_00411710` | `storm_handle_lock` | `EAX` handle: its block (`+4` word × 0x10000), the block's lock (`0x776cc0 + index × 0x18`) entered, its index out; returns the block. Callers `FUN_00412a60`, `FUN_00413310`, `FUN_004133d0` | no counterpart (Storm) |
| `FUN_00413490` | `str_find_char` | `strchr`: the first `param_2` in the string, null at the end; a null string sets 0x57. 28 callers | no counterpart (the standard library) |
| `FUN_00413550` | `str_ncmp` | `strncmp`; a null side sets 0x57 and returns the pointer difference. Callers `FUN_0043b9f0`, `FUN_0044b4e0`, `FUN_0044b600`, `FUN_006bd640` | no counterpart (the standard library) |
| `FUN_00413a00` | `vsnprintf_cut` | `EDI` buffer, `ESI` size (0x7fffffff unbounded): `vsnprintf`, the last character NUL when cut; the length. Callers `FUN_00413a40`, `FUN_00413a80` | no counterpart (the standard library) |
| `FUN_00413b80` | `str_token` | Splits `*cursor` at any of the separators (`param_4`), a quoted span (`"`, when a separator) kept whole, into the buffer; the cursor advances. Callers the chat command split `FUN_0047bad0`, `FUN_006c4ec0`, `FUN_006c4fb0` | not compared |
| `FUN_004141e0` | `str_upper` | `_strupr`. Callers `d2d_warden_digest` and three Battle.net functions | no counterpart (the standard library) |
| `FUN_00414c70` | `reg_set_value` | `EBX` flags: the key `Software\Blizzard Entertainment\` + subkey (unless flag 0x10; HKCU, HKLM with 4), created; the value set; flushed with 8; the error to `SetLastError`. Callers `FUN_00415000`, `FUN_00415070`, `FUN_004150e0` | no counterpart: d2d keeps settings in d2d.cfg |
| `FUN_00414d90` | `reg_key_check` | Both names required (0x57), then `FUN_00414990(name)`. Only caller `FUN_005186d0` | no counterpart (as `reg_set_value`) |
| `FUN_00415070` | `reg_set_string` | Key, value and string required (0x57); the string with its NUL (`reg_set_value`, type 1). 19 callers | no counterpart (as `reg_set_value`) |
| `FUN_00426e80` | `break_cmd_add` | Under `0x77930c`: a 12-byte `AUBREAKCMD` node holding the argument pushed on the list `0x7088ec`. Only caller `FUN_00420290` | no counterpart |
| `FUN_0042fb20` | `title_screen_build` | The title screen's records (`FUN_0042f430`), its texts: a string with the year (`2000` classic, `2001` with `FUN_00408f20`; read from `0x6d4190`) rewritten to `2000 - 2016`, into text lists (`FUN_004fc9b0`); `FUN_004fa410(0)`; the playlist on (`FUN_005148f0`) when `FUN_00514d80`. Callers `FUN_00433640`, `FUN_00435230` | not compared: d2d's title draws the version line (apps/d2d/frontend.cpp:138); this text's place wasn't followed |
| `FUN_0044b6b0` | `app_args_init` | Runs the init table `0x6d5be0`; with no video option (`FUN_0044d9f0`), the registry's `Default Command Line` parsed (`FUN_006c4fb0`); the user name (`GetUserNameA`, `noname` on failure) into `0x7a05c4`. Only caller the app start `FUN_0044b8a0` | not compared |
| `FUN_0044b7a0` | `get_7a0438` | Returns `DAT_007a0438`, the launch record. Only caller `FUN_0044e5a0` | not compared |
| `FUN_0044b820` | `launch_game_name` | The launch record's `+0x37` string when not empty, else null. Only caller `FUN_00459e90` | not compared |
| `FUN_0044b860` | `launch_flag_8` | The launch record's word `+0x1ef` gets bit 3. Only caller `FUN_00453910` | not compared |
| `FUN_0044c6e0` | `net_drain` | Reads packets until none (`FUN_0052a8b0`, 0x200 bytes, then `FUN_0045c850`), then the second queue (`FUN_0052a890`, `FUN_0045f7b0`), yielding between. Only caller `FUN_0044efa0` | not compared |
| `FUN_0044c820` | `light_preset_set` | With no light record (`DAT_007a0634` 0): `0x7a060e` = 1, `FUN_0061c240(…, FUN_004646a0(5, 0, 1), 5, 0, 1)`. Only caller `FUN_004a2cb0` | not compared |
| `FUN_00451910` | `record_on` | Returns `DAT_007a277c`. Only caller `FUN_0044f360` | no counterpart: d2d has no message recorder (game-frontier.md) |
| `FUN_00451940` | `record_start` | `DAT_007a2780` = 1, `DAT_007a2764` = `FUN_005173d3()`. Only caller `FUN_0044f360` | no counterpart (as `record_on`) |
| `FUN_00451970` | `record_flag_set` | `DAT_007a2788` = 1. Only caller `FUN_0044f360` | no counterpart (as `record_on`) |
| `FUN_00451980` | `record_get_7a2784` | Returns `DAT_007a2784`. Only caller `FUN_0044f360` | no counterpart (as `record_on`) |
| `FUN_00451d10` | `window_control_send` | Fastcall (control, kind, message, data): kinds 0, 1 `FUN_00420c70`, 2 `FUN_00420cb0`, 3 `FUN_00420c90`; message 0x111 (WM_COMMAND) also `FUN_00420cd0`; others assert (lines 0x131, 0x135). Callers `FUN_00451e10`, `FUN_00451eb0` | no counterpart: d2d has no Win32 window controls |
| `FUN_00452130` | `ui_colours_init` | `FUN_00483960`, `FUN_00499e10`, `FUN_00499e30`, `FUN_004ab2f0`, `FUN_00497250`, `FUN_00497270`, `FUN_004972b0`, `FUN_00497290`. Only caller `FUN_0044e100` | not compared |
| `FUN_00452170` | `return_arg` | Returns ECX. 6 callers, among them the item text (`FUN_0048c060`, `FUN_0048d1d0`, `FUN_0048dd90`) | no counterpart (identity) |
| `FUN_00452300` | `chat_strip_token` | A wide string under 500 characters (line 0x37f): each place a string (by id) occurs, removed along with the character after it. Only caller `FUN_0049f490` | not compared |
| `FUN_00452520` | `hover_line_set` | Fastcall (text, colour): the hover line `0x7a2d30` cleared then copied, `DAT_007a2f30` = colour. Only caller the hover text `FUN_00454ad0` | not compared |
| `FUN_0045afa0` | `get_7a520c` | Returns `DAT_007a520c`. Only caller `FUN_00459700` | not compared |
| `FUN_0045afb0` | `get_7a5208` | Returns `DAT_007a5208`. Only caller `FUN_00459700` | not compared |
| `FUN_00466ff0` | `clear_7a6a98` | `DAT_007a6a98` = 0. 5 callers, among them `FUN_0044c180` | not compared |
| `FUN_00467410` | `pet_colour_set` | Fastcall (unit, pet, value): the pet's colour record (`FUN_00478e40`) `+8` = value. Callers `FUN_00462b40`, `FUN_00462ca0`, `FUN_00493430` | not compared |
| `FUN_00467f20` | `cursor_click_left` | Keeps the click's x, y (`0x7a6ab0`, `0x7a6aac`) and tick; marks the event unhandled (`+0x18`); unless the cursor state is 5, a state with a handler (table `0x712020`, 0x1c bytes) moves to state 5 with mode 4. 4 callers, ported: `FUN_004a66e0`, `FUN_004a6790`, `FUN_004a7720` | not compared |
| `FUN_00467fa0` | `cursor_click_right` | As `cursor_click_left`; state 5 goes to mode 5, else state 1. 5 callers, among them `FUN_0048b7c0`, `FUN_004a6840`, `FUN_004a6920` | not compared |
| `FUN_004684c0` | `cursor_draw` | With a cursor up (`DAT_007a6b08`): an item cursor (kind 4, states 5..7) draws the item's cel centred on the mouse (`d2d_gfx_item_cel_draw`); else the state's draw function (`0x712024 + mode × 0x1c`). Callers `FUN_0044c990`, `FUN_0044e770` and two more | not compared |
| `FUN_00468820` | `stub_468820` | Returns. Only caller `FUN_0044c990` | no counterpart (empty) |
| `FUN_00469650` | `keyconfig_file_path` | Builds `<save dir><account>\<user>.key` (`0x7a0500`, `0x7a05c4`) and probes it (`FUN_00406960`); missing, the save folder's `<user>.key`. Callers `FUN_00469780`, `FUN_0046aae0` | not compared: d2d keeps keys in d2d.cfg (apps/d2d/keyconfig.hpp:201) |
| `FUN_0046acb0` | `keys_stop` | `DAT_007a7418` = 0; with `DAT_007a7428` 1, cleared and the four key-message handler rows at `0x712944` removed from the window `[0x7c8cbc]` (`FUN_00451e10`, count 4), the ones `keys_start` (`FUN_0046ac70`, ui-module-frontier.md) installs. Only caller `FUN_00456d80` | no counterpart (as `window_control_send`) |
| `FUN_0046bf30` | `throne_entry_check` | In levels 0x83..0x84 (Throne of Destruction, Worldstone Chamber), once (`DAT_007a746c`) while `DAT_007a7468`, with the player: `FUN_004c5420(0, 1)`; elsewhere the flag clears. Callers `FUN_00460e70`, `FUN_0046bff0` | not compared |
| `FUN_0046bfd0` | `countdown_start` | A positive value with none running: `DAT_007a7464` = now (`FUN_00410a80`) + value. Only caller `FUN_004a2cb0` | not compared |
| `FUN_0046c130` | `countdown_draw` | A thunk to `FUN_0046c060`: while the countdown runs, unless its unit is gone, in mode 0xc, or within 0x46 of the end: a string (by id) with the time left drawn at (0x177, 9). Only caller `d2d_panel_draw_order` | not compared |
| `FUN_0046ca60` | `unit_sound_at` | `EDI` unit: a sound request at its path's x, y (`FUN_00648e30`, `FUN_00648ea0`) and the argument (`FUN_004aff60`). Only caller `FUN_0046ce40` | not compared |
| `FUN_0046cab0` | `unit_sound_request` | `ESI` unit: a sound request (seven dwords) at its position (static path for types 2, 4, 5; dynamic otherwise). 4 callers, ported: the ambient sounds `FUN_0046d070`, `FUN_0046d110`, `FUN_0046d280` | not compared |
| `FUN_00473770` | `player_level_flag` | With the client's player: its level's `FUN_0061dc20` byte (after `FUN_0061dba0`) is nonzero. Only caller `FUN_004de730` | not compared |
| `FUN_00473c80` | `get_float_7a89a0` | Returns the float `0x7a89a0`. Callers `FUN_004de730`, `FUN_004e42e0` | not compared |
| `FUN_00473c90` | `set_7a8a10` | Fastcall: `0x7a8a10` = ECX. Only caller `FUN_00460e70` | not compared |
| `FUN_004742d0` | `light_set_radius` | Fastcall (light, radius): a radius above 0 (at most 0x12) becomes `+0x18` and `+0x1c` = radius × 8; a kind-2 light (`+0xc`) also clears `+0x2c` and frees `+0x30` (line 0x14a). Callers `FUN_00460930` and two more | not compared |
| `FUN_00474470` | `light_restart` | Fastcall light (non-null, line 0x18d): `+0x20` = 1, `+0x1c` = 0; kind 2 asserts (line 0x190). Callers `FUN_004d2d70` and one more | not compared |
| `FUN_00475b40` | `reciprocal_table_init` | `FUN_0061af60(light record, FUN_00475930)`, then `0x7b0a68..` = 0x10000 / n for n 1..255. Only caller `FUN_0044e100` | not compared |
| `FUN_00478590` | `cursor_refresh` | `FUN_00478350(&local)`. 4 callers, among them `FUN_00453910`, `FUN_00482ef0`, `FUN_0048a730` | not compared |
| `FUN_0047aa30` | `game_flags_bit8` | Bit 8 of the S->C 0x01 flags `DAT_00712efc`. Callers the join request `d2d_c2s_join_request` and one more | not compared |
| `FUN_0047b4f0` | `chat_line_wrap_draw` | Draws a name and its message, wrapping to x 0x83 one line (0xf) down when either would pass width − 0x109 (`d2d_common_glyph_top`), then `FUN_0047b450`. Only caller the chat draw `FUN_0047b720` | not compared |
| `FUN_0047b5c0` | `chat_numbered_draw` | (From its calls and data:) widens text (`MultiByteToWideChar`) and draws lines with a `%d: ` prefix from `DAT_007bb5f4` relative to the screen height. Only caller `FUN_0047b720` | not compared |
| `FUN_0047dbb0` | `gamemenu_cel_load` | (From its calls and data:) loads `%s\UI\%s\%s` under `DATA\LOCAL` (`FUN_004788b0`, language folder `FUN_00525260`), the LoD path by `FUN_00408f20`. Only caller the game menu's load `FUN_0047dd70` | not compared |
| `FUN_0047dfd0` | `gamemenu_cels_free` | Frees the game menu's DC6s (`0x7bc93c..0x7bc958`, `FUN_0047df00`). Only caller `FUN_00456d80` | no counterpart: d2d's sprites are owned values |
| `FUN_0047e9a0` | `gamemenu_slider_draw` | (From its calls and data:) draws the option slider cels (`d2d_gamemenu_cel`) from `0x7bc898..0x7bc978`. Only caller `FUN_0047f710` | not compared |
| `FUN_004817f0` | `get_7bc9b0` | Returns `DAT_007bc9b0`. 5 sound callers (`FUN_004ba020`, `FUN_004ba8f0`, `FUN_004dcaa0`) | not compared |
| `FUN_00481800` | `get_7bc9b4` | Returns `DAT_007bc9b4`. 4 sound callers | not compared |
| `FUN_00481810` | `get_7bc99c` | Returns `DAT_007bc99c`. Callers `FUN_004dcaa0`, `FUN_004dcd40` and one more | not compared |
| `FUN_00481830` | `sounds_stop_all` | `FUN_004df800`, then `FUN_004baab0`. Callers the game end `FUN_0044f360`, `FUN_00482ef0` | not compared |
| `FUN_00482ca0` | `aux_bnet_bits_save` | (From its calls and data:) reads and writes the registry's `Diablo II\Aux Battle.net` as `%d.%d.%d.%d` with `DAT_00721d98`. Only caller `FUN_00482ef0` | not compared: d2d keeps its unlock bits in d2d.cfg (apps/d2d/frontend.hpp:154) |
| `FUN_00482e30` | `endgame_archive_open` | (From its calls and data:) opens a file under `DATA\LOCAL` in the language folder (`FUN_00525260`, `FUN_004192f0`) from `0x6d41a0` / `0x6d41a4`, closes it (`FUN_004198d0`). Only caller `FUN_00482ef0` | not compared |
| `FUN_00483300` | `set_7bc9d4` | `DAT_007bc9d4` = 1. Only caller `FUN_004a2cb0` | not compared |
| `FUN_00483350` | `get_7bc9d7` | Returns the byte `0x7bc9d7`. 5 callers, ported: `FUN_004a23d0`, `FUN_004a34f0`, `FUN_004a3e40` | not compared |
| `FUN_004834a0` | `item_name_after_tag` | Fastcall wide name: the text after a trailing `[..]` (a `]` with a `[` three characters before), else the whole name. Only caller the item name `FUN_0048c060` | not compared |
| `FUN_004834e0` | `hire_name_compose` | (From its calls:) composes a wide string from parts (`d2d_wide_copy`, `FUN_00526700`). Only caller `FUN_0048be80` | not compared |
| `FUN_00483ab0` | `in_rect_7bcaa0` | Fastcall (x, y): inside `[0x7bcaa0, 0x7bcaa4) × [0x7bcaa8, 0x7bcaac]`. 6 callers, ported: `FUN_00483ff0`, `FUN_004912a0`, `FUN_00492510` | not compared |
| `FUN_00483b10` | `in_rect_7bca34` | `EAX` x, ECX y: strictly inside x `0x7bca34..0x7bca38`, y `0x7bca3c..0x7bca40` inclusive. Callers `FUN_00483ff0` and two more | not compared |
| `FUN_00483b40` | `in_rect_by_version` | `ESI` x, `EDI` y in the classic box (`0x7bcb44..0x7bcb50`) or LoD box (`0x7bca7c..0x7bca88`, by `d2d_is_expansion`), x exclusive at its left. 5 callers, ported: `FUN_00483ff0`, `FUN_00492510` | not compared |
| `FUN_00483ba0` | `in_rect_by_version_b` | As `in_rect_by_version` with the boxes `0x7bca68..0x7bca74` (classic) and `0x7bcaf4..0x7bcb00` (LoD), y from 0. Only caller `FUN_00492510` | not compared |
| `FUN_00483bf0` | `in_rect_7bcb74` | `EAX` x, ECX y inside `[0x7bcb74, 0x7bcb78) × [0x7bcb7c, 0x7bcb80]`. Callers `FUN_00483ff0`, `FUN_004927c0` and two more | not compared |
| `FUN_00484a70` | `item_quality_string` | `EAX` 0..6 (above asserts, line 0x69c): a string by id per value. Only caller the item name `FUN_0048c060` | not compared |
| `FUN_00486980` | `inv_cels_free` | (From its calls and data:) frees the inventory panel's DC6s (`0x7bcc1c..0x7bce7c`), the LoD ones by `d2d_is_expansion`. Only caller `FUN_00456d80` | no counterpart (as `gamemenu_cels_free`) |
| `FUN_00486ae0` | `inv_text_x` | Panel x + 0x113 (classic) or + 0x110 (LoD). Only caller `FUN_0048edf0` | not compared |
| `FUN_00486b30` | `inv_mode_allows_a` | 1 for inventory modes (`DAT_007bcbf0`) 0, 1, 2, 0xb, 0xc, 0xe, 0x13, else 0. Only caller `FUN_0048ffe0` | not compared |
| `FUN_00486b80` | `inv_mode_allows_b` | As `inv_mode_allows_a` with 0xf too. Callers `FUN_00490780`, `FUN_00490ba0`, `FUN_00490fc0` | not compared |
| `FUN_00487a10` | `inv_item_flags_scan` | (From its calls and data:) walks the player's inventory (`FUN_0063b2c0`, `FUN_0063dfa0`, `FUN_0063dfd0`), page by `FUN_00628250`, setting the bytes `0x7bcc04`, `0x7bcc05`, `0x7bcc08`. Callers `FUN_0048edf0` and two more | not compared |
| `FUN_00487b30` | `inv_mode_reset` | Mode 0; `0x7bcc04`, `0x7bce30`, `0x7bca4c`, `0x7bcbd8` cleared; the DC6 `0x7bcb04` freed; `0x7bca48` = 0. Callers `FUN_004b8730` and two more | not compared |
| `FUN_00487b80` | `inv_mode_use_item` | `0x7bcc04` = 0, mode 0x13. Only caller `d2d_use_item_open` | not compared |
| `FUN_00487be0` | `inv_mode_normal` | Four tab slots cleared, mode 1, then `FUN_0063c1e0` (the cursor item) and `FUN_00468070`. Callers `FUN_004917c0` and two more | not compared |
| `FUN_00487c20` | `inv_mode_leave_trade` | From mode 5 only: as `inv_mode_normal`. Callers `FUN_004b2650`, `FUN_004b2e30` | not compared |
| `FUN_00487c60` | `inv_mode_trade_confirm` | Mode 5 (else asserts, line 0x1ea1) to 6. Only caller `FUN_004b2650` | not compared |
| `FUN_00487ca0` | `inv_mode_set6` | 0 when already mode 6, else mode 6 and 1. Only caller `FUN_004b2650` | not compared |
| `FUN_00487cc0` | `inv_tab_last_used` | `0x7bcc40` = the last tab (of `DAT_00722160`) whose slot is set, then as `inv_mode_normal`. Callers `FUN_004b53f0` and two more | not compared |
| `FUN_00487ea0` | `store_tab_reset` | `EAX` tab: its slot's fields reset (`0x7bc9e0..` 20 bytes a tab). Only caller `FUN_00487ed0` | not compared |
| `FUN_004888c0` | `inv_can_close` | Modes 1..4 with no cursor item: 0 when the cursor state is 7, 1 or 4, else 1. Callers `FUN_00488b00` and one more | not compared |
| `FUN_004892f0` | `inv_cels_free_b` | Frees the DC6s `0x7bcb04`, `0x7bcc24`, `0x7bcc28`, and with LoD `0x7bcc38`. Only caller `FUN_00456d80` | no counterpart (as `gamemenu_cels_free`) |
| `FUN_00489840` | `inv_mode_not_123` | 0 in modes 1, 2, 3, else 1. Only caller `FUN_00497a40` | not compared |
| `FUN_00489920` | `in_gold_button` | `ESI` y, `EDI` x in the gold button's box (classic height − 0x103..− 0xf2, LoD − 0x1c7..− 0x1b6, from the panel y; x panel + 0x49..0x96). Callers `FUN_00489ac0`, `FUN_00492510` | not compared |
| `FUN_0048a630` | `inv_mode_normal_msg` | Mode 1, the cursor item, `FUN_00468070`, then `FUN_0049eb10(0x5a)`. Callers `FUN_004b2650` and one more | not compared |
| `FUN_0048a6f0` | `clear_7bce28` | `DAT_007bce28` = 0. Callers `FUN_004b8730` and two more | not compared |
| `FUN_0048bbe0` | `notice_set` | Copies a text, `DAT_007bcea4` = the argument, `FUN_00455f20(0)`. Only caller `FUN_0049f490` | not compared |
| `FUN_0048bc10` | `notice_draw` | (From its calls and data:) while UI state (`FUN_004538d0`) allows, draws a DC6 (`d2d_ui_dc6_load`) and the notice text (`DAT_007bcea4`) at the panel position, by version. Only caller `d2d_panel_draw_order` | not compared |
| `FUN_004aa900` | `skill_points_shown` | 0 with no player or without `FUN_00639df0(player, 0x36)`, else `DAT_007c0c3c`. Only caller `FUN_004abc30` | not compared |
| `FUN_004aaaa0` | `skilltree_text_centred` | A centred glyph line at `0x7a5220` − panel x. Only caller `FUN_004aace0` | not compared |
| `FUN_004ab310` | `skilltree_tooltip_draw` | (From its calls and data:) a box (`d2d_gfx_rect_size_draw`, colour `FUN_004fb180`) and centred strings near the mouse, kept on screen. Only caller `FUN_004ac690` | not compared |
| `FUN_004ab630` | `skilltree_hover_tip` | (From its calls and data:) a string at the mouse (`FUN_00502280`) when it is over a box from `0x724ce0` (`FUN_004ab5f0`). Only caller `FUN_004ac690` | not compared |
| `FUN_004ac740` | `colour_rec_ptr` | Fastcall: a kind-1 record's `+0x14` block + 0x1c, else (null, other kind, no block) its unchecked `+0x14` + 0x1c. Callers the monster colour `FUN_00466360` and one more | not compared |
| `FUN_004ac780` | `colour_rec_set_14` | A kind-1 record's block `+0x14` word = the value. Callers `FUN_00466360` and one more | not compared |
| `FUN_004ac7a0` | `colour_rec_set_26` | Block `+0x26` word = the value. Only caller `FUN_00466360` | not compared |
| `FUN_004ac810` | `colour_rec_flags` | Block `+0x16` word: the bits set or cleared by the third argument. Callers `FUN_00466360` and one more | not compared |
| `FUN_004ac840` | `colour_rec_flag_100` | Block `+0x16` bit 0x100 set or cleared. Only caller `FUN_004aff60` | not compared |
| `FUN_004b1640` | `npc_talk_target` | `EAX` unit: none clears `DAT_007c0d29`; else set, with its id (`+0xc`) and class (`+4`). Callers `FUN_004b6dd0` and one more | not compared |
| `FUN_004b1830` | `npc_line_pick` | Fastcall line id: the table `0x72739c` (`DAT_00725c04` rows) gives a replacement; else the quest's next line (`FUN_00661400`) not already said (the up-to-9 list `0x7c0d9a`), 0xffff when none. Callers `FUN_004b6dd0` and two more | not compared |
| `FUN_004b1980` | `npc_line_said` | A line id onto the said list (at most 9). Only caller `FUN_004b6dd0` | not compared |
| `FUN_004b1a10` | `npc_class_listed` | `EAX` class: 1 for 0x92, 0x20f, 0x219..0x21b, 0x209, 0x10a, 0x17a, 0xfb, 0x179, 0x14b, 0x196, 0x198. Callers `FUN_004b6dd0` and one more | not compared |
| `FUN_004b2110` | `npc_menus_close` | (From its calls and data:) for each open NPC menu flag `0x7c0d5f..0x7c0d7b`, `FUN_004b8020` and clear. Callers `FUN_004b3f10`, `FUN_004b4380`, `FUN_004b4fd0` | not compared |
| `FUN_004b2250` | `npc_menu_strings_init` | (From its data:) fills the NPC menu entries at `0x726cc1..0x726e4f`. Only caller `FUN_004b6dd0` | not compared |
| `FUN_004b23e0` | `npc_ui_init` | Clears the NPC UI state (`0x7c0c68`, 0x160 bytes), two lists (`FUN_0065c430`), loads `ui\panel\buysell` and `ui\menu\goldbtn`. Callers `FUN_00456970`, `FUN_004b32f0` | not compared |
| `FUN_004b24a0` | `npc_ui_end` | (From its calls and data:) closes the NPC menus (`FUN_004b8020`), frees the lists (`FUN_0065c490`) and DC6s. Only caller `FUN_00456d80` | no counterpart (as `gamemenu_cels_free`) |
| `FUN_004b25c0` | `npc_quest_special_3` | Fastcall S->C 0x50 packet (null: the fatal report, line 0xfc6). Acts only on subtype 3 (the u16 at +1), while an NPC is being talked to (`DAT_007c0d29`) and its unit (id `DAT_007c0d25`, type 1, `FUN_00463990`) still exists: `0x7c0d39` = 1 and the u16 at `0x7c0d3d` = 3. Nothing reads either address directly (both are inside the NPC UI state at `0x7c0c68`). Only caller `FUN_004b9210`, for subtypes 3 and 6; on 6 it does nothing | not compared |
| `FUN_004bc480` | `spinner_ctor` | `SpinnerWrapper`: a 0x54-byte spinner (`FUN_004bc340`) with the four arguments at `+8`. Only caller `FUN_00454150` | not compared |
| `FUN_004bdfd0` | `rec_14_plus_28` | Fastcall: `*(arg + 0x14) + 0x28`. Only caller `FUN_00454f30` | not compared |
| `FUN_004be060` | `ui_cel_free_7c546c` | Frees the DC6 `0x7c546c`. Only caller `FUN_00456d80` | no counterpart (as `gamemenu_cels_free`) |
| `FUN_004c5470` | `cast_to_target_in_range` | The target pair (`FUN_004c52e0`); with both set and within 10000 of the player (`FUN_006492a0`): `FUN_004cdb40(x, y, a, b)`. 8 callers, among them `FUN_004f0160` | not compared |
| `FUN_004dbe20` | `cof_frames_256` | A COF request for the unit's token and mode (`d2d_cof_build_params`, `FUN_006001f0`); its frame count × 256. Only caller `FUN_00470390` | not compared |
| `FUN_004dbfe0` | `hold_500ms` | `DAT_007c89cc` = now + 500. Only caller `FUN_004654c0` | not compared |
| `FUN_004dc0a0` | `hold_clear` | `DAT_007c89cc` = 0. Only caller `FUN_00482ef0` | not compared |
| `FUN_004dc510` | `unit_gfx_record_init` | Fastcall unit: a new gfx record (`d2d_gfx_record_new`) at `+0x54`; by unit type 0..2 the mode's graphics load (`d2d_gfx_unit_mode_load`), 3 and 4 nothing; none asserts (line 0x16). 4 callers, ported: `FUN_00465fd0`, the monster colour `FUN_00466360` | not compared |
| `FUN_004dcf70` | `unit_grid_contains` | (`EAX` x, ECX y, grid): inside the view's unit grid (origin `+0xea68`/`+0xea6c`, size `+0xea78` square). Only caller the grid fill `FUN_004dd7c0` | not compared: d2d's `rules::in_unit_grid` (components/rules/automap_scroll.hpp:37) |
| `FUN_004dcfa0` | `unit_grid_add_unit` | Fastcall grid, `EAX` unit: the next of 3000 0x14-byte entries (count `+0xea64`): kind 0, the unit, its position × 8. Only caller `FUN_004dd7c0` | not compared |
| `FUN_004dd550` | `unit_grid_add_tile` | `EDI` grid, `EBX` room: an entry of kind 1 at the room's corner × 5 plus the tile's spot (`FUN_00619730`), × 8; the grid's flag 8; appended to the cell's chain (`+0x10`). Only caller `FUN_004dd7c0` | not compared |
| `FUN_004dd600` | `unit_grid_add_unit_b` | As `unit_grid_add_unit` with `EBX` the grid. Only caller `FUN_004dd7c0` | not compared |
| `FUN_004df6c0` | `sound_env_set` | Fastcall: a new environment (≠ `DAT_0072ade4`): `FUN_00481920`; in sound mode 2 (`FUN_00514c90`) or for 0, with `FUN_00513b70`, `FUN_00513c50`; kept in `DAT_0072ade4`. Callers `FUN_004e42e0` and one more | not compared |
| `FUN_004dfa40` | `sample_busy` | Fastcall: `FUN_00515740` for a sample with a buffer, else 0. Only caller `FUN_004b95c0` | not compared |
| `FUN_004dfa60` | `samples_free_all` | With samples allocated (`DAT_007c8c78`): `FUN_004df800`, each of the 16 slots `0x7c8a78..` freed (`FUN_005160d0`) and cleared. Callers `FUN_004dfaa0`, `FUN_004e0000`, `FUN_004e0050` | not compared |
| `FUN_004dfb50` | `sample_slot_pick` | (From its calls and data:) the 16 slots at `0x7c8a78`: a free one, else one to stop (`sample_stop`) by `stream_samples_left` / `FUN_00515710`. Only caller `FUN_004e01b0` | not compared |
| `FUN_004e4020` | `stub_4e4020` | Returns. Callers `FUN_00480e30`, `FUN_00480e70`, `FUN_00480ec0` | no counterpart (empty) |
| `FUN_004e4100` | `now_plus` | Fastcall n: `FUN_004e40a0()` + n. 4 callers, ported: `FUN_004ba020`, `FUN_004cb460`, `FUN_004cc380` | not compared |
| `FUN_004e4120` | `now_minus` | `FUN_004e40a0()` − n. Only caller `FUN_004e42e0` | not compared |
| `FUN_004e4130` | `sqrt_float` | `sqrt` to float. Only caller `FUN_004dfc20` | no counterpart (the standard library) |
| `FUN_004e4160` | `length_sq` | x² + y² (floats). Callers `FUN_004b9e10`, `FUN_004ba020` and one more | no counterpart (arithmetic) |
| `FUN_004e4240` | `sound_tick_x6` | `FUN_004822b0` six times (three passes of two). Only caller `FUN_004e42e0` | not compared |
| `FUN_004e4280` | `music_state_reset` | Clears `0x7c8c7c`, `0x7c8c90..0x7c8c98`; the two music samples `0x7c8c80`, `0x7c8c84` released (`FUN_004ba840`) with their ids. Callers `FUN_004b9d00`, `FUN_004b9d20` | not compared |
| `FUN_004e4640` | `table_c3c_row` | Fastcall index: the 12-byte row of the data table at data tables `+0xc3c` (count `+0xc34`); out of range the first row. Only caller `FUN_004e4d80` | not compared |
| `FUN_004e4c50` | `itemstat_desc_value` | `EAX` value, `EDI` ItemStatCost row: for a row whose op (`+0x54`) is 2..5, scaled by the player's base stat (`+0x56`, shifted by its row's `+0x18`) and `+0x55`, then by the row's `+0x18`; stat 0x7a on an item with `FUN_00629bb0(item, 0x39)` adds 0x32. Callers `FUN_004e4d80` (twice) | not compared |
| `FUN_004e4ce0` | `itemstat_desc_pair` | (From its calls:) reads a stat pair (`FUN_00625d00`) and scales it through `itemstat_desc_value`. Only caller `FUN_004e4d80` | not compared |
| `FUN_004e6720` | `item_text_stat_line` | (From its calls:) one stat line for the item text: the stat (`FUN_00625790`, `FUN_00626cd0`, `FUN_006277e0`), formatted (`FUN_004e6410`, `FUN_0065fec0`), appended. Only caller `FUN_004e6850` | not compared |
| `FUN_004e6ff0` | `skill_value_for_text` | (From its calls:) the player's skill (`FUN_00639df0`), its stats (`FUN_006256b0`, `FUN_00625d00`) and `FUN_004e6ca0`. 3 callers, among them `FUN_004e86f0` | not compared |
| `FUN_004e72d0` | `skill_length_text` | (From its calls:) a skill's lengths (`FUN_00644d50`, `FUN_00644e40`, `d2d_skills_length_bonus`) into three outputs. 8 callers, among them `FUN_004eddd0` | not compared |
| `FUN_004e7930` | `skilldesc_line_string` | Fastcall: string 0x1506 gives two fixed strings; else a string formatted with the argument (0x100 characters) between two others. Only caller `d2d_skill_text_desc_line` | matches: run inside `tools/emu/skill_tips.py` (ui-module-frontier.md "SkillDesc") |
| `FUN_004efc50` | `missile_row_150` | Fastcall index: the Missiles.txt row's `+0x150` (0x23c bytes a row), the unchecked row for an index out of range. Callers `d2d_rules_find_potion`, `d2d_skills_calc_operand` and one more | not compared |
| `FUN_004f5210` | `get_light_option` | Returns `DAT_0072da50`. 4 callers, ported: `FUN_00473470`, `FUN_00475780`, `FUN_004de730` | not compared |
| `FUN_004f5220` | `set_light_option` | `DAT_0072da50` = the value. Only caller `FUN_0047cf80` | not compared |
| `FUN_004f52a0` | `set_7c8cc4` | Fastcall: `DAT_007c8cc4` = ECX. Only caller `FUN_0045aea0` | not compared |
| `FUN_004f5a30` | `renderer_ready` | A renderer (`DAT_007c8cc0`) and not `DAT_007c8cb4`. Callers `FUN_00468770` and two more | not compared |
| `FUN_004f6f60` | `tile_rle_blit_a` | (From its data:) a DT1 block blitter on the frame buffer (`DAT_007d544c`). Callers `FUN_004f8050` and one more | not compared |
| `FUN_004f7580` | `tile_blit_iso` | (From its data:) a tile blit with clipping against the view (`0x7c97a0`, `0x7ca318`, `0x7d2334`, `0x7d233c`). Callers `FUN_004f8050`, `FUN_004f88b0` | not compared |
| `FUN_004f7850` | `tile_blit_iso_lit` | (From its data:) as `tile_blit_iso` with the light tables (`0x7ca320`, `0x7d2348`). Callers `FUN_004f8120`, `FUN_004f89f0` | not compared |
| `FUN_004f8810` | `tile_block_blit` | A block with flag bit 2 (`+8`) through `tile_rle_blit_a`, else `FUN_004f69b0`, each at `0x7d2348 + (shade >> 3) × 0x40`. Only caller `FUN_004f8b80` | not compared |
| `FUN_004f88b0` | `tile_blit_clip` | (From its calls and data:) clips a tile to the view (`0x72db48`, `0x72db84`, `0x72dbc0`) and draws it (`tile_blit_iso`). Only caller `FUN_004f8b80` | not compared |
| `FUN_004f89f0` | `tile_blit_clip_lit` | As `tile_blit_clip` through `tile_blit_iso_lit`. Only caller `FUN_004f8b80` | not compared |
| `FUN_004f8fc0` | `set_7d55d8` | `DAT_007d55d8` = the value; 1. Only caller the app start `FUN_0044b8a0` | not compared |
| `FUN_004f9130` | `renderer_slot_d4` | The renderer's slot `+0xd4`. Only caller `FUN_0044f360` | not compared |
| `FUN_004f9140` | `set_7d55b0` | `DAT_007d55b0` = the value; 1. 4 callers, among them `FUN_0043b080` | not compared |
| `FUN_004f9160` | `set_7d5594` | `DAT_007d5594` = the value; 1. 5 callers, among them `d2d_frontend_screen_run` | not compared |
| `FUN_004f91c0` | `focus_control_set` | The focused control `DAT_007d55c8`: set; a different one first loses focus (`FUN_004fdcc0`). 18 callers | not compared |
| `FUN_004f9200` | `get_7d55cc` | Returns `DAT_007d55cc`. Callers `FUN_004ff620` and two more | not compared |
| `FUN_00500310` | `anim_state` | Fastcall kind-2 control: `+0x50`. Only caller `FUN_00433bf0` | not compared |
| `FUN_00500340` | `anim_advance` | Kind 2: state 3 becomes 4 (frame 0, tick now), 1; states 2 and 4 return 0; else 1. Only caller `FUN_00433bf0` | not compared |
| `FUN_00500480` | `anim_control_draw` | (From its data:) draws a kind-2 control (asserts). Only caller `FUN_005005b0` | not compared |
| `FUN_00500a70` | `control6_set_rect` | Kind 6: `+0xc..+0x18` = x, y, width, height. Only caller `FUN_0043b080` | not compared |
| `FUN_00500c20` | `button_hit` | `EAX` button, `ESI` x, `EDI` y: inside x `+0xc..+0xc+0x14`, y `+0x10 − +0x18..+0x10` (bottom-anchored). Only caller the button draw `FUN_00500db0` | not compared |

## 0x520000..0x5fffff: Battle.net client, strings, server game

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_005204a0` | `bnet_set_first_once` | Under `0x882004`: `DAT_0088201c` = the value when unset. 8 Battle.net callers | no counterpart: d2d has no Battle.net client |
| `FUN_005206b0` | `bnet_parse_786` | (From its size and caller only: 786 bytes, no calls.) Only caller `FUN_005209d0` (battlenet.md) | no counterpart (as `bnet_set_first_once`) |
| `FUN_00520ad0` | `bnet_flag_set` | `DAT_00882528` = 1. Callers `FUN_0051c470`, `FUN_0051c5c0`, `FUN_0051c7e0` | no counterpart (as `bnet_set_first_once`) |
| `FUN_00520bf0` | `bnet_table_882128` | Fastcall byte: the dword `0x882128[n]`. 6 callers, among them `d2d_bnet_host_ad_result`, `d2d_bnet_read_user_data` | no counterpart (as `bnet_set_first_once`) |
| `FUN_00520c20` | `bnet_table_8822a8` | The dword `0x8822a8[n]`. Callers `d2d_bnet_host_ad_result`, `FUN_0051ca90` | no counterpart (as `bnet_set_first_once`) |
| `FUN_00524930` | `tbl_string_by_index` | Fastcall (.tbl, string table, index): an index at or past the count becomes 500; the hash slot (`+0x15 + index × 2`) to an entry (0x11 bytes) that must be in range and used (`+0` = 1) with a string; else null. Callers the string lookup `FUN_00524a30`, `FUN_00525770` | not compared: d2d reads the .tbl files itself (tbl-strings.md) |
| `FUN_005249e0` | `tbl_key_hash` | The .tbl key hash: h = h × 16 + c, the top nibble folded in (`^ (h >> 24)`), 28 bits; mod the table size. Only caller `FUN_00524c60` | not compared (as `tbl_string_by_index`) |
| `FUN_005262a0` | `utf8_char_count` | Fastcall (text, bytes): characters in the first `bytes` bytes (`FUN_00526200` the length of each, -1 a single byte). Callers `FUN_0049a580`, `FUN_004a7d00` | not compared |
| `FUN_00526540` | `wcs_ncmp` | Wide compare of at most n characters: 0 equal, -1 or 1. 4 callers, ported: `FUN_00501a80`, `FUN_00501df0` | no counterpart (the standard library) |
| `FUN_005266c0` | `wcs_find_char_next` | The first `c` in a wide string, returned only when something follows it. Only caller `FUN_0048be80` | no counterpart (the standard library) |
| `FUN_00526740` | `wcs_ncat` | Appends at most n wide characters and a NUL. Callers `FUN_005269d0` and one more | no counterpart (the standard library) |
| `FUN_00526940` | `wcs_str` | `wcsstr`; null for a null side. 5 callers, ported: `FUN_0048be00`, `FUN_005269d0` | no counterpart (the standard library) |
| `FUN_00527720` | `tbl_buffers_free` | Frees `0x882b28`, `0x882b24`, `0x882b2c` (`delete[]`). Only caller `FUN_00524e40` | no counterpart |
| `FUN_00529f50` | `sha1_init_b` | The SHA-1 initial state and a zero count (the copy beside the SRP code). 7 callers | no counterpart: d2d has no SRP login |
| `FUN_0052a040` | `sha1_final_b` | (From its calls:) the SHA-1 padding and digest through `FUN_00529f80`. 7 callers | no counterpart (as `sha1_init_b`) |
| `FUN_0052be20` | `stub_52be20` | Returns. 6 callers, among them `FUN_0052d440` | no counterpart (empty) |
| `FUN_0052c060` | `client_slot_get` | Fastcall id: under `0x882d18`, the client slot `0x882d34[id]` unless 0 or -1. Callers `FUN_0053eff0` and one more | not compared |
| `FUN_0052c110` | `client_slot_set` | Slot `id` = the value unless it is -1 (reserved). Callers `FUN_00530930` and one more | not compared |
| `FUN_0052c170` | `client_slot_alloc` | (From its calls and data:) under the lock, a free slot of `0x882d34` up to `DAT_00731000`. Callers `FUN_00530930` and one more | not compared |
| `FUN_00535b10` | `player_party_lookup` | Fastcall (game, player): for a player unit, a 2-dword query (`FUN_005538d0`); its first dword. Only caller `FUN_00584870` | not compared |
| `FUN_00537930` | `s2c_send_unit_ref` | `EAX` unit: `FUN_0053b3d0(type, id)`, (6, -1) for none. Only caller `FUN_00539760` | not compared |
| `FUN_00537970` | `client_flag_1` | Fastcall client (asserts): `+0x3d4` bit 0. Callers `FUN_00532590`, `FUN_00569f80` | not compared |
| `FUN_0053f5d0` | `game_arena_type` | The game's arena block's first dword (asserts the block). Only caller `FUN_0053ac70` | no counterpart: d2d has no arena (game-frontier.md) |
| `FUN_0053fa90` | `player_arena_clear` | A player's arena record (player data `+0x34`) `+4` = 0 (asserts, lines 0xc5, 0xc9). Only caller `FUN_00553220` | no counterpart (as `game_arena_type`) |
| `FUN_0053fca0` | `game_arena_flag4` | The arena block's flags `+8` bit 2. Only caller `FUN_0052d440` | no counterpart (as `game_arena_type`) |
| `FUN_00541800` | `event_add_timed_wrap` | `FUN_005416b0(arg2, arg5, arg6, arg7)`. Only caller `FUN_00554ea0` | not compared |
| `FUN_00547300` | `objgroup_list_a_full` | Fastcall (game, level): the level's spawn record (game `+0x10f0` → `+0x48 + level × 4`): list A (`+0x18`, at most 4) full, or its count at or above the record's `+8` / 8. Only caller `FUN_005516c0` | not compared |
| `FUN_00547360` | `objgroup_list_b_full` | As `objgroup_list_a_full` for list B (`+0x14`, at most 10). Only caller `FUN_00552b50` | not compared |
| `FUN_00547390` | `objgroup_count_inc` | The level record's `+0x10` + 1. Only caller `FUN_00552b50` | not compared |
| `FUN_00547400` | `objgroup_list_a_add` | Appends (x, y) to list A (`+0x20`, 8 bytes each) while under 4. Only caller `FUN_005516c0` | not compared |
| `FUN_00547490` | `objgroup_list_b_add` | Appends (x, y) to list B (`+0x40`) while under 10. Only caller `FUN_00552b50` | not compared |
| `FUN_00547e90` | `level_monster_count_dec` | Unless `FUN_00573540`, the level's record (`FUN_00573520`) `+0x2d0` − 1 for a unit that isn't a pet (`FUN_006259b0` 0). Only caller `FUN_005cc960` | not compared |
| `FUN_0054e190` | `spawn_at_free_spot` | A free spot (`FUN_0054ddc0`); found, `FUN_005b3270(x, y, 0, EDI, 1, ESI, 0)`. Only caller `FUN_0054ec90` | not compared |
| `FUN_00552a00` | `unit_free_text_and_events` | Fastcall (game, unit): its overhead text (`+0xa4`) freed (`FUN_006611a0`), `d2d_unit_7c`, `FUN_0054f580`, `FUN_00540ee0`. Only caller the unit removal `FUN_00555600` | not compared |
| `FUN_00552aa0` | `game_value_180x2` | Fastcall game: `+0x180` × 2. Callers `FUN_005858a0` and two more | not compared |
| `FUN_00552ab0` | `return_2` | `return 2`. Callers `FUN_005858a0` and two more | not compared |
| `FUN_00552ee0` | `unit_serial_next` | Fastcall (game, type above 5): the counter `+0x90 + type × 4` + 1, skipping -1 (to 1). Only caller `FUN_00555230` | not compared |
| `FUN_00553060` | `unit_list_insert_point` | (unit, game): the hash list for its type (`0x6e10e0` offsets, id & 0x7f; type 5 `+0x1b20`), walked by id (next `+0xe4`) to the last node with a lower or equal id. Only caller `FUN_00554850` | not compared |
| `FUN_005564a0` | `unique_found_test` | `EAX` game, id up to 0x1000: the found-uniques bit (`+0x1b24`). Only caller `FUN_005566b0` | matches: `found_uniques` (components/rules/rules.cpp:1351) |
| `FUN_00557250` | `item_affixes_clear` | `EDI` item: its prefix and suffix slots 0..2 (`FUN_00627ef0`, `FUN_00627fb0`), the rare names (`FUN_00628010`, `FUN_00628070`), the set or unique id -1 (`FUN_00629df0`). 4 callers, ported: `FUN_005572a0`, `FUN_00557320`, `FUN_00557380` | not compared |
| `FUN_00557ee0` | `item_free_inventory` | Fastcall (game, item): an item with an inventory (`d2d_item_has_inventory`) has it freed (`FUN_00555ae0`, `d2d_inv_free`), then `FUN_00540ee0`. Only caller `FUN_00555600` | not compared |
| `FUN_00557ff0` | `inv_contains_item` | Fastcall (unit, item, inventory or the unit's): the cursor item, or any node of the list. 6 callers, ported: `FUN_00577830`, `FUN_00578050`, `FUN_00579510` | not compared |
| `FUN_00564a00` | `game_slot_init` | (From its data:) fills a record from `0x88c788..0x88caa8` (asserts). Callers `FUN_00530930` and one more | not compared |
| `FUN_00564cb0` | `stub_564cb0` | Returns. Callers `FUN_005335e0` and one more | no counterpart (empty) |
| `FUN_00570e30` | `unit_stats_packet` | (From its calls:) packs a unit's stats into a bit stream (`d2d_bitreader_init`, `FUN_00410eb0`, `lowest_set_bit_index`, the state words `FUN_0063a100`). Callers `FUN_00571f90`, `FUN_00571580` | no counterpart: sends are the netplay code's |
| `FUN_00571580` | `unit_stats_send_both` | `FUN_005711d0`, then `unit_stats_packet` (asserts the unit). Only caller `FUN_00598220` | no counterpart (as `unit_stats_packet`) |
| `FUN_005715a0` | `unit_state_send_b2` | Fastcall unit: the stat sublist flagged 0x80's stat 0xb2, when a valid state, sent as S->C 0x11 (`d2d_scmd_0x11`). Only caller `FUN_00598220` | not compared |
| `FUN_00571620` | `unit_overhead_send` | (From its calls:) the unit's overhead text (`FUN_006611e0`, `FUN_00661220`) sent to the client (`FUN_0053b3d0`, `FUN_0053c750`). 4 callers, ported: `FUN_00571f90`, `FUN_00598220` | no counterpart (as `unit_stats_packet`) |
| `FUN_005736a0` | `monster_free` | Fastcall (game, monster): `FUN_0057c980`; unless its MonStats row's `+0xd` bit 1: its inventory freed, its overhead text, its AI control (type 1), `FUN_005a64d0`, `FUN_00572bc0`, `FUN_00540ee0`. Callers the unit removal `FUN_00555600`, `FUN_00574370` | not compared |
| `FUN_005738d0` | `merc_events_clear` | `FUN_00540e60(2, 0)` and `(3, 0)`. Callers `FUN_00533c70`, `FUN_0053a9b0`, `FUN_0056aa50` and one more | not compared |
| `FUN_00575fa0` | `item_unique_flag4` | Fastcall: an item of quality 7 (unique) whose UniqueItems row has `+0x2c` bit 2. Only caller `FUN_00579510` | not compared |
| `FUN_00597c70` | `merc_send_57` | When `FUN_00573540` and a record (`FUN_005a0120`): S->C 0x57 (`d2d_scmd_0x57`) with its words and a flag test. Only caller `FUN_00598220` | not compared |
| `FUN_0059f8e0` | `missile_free` | Fastcall (game, missile): its data freed (`d2d_missile_data_free`), `FUN_00540ee0` (asserts). Only caller `FUN_00555600` | not compared |
| `FUN_0059fee0` | `missile_send` | (From its calls:) a missile's position and target (`FUN_0064a210`, `FUN_0064a380`, `FUN_0064a3e0`) into a packet (`d2d_scmd_copy_32`). Only caller `FUN_00571f90` | no counterpart (as `unit_stats_packet`) |
| `FUN_005a02e0` | `monster_umod_add` | `EAX` monster, byte mod: the first free of 9 mod bytes (`+0x1c` of its record) set. Only caller `d2d_uniques_roll_boss` | not compared |
| `FUN_005a0930` | `monster_boss_setup` | (From its data, 167 bytes.) Only caller `FUN_005a0c00` | not compared |
| `FUN_005a4390` | `object_blast_3` | `FUN_005a4270(arg, 0, 3)`. Only caller `d2d_objects_take_blast` | not compared |
| `FUN_005a5490` | `unit_hit_request` | The unit cast at (`FUN_00553540`): a 0x70-byte hit with `FUN_0057ec10`'s value, applied (`FUN_0057dbf0`, flags 0x80). Callers `FUN_005a7670`, `FUN_005d9c80` | not compared |
| `FUN_005a6b10` | `monster_mode_flag` | `EAX` mode under 16, `ECX` monster: for a mode row (`0x6e23d0`, 12 bytes) with its third dword and not its second, the MonStats2 mode bit (`+0x104`). 4 callers, ported: `FUN_005a7670`, `FUN_005a7c20`, `FUN_005a7e60` | not compared |
| `FUN_005a6b70` | `monster_step_spot` | (From its calls:) a spot near the cast target (`FUN_00553540`, `FUN_00648a00..b90`, `FUN_00650e30`). Only caller `FUN_005a6d50` | not compared |
| `FUN_005a7b30` | `monster_skill_reset` | `ESI` monster: its skill node 0; unless `EDI` is 0 or 0xc, `FUN_005541b0` then `FUN_005a73e0(1)`. Callers `FUN_005a7c20` and two more | not compared |
| `FUN_005aa5b0` | `ring_target_pick` | (From its calls:) a target in a ring around the unit (`d2d_rules_ring_pick`, `FUN_005aa460`, `FUN_0064a730`). Only caller `FUN_005aa650` | not compared |
| `FUN_005b0d70` | `ai_params_set` | Fastcall: `+0x14`, `+0x18`, `+0x1c` = the three values, each skipped when -0x29a. Callers `d2d_pet_variant`, `FUN_005ca910`, `FUN_005d8570` | not compared |
| `FUN_005bdff0` | `seed_step_table` | Fastcall unit: its seed `+0x6c` stepped (× 0x6ac690c5 + carry); returns `PTR_DAT_007415f0`. Only caller `FUN_0054ec90` | not compared |
| `FUN_005c0d00` | `affix_pick_add` | `EAX` slot under 0x1ff: the pair at `+slot × 8` = (value, weight). Only caller `d2d_rules_pick_affix` | not compared |
| `FUN_005c0d40` | `affix_pick_36` | (36 bytes; read shows no body.) Callers `FUN_005c2d40` and one more | not compared |
| `FUN_005c0d70` | `item_props_apply` | (From its calls:) an affix's properties onto the item's stats (`d2d_rules_socket_props`, `d2d_stat_list_set`, `FUN_006251f0`, `FUN_006257d0`) by its seed. Only caller `FUN_005c1260` | not compared |
| `FUN_005c21d0` | `item_rare_roll` | (From its calls:) the rare item roll: affixes (`d2d_rules_pick_affix`, `FUN_005c1260`, `FUN_005c12f0`, `FUN_005c19a0`, `FUN_005c1ab0`) into the six slots, the names (`FUN_00628010`, `FUN_00628070`). Only caller `FUN_00557450` | not compared |
| `FUN_005c2740` | `set_pick_old` | For an item whose data `+0x30` is 0 (else `FUN_005c25c0`): durability and max durability doubled (at most 255), then from a random start the first Sets row (`+0xc0c`, 0x128 bytes; version under 100 unless the item's is 100+) holding the item's base: set id, flag 0x10, `FUN_0065fec0(4, …)`, 1; none: 0. Only caller `FUN_005c2940` | not compared: d2d's set pick follows `FUN_005c25c0` (components/rules/rules.cpp:1351) |
| `FUN_005c2fb0` | `item_low_quality_if_v0` | An item whose data `+0x30` is 0: `FUN_005c2af0`, then `FUN_005c2d40`. Only caller `FUN_00557450` | not compared |
| `FUN_005c2ff0` | `paladin_blast_eighth` | Fastcall (_, unit): a player of class 3 takes a hit (flags 0x1000, 4) of max life / 8 (`d2d_objects_take_blast`, `FUN_0057cee0`). Callers `FUN_005c56c0` and one more | not compared |
| `FUN_005c31f0` | `unit_can_be_raised` | A unit not a pet, with `FUN_0056e2f0`, no `FUN_005541b0`, in a room without `FUN_0061ab00`, and `FUN_00554200`. Callers `FUN_005c3bcc` and one more | not compared |
| `FUN_005dd330` | `monster_owner_set` | Fastcall (monster, owner or null): the AI record (`+0x14` → `+0x28`) `+0xc` = owner id, `+0x10` = owner type; none: -1 and 1. 4 callers, ported: `FUN_005a0c00`, `FUN_005c55c0`, `FUN_005d8570` | not compared |
| `FUN_005fd7b0` | `monster_skill_from_row` | Fastcall (monster, 0): its MonStats row's skill (`+0x10c`) as its skill node; the row's `+0x10b` (under 16). Only caller `FUN_005ccb10` | not compared |
| `FUN_005fe060` | `clear_88cb0c` | `0x88cb10`, `0x88cb0c` = 0. Only caller `FUN_00477c00` | not compared |

## 0x600000..0x6cffff: tables, stats, rooms, DRLG, CRT

The table loaders below read their .txt through `FUN_006122f0` (the
column list given); d2d reads the same files with its own loaders.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_006104a0` | `stpl_list_ctor_a` | (From its calls:) a list object (`FUN_0060f9d0`), emptied (`d2d_stpl_list_unlink_all`). Only caller `d2d_ignorelist_ctor` | no counterpart: d2d has no Battle.net ignore list |
| `FUN_00610b30` | `stpl_list_ctor_b` | (From its calls:) `FUN_0060ecb0`, `FUN_006100b0`. Only caller `d2d_ignorelist_ctor` | no counterpart (as `stpl_list_ctor_a`) |
| `FUN_00611760` | `code4_pad_a` | Copies a code of up to 4 characters into `EAX`, padding with spaces. Only caller `FUN_006119f0` | matches: d2d reads a calc name by its first four characters (components/rules/skills.cpp:25) |
| `FUN_006117d0` | `table_96bcac_get` | The dword `0x96bcac[n]` below the count `0x96bcb4`, else 0. Callers `FUN_00664860`, `FUN_00664930` | not compared |
| `FUN_0061b530` | `call_61b4f0` | `FUN_0061b4f0()`. Callers `FUN_0061b6f0` and one more | not compared |
| `FUN_0061b640` | `room_tile_ref_inc_if_zero` | (rooms, level, …, room): the room entry (`FUN_00642bb0`, `FUN_00642c30`; a room from another level counts as none); an entry with count `+0xe` 0 gets `FUN_0061b490`. Only caller `FUN_0061a070` | not compared |
| `FUN_0061b690` | `room_tile_ref_dec` | As `room_tile_ref_inc_if_zero`: a nonzero count drops by one, then the hook `0x744398` and `FUN_0061b5b0`. Only caller `FUN_0061a0c0` | not compared |
| `FUN_0061b7b0` | `room_entry_or_default` | `FUN_00642c30(arg, 0, 0)`, else `FUN_0061b730()`. Only caller `FUN_0061a140` | not compared |
| `FUN_0061b840` | `room_active_list` | Fastcall: when both lists (`+0xbc`, `+0x1a8`) are their own sentinels or empty, the second's `+0x30`. Only caller `FUN_0061a360` | not compared |
| `FUN_0061b870` | `room_active_list_b` | As `room_active_list` for (rooms, room) when the room's `+0x44` is clear. Only caller `FUN_0061a3a0` | not compared |
| `FUN_0061bac0` | `room_flag_400000` | Fastcall (room, on): `+0x28` bit 0x400000 set for 0, cleared otherwise. Only caller `FUN_0061aed0` | not compared |
| `FUN_0061bae0` | `room_byte_44` | The byte `+0x44`. Only caller `FUN_0061a440` | not compared |
| `FUN_0061bdf0` | `light_step_set` | `EAX` light record: by its kind (`+0`) and whether `+0x30`, the step from the table `0x7443f0` or `0x744480` (12 bytes a kind): `+4` the rate, `+8` = `+0x28` × the factor. Callers `FUN_0061c240`, `FUN_0061c450` | not compared |
| `FUN_0061bec0` | `env_free` | Frees an environment block (`Env.cpp`, line 0x13f). Only caller `FUN_0061afd0` | not compared |
| `FUN_0061c540` | `levels_txt_load` | Loads Levels.txt (5669 bytes of column list: LevelName, LevelWarp, EntryFile, MonLvl1..3(Ex), MonDen, MonUMin/Max, NumMon, objects, warps…). Only caller the table load `FUN_00619300` | not compared: d2d's own Levels.txt reader |
| `FUN_0061dc60` | `table_c58_free` | Frees the data table at `+0xc58`, `+0xc5c` = 0. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_00624bd0` | `stat_list_insert_index` | `EAX` stat list, `EDI` key: binary search of the 8-byte entries (count `+4` word): the insert point, -1 if present. Only caller `FUN_00624c80` | not compared |
| `FUN_00624c10` | `stat_list_find_index` | The same search: the index of the key, else the first larger (or -1 past the end). Only caller `FUN_00626110` | not compared |
| `FUN_00627220` | `stat_set_if_unit` | `FUN_00627170(…)` for a non-null unit. Callers `FUN_0056bec0`, `FUN_0065c940` and one more | not compared |
| `FUN_006272e0` | `stat_lists_expire_flag4` | For a stat block with bit 2: each list flagged 4 has its state cleared (`d2d_unit_set_state_checked`) and, unless bit 31, is freed (`FUN_00626c00`); the bit cleared. Callers `FUN_004aff60`, `FUN_00624690` | not compared |
| `FUN_00627410` | `stat_block_clear_100` | The stat block's bit 0x100 cleared; with bit 31, its 0x80 sublist through `FUN_00627340`. Only caller `FUN_00553220` | not compared |
| `FUN_006276c0` | `socket_stats_merge` | (From its calls:) merges a stat list into another (`FUN_00625150`, `FUN_00626200`). Only caller `d2d_rules_takes_socket` | not compared |
| `FUN_00627810` | `stat_add_signed` | `FUN_006270b0(a, b, c, 0)`. Only caller `d2d_town_flash` | not compared |
| `FUN_00627af0` | `missile_row_194_ref` | Fastcall: the Missiles.txt row's `+0x194` index checked against `+0xb94`. Only caller `FUN_0062cbe0` | not compared |
| `FUN_00627c20` | `stat_calc_eval` | Runs the calc bytes at data tables `+0x98` + offset (`d2d_skills_eval_calc`, callbacks `0x627b40`, `0x74463c`) with two arguments; 0 past the end. 5 callers, among them the potion rate `d2d_rules_potion_rate` | not compared |
| `FUN_00639ee0` | `state_words_clear` | The unit's state words (`FUN_00625be0`) zeroed ((state count + 31) / 32 dwords). Only caller `FUN_00553220` | not compared |
| `FUN_00639f20` | `state_words_any` | Whether any state word is set. Callers `FUN_00598220` and one more | not compared |
| `FUN_0063a100` | `state_words_get` | `FUN_00625bb0()`. Callers `unit_stats_packet`, `FUN_005d6410` | not compared |
| `FUN_0063a2d0` | `state_set_test_dc` | `FUN_0063a180(arg, data tables +0xdc)`. Only caller `FUN_005d5220` | not compared |
| `FUN_0063a360` | `state_set_test_a` | `d2d_state_words_intersect` with a fixed set. Only caller `FUN_005d6410` | not compared |
| `FUN_0063a4a0` | `state_flag_test` | (unit, state): the state's bit in data tables `+0x100`, or `+0x104` for a monster; 0 out of range. 7 callers, ported: `FUN_0056e900`, `FUN_0057ac50`, `FUN_0057af80` | not compared |
| `FUN_0063a570` | `state_set_test_b` | `d2d_state_words_intersect` with one of the state sets (`0x63a570..0x63a670` are nine such tests). Callers the hireling and character panels `FUN_0048a990`, `FUN_004a7d00` | not compared |
| `FUN_0063a590` | `state_set_test_c` | As `state_set_test_b` | not compared |
| `FUN_0063a5b0` | `state_set_test_d` | As `state_set_test_b` | not compared |
| `FUN_0063a5d0` | `state_set_test_e` | As `state_set_test_b` | not compared |
| `FUN_0063a5f0` | `state_set_test_f` | As `state_set_test_b`; only `FUN_004a7d00` | not compared |
| `FUN_0063a610` | `state_set_test_g` | As `state_set_test_b` | not compared |
| `FUN_0063a630` | `state_set_test_h` | As `state_set_test_b` | not compared |
| `FUN_0063a650` | `state_set_test_i` | As `state_set_test_b` | not compared |
| `FUN_0063a670` | `state_set_test_j` | As `state_set_test_b` | not compared |
| `FUN_0063a910` | `grid_fill_rect` | Fills a rectangle of a dword grid (row stride `+8` bytes × 4, data `+0xc`) with a value. Only caller `FUN_0063afd0` | not compared |
| `FUN_0063e490` | `inv_slot_item_by_mode` | (inventory `0x1020304`, …, slot under 0xd): for an empty out and a slot entry (`FUN_0063add0(0x74479c)`): slot 4 or 5 with node kind 2 gives the hand's item. 11 callers, ported: `FUN_00560cd0`, `FUN_00560f00`, `FUN_005616a0` | not compared |
| `FUN_0063ebd0` | `unit_gear_max_light` | A player's 16 composite pieces (`FUN_00664860`) other than `lit `, `med `, `hvy `: the largest Items.txt `+0x12f` among them. Only caller `FUN_00460930` | not compared |
| `FUN_00640ee0` | `table_96d470_free` | Frees `0x96d470`. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_00640f00` | `table_96d478_free` | Frees `0x96d478`. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_00640f20` | `objgroup_txt_load` | Loads objgroup.txt (ID0..7, DENSITY0..7, PROB0..7, SHRINES, WELLS). Only caller `FUN_00619300` | not compared: d2d's own objgroup reader (components/game/objgroups.cpp) |
| `FUN_006412f0` | `objgroup_row` | Row n (0x34 bytes) of `0x96d478`, null past the count. Only caller `FUN_00552610` | not compared |
| `FUN_00641320` | `shrines_txt_load` | Loads shrines.txt (rarity, view name, niftyphrase, effectclass, LevelMin, duration, reset time). Only caller `FUN_00619300` | not compared |
| `FUN_00641500` | `shrines_count` | Returns `DAT_0096d46c`. Callers `d2d_gamedata_object_seed`, `FUN_0054f9d0` | not compared |
| `FUN_00641510` | `shrines_free` | Frees the shrines table. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_00641890` | `units_within_range` | (From its calls:) two units' positions compared with a range. Only caller `FUN_00584240` | not compared |
| `FUN_00643580` | `missile_param_by_level` | Fastcall (missile, level ≥ 1): `+0x160 + (level − 1) × +0x164` of its Missiles.txt row; 0 otherwise. Only caller `d2d_skills_calc_operand` | not compared |
| `FUN_0064a0e0` | `unit_stamp` | `d2d_unit_stamp_by_type` for a non-null unit. Only caller `FUN_004aff60` | not compared |
| `FUN_0064a1c0` | `missile_set_14` | A missile's data `+0x14` = the value. Only caller `d2d_ai_andariel_missile` | not compared |
| `FUN_0064a1e0` | `missile_set_c` | The word `+0xc`. Callers `FUN_004cd540`, `d2d_ai_andariel_missile` | not compared |
| `FUN_0064a240` | `missile_set_range` | The word `+0xa`, clamped to 0..0x7fff. Same callers | not compared |
| `FUN_0064a5f0` | `missile_set_8` | The word `+8`, clamped to −0x8000..0x7fff. Callers as above | not compared |
| `FUN_0064a690` | `missile_set_4c` | The missile unit's word `+0x4c`, clamped likewise. Callers `FUN_004cd540` and two more | not compared |
| `FUN_0064a850` | `stat_chance_rolls` | `ESI` unit: rolls stat 0x151 (% on its seed `+0x20`), else stat 0x8d's chance, then (with a target) `FUN_00645830`'s chance, each `rand % 100 <` the value. Only caller `FUN_0064b860` | not compared |
| `FUN_0064aba0` | `stat_scaled_value` | (From its calls:) a stat scaled by `d2d_rules_mul_div`. Only caller `FUN_0064b860` | not compared |
| `FUN_0064c1b0` | `path_free_part` | (163 bytes, asserts.) Only caller `FUN_0064c370` | not compared |
| `FUN_0064c450` | `unit_path_free` | A unit of a dynamic-path type: `FUN_0064fc20` on its path, `FUN_0064c370` and the path's first word cleared. Callers `FUN_004c1910`, `FUN_005431f0`, `FUN_005433f0` and one more | not compared |
| `FUN_0064c580` | `room_tile_flags_or` | (From its calls:) ORs a tile's subtile flags (`tile_get_28_ptr`) into the collision grid. Only caller `FUN_0064c860` | not compared |
| `FUN_0064c640` | `room_tile_flags_andnot` | As `room_tile_flags_or`, clearing. Only caller `FUN_0064c790` | not compared |
| `FUN_0064caa0` | `coll_cell_clear` | (x in arg, `EBX` y, `AX` mask): the room's collision grid (`d2d_room_collision_grid_get`) cell's mask bits cleared; the cell. Only caller `FUN_0064dc00` | not compared |
| `FUN_0064cff0` | `coll_cell_clear_rec` | Clears a cell and its neighbours through `FUN_0064cce0`, recursing `FUN_0064cdf0` − 1 times. 3 callers, among them `FUN_0064dc00` | not compared |
| `FUN_0064efa0` | `unit_coll_move` | A non-null unit: `FUN_0064ec10(unit, a, b, mask, pattern)`, then `d2d_grid_set_pattern_bits` at the new spot. 3 callers, among them `FUN_0064ff90` | not compared |
| `FUN_00650de0` | `seed_from_time` | (time + tick count + n) through three LCG steps (× 0x19660d + 0x3c6ef35f), 31 bits. 4 callers, ported: `FUN_0052c280`, `FUN_00552df0` | not compared: d2d seeds from its own clock |
| `FUN_00650e60` | `seed_set` | The two seed dwords = the arguments. Only caller `FUN_005d5220` | not compared |
| `FUN_00650e70` | `seed_get` | The two seed dwords out. Only caller `FUN_005d5220` | not compared |
| `FUN_00650eb0` | `code4_pad_b` | As `code4_pad_a`. Callers `FUN_00654440` and one more | not compared |
| `FUN_00650f00` | `montype_chain_a` | (From its calls and data:) walks MonType rows (`0x96c6a8`) through `FUN_0066a9b0`. Only caller `FUN_00651210` | not compared |
| `FUN_00651040` | `montype_depths` | For each MonType row, its chain to the root (`+4` parent, at most 256): the depth into each row on the way (`+0x4b`) and the length (`+0x4a`). Only caller `FUN_00651210` | not compared |
| `FUN_0065a400` | `monsters_tables_load` | Loads the monster tables in order (MonStats via `FUN_00651210`, `d2d_monlvl_load`, `d2d_monequip_load`, `d2d_monseq_load`, `d2d_monsounds_load`, `d2d_monumod_load`, `d2d_superuniques_load`, `d2d_hireling_load`, `d2d_npc_load`, `d2d_treasure_classes_load`, `d2d_monpreset_load`, …) and the unique name columns. Only caller `FUN_00619300` | not compared |
| `FUN_0065a590` | `units_room_near` | (unit, unit): 1 when the second's room is among the rooms near the first's (`d2d_act5_rooms_near`); asserts (lines 0x18, 0x19). Callers `d2d_c2s_merc_item` and one more | not compared |
| `FUN_0065b660` | `inventory_txt_load` | Loads inventory.txt (grid, belt, gloves, feet and the other boxes' left / right / top / bottom / width / height). Only caller `FUN_00619300` | not compared |
| `FUN_0065c130` | `inventory_free` | Frees the inventory table. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_0065c180` | `inventory_rect` | (page, resolution, out): row page + res × 16 (0xf0 bytes)'s first rectangle; asserts (lines 0x7f, 0x8c). Only caller `FUN_004835b0` | not compared |
| `FUN_0065c270` | `inventory_box_rect` | As `inventory_rect` for box n (five dwords at `+(n × 5 + 10) × 4`). Only caller `FUN_004835b0` | not compared |
| `FUN_0065c730` | `stat_param_rec` | By a kind in `ESI`: 0 `FUN_0065c660`, 1 the 16-byte entry n (under 12) + 0xc, 2 and 5 `FUN_0065c6d0`, 3, 4, 6, 7 the record itself; others assert. Only caller `FUN_0065fec0` | not compared |
| `FUN_0065c7a0` | `param_split_9_5` | A packed param: low 9 bits and the next 5. Only caller `FUN_0062cbe0` | not compared |
| `FUN_0065c7d0` | `param_split_9_5_8_8` | 9, 5, 8 and 8 bits (from bits 0, 9, 14, 22). Only caller `FUN_0062cbe0` | not compared |
| `FUN_0065ca30` | `stat_param_decode` | (193 bytes, no calls.) 5 callers, ported: `FUN_00485ee0`, `FUN_004e4d80`, `FUN_00626200` | not compared |
| `FUN_0065cb10` | `param_split_9_5_7` | 9, 5 and 7 bits. Only caller `FUN_0062cbe0` | not compared |
| `FUN_0065cb60` | `param_split_class_tab` | The low 5 bits as (/ 3, % 3), the next 5 bits. Only caller `FUN_0062cbe0` | not compared |
| `FUN_0065cbf0` | `stat_param_check` | (176 bytes, asserts.) 14 callers, ported: `FUN_0065ea50`, `FUN_0065f6a0`, `FUN_0065f870` | not compared |
| `FUN_00660290` | `belts_txt_load` | Loads belts.txt (numboxes, box1..16 left / right / top / bottom). Only caller `FUN_00619300` | not compared |
| `FUN_00660c60` | `belts_free` | Frees the belts table. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_00660d10` | `belt_box_rect` | (belt, resolution, out, box): row belt + res × 7 (0x108 bytes)'s box n rectangle; asserts (lines 0x79, 0x7a, 0x8f). 4 callers, among them `d2d_belt_key_labels` | not compared |
| `FUN_00661220` | `overhead_byte_8` | The byte `+8` of an overhead record. Callers `FUN_004a0b30` and one more | not compared |
| `FUN_00664530` | `table_96c7ec_free` | `FUN_006bd0b0(0x96c7e8)`, frees `0x96c7ec`. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_006649b0` | `experience_loss_txt_load` | Loads a table with PlayerDeath, PlayerDeathPercent, PlayerKill, PlayerKillPercent, MonsterKill, MonsterDeath, Suicide and an arena column. Only caller `FUN_00619300` | not compared |
| `FUN_00664af0` | `table_96d608_free` | Frees `0x96d608`. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_00664b80` | `chartemplate_txt_load` | Loads chartemplate.txt (skills 1..9 and their levels, hit points, mana regen, attack and other rates, velocity, right skill…). Only caller `FUN_00619300` | not compared |
| `FUN_00665740` | `table_96d600_free` | Frees `0x96d600`. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_00668510` | `code4_pad_c` | As `code4_pad_a`. Callers the cube recipe compilers `d2d_cube_compile_cube_input`, `d2d_cube_compile_cube_output` | not compared |
| `FUN_006685c0` | `cube_token_number` | `ESI` cursor, `EAX` separators: the token up to a separator as a number (`atol`), the cursor past it (null at the end). Only caller `d2d_cube_compile_cube_output` | not compared |
| `FUN_0066a1a0` | `table_96d628_free` | Frees `0x96d628`. Only caller `d2d_tables_unload_all` | no counterpart |
| `FUN_0066a200` | `cube_recipe_count` | Returns `DAT_0096d62c`. Only caller `d2d_cube_cube_recipe` | not compared |
| `FUN_0066a270` | `stub_66a270` | Returns. Only caller `FUN_00619300` | no counterpart (empty) |
| `FUN_0066a280` | `stub_66a280` | Returns. Only caller `d2d_tables_unload_all` | no counterpart (empty) |
| `FUN_0066ab70` | `level_rooms_set_8` | Fastcall (level, value): every room on its list (`+0x4c`, next `+4`) gets `+8` = value. Only caller `FUN_00619560` | not compared |
| `FUN_0066ab90` | `room_neighbours_flag_a` | Fastcall (level, room): for the room's entry, its list at `+0x14` loses flag 8 and its list at `+0xc` gains it. Only caller `FUN_0061ac50` | not compared |
| `FUN_0066abe0` | `room_neighbours_flag_b` | As `room_neighbours_flag_a` with the lists swapped. Only caller `FUN_0061ac90` | not compared |
| `FUN_0067f6f0` | `outdoor_presets_a` | `FUN_0067f470` with seven preset ids (0x18b, 0x19b, 0x191, 0x192, 0x18f, 0x18e, 0x193), mode 0. Only caller `FUN_0067f980` | not compared: d2d places outdoor presets (components/drlg/outdoor.hpp:1040) |
| `FUN_0067f740` | `outdoor_presets_b` | 0x18b, 0x19b, 0x190, 0x18e (mode 0) and 0x194..0x197 (mode 1). Same caller | not compared (as `outdoor_presets_a`) |
| `FUN_0067f7d0` | `outdoor_presets_c` | 0x19b, 0x18f, 0x18e, 0x193 (mode 0), then 0x18b twice (mode 1). Same caller | not compared (as `outdoor_presets_a`) |
| `FUN_0067f830` | `outdoor_presets_d` | 0x19d, 0x198, 0x199, 0x19a (mode 0). Same caller | not compared (as `outdoor_presets_a`) |
| `FUN_0067f870` | `outdoor_presets_e` | 0x18b, 0x190, 0x18e, 0x194, 0x195 (mode 0), then 0x19b twice (mode 1). Same caller | not compared (as `outdoor_presets_a`) |
| `FUN_00680a70` | `outdoor_grid_step` | (From its calls:) `FUN_0067c8e0` over the level's cells. Only caller `FUN_00680c80` | not compared |
| `FUN_006815c0` | `outdoor_cell_match` | `EAX` cell at (`EDI`, `EBX`): a level preset (`FUN_0066b980`) without flag 0x200 (`FUN_0067c570`), then a list node at the same spot. Only caller `FUN_00681630` | not compared |
| `FUN_00681d27` | `crt_exit` | The CRT's `doexit`: the atexit and terminator tables (`d2d_crt_decode_pointer`), then `ExitProcess`. Callers the exits `FUN_00681e09` and two more | no counterpart (the CRT) |
| `FUN_00683006` | `crt_ftol` | The CRT's float-to-integer rounding helper. Callers `d2d_click_spot_click_spot` and one more | no counterpart (the CRT) |
| `FUN_00684782` | `crt_fread` | `fread_s` with no buffer limit. 5 callers | no counterpart (the CRT) |
| `FUN_00687ac0` | `crt_math_a` | A CRT math wrapper: the SSE2 path when the FPU and MXCSR masks allow, else the x87 one. Callers the sound mix `FUN_00516830` and two more | no counterpart (the CRT) |
| `FUN_00687c10` | `crt_math_b` | As `crt_math_a` with two operands (`pow`-style). 5 callers, among them `FUN_00516830`, `d2d_palette_make_ramp` | no counterpart (the CRT) |
| `FUN_00687ea0` | `crt_math_c` | As `crt_math_a`. Only caller `FUN_005165f0` | no counterpart (the CRT) |
| `FUN_00688590` | `crt_math_d` | As `crt_math_a`. Only caller `d2d_light_act4_light` | no counterpart (the CRT) |
| `FUN_006886c0` | `crt_math_e` | As `crt_math_a`. Only caller `d2d_light_act4_light` | no counterpart (the CRT) |
| `FUN_0068abac` | `crt_fputc` | The CRT's character output to a stream. 10 callers | no counterpart (the CRT) |
| `FUN_0068b5a3` | `crt_output` | The CRT's formatted output engine (`printf` core, locale). 8 callers | no counterpart (the CRT) |
| `FUN_006bcac0` | `slot_pool_reset` | Fastcall pool not locked (`+0x108`): next -1, counts 0, `+0x118` = 0x8000, each slot's first word 0. 5 callers, ported: `FUN_00472590`, `FUN_004726f0`, `FUN_00472c20` | not compared |
| `FUN_006bcc10` | `slot_pool_get` | Fastcall (pool, handle): the slot (high word index under `+0x100`) whose serial matches the low word (0 any); asserts (lines 0xdb, 0xde). Only caller `FUN_006bccd0` | not compared |
| `FUN_006bccc0` | `slot_pool_lock_reset` | `+0x108` = 1, then `slot_pool_reset`. Only caller `FUN_00472320` | not compared |
| `FUN_006c0b80` | `calc_stack_push` | `EAX` stack: pushes while under 0x40 (count `+0x100`). Only caller `d2d_skills_eval_calc` | not compared |
| `FUN_006c0ba0` | `calc_stack_pop` | Pops, 0 when empty. Only caller `d2d_skills_eval_calc` | not compared |
| `FUN_006c7130` | `bnet_chat_decode` | (421 bytes, no calls.) Only caller `d2d_bnet_chat_command` | no counterpart (as `bnet_set_first_once`) |
| `FUN_006c7960` | `bnet_chat_call` | `FUN_006c78c0()`. Only caller `d2d_bnet_chat_command` | no counterpart (as `bnet_set_first_once`) |
| `FUN_006c79c0` | `warden_inflate` | A zlib-style inflate (`FUN_00429a10(0x38)`, `FUN_00429bb0`, `FUN_0042b170`): -3 on a data error, else its result and the size. Only caller `d2d_warden_load_module` | no counterpart: d2d has no Warden client |
| `FUN_006c9a38` | `str_len` | `strlen`. Callers `d2d_units_ds1_units` and one more | no counterpart (the standard library) |
| `FUN_006ca700` | `log_lock_init` | The log lock `0x75e838` and an `atexit`. Callers `FUN_00681c95`, `FUN_006ca740` | no counterpart |
