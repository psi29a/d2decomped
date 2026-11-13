# Act 3 quests — game.exe 1.14d

Conventions, flag helpers and the record layout are in `quests.md` /
`quests-act1.md`.

## The records

The quest init table at `0x731510` (24-byte rows `{init fn, act, 0x64,
flag, record id, quest word}`), Act 3's rows (0x11..0x17):

| record id | quest word | init fn | file |
|---|---|---|---|
| 0xe | 0x10 | `FUN_005b7010` | a3q0.cpp (the act's opening: Hratli) |
| 0xf | 0x11 | `FUN_005b7e90` | a3q1.cpp (Lam Esen's Tome: Alkor) |
| 0x10 | 0x12 | `FUN_005bda30` | a3q2.cpp (Khalim's Will: word 0x12 bit 0 breaks the orb at a join) |
| 0x11 | 0x13 | `FUN_005b9850` | a3q3.cpp (the Blade of the Old Religion: Hratli) |
| 0x12 | 0x14 | `FUN_005baa70` | a3q4.cpp (the Golden Bird: Cain, Meshif, Alkor) |
| 0x13 | 0x15 | `FUN_005bbeb0` | a3q5.cpp (the compelling orb binds it; the zealots spare its bit 0, monster-ai.md) |
| 0x14 | 0x16 | `FUN_005bcdc0` | a3q6.cpp (The Guardian: Mephisto binds it) |

Message tables: `tools/emu/quest_msgs.py <record +0xdc>` dumps them (0xc4-byte
blocks, quests.md). NPCs: 245 Cain, 252 Asheara, 253 Hratli, 254 Alkor, 255
Ormus, 264 Meshif, 297 Natalya.

## The opening (a3q0.cpp)

Hratli (253) greets with 465, a sorceress (the unit's class 1) with 466
(`FUN_005b6f20`), his "!" (`FUN_005b6fa0`) till word 0x10 bit 0. Hearing
either (`FUN_005b6ed0`) sets bit 0 and the game's bit 13; a join with bit 0
the game's (`FUN_005b6fd0`). d2d: `rules::HratliGreeting`.

## Lam Esen's Tome (a3q1.cpp)

State (+0xc, `FUN_00544350`'s EDX): 0 not yet, 1 given out (entering Lower
Kurast, 79), 2 Alkor told (549, 0x225), 3 under way (leaving the docks; the
tome dropped, 0x281; taken off its stand), 4 carried (0x26c), 5 done.
Data: +0 the first hand-in pays everyone (1 at init), +2 a tome was taken,
+8 tomes carried, +0xc Alkor's flourish on the talk's close (`LAB_005b76e0`:
unit event 0x23), +0x18 the stand's mode.

- **The stand** (objects.txt 193 `LamTome`, InitFn 23 `FUN_00544e30`: the
  record's +0x18, 2 when closed; OperateFn 28 `FUN_005b7a60`): one with bit 0
  gets the flourish; else `bbb ` drops there (`FUN_00559a30`), mode 2.
- **Alkor** (254): "!" (`FUN_005b72b0`) given out and not done or closed, or
  the tome carried. Blocks 0x73ecf0 by state (0x73f188: -1, 0, 1, 2, 3, 4);
  block 3 with the tome, 4 once rewarded (`FUN_005452c0`).
- **564 heard** (`FUN_005b7740`, 0x234) with the tome: it goes
  (`FUN_00544160`); the first time each player in act 3 not done gets bits 13,
  0, 1 (`FUN_005b74e0`), bit 1 pays 5 stat points (`FUN_006272b0` stat 4) and
  clears (`FUN_005b7580`), the rest bit 14 (`FUN_005b75d0`); done here: the
  game's bit 13, state 5, the chain (+0xf0).
- **Log** (`FUN_005b7d70`): hidden till word 0xf bit 0; done 0xb / 0xd; the
  tome carried 2; under way 1, lost 8 (9 in a multiplayer game, game +0x6a
  != 3); carried by someone 0xc (1 in act 3).

- **Item events** (set at init, `FUN_005b7e90`: +0xb0 `FUN_005b7970`, +0xb4
  `LAB_005b7a00`): event 4 (an item into a player's inventory, `0x543d80`)
  and event 5 (one dropped, `0x543db0`) go only to the records on the
  item's own list (item +0x74, `FUN_005439a0`; bound as it's made,
  `FUN_00555d20` at 0x555d82: Items.txt quest − 1), so only `bbb ` reaches
  a3q1; the handlers don't test the code again or the state. With +9: event 4 sends log 2 (`FUN_00544300`, EDX 2 at
  0x5b79a0), state 4 (EDX 4 at 0x5b79ba), the player into data +0x1c;
  event 5 takes the player out (0x5b7a1a), state 3 (EDX 3 at 0x5b7a2f),
  log 1 (0x5b7a44), whatever the state was (+9 the only gate).

- **Hratli's map AI** (`Q17_SetupHratliMapAi`, `FUN_005b7230`, namespace
  `A3Q1` but keyed by `QUESTINT_SpokeToHratli`, a separate internal
  record from the journaled a3q1): stores the current DRLG map pointer
  into that record's data and, the first time Hratli's own monster unit
  and a valid map are both in hand (`bDrlgMapTaken` one-shot), transfers
  the DRLG preset file onto Hratli's own map-AI
  (`DRLGPRESET_TransferMapAI`). No `QuestRecord` writes.
- **The sequence filter** (`Q18_SequenceFilter`, `FUN_005b7cd0`): the
  chain pattern in `quests.md` ("Quest-chain SequenceFilter"); once
  state 5 (done) or past intro, chains into
  `nSequenceQuestId = QUEST_BladeOfTheOldReligion` (a3q3 below).

d2d: `rules::LamEsenQuest`; the pickup / drop are polled from what's
carried. `tests/quests_d2d.py` plays it.

## The Blade of the Old Religion (a3q3.cpp)

State (EDX): 1 given out (entering the Flayer Jungle, 78; the altar used),
2 Hratli told (571, 0x293), 3 under way (leaving the docks, 0x1e2), 4 the
Gidbinn carried (0x2c6), 5 handed in (587, 0x26d). Data: +0 the Gidbinn
found, +1 Hratli's told (marks on the talk's close, `LAB_005b8c50`), +2 the
guardians came, +3 the altar used, +5 the timer set, +8 / +0xc the altar's
spot, +0x1c Gidbinns carried, +0x20 lost, +0x24 the guardian's id, +0x2c
the altar's mode.

- **The altar** (objects.txt 252 "gidbinn decoy": InitFn 25 `FUN_005b9ae0`,
  OperateFn 31 `FUN_005b9b40`; the stand 251, InitFn 39): used with none of
  bits 0, 7, 8: mode 1; the first use sets a 7-tick timer (`FUN_00543f10`,
  `FUN_005b9a30`, one look: every path returns 1, so with no player there
  the guardians never come): with a player in its room, `FUN_005b9930` makes a unique
  pack of class 0x197 (fetish11, `FUN_005a43e0`) and the record's +0xc0 =
  `FUN_005b9980`: the unique's death drops `g33 ` and marks everyone
  (`FUN_005b8b20`: carried bit 5; found bit 4 and bit 2; told bit 3).
- **Heard** (`FUN_005b9240`): Ormus 587 with the Gidbinn, bit 5, no bit 8:
  it goes, bit 6, the game's bit 13, state 5; Ormus 593 with bit 6: bit 8, a
  rare ring (`FUN_005466b0("rin ", 21 / 35 / 75, 6)`); Asheara 589: bit 7, a
  free mercenary (`FUN_00579180`: the first on her list, hired, the list
  rolled anew). Bits 7 and 8: 13 and 0.
- **"!"** (`FUN_005b8e10`), **talk** (`FUN_005b8ec0`, blocks 0x73fb98, state
  → 0x7401b8), **log** (`FUN_005b8cc0`) and **join** (`FUN_005b9700`) as
  `rules::BladeQuest`.

- **Completion notify** (`Q20_PlayerNotifyCompleted`, `FUN_005b8bf0`):
  the broadcast callback passed to `QUEST_SetStateAndBroadcast`; sends
  quest-update packet 0x11 to a player only when the quest is
  Completed or FinishedInPreviousGame *and* a fresh JustCompleted or
  CannotCompleteQuestInGame flag is pending.
- **Locking a player out this game** (`Q20_PlayerCheckBladeQuestCannotComplete`,
  `FUN_005b9130`): per-player iterate callback; if the quest is below
  Step0x06/0x07/0x08 and the player no longer holds the Gidbinn
  (`g33`/0x20333367), sets `QUESTSTATE_CannotCompleteQuestInGame`. Run
  after the item's consumed elsewhere, so whoever never picked it up
  can't still finish this game.
- **The sequence filter** (`Q20_SequenceFilter`, `FUN_005b9610`): the
  chain pattern (`quests.md`); below state 5 it bumps `nStateFlag` 0→1
  itself (the only SequenceFilter instance seen doing a state write of
  its own), then at state 5 chains to
  `nSequenceQuestId = QUEST_AbleGoToAct3`.
- **The stand** (object 251, InitFn 39, `Objects_InitFn39_GidbinnAltar`,
  `FUN_005b9d40`): records the object's GUID and spot (`nAltarX/Y`) and
  applies the stored update flag (`nQuestStage`) on (re)creation. No
  `QuestRecord` write.

d2d: `rules::BladeQuest`; the guardians' "room" is 20 cells; the lost log
(+0x20, 7 / 8) is left out. `tests/quests_d2d.py` plays it.

## The Golden Bird (a3q4.cpp)

State (EDX): 1 the figurine (dropped, 0x36f; picked up, 0x26e), 2 Cain told
(527, 0x1de), 3 the bird (Meshif's 529, 0x236; picked up, 0x263), 4 Alkor
took it (534, 0x1fd), 5 done (538, 0x218). Data: +1 armed, +0xc the
figurine due (both 1 at init), +4 its carrier, +0x10 items carried.

- **The carrier.** `FUN_005a09e0` (placing a unique, superunique or preset
  special) calls `FUN_00544e80`: in act 3, record 0x12 open, the monster not
  class 0x197 and not MonStats +0xd & 0x40, → `FUN_005bac70` while armed and
  due, and not while a3q3 makes its guardian (record 0x11 data +4, set
  around `FUN_005b9930`'s `FUN_005a43e0` at 0x5b993f..0x5b995a; that unit
  is class 0x197, excluded anyway): the quest bound to it
  (`FUN_005436b0(0x12)`), +0xc0 = `FUN_005bab60`.
- **Carrier release** (`FUN_005bacf0`): when data +2 is exactly 1 and
  data +4 matches the unit ID (or `0xffffffff` for a null unit), clear +2
  and set +1 to 1. `FUN_00544f20` reaches it for a quest-bound unit in Act 3.
  `tools/emu/quest_bird_carrier.py` checks 48 synthetic cases; no d2d parity
  or caller-path check is claimed.
- **Its death** (`FUN_005bab60`): unless the killer has bit 0, `j34 ` drops
  (`FUN_00559a30`), due off, state 1.
- **Talk** (`FUN_005b9ed0`, blocks 0x7401d0): done → block 6 once rewarded;
  bit 1 → 5; the bird: Cain without bit 4 → 3, else 2; the figurine: Cain or
  Asheara → 0 (7 once bit 2), the rest 1.
- **Heard** (`FUN_005ba320`): Cain 527 bit 2; Cain 531 bit 4; Meshif 529 with
  the figurine: it goes, `g34 ` made (`FUN_005466b0`); Alkor 534 with the bird:
  it goes, bit 1; Alkor 538 with bit 1: bit 1 off, bits 0, 13, bits 2..11 off
  (`FUN_0065c3e0`), bit 5, `xyz ` made, the game's bit 13.
- **"!"** (`FUN_005b9d90`): not done and no bit 1: Alkor with the bird,
  Meshif with the figurine, Cain with the figurine and no bit 2 or the bird
  and no bit 4.
- **The Potion of Life** (0x55e44e): with bit 5, bit 5 off and +20 max life
  (`FUN_006272b0` stat 7, 0x1400); else event 0x13. Picking it up wants bit 5
  (`FUN_0055cc90`).

- **Item event** (+0xb0 `FUN_005ba6a0`, set at init 0x5baacf; event 4 on the
  record's own items, `g34 ` / `j34 `): it also reads the item (event
  +8 -> `FUN_006335f0` -> code at +0x80): bits 0 and 1 clear; `g34 ` state
  3 (0x5ba721), log 3; `j34 ` log 1, state 1 (0x5ba76c), and bit 6 with
  unit event 0x48 the first time (0x5ba794). No +9 test.

- **Completion notify** (`Q21_PlayerNotifyCompleted`, `FUN_005b9e70`):
  same shape as a3q3's (above); packet 0x12, gated on Completed or
  FinishedInPreviousGame with a pending JustCompleted or
  CannotCompleteQuestInGame.
- **The sequence filter** (`Q21_SequenceFilter`, `FUN_005ba7b0`): the
  chain pattern (`quests.md`); at state 5 or past intro it chains
  through *both* `nSequenceQuestId` (`QUESTINT_SpokeToHratli`) and, in
  the same call, `QUESTINT_BladeOfTheOldReligion` — the only
  SequenceFilter instance seen chaining two quests instead of one.

d2d: `rules::GoldenBirdQuest`; the carrier is the first boss the World sees
on an act 3 level; pickups polled. `tests/quests_d2d.py` plays it.

## Khalim's Will (a3q2.cpp)

Cain's (245) all; state 1 entering the Great Marsh (77), 2 Cain told (543:
bit 2). Each part shown sets its bit (`FUN_005b8060`: 545 eye 3, 546 brain
4, 544 heart 6, 547 flail 5, 548 the Will 7). Talk (`FUN_005bd630`, blocks
0x73f1a0: greetings 0..5, the same as topics 6..11), "!" (`FUN_005bd500`)
and log (`FUN_005bd850`: no done case) by the parts carried.

- **The chests** (objects 405 / 407 / 406, OperateFns 57 heart / 58 eye / 59
  brain: `FUN_005b8860` / `005b8940` / `005b8a20`): rand(5) + 5 gold piles on
  the quest seed, one part for each player in the game carrying neither it
  nor Khalim's Will (`LAB_005b8810` counts them into record 0x10 data +0xc;
  `FUN_00559a30(2)` each; no quest bit is read), a chest's drops
  (`FUN_00585b90`). In the sewers 2 (93), the Spider Cavern (85), the Flayer
  Dungeon 3 (91).
- **Log-send filter** (`FUN_005b7f90`): reads the player's quest flags for
  the game's difficulty and sends quest 0x10 only when word 0x12 bits 0 and
  15 are both clear. `tools/emu/quest_khalim_log.py` checks all four bit
  combinations in all three difficulties with lookup, bit tests and sender
  hooked; it does not check those helpers or d2d parity.
- **The sewers' stairs** (366, InitFn 41 `FUN_005b8660`: the record's +8
  mode; OperateFn 44 `FUN_005b84e0`: in mode 2 the warp tiles of its room are
  made usable, `FUN_0059d9d0` → `FUN_005550b0`) open by the lever (367, InitFn
  42, OperateFn 45 `FUN_005b8530`: the stairs to mode 1, +8 = 2).
