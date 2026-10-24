# Act 5 quests — game.exe 1.14d

Conventions, flag helpers and the record layout are in `quests.md` /
`quests-act1.md` (callbacks: +0xa0 messages, +0xa8 talk closes, +0xac level
change (param[5] old level, [6] new), +0xc0 a bound unit dies, +0xc8 leaves,
+0xcc heard, +0xd4 joins, +0xd8, +0xe8 log, +0xec "!", +0xf0 open / chain).
Traced 2026-10-23..24 from the decompile; ported in `rules/quests_act5.hpp`,
`game/world_quests_act5.cpp` (thinks in `game/ai.cpp`: Wussie, BaalThrone,
BaalToStairs), played by `tests/quests_d2d.py ... 5`.

## The records

Quest init table `0x731510` (24-byte rows), act index 4:

| row | record | word | init | file | messages | state → block |
|---|---|---|---|---|---|---|
| — | — | 0x2a | `FUN_0058ea50` | (a5q0: the introductions) | 0x732ff8 | by class |
| 0x1f | 0x1f | 0x23 | `FUN_005876e0` | a5q1.cpp, Siege on Harrogath | 0x733308 | 0x733928 |
| 0x20 | 0x20 | 0x24 | `FUN_00588510` | a5q2.cpp, Rescue on Mount Arreat | 0x733940 | 0x734270 |
| 0x21 | 0x21 | 0x25 | `FUN_0058a410` | a5q3.cpp, Prison of Ice | 0x734320 | 0x734304 |
| 0x22 | 0x22 | 0x26 | `FUN_0058bb20` | a5q4.cpp, Betrayal of Harrogath | 0x734ac8 | 0x7350e8 |
| 0x23 | 0x23 | 0x27 | `FUN_0058ce30` | a5q5.cpp, Rite of Passage | 0x735100 | 0x7357e4 |
| 0x24 | 0x24 | 0x28 | `FUN_0058e500` | a5q6.cpp, Eve of Destruction | 0x735800 | 0x735ee4 |

a5q0 isn't in the table: `FUN_0058ea50` is set up with the act's records
(+0xa0 `FUN_00586b50`, +0xcc `LAB_0058e990`, +0xec `FUN_00586c50`).

Chain (+0x10): a5q1 → a5q2 → a5q3 → a5q4 → a5q5 → a5q6. Each +0xf0 opens
its record (0 → 1; a5q6 → 2) unless done; a5q3 passes on from state 5,
a5q4 from state 4.

