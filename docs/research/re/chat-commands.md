# The chat box's commands — game.exe 1.14d

The in-game chat box's keys and its 8-line history, and what it does with
a line when Enter is pressed: the commands the client keeps for itself (`/players`, `/nopickup`, `/fps`,
`/framerate`, `soundchaosdebug`) and what goes out as chat. Each section
names the oracle that runs game.exe's own code on it (`tools/emu`). The
box's IME side is in chat.md; the chat lines and the C→S 0x14 / 0x15 and
S→C 0x26 packets are in net-packets.md.

## The box's keys — FUN_0047ca70, FUN_0047bca0, FUN_0047bc70

`FUN_0047b090` (chat.md) registers four handlers from the table at
0x712f4c (12-byte rows: kind, message, function), and `FUN_0047b2e0`
removes them:

| Kind | Message | Function |
|---|---|---|
| 0 | 0x102 `WM_CHAR` | `FUN_0047ca70` |
| 3 | key 0x26 (Up) | `FUN_0047bca0` |
| 3 | key 0x28 (Down) | `FUN_0047bca0` |
| 0 | 0x201 `WM_LBUTTONDOWN` | `FUN_0047bc70` |

A message has wParam at +8, lParam at +0xc, and "handled" and the result at
+0x18 and +0x1c. The box's text is UTF-16 at 0x7bb638 (0x202 bytes), its
count at 0x7bb5e8. It takes characters while 0x7bb5ec is 1 or more:
`FUN_0047bdc0`, which `FUN_00455f20` calls when it turns UI 5 on, empties the
box and clears that word; `FUN_0047b720` and `FUN_0047ad10` also write it
(not followed).

**`FUN_0047ca70`** (`ret 4`) does nothing at all while 0x7bb5ec < 1.
Otherwise, by wParam:

- **8 (Backspace)**: when the count is above 0, it drops one and zeroes that
  unit.