- **The Will** made in the Cube (`FUN_005b86e0` from the transmute, 0x5664d4).
- **The quest-state packet** (`Q19_SendQuestStatePacket`, `FUN_005b81c0`):
  sends S->C **0x5D** quest-state (quest id 0x10, `nQuestPacketFlags`
  carried along) with whatever sub-stage byte the caller gives it;
  pure client notification, no `QuestRecord` write.
- **Item pickup** (`Q19_Callback04_ItemPickedUp`, `FUN_005b8210`): gated
  on `QUEST_AbleGoToAct3` Completed and Khalim's Will not already
  Completed. By the picked-up item's code (the flail `rt1`/0x20316671,
  eye `piv`/0x20796571, brain `box`/0x20726271, heart `msf`/0x20726871,
  or the assembled Will `rt2`/0x20326671) and which relics are already
  carried, it picks one of the sub-stage bytes (1..7) and calls
  `Q19_SendQuestStatePacket` with it — the log text for "found the
  flail but not the eye" vs. "all four, go to the chests" etc. Reads
  `QUESTSTATE_Step0x02..0x07` but never sets them.
- **The sewer stairs** (`Q19_UpdateSewerStairsObject`, `FUN_005b85e0`):
  run from the sewer lever's own timer event; once
  `bSewerStairsInitialized`, re-finds the stairs object by its stored
  GUID, re-applies its update flag, and — only on a change — schedules
  the stairs' own operate event `FrameCnt[1] >> 8` frames out. Pairs
  with `Objects_InitFn42_SewerLever` (`FUN_005b86b0`): while the quest
  is still pre-intro, forces the lever object's update flag to 2
  (hidden). Neither touches a `QuestRecord` bit.

