# NPC talk and speech — game.exe 1.14d

"talk" in an NPC menu (handler `0x4b6c70`) opens the talk submenu. Its
topics then play the NPC's speech in a scrolling text box.

## Talk table

`0x726850` holds 46 (`DAT_0072554c`) records of 22 bytes:

    u32 hcIdx | u8 act | u32 topics* | u32 count | u32 gossip (runtime) |
    u8 talked (runtime) | u8 | u8 | u8 no_intro | u8

Each topic is 15 bytes:

    u16 string.tbl id | u8 quest_gated | u32 quest_state | u32 quest | u32 class (7 = any)

`FUN_004b1d70` finds the NPC's record. Dumped into `components/game/npc/npc_talk.hpp`.

Akara's 13 topics are string IDs 11..23: `AkaraIntroGossip1`,
`AkaraIntroSorGossip1`, `AkaraGossip1..11`.

## Talk submenu (`FUN_004b5890`)

Same menu machinery and layout as the NPC menu (npc-menu.md). Lines:
- header: "talk" (0xd35), gold;
- "introduction" (0xd47), only when `no_intro` is 0;
- "gossip" (0xd43);
- quest topics (`FUN_0049f910`);
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

`FUN_004a0000` (12 bytes, `004a0000`) tells whether that record is up.
It has no arguments and no assert. It clears EAX, compares the dword at
`0x7bf0a4`, sets AL when the dword is not 0, and returns:

`xor eax, eax`; `cmp dword [0x7bf0a4], eax`; `setnz al`; `ret`.

EAX is 1 or 0. With the dword 0, then `0x12345678`, `tools/emu` returned
0 then 1 (`uv run python` from `tools/emu`, `Emu(exe=...).call(0x4a0000)`).

`FUN_004a10e0` stores `FUN_004a05e0` at `004a1261` (call `004a125a`).
`FUN_004a1320` stores it at `004a142e` (call `004a1427`). Both calls pass
EDX = 2, so the builder takes the 0x145 by 0x70 record. EDX 3 returns
before that. `FUN_0049f960` (`0049f98f`), `FUN_004a0680` (`004a06bd`) and
`FUN_004a0770` (`004a07e7`) write 0 after releasing it.
`game-srcfiles.tsv` files `FUN_004a05e0` and `FUN_004a10e0` as
`UI/text.cpp` (the assert string `.\UI\text.cpp` is at `006da13c`).

Nine calls. The hover-name function `FUN_00454f30` calls it at
`004551dd`. At `004551d0` it calls `FUN_004b3230` and jumps to
`00455489` when that returns nonzero. It then calls `FUN_004a0000` and
jumps to the same address when EAX is 1. Otherwise ECX is ESI and it
calls `FUN_00464a60` (the unit name). d2d cites `FUN_00454f30` for the
box over the feet (`apps/d2d/world_view.cpp`). The other calls are
`004539df` (`FUN_00453910`: while EAX is nonzero, EDI of 6, 7, 10, 0x11
or 0x13 falls through, and any other EDI returns 0), `00454ed1`
(`FUN_00454ad0`), `00456860` (`FUN_004567f0`), `00456acc` and `00456bd3`
(`FUN_00456970`), `0045717f` (`FUN_00456ee0`), `00466df9`
(`FUN_00466de0`) and `00467149` (`FUN_00467130`).

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

## UI state functions run in the emulator

`tools/emu/ui_state.py` runs each of these for real with every callee hooked
to log its registers and stack arguments, so the call order and the globals
written are the claim. The sequences come from the disassembly, because the
decompile drops register arguments and, once, invents one.

- `FUN_0049f900` returns `[0x7bf250]` (the gossip list); `FUN_004a0000`
  returns `[0x7bf0a4] != 0`.
- `FUN_0049f8c0` (the 0x76 packet handler; ECX = the packet): the decompile
  shows no arguments, the disassembly loads `EDX = byte [ECX + 1]` (unit
  type) and `ECX = dword [ECX + 2]` (unit id) for `FUN_00463990`, the unit
  lookup. A found unit has `+0xa4` released through `FUN_006611a0(0, +0xa4)`
  when set, and zeroed either way; no unit does nothing more.
