# Front-end screens and boxes (Src/MainMenus.cpp)

Traced 2026-11-24 by reading game.exe 1.14d, not run: the decompile, the
assembly for register arguments and immediates, the frontend record table
(`0x708d10 + index * 0x30`, layout in
[frontend-menu-table.md](frontend-menu-table.md)), the English string
tables for the TBL ids, and a byte scan for calls and pointers that
`game-calls.tsv` misses. These are the `Src\MainMenus.cpp` functions the
gap map listed as untouched on 2026-11-24. The source file name is
`0x6d41c0`. d2d has no Battle.net, disc check or credits screen, so none
of them has a counterpart.

```sh
python3 tools/ghidra/fn.py 00431050 00430ef0 00430c80 00430a20 00431570 00431970 00431930
python3 tools/ghidra/fn.py 00431f70 00432080 00432100 00432350 004325b0
python3 tools/ghidra/fn.py 00433130 004331c0 004332b0 00433460
```

Screen helpers, from other docs:

- `FUN_0042f430(index)` adds a record to the screen
  ([char-create-table.md](char-create-table.md)), and `FUN_0042f480`
  tears the screen down.
- `FUN_0042f3a0(index)` is the same for a second, popup list
  (`DAT_00779450`, count `DAT_00779948`, at most 64). `FUN_0042f3f0` frees
  every popup control and empties it.
- `FUN_004fa470(control, flag)` sets the modal control `DAT_007d55d4` and
  `DAT_007d55e8`. `FUN_004fa420(control, on)` sets or clears its `+8`
  bit 3.
- `FUN_00524a30(id)` is a TBL string. `FUN_004fc9b0` adds a line to a
  text box, its first stack argument the colour
  ([chatdlg.md](chatdlg.md)). `FUN_004fcf80` adds a narrow line in
  colour 4 ([frontend-text-box.md](frontend-text-box.md)).
- `FUN_004fb950` returns a text box's `+0x58` (kind 4 only, else fatal
  line 0x24a / 0x24b), which the credits use as its line count.

Font pairs used below (`{font, colour}`, font ids in
[tcpip-menu.md](tcpip-menu.md)): `0x7089ac` Font42, `0x7089b4` Font30,
`0x7089bc` Font24, `0x7089c4` Font16, `0x7089dc` FontFormal12.

## The credits

The title's CREDITS button (`LAB_004312c0`) tears the screen down, loads
`palette\sky`, adds records 0x29 and 0x2a, then three text boxes: left
`DAT_0077995c`, right `DAT_00779960` and centre `DAT_00779964`. They are
records 0x2b, 0x2c, 0x2d, or 0x2f, 0x30, 0x31 with the expansion
(`FUN_00408f20`). Then it adds 0x2e (`DAT_00779968`) and fills the boxes
with `FUN_00431050` (`0x43135f`).