## The Blackened Temple (a3q5.cpp)

State: 1 entering the Causeway (82), 3 Ormus told (594; 2 with the game's
bit 13), 5 leaving the docks (4), 6 / 7 the Council dead (the orb whole /
broken), 7 done. Data: +0xc the orb broken, +0xd / +0xe the flail / a Cube
dropped, +0x34 Council left, +0x38 the orb's hits, +0x3c / +0x40 players
needing a flail / a Cube (`FUN_005bb7b0`).

- **The Council**: superuniques 26 / 27 / 29 (Ismail, Geleb, Toorc; their
  class's case in `FUN_005a49b0`'s switch, 0x5a4ea4 → 2) carry the record.
  Each death (`FUN_005bbc00`): the first drops `qf1 ` per player needing it,
  the next `box ` per player without; the last: state 6 / 7, everyone in act
  3 in the killer's room or next to it bit 13 and bit 0 (Khalim's Will done)
  or bit 4 (`FUN_005bae20`), the rest bit 14. The orb (0x16e) dying: unit flag
  0x20000.
- **Object initialization** (`FUN_005bbb70`): looks up quest record 0x13
  and, when its data byte +0xc is nonzero, sets the supplied object's mode
  to 2. The function dereferences the returned record without a null check.
  `tools/emu/quest_temple_object.py` checks five flag values with lookup and
  mode setting hooked; it does not test a missing record or those helpers.
