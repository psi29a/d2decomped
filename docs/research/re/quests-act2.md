# Act 2 quests — game.exe 1.14d

Started 2026-10-10 with what Lut Gholein itself needs. Conventions, flag
helpers and the record layout are in `quests.md` / `quests-act1.md`.

## The records

The quest init table at `0x731510` (24-byte records `{init fn, act, 0x64,
flag, record id, quest word}`), Act 2's rows:

| record id | quest word | init fn | file |
|---|---|---|---|
| 7 | 8 | `FUN_00598810` | act 2's opening (Jerhyn's greeting) |
| 8 | 9 | `FUN_00599340` | a2q1 Radament's Lair |
| 9 | 10 | `FUN_0059eb00` | a2q2 The Horadric Staff |
| 10 | 11 | `FUN_0059eea0` | a2q3 Tainted Sun |
| 11 | 12 | `FUN_0059f2a0` | a2q4 Arcane Sanctuary |
| 12 | 13 | `FUN_0059c280` | a2q5 The Summoner |
| 13 | 14 | `FUN_0059d5b0` | a2q6 The Seven Tombs |
| 0x1a / 0x1b | 0x1e / 0x1f | `FUN_0059e330` / `FUN_0059e530` | (later) |

`FUN_00543640(game, id)` finds a record by id (the list at game
+0x10f4); `FUN_00544760(game, word, bit)` reads the game's own quest
flags (game +0x10f4 +0xc). Record +9 is open in this game, +0xc its state.

## Jerhyn and the palace gate (a2q4.cpp)

Lut Gholein's DS1 holds two invisible objects named "jerhyn" (objects.txt
121 and 122, token `ss`). Their InitFns (table 0x731bc0) run as their
rooms come up; each finds the quest record of type 0xb in the game's list
and calls a2q4's hook:

- **Object 121** (InitFn 18 `FUN_005448b0` → `FUN_0059f380`): unless
  record +0xd (Jerhyn made) is set, when word 8 and word 12 lack bit 13
  and record 13 (the Seven Tombs) is open (+9) with state under 2:
  `FUN_00545340(2, 0x100, spot, 10, 100)` finds room by the object, then
  Jerhyn (MonStats 201) is made there (`FUN_005b2f20(x, y, 0xc9, 1, -1,
  0)`); +0xc = 1, +0x3c his unit id.
