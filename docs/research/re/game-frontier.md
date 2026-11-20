# GAME frontier — game.exe 1.14d

The 23 `GAME` rows of `build/gaps/functions.tsv` with `frontier = true` on
2026-11-18: untouched functions that a function d2d ports calls. Each row
gives the reading from game.exe and what it means for d2d at that call
site. Re-check a reading with `tools/ghidra/fn.py <address>`, the call
site with `tools/ghidra/calls.py <caller> --to <address>`. Read from the
decompile and, where the decompiler drops registers, the disassembly
(`Disasm`); no oracle was run for these rows.

**d2d** column: *matches* (d2d does the same at that site), *no
counterpart* (d2d has no such state or step, with why), *not compared*
(the reading stands, d2d's side wasn't followed).

## The client's pet list (`0x7bb5bc`)

A node: `+4` PetType.txt row, `+8` the pet's unit id, `+0xc` its owner's
unit id, `+0x24` the pet's colour record (`FUN_00478e40` returns the
node + 0x24), `+0x30` next. S->C 0x7a adds and removes nodes. A null
owner is matched as id -1.

## The event lists (Event.cpp, game `+0xb8`)

The game's `+0xb8` points at a 0xa20-byte block (`FUN_00541470`, line
0x327): `+0` this frame's bucket (game frame `+0xa8` & 63), five unit
classes (unit type → class through the dword table `0x6e0b9c`), each with
64 timed lists at `+4 + (class * 0x40 + bucket) * 4` and an untimed list
at `+0xa04 + class * 4`; `+0xa18` the walk's next node, `+0xa1c` the node
blocks. A node is 0x30 bytes: `+0` event id, `+2` flags (1 running, 2
free, 4 untimed, 8 removed while running), `+4` frame (-1 untimed),
`+0x14`/`+0x18` arguments, `+0x1c`/`+0x20` next / previous, `+0x2c` a
callback (null: the class's table). `FUN_005414d0` runs, per class 2, 0,
1, 3, 4, the untimed list then this bucket's timed list.

## Rows in address order

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_0044dae0` | `client_fps` | Returns `DAT_007a04a8`, the frames drawn over the last 3 s / 3. Only caller `FUN_00475780` picks the light quality from it (≤ 9, ≤ 12, ≤ 15) | matches: `rules::LightQuality::fps` (components/rules/light.hpp:135) |
| `FUN_0044dc80` | `set_expansion_flag` | Fastcall `CL` → `DAT_007a04f4`, the dword `FUN_0044dcc0` (is_expansion) reads. Only caller the S->C 0x01 handler `0x45c8b0`, with packet byte +6 (disassembly: `MOV CL,[ESI+0x6]`) | no counterpart: d2d's 0x01 (apps/d2d/netgame.cpp:291) keeps difficulty and the flags only; a joined d2d takes expansion from its own character |
| `FUN_00451330` | `lng_dll_load` | Loads the language DLL: the registry's `Diablo II\LNG File` path (re-saved when set), else the one `FUN_00414e50` reads back, else `D2.LNG`; none fails the start (returns 0). Then `LoadAcceleratorsA(dll, 100)` into `DAT_007a0708` and `FUN_0041c2f0(0xfff, dll, 0)`. Only caller the app start `FUN_0044b8a0` | no counterpart: d2d reads its strings from the MPQ tables and loads no DLL |
| `FUN_00451620` | `message_record_save` | Writes `Record.dr1`: dword 0x14, then 20 bytes {1, block count, message count (last block's count + 500 per earlier block), client frame `FUN_0044da90`, `FUN_0061aac0(DAT_007a0634)`}, then each block's count × 16 bytes. Blocks are 0x1f4c bytes from `0x7a0818`: 500 records (frame, msg, wParam, lParam), `+0x1f40` count, `+0x1f48` next. Called on leaving a game while recording (`FUN_0044f360`, gated by `FUN_00451910`) and by `FUN_00451920` | no counterpart: d2d has no window-message recorder |
| `FUN_0045f9f0` | `unit_server_cmd_free` | When the unit has a server-command block (`d2d_unit_get_server_cmd`), frees it and its data (`FUN_0040b3c0`, lines 0x1516, 0x1517) and clears it. Only caller the client unit free `FUN_00465870` | no counterpart: d2d's units own their state |
| `FUN_00476140` | `view_scroll_reset` | Fastcall view (asserts non-null, line 0x1dd). When the view's `+0xc` or `+0x10` is set, re-centres the automap (`FUN_004ddb70`) unless the automap scroll mode is 3, then clears the view's bit 0. Only caller `FUN_0044c990`, after the view's corner is set | not compared |
| `FUN_004776c0` | `set_gamma` | Fastcall: `d2d_renderer_set_gamma(ECX)`. Callers the Gamma option `FUN_0047d230` (after saving `Diablo II\Gamma`) and `FUN_0044d760` | matches: the Gamma row drives the palette (apps/d2d/gamemenu.cpp:72) |
| `FUN_00477750` | `driver_is_3d` | `DAT_00712ccc > 3`: the light path's driver copy is 3dfx, OpenGL, D3D or Rave (bugs.md 106). Only caller the unit draw `FUN_00470ec0`: a unit in a state of the set at data tables `+0x154` (`FUN_0063a790`) loses its item colours (`d2d_cof_layer_tint`), except the client's own player on a 3D driver | not compared: d2d has the software path, where only the exception's other half applies |
| `FUN_00478150` | `load_bar_width` | Returns `DAT_007bb3b4`, the loading bar's fill 0..256 (`FUN_00478130`: done × 256 / total). The loading screen `FUN_004565e0` draws it 16 px high from the screen's centre − 0x80, in modes 6..9 | no counterpart: d2d draws no loading screen |
| `FUN_00478ee0` | `pets_count_of_type` | Fastcall (owner unit, PetType row): the pet-list nodes with that row and owner. Callers the portraits' icon-type-2 count `FUN_00493eb0` and `FUN_004b3610` | matches: the portraits count the pets of each icon-type-2 row (apps/d2d/session/draw.cpp:832) |
| `FUN_00478f70` | `owner_has_pet` | Fastcall (owner unit): 1 when any pet-list node has that owner. Only caller `FUN_00493340`, whether UI 0x13 (the portraits) stays up | matches: `portraits_shown` (apps/d2d/session/draw.cpp:770) |
| `FUN_00478fa0` | `pets_recolour_type4` | For each pet-list node of PetType row 4 whose unit the client knows, `FUN_00478e70` (whether the pet should take the viewer's colouring) then `d2d_unit_gfx_set_34` / `_38`. Only caller S->C 0x75's roster change `FUN_0047a850` | not compared |
| `FUN_00479180` | `pet_type_of` | Fastcall (unit id): the PetType row of the pet-list node with that id, 0 for none. Callers the monster colour `FUN_00466360` (row 7 with a colour record: `FUN_004b1090`) and `FUN_004c5fa0` (PetType `+4` bit 8) | not compared |
| `FUN_0053a620` | `act_rooms_send_units` | Fastcall (game, client's unit, room): when the act's room list is in use, for each room near `room` (`d2d_act5_rooms_near`, asserts the unit, line 0x57) `FUN_0053a5d0` sends its units to the client: player `FUN_00580860`, monster `FUN_00598220`, object `FUN_00581ad0`, item `d2d_unit_mode3_notify_if`. Only caller the arrival `FUN_005380d0` | no counterpart: d2d's own client reads the world; a host's sends are the netplay code's |
| `FUN_0053b0e0` | `bits_write_varint` | Fastcall (bit writer, value): below 0x100 a 0 bit and 8 bits; below 0x10000 bits 1, 0 and 16 bits; else bits 1, 1 and 32 bits. Only caller S->C 0x3e (item stat) `FUN_0053d130` | no counterpart: d2d sends no 0x3e (it splits one: components/net/d2gs/split.cpp:49) |
| `FUN_0053f500` | `game_arena_free` | Frees the game's arena block `+0x1d28` (line 0x40) and clears it. Only caller the game free `FUN_0052c7f0` | no counterpart: d2d has no arena |
| `FUN_0053f530` | `player_arena_alloc` | Allocates 8 zeroed bytes (line 0x47) as the player data's `+0x34` (asserts the player data, line 0x4a). Only caller the player init `FUN_005348c0` | no counterpart: d2d has no arena |
| `FUN_00540bb0` | `event_block_alloc` | A 0x7088-byte node block (line 0xa9), zeroed: 600 nodes of 0x30 chained by `+0x1c`, the free head `+0x7080`. Callers `FUN_00541470` (the first) and `FUN_00540c30` (a new one when no block has a free node) | no counterpart: d2d keeps a unit's timers on the unit |
| `FUN_00540f60` | `events_run_class0` | `EAX` list, `EBX` event block, `ECX` game, arg timed. Untimed: runs each node (flag 1 while running) and drops one flagged 8; timed: runs only nodes whose frame is the game's and drops each (`FUN_00540cd0`). A null callback goes through `FUN_00581220`: the table `0x6e1810` (15 entries) | no counterpart (as `event_block_alloc`); the order is ported (components/game/fight/missiles.cpp:820) |
| `FUN_00541160` | `events_run_class3` | As `events_run_class0` with the default `FUN_00586ad0`: the object event table `0x6e19b0` (asserts `IsBadCodePtr`, line 0x240) | as `events_run_class0` |
| `FUN_00541400` | `events_free` | Frees each node block (line 0x314) and the 0xa20 block (line 0x317), clears game `+0xb8`. Callers the game free `FUN_0052c7f0` and `FUN_00541470` | no counterpart |
| `FUN_005415e0` | `event_add_untimed` | `EBX` event id (< 15), `EAX` unit, `EDI` game: a node (`FUN_00540df0`) flagged 4, frame -1, callback and both arguments, pushed on the unit class's untimed list. Callers `FUN_00541650`, `FUN_00541680` and `FUN_005416b0` with frame -1 | no counterpart (as `event_block_alloc`) |
| `FUN_005b1a20` | `targets_free` | Fastcall (game, unit): for a player or monster whose `+0xd0` isn't 0xb, frees the target list at game `+0x10f8 + (its +0xd0) * 4` (line 0xe8), setting each listed unit's `+0xd0` to 0xb. Callers the unit removal `FUN_00555600` and `FUN_005b1b70` | no counterpart |