- **The orb** (object 404, InitFn 60 `FUN_005bbba0`; OperateFn 53
  `FUN_005bb980`): with the Will in hand (`FUN_0063bef0`, weapon type 0x2d),
  the second hit: word 0x12 bits 0, 13, the Will taken, word 0x15 bit 0 for
  one with bit 4; the orb monster gone, the object mode 1, +0xc set, the chain.
- **Cain** 626 with bit 4 (`FUN_005bb210`): Khalim's parts taken once his
  Will's done; bit 0, bit 4 off; state 7.
- Talk `FUN_005bafd0` (blocks 0x7408b8, state → 0x740ed8), "!"
  `FUN_005bad60`, join `FUN_005bb480`; no log function (+0xe8 0).
- **Completion notify** (`Q22_PlayerNotifyCompleted`, `FUN_005baf70`):
  same shape again; packet 0x13, same Completed-or-FinishedInPreviousGame
  plus pending-JustCompleted-or-CannotComplete gate.
- **The sequence filter** (`Q22_SequenceFilter`, `FUN_005bb410`): the
  chain pattern (`quests.md`); at state 7 (fully done) chains to
  `nSequenceQuestId = QUEST_TheGoldenBird`.
- **Tracking a Council kill** (`Q22_TrackCouncilKill`, `FUN_005bb550`):
  called per Council death; sets `bCouncilTrackingActive`, dedups the
  killer's GUID into a 6-slot list and bumps `nCouncilKillCount` /
  `nCouncilKillsRemaining`. On the first qualifying kill it broadcasts
  live state 1 or 3 via `QUEST_SetStateAndBroadcast` (using
  `Q22_PlayerNotifyCompleted` above), then sets `nStateFlag` to 4 or 5
  by whether Lam Esen's Tome (`quests-act3.md` "Lam Esen's Tome") has
  just completed, and resyncs every player's log. This is the detail
  behind the existing "**The Council**" paragraph's "the last [kill]:
  state 6 / 7" above — the two functions share the same kill path.
