# Vendor store (trade) — game.exe 1.14d

## Opening

NPC menu "trade" (`0x4b42b0`) → panel 0xc. The store shares the left
panel slot; the player's inventory opens on the right.

## Stock data (server side, `UNIT\sunitproxy.cpp`)

Item records (`0x1a8` bytes) carry 17-entry byte arrays indexed by
vendor:

| offset | field |
|---|---|
| 0x146 | Min |
| 0x157 | Max |
| 0x168 | MagicMin |
| 0x179 | MagicMax |
| 0x18a | MagicLvl |
| 0x1a4 | PermStoreItem |

These come from the armor/weapons/misc.txt columns `<Vendor>Min` … (the
field table in `FUN_006315d0`).

Vendor index by NPC hcIdx (`FUN_00536f80`):

| index | vendor | hcIdx |
|---|---|---|
| 0 | Akara | 0x94 |
| 1 | Gheed | 0x93 |
| 2 | Charsi | 0x9a |
| 3 | Fara | 0xb2 |
| 4 | Lysander | 0xca |
| 5 | Drognan | 0xb1 |
| 6 | Hratli | 0xfd |
| 7 | Alkor | 0xfe |
| 8 | Ormus | 0xff |
| 9 | Elzix | 199 |
| 10 | Asheara | 0xfc |
| 11 | (Cain) | — |
| 12 | Halbu | 0x101 |
| 13 | Jamella | 0x195 |
| 14 | Malah | 0x201 |
| 15 | Larzuk | 0x1ff |
| 16 | Drehya | 0x200 / 0x202 |

- `FUN_00536d50(vendor)` builds the vendor's candidate list: items with
  the spawnable flag (+0x133) and Max or MagicMax > 0. Each ordinary
  entry is 12 bytes `{min, max, magicMin, magicMax, code, magicLvl}`;
  permanent items only store their code.
- `FUN_00536070` copies those lists to each vendor NPC
  (NPC record +0x1d24).
- `FUN_00537230` restocks after 240 000 ms (4 minutes).
- The fill is FUN_00576980 (SUNITNPC.CPP), from the trade deal
  (FUN_00579430: when the NPC's proxy record +0x27 says restock, the old
  stock goes (FUN_00536d10) and it fills; Kashya, Greiz and Larzuk's
  records don't). On the proxy seed (game +0x1d24 +8, FUN_00536b40: a game
  seed step at game start, FUN_00536070):
  - the store level (FUN_00576890): clvl + 5, in Normal at most 12, 20,
    28, 36, 45 by the act (proxy record +0x22);
  - each candidate (Items order: weapons, armor, misc) whose level
    (Items +0xfd) is reached: normal copies min + rand(max + 1 − min)
    (FUN_004bc500; none from store level 25); an expansion base (+0xf6
    version 100) only in an expansion game; each copy's quality
    (FUN_00576900: rand(100), store level < 5: > 90 low; < 10: > 85
    superior; else > 74 superior; else normal); then, when the base can
    be magic (bitfield1 bit 0, FUN_00629c80) and MagicLvl is reached,
    magicMin + rand(magicMax + e − magicMin) magic copies, e = 1, or 1 +
    rand(2) + 1 from store level 25;
  - each permanent item once, normal; a quiver (cqv / aqv) filled to its
    max stack (stat 0x46, FUN_006295b0);
  - each copy is FUN_00576330; over 32 that don't take end the fill.
- FUN_00576330 (the maker): in Nightmare above clvl 25, rand(100000) on
  the proxy seed under ilvl·64 + 4000 takes the ubercode, else the
  NightmareUpgrade; in Hell, an expansion game's roll under ilvl·16 +
  1000 the ultracode, else under ilvl·128 + 5000 the ubercode, then the
  HellUpgrade wins. FUN_00559ce0 → FUN_00558d90 makes it (unit seeds off
  the game seed, as a drop); a low one named "Cracked" is thrown away and
  made again (5 tries); one that came out another base is made again
  normal; then its store page (FUN_0062e880: ItemTypes +0x22) and a spot
  in the NPC's grid (FUN_00560200; page 1 full: page 2).
