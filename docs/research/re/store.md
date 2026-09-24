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
- Not yet: magic stock, prices, buying/selling, repair, and the real
  roll.
