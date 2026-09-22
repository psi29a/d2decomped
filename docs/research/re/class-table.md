# Char-create class table (game.exe 1.14d)

The character-creation screen renders seven class silhouettes around a
central campfire. Each class is a five-slot animation set (nu1, nu2, fw,
nu3, bw). The screen assets are loaded by `FUN_004326f0` and the per-class
layout comes from the master menu table at `0x00708e00` (see also
`frontend-menu-table.md`).

## kClassKey order — array index → class

Our `Scene::class_anims[7][5]` and `kClassPos[7]` are stored in the same
order — this table is the one source of truth referenced everywhere else
(`--start-class N`, `kClassKey[N]`, position records, animation sprites).

| Idx | Class       | Anim prefix | TBL source     | TBL key       | Position record | (x, y, w, h)         |
|-----|-------------|-------------|----------------|---------------|-----------------|----------------------|
| 0   | Barbarian   | `ba`        | string.tbl     | `Barbarian`   | 0x70af30        | (400, 330, 88, 184)  |
| 1   | Necromancer | `ne`        | string.tbl     | `Necromancer` | 0x70afc0        | (217, 360, 88, 184)  |
| 2   | Paladin     | `pa`        | string.tbl     | `Paladin`     | 0x70b020        | (521, 339, 88, 184)  |
| 3   | Amazon      | `am`        | string.tbl     | `Amazon`      | 0x70b050        | (100, 337, 88, 184)  |
| 4   | Sorceress   | `so`        | string.tbl     | `Sorceress`   | 0x70aff0        | (626, 353, 88, 184)  |
| 5   | Druid       | `dz`        | patchstring.tbl| `Druid`       | 0x70b470        | (720, 370, 88, 184)  |
| 6   | Assassin    | `as`        | patchstring.tbl| `Assassin`    | 0x70b440        | (232, 364, 88, 184)  |

Note: our storage order (BA, NE, PA, AM, SO, DZ, AS) is *not* the same as
the RE master-table's row order (assassin, druid, amazon, necromancer,
barbarian, sorceress, paladin — see the class-list at `0x00708a00`,
16-byte records). We use left-to-right visual order for `class_anims` so
`--start-class` maps to something intuitive; the RE-order is only relevant
when reading a raw record from game.exe.

## Anim sprite paths

Sourced from `FUN_004326f0` decompilation (see the full listing in
`scratchpad/charselect-full.c` — not tracked). Path format:

    data\global\ui\FrontEnd\<class>\<prefix><suffix>.dc6

Class directories: `amazon`, `barbarian`, `necromancer`, `paladin`,
`sorceress`, `druid`, `assassin`. Suffixes (per state): `nu1`, `nu2`,
`fw`, `nu3`, `bw`. Example: Druid idle-1 = `druid\dznu1.dc6`.

The Sorceress, Necromancer, Paladin and Barbarian additionally load `s`
variants (`sonu3s`, `sofws`, `sobws`, `nenu3s`, `nefws`, `nebws`,
`banu3s`? no — just `bafws`, `pafws`) — see the file for the exact
inventory. These are the “selected” outfits with staff/skull/etc. drawn
in. We don’t use them yet.

## String precedence — patch → expansion → base

D2 resolves any TBL key (or ID) through three files in strict order:

1. `data\local\LNG\ENG\patchstring.tbl` — 826 entries; patch-shipped
   overrides. **Ships both `Druid` and `Assassin` in 1.14d** (verified via
   probe). Wins over the other two on collision.
2. `data\local\LNG\ENG\expansionstring.tbl` — 2788 entries; LoD-only
   additions (runewords, uniques added by LoD, skill descriptions, some
   `str<Class>Only` strings). Does *not* carry the base class names in
   1.14d.
3. `data\local\LNG\ENG\string.tbl` — 5099 entries; classic keys. Ships
   `Amazon`, `Barbarian`, `Sorceress`, `Necromancer`, `Paladin`.

Our `lookup_string(scene, key|id)` helper in `apps/d2d/main.cpp` walks
that chain. Missing key → `std::nullopt`; caller falls back to
`kClassKey[]` string literals.

## Frontend button label IDs (recap for reference)

From `frontend-menu-table.md` — the +0x18 field of each button record
carries a TBL ID:

| ID     | Meaning         | Source         |
|--------|-----------------|----------------|
| 0x13ed | EXIT            | string.tbl     |
| 0x13ee | OK              | string.tbl     |
| 0x13f2..0x13f7 | Main-menu buttons (SP/BN/OpenBN/Multi/Cred/Exit) | string.tbl |
| 0x140b | Character Name  | *(pending verification)* |