- **0x1b (Esc)**: shuts the box (`FUN_00455f20(5, 1, 0)`).
- **0xd (Enter)**: counts the leading units under 0x100 that `isspace` calls
  blank (the C locale's 9..13 and 32; 0xa0 and U+3000 are not). If nothing
  is left the box shuts. Otherwise the rest goes into the history
  (`FUN_0047ba00`, next section), the whole box is converted with
  `WideCharToMultiByte(CP_ACP, 0x250, box, -1, local, 0x100, NULL, NULL)`
  (0x250: `WC_COMPOSITECHECK | WC_DEFAULTCHAR | WC_DISCARDNS`), and the parser
  runs on it with the count of blanks, ECX the box.
- **Any other character** (read, not run: the font calls): it's added when
  the IME is open (0x7bb5e1) or `FUN_00502550` takes it, while the count and
  the length are both under 0xff and the text wrapped to W - 0x109
  (`FUN_00502970`) is 3 lines at most, a third line no wider than W - 0x113.
  The unit is written at the end (0x47cb61) and the text is wrapped again;
  the count grows only when that's under 3 lines or the third line is
  narrower than W - 0x113. What the box holds when the count doesn't grow
  wasn't followed.

Every path but the first then sets handled 1, result 0 and calls
`FUN_00420290(message)`.

**`FUN_0047bca0`** (`ret 4`) sets handled 1 and result 0 and calls
`FUN_00420290`. A repeat (lParam bit 30) does nothing more. Up shows the
history slot at 0x7bb5d8 and then steps +1 (older); Down steps -1 and then
shows.

**`FUN_0047bc70`** (`ret 4`): a click lower than H - 0x31 (y, lParam's high
word, > 0x711470 - 0x31, signed) shuts the box. It leaves the message
unhandled.

## The history — FUN_0047ba00, FUN_0047ba80, FUN_0047b9c0, FUN_0047bd30

Eight slots of 0x202 bytes at 0x7bb840, UTF-16 like the box.

| Address | What |
|---|---|
| 0x712f44 | the slot written last (starts at 7) |
| 0x712f48 | the slot written next once all 8 are full (starts at 7) |
| 0x7bb5d8 | the slot Up shows next |
| 0x7bb5e4 | u16: how many slots are full |

- **`FUN_0047ba00`** (EDX text): while fewer than 8 are full it writes slot
  7, then 6, ... 0 (0x712f44 steps down from the second line on); once all 8
  are, it writes 0x712f48's slot and steps that down, 0 to 7. It points
  0x7bb5d8 at the slot written and copies 0x202 bytes from where the text
  starts.
- **`FUN_0047ba80(step)`** (`ret 4`) moves 0x7bb5d8 by step, 8 wrapping to
  0 and -1 to 7, but keeps it where it was when fewer than 7 slots are full
  and the new slot isn't one of them (8 - slot > full). With exactly 7 full
  that test is skipped: the step from slot 7 goes to slot 0, which nothing
  has written (bugs.md 118).
- **`FUN_0047b9c0`** copies 0x7bb5d8's slot into the box and sets the count
  to its length (`FUN_00526800`).
- **`FUN_0047bd30`** sets 0x712f44 = 0x712f48 = 7 and 0x7bb5d8 = full = the
  count = 0; the slots keep their text. Its caller is `FUN_0044e200` (not
  followed).

What Up shows from a fresh history:

| Lines sent | Up, Up, ... |
|---|---|
| 3 | line 3, line 2, line 1, line 1, line 1 |
| 7 | line 7, ... line 1, (empty), line 7 |
| 8 | line 8, ... line 1, line 8, line 7 |

Oracle: `tools/emu/chat_history.py` drives the three handlers through
20,000 random steps (Enter on random texts, Backspace, Esc, Up, Down,
repeats, clicks, resets) and compares the box, its count, the four
globals, the eight slots and the calls after each step; then the scripted
case above. Hooked: the parser (its ECX and arguments checked), the box
toggle, `FUN_00420290` and `WideCharToMultiByte` (its arguments checked);
`isspace` and `FUN_00526800` run for real. The character path isn't run.

## The parser — FUN_0047c420

`FUN_0047c420(line, blanks)` (1607 bytes, `ret 8`, ECX the box's UTF-16
text at 0x7bb638) has one caller, the box's Enter (0x47cc4d). `line` is
the box converted to the ANSI code page, and the parser works on
`line + blanks`, past the leading blanks. It tests the line in this
order (strings at 0x6d7330..0x6d73a4):

| Line | Test | What it does |
|---|---|---|
| `/fps` | `_stricmp`, the whole line | `FUN_00477c60`, then stops |
| `/framerate` | `_stricmp` | `FUN_00477c00`, then stops |
| `/players…` | `_strnicmp(line, "/players", 8)`, and game type 0, 1, 6 or 8 | sets the player count (next section); the line becomes `players set to N` and goes on down the list |
| `/nopickup` | `_stricmp` | flips 0x7a6a90, then stops |
| `soundchaosdebug` | `_stricmp` (no slash) | `FUN_004babc0`: flips 0x7c5468, and calls `FUN_004baa50` when it goes back to 0; then stops |
| anything else | | chat, below |

Every path ends with `FUN_00455f20(5, 1, 0)`, which shuts the box (UI 5).
The whole-line tests take no trailing blank: `/fps ` is chat.

- `/fps` (`FUN_00477c60`) and `/framerate` (`FUN_00477c00`, video-drivers.md
  `+0x4c`) switch a frame-rate overlay: `/framerate` flips 0x7bb3a4 and
  clears 0x7bb3ac; `/fps` calls it unless the overlay is already on with
  0x7bb3ac clear, then sets 0x7bb3ac. Read, not run: the oracle hooks both.
- `/nopickup`'s flag is read only by `FUN_00467ac0` (cursor-units.md, the
  unit under the cursor): with it set, units of type 4 (items) are passed
  over (0x467b16, 0x467c46), so the cursor doesn't take an item on the
  ground. Read, not run.

