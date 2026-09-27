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
- the quest log, and the timing of its "Return to Akara" state;
- parties and late joiners;
- the "!" marker;
- the Den's lighting change;
- Akara's respec menu entry (quest 41).