- Checked: `tools/emu/store.py 1-100` (FUN_00576980 native, the maker
  hooked) against `drlg-dump ... store` (`rules::store_fill`): 10200 /
  10200 lines, every vendor, Normal to Hell, clvl 1 to 90.

## Panel (`FUN_00488400`)

The base y is 539 (= 60 + 255 + 224):

- **Background**: `ui\panel\buysell` frames 0..3 as 2×2 at (80, 60),
  like the stash.
- **Tabs**: `ui\panel\buyselltabs`, 79×31, frame i if active else i+4.
  - Position: x `80 + 80*i`, bottom `539 - 0x1c1 = 90`.
  - Labels (records at `0x722110`, 18 bytes each): x 42/121/201/281
    (+80), baseline `20 + 539 - 480 = 79`, font16, colour 4 (gold) when
    active.
  - Strings: 0xfc4 Armor, 0xfc5 Weapons, 0xfc5 Weapons, 0xfc7 Misc.
- **Buttons**: `ui\panel\buysellbtn` (32×32), frame = base + pressed.
  - Position: x `80 - 1 + table[mode*4 + i]` (`0x722168`; mode 3 → 116,
    169, 221, 273), bottom `539 - 0x3f = 476`.
  - Frames (`FUN_00487ed0` / `FUN_00487e20` / `FUN_00487e60`):
    - buy: 2 ("Buy", 0xd07);
    - sell: 4 (0xd08);
    - repair vendors (Charsi, Fara, Hratli, Halbu, Larzuk): repair 6
      (0xd0a) and repair all 18 (0x276f);
    - everyone else: an empty slot (0) and close 10 (0x1030).
- **Stock grid**: inventory.txt `Monster2`, 10×10 at (96, 123), 29 px.

## d2d

- Panel, tabs, buttons, grid and hover text are as above.
- Clicking a tab switches it; Close and Esc close the store.
- Stock:
  - each listed item Min..Max times, plus PermStoreItems once;
  - packed first-fit: armour → tab 0, weapons → 1 (spilling into 2),
    misc → 3;
  - rolled at the first opening and kept for the game (the vendor's own
    inventory on the server), what a trade leaves in it included.
- The hire lists (`FUN_00576070`, npc-menu.md "Hire"): game.exe's, on
  the game seed, ten of the seller's names offered; an empty list is
  rolled anew (`FUN_00577010`).
- Cain's identify fee and other payments take the purse first, then the
  stash (`FUN_00576d90`).
