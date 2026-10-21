# The Party screen — game.exe 1.14d

UI 0x16, `.\UI\Party.cpp` (0x499e50..0x49c3d0). The key config's Party
Screen (`0x468960`, P by default) toggles it, in a multiplayer game only
(`FUN_0044db30`). It is a left-hand panel. d2d: `apps/d2d/party.{hpp,cpp}`;
the joined game's data and packets are in `netgame.cpp`. devctl `debug
party on` shows it with three sample players.

Layout came from running `FUN_0049a640` (it builds the rows) and
`FUN_0049c3d0` (it draws) in the emulator on a made-up roster, with the
drawing calls hooked.

## The roster

`DAT_007bb5c0` is a list (+0x80 next) of `{+0x10 unit id, +0x1c class,
+0x20 u16 level, +0x22 u16 party, +0x24 area, +0x30 relationship,
+0x66 name}`.

Relationship (S→C 0x8b) is 0 INVITE, 2 ACCEPT, 3 LEAVE and 4 asked, our
own invitation waiting. Flags between two players (S→C 0x8c, read by
`FUN_004dc440`) are 1 loot, 2 not heard, 4 squelched and 8 hostile.

`FUN_0049a640` makes a row for every other player, at most nine. The rows
sit at y 0x5a + 0x26·i (panel coordinates, +0x50 / +0x3c at 800×600).

## Drawing

- `Menu\party`, four frames, as `NPCInv` is drawn.
- The player's own name is centred at 0x69, baseline 0x1f, in the
  current font. In a party, `Menu\partybuttons` sits at (0xe3, bottom
  0x23) with "LEAVE" (0x1038) centred on it in green.
- Each row is in font8 (`FUN_0049c270`). The colour is green (2) for our
  party, orange (8) for another party, red (1) when hostile, otherwise
  white:
  - the name at (0x24, y − 2), cut with "..." past 100;
  - the class (0x103a.., druid 0x2771, assassin 0x2772) at (0x24, y + 12);
  - "Lvl: n" (0xd28 0xf9d 0xf9b) at (0x10a, y + 12);
  - a party member's area at (0x7e, y + 12), cut at 0x8c.
- `FUN_0049a690`: the party button, `partybuttons` frame 0 at (0xbe, y),
  with INVITE / ACCEPT / LEAVE (0x1036..0x1038) centred on it. Then the
  `partyboxes`:
  - hostile at (0xe, y + 7): 4 off, 6 on;
  - loot at 0xf5: 0xc / 0xe;
  - hear at 0x10a: 0 / 2;
  - squelch at 0x11f: 8 / 10.

  The hostile box is washed red when it can't be used: either player
  under level 9, or the player out of town.
- `FUN_0049a9b0`: `Menu\partybar` frames 0 / 1 at x 0xc / 0x10b between
  two parties.
- `buysellbtn` 10 / 11 at (0x116, bottom 0x1a5) closes the screen.
- `FUN_0049b330`: tooltips on a dark box. Lines sit at baselines 0x32
  and 0x41 (0x37 alone), centred per button: party button 0xda, hostile
  0xa0 (0x6e for "Must be toggled in town"), loot 0xff, hear 0x114,
  squelch 0x128, LEAVE 0xd5. The texts are 0x1004..0x101a.

## Clicks (FUN_0049bb60, FUN_0049bdd0)

- The party button runs `FUN_00479eb0`. State 0 → 4 sends C→S 0x5e 6
  (invite); 4 → 0 sends 7 (cancel); 2 → 1 sends 8 (accept); 3 → 1
  sends 9 (leave).
- A box flips its flag on our side (`FUN_00479d30`) and sends C→S 0x5d
  {flag, on, player} (`FUN_00478740`). Hostility needs both players at
  level 9 and the player in town (`FUN_00479fe0`).
- LEAVE on top sends C→S 0x5e 9.

## d2d's ceilings

- The rows come by unit id, not join order.
- Area names come from 0x7f's level id.
- A party member's life bar (`FUN_0049a460`) isn't drawn.
- Squelch (`FUN_00479e20`) also adds the player to the Battle.net ignore
  list; d2d sends the flag only.
- The tooltip box's size is d2d's.
