# Src/ChatDlg.cpp: the Battle.net chat screen's helpers

Traced from game.exe 1.14d: each function's disassembly and decompile,
callers from `game-calls.tsv` and a byte scan of game.exe for rel32 calls
and raw pointers. Not run in `tools/emu`. The functions use the assert
path `.\Src\ChatDlg.cpp` (`0x6d5460`). d2d has no Battle.net screens, so
none of this has a counterpart in d2d. frontier-ring.md already has
`chatdlg_close_all` (`FUN_0043f8c0`) and `chatdlg_reopen` (`FUN_0043fb50`).
The functions from `FUN_004411e0` to `FUN_00446970` name no file of their
own: they sit inside ChatDlg.cpp's span (`0x43e250`..`0x446b30`), and
`FUN_004429a0`'s fatal line 0x12c2 falls among the file's other lines.

## The pieces

**Controls.** `FUN_0043e9d0` makes control EAX from the table at
`0x70ce50` (0x30 bytes per entry, `FUN_004f93c0`) and pushes it on the
screen's stack at `0x77ada8` (count `DAT_0077bbe8`, at most 0x20).
`FUN_0043ea20` frees the whole stack. The message boxes are built from
these entries:

| Entry | Type | What it is |
|---|---|---|
| 0x21 | 2 | the box of the one-line message, at (0x10c, 0x15e), 0x108 × 0xb0 |
| 0x22, 0x23 | 6 | its button: string 0x13ee (0x22) or 0x13ef (0x23), handler `FUN_0043eb10` |
| 0x24 | 4 | its text, at (0x10c, 0x140), 0x108 × 0x78 |
| 0x25 | 2 | the box of the two-text message, at (0x9e, 0x1cc), 0x1e5 × 0x122 |
| 0x27 | 6 | its button: string 0x13ee, handler `0x4428b0` |
| 0x28, 0x29 | 4 | its two texts |

`FUN_0043eb10`, the button handler, calls the callback in `DAT_0077bda4`.
`FUN_00440140` and `FUN_004401c0` set that callback. When they're given
none, they use `0x43eb00`, which only closes the box (`FUN_0043ea20`).

**The text queue.** Text that a Battle.net callback hands the screen
goes on a queue locked by the critical section `0x77ae28` (head
`DAT_0077bdac`, tail `DAT_0077bdb0`; 0x10-byte nodes: text copy, colour,
clear flag, next). `FUN_00441500` empties the queue (`FUN_0043e3a0`,
`0x4418e8`) into the text box `DAT_0077bd8c`. The two callbacks the chat
screen passes to the Battle.net interface's slot `+0xa8` (`0x441f9f`) use
it: `FUN_0043e4c0` queues a date (a Unix time made local) followed by a
text, and `FUN_0043e570` queues a node that empties the box. The
interface is the screen's `+0x225` (`bnclient_attach`, src-frontier.md),
which `FUN_00441b70` keeps in `DAT_0077b09c` (`0x441bbf`).

**What the screen checks before it sends a line.** `FUN_004423b0`, the
handler for a typed line, first saves the line in the history
(`FUN_0043f510`), then tries the commands `/r`, `/reply` and `/d2notify`
(`FUN_0043e780`). `FUN_004428d0` then checks the text twice:

- `FUN_0043e860` looks for a word, at word boundaries, whose length and
  hash (`FUN_00414150`) are the two dwords at `+0x3c7` and `+0x3c3` of
  the frontend record `DAT_0077bbd8`. `FUN_00432080` writes the
  password's hash and length there on the account screens, through
  `DAT_007795d4` (likely the same record; frontend-mainmenus.md).
- `FUN_0043e900` looks for 16 letters or digits in a row (`-` and space
  are skipped) that decode with `FUN_00449920` (the alphabet at
  `0x6d59f0`, then a checksum) as product 6, 7, 0xa or 0xc.

On a match, the screen shows the two-text box (`FUN_0043ea70`) with
strings 0x2b37 / 0x2b38 for the first check or 0x2b7a / 0x2b7b for the
second. It also clears the byte `0x77ae40` and returns 0. The box's
button (`0x4428b0`) runs `0x442810`, the same code a line without a match
goes to (`0x44299a`), then closes the box.

**The history.** Ten 0x100-byte lines start at `0x77b1d8`, newest first.
`DAT_0077bd50` is the count and `DAT_0070cd74` the line Tab last
recalled. Pressing Tab in the chat edit box `DAT_0077bd94` (the filter
`FUN_0043f9b0`, `FUN_004fdad0`) moves to the next line and wraps to the
newest after the oldest.

**The message of the day.** The realm's MCP 0x12 reply
(battlenet.md) comes in parts. Its handler (`0x44b120`) calls
`DAT_00799308`, which `FUN_00441b70` sets to `FUN_00440d10` (`0x441e26`).
The handler passes the byte at `+1` as the part number and the text
from `+2`. `FUN_00440d10` keeps the parts in a list at `DAT_0077bdb4`
sorted by part number, and `FUN_0043f730` writes them, joined, into the
text box `DAT_0077bc4c`.

