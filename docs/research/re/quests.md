# Quests — game.exe 1.14d

The quest scripts are D2Game's `Quests/aNqM.cpp`, linked into game.exe.
The asserts name the files: a1q1.cpp, the Den of Evil, is at
`0x58fd20..0x590830`. d2d's port is `components/rules/quests.hpp`
(`DenQuest`, test_quests).

## Flags

Each difficulty has 96 bytes of quest flags in the save (d2s +0x159), 16
bits per quest. `FUN_0065c310` tests bit `q*16 + b`, `FUN_0065c360` sets
it and `FUN_0065c3a0` clears it. `FUN_0065c3e0` clears bits 2..11.

Bit use in a1q1:

| bit | meaning |
|---|---|
| 0 | done (rewarded) |
| 1 | reward due |
| 2 | given (Akara spoke) |
| 3 / 4 | in the Den (4 when the log state is past 1) |
| 13 | cleared in this game (the player took part) |
| 14 | cleared by someone else (`FUN_00590080`) |
| 15 | closed |

Akara's reward also sets quest 41's bits 13 and 1. Quest 41 is her
stat/skill reset, which the reward opens.

## The quest record

`FUN_00590720` sets up the record. Each quest has one record per game.

- **Fields:**
  - +0xc: state, set by `FUN_00544350`, which logs "changing state to %d".
  - +0xb: the quest log's state.
  - +0xe0: the quest number (1); +0xe4: the respec quest (0x29).
  - +0xdc: the message table (`0x7366b0`); +0x18: 0x8c bytes of the quest's own data.
- **Callbacks:**

  | field | function | when |
  |---|---|---|
  | +0xa0 | `FUN_0058ff90` | which messages an NPC has |
  | +0xa8 | `LAB_0058fc40` | a talk closes |
  | +0xac | `FUN_00590470` | entering a level |
  | +0xc0 | `FUN_00590260` | a monster dies |
  | +0xc8 | `LAB_005901f0` | a player leaves |
  | +0xcc | `FUN_0058fdd0` | a message was heard (C→S 0x31) |
  | +0xd4 | `LAB_00590690` | a player joins |
  | +0xec | `FUN_005905b0` | the NPC has a quest ("!") |

States: 1 not given, 2 given, 3 in the Den, 4 cleared, 5 rewarded.

- **Join** (`LAB_00590690`): skipped if bit 0 or bit 15 is set.
  - bit 4 → state 3, log 2
  - bit 3 → state 3, log 1
  - bit 2 → state 2, log 1
- **Akara's A1Q1InitAkara (string 64) heard:** state 2. `LAB_0058fc90`
  then sets each player's flag for the state:
  - state 2 → bit 2
  - state 3 → bit 3 if the log state is 1, else bit 4
- **Entering level 8:** state 1 or 2 becomes 3. The log goes to 2 and the
  flags are marked as above.
- **A monster dies** (`FUN_00590260`):
  - Remaining = the Den level's +0x2cc − +0x2d0. Those counters move with
    monster modes (`FUN_00547d90`..`FUN_00547ed0`).
  - 0 remaining → state 4 and the game flag. Participants (the list at
    +0x18, `FUN_005455f0`) get bits 13 and 1. Others get bit 14 and the
    0xd82 message.
  - Participants play unit event 0x23 (`FUN_00553380`). After 8 ticks
    (`FUN_00543f10`, `LAB_00590230`) the log goes to 5, "Return to Akara".
  - Under 6 remaining → log 4, "Monsters remaining: N".
- **A1Q1SuccessfulAkara (76) heard while bit 1 is set:** state 5. Bit 0
  is set, bit 1 cleared, quest 41 opened and bits 2..11 cleared.
  `FUN_006272b0(player, 5, 1)` gives +1 new skill point.

## Messages

`FUN_0058ff90` picks a block of messages:
- block 3 while the reward is due;
- nothing once done, or when the state is past 3 without bit 13;
- otherwise `0x736cd0[state]` = −1, 0, 1, 2, 3, 4.

