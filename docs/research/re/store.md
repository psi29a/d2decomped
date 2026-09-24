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
- The function that rolls counts and creates the items hasn't been
  located yet.

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
    misc → 3.
- Prices (hover: "Cost: " 0xd01 in the store, "Sell value: " 0xd03 on
  your own items while it is open), per FUN_0062efb0:
  1. base = the item's cost column (armor/weapons/misc.txt);
  2. plus quality extras: low quality −base/2; magic prefix + suffix,
     set, unique, rare/crafted affixes, each `mul·base/1024 + add`
     from the affix/set/unique cost columns;
  3. plus half of each socketed item's cost; ethereal sells for ¼;
  4. × npc.txt buy or sell mult / 1024, then × the quest mult / 1024
     for each quest flag done on the active difficulty;
  5. × quantity for non-stackables; a sale is capped at the NPC's
     max buy for the difficulty; the minimum is 1.
  Checked: an Akara Scepter (cost 350, buy mult 512) = 175.
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
- Not yet: magic stock, repair, quest-item refusal, messages, and the
  real roll.