- **The status filter** (`Q22_StatusFilter`, `FUN_005bbe20`): hides the
  log entry (byte 0) until `QUEST_AbleGoToAct3` is Completed; once
  visible, Completed → 0xb (0xd if JustCompleted), Step0x04 → 0x4 or
  0xd by a council-tracking flag, else the raw `nStateFlagPrev`. The
  comment at its own call site notes `Q22_InitData` actually wires
  `fpStatusFilter` to null in this build, so this handler is dead code
  in 1.14d (kept, per its own namespace tag, as the historical/Mac
  status handler).

## The Guardian (a3q6.cpp)

State: 1 opened (a3q5's chain, `LAB_005bcc60`, or a level past 97), 3 Ormus
told (628; 2), then 5 (4 with the game's bit 13) leaving the docks, in the
Durance 1 or 3; 7 the report taken. Talk `FUN_005bc270` (blocks 0x740ef8,
state → 0x741518; bit 0xb → block 5: every docks NPC greets with the report,
657..663, `FUN_005bc5a0` clears bit 0xb); "!" `FUN_005bbfe0` (Ormus while
given out, everyone with bit 0xb); join `LAB_005bcc80` (bit 0, 0xb or word
0x17 bit 0: +0xc 2, the hellgate open).

The alert gate `FUN_005bbfe0` tests word 0x16 bit 0 first. If clear,
NPC 0xff and record state 1 return true immediately; all other cases return
whether bit 0xb is set. `tools/emu/quest_guardian_alert.py` checks 32
synthetic flag, NPC and state combinations with the flag test hooked.

Record callbacks (`FUN_005bcdc0`): +0xc0 a monster dies `FUN_005bc8b0`,
+0xa0 `FUN_005bc270`, +0xcc `FUN_005bc5a0`, +0xac entering a level
`FUN_005bc3c0`, +0xec `FUN_005bbfe0`. Data (0x34 bytes): +0 Mephisto's
log timer queued, +1 / +4 the hellgate made / its id, +2 / +8 the bridge
made / its id, +0xc the state (2 once Mephisto's dead), +0x10 the bridge's
mode, +0x1c soulstones owed.

- **Mephisto dies** (`FUN_005bc8b0`): the killer, if word 0x16 bit 0 is
  clear, and every player in the Durance of Hate 3 (level 0x66,
  `FUN_005bc190`) without it get bits 0xd, 0 and 0xb (`FUN_005bc140`) and
  progression diff × 4 (5 in LoD) + 3 (`FUN_00538680`, EDX 3: Act 4).
  Each one that hadn't bit 0xb owes a soulstone: the dying unit's drop
  code becomes `mss ` and `FUN_00559a30` drops that many. The state goes
  to 2; the hellgate goes to mode 1, its event 1 at FrameCnt1 frames on.
  With the record open (+9) and data +0 clear: a 12-tick quest timer
  (push 0xc at 0x5bc9d6), data +0 1.
- **Mephisto's log timer** (0x5bc720, no Ghidra function): unless the log
  (+0xb) is 4 (0x5bc727), +0x14 0 and `FUN_00544300(record, 4, 0,
  0x5bc210, 1)`: the log 4, sent to each player by the quest update
  notifier 0x5bc210 (word 0x16, message 0x14; quests.md). Then data +0 0;
  returns 1. d2d: `GuardianQuest::arm_log` / `quest_step`.
- **The hellgate** (objects.txt 342, `1y`, InitFn 44 `FUN_005bcbf0`):
  takes the record's state as its mode (0 till he's dead; level 104:
  mode 2). Its parts are DC6, not DCC. OperateFn 46 leads to Act 4.
- **The bridge** (341, `xj`, InitFn 45 `FUN_005bcb90`, 11 × 5): takes
  +0x10 as its mode and, short of 2, schedules event 7 20 frames on. Event
  7 (`FUN_005449e0` for class 0x155 → `FUN_005bcac0`), every 24 frames:
  with the state 2 and a player (unit type 0) of its room (its room's
  unit list, room +0x74; any player, the first) within 18
  (`FUN_00641530`), mode 0 → 1 (it rises, 28 frames) and the next 1 → 2,
  its footprint lifted (`FUN_00623830`: no HasCollision2). Mode 2 stops it.

- **Completion notify** (`Q23_PlayerNotifyCompleted`, `FUN_005bc210`):
  same shape as a3q3/a3q4/a3q5's; packet 0x14, same
  Completed-or-FinishedInPreviousGame plus pending-notify gate.
- **Party credit** (`Q23_PartyMember_SetCompleted`, `FUN_005bc750`):
  run via `ExecuteFunctionForEveryPartyMember` on Mephisto's death; for
  each party member not already Completed who is physically in Act
  III, calls `Q23_SetGuardianQuestComplete` and bumps this quest's
  `nPlayerCount` — Mephisto-kill credit for nearby party members who
  weren't in his room.
- **Driving the state-flag machine** (`Q23_NotifyGuardianStateToPlayers`,
  `FUN_005bcf60`, namespace `A3Q7` though its data is `pA3Q6`): while
  active and `nStateFlag < 6`, either saves the current
  `nStateFlagPrev` and advances to 6 (7 in multiplayer), or restores
  the saved value — both paths through `QUEST_SetStateAndBroadcast`
  with `Q23_PlayerNotifyCompleted` as the per-player callback. No
  direct `GetQuestState`/`SetQuestState` calls (uses the broadcast
  helper instead).
- **The Dark Wanderer's map AI** (`Q24_SetupDarkWandererMapAi`,
  `FUN_005bd040`, same `A3Q7`/`pA3Q6` pairing): the Act 3→4 transition
  actor's equivalent of a3q1's `Q17_SetupHratliMapAi` above — stores
  the DRLG map pointer, then one-shot transfers it onto the tracked
  monster's own map-AI once both exist and `bDrlgMapTaken` is still
  clear.

d2d: `World::mephisto_died`, `World::mephisto_bridge`; the hellgate's
OperateFn only says Act 4 isn't built. `tests/quests_d2d.py` (map seed 3).
Not traced: the record's other callbacks (the talk, the level entries).