`FUN_00431050` reads the text from `FUN_00430cd0`, the credits text
loader (bugs.md #105). The text is a run of wide strings, and the loader
gives their total length in units. No text is fatal (line 0x141b).

1. 50 blank lines go into each of the three boxes, so the text starts
   below the screen.
2. A string starting with `*` is a heading. The rest of it goes into the
   centre box in colour 1, and a blank line into the left and right
   boxes.
3. Otherwise the strings come in pairs. A first string that doesn't
   start with an ASCII letter ends the credits. Its next string goes
   with it unless that one starts with `*`.
   - With both strings, the first goes into the left box and the second
     into the right (`FUN_004fd060`).
   - With one string, it goes into the centre box.
   - Either way, a blank line follows in the centre box. Pairs repeat
     until the next `*` string.
4. After each heading or run of pairs, `FUN_00430ef0` pads the boxes.
   It brings the right box up to the left and the left up to the right.
   Then it brings both up to the centre and the centre up to them, and
   adds one more blank line to each.
5. At the end, `FUN_00430c80` adds 50 blank lines and `The End`
   (`0x6d41dc`) to the centre box. It keeps the centre's line count in
   `DAT_0077996c`, which `LAB_004341f0` reads (`0x4341fe`).
6. The text is freed (line 0x146f) and the function returns 1.

## The disc prompts

They are callbacks: the cinematics buttons (`FUN_00434320` and its
copies after it) pass `LAB_00431a00` to `FUN_004fad10`, and `LAB_00434220`
and `LAB_004342e0` to `FUN_004faec0`. When those two call them isn't
traced here.

- **`FUN_00431570(ECX expansion)`**, popup records:
  - 0xde (the PopUpOK background, modal), 0x3c (OK) and 0xd9 (Font16
    text);
  - the text is string 0x1430 "Please Insert the Diablo II", then 0x5578
    "Expansion Disc" (ECX nonzero) or 0x1431 "Play Disc".
  - `LAB_004342e0` calls it with ECX 1. `LAB_00434220` builds the same
    box inline with 0x1431.
- **`FUN_00431970`** is the cinematics disc's box:
  - records 0xd6 (background, modal), 0x3a (CANCEL), 0x3b (OK) and 0xd9;
  - the text is 0x1430, then 0x1432 "Cinematics Disc".
  - `LAB_00431a00` builds it, clears `DAT_007798d0` and `DAT_007798d4`,
    and runs the box (`FUN_004fa590` with the check `LAB_00431950`). It
    returns `DAT_007798d4`.
  - `LAB_00431950` ends the run (`FUN_004f9190`) once either flag is set.
- **The buttons** (record `+0x20`):
  - OK (0x3b `LAB_00431920`, 0x3c `LAB_00431910`) sets `DAT_007798d4`.
  - CANCEL is `FUN_00431930(arg)`, reached only through record 0x3a's
    handler slot (`0x709810`). It calls `FUN_00431600(arg)` (the
    cinematics screen again) and sets `DAT_007798d0`.

## The realm list

`FUN_00430a20` frees the realm list `DAT_00779988`, with count
`DAT_0077998c` ([src-frontier.md](src-frontier.md) `realm_list_load`).

- For each entry it frees the description `+4` (line 0x113a, fatal
  0x1139 when null) and the name `+0` (0x113c, fatal 0x113b). Then it
  frees the entry (0x113e) and the array (0x1141).
- It clears `DAT_00779988` but leaves the count, and returns 1.
- Callers: `realm_list_load` (`0x4340d8`), a jump at
  `LAB_00437a60`, and the end of `FUN_00437b90` (`0x437bdc`), which first
  frees the list `DAT_00779de8`.

## Character creation: a name already taken

`FUN_00431f70` is 1 when a save for the typed name exists:

- the save folder (`FUN_00407050`, 0x400) and the name box
  `DAT_0077934c`'s text, narrowed (`FUN_005263e0`, 0x100);
- `"%s%s.d2s"` (`0x6d4230`);
- `CreateFileA` for reading (`OPEN_EXISTING`, flags 0x8000000). A handle
  is closed and gives 1; a failure gives 0.

Its only caller `FUN_004365b0` (read here only up to `0x436607`) shows
string 0x142d "That character name is already taken. Try another." in
`FUN_00433460` (callback `FUN_0042f3f0`) when it is 1 and the mode
`DAT_007795ec` isn't 1 (Battle.net).

## Battle.net accounts

**`FUN_00432080(ECX account box, ESI password box)`** copies either
box's text, when given, into the record `DAT_007795d4` with
`FUN_00526f70` (0x100):

- the account into `+0x8d`;
- the password into `+0xa5`, then its hash (`FUN_00414150`) into
  `+0x3c3` and its length (`FUN_00413750`) into `+0x3c7`.
- `FUN_004359d0` zeroes `+0xa5`..`+0xbc` when the front end ends
  ([net-join.md](net-join.md)).
- The chat screen looks for a typed word with that hash and length
  through `DAT_0077bbd8` ([chatdlg.md](chatdlg.md)). That this is the
  same record is likely, not traced: both are the record a screen run
  gets in ECX, and both read the screen object at `+0x225`.
- It has four callers in `game-calls.tsv`, the account screens (not
  traced here).

**`FUN_00432100`, the Change Password screen:**

- The title screen's base (`FUN_0043c4f0`), then records:
  - 0x15, 0x16, 0x18, 0x19: the field backgrounds;
  - 0x1a, 0x1b, 0x1d, 0x1e: labels 0x1468 "Account Name", 0x2b2c
    "Current Password", 0x2b2d "New Password" and 0x2b2e "Verify New
    Password";
  - the title 0x2b2b "Change Password", in record 0x21 with the
    expansion, else 0x1f.
- Edit boxes:
  - 0x59 (`DAT_0077958c`), the account: a filter (`FUN_004fdad0`), `+0x48`
    = 0x10 (`FUN_004fdd50`), and the text from record `+0x8d`
    (`FUN_004ffe10`);
  - 0x5a (`DAT_00779334`, filter `LAB_0042ff00`), 0x5b (`DAT_00779340`)
    and 0x5c (`DAT_007795a8`), the passwords, with `+0x48` = 0xd.
  - The focus goes to the account box when record `+0x8d` is empty, else
    the current password. Each box is then linked to the next (a call at
    `0x4322b0`, `0x4322c1`, `0x4322d2`, `0x4322e3`): account, current,
    new, verify, then account again.
- Buttons: 0x5d (OK) and 0x5e (EXIT 0x13ed, `LAB_00432340`, which calls
  an account screen's builder, not traced here).
- Callers: `LAB_00432300`, `LAB_004371b0`, and an account screen (a call
  and a pointer, not traced here).
- Record 0x25's handler (`0x709420`) is `LAB_00432310`. It copies the
  account box into `+0x8d`, then calls the same builder.

**`FUN_004325b0`, the new-account notice** (one caller in
`game-calls.tsv`, an account screen):

- The title screen's logo records 6 and 7 (each passed on with 0,
  `0x4325c5`, `0x4325d7`) and 8, and `palette\sky`.
- Popup records:
  - 0x4d, the 593x451 background;
  - 0x63 OK (`LAB_00436280`) and 0x64 CANCEL (`LAB_00436300`);
  - 0x5f, Font42: 0x146b "PLEASE READ";
  - 0x60, 0x61 and 0x62, FontFormal12: 0x146c (inactive accounts are
    deleted unless logged in 2 hours within the first 48), 0x146d (then
    once every 90 days), and 0x2b77 "PLEASE WRITE DOWN YOUR PASSWORD.".
- 0x60 and 0x61 get `FUN_004fb740(-(height / 10), 0)`, the height being
  FontFormal12's (`FUN_00501a40` under font 5).

**`FUN_00432350`** (code in `EAX`) is 1 for 2, 0xa, 0xb, 0xc, 0xd, 0x7e
and 0x7f, else 0. Its only caller `FUN_00432400` reads the realm server
connection:

- 1 when `DAT_00779994` is set;
- 2 with no event (the call at `0x43240f` returns 0), or when
  `WaitForSingleObject(event, 0)` fails;
- 3 on a timeout (0x102);
- once signalled, 2 when `mcp_result` (`FUN_00449c00`) is one of those
  codes, else 0.

## Message boxes

Each frees the popup controls, adds a background (made modal), an OK
button and text boxes, and keeps the callback in `DAT_007795bc`. The
OK buttons' handler `LAB_00432060` calls it when set. `FUN_004330a0`
writes each text, a line per `\n`, in colour 0 or 4.

| Function | Arguments (`ret`) | Records |
|---|---|---|
| `FUN_00433130` | ECX title, EDX text, stack callback (`ret 4`) | 0xe7 410x245, OK 0xe8, 0xe9 title (Font30), 0xea text (Font24) |
| `FUN_004331c0` | ECX title, EDX text, stack second text, callback (`ret 8`) | 0xeb 485x290, OK 0xec, 0xed title (Font30); with a second text 0xef (Font24) and 0xf0 in colour 4 (Font16), else 0xee |
| `FUN_004332b0` | ECX, EDX, stack two more texts, callback (`ret 0xc`) | 0xf1 485x290, OK 0xf2, 0xf3 title (Font30), 0xf4, 0xf5 (Font24), 0xf6 in colour 4 (Font16) |
| `FUN_00433460` | ECX text, EDX callback (`ret`) | the character palette's 0xf7 264x176, 0xfa (labelled CANCEL), 0xf9 text (Font24) |

- The first three are called by the Battle.net message handler
  `FUN_0043b9f0` (`0x43bdea`, `0x43be8d`; `0x43beab`; `0x43bbb2`).
- `FUN_00433460` is called by `FUN_004365b0` (`0x4365e1`, above) and
  the character creation OK `FUN_004369f0`. That one shows (`0x436a8b`)
  string 0x141d, "There is not enough room on your Hard Disk…", when
  the product of three of `GetDiskFreeSpaceA`'s outputs is below 5000
  (`0x436a72`).

## The functions

| Address | Reading | Callers |
|---|---|---|
| `FUN_00430a20` | Free the realm list | `realm_list_load`, `LAB_00437a60`, `FUN_00437b90` |
| `FUN_00430c80` | Credits: 50 blank lines and `The End` | `FUN_00431050` |
| `FUN_00430ef0` | Credits: pad the three boxes | `FUN_00431050` |
| `FUN_00431050` | Credits: fill the boxes | `LAB_004312c0` |
| `FUN_00431570` | Play or Expansion disc box | `LAB_004342e0` |
| `FUN_00431930` | Cinematics disc box: CANCEL | record 0x3a (`0x709810`) |
| `FUN_00431970` | Cinematics disc box | `LAB_00431a00` |
| `FUN_00431f70` | Does a save for the typed name exist | `FUN_004365b0` |
| `FUN_00432080` | Keep the account, password and its hash | four account screens |
| `FUN_00432100` | Change Password screen | `LAB_00432300`, `LAB_004371b0`, an account screen |
| `FUN_00432350` | Realm server failure codes | `FUN_00432400` |
| `FUN_004325b0` | New-account notice | an account screen |
| `FUN_00433130` | Message box: title and text | `FUN_0043b9f0` |
| `FUN_004331c0` | Message box: title and one or two texts | `FUN_0043b9f0` |
| `FUN_004332b0` | Message box: title and three texts | `FUN_0043b9f0` |
| `FUN_00433460` | Character palette box: one text | `FUN_004365b0`, `FUN_004369f0` |
