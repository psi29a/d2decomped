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
| `FUN_0046acb0` | `hint_reset` | `DAT_007a7418` = 0; with `DAT_007a7428` 1, cleared and window control 4 released. Only caller `FUN_00456d80` | no counterpart (as `window_control_send`) |
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