- **Object 122** (InitFn 19 → `FUN_0059f440`): unless word 14 has bit 13,
  the palace guard (331, act2guard2, MonAI JarJar) one subtile east
  (`FUN_005b3090(x + 1, y, 0x14b, 1, 0)`); event 7. Then, unless Jerhyn's
  made, he stands in the palace (`FUN_0059ef70`: x − 10 while record 13
  is open and under 2, else x + 15; y − 3) — except while words 8 and 9
  lack bit 13 and record 13 holds (he's outside then).
- `FUN_0059f0c0` (a2q4's level change, +0xac) moves him in on entering
  level 40 once record +0x11 is set (not ported).

d2d: `rules::palace_spots` (quests.hpp, tests/test_quests.cpp); Lut
Gholein builds every candidate (`Npc::palace` 1..4) and
`World::show_palace` shows them as the quest bits stand.
Ponytails: the game's flags are the player's (one player), and record 13's
"under 2" is quest 14 having none of bits 1..11 (the Seven Tombs record
isn't ported); FUN_00545340's spot search is the nearest free spot.

## Act 2's opening (a2q0.cpp, record 7, quest 8)

`FUN_00598810`: +0xa = 1, state 0, no log (+0xd 0), blocks `0x738fc8`
(block 0: Jerhyn 201 string 253, kind 0; block 1: Cain 244 string 125).

- +0xa0 `FUN_005986b0`: Jerhyn without quest 8 bit 0 → block 0; Cain with
  quest 4 bit 14 (missed the rescue), not yet told → block 1.
- +0xcc `LAB_00598640`: 253 heard from Jerhyn → bit 0 and the game's word 8
  bit 13 (`FUN_00544720`): that's what moves Jerhyn into the palace
  (a2q4's objects read the game's flags). 125 from Cain → told.
- +0xd4 `LAB_005987d0`: a join with bit 0 sets the game's bit 13.
- +0xec `FUN_00598780`: Jerhyn's "!" while bit 0 is clear.

d2d: `rules::ActTwoOpening` (Cain's 125 too: `told` is data+0, and the talk
drops the player from quest 4's missed list as `FUN_005940a0`); the game's
flags are `World::game_bits`.

## Radament's Lair (a2q1.cpp, record 8, quest 9)

`FUN_00599340`: +0xa = 1, **state 1** (given out from the start), +0xd 4,
+0x10 = 13 (the Seven Tombs' record is next on the chain), blocks
`0x739218`, state → block `0x7398fc` (−1, 0, 1, 2, 3, 4). NPCs: Atma 176,
Warriv 175, Drognan 177, Fara 178, Greiz 198, Elzix 199, Geglash 200,
Lysander 202, Meshif 210, Cain 244. Radament is MonStats 229.

Bits: 0 done, 1 reward due, 2 Atma told, 3 / 4 under way (log 1 / 2),
5 the Book of Skill due, 13 took part, 14 killed by someone else, 15 closed.

| field | function | when |
|---|---|---|
| +0xa0 | `FUN_00598c40` | messages: bit 1 → block 3; rewarded → 4; else without bit 0, state ≠ 0 and (state < 4 or bit 13) → state − 1 |
| +0xa8 | `LAB_00598990` | Atma's talk closes after 304: log 1, one shot |
| +0xac | `LAB_00599130` | leaving level 40: off the rewarded list; state 2 (no bit 0 / 1) → 3, mark |
| +0xc0 | `FUN_00599020` | Radament dies (below) |
| +0xcc | `FUN_00598a70` | heard from Atma: 304 → data+8, state 2, mark; 334 with bit 1 → done (below) |
| +0xd4 | `FUN_00599230` | join (below) |
| +0xec | `FUN_00598910` | Atma's "!": no bit 0, and (open, state 1, no bit 15) or bit 1 |
| +0xf0 | `FUN_005991c0` | chain: stops while open and not done, else record 13's +0xf0 |

Mark (`LAB_005989e0`): no bit 0 / 1; state 2 → bit 2, state 3 → bit 3 if
log 1, else bit 4.

**Radament dies** (`FUN_00599020`, open): state 4, the game's word 9 bit 13,
+0xa8 = 0. Per player: `FUN_00598e20` (the player's room is the killer's
or next to it, no bit 0 / 1 → bits 13, 1 and 5; the party too,
`FUN_00598d40`); `LAB_00598dc0` (no bit 0 / 1 → bit 14, the notice);
`LAB_00598fa0` (bit 5 and no "ass" carried → a book to drop; bit 13 →
voice 0x32, the class's act2_complete_radament). A 12-tick timer (once,
data+10) → `LAB_00598f70`: state 4 → log 3. The books drop at him
(`FUN_00559a30`, code "ass" at +0xb8).

**Atma's 334 with bit 1:** bit 13 → (state ≠ 5: log 0xd, state 5, the
chain; closed: the game's bit 13); without it, a closed-with-reward-due
join's data+0x10 runs record 13's chain. Then bit 0 set, bit 1 cleared,
onto the rewarded list. No reward here: the book was it.

**Join** (`FUN_00599230`): bit 0 → if bit 5 and no book carried, bits 0, 1,
15, 5 clear (it's to be done again in a later game); the game's bit 13,
closed, state 0. Bit 15 → data+0x10 = bit 1, closed. Else bit 4 → log 2,
state 3; bit 3 → state 3, log 1; bit 2 → state 2, log 1.

**The Book of Skill** (item use, `0x55cd31` can-use and `0x55e3d0` use):
only with quest 9 bit 5; it clears bit 5, adds a skill point (stat 5,
`FUN_006272b0`) and the book is used up.

**The log** (client, `FUN_004a1950`): Act 2's records are 6..11 of
`0x7237a4` (quests 9..14); only the Seven Tombs (record 11, `0x723a64`)
has its own case.

d2d: `rules::RadamentQuest`, `World::radament_died`, the book in
`World::apply` (UseItem). Checked live (devctl, 2026-10-11): Atma 304 →
state 2 / log 1 / bit 2; out of town → state 3; the kill → bits
1 2 3 5 13, log 3, the book dropped; read → +1 skill point, bit 5 clear;
Atma 334 → state 5, log 0xd; the log's Act 2 tab says "Quest completed".
Ponytails: one player (the lists are bools, no party), "near" is the
player in Sewers level 3, the chain on to the Seven Tombs waits for its
record.

## The Tainted Sun (a2q3.cpp, record 10, quest 11)

`FUN_0059eea0`: +0xa = 1, state 0, +0xd 4, +0x10 = 0xb (the Arcane
Sanctuary next on the chain), blocks `0x73a188`, state → block `0x73a620`
(−1, 0, 1, 2). Bits: 0 done, 1 reward due, 2 the dark seen, 3 Drognan
told, 4 in the temple, 13 took part, 14 done by someone else, 15 closed.

| field | function | when |
|---|---|---|
| +0xa0 | `FUN_0059a270` | bit 1 → block 3; bit 14 → none; rewarded → 4; else open, state 1..3, no bit 0 → state − 1 |
| +0xac | `FUN_0059ee00` | into 44 / 45, state 0: the darkness timer (`FUN_0059edc0`: rand(17) ticks on the quest seed, once); into 40 with the dark pending: dark; out of 40: off the rewarded list, state 2 → 3 |
| +0xcc | `LAB_0059ebe0` | 348 from Drognan (177): log 1 → 2, state 1 → 2 + mark; 362..372 from anyone: bit 13 and not done → log 0xd, state 5, the chain; bit 1 → bit 0, rewarded (closed: the game's bit 13) |
| +0xd4 | `FUN_0059a5d0` | bit 0 / 1: the game's bit 13; bit 2: dark for them (`FUN_0059a570`), then bit 4 → log 2 state 3, bit 3 → log 2 state 2, else log 1 state 1 |
| +0xec | `FUN_0059a1c0` | the "!": no bit 0, and bit 1 over Atma, Warriv, Greiz, Elzix, Drognan, Lysander, Cain, Meshif, Geglash, Jerhyn; or Drognan in state 1 |
| +0xf0 | `FUN_0059a480` | the chain on to record 11 once done |

Mark (`LAB_0059a4f0`, no bit 0 / 1): state ≥ 1 bit 2, state 2 bit 3,
state 3 bit 4.

**The dark** (`FUN_0059a350`): log 1, state 0 → 1, the act's day into the
eclipse (`FUN_0061c450`) and each player sent S→C **0x53** `{u32 5, u32 0,
u8 1}` (`FUN_0059a100`). The client (`FUN_0061c240`) sets phase, time and
eclipse, its scale untouched: the eclipse phases (0x744480: 300° dawn,
0° day, 60° dusk, 120° / 180° / 240° night), no night's extra frame, and
the light falling 8 a frame to 32 whatever the hour (`FUN_0061bb80`).

**The altar** (objects.txt 149, InitFn 20 at 0x544910 → `FUN_0059a3f0`:
its room up → open and state < 2: dark, state 3; log 0 → 2; its mode
data+8). OperateFn 24, `FUN_0059a7e0`: a player who has it (bit 0 / 1) and
quest 10 done or carrying "vip" / "hst" gets voice 0x13; else once: mode
1, the sun back (`FUN_0061c4d0`, S→C 0x53 `{2, 0, 0}`: sunrise), state 4,
the game's bit 13; the operator bits 1 and 13 (14 off), the players in the
altar's room too (`FUN_0059a680`), their parties (`0x59a040`); bit 13 →
voice 0x34 (act2_complete_tainted); a Viper Amulet ("vip", item flag 0x10)
for each player without one, the staff or quest 10 done; the act's chest
class with quality forced magic (`FUN_00585b90`, EDX 4); rand(5) + 5 gold
piles on the quest seed (`FUN_00585970`); a 10-tick timer
(`LAB_0059a700`): state 4 → log 3.

**The quest seed** (`FUN_005438b0`, list +0x18): `{a game-seed step, 666}`
at game start (`FUN_00545d80` → `FUN_00650e40`), after sunitproxy:
`Spawning::quests`.

d2d: `rules::TaintedSunQuest`, `Day::set` / the eclipse (light.hpp), the
altar in `World::operate`; a joined game takes 0x53 (`NetGame::sky`).
Checked live (devctl, 2026-10-11): the Lost City darkens (light 32), the
Act 1 sky stays clear, the temple is state 3, the altar gives state 4 →
log 3, bits 1 2 13, the amulet, the chest roll and 8 gold piles, sunrise.
N-player gaps: one player (the rewarded list a bool, no party credit, the
altar's room is "the player"); one day clock for all acts.

## The Horadric Staff (a2q2.cpp, record 9, quest 10)

`FUN_0059eb00`: +0xa = 1, the log its own (+0xe8 `FUN_0059e630`, +0xd 0),
blocks `0x739918` (ten, Cain 244 one line each: 335 scroll, 336 amulet,
337 shaft, 338 Cube, 339 staff; 0..4 kind 0, 5..9 kind 2 for 339, 335,
336, 337, 338). Parts: `tr1`, `msf`, `box`, `vip`, `hst`, each "carried"
by `FUN_00558110` (the cursor's, then every item the player owns; a
questdiffcheck item only from this difficulty on, stat 0x164).

Bits: 0 done (the staff in the orifice, a2q6), 1 reward due, 3 the scroll
read (or any part shown), 4 / 5 / 6 the amulet / shaft / Cube shown,
10 the staff shown, 13 / 14 done here / by others, 15 closed.

- **The pick** (`FUN_00599590`): the staff → 4 (5 once bit 10); else the
  first part not shown in the order Cube (3), scroll (0), amulet (1),
  shaft (2); else the last carried one shown (9, 6, 7, 8).
- **+0xa0** (`FUN_00599910`, Cain only): bit 1 → block 4; a new part → its
  block; else rewarded → 5; bit 0 → none; else the last shown, or with
  nothing carried block 6 if bit 3.
- **+0xec** (`FUN_005996c0`): Cain, bit 1 or a new part.
- **+0xcc** (`FUN_005997f0`): 335 → the scroll goes (`FUN_00544160` →
  `FUN_005440a0`), bit 3; 336 / 337 / 338 → bit 4 / 5 / 6 and 3; 339 →
  bit 1 off, 3, 10, 4, 6, 5, rewarded.
- **+0xe8, the log** (`FUN_0059e630`): 0 without quest 7 bit 0; bit 0 / 1:
  6 (5 with bit 10) till quest 14 bit 0, then 13 / 11 by bit 13. Else
  shaft + Cube + amulet → 3 (bit 6) / 2 (bit 3) / 4; the staff → 6 / 5;
  an unread scroll → 1; bit 3 → 2; any part → 4; a part lost (the Cube or
  shaft after its chest gave one, data+0x29 / +0x28; the amulet after the
  altar, record 10 data+4) → 9.
- **+0xd4** (`FUN_0059e850`): counts the parts carried; without bit 0,
  record 13 open and its data+0xb clear (the staff not placed) and no
  staff: unless all three parts are carried, the missing ones' shown bits
  (5 shaft, 6 Cube, 4 amulet) clear.
- **+0xac** (`LAB_00599a00`): leaving 40, off the rewarded list.
- **The chests** (objects.txt 355 / 356 / 354; InitFns 32 / 33 / 31 do
  nothing): OperateFns 40 `FUN_00599c10` (Sewers 3), 41 `FUN_00599cf0`
  (Maggot Lair 3), 39 `FUN_00599df0` (Halls of the Dead 3). Once
  (`FUN_00545850`: mode 0 only; mode 1 with a Mode1, else 2): the part for
  each player who wants it (the scroll: no bit 0 / 3; the shaft,
  identified: no bit 0, shaft or staff; the Cube: none carried), the act's
  chest class forced magic (`FUN_00585b90`, EDX 4), rand(5) + 5 gold piles
  on the quest seed.

d2d: `rules::HoradricStaffQuest`, the chests in `World::operate`, the
scroll's removal in the said path. Checked live (devctl, 2026-10-11): the
three chests give their parts; Cain takes the Cube, then the shaft (bits
3, 5, 6); the Cube makes the staff (cube.md; bit 11, `FUN_0059e5c0`). Not
yet: the orifice (a2q6). N-player gaps: one player's counts, the rewarded bool.

## The Arcane Sanctuary (a2q4.cpp, record 11, quest 12)

`FUN_0059f2a0`: +0xa = 1, state 0, +0xd 5, +0x10 = 0xd, blocks `0x73a638`,
state → block `0x738d44` (−1, 0, 1, 2, 3, 4). Bits: 0 done, 1 reward due,
2..5 under way, 7 / 8 the palace guard's two lines heard, 13 / 14, 15.

- **Open**: the chain (`LAB_0059b500`: state 0 and open → 1, then on to
  record 13's +0xf0), from the Tainted Sun's done; or the staff made
  (`FUN_0059e5c0` → `FUN_0059b660`: open → state 1, log 1; closed → the
  guard stands aside, `FUN_0059aef0`).
- **+0xa0** (`FUN_0059b1c0`): the guard (331): line 186 (block 7) till he
  stands aside (data+0xe), then rand(3) + 8 on the quest seed (187..189).
  Others: bit 1 → block 4, rewarded → 5, else open, state ≠ 0, no bit 0,
  not past 4 without bit 13 → state − 1 (Drognan's 373; Jerhyn's 377; the
  town's 374..384; 385..395).
- **+0xec** (`FUN_0059aca0`): the guard while his line is news (bit 7
  before he stands aside, bit 8 after: data+0x40 / +0x46); Drognan in
  state 1, Jerhyn in state 2.
- **+0xcc** (`FUN_0059af50`): the guard's 186 → bit 7, 187..189 → bit 8;
  the thanks 397..407 with bit 1 → bit 1 off, rewarded; 396 (the journal):
  open and log < 5 → log 5; once, heard in the journal's room: the way to
  the Canyon (`FUN_00545340` spot, `FUN_0056d130`: object 60 to level 46),
  the game's bit 13; Drognan's 373 → state 2, data+4, the guard stands
  aside (`FUN_0059aef0`: data+0xe, +0x40 = 2, +0x46 if new, mark, the
  palace door); Jerhyn's 377 → state 3, log 3, mark.
- **+0xa8** (`LAB_0059ae60`): Drognan's talk shut after 373 → log 2, mark.
- **+0xac** (`FUN_0059f0c0`): into 74 → state 4 (from ≤ 3), log 4, mark;
  into 50 → bits 8, 7; out of 40 → Jerhyn's moves (palace_spots), off the
  rewarded list, state 3 (no bit 0 / 1) → 4, mark.
- **+0xd4** (`FUN_0059b530`): quest 11 done or quest 10 done or the quest
  under way → the guard stands aside; bit 0 / 1 → the game's bit 13; the
  staff carried → log 1, state 1; bit 5 → 4 / 4, bit 4 → 3 / 4, bit 3 →
  3 / 3, bit 2 → 2 / 2.
- **Mark** (`LAB_0059adb0`): state 2 → bit 2, 3 → bit 3, 4 → bit 4 (log 2
  or 3) / bit 5 (log 4).
- **Horazon's Journal** (objects.txt 357, OperateFn 42, `FUN_0059b970`):
  mode 1 once; every read its 396 (`FUN_005456a0`), its room kept; open
  and not state 5 → state 5, and each player in the Sanctuary without bit
  0 / 1, in order: bits 13, 1 and 0 set, bits 2..11 cleared, bits 8 and 7
  set (`FUN_0059b3f0`); else bit 14 (`LAB_0059b320`).
- **The Sanctuary's portal** (objects.txt 298, OperateFn 34,
  `FUN_005846b0`): shut, it opens (mode 1); open, 54 → 74 and 74 → 54
  (`FUN_0053aec0`). **Teleport pads** (OperateFn 27, `FUN_00581bf0`): the
  other pad of the row in the room, else a near room; the player beside it.

## The Summoner (a2q5.cpp, record 12, quest 13)

`FUN_0059c280`: state 0, +0xd 2, blocks `0x73af68` (0: the town's 408..418;
1: their thanks 419..429, greeting; 2: the same as topics), state → block
`0x73b278` (−1, 0, 1, 2). The Summoner is MonStats 250.

- **Open** (`FUN_0059c330`, from his AI's first attack think
  `FUN_005f85c0`): data+1; state 0 → 1; log < 2 → log 2, mark (bit 2 in
  state 1).
- **Dead** (+0xc0 `FUN_0059c150`): state 2; a 3-tick timer once
  (`LAB_0059bfd0`: log 4; a player with bit 13 in the Sanctuary says voice
  0x33, act2_complete_summoner); players by him (his room or next to it)
  without bit 0 / 1 → bits 13, 1 (`FUN_0059be70`), their party; the rest
  bit 14 (`LAB_0059c0f0`).
- **+0xcc** (`FUN_0059bd70`): 419..429 with bit 1: bit 13 → log 0xd, the
  game's bit 13, state 3; rewarded, bit 0, bit 1 off.
- **+0xa0** (`FUN_0059bbc0`): bit 1 → block 1; rewarded → 2; open, state ≠
  0, not (bit 0 without 13), not (past 1 without 13) → state − 1.
- **+0xec**: the town's people with bit 1. **+0xd4**: bit 2 → log 2, state
  1. **+0xac**: out of 40 → off the rewarded list.

d2d: `rules::ArcaneQuest`, `rules::SummonerQuest`, the journal / portals /
pads in `World::operate`, `World::canyon_portal` (portal slots 4 / 5).
Checked live (devctl, 2026-10-11): the staff made opens it; Drognan → 2,
Jerhyn → 3, the Sanctuary → 4; the Summoner's death (bits 1, 13, log 4);
the journal (bits 0 1 7 8 13), its 396 opens the Canyon portal, which
leads to the Canyon of the Magi. Gaps: the Summoner's AI isn't ported
(awareness stands in for its first attack think); "by him" is the player
in the Sanctuary; the portal draws as a town portal, not object 60's red
one; the palace door's mode; one player.

## The Seven Tombs (a2q6.cpp, record 13, quest 14)

`FUN_0059d5b0`: open, state 0, its own log (+0xe8, `FUN_0059ca50`). The
true tomb is the act's boss tomb (act +0x94, `FUN_0061aeb0`; drlg.md
"Act 2's layout"). Bits: 0 done, 2 under way, 3 Tyrael's heard (Act 3
open), 4 Jerhyn's reward heard, 5 at Duriel's death, 6..11 the town's
lines after Tyrael, 13 took part, 14 by others, 15 closed.

- **Open** (`FUN_0059d450`): the chain from Radament's +0xf0, and from
  the Tainted Sun's through the Arcane Sanctuary's: state 0 → 1.
- **Join** (`LAB_0059d4f0`): quest 10 bit 9 off; quest 10 done → the
  staff was placed in an earlier game (data+0xc, +0xd); bit 0 / 15 →
  closed; bit 2 → log 1, state 2; bit 3 → 5 / 5; bit 4 → 6 / 5.
- **Log** (+0xe8): Act 2 not opened (quest 8 bit 0) or done → 0; bit 3 →
  5; bit 4 → 6; closed → 0; bit 5 or state < 3 → the record's; else 0xc.
- **+0xac** (`FUN_0059d1c0`, levels 40..74): out of 40 → off the rewarded
  list; Duriel alive: into the true tomb → state 2, log 2 (from ≤ 1),
  mark; into the Canyon → state 2 (from 0), log 1 (from 0); into the Lair
  (73) at state ≤ 1 → state 2, log 2, mark.
- **Mark** (`FUN_0059c6c0`): state > 1, not done → bit 2.
- **+0xa0** (`FUN_0059c3c0`): blocks by state − 1: 0 Jerhyn's 430
  (greeting); 1 the town's 431..441 (Drognan quiet with the game's
  quest 13 bit 13); 2 Tyrael's 302; 3 the town's 442..452 with Jerhyn's
  442 and Tyrael's 302 as greetings; 4 the same, Jerhyn's not; 5 Meshif's
  450; 6 the six town lines (444..449, 452) as greetings. Nothing when
  done or past state 3 without bit 13.
- **+0xec** (`FUN_0059d300`): Jerhyn in state 1 (no bit 3 / 4) or with
  bit 3; Meshif with bit 4; Tyrael from Duriel's death till his portal;
  with bit 13, the town's six till their bit.
- **+0xcc** (`FUN_0059cb20`): Tyrael's 302 (once) → a town portal by the
  player (`FUN_0056d130`: object 59 to level 40), state 4; heard in the
  Lair without bits 13 / 3 / 4 → bits 13, 3 and Act 3 open
  (`FUN_0059c860` → `FUN_00538680`); not done and no bit 3 / 4 → bit 14
  (`LAB_0059c9f0`). Jerhyn's 430 → state 2; his 442 with bit 3 → state 5
  (bit 13), bit 4 on, bit 3 off. Meshif's 450 with bit 4 → the staff and
  its parts taken if quest 10 is done; bit 13: the game's bit 13, state
  5, log 0xd; done, bit 4 off, rewarded. The town's 444..449, 452 → bits
  9, 6, 7, 11, 8, 10.
- **+0xa8** (`LAB_0059c760`): talk shut: Tyrael's → log 4; Jerhyn's 430 →
  log 1, his 442 → log 6; mark.
- **Duriel's death** (+0xc0, `FUN_0059d050`): open → state 3, an
  8-tick timer once (`LAB_0059cee0`: log 3 unless 3..5); players by him
  (`FUN_0059cfb0`, the Lair) without bit 0 / 3 / 4 → bit 5; Tyrael talks
  (data+0x18 2).
- **The orifice** (objects.txt 152, InitFn 21 `FUN_0059db50`, OperateFn
  25 `FUN_0059dc70`): with the Horadric Staff carried, a panel takes it
  (C→S 0x44 3); placed (`FUN_0059dd80`): quest 10 bits 0 and 13, the staff
  and its parts gone, a timer once (`FUN_0059d870`) that opens the way
  down: object 100 thirteen subtiles left of the orifice and three down
  (`FUN_00555230`), OperateFn 43 into level 73. Without the staff: "I
  can't use this yet" (voice). Its room coming up opens it (mode 2) on
  data+0xc: the staff placed in an earlier game.
- **Tyrael** is the Lair's DS1 preset (`1:251`, NpcStationary) by
  objects 352 / 353; he stands there from the start, and leaves once his
  portal is open and no player is within 12 of him (monster-ai.md).

d2d: `rules::SevenTombsQuest`, the orifice in `World::operate`,
`World::durielportal_open`, portal slot 6. Checked headless (devctl,
2026-10-11, quests 8..13 done by `debug quest`): into the Lair → state
2, log 2 (bit 2); Duriel's death → 3 / 3 (bit 5); Tyrael → 4 / 4, bits
3 and 13, his portal; Jerhyn → 5 / 6 (bit 4); Meshif → done (bits 0, 2,
5, 13). Gaps: the orifice's panel (using it with the staff places it);
Tyrael's prison (data+0x2c) isn't opened; the log override (data+0x10 /
+0x14) isn't traced; Meshif's Sail East (Act 3) isn't there; one player.

## Meshif's boat to Act 3

Client: table 0x7253e0's row {0xd2, quest 14, bit 0, set, FUN_004b6620}
gives Meshif (210) 3 entries, [1] 0xd38 "sail east" (handler 0x4b51a0,
C->S 0x38 kind 0). Server FUN_00579d60, hcIdx 0xd2: quest 14 bit 0 ->
FUN_0054b830(0x4b, 0) (Kurast Docks, tile 0), FUN_005467e0: unless quest
15 bit 0, quest 10's bits 0 / 13 when unset (and FUN_00544160 takes "msf "
and "vip " off the player), then quest 15's bits 0 / 13; the town's
waypoint. hcIdx 0x108 (Kurast's Meshif) sails back to 0x28.
d2d: `World` cmd::Caravan; Kurast Docks (DockTown3.ds1, LvlPrest 529)
builds, `diff_drlg.py 1-5 75 tiles units seeds` 5/5 (FUN_00665950's act 3
monster-to-object remaps: 0x129 -> 0x17e, 0x16e -> 0x194; act 5's too).
Checked headless both ways. Gap: FUN_00544160's item removal.
