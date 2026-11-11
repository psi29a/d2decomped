# Players and parties — counts, experience, gold (1.14d game.exe)

Our port: `components/rules/monsters.hpp` (`player_life_pct`,
`player_exp_pct`, `monster_stats`), `components/rules/combat.hpp`
(`party_shares`), `components/rules/drops.hpp` (`roll_drops`' `nearby`),
`components/game/fight/deaths.cpp` (`Fight::killed`, `drop_counts`),
`components/game/loot.hpp` (`Loot::take`'s gold), `World::credit_party`.
Tests: `tests/game/test_game.cpp` (two players). The first survey is in
docs/design/net-host.md section 2; this file has the traced detail.

## Party membership — the control list

The host keeps one `D2UnitPartyControl` per game (game +0x1d2c, net-host.md):
an `nPartyId` counter and a `pSub` list of `D2UnitPartyControlSub` (one per
party), each with its own `nPartyId`, a `pPrev` link to the next sub in that
outer list (despite the name — the list is walked only that way, newest
first), and a `pSub` list of `D2UnitPartyControlSubSub` (one per member:
`nUnitGUID`, `pNext`).

- FUN_00540000 FreePartyControl: frees every member node, every party sub,
  then the control block itself (game teardown; `.\UNIT\Party.cpp` lines
  0x3f/0x43/0x47).
- FUN_005400b0 PARTY_UpdateNextPartyId: the next free id ≥ 3 not already
  used by an existing party sub, then leaves the control's counter one past
  it. Ids 0..2 are never issued (0xffff means none).
- FUN_00540100 PARTY_CreateParty: allocates a `D2UnitPartyControlSub` with
  that id and prepends it to the control's list. Halts (lines 0x7f/0x81)
  without a control block, or on allocation failure.
- FUN_00540180 PARTY_FreePartyMembers: frees every member node of one party
  sub, sending `SendAllPlayersPartyInfo` and S→C 0x8D (PLAYERPARTY) for
  each — but only while every member's GUID still resolves to a live unit;
  the first miss falls through to the halt at line 0x9a instead of being
  skipped. Then frees the sub itself.
- FUN_00540220 PARTY_DisbandParty: finds the sub by id in the control's
  list, unlinks it (patching the predecessor's `pPrev`, or the control's
  `pSub` for the head), then PARTY_FreePartyMembers it. A silent no-op if
  the id isn't found (no halt).
- FUN_005402d0 CheckUnitGUIDInAllPartyControlSubs: despite the name, checks
  one sub's own member list, not every party — true if `nUnitGUID` is
  already a member there.
- FUN_00540320 PARTY_AddUnitToParty: finds the sub by id, checks
  CheckUnitGUIDInAllPartyControlSubs first (already a member → returns 1,
  no-op), else allocates a member node, prepends it, sends S→C 0x8D
  (AssignPlayerToParty), then clears the hostile relation both ways between
  the new member and every member already in the list
  (PARTYSCREEN_ClearHostileRelation). Halts if the party, the unit, or the
  unit's type (must be UNIT_PLAYER) is invalid.
- FUN_00540460 CountPartyControlSubSize: member count of the party with
  that id, or -1 if no such party.
- FUN_00540710 GetPartyIdFromUnit: scans every party sub and its member
  list for pUnit's GUID; -1 if none. The search GUID is read through
  `pUnit->pInventory` via FUN_0063d450 when the unit has one — confirmed in
  the asm that function (named "INVENTORY_GetCursorItemGUID" by Ghidra)
  returns `pInventory->nOwnerGUID`, the inventory's own owner, i.e. the
  same GUID as `pUnit->nUnitGUID` for a normal player; it is not a
  cursor-item lookup, and not a bug.
- FUN_005407a0 PARTY_RemoveUnitFromParty: finds the unit's party
  (GetPartyIdFromUnit), unlinks its member node, sends the same
  SendAllPlayersPartyInfo / 0x8D pair as PARTY_FreePartyMembers, frees the
  node, then disbands the party (PARTY_DisbandParty) if membership drops
  under 2. Halts on a non-player or missing unit (line 0x130) or an
  unresolvable party sub (line 0x138).

Confirmed directly (already cited): FUN_00540290 FindPartyControlSub
(net-host.md:132) returns the `D2UnitPartyControlSub` matching an id by
walking the control's list, as described there; no correction needed.

d2d is a client only — it never allocates a `D2UnitPartyControl`; this is
the host's own party bookkeeping, which d2d doesn't run (net-host.md:
hosting is design, not yet built). `apps/d2d/netgame.cpp` only mirrors the
wire results (S→C 0x8b/0x8d/0x75/0x7f) into `NetGame::Unit::party` /
`party_life` / `area` / `relation` (`apps/d2d/netgame.hpp:40-44`). Nothing
here ports into `components/` until hosting exists.

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
2. The merc (FUN_00574ec0(7)) gets its own share: FUN_0057e480's award at
   the merc's level, 86/256 of it (to zero) when the blow wasn't the
   merc's; FUN_0057e860 adds twice that to its experience while it's under
   its owner's level and level 98 (FUN_00611830(0) - 1), then levels it up
   by Hireling.txt's (L+1)·L²·Exp/Lvl (FUN_00663790; FUN_00572840 the
   stats). d2d: rules::merc_kill_exp, Fight::merc_share.
3. No party (FUN_00554630 == 0xffff): the killer gets FUN_0057e480's
   award, added by FUN_0057e510. FUN_0057e480: 1 for a kill worth none, 0
   at MaxLvl (FUN_00611830); the level difference (FUN_0057e2f0, 256ths:
   the character 6..10+ above 207 / 159 / 110 / 61 / 13 at 0x6e1668, the
   monster 6..10+ above 225 / 174 / 92 / 38 / 5 at 0x6e1694, or clvl / mlvl
   from clvl 25); Experience.txt's ExpRatio for clvl >> the MaxLvl row's
   (FUN_0057e390); + stat 0x55 % (FUN_00483360, the truncating MulDiv with
   shortcuts: rules::scale); a merc's capped at its level's span / 64
   (FUN_0057e3f0). d2d: rules::kill_award, checked against game.exe by
   tools/emu/kill_exp.py.
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

Verified in the emulator (`tools/emu/roster_small.py`, 15 functions from game.exe 1.14d on random
rosters; the relation list `FUN_004dc320` / `004dc440` / `004dc3e0` and the lookups `FUN_004792e0` /
`0047a310` run as stock; the C->S 0x5d send, the redraws and the ignore-list singleton are hooked):

- `FUN_004792e0(id)` (EDX) returns the first record whose `+0x10` equals the id **or whose `+0x38` list
  (nodes of id, next) holds it**; -1 and an empty roster give 0. `FUN_00479320` is the same lookup for the local
  player (`FUN_00463dd0` +0xc, -1 without one) and is fatal (line 0x75) when it finds nothing.
- Record `+0x34` points at a cell holding the first relation node (`{id, mask, next}`). A relation
  query is `FUN_004dc440(mine, other, mask)`, 0 when `mine == other` or either record is missing.
  `FUN_00479d30` (custom call: EBX the record, DI the mask, EDI clobbered) adds the node when missing
  and then sets every mask bit when none of them is set, else clears them all (`neg`/`sbb`/`add 1` at `00479d80`; fatal line 0x463 on a null record).
- `FUN_00479e20` (squelch) toggles bit 4, sends C->S 0x5d with ECX 3 and the new bit-4 value (4 or 0, the raw
  masked value), then calls the ignore-list object's slot 0 (add) when the bit is now set, else slot 1 (delete),
  with the record name `+0x66`. A null record does nothing.
- `FUN_00479fe0` (hostile): a null record does nothing; it proceeds only when bit 8 is already set or the
  record's level (`+0x20`, unsigned word) is at least 9. It toggles bit 8, sends 0x5d with ECX 4 and the
  new value, and if the bit is now set and the record's area `+0x22` is not 0xffff and equals the local
  record's, runs `FUN_00479f60` (own `+0x22` := 0xffff, `+0x30` := 1, two redraws; fatal 0x4ed without an own record). With no own record `FUN_00479fe0`
  is fatal 0x75 instead, from `FUN_00479320` called at `0047a04a` just before the area compare, so 0x4ed
  is not reached from it.
  The "both at level 9 and in town" rule in Clicks above is the caller's; this function checks the other player's level only.
- `FUN_0047a100(id, x, y)` stores `+0x28` / `+0x2c`; `FUN_0047a690(id, life)` stores DL zero-extended as a dword at `+0x14`
  (`movzx ecx, bl`; clears `+0x15..+0x17`; both do nothing for an unknown id); `FUN_0047a210(unit)` returns the record name for a player unit (type
  0, else fatal 0x5dc; null 0x5db), or the unit's player data pointer when it has no record (fatal 0x5e5 if that is null).
- `FUN_004795a0` (next), `FUN_004795c0` (name, fatal 0x174), `FUN_00479b90` (level), `FUN_00479ba0` (+0x1c),
  `FUN_0047a0c0` (id, -1 for null) and `FUN_0047a1e0` (+0x24).

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
  - the name at (0x24, y − 2), cut by `FUN_0049a2c0` (EDX the name,
    third argument the row in a party, `FUN_00479bc0` != -1 at 0x49c31f):
    0x60 px in a party, 0x88 in none. Under the limit it is kept; else
    letters go from its end while it measures the limit or more (the
    loop zeroes the 100-unit buffer from its end, 0x49a330), then "..."
    is appended unmeasured (0x49a379), so the result can pass the limit
    by the dots;
  - the class (0x103a.., druid 0x2771, assassin 0x2772) at (0x24, y + 12);
  - "Lvl: n" (0xd28 0xf9d 0xf9b) at (0x10a, y + 12);
  - a party member's area at (0x7e, y + 12), cut by `FUN_0049a830` past
    0x8c px: the suffix is "...." (0x6da05c) with its fourth unit
    swapped for the area's last letter (0x49a8ce), so "..." and that
    letter; letters go from its end while it measures more than 0x8b
    less the suffix's width, then the suffix is appended: "Catacombs
    Lev...4".

  d2d: `client::cut_party_name` / `cut_party_area` (apps/d2d/party_cut.hpp)
  match stock 1.14d on 2000 cases with a hooked font
  (`tools/emu/party_cut.py`, tests/test_party_cut.cpp).
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
