# Monsters — spawning (1.14d game.exe)

Our port: `components/rules/monsters.hpp` (`monster_region`, `populate_room`,
`monster_stats`), loaded by `load_monsters` (apps/d2d/load.hpp), tested by
`tests/test_monsters.cpp`.

## Tables

- MonStats rows are 0x1a8 bytes (`[0x744304]+0xa78`, count `+0xa80`). Offsets
  used here: `+0x02` BaseId, `+0x0c..0x0f` flag bytes, `+0x20` spawn,
  `+0x26/+0x28` minion1/2, `+0x2c/+0x2d` PartyMin/Max, `+0x2e` Rarity,
  `+0x2f/+0x30` MinGrp/MaxGrp, `+0x31` sparsePopulate.
- MonStats and MonStats2 are **not** row-aligned (734 vs 609 rows in 1.14d):
  MonStats `MonStatsEx` names the MonStats2 row.
- 1.14d stats are percentages of MonLvl.txt: HP = MonLvl HP × minHP..maxHP / 100
  (zombie1 level 1: 7 × 101..181 % = 7..12). We read the LoD `L-*` columns.

## Monster region — FUN_005479c0 / FUN_005475e0

One region per level (0x2e4 bytes, `MonsterRegion.cpp:0x87`): density
(`+0x2b8`, Levels MonDen for the difficulty), MonUMin/Max (`+700/+0x2bd`),
MonWndr (`+0x2be`), monster level (`+0x2dc`, MonLvl or MonLvlEx).
FUN_005475e0 draws `min(NumMon, 13, list size)` types without replacement
from mon1.. (normal) or nmon1.. (NM/hell), keeping each whose MonStats
flag (`+0xc`) allows it as `{row, rarity}` (0x34-byte entries from `+0x14`,
count `+0x12`, rarity sum `+0x11`). The seed is one step of the game seed
(FUN_00547d20).

## Room population — FUN_0054ec90

Per room rect (tiles × 5 = subtiles), `(h/3)·(w/3)` rolls of the **game**
seed: `seed % 100000 <= density` (density capped at 10000) spawns:

1. FUN_005bde80: `room_seed(total rarity) + 1`, walk the entries subtracting
   rarity until < 1. (MonStats `spawn` may replace the pick 80 % of the
   time for flagged monsters; no act 1 wilderness monster has one.)
2. FUN_005be020 (room seed): unique/champion if under MonUMin (chance rising
   with rooms done) or under MonUMax (6 %); else a group. Its 1/2 results
   both become "group". Normal act 1 has MonUMin/Max 0 — one roll, a group.
3. Group size MinGrp..MaxGrp, but 1..1 for BaseId 19 (fallen1) and 91
   (scarab1) — FUN_0054ec40. sparsePopulate: `game_seed % 100 > sparse` skips.
4. FUN_0054df80 → FUN_0054dc40: up to 20 random spots in the room rect
   shrunk one subtile at the top left (room seed), skipping any within
   WarpDist² (Levels.txt, 2025) of the level's entrances (FUN_0054db50),
   testing placement (FUN_005b2a00 with flag 1 = test only).
5. The leader at that spot; FUN_005b2830 adds PartyMin..PartyMax minions
   (minion1/minion2 alternating, radius 4) — how Fallen come in packs;
   then `rand(max-min+1) + min - 1` more of the type (radius 3).

FUN_005b2a00 places by rings of 3 subtiles out to 3 × radius (radius −1:
the point only), each walked from a random point round the square (room
seed: parity bit picks the axis, `rand(c)` the offset, two sign bits), the
first spot inside the room whose collision (FUN_0064d9b0, MonStats2
spawnCol → mask 0x3c01 / 0x1c0 / 0x3f11 / 0) is clear.

## Not yet traced / approximated

- Room activation order (game.exe populates when a room first becomes
  active; we do every room at load in cell order) and the game seed itself
  (we use the map seed).
- The seed at `+0x20` used for party and group counts (we use the room's).
- Uniques/champions (FUN_005a43e0), the `spawn` replacement, level def
  `+0x31`'s first-pick retry.
- The monster's component roll and stat init.

Seed 3's Blood Moor: 155 monsters (107 fallen1, 24 quillrat1, 24 zombie1).
