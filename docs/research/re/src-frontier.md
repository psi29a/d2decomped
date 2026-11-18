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
| `FUN_00435e00` | `frontend_ret1_00435580` | `FUN_00435580`, then 1. Only caller `FUN_00439840` | not compared |
| `FUN_00437a20` | `realm_known` | `EDI` name: 1 when it matches (case-insensitive, `FUN_00413590`) one of the realm names, else 0. Callers `FUN_0043a0d0`, `FUN_0043b080` | no counterpart (as `realm_count`) |
| `FUN_00437d20` | `char_select_strings_init` | Once (`DAT_00779da8`): seven strings converted, then the 0x80-word slots from `0x779df0` to `0x77abf0` filled (`FUN_005264f0`), 1 at the first failure. Only caller `FUN_00438ad0` | not compared |
| `FUN_00437f60` | `char_class_name` | `EAX` class 0..6: that class's string (other values a default) copied out. Callers `FUN_004380f0`, `FUN_00438560`, `FUN_00439210` | not compared |
| `FUN_0043a2b0` | `realm_pick` | For a Battle.net game (`DAT_007795ec` 1): loads the realms; none shows an error (`FUN_0043b9a0`); else the registry's `Diablo II\Preferred Realm` when it's in the list, else the gateway's realm (`BNGatewayAccess::Realm`), else the first, saved back to the registry; 0 only for the error. Only caller `FUN_0043ae30` | no counterpart (as `realm_count`) |
| `FUN_0043b7a0` | `bnclient_attach` | Fastcall screen: when `+0x225` is unset, the Battle.net interface (`FUN_00518c00`) into `DAT_0077abf0` and `+0x225`, its first virtual call, 1. Only caller `d2d_frontend_screen_run` | no counterpart (as `realm_count`) |
