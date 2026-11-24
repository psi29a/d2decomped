# Src/ChatDlg.cpp: the Battle.net chat screen's helpers

Traced from game.exe 1.14d: each function's disassembly and decompile,
callers from `game-calls.tsv` and a byte scan of game.exe for rel32 calls
and raw pointers. Not run in `tools/emu`. The functions use the assert
path `.\Src\ChatDlg.cpp` (`0x6d5460`). d2d has no Battle.net screens, so
none of this has a counterpart in d2d. frontier-ring.md already has
`chatdlg_close_all` (`FUN_0043f8c0`) and `chatdlg_reopen` (`FUN_0043fb50`).

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