**Binding** (`FUN_005b1cf0` by MonStats base, `FUN_005a49b0` by
superunique): prisondoor 0x1b2 and act5pow → 0x20; Nihlathak (base 0x20e)
→ 0x22; baalcrab (0x220) → 0x24 (0x2c5 → 0x12, the uber's); Shenk
(superunique 0x2a) → 0x1f, also `FUN_005b24e0(0x1c5, ...)` his guard.

**Log values** are DL at `FUN_00544300` (the record's +0xb). Records
with +0xe8 null (a5q2, a5q5, a5q6) use the default log with done = +0xd 4.

## NPC introductions (a5q0) and the save

The player's intro bits: pcdata +0x60 + difficulty × 4 → a bitfield, bit
by the table at 0x732734 (count) / 0x732738 ({hcIdx, bit}: Larzuk 29,
Anya 30, Malah 31, Nihlathak 32, Qual-Kehk 33, Cain 34; -1 → 0).
`FUN_005723c0` tests, `FUN_00572360` sets. Saved by `FUN_00569490` at
0x2C9: `01 77 34 00` (u16 0x7701, u16 size 0x34), then 3 × 8 bytes of
intros (0x2CD) and 3 × 8 of congratulations (0x2E5).

`FUN_00586b50`: an NPC whose intro isn't heard gets block 1 for one class,
block 2 for another, else 0 — Malah 4 / 1, Nihlathak 6 / 2, Qual-Kehk
5 / 3, Anya and Cain always 0. Hearing any of them sets the bit; Malah's
(20037..20039) also opens a5q1 (state 0 → 1). Malah's "!" till heard.

## Siege on Harrogath (a5q1)

States: 1 open, 2 Larzuk told (20077), 3 the barricades (110..112) or out
of Harrogath, 5 rewarded (20090). Shenk's first think (`FUN_00587900`):
log 2. His death (`FUN_00587330`): players in his room or its near list
(`FUN_00619790`) bits 1 and 13 (`FUN_00587100`), the rest 14; log 3. Marks
(`FUN_00586de0`): state 2 bit 2, state 3 bit 3 (bit 4 with the log at 2).
20090 with bit 1: bits 2..11 cleared, bit 5, state 5, the chain, log 13.
Bit 0 comes with Larzuk's socketing. Log `FUN_00586ce0`: bit 1 / 13 → 3 (4
with bit 5).

## Rescue on Mount Arreat (a5q2)

Data: +0x84 told, +0x86..88 cages up, +0x8c cage spots, +0xa4 made, +0xa8
dead, +0xac freed, +0xb0 prisoner ids (3 × 5), +0x108 cage emptied,
+0x10e portal opened, +0x114 gone per cage, +0x120 freed ids.

- Cage (object 473, InitFn 62 `FUN_005886a0`): state 2, log 1, a slot,
  five prisoners (`FUN_00588600`: 25 tries of `FUN_005b2f20(x, y, 0x216, 1,
  5)`, flags 0x3000000).
- Kill (`FUN_00588040`): a door — living prisoners within 15 of it freed;
  a freed prisoner — its cage's gone count; a caged one — dead, and gone.
  Five dead: log 12, the rest bit 14.
- The prisoner's think (Wussie, MonAI 131, `FUN_005ee3c0`): its cage's
  portal open (`FUN_00588d60`): unless one of its cage has gone, ≥ 4 off it
  70 % walks at it (`FUN_005dec80` flags 7), else rand(100) < 10 wanders 4,
  else stands 10; there it's gone (`FUN_00588880`). Else a player within
  16 (`FUN_005ddf20`): door broken (`FUN_00588e10`) → the portal
  (`FUN_005888d0`: object 189 at the dead door +2, prisoners only); still
  caged → log 5 (`FUN_00588dd0`) and rand(1000) < 100 the overhead 0x14ff.
  Stands 25.
- The portal's opener: every cage up and made = dead + freed: ≥ 12 freed
  → log 3, bits 13, 1 and 5 / 6 / 7 (15 / 14 / 12..13); else bit 14. Not
  all: bit 4, log 2.
- Qual-Kehk's 20110: runes r07 / r08 / r09 (3, 2 with bit 6, 1 with 7),
  bit 0. A level change moving the log from 0 clears the record's +0xa8.

## Prison of Ice (a5q3)

Data: +0x84 Anya (0 frozen, 1 thawed, 2 home), +0x88 Nihlathak's left
town, +0x8c the thaw step, +0x90 drehyaiced made, +0xa4 fana made, +0xac
told, +0xd8 potions out, +0x100 thawing, +0x101 potion heard, +0x102 the
second scroll.

- Markers: 459 (InitFn 66) Anya in town at +0x84 2; 460 (InitFn 67, event
  7 `FUN_0058a500`, 25 frames, again till made) makes fana (558) at its
  spot; 461 (InitFn 68) town Nihlathak while +0x88 is 0 (event 7:
  `FUN_00589340` removes him); 462 (InitFn 69, `FUN_0058a6c0`) superunique
  60 (Nihlathak) at +0x88 1.
- `FUN_005893e0` (into 112 from state 0, into 113 / 114, 20131 heard):
  town Anya removed unless home, town Nihlathak removed, +0x88 = 1. Temple
  (121..124) at state < 5: state 6, Anya home, bit 14 unless 13.
- Fana: InitFn 74 log 2; OperateFn 67 without the potion she says 20131
  (state 4, log 3); with it: taken, state 5, Anya 1, bits 13 and 1, log 5,
  the thaw timer (`FUN_0058aab0`: tick 1 fana mode 2, tick 2 drehyaiced at
  her spot, fana removed, the chain).
- Malah: 20116 told; 20127 the potion ("ice ") if none out; 20132 with
  bit 1 the scroll ("tr2 ", bit 8; with bit 9 done), Anya home
  (drehyaiced goes, `FUN_005891d0`); again once if lost.
- Anya 20136 with bit 1: a rare class item at the player's level
  (0x735f6c / 0x735efc / 0x735f34 by difficulty, {codes, count} per class,
  rand(count) on the quest seed), bit 9 (done with 8), bit 10.

## Betrayal of Harrogath (a5q4)

States: 1 open, 2 Anya told (20137), 3 out of Harrogath, 4 Nihlathak dead,
5 rewarded (20148). Anya's talk closing with +0x87 (told, or a join at bit
2 / 3) opens her red portal (`FUN_0058af90`: object 60 at her +10, +5 to
level 121); marker 459's event 7 retries it. Nihlathak's death
(`FUN_0058b7a0`): his room's players with a5q3 done or due bits 1 and 13,
the rest 14; state 4; the log 4 eight ticks on (`FUN_0058b770`). 20148:
bit 4 (her personalizing then due; bit 0 with it).

