# ItemMode.cpp and Items.cpp leftovers — game.exe 1.14d

For the detailed branch and ABI audit, completed name inventory and bounded
original-code lifecycle checks, see [the module trace index](items-module.md).

The server's item moves (`.\ITEMS\ItemMode.cpp`) and the small item
getters (`.\ITEMS\Items.cpp`) that [items.md](items.md),
[weapon-switch.md](weapon-switch.md) and [d2common-items.md](d2common-items.md)
don't cover. Item modes (unit `+0x10`): 0 stored in a grid, 1 worn, 2 in
the belt, 3 on the ground, 4 on the cursor. The page (item data `+0x45`,
`FUN_00628250`): 0 the inventory, 3 the Horadric Cube. Items.txt offsets
are named from its column list (`FUN_006315d0`); re-check with
`tools/ghidra/fn.py <address>`. The C→S table is `0x6e0d18` (8 bytes an
entry: handler, flag), so a handler's packet id is its offset / 8. "No
caller" rows were byte-scanned and are in [stubs.md](stubs.md).

## C→S handlers' bodies

| Packet | Handler → body | What the body does |
|---|---|---|
| 0x19 | `FUN_0054acd0` → `FUN_00560420` | A stored item to the cursor: refused while the cursor holds one (unless the last argument); for a player, page ≠ 0 also needs `FUN_00535060`. Out of the grid (`FUN_0063ad90`, line 0x965), page 0xff, cursor (except a player's page 1), mode 4; with arg 5 = 1 flags 0x20 (arg 6), page 2 `FUN_00567400`, flag 4, onto the changed list |
| 0x1f | `FUN_0054b0f0` → `FUN_00561b00` | Swaps the cursor item (mode 4) with a stored one: the cube can't go into page 3 (its class from `box `, cached at `0x88c6fc`), page 2 is refused; the stored item to the cursor (flag 0x40000, line 0xf15), the cursor item into the grid at x, y (`FUN_0063bcc0`, `FUN_0063b210`). The later `page == 2` branch (`FUN_00568770`) can't run after that refusal |
| 0x22 | `FUN_0054b380` → `FUN_0055e9a0` | `FUN_0055e9a0` is `return 0` |
| 0x24 | `FUN_0054b450` → `FUN_00562250` | A belt item (mode 2) to an empty cursor: flag 0x800, then the column above drops (`FUN_0055edc0`) |
| 0x25 | `FUN_0054b4e0` → `FUN_0055eb30` | A beltable cursor item (mode 4) swapped with a belt item (mode 2, lines 0x1290..0x12d8) |
| 0x2a | `FUN_0054b790` → `FUN_005628c0` | An item (mode 3 or 4) into a stored Horadric Cube (`box `): page 3 through `FUN_00560200`; while the cube is open (`FUN_005678a0(1)`) and not on page 0, `FUN_00553380` instead |

## ItemMode.cpp

| Address | What |
|---|---|
| `FUN_00558530` / `FUN_00558580` | With stat 0xfc / 0xfd (replenish durability / quantity) set and no timer 3 queued (`FUN_005415a0(3)`), queue timer 3 at frame + 2500 / rate + 1 |
| `FUN_00558a10` | When a dropped item disappears: never for `FUN_00628cd0`; magic (quality 4) frame + 30000; set to crafted (5..9) + 45000; gold above 10000 + 45000; ItemTypes 0x35 + 30000; else + 15000 |
| `FUN_00558b90` | (game, act): removes the act's ground items whose time (item data `+0x24`) has come (`FUN_0061a270`, `FUN_00623830`, `FUN_0064c370`, `FUN_005538d0`) |
| `FUN_00558c50` | Clears item flag 0x4000000 and `FUN_00627ef0`, and frees the item's state-0xab stat list |
| `FUN_00558cb0` | Makes an item from a saved record (`FUN_00555230`, `FUN_0062e430`): flag 0x80000 on, 0x2000 off, the replenish timers |
| `FUN_00559130` | Drops an item at a spot: level from the area (`d2d_ai_area_monster_level` − 1, at most 0x40), a free cell (`FUN_0064e810`, mask 0x3e01), a 0x84-byte create record into `FUN_00558d90`, copied out. Caller `FUN_00552140` |
| `FUN_00559830` | The same with the level 1 outside an area and the record's kind from arg 6. No caller |
| `FUN_0055bed0` | A unit in mode 3 without flag 0x10: `FUN_00537860`, then `FUN_0053ec90` (flag 0x1000) or `FUN_0053ecf0` |
| `FUN_0055bf30` | `FUN_0055bed0` when unit `+0xc4` bit 0 |
| `FUN_0055bff0` | (ESI item): the books.txt skill of a type-0x16 item (`+8`) or type-0x12 (`+0xc`), by its code (`FUN_006374b0`); -1 else |
| `FUN_0055c110` | (EDI player, EAX item, add): that skill's charge count follows the item's quantity (stat 0x46; a type-0x12 item without one counts 1): learnt when missing (line 0x225), count ± quantity (fatal 0x230), S→C 0x22 (`FUN_0053c520`); at 0 a hand skill on it is cleared (`FUN_005701b0`) |
| `FUN_0055c270` / `FUN_0055c6e0` | A player's item in / out: `FUN_0055c110(1)` / `(0)` (fatal 0x24d / 0x394 on null) |
| `FUN_0055c460` | `FUN_0055c2c0`. Caller `FUN_0054ced0` |
| `FUN_0055c470` | (EAX unit, EDI &wielded, EBX &other): the wielded weapon and the other hand's item when it's a weapon (type 0x2d) |
| `FUN_0055c4d0` | The skill usable (`FUN_00647960` not 2 or 7) |
| `FUN_0055c4f0` | Puts a kept right / left skill back (EBX picks; `FUN_00623060` / `FUN_006230c0`) when it's still usable and not EDI |
| `FUN_0055c560` | The skill's ItemTypes (`+0x18`) is-a 0x30 and `+0x14` is 2, and it's usable |
| `FUN_0055ca00` | Are two items' codes (`+0x80`) the pair (a, b) either way |
| `FUN_0055ca40` | Would picking this item make two of a kind: a unique with UniqueItems flag `+0x2c` bit 2 when another of its row is held off page 1; or another quest item (Items `quest` `+0x12a` equal) of the same code or a pair: `j34`/`g34`, `bks`/`bkd`, `d33`/`g33`, `hst`/`msf`, `hst`/`vip`, `qf2` with `qf1`, `qhr`, `qey`, `qbr` |
| `FUN_0055d930` | Empty (`ret`). Caller `FUN_0057f410` |
| `FUN_0055d940` | (EDI player, ESI item): `d2d_rules_wield_leaves`, then `FUN_0055c730` when worn. No caller |
| `FUN_0055d9d0` | (EAX unit, ESI out): life, mana, stamina (stats 6, 8, 10), the right and left skills' ids and `FUN_00643ad0` (lines 0x9df, 0x9eb) |
| `FUN_0055df10` | (game, player, item, tell): takes a stored item off the grid (line 0xf7b), page 0xff, optionally tells (0xf7e), `FUN_00557fd0` |
| `FUN_0055e8e0` | Fills a 0x84-byte create record from a unit (EDI) at its spot |
| `FUN_0055ed30` | Puts an item back at its cell (`FUN_0063c550`, line 0x12e9) and `FUN_00557fd0` |
| `FUN_0055edc0` | (game, player, slot): the belt slots above in that column (rows of 4) each move down one |
| `FUN_0055f2a0` | The item in the other hand (`d2d_rules_other_side`). No caller |
| `FUN_0055f6e0` | Sets quantity (stat 0x46), tells the client (line 0x18e0), `FUN_0055c070` |
| `FUN_0055f740` | (game, player, item, test): a stored type-0x12 item on page 0 that matches (`FUN_0055f5d0`) with room for a type-0x16 item's quantity; unless testing, it takes the quantity (line 0x1905). No caller |
| `FUN_0055f9c0` | Zeroes the item skill's count and tells the client (line 0x1962) |
| `FUN_0055fb10` | An item to mode 4 with flag 0x100000, onto the changed list (0x19a0, 0x19a1) |
| `FUN_0055fb80` | A gamble pick: one of two code lists (counts at `+0` and `+0x7c`) by the game seed (`+0x20`, multiplier 0x6ac690c5), upgraded to ubercode (`+0x88`) / ultracode (`+0x8c`) by DifficultyLevels GambleUber / GambleUltra rolls (`FUN_0045c390`); a code above the player's version is fatal (0x19c9). No caller |
| `FUN_0055fcc0` | Takes off every worn item (node kind 3, or kind 1 failing its requirements) and empties the inventory (`FUN_0063e0b0`; lines 0x19fd, 0x1a0f) |
| `FUN_0055fde0` | `FUN_0055dbc0(0)`, then `FUN_00621000` when arg 3 |
| `FUN_0055fe00` | Item flag 0x10; a free item whose requirements changed refreshes the player |
| `FUN_0055fe60` | `FUN_0055c2c0(arg, 0)` |
| `FUN_00561e70` | Tells the client (line 0xf94, `FUN_0053eed0`), then `FUN_0055ed30` |
| `FUN_00562c40` | One point of a stat (EBX, the rate in EAX) up to the max, the client told (S→C, line 0x1580), flag 0x100 rechecked; then the timer again at frame + max(2500 / rate + 1, 125) |
| `FUN_00562d30` | Timer 3 (the table's entry, `0x6e1188`): an unbroken item with durability gets a point (`FUN_00562c40` up to `FUN_00625e00`), else a stackable one (`FUN_006289f0`) a unit of quantity |
| `FUN_00562da0` | Runs item timer n from the table `0x6e117c` (a bad pointer is fatal, 0x15ea). Caller `FUN_00541320` |
| `FUN_00563350` | Each socketed child of an item is copied (`FUN_0055a2a0`) and must still fit (`d2d_rules_takes_socket`) |
| `FUN_00563470` | Puts an item back by mode: stored off the grid (told, line 0x1a75), worn unequipped (`FUN_00560cd0`), cursor `FUN_0055eea0` |
| `FUN_00563840` | Emptying a Horadric Cube (`box `): each page-3 item to the inventory (`FUN_0063b950`, line 0x817), else to the ground (`FUN_00555da0`, `FUN_00558aa0`) |
| `FUN_00563fe0` | Transmogrify (Items `+0x139`): a stored item becomes TMogType (`+0xc8`) with quantity TMogMin..TMogMax (`+0x13a`, `+0x13b`), into the grid, else anywhere, else dropped (`FUN_00563c00`) |

## Items.cpp

| Address | What |
|---|---|
| `FUN_00627c90` / `FUN_00627d00` | Allocate (0x74 bytes, `+0xc` = -1, lines 0xd2, 0xd5) / free (out of its inventory first, 0x10a) an item's data |
| `FUN_00627dc0` | `FUN_00650e30` on an item with data |
| `FUN_00627e20` | Item data `+0x10` (lines 0x187..0x18b) |
| `FUN_006281b0` | Item data `+0x14`. No caller |
| `FUN_006282a0` / `FUN_006282d0` | Item data byte `+0x46` get (0xff none; no caller) / set |
| `FUN_006283f0` | Item data byte `+0x49` |
| `FUN_00628440` / `FUN_006284c0` | ItemTypes row's word `+0xc` / byte `+0x13`. No caller |
| `FUN_00628530` | Items `2handedwclass` (`+0xc4`) |
| `FUN_006285f0` | Items alternategfx (`+0x90`), else code (`+0x80`) |
| `FUN_00628660` | Items `component` (`+0x115`) |
| `FUN_006288a0` | Items `missiletype` (`+0xfa`), -1 for a non-item |
| `FUN_00628900` / `FUN_00628950` | Is the class's / item's `wclass` (`+0xc0`) arg (fatal 0x8eb) |
| `FUN_00628980` | Items `2handed` (`+0x11c`) |
| `FUN_00629620` / `FUN_00629660` | Items `spawnstack` (`+0xec`) by class / item |
| `FUN_006296b0` | Items `minstack` (`+0xe4`) |
| `FUN_00629740` | Items bitfield1 (`+0xdc`) bit 3. No caller |
| `FUN_006297a0` / `FUN_00629800` | Items `durwarning` (`+0x135`; no caller) / `qntwarning` (`+0x136`) |
| `FUN_00629860` / `FUN_006298a0` | Items `strbonus` / `dexbonus` (`+0x106` / `+0x108`), 100 when 1 |
| `FUN_006298e0` / `FUN_00629990` | Items `hasinv` / `gemsockets` (`+0x137` / `+0x138`). No caller |
| `FUN_00629a00` / `FUN_00629a40` | Items `gemapplytype` (`+0x13e`) by class / item |
| `FUN_00629b50` | Is ItemTypes row a one of b (the bit matrix from `FUN_00638d80`, [datatbls.md](datatbls.md)) |
| `FUN_00629d00` | Items bitfield1 bit 2. No caller |
| `FUN_00629e40` / `FUN_00629ea0` | Item data `+0x1c` / `+0x20` get / set |
| `FUN_00629ed0` / `FUN_00629f20` | Item data `+0xc` set / get (lines 0x10bc..0x10ca) |
| `FUN_0062a060` / `FUN_0062a0a0` | Items Transmogrify (`+0x139`) set, by class / item |
| `FUN_0062a250` | (EAX transform 1..8, ECX colour < 0x15): transform × 32 + colour, else 0 (the tint byte, [compcode.md](compcode.md)) |
| `FUN_0062a2b0` | Items `compactsave` (`+0x143`) of EAX |
| `FUN_0062a310` | Quantity (stat 0x46) above 0: true; else with stat 0x7d it's set to 0 and true; else true only when the max (`FUN_006295b0`) is 0 or less |
| `FUN_0062a360` | `return 0`; six callers |
| `FUN_0062a490` | A set item's SetItems row (0x1b8 bytes at `+0xc18`) |
| `FUN_0062a670` | Item data word `+0x30` (lines 0x1504..0x1507); 42 callers |
| `FUN_0062a900` | (EAX item): how many items are socketed in it (with `hasinv`) |
| `FUN_0062ac80` | Reads one saved stat (EAX index, ESI the stat's save record): saves before 0x5a / 0x5d remap stats 0x5c..0x6a and the widths of 0x17, 0x18, 0x4a, 0x4f, 0x50; value − save add, shifted, into the list |
| `FUN_0062ae20` | Reads a saved item's header (bits, `FUN_00411020`): `JM` (0x4d4a) when asked, 32 flag bits, 10 skipped, mode 3 bits; on the ground or dropping (3, 5) x and y 16 bits, else location 4, x 4, y 4, page 3 − 1; then the code (`ear ` for flag 0x10000; before version 0x5d `nec ` reads as `neg `) and 3 socket bits unless flags 0x2200000 |
| `FUN_0062af50` | Writes a value in n bits, clamped to the field (32 bits as is) |
| `FUN_0062b3a0` | A set item's SetItems word `+0x2e`, -1 else |
| `FUN_0062b450` | (stat, &sum; EBX parent item): sum the stat over socketed children only when the **parent** is in mode 1. Assembly at `LAB_0062b496` sets ECX=EBX before the mode call; it does not inspect child modes. See [the detailed trace](items-common-trace.md). |
| `FUN_0062b560` | `FUN_0062b4d0` of an item's class (lines 0x78c, 0x78d). No caller |
| `FUN_0062ba60` | `d2d_item_text_required_level` |
| `FUN_0062bbd0` | ItemTypes byte `+0x1f` of the item's type (7 without one) |
| `FUN_0062be00` | Sets an item's sockets: at most invwidth × invheight (cap 6) and `FUN_0062bc20`, at least 1; flag 0x800, stat 0xc2 |
| `FUN_0062beb0` | `FUN_00629bb0(item, 0x35)` |
| `FUN_0062c9f0` | (ESI item, stat): raises a damage or armour stat to its Items floor: 0x10/0x1f armour (type 0x32 with maxac `+0xd0`) to its value + 1, at least maxac + 1; 0x11/0x16 maxdam `+0xff`, 0x18 2handmaxdam `+0x103`, 0xa0 maxmisdam `+0x101` (throwable); 0x12/0x15 the minimums `+0xfe`, 0x17 `+0x102`, 0x9f `+0x100` |
| `FUN_0062e410` | `FUN_0062ae20(..., 0x60)`; 26 callers |
| `FUN_0062e7e0` / `FUN_0062e830` | ItemTypes bytes `+0x11` / `+0x12` of the item's type |
| `FUN_0062e920` | The item type's cel variant n (`+0x24 + 32 n`, capped at `+0x23` − 1); `0x7446e4`'s string without one |
| `FUN_0062ea80` | ItemTypes bytes `+0xa` / `+0xb` of the item's type |