- `FUN_0049f960` (speech close), in order: `FUN_00467430(0)`; `[0x7bf0a4]`
  set: `FUN_0049d370` (EAX = its first dword), `FUN_0040b3c0` of it (line
  0x68e), cleared; `[0x7bf260]` set: `FUN_004b9610` (ECX = it) and, when that
  returns nonzero, `FUN_004b9ef0(0, 4)`, the dword cleared; `[0x7bf24b]`
  cleared; `[0x7bf27c] == 1` (exactly 1; 2 is left alone): cleared and
  `FUN_00451e10(ECX = [0x7c8cbc], EDX = 0x722600, 7)`; `[0x7bf230]` set:
  `FUN_004786a0(CL = 0x31, EDX = -1, word [0x7bf212])`, cleared.
  **`FUN_004f59a0` is not called with 7**: it is `mov eax, [0x7c8cbc]; ret`
  and the 7 pushed before it is `FUN_00451e10`'s stack argument (`ret 4`), so
  the decompile's `FUN_004f59a0(7)` is a misreading. The same pair sits in
  `FUN_004a0770`.
- `FUN_004a0680` (reset the speech state): frees the gossip list
  (`FUN_006612a0`), the speech record `[0x7bf0a4]` and the second record
  `[0x7bf1a8]` (the latter also frees a second block, line 0x6e4), calls
  `FUN_004b8020` for `[0x7bf1b0]`, the box object's destructor (vtable slot
  0, argument 1) for `[0x7bf1b4]`, then `FUN_0049fe10`, `FUN_00467430(0)` and
  `FUN_0049d4d0` on `0x7bf1e4`, `0x7bf1ea`, `0x7bf1f0`; each global is zeroed
  and `[0x7bf24b]` too.
- `FUN_004a0770` (EAX = a record, one stack argument; the per-frame step):
  a nonzero argument writes `2` to `[EAX + 0x16]` and returns 0. Otherwise
  `FUN_00514cc0` (the master volume) and, when `[0x7bf24b]` is set,
  `FUN_004b95c0`: that flag with `FUN_004b95c0` zero and the volume nonzero
  returns 1 at once. Else `[0x7bf24b] = 0`, and `FUN_0049d5a0` (the box's
  draw) returning 0 returns 1. When it returns nonzero: `[0x7bf1bc] = 1`; a
  record in `[0x7bf0a4]` with `[0x7bf20a]` clear is freed through
  `FUN_0049d410`; `FUN_0049f960`; a callback at `[0x7bf258]` takes
  (ECX = `[0x7bf25c]`, EDX = 0, 0) and its answer is returned; with none,
  `[0x7bf1c4] == 1` is cleared, else `[0x7bf20a]` set calls
  `FUN_004b3d10(ECX = [0x7bf202])`; then `FUN_00453ae0`, `[0x7bf20a] = 0`,
  `FUN_00467430(0)`, the `[0x7bf27c]` pair above, and returns 0.
- `FUN_00453d90` calls, in order, `FUN_0049c0a0`, `4a7aa0`, `4aa920`,
  `4a8a30`, `4b8720`, `487990`, `498e80`, `48a600`, `4a3050`, `4939b0`,
  `457640`, `47e720`: all with no arguments. `FUN_00453de0` calls `49c0f0`,
  `4a7ad0`, `4aa980`, `4a8c00`, `4a3020`, `4b32f0`, `4b91f0`, `4a0010`,
  `492f90`, then `FUN_00478a00` for each of `[0x7a2794]`, `[0x7a2894]`,
  `[0x7a2898]` that is set (zeroing it), then `47e750` and `468070`.
- `FUN_00453ee0`: `[0x7a27b4]` set: `FUN_0046aa20(ECX = 1, EDX = 1)`,
  cleared; `[0x7a27a8]` set: its vtable slot 0x2c result goes to
  `[0x7a2a68]` (before anything is freed); `[0x7a27a0]` set:
  `FUN_004b8020`, cleared; then the objects at `[0x7a27a4]`, `[0x7a27a8]`,
  `[0x7a27ac]`, `[0x7a27b0]` get their destructor (slot 0, argument 1) in
  that order and are zeroed; last `[0x7a279c]`: 0 tail-calls `FUN_004a7a80`,
  1 to 4 `FUN_00489880`, anything else returns.

What these functions' callees do was not traced here; the oracle hooks them.
Re-check: `cd tools/emu && uv run python ui_state.py`.

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
- A line that meets the box is drawn for the glyph rows inside it
  (`speech_line_visible`, `FUN_00501df0`). A line clear of the box is not drawn.
- Voice: when a speech starts, `FUN_004a10e0` looks up its string with
  `FUN_004e0650`. That walks `{u32 sound, u32 string}` pairs at
  `0x72b0e0` (864, zero-terminated; `apps/d2d/speech_sound.hpp`) and plays
  the Sounds.txt sound it finds. Example: AkaraIntroGossip1 (11) → 3499
  `akara_act1_intro`, `act1\akara\aka_act1_intro.wav`. Speech WAVs are in
  `data\local\sfx\` (d2speech / d2xtalk); effects are in
  `data\global\sfx\`. d2d stops the voice when the box closes.
- A click or Esc skips the speech.