**Chat.** The packet starts as C→S 0x15, type 1, the language byte at
0x7bb5dc. `FUN_0047bad0` (ported in apps/d2d/town/update.cpp) matches the
line's start against its table at 0x712f7c: three 14-byte rows of a
string pointer, a u16 type, a u32 "a name follows" and a u32 "in game
type 0 too".

| Prefix | Type | Name follows | In game type 0 |
|---|---|---|---|
| `/` | 3 | no | no |
| `!` | 0 | no | yes |
| `@` | 2 | yes | no |

Type 0 (`!text`) makes the packet 0x14 (overhead) with type 0 and the text
after the `!`. Then every line is tested against the ignore list's commands
(`/ignore `, `/unignore `, `/squelch `, `/unsquelch `, `/filtermsg `,
`/unfiltermsg `, `/ignorepersist`, `/d2notify`; chat-ignorelist.md), whose
result goes to the chat lines (`FUN_0049e3a0`, kind 3). A type 3 line is
then a Battle.net command: `/msg` `/m` `/whisper` `/w` (`FUN_0047c1f0`),
`/help` `/?` (`FUN_0047c100`), and last the Battle.net object's slot +0x14
(`FUN_0044b7b0`). It never goes out as chat. Any other line whose box text
isn't empty (`FUN_00526800`) is copied (255 bytes at most) and sent by
`FUN_004787b0` (net-packets.md "Chat's framing").

So in game type 0 a line starting with `/` that isn't one of the commands
above is plain chat; in any other game type it is never sent.

Oracle: `tools/emu/chat_parse.py` runs the parser on 35 lines, in game
types 0..9, with 0 and 2 leading blanks (700 runs), and the two toggles
from on. Hooked: the box toggle, the two overlay calls, `FUN_004baa50`,
the game type, `FUN_004787b0` (the packet is read back), the ignore list
getter `FUN_00611560` (the run stops there), `FUN_0047c1f0`, `FUN_0047c100`,
`FUN_0044b7b0` and `FUN_0049e3a0`. The CRT, `FUN_0047bad0`, `FUN_0047b3b0`,
`FUN_004babc0` and `FUN_00535780` run for real. The ignore commands' own
work isn't run.

## `/players` — FUN_00535780, FUN_00535790

- **The value** is `atol(line + 8)`, MSVC's: leading blanks, a sign, the
  digits, and a value past 32 bits held at 2147483647 or -2147483648. A value
  of 1 or less gives 1 (a bare `/players`, `/players 0`, `/players -5`,
  `/players abc`); 8 or more gives 8. Only 8 characters are matched, so
  `/players8` and `/PLAYERS 3` work too, and `/playersX` sets 1.
- **The game types** (`FUN_0044db30`, net-join.md "Game types"): 0 single
  player, 8 the TCP host, 6 the other open-game host, and 1, which no traced
  code sets. Those are the clients that run the server in their own process.
  A TCP joiner (9) or a Battle.net game takes the line as a Battle.net
  command (above): nothing is set and nothing is said.
- **`FUN_00535780`** (12 bytes, fastcall ECX n) stores n in `DAT_00883d70`
  when n < 9, unsigned. The parser is its only caller and 0x535785 the
  global's only write, so nothing resets it: a later game in the same
  process keeps the value (not checked in play). It starts at 0.
- **`players set to N`** then goes out as an ordinary chat line (0x15); the
  server's 0x26 comes back to the sender (net-packets.md), which is the line
  the player sees.
- **`FUN_00535790`** (game; the server's count) gives the players
  `FUN_005538d0` finds in the game's player hash (callback 0x535760; party.md
  "The player count"), or `DAT_00883d70` when game +0x6a is 1..3 and the
  value is larger (a signed compare, 0x5357c0). Monster life and
  experience (`FUN_00573930`) and NoDrop (`FUN_0055a6d0`) read it (party.md).

