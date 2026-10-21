# Players and parties — counts, experience, gold (1.14d game.exe)

Our port: `components/rules/monsters.hpp` (`player_life_pct`,
`player_exp_pct`, `monster_stats`), `components/rules/combat.hpp`
(`party_shares`), `components/rules/drops.hpp` (`roll_drops`' `nearby`),
`components/game/fight.cpp` (`Fight::killed`, `drop_counts`),
`components/game/loot.hpp` (`Loot::take`'s gold), `World::credit_party`.
Tests: `tests/test_game.cpp` (two players). The first survey is in
docs/design/net-host.md section 2; this file has the traced detail.

## The player count — FUN_00535790

Players in the game (FUN_005538d0 over the player hash, game +0x1120,
skipping units with FUN_00639df0(unit, 7) set). `/players` (DAT_00883d70,
FUN_00535780) wins only when larger and only for game types (+0x6a) 1..3.
d2d counts the World's players; there is no `/players`.

## Monster life and experience — FUN_00573930, FUN_00573cb0

FUN_00573930 (monster unit, type 1, its data at unit +0x14): unless the
data's byte +0x4c is set, n = FUN_00535790, life % = FUN_005738f0(n),
experience % = FUN_00573910(n); with +0x4c, n = 1 and no bonus.

- FUN_005738f0: n < 9 → table 0x6e1590, else (n - 2) x 50.
- FUN_00573910: n < 9 → table 0x6e15b4, else (n x 5 + 130) x 2.
- Both tables (read from game.exe's memory): 0, 0, 50, 100, 150, 200,
  250, 300, 350.

FUN_00573cb0 (0x573f83..0x573fdb): life = rand(max - min + 1) + min on
the unit seed (FUN_0045c3e0), then life + FUN_00483360(life, life %, 100),
at most 0x7fffff, x 256 into stats 7 and 6. Experience (stat 13) =
exp + FUN_00483360(exp, experience %, 100). Stat 100 is set to n.

FUN_00483360 is MulDiv truncated (plain integer division; 64-bit only
when the product would overflow).

The four other callers of FUN_00573930 (FUN_005e8810, FUN_005e9750,
FUN_005f0b00, FUN_005fc450) are AI thinks reading n; they don't rescale.
A monster keeps its stats when players join or leave.

Open: what sets monster data +0x4c. d2d's pets never take the bonus.

## NoDrop — FUN_0055a6d0, FUN_005408e0

n = nearby + (players - nearby) / 2 (drops.md "Rolling"): players is
FUN_00535790's; nearby is FUN_005408e0's count of the killer's party alive
on its level (FUN_005404f0), 1 without a party.

## Experience for a kill — FUN_0057e990

1. The killer resolved (FUN_0057e7b0): a pet's owner, a player as is.
2. The merc (FUN_00574ec0(7)) gets its own share (FUN_0057e860: 86/256 of
   the penalised award when the killer isn't the merc). d2d: not kept.
3. No party (FUN_00554630 == 0xffff): the killer gets FUN_0057e480's
   award (the level penalty, + stat 0x55 item_addexperience), added by
   FUN_0057e510.
4. A party: FUN_0057e6c0.

### FUN_0057e6c0

FUN_005405a0 walks the killer's party members whose room is on the
killer's level, calling FUN_0057e5a0 on each:

- skipped when dead (FUN_005541b0: a player in mode 0 or 0x11);
- skipped when dx² + dy² > 0x1900 (unsigned), dx / dy the monster's
  subtile less the member's;
- an 8th member is a fatal error (message 0xf25).

It records each member and its character level and sums the levels.

- No member: nobody gets anything.
- One member: the killer gets the whole award (even when the member found
  isn't the killer).
- More: total = exp + ((n - 1) x exp x 0x59) / 256 (32-bit IMUL, the
  shift rounding toward zero); share = (float)(total / Σ levels) (FILD,
  FIDIV, FSTP float); each member gets _ftol(level x share) through
  FUN_0057e480 (its own level against the monster's) and FUN_0057e510.

## Gold picked up — FUN_00540900

Called from FUN_0055c850 when the gold has no player owner. Members =
the picker's party alive on its level (FUN_005404f0).

- Fewer than 2: the picker takes what their purse holds (FUN_00622e70:
  level x 10000); the rest stays on the ground.
- Otherwise share = gold / members; every other member alive on the level
  (mode not 0, not 0x11) gets min(share, room) (FUN_00540650), the rest
  coming back to the picker, who takes what fits; what doesn't stays.

d2d: a player's own dropped gold is shared too (no owner is kept).


## The Party screen (UI 0x16)

UI 0x16, `.\UI\Party.cpp` (0x499e50..0x49c3d0). The key config's Party
Screen (`0x468960`, P by default) toggles it, in a multiplayer game only
(`FUN_0044db30`). It is a left-hand panel. d2d: `apps/d2d/party.{hpp,cpp}`;
the joined game's data and packets are in `netgame.cpp`. devctl `debug
party on` shows it with three sample players.

Layout came from running `FUN_0049a640` (it builds the rows) and
`FUN_0049c3d0` (it draws) in the emulator on a made-up roster, with the
drawing calls hooked.

### The roster

`DAT_007bb5c0` is a list (+0x80 next) of `{+0x10 unit id, +0x1c class,
+0x20 u16 level, +0x22 u16 party, +0x24 area, +0x30 relationship,
+0x66 name}`.

Relationship (S→C 0x8b) is 0 INVITE, 2 ACCEPT, 3 LEAVE and 4 asked, our
own invitation waiting. Flags between two players (S→C 0x8c, read by
`FUN_004dc440`) are 1 loot, 2 not heard, 4 squelched and 8 hostile.

`FUN_0049a640` makes a row for every other player, at most nine. The rows
sit at y 0x5a + 0x26·i (panel coordinates, +0x50 / +0x3c at 800×600).

### Drawing

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

### Clicks (FUN_0049bb60, FUN_0049bdd0)

- The party button runs `FUN_00479eb0`. State 0 → 4 sends C→S 0x5e 6
  (invite); 4 → 0 sends 7 (cancel); 2 → 1 sends 8 (accept); 3 → 1
  sends 9 (leave).
- A box flips its flag on our side (`FUN_00479d30`) and sends C→S 0x5d
  {flag, on, player} (`FUN_00478740`). Hostility needs both players at
  level 9 and the player in town (`FUN_00479fe0`).
- LEAVE on top sends C→S 0x5e 9.

### d2d's ceilings

- The rows come by unit id, not join order.
- Area names come from 0x7f's level id.
- A party member's life bar (`FUN_0049a460`) isn't drawn.
- Squelch (`FUN_00479e20`) also adds the player to the Battle.net ignore
  list; d2d sends the flag only.
- The tooltip box's size is d2d's.