**The ladder.** The ladder screen's list box is `DAT_0077bd00`. Its type
is the byte at `0x70cd62`, from four groups: 0 (classes 0..7), 9, 0x13
and 0x1b, plus a class 0..7. Class 0 asks for 1000 rows (`DAT_0077ad74`);
classes 1..7 ask for 200. Rows not yet received show `-`; the next page
of 16 to ask for is the one holding the first such row in view
(`FUN_004402a0`), and the answer arrives in `FUN_004403f0`. The four
openers (`0x4408b0`, `0x440980`, `0x440a50`, `0x440b30`, picked by
`DAT_00798ce8` and `DAT_00798ce4` in code at `0x443ef0`) register
`FUN_004403f0` and `0x43ed50` through `0x44b210`, code Ghidra has no
function for. It stores them in `0x799300` and `0x799304`, which the MCP
0x11 handler `0x44afc0` reads. Then `FUN_0043ec60` asks for the rows of
the name at `0x798cd4`. The eight class tabs'
handlers sit at `0x43f1d0` + 0x60 × class, in code Ghidra has no function
for. Each comes from its control entry (0x6e's handler is `0x43f1d0`).
Each one:

1. raises all the tabs (`0x43f060`) and presses its own (`FUN_005009a0`, 1);
2. stops there when the type is already that class in group 0 or 9;
3. otherwise sets the row count, clears the list (`FUN_0043f0e0`), sets
   `DAT_0070cd6c` to 1 and `DAT_0070cd7c` to 0, and sets the type to
   `FUN_0043f150(type, class)`.

Step 2 tests only groups 0 and 9, so a tab of group 0x13 or 0x1b asks for
its rows again even when they're already showing. `DAT_0070cd70`, which
adds the last two tabs, is only read (`0x43efe7`), and its static value
is 1.

**The profile.** Three Battle.net keys at `0x70cd54`: `profile\sex`,
`profile\location` and `profile\description`. Their buffers are 8, 0x28
and 0xc8 bytes. `FUN_00440ca0` reads them and `FUN_0043f5f0` writes them.
`FUN_00443d50` labels the profile box: entries 0x8b..0x8e with strings
0x1493..0x1497 (Name, Gender, Location, Description; 0x1495 is skipped)
and 0x92 with 0x1492 (PLAYER PROFILE).

**The control entries this section's functions handle.** An entry is
`+0` type (1 edit box, 2 box, 4 text list, 6 button, 8 the queue's
timer), `+4`, `+8` x and y, `+0xc`, `+0x10` width and height, `+0x18`
string, `+0x20` the click handler and `+0x24` an edit box's Enter
handler:

| Entry | Type | At, size | String | Handler |
|---|---|---|---|---|
| 0 | 6 | (0x1b, 0x1d6), 0x50 × 0x14 | 0x14b9 SEND | `FUN_00446890` |
| 1 | 6 | (0x6b, 0x1d6), 0x50 × 0x14 | 0x14ba WHISPER | `FUN_004468f0` |
| 5 | 6 | (0xc3, 0x1ea), 0x48 × 0x14 | 0x14bd EMOTE | `FUN_00446970` |
| 0x16 | 1 | (0x1c, 0x1c2), 0x14f × 0x20 | | Enter `FUN_004429a0` |
| 0x30 | 1 | (0x25e, 0x94), 0x9b × 0x14 | | Enter `FUN_004439f0` |
| 0x31 | 4 | (0x1b0, 0x189), 0xa0 × 0xad | | `FUN_004450d0` |
| 0x3c | 1 | (0x1b0, 0xa2), 0x9b × 0x14 | | Enter `FUN_00441240` |
| 0x3e | 4 | (0x1b0, 0x18d), 0x141 × 0xc8 | | `FUN_00441320` |
| 0x3f | 6 | (0x29f, 0x1b1), 0x60 × 0x20 | 0x13ee OK | `FUN_00441240` |
| 0x4e | 6 | (0x1af, 0x155), 0xf × 0x10 | | `FUN_00445550` |
| 0x56..0x59 | 6 | (0x2a9, 0x128 / 0x136 / 0x14c / 0x15a), 0xf × 0xc | | `FUN_004433c0`, `FUN_00443420`, `FUN_00443480`, `FUN_004434e0` |
| 0x9a | 6 | (0x1b1, 0x1b1), 0x60 × 0x20 | 0x13ef CANCEL | `0x4438c0` |
| 0x9b | 8 | | | `0x445530` |

**The channel screen.** `FUN_004411e0` joins the channel named in ECX:
the Battle.net interface's JOINCHANNEL (slot `+0x1c`, bnclient.md, EDX 0
and stack 0), then it closes the boxes (`chatdlg_close_all`) and raises
the four tab buttons `DAT_0077b070`..`DAT_0077b07c`. The name comes from
the edit box `DAT_0077bcf4`, through OK or Enter (`FUN_00441240`, only
while the button `DAT_0077bcec` is enabled), or from the channel list
`DAT_0077bcfc` (`FUN_00441320`): a click copies the row's name into the
edit box; a second click of message 0x201 on the same row within 500 ms
(`DAT_0070eb90`, `DAT_0077bda8`) joins it.

**The chat line.** Enter in the chat edit box (`FUN_004429a0`) and the
SEND button (`FUN_00446890`) hand the line, narrowed, to `FUN_004428d0`
with EDX 0; WHISPER (`FUN_004468f0`) passes EDX 1. Enter resets the
history cursor (`DAT_0070cd74` := −1) and empties the box when the line is
only spaces (`_strtok` finds no word). EMOTE (`FUN_00446970`) sends
`/me %s` (`0x6d57e8`), but not for a line that starts with `/`, and not
within 1000 ms of the last one (`DAT_00798f50`). `FUN_00446820` cuts a
user name `character*account` to the part after `*` (the whole name when
nothing follows the `*` or it has none); `FUN_004428d0` uses it for its
`/whisper *%s %s` (`0x442905`).

