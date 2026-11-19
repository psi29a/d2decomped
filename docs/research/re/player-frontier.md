# PLAYER frontier — game.exe 1.14d

The 39 `PLAYER` rows of `build/gaps/functions.tsv` with `frontier = true`
on 2026-11-18: untouched functions that a function d2d ports calls. Read
from the decompile and, where registers matter, the disassembly; no
oracle was run for these rows. Re-check with `tools/ghidra/fn.py
<address>` and `tools/ghidra/calls.py <caller> --to <address>`. The **d2d**
column is as in [game-frontier.md](game-frontier.md): *matches*, *differs*,
*no counterpart* (with why), *not compared*.

## Too many corpses (`FUN_0057f540`)

The death path `FUN_0057f700` first counts the player's corpses that
still exist (`FUN_0057f690`: corpse nodes whose unit is gone are removed,
the rest counted). Above 15, it makes no corpse: `FUN_0057f540` does
instead, and the death returns 0. For the cursor item (pass -1, sent clear
with 0x42 first) and each of the 12 body slots (`d2d_rules_hand_index`),
the item comes off the player and a copy (`FUN_0055a2a0(item, 1)`) goes
on the ground at a free spot near the player (`FUN_00555da0`,
`FUN_00558aa0`). Then it reads the gold (stat 0xe), sets it to 0, and
calls the gold drop `FUN_00535510` with the old amount (`0057f632`..
`0057f65f`). That drop refuses an amount over the stat, now 0, so gold
left then is lost rather than dropped (bugs.md 121). There is gold left
only after a PvP death: the death's penalty (`FUN_005357d0`) empties the
purse onto the ground unless the killer was a player or a player's pet.

d2d matches: `World::make_corpse` (components/game/world/travel.cpp)
makes no corpse past 15 (`rules::death_makes_corpse`) and drops the item
in hand, then the worn items in slot order, at the nearest free spot. The
branch is checked against game.exe: `cd tools/emu && uv run python
corpse_cap.py` (counts 0..20, frozen as tests/corpse_cap_cases.inc;
test_corpse_cap, test_corpse_cap_drop).

## The old save loader (`FUN_00534020`)

`FUN_00534330` sends a save whose version is under 0x5c to
`FUN_00534020` (net-join.md "Join act"). Its sections are each read by
one function below, by their two-byte tags: `WS` waypoints, `0x7701` the
NPC flags, `gf` stats, `if` skills, `JM` items. d2d reads versions 92..96
(0x5c..0x60) only (components/d2s/d2s.hpp:147); whether 1.14d's own
client offers an older save to upload isn't traced.

