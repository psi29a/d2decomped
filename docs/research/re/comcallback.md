# Src/ComCallback.cpp: the Battle.net chat screen's users and events

Traced from game.exe 1.14d: each function's disassembly and decompile,
callers from `game-calls.tsv`, and a byte scan of game.exe for rel32 calls
and raw pointers. Not run in `tools/emu`. The file sits between
`0x446b70` and `0x4496b0`; ChatDlg.cpp's helpers are in chatdlg.md, and
the figures the users stand as (D2Comp.cpp) are in src-frontier.md "D2Win:
controls, fonts, D2Comp". d2d has no Battle.net screens, so none of this
has a counterpart in d2d.

## The pieces

**The event pump.** `FUN_00447a10` asks the Battle.net interface
(`DAT_0077b09c`, slot `+0x24`) for its next message: kind 0 is a chat
event (`FUN_00447740`), kind 2 copies 0x32 characters to `0x798cf8`,
kind 3 is an ad (`FUN_00446b70`). Its only caller is `FUN_00441500`.

**The event table** at `0x70ec20`: 24 handlers, one per event id
(`FUN_00447740` refuses ids from 0x18 up). An event is `+0` id, `+4` and
`+8` passed in `ECX` and `EDX`, the user name at `+0x18` and the text
after its terminator. A handler's nonzero result is returned, else 1.

| Id | Handler |
|---|---|
| 0 | `0x446d20` |
| 1 | `FUN_00449330` (adds or updates the user) |
| 2 | `FUN_004493c0` (the same, with the join notice) |
| 3 | `FUN_00449690` |
| 4 | `FUN_00448920` |
| 5 | `FUN_00448370` |
| 6, 0x12, 0x15, 0x16 | `FUN_004484e0` |
| 7 | `FUN_004496b0` |
| 8, 0xb, 0xc, 0x10, 0x11, 0x14 | `0x447730` |
| 9 | `FUN_004480d0` (updates a listed user) |
| 0xa | `FUN_00448ad0` (the name with its title) |
| 0xd | `FUN_004485a0` |
| 0xe | `FUN_00447700` |
| 0xf | `FUN_00448610` |
| 0x13 | `FUN_00448540` |
| 0x17 | `FUN_004486f0` |

**The user list** `DAT_00798f34` (next `+0xb0`, count `DAT_00798f40`):
0xb4-byte records (`FUN_00449050`, line 0x6d4). Each has `+0` the product
code (written as its four characters, high byte first: `D2DV`, `D2XP`,
`D2ST`, `STAR`, …; `UNKN` when unknown), `+4` the user name
(`character*account`, or `*account`), `+0x36` the character name, `+0x46`
the realm, `+0x66` 16 appearance bytes and `+0x76` 16 tints, `+0x86` the
class, `+0x88` level, `+0x8a` the character flags, `+0x90` the user's
flags, `+0x9c` its figure (a D2Comp composite) and `+0xa0`, `+0xa4`,
`+0xa8`, `+0xac` the flags' bits 2, 3, 5 and 6. The figures show 11 at a
time from `DAT_00798f44`; `DAT_00798f48` is the selected user (its slot
in `DAT_0070ec1c`, -1 for none).