**The game list.** `DAT_00798ccc` games in 0x3b-byte entries from
`0x77bdc8`: `+1` name, `+0x11` description, `+0x31` player count (word),
`+0x33` the game's id (word), `+0x37` a dword. `FUN_00445020` asks for it
again (the chat loop `FUN_00441500` every 15 s while the state is 3,
`0x4418c4`, and the panel
builder `FUN_00445ff0` at `0x445fc5` when 5 s have passed since
`DAT_00798cf0`): it sets the screen state
`DAT_0077bbe4` to 3 and the count to 0. When the frontend record's
`+0x35e` byte is 2 it sends MCP 0x05 (`FUN_0044a590`, battlenet.md) with a
zeroed 0x18-byte filter, its `+0x14` dword 0x800 for a hardcore character
(record `+0x1ef` bit 2), under the next request id (`DAT_0070cd60`,
skipping 0), kept in `DAT_0077bbdc`. Otherwise `FUN_00444e70` fills the
list from Battle.net's GETADVLISTEX (slot `+0x38`, ECX and EDX 0): a
count, then per game a dword at `+0xc` (to `+0x37`), the name at `+0x20`,
two more strings, then a string whose first character `FUN_00442b20`
tests against the character's titles; a game it refuses is dropped, the
others keep the rest of that string as their description. The hardcore,
expansion and difficulty words `FUN_00444e70` works out for the call are
never read by GETADVLISTEX (bugs.md #139).

"On a realm" below means `FUN_00449b30` is nonzero: the realm connection
`DAT_00798fb4` is up. There the answers come one game per MCP 0x05 reply
into the record at `0x799108` (`FUN_0044b2d0`; mcp-connect.md). In state
3 the chat loop reads it (`0x4415cf`) when its request id (`+4`) is set:
for the list's id, status 0x33 or `+0` set (the reply runs past its two
strings) redraws the list (`FUN_00442c20`), status 0x32 calls
`FUN_00443130` and `FUN_004413e0(1)`, and anything else adds the game
(`FUN_00442b90`, skipped when a listed game has the same id). The add
has no bound: the array holds 1000 entries (`0x77bdc8`..`0x78a440`) and
the redraw's snapshot follows it. For
the info request's id `DAT_0077bbe0` it shows the game's details
(`FUN_00442eb0`). Either way the record is then cleared (`FUN_00449d60`).

`FUN_00442c20` redraws the list box `DAT_00798bc0` from a snapshot of the
entries at `0x78a440` (count `DAT_00798cc8`): each game's name, and on a
realm its player count in column 1; the selected name and the top row
are kept. A click on a game (`FUN_004450d0`) puts its name in the join
edit box `DAT_00798bd4` and, on a realm, queues an MCP 0x06 for it
(`FUN_00442dd0`: the name at `0x798ab8`, the request id in
`DAT_0070ebd8`), which `FUN_00442e00` sends at most once a second from
the chat loop (`0x4418e3`). Off a realm the click shows the entry's
description (`FUN_00442e30`). A second click within 500 ms joins
(`FUN_004439f0`, with `DAT_0070ebdc`, `DAT_00798cec`). Typing in the join
box clears the list's selection (`FUN_004450a0`); the box's change
handler `0x4450c0` enables the join button `DAT_00798bcc` while the box
holds text (`FUN_004437e0`). The join name allows letters, digits, space,
`'`, `-` and `_` (`FUN_00443540`, the table at `0x443590`); the password
box `DAT_00798bd8` and another box of `FUN_00445ff0` (`0x4461b5`) allow
`0x20`..`0x7f` (`FUN_00443600`, `FUN_004435d0`, the same code).

Joining a game not on a realm asks Battle.net for that one game
(`FUN_004438e0`, GETADVLISTEX with the name and password in ECX and EDX)
and copies the first answer into `FUN_004439f0`'s entry: `+0x33` 1, `+0x37`
from the answer, the name and, past the title test, the description.

**A game's details** (`FUN_00442eb0`, into the box `DAT_00798bc4`): the
description and an empty line, `Elapsed Time: %d:%02d:%02d` of the
record's `+0xc` seconds, then for a level byte `+0x11` under 100 either
`Level %d` (`+0x10`, a difference of 0) or `Level %d to %d`
(`+0x10` − `+0x11`, at least 1, to `+0x10` + `+0x11`, at most 99), and
`Up to %d Players` for a `+0x12` of 1..7 (8 shows nothing). After an
empty line, each of the `+0x14` players with a name (`+0x8c` + 0x10 × n):
the name, then `Level %d <class>` from the words at `+0x6c` and `+0x4c`
(classes 0..6; any other gives `Level %d`), added in colour 5
(`FUN_004fcff0`).

**The create-game panel.** The max-players box `DAT_00798c38` and the
level-difference box `DAT_00798c3c` each have up and down arrows
(entries 0x56..0x59): 1..8 and 0..99, written into the static buffers
`0x70ebd4` and `0x70ebd0`. Their starting values come from the registry
(`Diablo II`): `MAX_PLAYER` (`FUN_004431b0`, written only when 1..8; a
missing value is stored as 4) and `LVL_REST` (`FUN_00445300`; a missing
value is stored as 4 and the boxes are left alone; 666 unticks the
level-difference button `DAT_00798c40`, any other value goes in the box
and ticks it). The button's handler `FUN_00445550` and `FUN_004451c0`
are the same code (the handler returns 1): ticked, `control_set_hidden`
(`FUN_004f9740`) gets 0 for `DAT_00798c44` and `DAT_00798c24` and 1 for
`DAT_00798c1c` and `DAT_00798c20`, those two and the box are enabled
(`FUN_004f96f0`), and the labels `DAT_00798c04` (0x148b "Character
Difference:") and `DAT_00798c08` (0x148c "Levels") turn colour 4;
unticked, the reverse in colour 5. Both then call `FUN_00443630`, which
enables the button `DAT_00798c28` when the name box `DAT_00798c2c` holds
a name that neither starts nor ends with a space and, on a realm, the
max-players box isn't empty (a value under 1 or over 8 is rewritten as 1
or 8) and, when the button is ticked, the difference box holds a number
of 0 or more.
`0x4456a0`, `0x4456b0` and `0x4456c0` call it too.