## Rite of Passage (a5q5)

Data: +0 won, +1 told, +0xc players dead on the Summit, +0x10 / +0x11
heard / awake, +0x14..1c statues, +0x20 Ancients, +0x30 statue modes,
+0x3c dead flags, +0x40 made, +0x44 alive, +0x48 fighting, +0x4a stairs
open, +0x4c altar mode.

- Qual-Kehk 20153: state 2, log 1. Into 120: log 2, state 3; out of town
  at 2: state 3. Marks only for a player of level (difficulty + 1) × 20.
- Altar (546, OperateFn 65): at mode 0 the Ancients speak (20002), state
  2, log 3, mode 1. Heard: statues to mode 3, event 7 20 frames on
  (`FUN_0058c0e0`: each its Ancient — 475 → 0x2b, 474 → 0x2d, 476 → 0x2c —
  via `FUN_00545c30`, statue mode 4); awake.
- The Ancients sleep (their AI, `FUN_0058cf90`) till awake. A player's
  death on the Summit (`FUN_005be290` → `FUN_0058cf00`) puts it back
  (`FUN_0058c000`).
- Each dead (`FUN_0058c9a0`); the last: won; killer and the Summit's
  players of the level bits 0 and 13 and experience (`FUN_0058c5c0`: 1.4M
  / 20M / 40M, at most the next level's span), the rest 14; state 5, log
  13, object 561 at its fall (OperateFn 69: 20169, bit 4), the chain.
- Stairs (547, OperateFn 66): bit 0 or 1, and won here (or the record
  shut); mode 0 opens, mode 2 leads to 128.
- Town congratulations: bits 5..9 (20167 / 20165 / 20166 / 20168 / 20164).

## Eve of Destruction (a5q6)

- Chain: state 2, log 1. Out of town at 2: state 3, log 1. Into 131+:
  log 2.
- BaalThrone (MonAI 134, `FUN_005ef320`): +0x14 waves, +0x18 flags, +0x1c
  next frame. No other monster within 64: flag 1 → the next wave
  (`FUN_005ef210`: skill 286 in S3 at y + 13, superunique 61..65 via
  0x6e3528 / [0x744304]+0xae0; 100 frames), or after five baalcrabstairs
  (0x22f, `FUN_00574370`); flag 1 unset → skill 285 in S3, 250 frames.
- BaalToStairs (MonAI 138, `FUN_005ef620`): object 563 within 25, walked
  at; within aip1 gone in (`FUN_0058e600`: +0x86, log 3).
- BaalTaunt (MonAI 136, `FUN_005ef710`; monster 545, a MonPreset of act 5):
  a target in mode 1 (a player or monster standing) counts up +0x14; past
  aip2 (10) it's 0 again and Skill1 Baal Taunt (284) goes at it in A1
  (`FUN_005dead0(4, 0x11c)`). Not standing: +0x14 back to 0. Then past aip3
  (20) it warps to the target (`FUN_00554ea0`, then stands 25), past aip1 (3)
  walks straight at it (`FUN_005deca0`), else stands 25. Proven:
  `tools/emu/baal_taunt.py` (600 runs, `tests/baal_taunt_cases.inc`).
- Baal Taunt (srvdo 28) stands baal taunt control (546) at the target. Its
  do 34 (`FUN_005b04a0`): from frame Param1 (25), each frame rand(2) picks
  SubMissile1 (lightning control, delay Param2 3) or SubMissile2 (poison
  control, Param3 45), laid when its age divides by that delay; Range 75.
  The controls live a frame (srvdo 3, AlwaysExplode); at their end hit 58
  (`FUN_005acdf0`): baal taunt lightning at a random subtile within sHitPar1
  (10), or hit 59 (`FUN_005acf20` → `FUN_005a9370`): baal taunt poison the
  8 even ways of 0x6e2510 / 0x6e24d0 at Param1 << 7 and the odd ones at
  Param2 << 7 (`FUN_0059fa30` flags 0x17: that velocity as is, × 75 / 100).
- Object 563 (OperateFn 70): on 131 with +0x86 to 132; else to 131.
- Baal's death (`FUN_0058df20`): log 4; the Chamber's players without bits
  0 / 1 bits 13 and 0 and the next difficulty (`FUN_0058dd30`), the rest
  14; gold for the killer; state 5; missile 625 (baalfx control, 650
  frames; its end `FUN_005ad970` → `FUN_0058e920`: Tyrael 0x209 at −5, −5).
- Tyrael's talk closed (once): object 565 by each Chamber player
  (OperateFn 72: with bit 13, Harrogath, the save, cinematic 7, bit 10:
  "The last portal" below).
- Join with it done: bit 10 and the next difficulty.

## Larzuk's sockets and Anya's name

Both are Charsi's imbue (C->S 0x38 kind 0, `FUN_00579d60`) at another NPC;
the client's menu table 0x7253e0 adds the entry with handler 0x4b35b0 (the
item panel) while the reward is due:

- Larzuk (0x1ff, quest 0x23 bit 1; `0x4b3730`: slot 2 = 0x58dc "add
  sockets"): `FUN_0062c770` (not gold, not a quest item but Wirt's leg, no
  sockets, `FUN_0062bc20` > 0, stat 194 none) — sockets: normal / superior /
  low the most (`FUN_0062bc20`: min(gemsockets, MaxSock1 / 25 / 40 by item
  level <= 25 / <= 40 / above)), magic 1 + rand(min(most, 2)) on the item's
  seed, set / rare / unique / crafted 1; `FUN_0062bcb0` caps at the grid
  and 6. `FUN_005877c0`: bit 0, bit 1 off.
- Anya (0x200, quest 0x26 bit 1; `0x4b3760`: slot 3 = 0x58dd
  "personalize"): `FUN_0062c6a0` (not gold, types 5..7 quivers and ears,
  not broken or personalized, not compact-save (Items +0x143), nameable
  (+0x145)) — flag 0x1000000 and the player's name. `FUN_0058bc00`: bit 0,
  bit 1 off.

## The Ancients' deaths and the statues' return

`FUN_0058c9a0` (a dead unit bound to 0x23), while the Ancients are heard
(+0x10), awake (+0x11) and no player has died on the Summit (+0xc 0):

- The first time, the return timer (+4; `FUN_00543f10(record,
  FUN_0058bd50, 2)`: every 2 frames till it returns true).
- By class: 0x21d marks +0x3c (statue id +0x14), 0x21e +0x3d (+0x18),
  0x21c +0x3e (+0x1c), each its own statue (474 / 475 / 476 hold 0x2d /
  0x2b / 0x2c, SuperUniques' ancientbarb3 / 1 / 2).
- Its death missile (`FUN_0058c8d0`, `FUN_0059fa30` flags 0x420):
  missile 541 "ancient death center" from the dead Ancient at its
  statue's spot, level 1, the statue as its target unit (`FUN_0064a710`).
  Flag 0x400 makes its range the frames to get there. It has no cel; its
  light is red (2), and the client's do 55 (`FUN_004d8260`, the table
  0x72a398 + 4 x n) makes an "ancient death cloud" (542, its TravelSound
  `quest_ancients_cloud`) every third frame: from a subtile -1..0 off it
  each way (two `& 1` draws), sent at that start's -10..9 off (two `% 20`
  draws, taken first), flags 0x21.
- Then the count (+0x44) and the win as above.

The timer (`FUN_0058bd50`): with +0xc 0 and +0x11, each marked statue goes
to mode 1 (`FUN_00624690`) with its event 1 at FrameCnt1 frames (mode 2),
its mark cleared and +0x30 2. A statue not found keeps the timer going.
Otherwise +4 is cleared and the timer ends.

A player's death on the Summit (`FUN_0058cf00`, the record not won): +0xc
goes up by one, +0x10 / +0x11 are cleared, and `FUN_0058c000` runs: the
Ancients still made are removed, the marks cleared, the statues and the
altar back to mode 0.

## Baal's death: the gold and the save