Oracle: the same script runs `FUN_00535780` on 24 values (0..20 and the
32-bit edges) and `FUN_00535790` 15,360 times: game +0x6a 0..255, six
settings, 0..9 players, with `FUN_005538d0` hooked to give the count.

## The string helpers — FUN_0047b3b0, FUN_0047c3f0, FUN_0047bd60, FUN_0047bd80, FUN_0047bd00

- **`FUN_0047b3b0`** (EAX prefix, stack line, `ret 4`; 151 bytes) asks
  whether the line starts with the prefix, in any case. It gives -1 when the
  line is shorter than the prefix. Otherwise the prefix's bytes are compared
  with the line's through `toupper`, each byte sign-extended first (MOVSX,
  so 0x80..0xff stay negative and unchanged): 0 when all match, else 1 when
  the line's byte is the larger and -1 when it's the smaller. Its callers
  are the parser's ignore commands, `FUN_0047c1f0` and `FUN_0047c100`.
- **`FUN_0047c3f0`** (EAX line, ESI out, stack size, `ret 4`) copies what
  follows the first space with `FUN_004135d0(out, after, size)` (at most
  size - 1 bytes, then a 0); without a space, out is "". The ignore commands
  take their name with it (sizes 0x11 and 0x104).
- **`FUN_0047bd60`** (ESI name) and **`FUN_0047bd80`** (ESI name, EBX out)
  split a Battle.net `character*account` name for the ignore command
  (`FUN_0047bdf0`, chat-ignorelist.md). `FUN_0047bd60` returns the text after
  the first `*` when that isn't empty, else the name. `FUN_0047bd80` copies
  the text before the first `*` (memcpy, then a 0), or the whole name when
  it starts with `*` or has none (`FUN_004135d0` with 0x7fffffff); it
  doesn't bound the copy.
- **`FUN_0047bd00`** (ECX UTF-16 text, returns AL) is meant to say whether
  every unit is under 0x100. The parser asks it when the IME is open
  (0x7bb5e1), before `FUN_0047bad0` and the Battle.net commands. The load of
  `text[length]` sits before the loop (0x47bd0e; the loop goes back to
  0x47bd12), so every pass tests that one unit, the terminating 0, and it
  returns 1 for every text (bugs.md 117).

Oracle: `tools/emu/chat_helpers.py` runs the first four on 4,000 generated
strings each (the command prefixes, mixed case, bytes 0x80..0xff, sizes 1
to 0x104, names with and without `*`) and `FUN_0047bd00` on 4,000 UTF-16
texts, 3,681 of them holding a unit over 0xff: it gives 1 for all. The CRT
and Storm's string helpers run for real.

## d2d

No `/players`: the box's Enter (apps/d2d/town/update.cpp) parses only
`@name`, and spawns get `players.size()` (world/objects.cpp, the quests) or
`players_in(foes)` (ai.cpp), with no setting. todo.md row 52. `/nopickup`,
`/fps`, `/framerate`, `soundchaosdebug` and `!` (the 0x14 overhead line)
aren't ported either. The box has no history (Up and Down do nothing while
typing), and its Enter sends the line as typed: leading blanks go out, and a
line of blanks is sent where game.exe shuts the box.

## Re-check

```sh
cd tools/emu
uv run python chat_parse.py
uv run python chat_helpers.py
uv run python chat_history.py
```

and `python3 tools/ghidra/fn.py 0047c420 00535780 00535790 0047b3b0
0047c3f0 0047bd60 0047bd80 0047bd00 0047ca70 0047bca0 0047bc70 0047ba00
0047ba80 0047b9c0 0047bd30`, the strings at 0x6d7330..0x6d73a4, and the
tables at 0x712f4c (12-byte rows) and 0x712f7c (14-byte rows).