Blocks are 0xc4 bytes: up to 16 `{u32 npc hcIdx, u32 string, u32 kind}`
entries, then a u32 count. `FUN_00543790` sends the NPC's entries to the
client (kind 1 becomes 0).

| block | messages |
|---|---|
| 0 | Akara 64 (kind 0) |
| 1 | Akara 65, Warriv 70, Gheed 69, Kashya 66, Charsi 67 |
| 2 | Kashya 72, Warriv 75, Charsi 73, Akara 71, Gheed 74 |
| 3 | Kashya 77, Warriv 80, Charsi 78, Akara 76 (kind 0), Gheed 79 |
| 4 | Warriv 80, Charsi 78, Gheed 79 (late joiners) |

The unmarked entries are kind 2.

**Client side:**
- On clicking the NPC, the first kind-0 message plays at once (`FUN_00661400`, then `FUN_004a10e0`).
- Kind-2 messages become Talk submenu entries (`FUN_00661390` / `FUN_006613c0`).
- Each entry is labelled with its quest's name from the `{u16 message, u16 name}` table at `0x722678`: messages 64..80 → 3714 "Den of Evil".

## Player voice events

Unit event e (`unit+0x6e`, `FUN_00553380`) plays a line from the class's
speech block. From its callers, 0x21..0x25 are Act 1's `complete_*` lines:
- 0x21 Andariel
- 0x22 the Burial Grounds (a1q2, `LAB_00590e30`)
- 0x23 the Den
- 0x24 the Tools of the Trade
- 0x25 the Tower

This is the order of `<class>_act1_complete_*` in Sounds.txt.

## d2d

`DenQuest` runs all of the above for one player:
- The World counts the Den's live monsters each tick and calls `killed` when the number drops.
- The talk event carries the NPC's messages.
- The client plays a greeting and lists kind-2 messages under Talk.
- `cmd::QuestMessage` (0x31) is checked against what that NPC has.

ponytail: not built yet:
- parties and late joiners;

## The Den lights up