`FUN_0058df20`, with a killer (`param[3]`): `bVar2` is the killer's
quest 0x28 bit 0 *before* the shares. The shares are `FUN_0058dea0` (in
the Chamber 0x84 without bits 0 / 1: `FUN_0058dd30`, bits 13 and 0, the
next difficulty, +0x88 += 1), `LAB_0058dd90` (bit 13 and a party:
`FUN_0058dc60` gives the party's act-5 players bits 0 and 13),
`LAB_0058dde0` (not done: bit 14, `FUN_00545920`) and `FUN_0058de30` (bit
13: S->C 0x5d {0x24, 2}, interact cleared). For a first killer, +0x88 gold
piles at Baal (`FUN_0055b030`), each `FUN_004bc500(quest seed, 1500 + 6000
x difficulty, min(3000 + 6000 x difficulty, 0xffff))`. Then the game is
saved (`FUN_0052e2a0`, every player) and the state is 5. Missile 625 comes
after all that.

## Baal Corpse Explode (skill 285)

Srvdo 141 (`FUN_005d2e80`): an area search (`FUN_0056dcc0`, flags 0x3002,
radius Param5 + (level - 1) x Param6 = 40) calls `FUN_005d2e50` on each
corpse: `FUN_005c50b0(285, level)`, which is Corpse Explosion's
`FUN_005c4df0`. That takes its damage from calc1..calc2, and row 285 has
neither, so the blast does nothing (bugs.md #27). The corpses are spent.

## Baal Taunt's sound

Cltstfunc 46 (`FUN_004f0160`, the table 0x727a90): at the cast's start it
plays the skill's prgsound (+0x10e, `monster_baal_taunt_1`) on the caster
(`FUN_004b9a00`), then the client missile.

## The last portal (OperateFn 72, `FUN_0058e740`)

It needs the record's +0x9c 2 and the player's bit 13 (else the
interact is cleared, `FUN_00553380`). Then the player goes to Harrogath
(`FUN_0053aec0(0x6d, 0)`), the game is saved (`FUN_0052e2a0`), and if
the client isn't busy (`FUN_00535060`): the interact is cleared
(`FUN_00554190`), client +0x4c = 1 (busy), and S->C 0x61 {7}
(`FUN_0053d940`): the client plays cinematic 7, D2X_OUT. Then bit 10. The
0x39b here is an assert line (`FUN_005531c0(unit, file, line)` gets the
client), not a cinematic. The client's 0x61 handler is `FUN_0045e660`
(table 0x7114d0). What it sends back to clear +0x4c isn't traced.

## InitFns 66..70

- 66 (`FUN_0058eac0`, 459 "Drehya Start In Town"): it keeps the marker's
  id and spot (+0xe4..+0xec). With +0x84 2: the chain check
  (`FUN_00589160`), then Anya (0x200) within 1 of it once (+0x91, +0x94).
  With +0x87 (told) it sets its event 7 twelve frames on (her portal again).
- 67 (`FUN_0058a5b0`, 460 "Drehya Start Outside Town"): keeps its id
  (+0xf8); fana not made: event 7 at 25 frames.
- 69 (`FUN_0058a6c0`, 462 "Nihlathak Start Outside Town"): with +0x88 1
  and not made (+0x92), superunique 60 at its spot (`FUN_0054e600`).
- 70 (`FUN_00587830`, 542 "Larzuk Greeting"): an empty function
  (stubs.md).

## In d2d

- `World::act5_kill` (world_quests_act5.cpp): the death missile (visual
  only, `Missile::sub` the cloud), `RiteQuest::ancient_fell` /
  `statues_back`, `World::rite_update`'s timer; Baal's gold and
  `World::save_due`.
- The clouds: `missiles_update`'s client do 55 (ai.hpp).
- Corpse Explode: the BaalThrone think (ai.cpp) spends the corpses.
- Baal Taunt's sound: `Fight::monster_sounds` (cltstfunc 46).
- The last portal: `World::movie` -> main.cpp plays D2X_OUT and returns
  to the game (and marks `d2xout` seen). A host's 0x61 goes the same way
  (netgame.cpp).
- Not yet: Baal Taunt's missiles draw on the update's rng, not the
  missile's seed; the death missile's distance is the straight line
  (`FUN_006417f0` not traced); the record's +0xc isn't kept.