- Prices (hover: "Cost: " 0xd01 in the store, "Sell value: " 0xd03 on
  your own items while it is open), per FUN_0062efb0:
  1. base = the item's cost column (armor/weapons/misc.txt);
  2. plus quality extras: low quality −base/2; magic prefix + suffix,
     set, unique, rare/crafted affixes, each `mul·base/1024 + add`
     from the affix/set/unique cost columns;
  3. plus half of each socketed item's cost; ethereal sells for ¼;
  4. × npc.txt mult / 1024, then × the quest mult / 1024 for each quest
     flag done on the active difficulty. The columns are the vendor's
     side: a buy takes "sell mult" and "questsellmult", a sale "buy mult"
     and "questbuymult";
  5. × quantity for non-stackables; a sale is capped at the NPC's
     max buy for the difficulty;
  6. a buy, gamble or repair takes the player's item_reducedprices
     (stat 87, at most 99) off: price − price × r / 100; the minimum is 1.
  Checked live on a 1.14d host (2026-10-05; Akara, Den of Evil done, a
  14% Gheed's Fortune): mp1 47, hp1 24, isc 62, tsc 78, vps 19, yps 31 =
  cost × 922 / 1024 − 14%. d2d had the columns swapped (buying at half,
  selling at full) and no stat 87.
- Gold (FUN_00488100, font16 white; 800x600 offsets 0x7a2858 = 80,
  0x7a285c = −60):
  - inventory: goldcoinbtn at bottom-left (484, 469), carried gold
    (stat 14) at x 508, baseline 468;
  - store open (0x7bcbf0 in 1..9): "Stash" (0xcf3) at x 101, baseline
    434, stash gold (stat 15) right-aligned to x 278.
- Buying and selling:
  - right-click stock buys; Buy or Sell toggled on makes a left click
    buy the stock item or sell the clicked inventory item;
  - buying needs a free 10×4 inventory spot (column-major first fit) and
    enough carried + stash gold (carried first, a guess); perm items
    stay in stock;
  - selling adds the sell value to carried gold (capped at clvl × 10000)
    and puts the item into the vendor's stock.
  - All in memory; the save isn't written.
- The World's stores are game.exe's (2026-10-11): `game::open_store`
  runs `rules::store_fill` on the proxy seed and makes each ask with
  `rules::make_store_item` (the upgrades, the item off the game seed as a
  drop, magic affixes as drops.md "Magic", the Cracked retry), on its
  type's StorePage. Not yet: FUN_00560200's own spot search (first fit by
  column), the 4-minute restock (FUN_00537230: the stock lasts the game),
  quest-item refusal, messages.

## Repair (FUN_0062efb0 mode 3)

The price function computes buy, sell and repair together. Repair:

1. base = the buy base with its quality extras (no ethereal ¼);
2. nothing for items with no durability, whole ones, ethereal ones
   or indestructible ones; a throwable (FUN_0062ba80, ItemTypes
   Throwable) skips the durability term;
3. × (max − current) / max, where max is the maxdurability stat (73)
   with item_maxdurability_percent (75) — the save stores the base max;
   a stacking throwable (ItemsTxt +0x132 stackable, FUN_006289f0) pays
   per unit instead: base / max stack × the missing quantity;
4. × npc.txt rep mult and the quest rep mults / 1024; minimum 1;
5. plus the charge recharge cost (FUN_00628d30) — not in d2d yet.

The repair itself (`FUN_005761c0`): a stacking throwable's quantity back
to its max stack (`FUN_006295b0`); a broken item (flag 0x100) unbroken
(`FUN_0055f900`), else its durability to max.

## Wear (`FUN_0057d4f0` → `FUN_0057d3d0` → `FUN_00559e30`)

Each landed hit: a player attacker wears its weapon; a player defender
wears one armour piece, picked by weight from the table at 0x732b90
(7 pairs: head 3, torso 5, right / left arm 4, belt 2, feet 2, gloves 2)
among the slots holding armour (ItemTypes 50): a random start slot, a
random point in the total weight, walked round the slots.

`FUN_00559e30` on an armour (50) or weapon (45) with durability: a roll
on the unit's seed under 10 (armour, a throwable) or 4 (other weapons)
takes 1 durability. At 0: armour breaks (`FUN_0055f850`: flag 0x100, its
stats off the owner); a stack loses one of its quantity and comes back
whole, its last running out as ammo does; any other weapon stays at 0.

Ammo run out (`FUN_00580310` → `FUN_00580030`): a magic or better weapon
(quality 4..9, `FUN_0062a0f0`) breaks and stays in hand; anything else
is destroyed and the inventory's next of the same class (not broken)
moves into the slot (`FUN_00562a30`).

Buttons at repair vendors: repair (frame 6) toggles like buy/sell and a
click repairs the worn or carried item; repair all (18) repairs every
worn and carried item (not the stash). `components/rules`
`repair_cost` / `store_repair` / `store_repair_all`.

## Gambling (FUN_0062efb0 mode 2 → FUN_00629370)

Menu entry 0xd46. Price: rings and amulets cost their "gamble cost"
column. Everything else, with C = max(clvl, 5), qlvl the base's level:

    pb  = max(0, (clvl − exceptional qlvl) × 100 / 2 + 1)    (of 10000)
    pu  = max(0, (clvl − elite qlvl)       × 100 / 4 + 1)
    mix = ((10000 − pu − pb) × cost × avg stack + elite cost × pu
           + exceptional cost × pb) / 10000
    price = ((max(qlvl − 45, 0) − qlvl/2 + C) × 250 / 3 + mix)
            × ((2C + 1) / 3 + 20) / 15

(ItemsTxt fields: +0xd4 gamble cost, +0xe0 cost, +0xe4/+0xe8 min/max
stack, +0xfd level, +0x88/+0x8c uber/ultra codes.)

d2d rolls the item with the same upgrade weights, level clvl−5..clvl+4,
and DifficultyLevels GambleUnique / GambleSet / GambleRare per 100000,
else magic (`gamble_item`); the server's own roll isn't traced. Stock:
every gamble.txt base up to clvl (the game shows a random subset).

## Item generation (d2d, `components/rules` `generate_item`)

Affix level `ilvl − qlvl/2` below `99 − qlvl/2`, else `2·ilvl − 99`;
MagicPrefix/MagicSuffix rows (raw rows = the save's IDs) by spawnable,
level, maxlevel, rare, itype/etype and group, weighted by frequency;
mods → stats through Properties.txt funcs (1/2/3/5/6/7/8/10/11/15/16/17/
19/20/21/22). Uniques and sets by code and level, weighted by rarity,
else rare.


## Stash gold, button hovers

- The stash's gold (`FUN_00488100` with 0, LoD): goldcoinbtn frame 0 (1
  held) at x 155, bottom 100; the stash gold (stat 15) at x 245,
  baseline 100. Over the coin's box (`FUN_00486820`: 20x18 up from
  (153, 102)) string 0x101c "Withdraw" at x 133, baseline 82. The
  inventory's coin box is (483, 471) the same way.