Clearing the Den of Evil lights it, in this game only (the client's quest
states for the game come in S→C 0x02, 37 bytes, `FUN_004b92b0` → 0x7c0ea4;
a new game's Den is dark again).
- **The event:** S→C 0x2d `{0x2d, event}` (`FUN_004b9330` →
  `FUN_0046b630`) runs the client's quest event table 0x7129d8; event 0
  (`FUN_0046b0c0`) starts a count at 0x7129cc.
- **The flash** (`FUN_0046bd50`, as the Den's own light, `FUN_0046bdd0`):
  while the Den's quest is done in this game and the beams aren't up, its
  ambient is 80, or with the count running int(cos(count × 128 / 30) × 80)
  (the 512-step table), (255, 64, 48). The count steps a client frame
  (`FUN_0046beb0`); past 29 the beams come.
- **The beams** (`FUN_0046b0d0` → `FUN_0046af70`, and `FUN_0046be60` for
  a Den room brought up later): in each room, up to 25 tries at a random
  subtile of it whose collision lacks 5, three client missiles
  `denofevillight` (287: LightBeams, light radius 10, additive). Its client
  function 23 (`FUN_004d4b80`) tops its life back to 500 below 100: they
  stay. The Den's ambient goes back to its own (0).

d2d: `Town::den_tick` / `den_ambient` (the View's `den_cleared`, the
DenQuest state ≥ 4); the beams are client missiles drawn and lit like the
World's.

## The balloon over an NPC (npcalert)

`FUN_00544590` asks each of the NPC's act quests (+0xec; a1q1:
`FUN_005905b0`). When one says yes, the player is sent 0x80d and the NPC
shows Overlay.txt `npcalert`: NPCSpeechBalloon.dcc, 16 frames drawn about
100 px above the anchor, Xoffset −5, Yoffset −7, Trans 3.

d2d: `DenQuest::alert` → `UnitState::alert` (replicated) → `Unit::overlay`
(world.hpp, drawn additive).

## Akara's Reset Stat/Skill Points (quest 41)

- **The menu:** `0x4b6da0` patches Akara's menu record (0x726c48) to talk,
  trade, 0x2ba0 "Reset Stat/Skill Points". The menu builder (`0x4b4830`)
  shows the reset line while quest 41 bit 0 is clear and either bit 1 is
  set or the game is in Hell (`FUN_0044dcd0() == 2`).
- **Confirming:** choosing the line opens a confirmation (`0x4b5ad0`) with
  the reset name as heading, "ok" (0xd49) and "cancel" (0xd48). "ok"
  (`0x4b4290`) sends 0x38 to Akara.
- **The server (0x94 in the 0x38 handler):**
  - in Hell, if the Den of Evil is done and quest 41 bits 1 and 0 are both
    clear, it sets bits 13 and 1 (`FUN_0058fd20`);
  - then, while bit 1 is set, it resets skills (`FUN_00570360`) and stats
    (`FUN_00570c80`), and `FUN_0058fd50` sets bit 0 and clears bit 1.
- **Skills:** each skill's base points go back into stat 5 and its level
  becomes 0.
- **Stats:** each stat goes back to the class's CharStats base, with the
  difference moved to or from stat 4. Energy (`FUN_00570a80`) and
  vitality (`FUN_00570b60`) also change max mana, life and stamina by the
  per-point quarters. Current values rise with them, or are cut down to
  the new max.

d2d: `rules::respec`, `cmd::Respec` (0x38 kind 3), `open_respec_menu`.
Quest menus now open after the quest speech, as game.exe builds them
then.

## The quest log (QuestLog.cpp)

- **Art:** `FUN_004a23d0` loads questbackground (4 pieces), questtabs /
  expquesttabs, questsockets, questdone, invps and questlast.
  `FUN_004a3220` loads each quest's icon by the name list at 0x6da2c8
  (a1q1 ..).
- **The table:** 0x723f30 has 16 bytes a quest: shown, icon, slot, act,
  the log record, the quest's flag number. The acts' icon ranges are at
  0x723f08.
- **Drawing (`FUN_004a34f0`):**
  - the background at the panel;
  - the tabs at x 5, 0x43, 0x81, 0xbf, 0xfd (the expansion's), frame
    2 × act when selected, else + 1;
  - each icon at its slot (0x723ea8: (26 | 123 | 220, 121 | 218),
    bottom-left), with a socket under it (frame 1 when selected);
  - the selected quest's name at y 248, then its lines from y 270,
    20 apart, wrapped to 270 px.
- **Icon frames:** 26 not started, 0 under way, 1–24 the completion
  animation (100 ms a frame), 24 done, 25 selected.
- **The text:** `FUN_004a1950` picks the line from the flags and the log
  state the server sends. Each record is the name, a message to replay,
  then {string, message} per log state.

- **The Den's record** (0x7237a4, the table's +4): name 3714, the message
  to replay 76, then {string, message} a log state, state s at [2s + 1]:
  1 3735, 2 3736, 3 3737 (empty), 4 3738 "Monsters remaining: " + the
  count (3739 "One monster left." for one), 5 3740; 11 3728 (done in a
  previous game), 12 3727 (another player's), 13 3726 (done).
- **The done animation:** a finished quest whose bit 12 is clear plays
  frames 0..24, one a 100 ms (`GetTickCount`), cursor_questdone (Sounds.txt
  14) at frame 1; then bit 12 is set in the client's copy of the flags
  (0x4a3943), which the server doesn't save: once a game.
- **Buttons** (bottoms 58 above the screen's, panel y + 422): close, the
  store buttons' frames 10 / 11 at x 0x116 (hit 0x24 x 0x22); questlast at
  0xe2 (hit from 0xe6, 0x1e x 0x21), which plays the quest's message again.

d2d: `draw_quest_log` / `quest_text` / `quest_button_at` in panels.hpp,
opened with Q; the View carries the log states, the game's flags and the
Den's count. All six Act 1 records are in (quests-act1.md).

### The panel in full (FUN_004a34f0, FUN_004a3e40, FUN_004a42a0)

- **Tabs:** an act's tab is drawn only once the act before it is over
  (quests 7, 15, 23, 26 bit 0; the fifth needs the expansion): the open
  tab frame 2 × act, the others 2 × act + 1, the rest none. The classic
  game loads `questtabs` (four, x 5, 0x52, 0x9f, 0xec, bottom 32), the
  expansion `expquesttabs` (five, bottom 33). A press in the top 0x1c rows
  (FUN_004a26f0, 0x40 a tab, 0x50 classic) switches tab: the old tab's
  pending done animations count as seen (FUN_004a2760 sets bit 12), the
  act falls back to the last open one (FUN_004a2220), the pick is made
  again (FUN_004a3220(act, 1)).
- **Icons** by the picker's +0x266: 2 frame 26; 3 frame 0, 25 while held
  (0x7bf2b5, the icon the mouse went down on); 1 frame 24, or while held
  `questdone` frame = the icon's index a row up (0x724220 = −1); 0 the done
  animation. The socket under it: frame 1 and a row up (0x72421c = −1)
  when picked (0x7bf2b9).
- **Text:** font 8 (FontFormal11). The name centred at x 0xa0, bottom 248;
  the lines left-aligned at x 16 (0x724214), from y 270 (0x724210), 20
  apart, wrapped to 270 (FUN_00502970). Not drawn while a questlast replay
  plays (0x7bf2b3).
- **Clicks:** an icon is held only when it has text (+0x264), with
  cursor_button_click (Sounds.txt 4); let go on the same icon and it is
  picked and remembered for the tab (0x7bf2bd). Hit boxes 0x38 × 0x34 at
  0x723eb0 (the slot table's second pair). Close: hit x 0x116..0x139, y
  0x188..0x1a9 (FUN_004a2690); questlast: 0xe6..0x103 by 0x187..0x1a7
  (FUN_004a2610). Hover texts (font 9, FUN_00502280, bottom 0x5f above the
  panel's): 4144 "Close" centred at 0x128, 3720 "Speech" at 0xf1.
- **Opening** (FUN_004a3fe0 → FUN_004a2300, FUN_004a3220(act, 0)): the
  player's act's tab (as far as open), the animation counters reset. The
  pick: the Quest Log button's quest for the tab (0x7bf280); else, in the
  table's order, the first quest whose state changed since it was last
  read (+0x263: the picker keeps each quest's state at 0x7bf380) or whose
  done animation is to play; else the tab's remembered pick; else the
  first under way (shown 3).
- **Shutting** (UI 0xf off, FUN_00455ae0 → FUN_004a28d0): the tab's pending
  done animations count as seen.

### The Quest Log button (UI 0x11)

S→C 0x5d (6 bytes: quest, flags, log state, s16; `FUN_0045e540` →
`FUN_004a2cb0`) with no flags is a quest's log state: with the log shut it
turns on UI 0x11 and keeps the quest's slot for its tab (`FUN_004a2c30`,
0x7bf298 = 1); with it open the quest is picked if it's in the tab. The
server sends it from `FUN_00544190` (each log change sent per player) and
`FUN_00545920` (log 12, "another player"). Flags 2 are sounds: event 4
(Akara's ring, `FUN_005458e0(player, 4)`) plays item_ring (241).

`FUN_004a2a80` draws it like New Stats (levelsocket, level, the label
3928 "Quest Log" centred over it). Its row (0x7241c0, 20 bytes x0, x1,
y0, y1, label bottom, filled for 640 × 480 at 0x6cac30, drawn + H − 480;
`FUN_004a2900` picks): x 40 (W/2 + 40 with only the left panel open), y
H−195..H−160 (label H−198), or H−140..H−105 (H−143) with the left panel
and the character panel open. Not drawn with both panels open or the log
up. Pressed over it (`FUN_004a2a20`): held, Sounds.txt 4; let go over it
(`FUN_004a4110`): UI 0x11 off and the log opens on its quest. Opening the
log with Q doesn't turn it off.

d2d: `quest_log_open` / `_close` / `_tab` / `_notify`, `quest_log_button`
(panels.hpp), `rules::quest_log_pick` / `quest_log_act`.
ponytail: a change in the View's log state stands in for each 0x5d (so a
log change the server keeps to itself, Burial's 13 at the reward, also
brings the button); the player's act is taken as Act 1's; the description
stays up while questlast replays.
