# NPC talk and speech — game.exe 1.14d

"talk" in an NPC menu (handler `0x4b6c70`) opens the talk submenu. Its
topics then play the NPC's speech in a scrolling text box.

## Talk table

`0x726850` holds 46 (`DAT_0072554c`) records of 22 bytes:

    u32 hcIdx | u8 act | u32 topics* | u32 count | u32 gossip (runtime) |
    u8 talked (runtime) | u8 | u8 | u8 no_intro | u8

Each topic is 15 bytes:

    u16 string.tbl id | u8 quest_gated | u32 quest_state | u32 quest | u32 class (7 = any)

`FUN_004b1d70` finds the NPC's record. Dumped into `apps/d2d/npc_talk.hpp`.

Akara's 13 topics are string IDs 11..23: `AkaraIntroGossip1`,
`AkaraIntroSorGossip1`, `AkaraGossip1..11`.

## Talk submenu (`FUN_004b5890`)

Same menu machinery and layout as the NPC menu (npc-menu.md). Lines:
- header: "talk" (0xd35), gold;
- "introduction" (0xd47), only when `no_intro` is 0;
- "gossip" (0xd43);
- quest topics (`FUN_0049f900`);
- "about the merchants" (0xd40), Greiz only;
- "Horadric Cube" (0x8b7), Cain when carrying one;
- "cancel" (0xd48).

## Choosing the topic

- **introduction** (`0x4b41e0`): topic 1 when its class is the player's,
  else topic 0. Three expansion NPCs (hcIdx 0x201–0x203) are special-cased.
- **gossip** (`0x4b41c0`): the record's current gossip. `FUN_004b1680`
  picks it once per game (`FUN_004b17a0` runs when talking starts, if not
  done yet): a random topic with index ≥ 2 whose class is 7 or the
  player's and, when `quest_gated`, whose quest state matches. Up to 10
  tries, else topic 2.

## Speech box

Built by `FUN_004a10e0` / `FUN_004a05e0` and parsed by `FUN_004a0320`;
drawn at `0x49d5a0`.

- **Text**: the string's line 0 is the scroll rate (`atol`; 8 with a
  warning if it isn't numeric). The remaining lines are pre-wrapped at
  `\n`.
- **Box**: `x = (W - 0x145)/2`, `top = 12` (`DAT_007225fc`). A half-dark
  rect of 325 × 122 at `(x, top - 5)`, via `FUN_0046efd0` with draw mode 1.
- **Font**: 8 = FontFormal11. The game.exe font table order is Font8,
  Font16, Font30, Font42, FontFormal10, FontFormal12, Font6, Font24,
  FontFormal11, …
- **Scrolling**: `offset` (1/1024 px) = `(GetTickCount delta / 4) * rate`.
  Line i's baseline is `top + 0x70 - offset_px + 18 * i`. Lines touching
  the top 18 px or the bottom are drawn partially (`FUN_00501df0`).
- **End**: the speech ends once `offset_px > max(lines - 1, 1) * 18 + 0x70`.

## d2d

- Topics, submenu and scrolling are as above.
- Quest-gated topics test the save's quest flags for the difficulty it
  was last played on. The d2s block at 0x14F is "Woo!", u32 6, u16 298,
  then 3 × 96 bytes: 16 bits per quest, bit 0 = done. That is the
  game's own layout: `FUN_0065c310(flags, quest, bit)` tests bit
  `quest*16 + bit` through `FUN_00410b30` (`byte[n>>3] & mask[n&7]`).
- The submenu's quest topics are *not* shown. They come from
  `DAT_007bf250`, a list of message IDs the server sends (entries of
  type 2, `FUN_00661390` / `FUN_006613c0`), so the game-server quest
  scripts decide them.
  d2d builds them for the Den of Evil (quests.md): its kind-2 messages
  are topics named "Den of Evil", and a kind-0 one plays on the click.
- Lines are clipped to the box rather than revealed partially.
- Voice: when a speech starts, `FUN_004a10e0` looks up its string with
  `FUN_004e0650`. That walks `{u32 sound, u32 string}` pairs at
  `0x72b0e0` (864, zero-terminated; `apps/d2d/speech_sound.hpp`) and plays
  the Sounds.txt sound it finds. Example: AkaraIntroGossip1 (11) → 3499
  `akara_act1_intro`, `act1\akara\aka_act1_intro.wav`. Speech WAVs are in
  `data\local\sfx\` (d2speech / d2xtalk); effects are in
  `data\global\sfx\`. d2d stops the voice when the box closes.
- A click or Esc skips the speech.