**The figure token.** `FUN_00446d90` picks the composite token (the
COF tables of src-frontier.md's `cof_path`) from the user's flags first:
bit 1 0x13, bit 3 0x14, bit 0 0x12, bit 2 0x17, `0x101000` 0x15. Then
the product: `CHAT` 0x16, `DRTL`, `DSHR`, `DTST` 10, `JSTR`, `STAR`,
`SSHR` 0xd, `SEXP` 0xe, `W2BN` 0xf, `WAR3`, `W3XP` 0x10, any other 7
(the product stored as `UNKN`). A D2 product with no character is 0x18;
one with no appearance is 7 (`UNKN`); with one, the class, except that
a character with both bits 2 and 3 set is 9 for classes 0, 1, 6, 9 and
8 for the rest. D2Comp's `composite_hover_draw` names each token.

**The figure's flags** (`+0` of the composite, set by `FUN_00447bb0`):

| Bit | Meaning |
|---|---|
| 0x1, 0x2, 0x4, 0x8 | shadow and body, name label, shadow pass, hover box: all four for a figure in the shown 11 |
| 0x10 | the character flags' bit 3 |
| 0x20 | bit 2 (hardcore): the label in colour 1 |
| 0x40, 0x80 | the title, from the character flags' bits 8..12 (classic 4, 8, 12; expansion 5, 10, 15) |
| 0x100 | a shown figure at an odd place in the list: the label 0xf lower |
| 0x200 | the user's flag 0x20 |
| 0x400 | the selected user |
| 0x800 | the user's flag 1 or 8 |
| 0x1000 | the character flags' bit 5 (expansion) |
| 0x2000 | a figure outside the shown 11 whose `+0x94` byte, hardcore or expansion differs from the player's (`DAT_0077bbd8` `+0x3c2`, `+0x1ef`) |

## The functions

| Address | Reading | Callers |
|---|---|---|
| `FUN_00446b70` | `ESI` ad: the old ad image freed (`FUN_00506990`, `DAT_0077b098`), the data written to a new `BNetAd` temp file (`GetTempFileNameA`; `.` when there's no temp path), the last one deleted (`0x798e30`, which keeps the new name), the image loaded from it by its extension's first letter (`FUN_00506910`); `DAT_0077b0a4` = the ad's `+0`; two buffers freed (lines 0x386, 0x387); returns 1 | `FUN_00447a10` |
| `FUN_00446d90` | `EAX` text, `EDX` user flags, `EDI` record: returns the figure token ("The figure token"). For `D2DV`, `D2ST`, `D2XP` with a character: the realm (to `,`) into `+0x46`, the character name (to `,`) into `+0x36`, then after a word (`FUN_006bbf20`) 11 appearance bytes (the rest 0xff), and when the first is set the class (byte − 1, `+0x86`), 11 tints, the level (`+0x88`), the character flags (`+0x8a`, bits 2, 3, 5, 6 into `+0xa0`, `+0xa4`, `+0xa8`, `+0xac`), a word into `+0x8c` and three bytes into `+0x94`..`+0x96` (0xff as 0). An empty part is fatal (line 0x579). Every other user, a flagged D2 one included, gets the default look (appearance 0 but byte 1 = 1, tints 0xff) | `FUN_00447fd0`, `FUN_00449050` |
| `FUN_004471b0` | `EBX` record: the figure's labels (D2Comp's `composite_set_labels`): for a `*account` name the account alone; else `*` + the account (after the `*`) and the character name (before it, 0x10 characters); the extra text from `+0x97`, the realm from `+0x46`, the level `+0x88` for D2 products (else 0), the product | `FUN_00447fd0`, `FUN_00449050` |
| `FUN_00447400` | ECX user name, EDX character name: the alias list `DAT_00798f38` (0x48-byte entries: `+0` name, `+0x32` character name, next `+0x44`; count `DAT_00798f3c`): an entry whose name matches after its `*` gets the new character name; else a new entry (line 0x93a) goes last, the first one dropped (line 0x936) when there are 0x400 | `FUN_00449050` (at `0x4492a5`, with `+4` and `+0x36`) |
| `FUN_00447560` | ECX name, EDX title out, stack display name out (`ret 4`): a `*account` name is shown whole; one with an `@` before its `*` is shown up to the `*`. Else, when the alias list has the name and the user list has it with title flags (0x40, 0x80), the title (`FUN_00505640(class, flags, bit 12)`). The alias's character name is copied into the display name but always overwritten (`0x447691`, then `0x4476a6`): the result is the name up to its `*`, or the whole name | `FUN_004481f0`, `FUN_00448370`, `FUN_004486f0`, `FUN_00448920`, `FUN_00448ad0` |
| `FUN_00447740` | `EAX` event: its handler from the table at `0x70ec20` ("The event table") | `FUN_00447a10` |
| `FUN_004477a0` | The chat buttons: `DAT_0077b06c` and `DAT_0077b058` enabled while the edit box `DAT_0077bd94` holds text; `DAT_0077b05c` while it does and a user is selected; `DAT_0077b064` for a selected user without flag 0x200, `DAT_0077b068` for one with it (`FUN_004f96f0`) | `0x43db90`, `FUN_0043fc20`, `FUN_00441b70`, `FUN_00448050`, `FUN_00448c70` |
| `FUN_00447870` | ECX wide text, EDX colour: the colour code (string 0xf9a `ÿc`, then the number) appended; an empty text becomes the code. Fatal when the text reaches 500 characters (line 0xb7d) | `FUN_00447b00` |
| `FUN_00447940` | As `FUN_00447870`, the code put before the text (line 0xb9b) | `FUN_00447b00` |
| `FUN_00447a10` | The event pump ("The pieces"): 0 when there's no message, else 1 | `FUN_00441500` |
| `FUN_00447b00` | (`EDX` name part, stack text and the name part's colour, `ESI` the text's colour; `ret 8`): both widened (0x240) and passed through `FUN_00446a90`; the name part gets its colour code before and the text's after (`FUN_00447940`, `FUN_00447870`), the text appended, and the line added to the chat box (`FUN_0043e610`) | `FUN_004481b0`, `FUN_004481f0` |
| `FUN_00447bb0` | Sorts the user list by rank (flag 2 first, then 8, 1, 4, the rest), then sets every figure's flags ("The figure's flags"); the shown 11 are given 0x46, 0x82, … (0x3c apart, `FUN_005052b0`) | `FUN_00447fd0`, `FUN_00448bd0`, `FUN_00449050`, `FUN_004493e0` |
| `FUN_00447fd0` | (`ECX` record, `EDX` user flags, `EAX` text, stack name): updates a listed user: its token (`FUN_00446d90`), name `+4`, mode 5, or 1 for a character with bit 2 and not bit 3 and with the expansion (`FUN_00408f20`) or a token under 5; the figure's look (`composite_set_look`), labels (`FUN_004471b0`) and places (`FUN_00447bb0`) | `FUN_004480d0`, `FUN_00449050` |
| `FUN_00448050` | Clears the selection: bit 0x400 off the selected figure (fatal without one, line 0x85a), `DAT_00798f48` = 0, `DAT_0070ec1c` = -1; then the buttons (`FUN_004477a0`) unless `FUN_0043db10` | `FUN_00448c70`, `FUN_00448e30`, `FUN_00448f60`, `FUN_004493e0`, `FUN_004495a0` |
| `FUN_004480d0` | Event 9: the user whose name matches exactly is updated (`FUN_00447fd0`), 1; 0 when none does | table entry 9 (`0x70ec44`) |
| `FUN_004481b0` | `<%s> ` and the line (`FUN_00447b00`) | `FUN_00448370` |
| `FUN_004481f0` | The player's own line: its name from the interface (slot `+0x18`) matched in the user list (colour 4; 0 with flag 2, 3 with flag 1 or 8), the title (`FUN_00447560`), `<%s%s> ` or `<%s> `, then the line | `FUN_004428d0` |
| `FUN_00448ad0` | Event 0xa: the name with its title (`FUN_00447560`, `%s%s`), or the name alone, to `FUN_00448a60`; 1 | table entry 10 (`0x70ec48`) |
| `FUN_00448bd0` | ECX rows: scrolls the shown 11 (`DAT_00798f44`, kept within 0 and the count − 11; 0 below 12 users), places them again when it moved, then four controls' states (`FUN_004f96f0`) | `FUN_00449050`, `FUN_004493e0`, `FUN_004495a0` |
| `FUN_00448c70` | A control event on one of the 11 figure slots (`0x77ad78`), else 0. Message 0x205: the user in that slot goes to `FUN_00443e10`, then to `FUN_00445a80` (its `+0xa8`) when it is the player (name after the `*` against `DAT_0077bbd8` `+0x8d`), else `FUN_00445c70` (`+0xa0`, `+0xa8`). Other messages: the selected slot is deselected (`FUN_00448050`); any other user is selected (bit 0x400, `DAT_0070ec1c`, `DAT_00798f48`, its `GetTickCount` in `0x798f4c`; fatal without a figure, line 0x135) and the buttons set. Returns 1 | passed by `FUN_0043fc20` (`0x43fdd1`) and `FUN_00441b70` (`0x441d4e`) |
| `FUN_00449330` | Event 1: `FUN_00449050`, 1 | table entry 1 (`0x70ec24`) |
| `FUN_00449350` | (`EDI` name, `ECX`, `EAX`): `FUN_00449050`; with join notices on (`DAT_0077bd54`, chatdlg.md `FUN_0043e780`), `%s has joined the channel.` with the character name, or the name when it has none, in colour 5 (`FUN_00447ab0`) | `FUN_004493c0` |
| `FUN_004493c0` | Event 2: `FUN_00449350`, 1 | table entry 2 (`0x70ec28`) |