- The server's 0x4f buttons with a stash open (`FUN_00564d50`): 0x13
  withdraws `n` (n <= stash gold, carried + n <= clvl x 10000
  (`FUN_00622e70`), else nothing moves and a message); 0x14 deposits
  `n` <= carried, up to the stash's 2,500,000 (`FUN_00623460`).
- The coins open the gold box (`0x454150`, mode in `0x7a279c`): 1 drop
  (the inventory's coin), 2 a trade's offer, 3 deposit (the inventory's
  coin with the stash open), 4 withdraw (the stash's). Prompts 0xfc1 /
  0xfce / 0xfd1 / 0xfd2 wrapped to 200 px, 24 down; `menu\dialogbackground`
  (210x158) top-left at (215, 140); `menu\spinner` (`0x4bc340`) bottom
  219 / 232 at x 224; the amount, an edit box (`0x4bbd80`) up to the gold
  it's from (stat 14, 15 to withdraw), 10 digits, bottom 228 from x 258,
  100 wide; `panel\buysellbtn` OK (frame 16, 0xd49) / Cancel (10, 0x102e)
  bottom 287 at x 250 / 355 (`0x4baf20`, table `0x7275a8`). OK
  (`0x454080`): 0/1 C->S 0x50 (u32 player id, u32 amount; `FUN_00535510`
  drops it), 2 `FUN_004b9110` (0x4f 8), 3 0x4f 0x14, 4 0x4f 0x13; an
  amount of 0 sends nothing but a trade's.
- The spinner (`SpinnerImplementation`, vtable 0x6da83c; the box's
  callback 0x453fe0 adds the step, at most the gold it's from, or takes
  it, not below 0): a click on an arrow (`FUN_004bc0a0`) notes the tick
  and steps 1. While held, each draw (`FUN_004bbe70`) counts one more
  (+0x40, cleared on release) and asks `FUN_004bbfb0` for a step: none
  if under 70 ms (0x46) since the last, else 1 for the first second, the
  count >> 5 past 1000 ms, >> 4 past 2000, >> 2 past 3000, and (ms held
  >> 11) x the count past 4096. d2d counts the draws at 25 a second.
- A button's hover (`FUN_004886c0`): box x .. x + 0x2d by 432..474, the
  string centred on x = 80 + the button table's x, bottom 440. Repair
  all's string 0x276f ("Repair all equipment: %d") takes the total
  repair cost (`FUN_0062fe60`).
- A bought scroll goes into a carried tome with room (`FUN_00577830` →
  `FUN_0055f640`), as one picked up does. 0x29 (`FUN_0054b710` →
  `FUN_0055ef20`) puts the held scroll into a stored tome of the same
  spell, quantity (stat 70) under its max.
