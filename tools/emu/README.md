# emu: game.exe as the ground truth

Runs game.exe's own code (1.14d) in an emulator, so d2d's version of a
function can be checked against the real one. No Windows or wine needed.

## Run it

```sh
cd tools/emu
uv sync                                   # once
uv run python diff_drlg.py 1-20 2         # d2d's Blood Moor vs game.exe's, map seeds 1..20
```

Good: `level 2: 20/20 seeds match game.exe`.

## Why it exists

- d2d must match game.exe exactly (levels, drops, monster AI, ...).
  Reading decompiled code gets close; running the real code proves it.
- The dump oracles (levels, drops, objects) print game.exe's answer in
  the same text form a small d2d tool prints, so a plain diff finds the
  first difference. The AI checks (sight.py, search.py, moves.py,
  missiles.py) compare against line-for-line copies in-process and print
  `ok`.

## The checks you'll use most

| Checks | Command (in tools/emu) | Good output |
|---|---|---|
| The ambient's dip to black, a frame at a time (FUN_0046ae50, FUN_0046ad10, the fill FUN_00474550 flag 1) | `set -o pipefail; uv run python ambient_dip.py gen 20000 7 > cases.tsv && uv run python ambient_dip.py game cases.tsv > g.txt && ../../build/tests/test_ambient_dip cases.tsv \| diff g.txt -` | no output |
| The animated image control (D2WinAnimImage.cpp FUN_00500850: its fields, draw mode 0 stored as 5, on all 15 kind-3 screen records) | `uv run python anim_image.py` | `ok` |
| Application mode from the command line (Main.cpp, FUN_00406340: split on `-`, case-sensitive name prefix, client never stored, last match wins) | `uv run python app_mode.py` | `ok` |
| Automap top-right lines (FUN_00459ff0 EXPANSION, FUN_0045a0a0 version, FUN_0045a190 difficulty: string ids, colour, line step, right edge) | `uv run python automap_lines.py` | `ok: 15 automap top-right line runs` |
| Automap cell trees (UI\automap.cpp: FUN_00457c30, FUN_00457b00 insert and drop, FUN_00458440 / FUN_00458470 count and walk, FUN_00458680, FUN_00458cf0, the group table and option defaults of FUN_0045a4c0; frozen as tests/automap_tree_cases.inc) | `uv run python automap_tree.py` | `ok: 600 automap tree runs, group table, defaults, layer map and free checked` |
| Hover label placement (UI\\ui.cpp FUN_00453060: centre on the anchor, clamp to the screen, clear the buffer) | `uv run python ui_label.py` | `ok: FUN_00453060 placed 2004 labels as the clamp rule says, cleared the buffer, and ignored an empty one` |
| UI state functions (UI\\text.cpp / ui.cpp: FUN_0049f900, FUN_004a0000, FUN_0049f8c0, FUN_0049f960, FUN_004a0680, FUN_004a0770, FUN_00453d90, FUN_00453de0, FUN_00453ee0: call order, arguments, globals) | `uv run python ui_state.py` | `ok: FUN_0049f900, 004a0000, 0049f8c0, 0049f960, 004a0680, 004a0770, 00453d90, 00453de0, 00453ee0 call sequences and globals checked` |
| Client DC6 load and free helpers (CORE\ARCHIVE.CPP: FUN_004788b0, FUN_00478950, FUN_00478a00) | `uv run python archive_dc6.py` | `ok: 9 DC6 load/free cases; 68 + 8 call sites pass flag 0` |
| Which self states a new one switches off (States.txt group) | `uv run python armor_group.py game > g.txt; ../../build/tests/test_armor_group --armor-group \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| Client UI and input helpers (FUN_00469aa0, FUN_0046a530, FUN_00477730, FUN_004f6190, FUN_0049e3a0, FUN_00466730: key lookup and name dispatch, driver option, frame pacing with a scripted clock, chat line queue, ambient critter make; callees hooked as listed in the file; `--mutation-check` 30 ways) | `uv run python ui_client_helpers.py` | `ok: 3563 client helper cases over 6 functions` |
| A unique's, champion's and minion's spawn stats (FUN_005a2120 and the mods' inits: life, level, experience, damage, speed, resists, enchanted damage and cold length, mana burn) | `set -o pipefail; uv run python umod_stats.py 100000 1 > game.tsv && ../../build/tests/test_umod_stats --umod-stats game.tsv > d2d.tsv && uv run python umod_stats.py --compare game.tsv d2d.tsv` | the fields d2d models: `cases N`, then each field that differs (a minion has no mods in d2d: its rows differ) |
| A Lightning cast on a pack in its line (one unit a frame, the last unit skipped: FUN_005ae1f0, FUN_005a8730, FUN_005adf10) | `set -o pipefail; ../../build/tests/test_light_pierce --light-hit 1 400 > d2d.txt && grep -q '^T' d2d.txt && uv run python light_pierce.py d2d.txt > g.txt && grep -E '^[HT]' d2d.txt \| diff g.txt -` | no output |
| A skill's aurafilter on a monster (FUN_0056b3e0 whole: the modes, unit flags 0x80 / 0x400, town 0x100, sides and the npc mask 0x6ce268 under 0x10000, 0x20000, state 0x56, the clear line 0x200; its predicates hooked) | `set -o pipefail; uv run python aura_filter.py gen 200000 1 > cases.tsv && uv run python aura_filter.py game cases.tsv > g.txt && ../../build/tests/test_aura_filter cases.tsv \| diff g.txt -`; `inc` writes `tests/aura_filter_cases.inc` (gen 2000 1) | no output |
| Shift-click to the belt (C->S 0x63: Beltable, inventory page, the box) | `uv run python auto_belt.py gen 100000 1 > cases.tsv; uv run python auto_belt.py game cases.tsv > g.txt; ../../build/tests/test_game --auto-belt cases.tsv \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| Belt step of a pickup (which box, or none) | `uv run python belt_pickup.py gen 100000 1 > cases.tsv; uv run python belt_pickup.py game cases.tsv > g.txt; ../../build/tests/test_game --belt-pickup cases.tsv \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| A layer's frame block in the sprite buffer (FUN_005feb80: no cell at or past byte +4, the direction choice, the block walk; FUN_00600c90 hooked) | `uv run python sprite_frame_block.py --mutation-check` | `ok: 30000 FUN_005feb80 cases (...)`, then `mutation check ok` |
| A composite layer's tint colormap (FUN_005038d0: stored byte, Transform 0 as 8, none for colour > 20 and Transforms 3, 4; d2d's `tint_of`); `inc` writes `tests/tint_of_cases.inc` | `uv run python tint_colormap.py --mutation-check` | `ok: 520 FUN_005038d0 cases (...)`, then `mutation check ok` |
| Battle.net CACHE.CPP, bncache.dat: CRC, crypt table, name hash, cipher, time, hash find/delete, path, open (codes 1-12), read, write, close, rebuild (16 functions, FUN_00518de0 ... FUN_00519db0; `--mutation-check` 11 ways) | `uv run python bncache.py` | `ok: 1165 CACHE.CPP cases over 18 checks` |
| Battle.net gateway parser and lifecycle (synthetic I/O) | `uv run python bnetgw.py` | `ok: 63 BNetGW parser, access and lifecycle checks` |
| Three server-side bugs of game.exe (bugs.md #111 to #113: S->C 0x16's list a player short, FUN_00537d10 / FUN_00537ee0; the game-name suffix, FUN_0052f940; S->C 0x7e's unset bytes, FUN_0053db70) | `uv run python net_join_bugs.py` | `ok: 0x16 sends [1, 1, 2, 4] of 1, 2, 3, 5 players; first suffix abc~<the name's address>; 0x7e is 7eaaaaaaaa` |
| BNNews cache save/load and array growth (synthetic I/O and heap) | `uv run python bnnews_cache.py && uv run python bnnews_array.py` | `ok` |
| BNNews initialization and request packet (cache and socket scripted) | `uv run python bnnews_init.py` | `ok` |
| BNNews 0x46 reply merge, cutoff and callback order (cache writer hooked; signed count guard) | `uv run python bnnews_reply.py` | `ok` |
| Battle.net message queue (four BnMessQueue.cpp routines, stock x86 with allocator and locks scripted) | `uv run python bnqueue.py` | `ok: queue FIFO, zero length, limits, string truncation, clear and reuse` |
| Battle.net BnSend.cpp, the 28 packet senders, reply copiers and readers (FUN_0051ac20 ... FUN_0051be20; `--mutation-check` 11 ways) | `uv run python bnsend.py` | `ok: 2016 BnSend cases over 23 checks` |
| A boss's death (MonUMod 22 by class, FUN_005dfbf0's area, pick and delay, mod 21's FUN_005a3aa0) | `uv run python boss_deaths.py` | `ok` |
| Missile velocity, skill interpolation wrapper, physical-damage roll and aurastats loop (FUN_00663270, FUN_00645c10, FUN_0056e170, FUN_005c6cc0; callees hooked as listed in the script; `--mutation-check` 16 ways) | `uv run python skills_combat_helpers.py` | `ok: 105 synthetic cases over 4 functions` |
| When a Sorceress self cast pays its mana and runs its action frame (with a get-hit cut) | `uv run python cast_mana.py gen 1 > cases.tsv; uv run python cast_mana.py game cases.tsv > g.txt; ../../build/tests/test_cast_mana --cast-mana cases.tsv \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| Cast speed and cast length (FCR 0..400) | `uv run python cast_rates.py > g.txt; ../../build/tests/test_combat --cast-rates \| diff g.txt -` | no output |
| DC6 / DCC cel placement | `uv run python cel_anchor.py \| diff - ../../tests/cel_anchor_cases.inc`; then `test_cel_anchor` | no output; the test passes |
| Chat IME helpers (UI/chat.cpp: FUN_0047ad60, FUN_0047b010, FUN_0047b070) | `uv run python chat_ime.py` | `123 cases: candidate list refresh ...`, then `ok` |
| Overhead message node (Chat/Chat.cpp: FUN_00661110, FUN_006611a0) | `uv run python chat_node.py` | `30 nodes: time = min(length, 254) x 8 + 125 (2157 frames at 254 and above), ...`, then `ok` |
| GAME Clients.cpp client records: field accessors, id and name table lookups, save upload buffer, packet queue, client constructor (30 functions and the getter at 0x538230: FUN_005381f0 ... 00539a30; `--mutation-check` 62 ways) | `uv run python clients_small.py` | `ok: 56770 CLIENTS record, lookup, upload and constructor calls checked` |
| COF draw effects (which blend each is) | `uv run python cof_effects.py \| diff - ../../tests/cof_effect_cases.inc`; then `test_cof_effects` | no output; the test passes |
| Room collision-grid release (FUN_0064ca10) | `uv run python collisn_release.py` | `ok: null room, null room1, one release, and repeat release` |
| COF name builder and its wrappers (FUN_0064f5b0, FUN_0064f380, FUN_0064fa00; callees hooked and logged) | `uv run python composit_cof.py` (`--mutation-check` for the model defects) | `ok: FUN_0064f5b0 1772 cases, FUN_0064f380 392, FUN_0064fa00 208; e.g. 1h1js` |
| Body-armour layers and tiers (FUN_0064f420, FUN_0064f500, FUN_0063d690) vs compcode.hpp | `uv run python composit_layers.py` | `ok: lit layers TR=torso LG=legs RA=rArm LA=lArm S1=rspad S2=lspad; 10 other layers fatal; d2d's compcode tables match` |
| Inventory panel decisions (FUN_00483c50, FUN_004844a0, FUN_00486d10, FUN_00486da0, FUN_00486e60, FUN_00486ef0, FUN_00487000) | `uv run python d2common_inventory_ui.py` | `ok: 6814 inventory-ui cases` |
| Inventory mode, grid-cell and panel hit-test helpers (FUN_00483e10, FUN_00486bd0, FUN_00486e10) | `uv run python d2common_item_ui.py` | `ok: 37 stock D2COMMON item UI helper cases` |
| Level layouts, monsters, objects | `uv run python diff_drlg.py 1-20 2 [tiles\|monsters\|objgroups\|collision\|game\|npcs]` | `N/N seeds match game.exe` |
| Drlg.cpp act and level helpers (FUN_00642190, 006427a0 ... 00643020; `--mutation-check` 3 ways) | `uv run python drlg_act.py` | `ok: 720 Drlg.cpp act and level helper cases` |
| The five large DRLG generators run for real and their output diffed against d2d's `drlg-dump` (FUN_00665950, 00667970, 00675850, 006719f0, 00677880; needs `D2_BUILD` with tools/drlg-dump and tools/mpq-cat; `--mutation-check` 3 ways) | `D2_BUILD=<build> uv run python drlg_generators.py` | `ok: FUN_00665950 entered ...x, ...; port output equals game.exe on every row` |
| Geometry, direction and list-link helpers (12 functions, FUN_00666170 ... FUN_0067d7e0; `--mutation-check` 8 ways) | `uv run python drlg_geom.py` | `ok: 1200 DRLG geometry, direction and list-link helper cases` |
| DrlgGrid.cpp grid helpers (18 functions, FUN_0067c430 ... FUN_0067ce00; `--mutation-check` 4 ways) | `uv run python drlg_grid.py` | `ok: 1440 DrlgGrid.cpp helper cases` |
| Drlg.cpp level teardown and room-at-point search (FUN_00642010, FUN_00642c30; `--mutation-check` 3 ways) | `uv run python drlg_level.py` | `ok: 240 Drlg.cpp level teardown and room search cases` |
| Drlg.cpp tile cache, neighbour free, knockback, LvlSub loader and sweep, A* box (6 functions, FUN_00642a30 ... FUN_0067ad00; `--mutation-check` 8 ways) | `uv run python drlg_mid.py` | `ok: 900 Drlg.cpp tile cache and neighbour free cases` |
| Maze room preset picker and prescribed-neighbour helper (FUN_006709b0 for LevelTypes 0..0x25 and out-of-range values, every link mask and both flags; FUN_00670de0 with its room callees hooked; `--mutation-check` 9 ways) | `uv run python drlg_room_helpers.py --exe /path/to/game.exe` | `ok: 13392 DRLG room helper cases; branches {...}` |
| Record, table and packed-point helpers (16 functions across Preset, DrlgRoom, DrlgLogic, Maze; `--mutation-check` 3 ways) | `uv run python drlg_misc.py` | `ok: 1600 small DRLG helper (records and tables) cases` |
| Record, teardown and list helpers, round two (10 functions across Preset, DrlgRoom, DrlgLogic, Maze, Level; FUN_006660b0 ... FUN_0067d680; `--mutation-check` 6 ways) | `uv run python drlg_more.py` | `ok: 800 DRLG record, teardown and list helper cases` |
| Outdoors.cpp, level-link and distance helpers (15 functions, FUN_00673fe0 ... FUN_00678d30; `--mutation-check` 3 ways) | `uv run python drlg_outdoor.py` | `ok: 1200 Outdoors.cpp and distance helper cases` |
| DRLG path helpers, missile offset and OutPlace edge borders (8 functions, FUN_00675de0 ... FUN_0067a9f0; `--mutation-check` 7 ways) | `uv run python drlg_path.py` | `ok: 1600 DRLG path and edge helper cases` |
| Preset.cpp POP updater (FUN_006671e0; `--mutation-check` 3 ways) | `uv run python drlg_pops.py` | `ok: 300 Preset.cpp POP updater cases` |
| Preset.cpp and RoomTile.cpp small helpers (15 functions, FUN_00666610 ... FUN_0066f380; `--mutation-check` 3 ways) | `uv run python drlg_preset.py` | `ok: 1200 Preset.cpp and RoomTile.cpp helper cases` |
| Preset.cpp file cache, release, level init, POP locations and POP tile refresh (6 functions, FUN_00665f40 ... FUN_00668300; `--mutation-check` 5 ways) | `uv run python drlg_preset_mid.py` | `ok: 1200 Preset.cpp file cache and POP tile cases` |
| Preset.cpp object presets (3 functions, FUN_00666220, FUN_00666990, FUN_006663c0; `--mutation-check` 3 ways) | `uv run python drlg_presets.py` | `ok: 600 Preset.cpp object preset cases` |
| DrlgRoom.cpp helpers (16 functions, FUN_0066b4f0 ... FUN_0066c0b0; `--mutation-check` 4 ways) | `uv run python drlg_room.py` | `ok: 1280 DrlgRoom.cpp helper cases` |
| Small DRLG helpers (19 functions of Drlg.cpp, DrlgRoom.cpp, RoomTile.cpp, Preset.cpp, DrlgGrid.cpp; FUN_00642380 ... FUN_0067c570; `--mutation-check` 8 ways) | `uv run python drlg_small.py` | `ok: 1520 small DRLG helper cases` |
| Ten more DRLG functions (animation frame copy FUN_0066d750, corner border FUN_00675f80, Act 2 placement check FUN_00676fc0, substitution groups FUN_0066f990, LvlSub stamps FUN_006707a0, maze room allocation FUN_00670d70 / 00670e70 / 00673b10, preset spawn FUN_0067db00, path smoothing FUN_00679b30; `--mutation-check` 44 ways) | `uv run python drlg_finish.py` | `ok: 2010 more small DRLG function cases` |
| RoomTile.cpp counters, tile frees and LvlSub tile-type match (6 functions, FUN_0066d290 ... FUN_0066fe00; `--mutation-check` 6 ways) | `uv run python drlg_tiles.py` | `ok: 1200 RoomTile.cpp counter and LvlSub match cases` |
| Act IV opening alert (`FUN_005b3750`; flag test hooked) | `uv run python quest_act4_open_alert.py --exe /path/to/game.exe` | `ok: 24 Act IV opening alert cases (flag test hooked)` |
| Terror's End object initializer (`FUN_005b5840`; callees hooked) | `uv run python quest_diablo_object_mode.py --exe /path/to/game.exe` | `ok: 10 Terror's End object initialization cases (callees hooked)` |
| Izual search gate (`FUN_005b43f0`; callees hooked) | `uv run python quest_izual_nearby.py --exe /path/to/game.exe` | `ok: 18 Izual search gate cases (callees hooked)` |
| Diablo Act IV share gate (`FUN_005b4da0`; callees hooked) | `uv run python quest_diablo_act_share.py --exe /path/to/game.exe` | `ok: 72 Diablo Act IV share cases (callees hooked)` |
| Hellforge alert gate (`FUN_005b5ee0`; flag test hooked) | `uv run python quest_hellforge_alert.py --exe /path/to/game.exe` | `ok: 486 Hellforge alert cases (flag test hooked)` |
| Item drops | `uv run python drops.py 1-20000` (also `tables`, `items`, `objects`, `stands`) | `N/N ... match game.exe` |
| Dungeon.cpp field accessors and forwarding wrappers (52 functions, FUN_00619550 ... FUN_0061afa0; null asserts, callee arguments; `--mutation-check` 5 ways) | `uv run python dungeon_accessors.py` | `ok: 1248 Dungeon.cpp accessor and wrapper cases` |
| Dungeon.cpp room1 node list, sorted set, teardown and bit table (FUN_0061a270 ... FUN_0061afd0; `--mutation-check` 4 ways) | `uv run python dungeon_lifecycle.py` | `ok: 312 Dungeon.cpp room1 node, set, teardown and bit-table cases` |
| Dungeon.cpp room1 geometry, searches, near-array build and creation (FUN_00619800 ... FUN_00619d00; `--mutation-check` 4 ways) | `uv run python dungeon_rooms.py` | `ok: 720 Dungeon.cpp room1 geometry, search and creation cases` |
| A potion's end percent, the globe preview S->C 0x18 carries (FUN_005485b0 life, FUN_00548640 mana: 32-bit wraps, the low byte) | `set -o pipefail; uv run python end_percent.py gen 100000 7 > cases.tsv && uv run python end_percent.py game cases.tsv > g.txt && ../../build/tests/test_hud_globe cases.tsv \| diff g.txt -` | no output |
| Gfx.cpp action dispatch FUN_0046f950 (death predicate and dispatch hooked; `--mutation-check` 3 ways) | `uv run python engine_action.py` | `ok: 24 ENGINE action dispatch cases` |
| GfxUtil armor tier output FUN_004daab0 (equipment and table lookups hooked; `--mutation-check` 3 ways) | `uv run python engine_armor_tiers.py` | `ok: 128 ENGINE armor tiers cases` |
| GfxUtil cel visibility FUN_004dab40 (metric getters hooked; `--mutation-check` 3 ways) | `uv run python engine_cel_bounds.py` | `ok: 512 ENGINE cel bounds cases` |
| Gfx.cpp blocking COF preload FUN_0046fb60 (lookups, loaders and cel calls hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_blocking.py` | `ok: 1000 ENGINE COF blocking cases` |
| Unit COF box tests FUN_00470860, FUN_004709a0 (node and header lookups hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_box.py` | `ok: 3000 ENGINE COF box cases` |
| COF cache start / stop and preload timer FUN_00470200, FUN_00470290, FUN_00470350 (pool, tick and workers hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_cache.py` | `ok: 1500 ENGINE COF cache cases` |
| COF cache loaders FUN_0046dcc0, FUN_0046ddf0 (pool, file loader, name hash hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_file.py` | `ok: 1600 ENGINE COF file cases` |
| Monster mode COF preload FUN_0046f650 (path, cache, request and cel calls hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_layers.py` | `ok: 800 ENGINE COF layers cases` |
| COF load wrappers FUN_0046e8d0, FUN_0046e980, FUN_0046ea50 (transform, tables, loader hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_load.py` | `ok: 432 ENGINE COF load wrapper cases` |
| Composite COF loader FUN_0046e740 (path, node, cache and entry calls hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_node.py` | `ok: 800 ENGINE COF node cases` |
| COF parameter builder FUN_004db7b0 (every lookup and file setter hooked; `--mutation-check` 4 ways) | `uv run python engine_cof_params.py` | `ok: 3000 ENGINE COF params cases` |
| Monster COF path FUN_0046f490 and life tint FUN_0046ed50 (token record, transform, palette hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_path.py` | `ok: 1600 ENGINE COF path cases` |
| Mode loaders and preload walkers FUN_0046f9e0, FUN_0046fa80, FUN_0046fda0, FUN_0046feb0, FUN_0046ff50 (loaders and queue hooked; `--mutation-check` 4 ways) | `uv run python engine_cof_preload.py` | `ok: 2000 ENGINE COF preload cases` |
| Type-token cache FUN_004da790, monster COF preload FUN_0046f870, layer parameters FUN_004db2e0 (record lookups hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_request.py` | `ok: 1500 ENGINE COF request cases` |
| Near-room COF preload walk FUN_00470070 (room, queue and workers hooked; `--mutation-check` 3 ways) | `uv run python engine_cof_rooms.py` | `ok: 1200 ENGINE COF rooms cases` |
| GfxUtil direction conversion FUN_004db290 (lookup hooked; `--mutation-check` 3 ways) | `uv run python engine_direction.py` | `ok: 512 ENGINE direction conversion cases` |
| Gfx.cpp record allocation FUN_0046ebb0 (pool allocator hooked; `--mutation-check` 3 ways) | `uv run python engine_gfx_alloc.py` | `ok: 24 ENGINE graphics allocation cases` |
| Gfx.cpp frees and bundle open FUN_0046eb70, FUN_0046ec10, FUN_0046f370, FUN_0046f3f0, FUN_0046f440, FUN_0046f310, FUN_0046e150 (pool and loaders hooked; `--mutation-check` 6 ways) | `uv run python engine_gfx_free.py` | `ok: 672 ENGINE graphics free cases` |
| Small Gfx.cpp / GfxUtil.cpp helpers: COF list, layer flags, overlay test, cache getters, COF header layer lookups, token and row helpers, the unused list helpers (FUN_0046e100 ... FUN_0046f9d0, FUN_004da720 ... FUN_004db1c0; `--mutation-check` 11 ways) | `uv run python engine_gfx_small.py` | `ok: 816 ENGINE Gfx.cpp / GfxUtil.cpp helper cases` |
| Item cel draw FUN_0046ee80 (request, load, draw and overlay calls hooked; `--mutation-check` 3 ways) | `uv run python engine_item_cel.py` | `ok: 1500 ENGINE item cel cases` |
| Item cel name FUN_004dabc0 and transparency FUN_004db360 (item tables hooked; `--mutation-check` 4 ways) | `uv run python engine_item_gfx.py` | `ok: 1600 ENGINE item gfx cases` |
| GfxUtil layer predicates FUN_004dae60, FUN_004daf70 (row lookups hooked; `--mutation-check` 3 ways) | `uv run python engine_layer_info.py` | `ok: 1200 ENGINE layer info cases` |
| GfxUtil layer token and tint FUN_004dad80, FUN_004db570 (equipment and item lookups hooked; `--mutation-check` 3 ways) | `uv run python engine_layer_token.py` | `ok: 3000 ENGINE layer token cases` |
| GfxUtil base mode token FUN_004daa00 (player/object lookups hooked; `--mutation-check` 3 ways) | `uv run python engine_mode_token.py` | `ok: 120 ENGINE mode token cases` |
| Unit motion record (ENGINE\\Particle.cpp, FUN_004da000 ... FUN_004da6e0 incl. the per-tick step FUN_004da350, FUN_0046f060, FUN_0046f070; the pool, owner and screen-conversion callees hooked; `--mutation-check` 7 ways) | `uv run python engine_motion.py` | `ok: 1048 unit motion record cases` |
| GfxUtil death-mode direction update FUN_004db1e0 (transform and path hooks; `--mutation-check` 3 ways) | `uv run python engine_death_direction.py` | `ok: 96 ENGINE death direction cases` |
| Gfx.cpp overlay clearing FUN_0046f170 (removal hooked; `--mutation-check` 3 ways) | `uv run python engine_overlay_clear.py` | `ok: 24 ENGINE overlay clear cases` |
| Overlay row draw FUN_0046e1b0 (request and draw hooked; `--mutation-check` 4 ways) | `uv run python engine_overlay_draw.py` | `ok: 1500 ENGINE overlay draw cases` |
| Kind-8 overlay frame helpers FUN_0046def0, FUN_0046df40 (state calls hooked; `--mutation-check` 3 ways) | `uv run python engine_overlay_frames.py` | `ok: 1600 ENGINE overlay frames cases` |
| Positioned overlay draw FUN_0046e300 (position getters, request and draw hooked; `--mutation-check` 4 ways) | `uv run python engine_overlay_place.py` | `ok: 3000 ENGINE overlay place cases` |
| Gfx.cpp overlay selection FUN_0046f0c0 (removal hooked; `--mutation-check` 3 ways) | `uv run python engine_overlay_remove.py` | `ok: 256 ENGINE overlay selection cases` |
| Sized rectangle draw FUN_0046efd0 (rectangle draw hooked; `--mutation-check` 3 ways) | `uv run python engine_rect_size.py` | `ok: 800 ENGINE rect size cases` |
| Unit cel draw FUN_00471450 (position, request and draw hooked; `--mutation-check` 3 ways) | `uv run python engine_unit_cel.py` | `ok: 1500 ENGINE unit cel cases` |
| GfxUtil unit-on-screen FUN_004db670 (position, node and layer lookups hooked; `--mutation-check` 3 ways) | `uv run python engine_unit_onscreen.py` | `ok: 1200 ENGINE unit on screen cases` |
| Unit selection outline FUN_00471bd0 (every callee hooked; `--mutation-check` 4 ways) | `uv run python engine_unit_outline.py` | `ok: 2000 ENGINE unit outline cases` |
| Per-unit COF loaders FUN_00470610, FUN_00470b70 (transform, loaders, queries hooked; `--mutation-check` 3 ways) | `uv run python engine_unit_preload.py` | `ok: 1600 ENGINE unit preload cases` |
| Unit coloured shape draw FUN_00471a30 (position, request and draw hooked; `--mutation-check` 3 ways) | `uv run python engine_unit_shape.py` | `ok: 1500 ENGINE unit shape cases` |
| Excel link reads and the bin field map (Excel/Excel.cpp: FUN_006bce20, FUN_006bd040, FUN_006bd130, FUN_006bd180, FUN_006bd330, FUN_006bd380) | `uv run python excel_fields.py` | `ok: 4949 excel link read, code search, tree search and bin field map cases` |
| Excel name links and txt splitter (Excel/Excel.cpp: FUN_006bcda0..FUN_006bd640, 8 functions) | `uv run python excel_links.py` | `923 cases: add name / lookup / ...`, then `ok` |
| HUD life / mana / stamina glide (FUN_00496dd0) | `uv run python globe_glide.py gen 2000 1 > cases.tsv; uv run python globe_glide.py game cases.tsv > g.txt; ../../build/tests/test_globe_glide g.txt \| diff g.txt -` | no output |
| H/stpl.h hash tables and bucket arrays (10 entry points; synthetic allocation and dispatch) | `uv run python h_hasharrays.py --mutation-check` | 477 cases; six instruction mutations rejected |
| H/stpl.h lists, arithmetic and temporary-string forwarding (34 entry points; synthetic external dependencies) | `uv run python h_lists.py --mutation-check` | 498 cases; four model mutations rejected |
| H/stpl.h string and ignore-list methods (19 entry points; in-memory storage hooks) | `uv run python h_strings.py` | 315 cases; direct entry coverage checked |
| A Blessed Hammer hit (min / max with the caster's Concentration, roll, undead / demon bonus, resistance, life) | `set -o pipefail; uv run python hammer_hit.py gen 100000 1 > cases.tsv && uv run python hammer_hit.py game cases.tsv > g.txt && ../../build/tests/test_hammer_hit --hammer-hit cases.tsv \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| Grim Ward's start, hit function 26 (FUN_005ab8d0: flags 0x8001, range sHitPar1 else calc1 at least 5) | `uv run python hit_grim_ward.py \| diff - ../../tests/hit_grim_ward_cases.inc`; then `test_grim_ward_start` | no output; the test passes |
| Lightning Fury, hit function 20 (FUN_005ab370 -> FUN_0056b7e0 -> FUN_005ab2a0: radius, count, the aurafilter else 0xa783; the unit struck gets one too; the owner by FUN_00552fd0 / FUN_00552f60, none no burst, the walk's room and the filter's caster) | `uv run python hit_fury.py \| diff - ../../tests/hit_fury_cases.inc`; then `test_lightning_fury` | no output; the test passes |
| The area walk's per-room reach test refuses no room (bugs.md #114: FUN_0056b770 with FUN_00619730 for real; FUN_0056b7e0 stops before any room at a radius <= 0) | `uv run python walk_room_reach.py` | `ok: 33616 FUN_0056b770 cases, 0 rooms refused at a radius >= 0, 2277 at a negative one; ...` |
| An armor's pool over a frame's damage records, then the element mark (FUN_0057c1e0 with Bone Armor's FUN_005c6370 or Cyclone Armor's FUN_005c87a0 on each record, the pool carrying on; FUN_0057ce30 on what is left) | `set -o pipefail; uv run python hit_absorb.py gen 100000 1 > cases.tsv && uv run python hit_absorb.py game cases.tsv > g.txt && ../../build/tests/test_combat --get-hit-absorb < cases.tsv \| diff g.txt -` | no output |
| The get-hit decision (FUN_0057cee0 with FUN_0057ce30 / FUN_0057cb00: hit class, element mark, frozen, all poison, stun, the size bands on the unit's seed, a monster's GH mode after them; the seed after each frame) | `set -o pipefail; uv run python hit_rule.py gen 100000 1 > cases.tsv && uv run python hit_rule.py game cases.tsv > g.txt && ../../build/tests/test_combat --get-hit < cases.tsv \| diff g.txt -` | no output |
| The frame the client draws at each tick of a monster's get-hit (FUN_004b1280 on the mode table's get-hit row: FUN_006217c0's over test, FUN_00623e00's step; FUN_00621810's read) | `set -o pipefail; uv run python hit_len.py gen 100000 7 > cases.tsv && uv run python hit_len.py game cases.tsv > game.tsv && uv run python hit_render.py game cases.tsv game.tsv > g.txt && ../../build/tests/test_combat --get-hit-frames < cases.tsv \| diff g.txt -` | no output |
| The hire window's list box: the rows that fit and the row under the mouse (FUN_004be340, FUN_004bea50) | `set -o pipefail; uv run python hire_list_box.py gen 50000 7 > cases.tsv && uv run python hire_list_box.py game cases.tsv > g.txt && ../../build/tests/test_hire_list_box cases.tsv \| diff g.txt -` | no output |
| The hire window's list box scroll bar: the thumb, a track release, a step and a wheel notch (FUN_004be110, FUN_004bf2f0, FUN_004be910, FUN_004be990, FUN_004be860) | `set -o pipefail; uv run python hire_list_scroll.py gen 50000 7 > cases.tsv && uv run python hire_list_scroll.py game cases.tsv > g.txt && ../../build/tests/test_hire_list_scroll cases.tsv \| diff g.txt -` | no output |
| Ignore list account mapping and node teardown (Chat/Ignorelist.cpp: FUN_00611320, FUN_0060edf0, FUN_00610690, FUN_0060ef60, FUN_0060ef30, FUN_0060edb0, FUN_0060eef0) | `uv run python ignorelist.py` | `9910 cases: 244 accounts mapped; ...`, then `ok` |
| Ignore list remaining helpers (FUN_0060ed30, FUN_0060ed70, FUN_0060ee60, FUN_006114e0, FUN_00611560) | `uv run python ignorelist_remaining.py` | `ok: node cleanup, hash deletion, empty and populated table clear, eager and lazy singleton` |
| Inventory list and grid linking: item out of a grid, out of an inventory, free an inventory, empty an inventory, append to the item and grid lists, spot fit, edge-search choice (FUN_0063a970 ... FUN_0063b810; the real unlink code runs, the stats, id and pool callees are recorded) | `uv run python inventory_lists.py` | `ok: 4031 Inventory.cpp list and grid cases` |
| The look token table and its callers (FUN_0063d710 builds 255 entries from every Items row, FUN_0063d900 looks one up: a zero alternategfx matches the first empty slot; FUN_0063d930 and FUN_0063da70 write a worn item's look slots and tint on hooked helpers) | `D2_BUILD=<build with tools/mpq-cat> uv run python inventory_look.py` | `ok: 2041 look token table cases (131 entries, 123 empty slots) and 6000 look slot cases` |
| InvMode.cpp dispatch, list walks, flag clear and the socket move (FUN_005973f0, 00597890, 005979b0, 00597a20, 00597b00, 0063b210; the handlers and unit lookups hooked and their calls compared) | `uv run python inventory_mode.py` | `ok: 25800 InvMode.cpp cases` |
| Inventory.cpp queries and node lists: the body-grid searches (FUN_0063ce60, 0063cf30, 0063d010, 0063d0f0), the pack-grid searches (0063c3b0, 0063cd80), hand slots, look components, belt item, changed-unit and corpse lists (FUN_0063be40 ... FUN_0063e020; item-property helpers hooked with memoised answers and their call order compared) | `uv run python inventory_queries.py` | `ok: 32999 Inventory.cpp query cases` |
| Small Inventory.cpp helpers: empty stubs, field getters, node getters, cursor, wield, belt cell, placement wrappers (FUN_0063bcc0 ... FUN_0063e070; `FUN_0063afd0` and `FUN_0063aaf0` hooked and recorded) | `uv run python inventory_small.py` | `ok: 2525 small Inventory.cpp helper cases` |
| Trade fit test and its pieces: copy a pack grid, give an item a spot, every page-2 item gets a spot, the whole test and which side fails (FUN_0063e100, 0063e1c0, 0063e2e0, 0063e370; the free-spot search, pool and item helpers hooked) | `uv run python inventory_trade.py` | `ok: 4800 Inventory.cpp trade fit cases` |
| Book/scroll skill lookup (`FUN_0055bff0`; lookups hooked) | `uv run python item_books_skill.py --exe /path/to/game.exe` | `ok: 16 book/scroll skill lookup cases (callees hooked)` |
| Book skill-count delta (`FUN_0055c070`; callees hooked) | `uv run python item_book_count_delta.py --exe /path/to/game.exe` | `ok: 36 book skill-count delta cases (callees hooked)` |
| Book skill-count add/remove (`FUN_0055c110`; callees hooked) | `uv run python item_book_count_update.py --exe /path/to/game.exe` | `ok: 10 book skill-count add/remove cases (callees hooked)` |
| Item lifecycle: replenishment scheduling, expiry deadlines and runeword cleanup (synthetic callees; original control flow/accessor writes, no d2d comparison) | `uv run python item_lifecycle.py` | 48 scheduling, 288 expiry and 8 cleanup cases pass |
| DATATBLS ItemTbls.cpp helpers (14 functions: FUN_00633590 ... 00639cb0; `--mutation-check` 14 ways) | `uv run python item_tbls.py` | `ok: 2078 ItemTbls helper cases` |
| ITEMS small functions: stubs, wrappers, item-data and Items-row getters, fatal class-id getters, item free, container count, set-item row (39 functions, FUN_0055d930 ... FUN_0062a900; EAX upper bits, callee log, `ret n`; `--mutation-check` 20 ways) | `uv run python items_traces.py` | `ok: 3120 ITEMS small function cases` |
| A knockback's pace (FUN_00623f50's KB velocity 0x1000; FUN_0064fe40, FUN_00650090: a subtile a frame, the snap at the path's end) | `uv run python knock_pace.py gen 2000 7 > cases.tsv; uv run python knock_pace.py game cases.tsv > g.txt; ../../build/tests/test_knockback --knock-pace cases.tsv \| grep -E '^[0-9]+ ' > d.txt; uv run python knock_pace.py compare g.txt d.txt` | `frame mismatches 0 velocity not 0x1000 0`, the step gap under 655 |
| Key bindings (assign rules, key names) | `uv run python keys.py \| diff - <(../../build/tests/test_keyconfig --dump)` | no output |
| Every level's collision vs libd2's engine captures (200 seeds × 3 difficulties, ~8 min) | `D2_BUILD=build-release uv run python libd2_crc.py` | `N/131 levels match on all 200 seeds` |
| DATATBLS LvlTbls.cpp helpers (19 functions: FUN_0061ddf0 ... 006200d0; `--mutation-check` 19 ways) | `uv run python lvl_tbls.py` | `ok: 3220 LvlTbls helper cases` |
| A player's mana a frame, state 0x55 (nomanaregen) off and on (FUN_005806f0 up to 0x58079e: the idiv, MulDiv FUN_00483360, the 32-bit add) | `set -o pipefail; uv run python mana_frame.py gen 50000 2 > cases.tsv && uv run python mana_frame.py game cases.tsv > g.txt && ../../build/tests/test_mana_frame g.txt \| diff g.txt -` | no output; the fixture: `uv run python mana_frame.py gen 1000 1 > f.tsv && uv run python mana_frame.py game f.tsv > fg.txt && uv run python mana_frame.py inc fg.txt \| diff ../../tests/mana_frame_cases.inc -`, no output |
| Missile frame directions | `uv run python missile_dirs.py 2000 > g.txt; cut -d: -f1 g.txt \| ../../build/tools/missile-dir-dump/missile-dir-dump \| diff g.txt -` (also `table`) | no output |
| A Sorceress missile's hit (min / max, roll on the missile's seed, resistance, life) | `set -o pipefail; uv run python missile_hit.py gen 60000 1 > cases.tsv && uv run python missile_hit.py game cases.tsv > g.txt && ../../build/tests/test_missile_hit --missile-hit cases.tsv \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| Missiles | `uv run python missiles.py 1500 7` | `ok` |
| MissMode.cpp, the 44 functions (FUN_005ab630 ... FUN_005adcc0: hit, damage, srvdo and timer entry points; callees hooked and logged) | `uv run python missmode.py` | `ok: 43 MissMode.cpp function groups (44 functions) against stock game.exe` |
| A monster's MonEquip gear walk (FUN_005d6b60: rows by level, the item picked on the unit seed, the worn and owner's slots; synthetic tables) | `set -o pipefail; uv run python mon_equip.py gen 2000 20261111 > cases.txt && uv run python mon_equip.py game cases.txt > g.txt && uv run python mon_equip.py inc cases.txt g.txt \| diff ../../tests/monsters/mon_equip_cases.inc -` | no output; then `test_mon_equip` passes |
| Monster bind, animation call and announce (FUN_00573270, 005735a0, 00597d70; `--mutation-check` 11 ways) | `uv run python monster_bind.py` | `ok: 1500 monster bind, animation and announce cases` |
| A monster's blow on a player (FUN_0057b420's roll in 256ths, FUN_0057c1e0: the Bone Armor pool, then the flat and the percent reduction) | `set -o pipefail; uv run python monster_damage.py gen 20000 20261103 > cases.tsv && uv run python monster_damage.py game cases.tsv > g.txt && ../../build/tests/test_combat --monster-damage cases.tsv > d.txt && uv run python monster_damage.py compare cases.tsv g.txt d.txt` | `# <kind>: ... life differ 0 pool differ 0 seed differ 0` for each kind |
| MONSTER data-field accessors (FUN_005730e0, 00573100, 005734c0, 005734e0, 00573500, 00573520, 00573540, 00573570; `--mutation-check` 6 ways) | `uv run python monster_data_fields.py` | `ok: 3200 monster-data accessor cases` |
| DATATBLS MonsterTbls.cpp loaders, freers and getters (19 functions: FUN_00653db0 ... 0065a390; `--mutation-check` 36 ways) | `uv run python monster_loaders.py` | `ok: 4050 MonsterTbls loader cases` |
| DATATBLS difficultylevels, pettype, itemratio, books, lvlwarp, lvlmaze, monlvl, lowqualityitems, qualityitems loaders and the ExpRatio getter (FUN_00617d90, 00618d90, 00637620, 00637320, 0061f130, 0061f380, 00659500, 00637500, 00636770, 00613e60; `--mutation-check` 25 ways) | `uv run python txt_schemas.py` | `ok: 1200 DATATBLS table loader and getter cases` |
| DATATBLS FieldTbls.cpp (expfield init, free, centre offsets, field byte, point alloc / free / set, expfield loader: FUN_0066a2b0, 0066a420, 0066a470, 0066a4b0, 0066a500, 0066a540, 0066a580, 0066a700; `--mutation-check` 17 ways) | `uv run python fieldtbls.py` | `ok: 600 FieldTbls cases` |
| DATATBLS token, missile and overlay tables (monmode, composit, armtype, the token and missile freers, overlay: FUN_0065aef0, 0065b2e0, 0065b350, 0065b3c0, 00663210, 00664150; `--mutation-check` 17 ways) | `uv run python tok_overlay.py` | `ok: 540 token, missile and overlay table cases` |
| DATATBLS AnimTbls.cpp (AnimData loader and record free: FUN_0066a770, 0066a830; `--mutation-check` 7 ways) | `uv run python anim_tbls.py` | `ok: 80 AnimData loader and record free cases` |
| DATATBLS TokTbls.cpp merges (plrmode_plrtype FUN_0065adc0, objtype_objmode FUN_0065b1b0; `--mutation-check` 8 ways) | `uv run python tok_merge.py` | `ok: 160 token merge cases` |
| DATATBLS LvlTbls.cpp expansion free, lvlsub free and portal level array (FUN_0061fbf0, 0061f9c0, 0061dd00; `--mutation-check` 15 ways) | `uv run python lvl_small.py` | `ok: 440 LvlTbls small-table cases` |
| DATATBLS small helpers (DataTbls 00611d20, 006125a0; ItemTbls unloads 00636b00, 006372a0, 006375a0, 006378f0, 00637490, the books count 006374a0, the rare-affix free 00634220, the unique-items free 00634d90; `--mutation-check` 9 ways) | `uv run python item_free_small.py` | `ok: 400 small DATATBLS helper cases` |
| DATATBLS States helpers (bit mask FUN_00611d60, state lists FUN_00611dc0 and FUN_00611e60; `--mutation-check` 7 ways) | `uv run python states_tbls.py` | `ok: 600 States helper cases` |
| Monster display-name slot (FUN_0063e810: UTF-16 copy to MonsterData +0x2c, with null/type guards) | `uv run python monster_name_slot.py` | copied names including supplementary Unicode, guard cases unchanged, and the no-op mutation detected |
| MONSTER pool allocate / release helpers (FUN_00547b50, 00572ba0, 00572bc0, 005a6490, 005a64d0; `--mutation-check` 8 ways) | `uv run python monster_pools.py` | `ok: 130 MONSTER pool allocate and release cases` |
| NPC talker chain (FUN_00572c10, 00572da0, 00572de0, 00572e00, 00572e60, 00572ea0, 00572ee0, 00573020, 00573180; `--mutation-check` 13 ways) | `uv run python monster_talk.py` | `ok: 1260 NPC talker list cases` |
| DATATBLS MonsterTbls.cpp helpers (21 functions: FUN_00653d60 ... 00659c30; `--mutation-check` 26 ways) | `uv run python monster_tbls.py` | `ok: 4930 MonsterTbls helper cases` |
| DATATBLS DataTbls.cpp / LvlTbls.cpp (the growing buffer FUN_006118b0, skills and skilldesc code compilers 00611bd0 / 00611c70, the five class-name getters 00611ee0 .. 00612030, skills and states frees 00617c30 / 00618c90, tables unload 00619140, experience 00613d30, lvltypes 0061e520, expansion tile sets 0061fae0; `--mutation-check` 54 ways) | `uv run python datatbls_finish.py` | `ok: 2150 more DATATBLS function cases` |
| Shattered death's ice (FUN_004afc10: Missiles.txt 0x10f + MonStats2 Height, icebreaksmoke; uberminion's pair; a monster's only) | `uv run python shatter_missiles.py` | `ok: 112 shattered deaths` |
| Shrine-boost arithmetic (FUN_00582800, 00582940, 00583740, 00583840; `--mutation-check` 6 ways) | `uv run python objects_mode_calc.py` | `ok: 1600 shrine-boost arithmetic helper cases` |
| Object operate and event handlers (FUN_00582610, 00584420, 005845d0, 00584d00, 005852e0, 00585580, 00585a80, 00585ce0; `--mutation-check` 8 ways) | `uv run python objects_mode_operate.py` | `ok: 7600 object operate and event handler cases` |
| Small ObjMode helpers (FUN_00582490 ... 005824f0, 005825b0, 00583bd0, 005843d0, 00584820, 00585120 ... 00585240, 005854d0, 00585550; `--mutation-check` 6 ways) | `uv run python objects_mode_small.py` | `ok: 1940 small ObjMode helper cases` |
| Object region data (objrgn.cpp, Objects.cpp: FUN_00546e90, 00546fb0, 00546ff0, 00547080, 00547170, 00547210, 0054f520, 0054f580; `--mutation-check` 6 ways) | `uv run python objects_region.py` | `ok: 1562 object region data helper cases` |
| Overlay frames, loop/one-shot endings, delayed-loop wait | `uv run python overlays.py > /tmp/overlay_cases.inc && diff ../../tests/overlay_cases.inc /tmp/overlay_cases.inc` | no diff; also checks kind 6's wait and duplicate clock |
| The panel keys beside the stash, Cube or a store: UI 1, 2, 4 opening with UI 0, 0xc, 0x19, 0x1a up (FUN_00453910 and its table at 0x711130; FUN_004a0000, FUN_00455f20, FUN_004b3230 / FUN_004b3c20 hooked) | `uv run python panel_keys.py --dump ../../tests/panel_keys_cases.inc > /dev/null && git diff --exit-code ../../tests/panel_keys_cases.inc` | no output |
| The stash's and the Cube's close buttons, every point 3 px round each box, the stash classic and expansion (FUN_00489980, FUN_00489fb0, FUN_00486b00 real; FUN_0044dcc0 hooked) | `uv run python storage_close.py --dump ../../tests/storage_close_cases.inc && git diff --exit-code ../../tests/storage_close_cases.inc` | `6627 points, 4563 over` |
| Party screen name and area cuts (FUN_0049a2c0, FUN_0049a830; the font and unit-string helpers hooked) | `uv run python party_cut.py 2000 1 --dump ../../tests/party_cut_cases.inc && git diff --exit-code ../../tests/party_cut_cases.inc` | no output |
| The PATH dynamic-path accessors and wrappers (70 functions, FUN_00648460..FUN_00649ca0) | `uv run python path_dynamic.py` | `ok: 5065 PATH accessor, setter, collision and recalc cases` |
| Missile pierce charges (FUN_0059f940: draws on {pierce_idx, 666} under skill_pierce + item_pierce, at most 4; the stat getters hooked) | `uv run python pierce_charges.py 2000 1 --dump ../../tests/pierce_charges_cases.inc && git diff --exit-code ../../tests/pierce_charges_cases.inc` | no output |
| Click-on-unit handlers of PlrMsg.cpp: the shared click check, walk / run to entity, skill on entity (left, right, no approach, held) and interact (FUN_00549830 ... FUN_0054aa90; callees hooked and logged) | `uv run python plrmsg_click.py` | `ok: 1680 PlrMsg click-on-unit handler cases` |
| The dispatcher's player gate (FUN_0054d750 -> FUN_0057eec0(1, 0, 0): state 0x36, a null unit, modes 0 and 0x11 refused; walking and running pass) | `uv run python player_may_act.py` | `ok: FUN_0057eec0(1, 0, 0) refuses ...` |
| Location handlers of PlrMsg.cpp: the shared location check, walk / run to x, y, skill at x, y (left, right, held), the cast, the inventory-full message and the keepalive (FUN_005496f0 ... FUN_0054a140; callees hooked and logged) | `uv run python plrmsg_loc.py` | `ok: 1732 PlrMsg location handler cases`; `--mutation-check` plants 55 defects |
| Server packet builders of SCmd.cpp and their queue (FUN_0053b280 ... FUN_0053e9b0, 34 functions: fixed layouts, struct copies, 1/2/4-byte tiers, bit-packed 0x95 / 0x96, fatal reports; the queue unhooked on a fake client; `--mutation-check` 35 ways) | `uv run python scmd_senders.py` | `ok: 2263 SCmd.cpp builder and queue cases match the stock code` |
| Server-side client-packet handlers of PlrMsg.cpp, the small ones: size gates (status 3) and the forwarded arguments of 0x31, 0x33, 0x34, 0x35, 0x36, 0x3f, 0x40, 0x48, 0x4f, 0x53, 0x54, 0x58, 0x62 (FUN_0054ba90 ... FUN_0054c9c0; the callees hooked and logged; status of 0x33 and 0x4f is the callee's) | `uv run python plrmsg_small.py` | `ok: 40 PlrMsg small handler rounds` |
| Vendor buy / sell / repair price | `uv run python price_oracle.py gen 100000 7 > cases.tsv; uv run python price_oracle.py game cases.tsv > g.txt; ../../build/tests/test_game --price cases.tsv \| grep -E '^[0-9]+ ' > d.txt; uv run python price_terms.py cases.tsv g.txt d.txt` | last line `outside stacks, whole, bigmul, nodur: buy 0/…, sell 0/…, repair 0/… differ`; the terms d2d doesn't model yet are listed above it |
| The profile dump is dead (Logging\ProfCore.cpp FUN_006116b0: 23 rows, all 0; FUN_00611660 and 0x611730 never referenced) | `uv run python profile_dump.py` | `ok` |
| Quest portal attempt (`FUN_0058bc80`, callees hooked; `--mutation-check` three ways) | `uv run python quest_arreat_portal.py` | `ok: 192 quest portal cases` |
| Quest 0x21 coordinate and notification helpers (two functions, callees hooked; `--mutation-check` three ways) | `uv run python quest_barbarian_helpers.py` | `ok: 48 quest barbarian helper cases` |
| Quest flag buffer allocation and release (pool boundaries hooked) | `uv run python quest_buffer.py` | `ok: 29 quest buffer lifetime cases` |
| Quest state cleanup (`FUN_00543800`, allocator and buffer free hooked; `--mutation-check` three ways) | `uv run python quest_destroy.py` | `ok: 24 quest state cleanup cases` |
| Quest list cleanup (`FUN_00543590`, allocator hooked; `--mutation-check` three ways) | `uv run python quest_free_list.py` | `ok: 24 quest list cleanup cases` |
| Quest frontier accessors (nine functions, lookup hooked; `--mutation-check` nine ways) | `uv run python quest_frontier.py` | `ok: 216 quest frontier cases` |
| Four small unit helpers (FUN_00622e70, FUN_0063a130, FUN_0062a0a0, FUN_00554120; stat, state-list, item-flag, name and sprintf callees hooked; `--mutation-check` 11 ways) | `uv run python items_objects_helpers.py --exe /path/to/Game.exe` | `ok: 31 cases over 4 functions` |
| Quest item and party check (`FUN_00591cd0`, callees hooked; `--mutation-check` three ways) | `uv run python quest_horadric_party.py` | `ok: 48 quest item party cases` |
| Quest callback wrapper (`FUN_005b30e0`, receiver hooked; `--mutation-check` three ways) | `uv run python quest_izual_forward.py` | `ok: 24 quest callback wrapper cases` |
| Quest level warning (`FUN_00545cd0`, lookups and warning hooked; `--mutation-check` three ways) | `uv run python quest_level_warning.py` | `ok: 72 quest level warning cases` |
| Talk-submenu quest names (FUN_0049f910, table 0x722678) vs `rules::quest_name` | `uv run python quest_name.py` | `65536 messages match d2d's 32 runs; 527 have a name; 164 (row 100) is 3716`, then `ok` |
| Quest object-class dispatch (`FUN_005447a0`, handlers hooked; `--mutation-check` three ways) | `uv run python quest_object_dispatch.py` | `ok: 48 quest object dispatch cases` |
| Quest 0x11 object timer (`FUN_005b9cd0`, lookups and scheduler hooked; `--mutation-check` three ways) | `uv run python quest_object_timer.py` | `ok: 24 quest object timer cases` |
| Ten QUESTS.CPP packet and state-list senders (`FUN_00544520`, `00544f60`, `00544fa0`, `00545090`, `00545100`, `00545530`, `005455b0`, `005456f0`, `00545780`, `00545920`; callees hooked; `--mutation-check` sixteen ways) | `uv run python quest_packets.py` | `ok: 288 quest packet cases` |
| Golden Bird carrier release (`FUN_005bacf0`) | `uv run python quest_bird_carrier.py --exe /path/to/game.exe` | `ok: 48 Golden Bird carrier release cases` |
| Blackened Temple object-mode gate (`FUN_005bbb70`; callees hooked) | `uv run python quest_temple_object.py --exe /path/to/game.exe` | `ok: 5 Blackened Temple object-mode cases (callees hooked)` |
| Guardian alert gate (`FUN_005bbfe0`; flag test hooked) | `uv run python quest_guardian_alert.py --exe /path/to/game.exe` | `ok: 32 Guardian alert cases (flag test hooked)` |
| Khalim log-send filter (`FUN_005b7f90`; callees hooked) | `uv run python quest_khalim_log.py --exe /path/to/game.exe` | `ok: 12 Khalim log-filter cases (callees hooked)` |
| Siege log filter (`FUN_00586c80`; callees hooked) | `uv run python quest_siege_log.py --exe /path/to/game.exe` | `ok: 48 Siege log-filter cases (callees hooked)` |
| Prison of Ice position gate (`FUN_0058a7d0`; lookup hooked) | `uv run python quest_prison_position.py --exe /path/to/game.exe` | `ok: 54 Prison of Ice position cases (lookup hooked)` |
| Betrayal portal retry (`FUN_0058b940`; callees hooked) | `uv run python quest_betrayal_retry.py --exe /path/to/game.exe` | `ok: 48 Betrayal portal retry cases (callees hooked)` |
| Rite of Passage object-mode gate (`FUN_0058d280`; callees hooked) | `uv run python quest_rite_object.py --exe /path/to/game.exe` | `ok: 10 Rite of Passage object-mode cases (callees hooked)` |
| Quest party callback dispatch (`FUN_005ba2f0`, party routines hooked; `--mutation-check` three ways) | `uv run python quest_party_callback.py` | `ok: 24 quest party callback cases` |
| Quest prisoner packet (`FUN_0058e120`, lookups and sender hooked; `--mutation-check` three ways) | `uv run python quest_prisoner_packet.py` | `ok: 24 quest prisoner packet cases` |
| Blood Raven quest-log filter (`FUN_00590830`; callees hooked) | `uv run python quest_burial_log.py --exe /path/to/game.exe` | `ok: 48 Blood Raven log-filter cases (callees hooked)` |
| Cain scroll count (`FUN_00592b90`; item lookup hooked) | `uv run python quest_cain_scroll_count.py --exe /path/to/game.exe` | `ok: 16 Cain scroll-count cases (lookup hooked)` |
| Cain scroll-loss rewind (`FUN_00592c80`, `FUN_00592bd0`; callees hooked) | `uv run python quest_cain_rewind.py --exe /path/to/game.exe` | `ok: 5 Cain transitions and 10 scroll-loss trigger cases (callees hooked)` |
| Rescue reward across difficulty quest buffers (`FUN_0058a0a0`, hooked callees; `--mutation-check` three ways) | `uv run python quest_rescue_resistance.py` | `ok: 24 quest resistance cases` |
| Ten small QUESTS.CPP helpers (`FUN_00543b90`, `00543ce0`, `00543de0`, `00544470`, `00545290`, `00545300`, `00545760`, `00545830`, `00545990`, `00545d40`; callees hooked; `--mutation-check` seven ways) | `uv run python quest_small.py` | `ok: 240 small quest helper cases` |
| Quest 0x21 spawn attempt (`FUN_0058a820`, lookup, room and spawn hooked; `--mutation-check` three ways) | `uv run python quest_spawn_rescue.py` | `ok: 24 quest rescue spawn cases` |
| Tomb quest transition (`FUN_0059c810`, callees hooked; `--mutation-check` three ways) | `uv run python quest_tomb_transition.py` | `ok: 24 quest tomb transition cases` |
| Quest 0x14 two-object updates (`FUN_005bcfd0`, lookup and setter hooked; `--mutation-check` three ways) | `uv run python quest_two_objects.py` | `ok: 24 quest two objects cases` |
| Quest waypoint activation (`FUN_005b4ff0`, three callees hooked; `--mutation-check` three ways) | `uv run python quest_waypoint_activate.py` | `ok: 24 quest waypoint activation cases` |
| Small Quests helpers: the 11 quest-update notifiers, Horadric Staff test, A2/A4/A5 data helpers, filters and seal glows (31 functions, FUN_005879d0 ... FUN_005b7340; `--mutation-check` 13 ways) | `uv run python quests_small.py` | `ok: 768 Quests small-helper cases` |
| GAME Roster.cpp party roster: lookup by id and id list, relation toggle, squelch, hostile, leave-area, unit name, readers and setters (15 functions: FUN_004792e0 ... 0047a690; `--mutation-check` 36 ways) | `uv run python roster_small.py` | `ok: 29374 ROSTER accessor and relation calls checked` |
| Screen shake and quest control frames | `uv run python shake.py > ../../tests/shake_cases.inc`; `uv run python shake.py --controls > ../../tests/quake_control_cases.inc` | `test_shake` matches both captured fixtures |
| Show Items label layout (FUN_004c0640's 12 tries in FUN_004c0810's loop) | `uv run python show_items_layout.py gen 5000 1 > cases.tsv; uv run python show_items_layout.py game cases.tsv > g.txt; ../../build/tests/test_show_items_layout cases.tsv \| diff g.txt -` | no output |
| Show Items' labels, FUN_004c0810 run itself (the near rooms' walk, newest first in a room, at most 0x20; each label's rect and whether it's lit, not its colour) | `set -o pipefail; uv run python show_items_walk.py gen 30000 7 > cases.tsv && uv run python show_items_walk.py game cases.tsv > g.txt && ../../build/tests/test_show_items_walk cases.tsv \| diff g.txt -` | no output |
| Monster sight, search, moves | `uv run python sight.py 20000 3`, `search.py 20000 2`, `moves.py` | `ok` |
| Player mana check and payment (FUN_0056c160, FUN_0056bfe0: no minmana floor in the check, the payment needs enough) | `set -o pipefail; uv run python sk_mana.py gen 100000 7 > cases.tsv && uv run python sk_mana.py game cases.tsv > g.txt && ../../build/tests/test_sk_mana --sk-mana cases.tsv \| grep -E '^[0-9]+ ' > d.txt && uv run python sk_mana.py diff cases.tsv g.txt d.txt` | `different 0` |
| Player Teleport destination (srvdofunc 27, FUN_005ca360 and the warp FUN_00554ea0: the caster's room and its near list, the nearest free spot) | `set -o pipefail; uv run python sk_tele_spot.py gen 100000 1 --no-units > cases.tsv && uv run python sk_tele_spot.py game cases.tsv > g.txt && ../../build/tests/test_tele_spot --tele-spot cases.tsv \| grep -E '^[0-9]+ ' > d.txt && uv run python sk_tele_spot.py diff g.txt d.txt cases.tsv` | `100000 cases`, then one line: `100000  same` |
| Level warp arrival spot (FUN_005550b0: FUN_0064e7b0 with size 2, mask 0x1c09, flag 1, then FUN_00554ea0; no spot in rings 1 to 49 refuses the warp) | `set -o pipefail; uv run python sk_tele_spot.py gen --warp 20000 1 > cases.tsv && uv run python sk_tele_spot.py warp cases.tsv > g.txt && ../../build/tests/test_warp_arrival --warp-arrival cases.tsv \| grep -E '^[0-9]+ ' > d.txt && uv run python sk_tele_spot.py diff g.txt d.txt cases.tsv` | `20000 cases`, then one line: `20000  same` |
| Skills.cpp skill-list and skill-record accessors (23 functions, FUN_00643930 ... FUN_00645120: left / right / current skill, light, params, flags, target fields, null asserts; `--mutation-check` 7 ways) | `uv run python skill_accessors.py` | `ok: 920 Skills.cpp skill-list and skill-record accessor cases` |
| Skills.cpp corpse predicates (FUN_00645510, FUN_006454c0, FUN_00645590, FUN_00645630, FUN_006455e0, FUN_006456d0; `--mutation-check` 4 ways) | `uv run python skill_corpse.py` | `ok: 900 Skills.cpp corpse predicate cases` |
| Skill element damage and length (synergies, masteries) | `uv run python skill_damage.py gen 100000 1 > cases.tsv; uv run python skill_damage.py game cases.tsv > g.txt; ../../build/tests/test_game --skill-damage cases.tsv \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| Skill start sounds on a player or a monster (FUN_004c6140: stsound or ItemCastSound, the weaponsnd swing, stsoundclass, stsounddelay, stsuccessonly) | `set -o pipefail; uv run python skill_start_sound.py gen 20000 7 > cases.tsv && uv run python skill_start_sound.py game cases.tsv > g.txt && ../../build/tests/test_skill_start_sound cases.tsv \| diff g.txt -` | no output |
| Skills.cpp table-row and unit-skill helpers (14 functions: FUN_00643a60, FUN_00643d00, FUN_00644300, FUN_00644750, FUN_00644aa0, FUN_00644c20, FUN_00645080, FUN_00645160, FUN_00645a70, FUN_00645b70, FUN_00645bc0, FUN_00645fb0, FUN_00646170, FUN_006447b0; `--mutation-check` 12 ways) | `uv run python skill_table_rows.py` | `ok: 1121 Skills.cpp table-row and unit-skill helper cases` |
| Skill tooltip lines | `uv run python skill_tips.py 20 0 > g.txt; ../../build/tests/test_game --skill-tips 20 0 \| grep '^[0-9]' \| iconv -f latin1 -t utf-8 \| diff g.txt -` | no output |
| SoundHdr cache eviction (FUN_004824a0) | `uv run python sound_hdr_evict.py` | `ok: SoundHdr eviction pass, age and active-sound cases` |
| SoundHdr group choice (FUN_00482680) | `uv run python sound_hdr_group.py` | `ok: SoundHdr group selection and history retry cases` |
| SoundHdr lifecycle wrappers (FUN_00482260, FUN_00482860, FUN_004828b0, FUN_00482940) | `uv run python sound_hdr_lifecycle.py` | `ok: SoundHdr initialize, touch, release and shutdown` |
| SoundHdr sample-load dispatcher (FUN_00482970) | `uv run python sound_hdr_load.py` | `ok: SoundHdr loading guards, cache rejection, sync and async cases` |
| SoundHdr path selection (FUN_00482710) | `uv run python sound_hdr_paths.py` | `ok: 28 sound path and length-boundary cases` |
| SoundHdr falloff range getters (FUN_004825b0, FUN_00482610) | `uv run python sound_hdr_ranges.py` | `ok: 42 falloff minimum/maximum getter cases` |
| SoundHdr grouped reference counts (FUN_004822b0, FUN_00482370) | `uv run python sound_hdr_refs.py` | `ok: grouped count adjustment, clearing, guards and underflow ordering` |
| SoundHdr loaded-record release (FUN_004823e0) | `uv run python sound_hdr_release.py` | `ok: unloaded, synchronous, asynchronous and active sound release` |
| SoundHdr table loader (FUN_00481950) | `uv run python sound_hdr_tables.py` | `ok: synthetic SoundHdr table descriptors, group bases, block counts and song range` |
| SRP state allocation and interleaved dual SHA-1 helper (FUN_00528500, FUN_0052a120) | `uv run python srp_sha.py` | `ok: SRP constructor success/failure and 14 interleaved SHA-1 cases` |
| Stat-list array primitives: insert, remove, queue, sums, add, set, chains, queued-stat callbacks, layer copy (FUN_00624c80 ... FUN_006261b0; `--mutation-check` 10 ways) | `uv run python stats_array_ops.py` | `ok: 33425 stat-list array primitive cases` |
| Stat-list field getters and setters, wrappers and the named-stat getters (StatsEx.cpp FUN_00625250 ... FUN_00626d10; `--mutation-check` breaks the model 8 ways) | `uv run python stats_list_fields.py` | `ok: 10041 stat-list field accessor, wrapper and named-stat getter cases` |
| Stat change push-up and derived-stat refresh (FUN_00626920, FUN_006266c0; `--mutation-check` 8 ways) | `uv run python stats_propagate.py` | `ok: 3000 stat refresh and change push-up cases` |
| String table number formatters (FUN_00525350, 00525490, 005255c0) | `uv run python strtable_formatters.py [--exe /path/to/Game.exe]` | `ok: stock TBL unsigned, signed, and K/M/B/T formatter cases` |
| String table file opener (FUN_00525920) | `uv run python strtable_formatters.py [--exe /path/to/Game.exe]` | `ok: CRC-checked synthetic TBL open and rejection cases` |
| String table leaves (StrTable/strtable.cpp: FUN_00524fd0, 00524fe0, 00525020, 00524e20, 00525090, 00525100, 00525260, 005251c0, 00524c60, 00524ac0, 00524e40, 00525770) | `uv run python strtable_leaves.py` (`--mutation-check`) | `ok: 6560 string table leaf cases` |
| Every level, all cores | `./sweep.sh 1-50 monsters objgroups` | every level matching |
| NPC gossip text list (Text/Text.cpp: FUN_00661240..FUN_006616e0, 13 functions) | `uv run python text_list.py` | `929 cases: new / add / remove / free / getters / build / store / sort; ...`, then `ok` |
| Town Portal read from the pack (C->S 0x20: opened, charge, old pair) | `uv run python tome_use.py gen 100000 7 > cases.tsv; uv run python tome_use.py game cases.tsv > g.txt; ../../build/tests/test_game --tome-use cases.tsv \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |
| Table loaders (LoadTxt FUN_00504430, CompileTxt FUN_006122f0) | `uv run python txt_loaders.py` | `46 cases: LoadTxt (.txt / .bin) and CompileTxt (4 flag sets) ...`, then `ok` |
| Font map and the TBL character count (FUN_00527790 `default.map`, FUN_005276e0 lead bytes, FUN_00527d50) | `uv run python unicode_kamap.py` | `ok: 8 font map / count cases; Latin lead bytes 0x7f..0x9f; 9388 English TBL values counted at or above their decoded length` |
| SUnitEvent.cpp queued-event removal by queue and owner (FUN_005c0b50; `--mutation-check` 3 ways) | `uv run python unit_event_remove.py` | `ok: 3000 SUnitEvent remove-by-queue-and-owner cases` |
| UnitSnd.cpp sound-table lookups (FUN_004ca3e0, 004cb380, 004cb3e0; `GetSndUnitClassId` hooked; `--mutation-check` 4 ways) | `uv run python unit_snd_tables.py` | `ok: 9383 UnitSnd.cpp table-lookup cases (GetBaseSoundId, GetModeChangeSound, GetSpecialSoundByMode)` |
| Client UNIT\\CUnit.cpp, CUnitEvent.cpp and monster-flag helpers: field accessors with null asserts, the two unit hash tables (find, remove, iterate, clear), the unit-flag setters, dead / attackable / selected tests, event list, warp, tail-call wrappers (51 functions: FUN_00463720 ... 004dc190; callees hooked and logged; `--mutation-check` 61 ways) | `uv run python unit_client.py` | `ok: 5120 client UNIT cases` |
| A unit's footprint height, UNITS Units.cpp FUN_006205a0 (by type: player 2, item 1, MonStats2 SizeY signed, objects.txt SizeY, Missiles.txt Size unsigned; FUN_00451fe0 / FUN_0046ace0 run for real on fake tables) | `set -o pipefail; uv run python unit_size.py gen 400 1 > cases.tsv && uv run python unit_size.py game cases.tsv > g.txt && ../../build/tests/test_unit_size cases.tsv \| diff g.txt -` | no output |
| UNITS Units.cpp / Missile.cpp / UnitFinds.cpp / MisStream.cpp call-outs: unit free, speed word, stat pair, player data, vitals cost, face (17 functions: FUN_00620300 ... 00663d90; `--mutation-check` 33 ways) | `uv run python units_calls.py` | `ok: 36 UNITS call-out check rounds` |
| UNITS Units.cpp accessors: position, path, animation, object row and player-data readers, kept skills, static path (61 functions: FUN_00620360 ... 00623570; `--mutation-check` 33 ways) | `uv run python units_small.py` | `ok: 103 UNITS small-accessor check rounds` |
| Walk / run animation rate | `uv run python walk_rates.py > g.txt; ../../build/tests/test_combat --walk-rates \| diff g.txt -` | no output |
| Walk % of a monster, player, merc and pet (FUN_00623f50: velocity, chill, floor, velocitypercent) | `uv run python walk_velocity.py gen 100000 9191 > cases.tsv; uv run python walk_velocity.py game cases.tsv > g.txt; ../../build/tests/test_walk_velocity --walk-velocity cases.tsv \| grep -E '^[0-9]+ ' > d.txt; uv run python walk_velocity.py compare cases.tsv g.txt d.txt` | `mismatches 0` |
| A blocked path end walked back toward the start, and a player's push-on (FUN_00648120, FUN_00648050) | `uv run walk_back.py 20000 > ../../tests/walk_back_cases.inc`, rebuild, `../../build/tests/test_walk_back` (800 cases are committed) | `OK: 20000 blocked ends walk back as FUN_00648120 walks them` |
| A walker's frame (FUN_00650840: the 16.16 step, landing on a point, points passed, the blocked stop) | `uv run walk_step.py 3000 > ../../tests/walk_step_cases.inc`, rebuild, `../../build/tests/test_walk_step` (250 cases are committed) | `OK: 3000 walks step as FUN_00650840 steps them` |
| A walker's facing: the aim's 64-step direction and the turn toward it (FUN_0064fc60, FUN_006485f0, FUN_00648640) | `uv run facing.py 20000 > ../../tests/facing_cases.inc`, rebuild, `../../build/tests/test_facing` (400 of each are committed) | `OK: 20000 aims and 20000 turns as game.exe's` |
| Warden client WardenClient.cpp: signature check, module load pipeline (RC4, zlib, loader hooks), load by name, cache write, the 7 callbacks, module start/unload/packet/tick/shutdown/init, C->S 0x66 framing (FUN_0047fa20 ... FUN_004803c0, FUN_00478830; no module code is run; `--mutation-check` 13 ways) | `uv run python warden_client.py` | `ok: 267 WardenClient cases over 11 checks` |
| Stock waypoint allocation/release (stock-only lifecycle check; allocator, diagnostic and fatal boundaries intercepted) | `uv run python waypoint_lifecycle.py`; `uv run python -m unittest test_waypoint_lifecycle.py` | `ok: 9 allocations, 63 releases/rejections`; 8 tests pass with game data |
| Waypoint refusal window (C->S 0x49: refused or travels) | `uv run python wp_refuse.py gen 100000 7 --solo > cases.txt; uv run python wp_refuse.py game cases.txt > g.txt; ../../build/tests/test_game --waypoint-refuse cases.txt \| grep -E '^[0-9]+ ' \| diff g.txt -` | no output |

## When it breaks

| You see | Fix |
|---|---|
| `SystemExit` naming an import | game.exe called a Win32 function nobody stubbed: add a `w_<ImportName>` method to `emu.py` |
| Can't find game.exe or the MPQs | Set `D2_MPQ_DIR`, or run the launcher once (it writes `data =` and puts `bin/game.exe` beside the MPQs) |
| `mpq-cat` not found | Build first: `cmake --build build` |
| 1.14d tables missing | Set `D2_PATCH_INSTALLER=/path/to/LODPatch_114d.exe` |
| First run is slow | Normal: files are cached in `.cache/` after that |

The rest of this page is the reference for every oracle and for adding
your own.

## Setup

```
cd tools/emu
uv sync                      # .venv with unicorn, pefile, capstone
```

Needs `bin/game.exe` beside the MPQs (the launcher puts it there) and a
built `build/tools/mpq-cat/mpq-cat`. The MPQ folder is found as d2d finds
it: `$D2_MPQ_DIR`, else `data =` in d2d.cfg (`~/.config/d2d/` on Linux,
`~/Library/Preferences/d2d/` on macOS), else the macOS launcher's setting.
The 1.14d patch data comes from `$D2_PATCH_INSTALLER`, which defaults to
`~/Downloads/Diablo II + LoD/patch/LODPatch_114d.exe`.

## What's there

- `emu.py`: the core `Emu` class.
  - `call(addr, *stack_args, ecx=, edx=)` calls a function. It covers
    stdcall, fastcall and cdecl (the caller pops nothing). It returns EAX.
  - `hook(addr, fn, nstack)` replaces a game.exe function with Python;
    `stub(value, {addr: nstack, ...})` hooks many to return one value.
    `Emu(real={addr, ...})` keeps those game.exe's: a harness that hooks
    them (`life_apply.Game(real=...)`) runs them for real instead.
  - `view(kind, addr)` names a struct's fields from
    `docs/research/re/structs.tsv`: `e.view("unit", u).mode = 3`. A field
    not there is a KeyError: add its row, with the line that shows it.
  - `alloc`/`put`/`r32`/`s32`/`w32`/`read`/`cstr` handle memory. The heap
    is a bump allocator that never frees.
  - `crt_init()` runs the static CRT's heap and thread-data startup. It
    also hooks the fatal-error handler (0x408a60, which raises
    `SystemExit` with the message) and the string-table lookup
    (0x524d30, which returns `""`).
  - `serve_files(e)` answers Storm's file calls (open, size, read, seek,
    close, name) from the MPQ stack through `mpq-cat`. Files are cached
    in `.cache/`, so the first run is slow and later runs are fast.
  - Win32 stubs are `w_<ImportName>` methods that return
    `(eax, stdcall arg count)`. An unstubbed import stops the run with its
    name and a stack of return addresses, so you add them as you go.
  - `backtrace()` scans the stack for code addresses. It's a heuristic,
    not an unwind.
- `mpq_cache.py` caches MPQ reads by archive and patch version. A tool
  error stops the oracle; only a confirmed absent archive entry is cached.
- `python3 -m unittest test_tooling.py` checks cache recovery and sweep
  failure reporting without game data.
- `drops.py`: the drop roller (FUN_0055a6d0) as an oracle. It diffs
  `build/tools/drop-dump` over a seed range (`drops.py 1-100000`), or
  every treasure class's entries (`drops.py tables`), made items
  (`drops.py items`), containers opened (`drops.py objects`) and armor
  stands' / weapon racks' items (`drops.py stands`). Item creation is
  hooked, so only the dropper's seed moves. See
  `docs/research/re/drops.md`.
- `new_character_stacks.py`: a new character's start items (FUN_00534f10
  over its CharStats row) and the quantity each gets. It should print
  `jav 60` for the Amazon (class 0), and `aqv 250, aqv 100, cqv 250` with
  `quivers`. See `docs/research/re/drops.md` "A new character's items".
- `drlg.py`: the level generator as an oracle.
  - `boot()` loads every data table (FUN_00619300(0, 1, 1), about 1.4 s
    warm), including the LvlPrest and LvlSub DS1s.
  - `alloc_act(e, act, seed, level)` is FUN_006194a0, server side. It
    builds `level` straight away.
  - `dump()` prints a level in the same text form as
    `build/tools/drlg-dump/drlg-dump`.

```
uv run python drlg.py 3              # the Blood Moor for map seed 3
uv run python drlg.py 1-300 2 out/   # out/<seed>.txt for seeds 1..300
```

Diff it against the port:

```
build/tools/drlg-dump/drlg-dump ~/Workspace/private/diablo2 3 > ours.txt
(cd tools/emu && uv run python drlg.py 3) > game.txt && diff game.txt ours.txt
```

- `objgroups.py`: each room's preset units and random object groups
  (FUN_005559a0, FUN_00552610) as an oracle: the room seed in and out,
  the object seed, and every object made. The regression check for
  `components/game/objgroups.cpp`, which should report every seed matching
  for every Act 1 level:

```
uv run python diff_drlg.py 1-20 2 objgroups     # level 2..39
uv run python diff_drlg.py 1-20 2 drops         # then three items dropped at each group object (FUN_00555da0)
./sweep.sh 1-50 monsters objgroups              # every level 2..39, a job per core but two (JOBS=n); progress on stderr
```

`sweep.sh` exits 2 on bad input (seeds not FIRST-LAST or with a leading
zero, an unknown kind, JOBS not a positive integer) and 1 when any level
fails, mismatches or reports fewer results than asked.

- `monprop_rows.py`: the MonProp rows as game.exe compiled them
  (DataTables +0xc30), per row its MonStats users and each difficulty's
  {prop, par, min, max, chance} slots; test_game's MonProp cases are its
  output.
- `sight.py`: a monster's line of sight (FUN_00622920 → FUN_0064e260)
  on random walls in a fake room, against a line-for-line Python copy of
  `rules::sight_blocked`. `uv run python sight.py 20000 3` should print
  `ok`. See `docs/research/re/monster-ai.md`.
- `search.py`: a monster's target search (FUN_005dd7f0) on random players
  with pets, good and neutral monsters in lists 8 / 9, rooms for the mode 5
  search, and Attract's / Confuse's skill-set target (FUN_005dd610), against
  `rules::search_pick` / `search_near` and ai.cpp's flow.
  `uv run python search.py 20000 2` should print `ok`; `--break` should not.
- `moves.py`: a monster's move. The toward pather (FUN_00679c80) runs on
  random walls, and the chase check (FUN_006503f0 → FUN_00650350: stop,
  go on, re-path, the budget) runs on random paths and targets. Both are
  compared against line-for-line copies of `rules::toward_path` /
  `rules::chase_check`, and the modes that think at once (DAT_0073c6d0)
  against ai.cpp's. `uv run python moves.py` should print `ok` three times.
  `--break` breaks the copy and should print mismatches; `--dump` prints the
  test_monster_ai.cpp cases.
- `collision` (drlg.py `collision_dump`): the level's rooms brought up out
  of list order ($ORDER shuffle, the default, reverse or list), then every
  room's collision grid (room1 +0x20), against `relevel`'s:
  `uv run python diff_drlg.py 1-10 2 collision`, `./sweep.sh 1-10 collision`.
- `game` (objgroups.py `game_dump`): one game, $LEVELS (default 2,8,4,9;
  put 1 first for the camp, whose rooms print without their seeds) made in turn, each level's rooms brought up in $ORDER and populated newest
  first, then every container they made opened (drops.py's openers): per
  room the room1 seed, the room seed, the object and game seeds, the objects
  made; per container its drops and the seeds after. The check for the one
  object seed and the room order: `uv run python diff_drlg.py 1-20 0 game`.
- `units` brings game.exe's rooms up in $ORDER first (drlg.py `bring_up`):
  the units and warps come out the same in any order.
- `layout` (drlg.py `layout_dump`): act 2's outdoor rectangles, Lut
  Gholein's LvlPrest file and the two tombs, nothing built:
  `uv run python diff_drlg.py 1-2000 40 layout`. Every dump builds a level
  in its own act (`act_of`).

## Reading game.exe without Ghidra

Quick capstone helpers for when a Ghidra run is too slow or its decompile
hides the detail (fastcall registers, stack-built tables). Each finds
game.exe as `emu.py` does (`--exe PATH` for another).

- `disasm.py 623f50 [COUNT]`: a function from its address to its end,
  the `ret` followed by 0xCC padding or the next `push ebp; mov ebp, esp`,
  so an early return doesn't stop it (or COUNT instructions). It can run on into an
  unpadded next function with no frame pointer (`40a420`): pass COUNT there. `disasm.py --all out.txt` writes every
  executable section, a line an instruction, to grep for an immediate
  (a string's or a table's address) or an instruction shape.
- `scan_writes.py OFFSET [SIZE] [LO-HI]`: the .text instructions with a
  `[reg + OFFSET]` operand capstone marks as written (`--reads`: read),
  to find who sets a struct field Ghidra's xrefs can't see. Implicit
  writes (`rep movs`, `push`) don't show.
- `txt_fields.py NAME` / `txt_fields.py LOADER`: the column list an excel
  table's loader builds on the stack, a line a column: its type, size or
  bit, and record offset (`HitClass type 0x4 ... +0x94` in Missiles.txt,
  FUN_00661b20). `--all` shows NAME in every list a loader builds with
  `mov [ebp+disp], imm` name stores.

## Adding a new oracle

1. Find the entry point and its calling convention with Ghidra.
   `Disasm.java` shows the register args the decompiler drops (see
   `tools/ghidra/README.md`).
2. Call it with `boot()`-style setup, then read the result structs back
   with `e.view` (a new field gets a row in `docs/research/re/structs.tsv`).
   Start from a harness that already hooks the neighbourhood (`life_apply.Game`
   for damage and stats) and pass `real=` for what your claim runs.
3. When it faults, the message names the eip and the stack. Stub the
   import, or `hook` the function if it reaches outside what you're testing
   (UI, network, sound).
4. Print the result in a text form that a small C++ tool beside the port
   also prints, then diff.

ponytail: `__cinit` (C++ static constructors) isn't run, because its C
initialisers want locale setup. Add it when a global constructor turns out
to matter.

- `missiles.py`: missile flights (FUN_0059fa30, then srvdofunc 1 a frame at
  a time) in a Blood Moor game, against a line-for-line copy of
  `rules::MissileFlight`: every frame's 16.16 position and how it ended
  (range, wall / rooms, the foe struck). `uv run python missiles.py 1500 7`
  should print `ok`; `--break` should not; `--dump` prints the
  test_monsters.cpp cases. See `docs/research/re/missiles.md`.
- `npcs.py`: the camp's NPCs thinking (MonAI Npc, FUN_005e7130) with its
  effects hooked, against `drlg-dump ... npcs` (`rules::npc_think`):
  `THINKS=150 uv run python diff_drlg.py 1-40 0 npcs`. See
  `docs/research/re/town-npcs.md`.
- `npc_first_think.py`: when the camp's NPCs first think, as game.exe
  files it: made with no client in the room (aidel), then the room's
  first client (FUN_0053a8e0 → FUN_00573780; FUN_005417d0 watched):
  `uv run python npc_first_think.py 1` should print `made 15 in play 2`
  for each (`npc_patrol`, `tests/game/test_npc_first_think.cpp`). See
  `docs/research/re/town-npcs.md` "The first think".