**The create queue.** `FUN_004453b0` replaces the panel with the queue
box: entry 0x19, the title 0x97 (0x141e CREATE GAME), the text 0x98 with
0x2b0f "You are in line to create a game…", then 0x2b12 "Your position in
line is: ", the colour code (0xf9a and `0`) and `%d ` of the position
(`FUN_00449e20`, mcp-connect.md), the CANCEL button 0x9a and the timer
0x9b in `DAT_00798c28`. Its callers: `FUN_004456d0` (`0x445a60`),
`FUN_00445ff0` (`0x44601e`, when `FUN_00449e40` says a queue is up) and
the timer's handler `0x445530` (closes the boxes and draws it again).
CANCEL (`0x4438c0`) sends MCP 0x13 (`FUN_0043f850`). The loop step
`0x443880` ends the wait when the queue flag, the join token
(`FUN_00449be0`) or `mcp_result` turns nonzero, or `DAT_00798cc4` is set.
On open Battle.net `FUN_004456d0` waits instead with the loop step
`FUN_00443840`, which ends when the STARTADVEX3 reply (slot `+0x8c`,
bnclient.md) is nonzero.

**The ladder search.** `FUN_00443d10` (EAX name, stack two flags) copies
the name to `0x798cd4` and the flags to `DAT_00798ce4` and
`DAT_00798ce8`: the inputs of the code at `0x443ef0` that opens the
ladder.

## The functions

Rows run in address order.