## Rows in address order

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00532c70` | `oldsave_waypoints` | Copies 0x50 bytes; tag `0x5357` (`WS`) else 3; three 0x18-byte difficulty records (version 0x101, 0x102 or 0) into player data `+0x1c..` (`FUN_00661030`); 0x50 bytes used. A record whose version word is another value returns 3 | no counterpart (the old loader) |
| `FUN_00532d00` | `oldsave_npc_flags` | Copies 0x34 bytes; tag `0x7701` else 0xb; for each of three difficulties two 8-byte flag sets into the player data (`FUN_00572540`, `FUN_00572550`); 0x34 bytes used | no counterpart (the old loader) |
| `FUN_00532da0` | `oldsave_stats` | Tag `0x6667` (`gf`) else 4; a bit mask at +2, then one dword at +5 per set bit; each of the first N stats is set (`FUN_00627260`), an unset bit as 0. When `FUN_0053fce0`, life (6) and mana (8) are set to their max. Stamina (10) from `FUN_00625db0`. Bytes used 5 + 4 × set bits | no counterpart (the old loader) |
| `FUN_00532e90` | `oldsave_skills` | Tag `0x6669` (`if`) else 5; one byte per class skill (`FUN_00646140` count, `FUN_006460f0` the id): a nonzero byte is that many points (`FUN_0056deb0`). Then `FUN_0053c5d0` and `d2d_passives_then_pet_limits`; count + 2 bytes used | no counterpart (the old loader) |
| `FUN_00533350` | `oldsave_items` | Tag `0x4d4a` (`JM`) and a word count; each item `JM` record (`FUN_00532f30` parses, `FUN_00531040` / `FUN_00531390` adds, by its last argument) with its socketed items after it; a record equal (id fields) to one already read skips its sockets. A bad tag stops the read. 20 KB of stack for the seen list | no counterpart (the old loader) |
| `FUN_00534800` | `player_npc_flags_alloc` | Player data `+0x60`, `+0x64`, `+0x68` each from `FUN_00572560` (one per difficulty); `+0x6c` = 0. Only caller the player init `FUN_005348c0` | not compared |
| `FUN_00534f80` | `player_items_send` | For a unit with an inventory (asserts it, line 0x187), each of its items: node kind 3 `FUN_0053d090`; the client's own player's kind 1 `FUN_0053ed50`, kind 2 `FUN_0053ee70`. Only caller `FUN_00571f90` | no counterpart: d2d's client reads the world; sends are the netplay code's |
| `FUN_005484b0` | `unit_mode_send` | By the unit's mode (`+0x10`, 0 for none): the function in the 12-byte rows at `0x7319e8` (`IsBadCodePtr` asserts, line 0x148), called (client, mode). Callers `FUN_00571f90`, `FUN_00580860` | no counterpart (as `player_items_send`) |
| `FUN_005489f0` | `unit_stats_send` | For a unit other than the client's player: each stat in the -1-ended list at `0x731ad8` (`FUN_006253b0`) sent through `FUN_00548520`. Only caller `FUN_00571f90` | no counterpart (as `player_items_send`) |
| `FUN_00548a80` | `units_other_act` | `EAX` unit, stack unit: 2 when their rooms' levels are in different acts, else 0. Callers `FUN_00549150`, `FUN_0054b930`, the talk end `FUN_0054b9f0` | not compared |
| `FUN_005490e0` | `c2s_check_cursor_item` | Asserts the player (line 0x463); 0 (accepted) when the id is an item in mode 4 that is the player's cursor item, else 1. Callers C->S 0x17 (drop, `FUN_0054ab40`) and others | matches: d2d drops only the held item of that id (components/game/world/commands.cpp:585) |
| `FUN_005491b0` | `c2s_check_own_item` | Asserts (line 0x491); accepted when the id is a stored or worn item (mode 0 or 1) in the player's inventory (`FUN_0063ad50`). Only caller C->S 0x28 (socket) `FUN_0054b650` | matches: "the item stored or worn" (components/game/protocol.hpp:75) |
| `FUN_00549220` | `c2s_check_inv_item` | Asserts (line 0x4a8); accepted when the id is an item in the player's inventory that isn't the cursor item. Callers C->S 0x27 (identify with, `FUN_0054b280`), `FUN_00549150`, `FUN_0054b380` | not compared |
| `FUN_00549350` | `c2s_check_book` | Asserts (line 0x503); by the item's mode: 0, 1, 2, 4 as `c2s_check_inv_item`; 3 (on the ground) refused in another act (`units_other_act`), else the reach check `FUN_00548ef0`. Callers C->S 0x29 (scroll to tome, `FUN_0054b710`), `FUN_0054b790` | matches for a stored tome (components/game/protocol.hpp:78); the ground-tome branch has no d2d UI path |
| `FUN_00566b30` | `trade_backup_make` | `EBX` player: when player data `+0x5c` is free, a 0x18-byte record (line 0x806) and a 0x2000-byte buffer (line 0x807) with the player's items written in (`FUN_00531bb0`, line 0x809 if none), `+0x10` the gold. Only caller `FUN_00568060` | no counterpart: d2d has no player-to-player trade (components/rules/rules.hpp:2995) |
| `FUN_00566bf0` | `trade_backup_free` | Frees player data `+0x5c`, its chain at `+8` and the buffer (lines 0x822, 0x827, 0x82b) | no counterpart (no trade) |
| `FUN_00566d90` | `s2c_event_name_send` | S->C 0x5a with a name (up to 15 characters, `FUN_004135d0`) to the client (`FUN_005531c0`, line 0x8ce; `FUN_0053c850`). Callers the party command `FUN_005a5e50`, `FUN_00566e60`, `FUN_00567620` | not compared |
| `FUN_00566e20` | `inv_has_cube_item` | `EAX` player: 1 when any inventory item is on page 3 (the Horadric Cube, `FUN_00628250`). Only caller `FUN_00567620` | no counterpart (no trade) |
| `FUN_00567330` | `player_interact_end` | `FUN_00554190(unit)`, then `d2d_item_rebuild_scroll_skill_counts`. Callers `FUN_005350f0`, `FUN_005351c0` | not compared |
| `FUN_005678f0` | `trade_restore` | When the player has a trade backup: S->C (line 0x84f, `FUN_0053b3d0`), `FUN_005538d0`, each cursor-mode (4) item (and the cursor item) back onto the changed list and flagged, the player stripped and its items re-read from the backup (`FUN_00566cc0`). Callers `FUN_005679e0`, `FUN_00568060` | no counterpart (no trade) |
| `FUN_00568760` | `trade_cancel_both` | A thunk to `FUN_00568640`: when the player and its trade partner name each other, both get state 9 and event 0xd next frame, both restored and their backups freed, and S->C 0x77 (line 0xc2f). Only caller `FUN_005350f0` | no counterpart (no trade) |
| `FUN_00568d80` | `strnlen_or_minus1` | Fastcall (text, size): the length before a NUL within `size`, else -1. Only caller `FUN_00568f20` | not compared |
| `FUN_00568db0` | `oldsave_stub0` | `return 0`. Only caller `FUN_00534020` | no counterpart (the old loader) |
| `FUN_005697b0` | `items_write_at` | Writes the items (`FUN_00531bb0`) from `*cursor` up to `end`; a negative count returns 3, else advances the cursor and returns 0. Callers `FUN_005697f0`, `FUN_00569ad0` | not compared |
| `FUN_00569ec0` | `hotkey_decode` | A save's hotkey word pair: 0xffff → skill -1, left 0, item -1; else skill = low 12 bits, left = bit 15, item = the second word (0 → -1). Only caller the header check `FUN_0056a090` | not compared: d2d keeps the raw dwords (components/d2s/d2s.hpp:161) |
| `FUN_00569f20` | `player_join_init` | `FUN_005378e0`, `FUN_00553180`, the client's name onto the player, `FUN_00552df0`, `FUN_00624690(unit, 1)`, player data `+0x50`, `+0x54`, `+0x58`, `+0x5c` = 0. Callers `FUN_00569f80`, `FUN_0056a090` | not compared |
| `FUN_0056af20` | `inv_nth_item_id` | Fastcall (_, unit, n): the unit id of the n-th (from 1) item in its inventory list, -1 if none. Only caller `FUN_0056af80` | not compared |
| `FUN_005749b0` | `pet_record_set` | (owner, PetType row, pet id, 3 dwords): copies the dwords into that pet's node (`+8`, `+0xc`, `+0x10`) in the owner's list for the row (player data `+0x44`, 12 bytes a row). Callers `FUN_00533c70`, `FUN_0056aa50` | not compared |
| `FUN_00574c60` | `pet_list_free` | Frees each node of a row's list (`FUN_00574450`, line 0x4a5) and zeroes its head and count. Only caller `FUN_005754b0` | no counterpart: d2d's pets are owned values |
| `FUN_00574d90` | `pet_list_each_live` | Each node without bit 0 whose unit exists: `FUN_00574cc0`. Only caller `FUN_005754b0` | not compared |
| `FUN_00575380` | `pets_release_far` | Each live node whose unit is farther than the third argument (`FUN_006492a0`) from the given unit: released (`FUN_005750e0(id, 1)`). Only caller `FUN_005754b0` | not compared |
| `FUN_00575bc0` | `pet_lists_refit` | For each PetType row from 1: `FUN_00574570(player, list, row == 7, row == 7)`, then `FUN_00575900`. Callers `FUN_0053acc0`, `FUN_0057fca0` | not compared |
| `FUN_0057f070` | `player_event_ret1` | `return 1`. In the player event table (`0x6e1810` → `FUN_005811d0`) and `FUN_006c9900` | matches: a player's type-0 event does nothing (components/rules/combat.hpp:535) |
| `FUN_0057f4f0` | `cursor_item_to_corpse` | `ESI` player, `EBX` item: S->C 0x42 (line 0x27d) clears the cursor, the cursor item unset, the item flagged and on the changed list, `FUN_00621000(player, 1)`. Only caller the death `FUN_0057f700`, pass -1 | matches: the held item goes to the corpse first (components/game/world/travel.cpp:329) |
| `FUN_0057f540` | `death_drop_no_corpse` | See "Too many corpses" | matches (see above; tools/emu/corpse_cap.py) |
| `FUN_005801e0` | `player_skill_pair` | `EBX` unit, `ESI` 4 dwords: right skill's node id, left's node id (lines 0x57a, 0x56e), right skill id, left skill id (`FUN_00643ad0`); none: 0 and -1. Only caller `FUN_00580310` | not compared |
| `FUN_005805d0` | `life_fraction_128` | Fastcall unit: (life >> 8) × 128 / (max life >> 8); 128 for null, a max under 1, or life at the max. Callers the potion rate `FUN_005be3f0` and `FUN_005beac0` | not compared |
| `FUN_005a5d20` | `party_each_client` | `FUN_00554630`, then `FUN_00540510(FUN_005a5c70, unit)` and `FUN_005407a0`. Callers the party command `FUN_005a5e50`, `FUN_006c9900` | no counterpart: hostility isn't modelled (components/game/world/commands.cpp:359) |
| `FUN_005a5dc0` | `party_hostile_check` | `EAX` player: in a party (`FUN_00554630` ≠ -1), level (stat 0xc) above 8 walks the clients (`FUN_005a5d50`); else both units' state reset (`FUN_005a60e0`) and S->C (line 0x19c, `FUN_005a5690(id, 3)`). Only caller `FUN_005a5e50` | no counterpart (as `party_each_client`) |
