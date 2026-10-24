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
- Object 563 (OperateFn 70): on 131 with +0x86 to 132; else to 131.
- Baal's death (`FUN_0058df20`): log 4; the Chamber's players without bits
  0 / 1 bits 13 and 0 and the next difficulty (`FUN_0058dd30`), the rest
  14; gold for the killer; state 5; missile 625 (baalfx control, 650
  frames; its end `FUN_005ad970` → `FUN_0058e920`: Tyrael 0x209 at −5, −5).
- Tyrael's talk closed (once): object 565 by each Chamber player
  (OperateFn 72: with bit 13, the cinematic 0x39b, Harrogath, bit 10).
- Join with it done: bit 10 and the next difficulty.

## Not yet in d2d

- Baal in the Chamber (BaalCrab / BaalCrabClone, the Diablo-like chooser
  `FUN_005fbdd0..`), BaalTaunt, AncientStatue, the ubers.
- Larzuk's socketing and Anya's personalizing (a5q1 / a5q4 bit 0).
- The Ancients' death missile (`FUN_0058c8d0`), the statues' return
  (`FUN_0058bd50`), Baal's Corpse Explode, the gold for Baal's killer, the
  cinematics.