| Function | Operation | Callers |
|---|---|---|
| `FUN_0043e250` | EDI text, stack colour, clear (`ret 8`): a queue node (0x10 bytes, line 0xc1d): a copy of the text (line 0xc28, `FUN_004135d0`) or 0, the colour and the flag | `FUN_0043e310` |
| `FUN_0043e310` | ECX colour, EAX clear, EDI text: a node (`FUN_0043e250`) added at the queue's tail under the lock `0x77ae28` | `FUN_0043e4c0` (three times) |
| `FUN_0043e3a0` | empties the queue: for each node, a set clear flag empties the box `DAT_0077bd8c` (`FUN_004fb6a0`); a text goes in line by line in its colour (`FUN_0043e1a0`) and is freed (line 0xc38); the node is freed (0xc39) and the box scrolled to its top (`FUN_004fba30`, 0) | `FUN_00441500` (`0x4418e8`) |
| `FUN_0043e450` | ESI out, EBX size, stack FILETIME (`ret 4`): the date (`FileTimeToSystemTime`, `GetDateFormatA` with 0x400 and 2) in ESI, widened with the ANSI code page (`MultiByteToWideChar`, 0x100) and narrowed back into ESI (`FUN_005263e0`, EBX) | `FUN_0043e4c0` |
| `FUN_0043e4c0` | ECX Unix time, EDX text: a null text is fatal (line 0xca4). A time of 0 queues nothing. Otherwise the time is converted to a FILETIME (+ 11644473600 s, × 10⁷), made local (`FileTimeToLocalFileTime`) and formatted (`FUN_0043e450`, 0x40 bytes). Queues the date (colour 0), the text (colour 4), then an empty line. Returns 1 | none: passed to the Battle.net interface's slot `+0xa8` as ECX (`0x441f9a`) |
| `FUN_0043e570` | queues a node with no text and the clear flag set; returns 1 | none: passed as EDX with `FUN_0043e4c0` (`0x441f95`) |
| `FUN_0043e5f0`, `FUN_0043e610` | ECX text, EDX colour: added to the box `DAT_0077bd8c` (`FUN_004fc9b0`, stack colour, 0, 1) when it exists; returns 1. The two are the same code | `FUN_0043e5f0`: `FUN_004423b0` (7 sites); `FUN_0043e610`: 11 ComCallback.cpp functions (`FUN_00447ab0` … `FUN_004496b0`) |
| `FUN_0043e630` | ECX text: the box `DAT_0077bd98` emptied (`FUN_004fb6a0`) and given the text in colour 3; returns 1 | `FUN_00446b30`, `FUN_00449050`, `FUN_004493e0`, `FUN_004495a0` |
| `FUN_0043e660` | the button `DAT_0077bcec` enabled (`FUN_004f96f0`) when the edit box `DAT_0077bcf4` holds at least one character (narrowed, 0x1f) | `0x440110` (passed with that edit box at `0x441097`), the tail jump at `0x4411d7` |
| `FUN_0043e6e0` | the same code as chat-commands.md's `FUN_0047b3b0` (`chat_prefix_compare`, call targets aside) | `FUN_0043e780`, `FUN_00442180`, `FUN_004423b0` |
| `FUN_0043e780` | EBX command, EAX line: when the line starts with the command (`FUN_0043e6e0`) followed by a space or its end, toggles `DAT_0077bd54` and adds "Player entering/leaving notifications are enabled." or "… disabled." (`0x6d54a8`, `0x6d5474`; `FUN_00526320`, 0x200) to the box `DAT_0077bd8c` in colour 3; returns 1. Else 0 | `FUN_004423b0` (`0x4423f4`, command `/d2notify`) |
| `FUN_0043e860` | EBX text, stack hash, length (`ret 8`): 0 for length 0. For each start where the word of that length has no letter or digit (`0x6870fb`) just before it or just after it, hashes it (`FUN_00414150`, the text cut there for the call) and returns 1 when the hash matches. Else 0 | `FUN_004428d0` (`0x442947`, the record's `+0x3c3` and `+0x3c7`) |
| `FUN_0043e900` | (text; `ret 4`): collects letters and digits, skipping `-` and space, and starts again at any other character; at 16 it decodes them (`FUN_00449920`) and returns 1 for product 6, 7, 0xa or 0xc. Else 0 | `FUN_004428d0` (`0x44297d`) |
| `FUN_0043e9d0` | EAX entry: a control from `0x70ce50 + EAX × 0x30` (`FUN_004f93c0`) pushed on the stack `0x77ada8`; returns it. A full stack (0x20) is fatal (line 0x1475) | 49 call sites: `FUN_0043ea70`, `FUN_0043ed60`, `FUN_00440140`, `FUN_004401c0` and the ladder code at `0x4406f0`..`0x440bad` |
| `FUN_0043ea70` | ECX text, EDX text: frees the stacked controls (`FUN_0043ea20`), then entries 0x25, 0x27, 0x28 with ECX and 0x29 with EDX (`FUN_004fc9b0`, 0, 0, 0); `FUN_004fa470(0x25, 0)`, and `+8` bit 3 set on all four (`FUN_004fa420`) | `FUN_004428d0` (`0x44296a`) |
| `FUN_0043eb10` | (arg; `ret 4`): `DAT_0077bda4(arg)` when set; returns 1 | none: the handler of entries 0x22 (`0x70d4d0`) and 0x23 (`0x70d500`) |
| `FUN_0043ec60` | ECX name, EDX, stack flag (`ret 4`): with the list `DAT_0077bd00`, copies the name to `0x77b040`; sets both type bytes `0x70cd62`, `0x70cd63` to 0x1b (flag, EDX 0), 0x13 (flag), 9 (EDX 0) or 0; sets `DAT_0070cd7c` and `DAT_0070cd80` to −1; sends `FUN_0044aae0(name, EDX, flag)` (MCP 0x16, battlenet.md); `DAT_0077bd5c` := `GetTickCount` + 1000 | the code at `0x443ef0` (`0x443f3b`: name `0x798cd4`, EDX `DAT_00798ce4`, flag `DAT_00798ce8`) |
| `FUN_0043ed60` | the ladder's class tabs: entries 0x6e..0x73 (and 0x74, 0x75 when `DAT_0070cd70` is set) into `DAT_0077bc98`..`DAT_0077bcb4`, the first pressed (`FUN_005009a0`, 1); entry 0x76 labelled with string 0x14ce, 0x77..0x7d with `AMA`, `SOR`, `NEC`, `PAL`, `BAR`, `DRU`, `ASN` (widened, `FUN_004fd060`) | the code at `0x4406f0` (`0x4406f1`), which `0x440942` calls |
| `FUN_0043f0e0` | the list `DAT_0077bd00` emptied (`FUN_004fb6a0`) and refilled with `DAT_0077ad74` rows of `-` (`FUN_004fcc40`, the count, 4, 0), scrolled to the top; `DAT_0077bbf4` := −2 | the eight tab handlers (`0x43f1fc` … `0x43f49c`), the ladder code (`0x44087d`) |
| `FUN_0043f150` | EAX type, stack class (`ret 4`): the type's group (0..7 → 0, 9..0x10 → 9, 0x13..0x1a → 0x13, 0x1b..0x22 → 0x1b) plus the class. Any other type is fatal (line 0x1a23) | the eight tab handlers (`0x43f214` … `0x43f4b4`) |
| `FUN_0043f4d0` | empties the ten history lines (`0x77b1d8`..`0x77bbd8`); `DAT_0070cd74` := −1 | `FUN_00441b70` (`0x441db2`) |
| `FUN_0043f510` | ESI line: moves history lines 0..8 down one (0x900 bytes, `0x683840`), copies the line to the first, the count + 1 (at most 10) | `FUN_004423b0` (`0x4423c0`) |
| `FUN_0043f560` | ECX control: `+4` := `DAT_0077bc6c` (`FUN_004f91a0`), then disabled (`FUN_004f96f0`, 0) | `FUN_00445ff0` (`0x446681`, `0x446701`) |
| `FUN_0043f590` | (`ret 4`): the interface's slot `+0x7c` (ECX 3, EDX 1, the buffers `0x77b0a8`, sizes 8, 0x28, 0xc8). A nonzero result, or `DAT_0077ada4` set, ends the loop (`FUN_004f9190`). Returns 1 | none: the loop step `FUN_00440ca0` passes to `FUN_004fa590` (`0x440cf0`) |
| `FUN_0043f5f0` | ECX account, EDX sex, stack location, description (`ret 8`): the interface's slot `+0x74` (ECX, EDX 3, the keys `0x70cd54`, the three values) | `FUN_00443f80` (`0x444004`: the three edit boxes `DAT_00798c80`..`88` narrowed to 8, 0x28 and 0xc8 bytes, ECX the frontend record `+0x8d`) |
| `FUN_0043f800` | frees the message-of-the-day parts (line 0x1c01) and clears `DAT_0077bdb4` | `FUN_00441b70` (`0x442026`) |
| `FUN_0043f850` | `FUN_0044aa80` (MCP 0x13, battlenet.md), `FUN_00449e50(0)`; returns 1 | `FUN_004456d0` (`0x445a4d`), the code at `0x4438c0` (`0x4438c5`) |
| `FUN_0043f870` | the same code as chat-commands.md's `FUN_0047c3f0` (`chat_after_space`) | `FUN_004423b0` (5 sites) |
| `FUN_0043f930` | (a, callback; `ret 8`): a null callback is fatal (line 0x559); `chatdlg_close_all`, `DAT_0077bda4` := callback; returns 1 | orphan: no call, jump or pointer (byte scan) |
| `FUN_0043f9b0` | (a, position, key; `ret 0xc`): key 0xe, with text in `DAT_0077bd94` (`FUN_004fdd90`) and `DAT_00798f48` set, puts the name at `DAT_00798f48 + 4` (cut at a `*`, `FUN_00446840`) into the text at the position (`%s%s%s`, `0x6d5514`) when the result fits 0xff; key 9 (Tab) recalls the next history line (`FUN_004ffe10`). Both return 0, any other key 1 | none: the edit box filter (`FUN_004fdad0`) set at `0x441cc0` and `0x43fd43` |
| `FUN_0043fbd0` | `chatdlg_close_all`, the buttons `DAT_0077b070`..`DAT_0077b07c` raised (`FUN_005009a0`, 0), `DAT_0077bbe4` := 0 | `FUN_00447700` (`0x447717`) |
| `FUN_00440140` | ECX text, EDX callback: frees the stacked controls, then entries 0x21, 0x22 and 0x24 with the text (`FUN_004fa470(0x21, 0)`, `+8` bit 3 set on all three); `DAT_0077bda4` := the callback, or `0x43eb00` | 10 functions: `FUN_00440de0`, `FUN_004412a0`, `FUN_004413e0`, `FUN_00442180`, `FUN_004423b0`, `FUN_004428d0`, `FUN_004439f0`, `FUN_00446a00`, `FUN_00448e30`, `FUN_00448f60` |
| `FUN_004401c0` | as `FUN_00440140` with entry 0x23 for 0x22, and `DAT_0077bbf0` := `GetTickCount` (`FUN_0043ea20` clears it) | `FUN_00440ca0`, `FUN_00441a90`, `FUN_004439f0`, `FUN_004456d0` |
| `FUN_004402a0` | the first row from the list's top (`FUN_004fb950`) through the rows in view (`FUN_004fbab0`, at most `DAT_0077ad74`) whose first cell (`FUN_004fbb90`) is `-`: `DAT_0070cd68` and `DAT_0070cd7c` := the row rounded down to 16; returns 1. Else 0 | the ladder step at `0x440350` (`0x44036a`; pointer `0x70e670`), once `GetTickCount` is past `DAT_0077bd5c` |
| `FUN_004403f0` | ECX first row, EDX count, stack name size, type, entries (`ret 0xc`). Count 0: "Player '%s' is not on the ladder." (`0x6d5540`, the name from `FUN_00443f50`) through `chatdlg_reopen`. A type other than `0x70cd62` is ignored. After a name search (`DAT_0070cd7c` −1), the list scrolls to the first row. Each entry is name size + 0xc bytes: +0 / +4 experience, +8 flags (class & 0xf, title bits 8..12, 0x20, 0x40, level bits 16..23), +0xc name. Its row gets colour 0 when the name is `0x77b040`'s, else (flags & 0x10 \| 0x40) >> 4, and the cells rank, title + name (`FUN_005068a0`), class code (`ama` … `asn`, `0x6d5524`), level and experience (`FUN_005255c0`) | none: registered with `0x44b210` (code Ghidra has no function for) by the four openers (`0x440971`, `0x440a41`, `0x440b16`, `0x440bf6`); read by the MCP 0x11 handler `0x44afc0` |
| `FUN_00440ca0` | ECX, EDX, stack two buffers (`ret 8`): the three buffers into `0x77b0a8`; the interface's slot `+0x78` (ECX, EDX 3, 1, the keys `0x70cd54`); a box (`FUN_004401c0`, string 0x147b, the cancel `0x43f580`, which sets `DAT_0077ada4`); the loop `FUN_004fa590(FUN_0043f590)`; closes the box. Returns 1 unless cancelled | `FUN_00445a80` (`0x445ae0`), `FUN_00445c70` (`0x445cc8`) |
| `FUN_00440d10` | ECX part, stack text (`ret 4`): the part's node in `DAT_0077bdb4` (0x208 bytes, line 0x1bae; text, `+0x200` part, `+0x204` next), made in sorted place when new; the text copied in (0x200); then `FUN_0043f730` | none: `DAT_00799308` (`0x441e26`), called by the MCP 0x12 handler (`0x44b13d`) |
| `FUN_004411e0` | ECX channel name: JOINCHANNEL (the interface's slot `+0x1c`, EDX 0, stack 0), `chatdlg_close_all`, the buttons `DAT_0077b074`, `DAT_0077b070`, `DAT_0077b078`, `DAT_0077b07c` raised (`FUN_005009a0`, 0), `DAT_0077bbe4` := 0 | `FUN_00441240`, `FUN_004412a0`, `FUN_00441320`, `FUN_004423b0` (`0x442618`) |
| `FUN_00441240` | (arg; `ret 4`): 0 while the button `DAT_0077bcec` is disabled (`FUN_004f9710`); else the edit box `DAT_0077bcf4`'s text, when there is one, narrowed (0x100) and joined (`FUN_004411e0`); returns 1 | none: the Enter handler of entry 0x3c (`0x70d9b4`) and the handler of the OK button 0x3f (`0x70da40`) |
| `FUN_00441320` | (event; `ret 4`): the channel list `DAT_0077bcfc`'s selected row (`FUN_004fb990`); none does nothing. A message 0x201 on the row last clicked (`DAT_0070eb90`) within 500 ms of it (`DAT_0077bda8`) joins the edit box's channel (`FUN_004411e0`); any other click records the row and time and puts the row's first cell in the edit box `DAT_0077bcf4` (`FUN_004ff5a0`), focus cleared (`FUN_004f91c0`, 0). Returns 0 | none: the handler of list entry 0x3e (`0x70da10`) |
| `FUN_004429a0` | (wide line; `ret 4`): `DAT_0070cd74` := −1; an empty line returns 0; no Battle.net interface `DAT_0077b09c` is fatal (line 0x12c2); the line narrowed (0x100). With no word (`_strtok` on a copy, delimiter space) the chat edit box `DAT_0077bd94` is emptied and 1 returned; else `FUN_004428d0(line, 0)` | none: the Enter handler of entry 0x16 (`0x70d294`) |
| `FUN_00442ae0` | ECX string: the byte after its terminator. Null is fatal (line 0xad) | `FUN_004438e0` (three sites) |
| `FUN_00442b20` | ECX string: tests its first character `c` against the frontend record's `+0x1ef` word: with `t` = (bits 8..12) / 5 + 1, returns 1 (refuse) when, with bit 2 set, `c` is under `3` or `c` − `3` > `t`, and without it when `c` − `0` > `t`; else 0 | `FUN_004438e0` (`0x4439b1`), `FUN_00444e70` (`0x444f8f`) |
| `FUN_00442b90` | ECX the MCP 0x05 record: unless a listed game's `+0x33` equals the record's `+8` word, a new entry at the count: `+0x33` that word, `+0x37` 0, `+0x31` the `+0x14` word, `+1` the name `+0x1c`, `+0x11` the description `+0x2c`; the count `DAT_00798ccc` + 1. No bound | `FUN_00441500` (`0x441635`) |
| `FUN_00442c20` | with the list box `DAT_00798bc0`: its selected name (narrowed) and top row (`FUN_004fb950`) kept; it and the info box `DAT_00798bc4` emptied; `DAT_00798cc8` := the count; the entries copied to `0x78a440`; each name added (`FUN_004fcf80`), and on a realm the player count (`__itoa`) in column 1 (`FUN_004fcd40`); the row with the kept name selected again (`FUN_004fb9d0`); the top restored (`FUN_004fba30`) | `FUN_00441500` (`0x4415f6`, `0x441624`), `FUN_00444e70` (`0x444feb`) |
| `FUN_00442dd0` | ECX name, DX request id: the name to `0x798ab8` (0x100, `FUN_00687330`), `DAT_0070ebd8` := the id | `FUN_004450d0` (`0x445176`) |
| `FUN_00442e00` | when `DAT_0070ebd8` isn't −1: MCP 0x06 (`FUN_0044a610`, ECX `0x798ab8`, EDX the id), `DAT_0077bbe0` := the id, `DAT_0070ebd8` := −1 | `FUN_00441500` (`0x4418e3`, once a second) |
| `FUN_00442e30` | EAX row (the entry at `0x77bdc8` + 0x3b × row): with the info box `DAT_00798bc4`: emptied, the entry's description (`+0x11`) and an empty line added when not empty, scrolled to the top; the name (`+1`) into the join box `DAT_00798bd4` (`FUN_004ffe10`), focus cleared | `FUN_004450d0` (`0x4451aa`) |
| `FUN_00442eb0` | ECX the MCP 0x06 record: a game's details into the info box `DAT_00798bc4` (see "A game's details"; the strings at `0x6d56e8`..`0x6d57a0`, the class jump table at `0x443110`) | `FUN_00441500` (`0x44164f`) |
| `FUN_004431b0` | the registry's `Diablo II\MAX_PLAYER` (`FUN_00414f10`; missing: 4 written, `FUN_004150e0`); a value of 1..8 into the box `DAT_00798c38` (`%d`) | `FUN_00445ff0` (`0x44642b`) |
| `FUN_00443360`, `FUN_00443390` | the box `DAT_00798c38` (`FUN_00443360`) or `DAT_00798c3c` (`FUN_00443390`) narrowed to 5 bytes, `_atoi` | orphan: no call, jump or pointer (byte scan) |
| `FUN_004433c0`, `FUN_00443420`, `FUN_00443480`, `FUN_004434e0` | (arg; `ret 4`): the box's number + 1 up to 8, − 1 down to 1 (`DAT_00798c38`, buffer `0x70ebd4`), + 1 up to 99, − 1 down to 0 (`DAT_00798c3c`, buffer `0x70ebd0`), written back (`%d`); returns 1 | none: the handlers of entries 0x56..0x59 (`0x70de90`, `0x70dec0`, `0x70def0`, `0x70df20`) |
| `FUN_00443540` | (a, b, character; `ret 0xc`): 1 for a letter, digit, space, `'`, `-` or `_` (the byte table `0x443590` for `0x20`..`0x5f`), else 0 | `FUN_004450a0` (tail jump `0x4450b2`); passed at `0x44614f` in `FUN_00445ff0` |
| `FUN_004435d0`, `FUN_00443600` | (a, b, character; `ret 0xc`): 1 for `0x20`..`0x7f`, else 0. The same code | passed as edit filters: `FUN_004435d0` at `0x4461b5`, `FUN_00443600` at `0x445f35` (the password box `DAT_00798bd8`) and `0x44618a` |
| `FUN_00443630` | enables the button `DAT_00798c28` (`FUN_004f96f0`) when the game name, max players and level difference pass ("The create-game panel"), else disables it | tail jump from `FUN_004451c0` (`0x4452f7`), `FUN_00445550` (`0x445687`), `0x4456a0`, `0x4456b0`, `0x4456c0` |
| `FUN_004437e0` | the join button `DAT_00798bcc` enabled when the join box `DAT_00798bd4` holds at least one character (narrowed, 0xff), else disabled | `0x4450c0` (the join box's change handler, set at `0x445f09`), tail jump from `FUN_00445ff0`'s end (`0x445fe1`) |
| `FUN_00443840` | (arg; `ret 4`): the STARTADVEX3 reply (the interface's slot `+0x8c`); nonzero ends the loop (`FUN_004f9190`); returns 1 | none: the loop step `FUN_004456d0` passes to `FUN_004fa590` (`0x445906`) |
| `FUN_004438e0` | ECX name, EDX password, stack entry (`ret 4`): GETADVLISTEX (slot `+0x38`, with the words of `FUN_00444e70`); with at least one game, the first goes into the entry (`+0x33` 1, `+0x37` its `+0xc` dword, `+1` its name; past `FUN_00442b20`, `+0x11` its description) and the count is returned; else 0 | `FUN_004439f0` (`0x443b13`) |
| `FUN_00443d10` | EAX name, stack two flags (`ret 8`): the name to `0x798cd4`, the flags to `DAT_00798ce4`, `DAT_00798ce8` | `FUN_00445a80` (`0x445c55`), `FUN_00445c70` (`0x445de0`) |
| `FUN_00443d50` | the profile box's labels: entries 0x8b, 0x8c, 0x8d, 0x8e, 0x92 into `DAT_00798c90`, `94`, `98`, `9c`, `a8` with strings 0x1493, 0x1494, 0x1496, 0x1497, 0x1492 (`FUN_004fd060`) | `FUN_00445a80` (`0x445bdd`), `FUN_00445c70` (`0x445dcc`) |
| `FUN_00444e70` | the Battle.net game list: GETADVLISTEX (slot `+0x38`, ECX and EDX 0, stack: 4 for record `+0x1ef` bit 2 \| 8 for bit 5, and the same \| 3 or 2 by the title bits: none for titles from 0xa00 (bit 5) or 0x800, 2 from 0x500 or 0x400, else 3); the count into `DAT_00798ccc`; each game into the next entry and its name into the list box, a game `FUN_00442b20` refuses taken off the count; then `FUN_00442c20`. A null string pointer is fatal (line 0xad) | `FUN_00445020` (`0x445095`) |
| `FUN_00445020` | asks for the game list again: `DAT_0077bbe4` := 3, the count := 0; record `+0x35e` 2: MCP 0x05 (`FUN_0044a590`) with a 0x18-byte filter (`+0x14` 0x800 for record `+0x1ef` bit 2) under the next request id `DAT_0070cd60` (skipping 0), kept in `DAT_0077bbdc`; else `FUN_00444e70` | `FUN_00441500` (`0x4418c4`), `FUN_00445ff0` (`0x445fc5`) |
| `FUN_004450a0` | (a, b, character; `ret 0xc`): the list box's selection cleared (`FUN_004fb9d0`, −1), then the name filter (`FUN_00443540`) | none: the join box's filter (`FUN_004fdad0`, `0x445ef9`) |
| `FUN_004450d0` | (event; `ret 4`): with the list box's selected row, the info box emptied. A message 0x201 on the row last clicked (`DAT_0070ebdc`) within 500 ms (`DAT_00798cec`) joins (`FUN_004439f0`, 0). Else, on a realm, the row's name goes to `FUN_00442dd0` with the next request id and into the join box, focus cleared; off a realm, `FUN_00442e30`. Returns 0 | none: the handler of list entry 0x31 (`0x70d7a0`) |
| `FUN_004451c0`, `FUN_00445550` | the level-difference button `DAT_00798c40` ticked or not (`FUN_00500960`: 1 when the button's `+0x44` and `+0x48` are both nonzero; a control not of type 6 is fatal, line 0x65): hides, shows and enables the panel's controls ("The create-game panel"), labels 0x148b, 0x148c in colour 4 or 5, then `FUN_00443630`. The same code; `FUN_00445550` returns 1 (`ret 4`) | `FUN_004451c0`: `FUN_00445300` (`0x445361`, `0x44539c`); `FUN_00445550`: `FUN_00445ff0` (`0x446775`) and the handler of entry 0x4e (`0x70dd10`) |
| `FUN_00445300` | the registry's `Diablo II\LVL_REST`: missing writes 4 and returns; 666 unticks the button `DAT_00798c40`; else the value into the box `DAT_00798c3c` (`FUN_00413a40`, `%d`, 4 bytes) and the button ticked; then `FUN_004451c0` | `FUN_00445ff0` (`0x446426`) |
| `FUN_004453b0` | the create queue box ("The create queue"): entry 0x19 in `DAT_00798bec`, 0x97 and 0x98 in `DAT_00798bf0`, `DAT_00798bf4`, the queue position from `FUN_00449e20`, the CANCEL button 0x9a in `DAT_00798c48` (`FUN_004fa450`), the timer 0x9b in `DAT_00798c28`, then `FUN_0043dad0` (ECX 0: `control_set_hidden` of `DAT_0077bc4c` with ECX; returns 1) | `0x445535` (the timer's handler `0x445530`), `FUN_004456d0` (`0x445a60`), `FUN_00445ff0` (`0x44601e`) |
| `FUN_00446810` | a tail jump to `FUN_00687740`, the character search `split_number` uses | orphan: no call, jump or pointer (byte scan) |
| `FUN_00446820` | ECX user name: the text after its `*` (`FUN_00413490`, 0x2a) when that isn't empty, else the name | `FUN_004428d0` (`0x442905`), `FUN_00445c70` (`0x445c7d`) |
| `FUN_00446890`, `FUN_004468f0` | (arg; `ret 4`): the chat edit box `DAT_0077bd94`'s text, when it isn't empty, narrowed (0x100) and sent: `FUN_004428d0(text, 0)` (SEND) or `(text, 1)` (WHISPER); else 1 | none: the handlers of buttons 0 (`0x70ce70`) and 1 (`0x70cea0`) |
| `FUN_00446970` | (arg; `ret 4`): the chat edit box's text, narrowed (0x12c), not starting with `/`, at least 1000 ms after `DAT_00798f50` (which it then sets): `/me %s` (`0x6d57e8`) through `FUN_004428d0(text, 0)`. Returns 1 | none: the handler of button 5 (`0x70cf60`) |
