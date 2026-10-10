# Act 1 gaps — the ponytail triage (2026-10-10)

Every `ponytail:` note in components/ and apps/ (365 then) was read and
sorted by one question: does it change single-player Act 1 play?

## Fixed (they did)

| Gap | Commit |
|---|---|
| Single player didn't pause under the game menu (`FUN_0044efa0`) | dedfe9c |
| Bows / crossbows didn't shoot on a plain attack; Throw didn't throw; no ammo spent (`FUN_0056f070`, `FUN_0056f460`, `FUN_0056c3f0`) | 9782261 |
| No durability wear; armour never broke; empty quivers / stacks never refilled; repair didn't restock (`FUN_00559e30`, `FUN_00580030`, `FUN_005761c0`) | 0845176 |
| Sets.txt partial / full bonuses missing (`FUN_00660120`) | 8ed1327 |
| No shift + belt key to feed the merc (`FUN_00562390`) | a9d6004 |
| Vendor stock and the hire list rerolled on every opening; Cain's fee ignored the stash (`FUN_00577010`, `FUN_00576d90`) | e9a94ce |
| Stale notes (traced pets, the merc's attack think, Dragon Flight) | f72f211 |

## The rest (they don't)

Sorted by keyword, so the counts are rough (368 notes):

- **Multiplayer / networking (~33)**: party shares, other players,
  deltas, the D2GS codec. Wait for networking.
- **Later acts and difficulties (~30)**: Act 2+ remaps, level 24 / 30
  skills, Nightmare / Hell rows. Act 1 never reaches them.
- **Seed stand-ins (~32)**: d2d's rng where game.exe rolls a unit's or
  the game's own seed. The odds are the same, the exact rolls aren't.
  They matter once machines share a game.
- **Drawing, UI and sound (~55)**: fonts, colours, tooltips, placement
  by eye, blends in RGB. None changes what happens.
- **Install, tools and formats (~21)**: launcher, ISO / MPQ / TBL
  readers.
- **Other untraced details (~197)**: a game.exe function read as its
  published or observed behaviour (a target pick taken as the nearest,
  a timing taken from the animation, a check that never fails in
  Act 1). Each one names the function to trace when it matters.

Verified end to end: tests/quests_d2d.py plays all six Act 1 quests
headless.
