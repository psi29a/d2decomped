# Front-end screens and boxes (Src/MainMenus.cpp)

Traced 2026-11-24 by reading game.exe 1.14d, not run: the decompile, the
assembly for register arguments and immediates, the frontend record table
(`0x708d10 + index * 0x30`, layout in
[frontend-menu-table.md](frontend-menu-table.md)), the English string
tables for the TBL ids, and a byte scan for calls and pointers that
`game-calls.tsv` misses. These are the `Src\MainMenus.cpp` functions the
gap map listed as untouched on 2026-11-24, then the untouched Battle.net
account screens next to them (`0x434500..0x4378d0`) and the small helpers
they use. The source file name is `0x6d41c0`. d2d has no Battle.net, disc check or credits screen, so none
of them has a counterpart.

```sh
python3 tools/ghidra/fn.py 00431050 00430ef0 00430c80 00430a20 00431570 00431970 00431930
python3 tools/ghidra/fn.py 00431f70 00432080 00432100 00432350 004325b0
python3 tools/ghidra/fn.py 00433130 004331c0 004332b0 00433460
python3 tools/ghidra/fn.py 004361b0 00434e00 00436910 00435030 004365b0 004370c0
python3 tools/ghidra/fn.py 004378d0 00437730 00436d50 00437620 00436c10 00437550 00436ad0
python3 tools/ghidra/fn.py 00434700 00434500 0042f540 004f9710 004fb990 0042fdd0
python3 tools/ghidra/fn.py 0043c180 0043b8e0 0043b940 0043bf40 0043c540 0043c6a0
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
  `FUN_004fb990` returns its `+0x60` (fatal 0x256 / 599), the selected
  line of a list.
- `FUN_004f9710(control)` is its `+8` bit 0 (null is fatal, 0x33f). The
  account buttons' handlers return early without it.
- `FUN_0042e6a0(ECX)` sets the box callback `DAT_007795bc`, and
  `FUN_0042e6b0` sets `DAT_00779954` to 1.
- `FUN_0042fdd0` goes to character select: `FUN_00515020`, a teardown,
  `FUN_0043b080`.
- The Battle.net client is the object at `DAT_0077abf0`. Its methods are
  called by slot (`+0x68`, `+0x80`, `+0xb4` .. `+0xc0`); what each does
  inside the client isn't traced here.

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

- **`FUN_00434500`**, the cinematics screen's record 0x44 handler
  (`0x7099f0`; string 0x552a), is that button's own copy of the pattern:
  `FUN_004fad70`, then `FUN_004faec0` with `LAB_00434220` and
  `LAB_004342e0` (called again when it returns 0), then
  `FUN_00431870(arg)` with `EBX` `0x6d4894`. It returns 1.

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

Its only caller `FUN_004365b0`, called by the character creation OK
`FUN_004369f0` (`0x436aa8`), finishes the new character:

- When the save exists and the mode `DAT_007795ec` isn't 1 (Battle.net),
  it shows string 0x142d "That character name is already taken. Try
  another." in `FUN_00433460` (callback `FUN_0042f3f0`) and stops.
- A class 5 or 6 (`+0x1ee`) sets record `+0x1ef` bit 0x20. The name box
  goes into `+0xbd` (`"%s"`), cut at `+0xd5`. Bit 0x20 also sets
  `DAT_00779984`.
- With `+0x1ef` bit 4 it makes one call (not traced here) and stops.
- Otherwise, by the mode:
  - **0:** a teardown, the new character's header (`FUN_0043c540`,
    below), `FUN_0043c6b0`; `+0x35e` = 1, `+0x19` = 0, `DAT_007795e8` = 1.
  - **2:** the same with `FUN_0043c6a0` first, and `+0x35e` = 3.
  - **3:** the realm connection closed (`FUN_00449ad0`), then the same;
    `+0x3c1` = 1, 16 bytes from `0x6d4d88` into `+0x3a1`, `+0x3b1` zeroed
    (16), `+0x3c2` = flags bit 6, `+0x3cb` = the flags, `+0x35e` = 4,
    `+0x22d` and `+0xd5` = 0, `DAT_007795e8` = 3.
  - In those three, `+0x209` = 4 (0x804 with flags bit 4, and 0x100000
    added with bit 0x20), then the screen run ends (`FUN_004f9190`).
  - **1:** `FUN_00435e10`.

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
- Callers: `FUN_00434e00`, `FUN_00435030`, `FUN_004361b0` and
  `FUN_00436910` (below).

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
- Buttons: 0x5d (OK, `FUN_00435030`) and 0x5e (EXIT 0x13ed,
  `LAB_00432340`, which opens Account Settings, `FUN_004370c0`).
- Callers: `LAB_00432300`, `LAB_004371b0` (Account Settings' CHANGE
  PASSWORD), and `FUN_00435030` (a call, and a pointer at `0x4350ba`).
- ACCOUNT SETTINGS (record 0x25, handler slot `0x709420`) is
  `LAB_00432310`. It copies the account box into `+0x8d`, then calls
  `FUN_004370c0`.

**`FUN_004325b0`, the new-account notice** (only caller `FUN_00436910`,
below):

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

## The account screens

The Battle.net front end. Its handlers get 0 for CANCEL or Esc and the
control otherwise (`ret 4`).

**Log in** (`FUN_004361b0`): LOG IN (record 0x26), and Enter in the
account and password boxes (0x22, 0x23; record `+0x24`).

- 0 goes back to the title (`FUN_004336c0`).
- With the button `DAT_00779594` off (`FUN_004f9710`) it returns 0.
- It keeps the account and password (`FUN_00432080`, `DAT_0077958c`,
  `DAT_00779334`) and runs the login exchange (`FUN_0043c180`, below)
  with 0.
- On success it writes the account to the registry value `Diablo II\Last
  BNet` (`FUN_00415070`). In mode 1 (Battle.net) it goes to character
  select. Otherwise it goes there when a save exists (`FUN_00430bc0`,
  `FUN_0042fdd0`), else to character creation (`FUN_00435580`).

**CREATE NEW ACCOUNT** (`FUN_00434e00`, record 0x24):

- It keeps the account and password, then takes the save folder
  (`FUN_00407050`) up to its first `\` and asks `GetDiskFreeSpaceA`.
- With at least 5000 (sectors × bytes × free clusters), it goes on to
  `FUN_00433810`.
- Otherwise string 0x141d "not enough room" in records 0xde, 0xe1 and
  0xe0, or 0xf7, 0xf8 and 0xf9 on the character palette (`DAT_007795f0`).
  The callback is `FUN_0042fdd0`.

**The new account's OK** (`FUN_00436910`, record 0x57, and Enter in boxes
0x54, 0x55, 0x56):

- 0 goes back to the realm and login screens (`FUN_00435330`).
- The account (`DAT_0077958c`), password (`DAT_00779334`) and verify
  (`DAT_007795a8`) boxes are checked in turn:
  - an account of fewer than 2 units: 0x1460 "Account name must be at
    least 2 characters.";
  - more than 15: 0x1461;
  - the two passwords differ (`FUN_005264f0`): 0x1462 "Passwords don't
    match.";
  - a password of fewer than 2: 0x1463;
  - more than 20 (`0x4369c5`): 0x1464, which says "can't be more than 15
    characters" (bugs.md #136).
- An error shows in the popup `FUN_00433380` (flag 1), with the callback
  `LAB_004301a0`. If all pass, it shows the new-account notice
  (`FUN_004325b0`).

**Change Password's OK** (`FUN_00435030`, record 0x5d, and Enter in 0x59
.. 0x5c):

- 0 opens Account Settings (`FUN_004370c0`).
- It keeps the account and the new password (`DAT_00779340`) and copies
  the current one into `0x779550`.
- It runs the same checks on the account, new (`DAT_00779340`) and verify
  (`DAT_007795a8`) boxes (the 20 at `0x435105`). An error's callback is
  `FUN_00432100`.
- It clears `DAT_007795f4` and runs the login exchange with 1 (change
  the password first):
  - on success, `Last BNet`, the popups freed and `FUN_0042fdd0`;
  - on failure with `DAT_007795f4` set, the Change Password screen again.

**Account Settings** (`FUN_004370c0`):

- With the client's `+0x68` zero, it shows 0x14e3 ("You were
  disconnected from battle.net…") through `FUN_00433450` and stops.
- Otherwise the title screen's base, the title 0x2b65 "Account Settings"
  (record 0x135, Font30), and three Font16 lines (0x136 .. 0x138:
  0x2b66, 0x2b67, 0x2b68, change password / forgot password / change
  email).
- Buttons:
  - CHANGE PASSWORD (0x139, `LAB_004371b0`: `FUN_00432100`);
  - GET NEW PASSWORD (0x13a, `LAB_004371c0`);
  - CHANGE EMAIL (0x13b, `LAB_00437720`).
- Callers: `LAB_00432340`, `LAB_00432310`, `FUN_00435030`,
  `LAB_004374e0`, `LAB_00437710`, and the 0 path of the two SEND handlers
  below (`0x43773f`, `0x4378df`).

**GET NEW PASSWORD's SEND** (`FUN_004378d0`, record 0x12c, and Enter in
0x131, 0x134):

- 0 goes back to Account Settings, and the button `DAT_007799e4` must be
  on.
- The account (`DAT_007799dc`) and email (`DAT_007799e0`) are narrowed
  and passed to the client's `+0xbc`, once `+0x68` is nonzero.
- On success, 0x2b60 ("Your request for a new password has been
  received…") in the box `FUN_0042f540` (OK, `LAB_004370b0`). Otherwise
  0x14e3, then the title.
- `FUN_00436eb0` turns SEND on (`FUN_004f96f0`) when the account box has
  text and the email passes `FUN_00436c10`.

**CHANGE EMAIL's SEND** (`FUN_00437730`, record 0x14b, and Enter in
0x141, 0x144, 0x147, 0x14a):

- 0 goes back to Account Settings, and the button `DAT_007799a4` must be
  on.
- The boxes are the account (`DAT_007799bc`), the old email
  (`DAT_007799c0`), the new one (`DAT_007799c8`) and its verify
  (`DAT_007799c4`):
  - new and verify differ: 0x2b72 ("…two new email addresses match.");
  - old and new the same: 0x2b71;
  - each goes through `FUN_00403040` first (two 0x100 buffers,
    `LAB_0042f530`) and shows in `FUN_0042f540` (OK, `LAB_004334d0`).
- Otherwise all four are narrowed and passed to the client's `+0xc0`,
  once `+0x68` is nonzero. Success goes back to the realm and login
  screens (`FUN_00435330`). Failure shows 0x14e3, then the title.
- `FUN_004371e0` turns SEND on when the account has more than 2 units and
  all three emails pass `FUN_00436c10`.

**The email registration screen** (`FUN_00436d50`):

- The login exchange runs it when the client's `+0xb4` is nonzero
  (`0x43c448`), as a screen run (`FUN_004fa590`, `LAB_00436e90`). Then
  `FUN_00436ea0` returns `DAT_007799d8`; when it is set, the exchange
  closes the realm connection and goes back to the title.
- It clears `DAT_007799d8`, then adds the title screen's base and two
  Font16 texts: 0x2b55 "Enter a valid email address." (0x122) and 0x2b56
  (0x123, what an email gives).
- Fields:
  - 0x120, 0x124 "Email Address:" and the box 0x126 (`DAT_007799cc`);
  - 0x121, 0x125 "Verify Email Address:" and the box 0x127
    (`DAT_007799d0`).
  - Both boxes get `LAB_00436d40` through `FUN_004fdb00` (`+0x26c`).
- Buttons:
  - REGISTER (0x128, `DAT_007799d4`, `FUN_00437620`);
  - DON'T REGISTER (0x129, `LAB_00436c90`);
  - CANCEL (0x12a, `LAB_00436cd0`: sets `DAT_007799d8` and ends the run).
- It focuses and links the two boxes, then runs `FUN_00436cf0`.
- `FUN_00436cf0` (also from `LAB_00436d40`) turns REGISTER on when
  **either** box has text.
- `LAB_00436c90` asks again: 0x2b5e (no recovery without an email, and
  no second chance) in `FUN_0042f540`, with CONTINUE (0x2b61,
  `LAB_00436c70`) and CANCEL (0x13ef, `LAB_004334d0`).
- **REGISTER** (`FUN_00437620`):
  - the two boxes must match (`FUN_005264f0`), else 0x2b5b;
  - the first, narrowed, must pass `FUN_00436c10`, else 0x2b5c "Invalid
    Email Address" with 0x2b5d;
  - then it is passed to the client's `+0xb8` and the run ends.

**`FUN_00436c10`** (text in `EBX`) is the email test. It is 1 when the
text has an `@` that isn't first, a `.` after the `@` with at least one
character between them, and at least one character after that `.`. A
null text is 0.

**The connecting box** (`FUN_00437550`, called by `FUN_0043bf60` at
`0x43bff2`):

- It notes the time (`GetTickCount`, `DAT_007799b8`), frees the popups
  and sets `DAT_00779954` (`FUN_0042e6b0`).
- Records:
  - 0xe2 (background, made modal with 0) and 0xe3 (CANCEL,
    `LAB_00432060`);
  - 0xe4 (Font30) with 0x1433 "Connecting to Battle.net";
  - 0xe5 (`DAT_007799a8`, Font24) and 0xe6 (`DAT_007799ac`,
    FontFormal12).
- It sets `DAT_007799b0` to -1 and `DAT_007799b4` to 0, keeps ECX as the
  callback (`FUN_0042e6a0`), then runs `FUN_00436ad0` once.
- `FUN_00436ad0` (a tail jump at `0x437613`, and a call from
  `LAB_004374f0`) does the update. While `DAT_007799a8` exists, it reads the client's state
  (`+0x80`):
  - states 0..6 show 0x1433 .. 0x1439 in 0xe5 ("Connecting to
    Battle.net", "Querying gateway", "Connecting to the fastest server",
    "Accessing your account", "Checking versions", "Verifying CD-key",
    "Getting player profile");
  - 0xe6 gets 1 to 20 dots: ((now − `DAT_007799b8`) / 250) mod 20 + 1.

**The login exchange** (`FUN_0043c180`: ECX the record, EDX "create
the account first", stack "change the password first"; `ret 4`).
Callers: LOG IN (`0x4361f6`, 0 and 0), Change Password's OK (`0x435125`,
0 and 1), the new-account notice's OK (`LAB_00436280`, `0x436297`, 1 and
0) and `FUN_00434a00` (`0x434aa0`, 0 and 0). It returns 1 when logged in.

1. With the client's `+0x68` zero: 0x14e3 ("You were disconnected…") in
   `FUN_00433450`, going back to the title, and 0.
2. Already logged in (`DAT_0077ac08`): 1.
3. The PLEASE WAIT box (`FUN_0042f6e0`), then the client's `+0x28` into
   an error word.
4. **Create** (EDX nonzero, no error):
   - the client's `+0x60` with the account (`+0x8d`) and password
     (`+0xa5`), and a screen run checked by `FUN_0043b8e0`;
   - a cancel (`DAT_0077ac04`) frees the popups (`FUN_0042f880`) and
     returns 0;
   - then the result from `+0x64`. A nonzero result while still
     connected goes to the message handler (`FUN_0043bf40`): 2..8 as
     codes 0x14..0x1a, anything else as 0x13. That returns 0.
5. **Change** (stack nonzero, no error):
   - the client's `+0x9c` with the account, the old password
     (`0x779550`) and the new one (`+0xa5`), checked by `FUN_0043b940`;
   - a result from `+0xa0` other than 1, while connected, is code 0xe,
     and returns 0.
6. **Log in**, when connected with no error:
   - the client's `+8` with the account and password;
   - then the password at `+0xa5`..`+0xbc` is zeroed;
   - a screen run checked by `LAB_0043b890`. A cancel, or
     `FUN_0042f6d0` (`DAT_00779974`) set, frees the popups and returns
     0.
7. The client's `+0x28` again, and the popups freed. An error goes to
   `FUN_0043b9f0` and returns what it gives.
8. `DAT_0077ac08` = 1. When still connected and `+0xb4` is nonzero: the
   email registration screen (`0x43c448`). A cancel there
   (`FUN_00436ea0`) closes the realm connection, calls the client's
   `+0x10`, and goes back to the title with 0.
9. Still connected: `FUN_0042ff10(0)` (`DAT_00779980`) and 1. Otherwise
   code 0x12 to `FUN_0043b9f0`, and 0.

The three checks are alike. Each calls one client slot (`+0x64`, `+0xa0`;
`LAB_0043b890` `+0xc`) and ends the run (`FUN_004f9190`) when it reports
done. They also end it, after the client's `+0x10`, on a cancel, a lost
connection or `DAT_00779974`.

- `FUN_0043bf40` (code in `EAX`, `ECX`) passes `{EAX, ECX}` to
  `FUN_0043b9f0`.
- `FUN_0042f880` frees the popup controls, the same code as
  `FUN_0042f3f0`.

**The new character's header** (`FUN_0043c540`: ECX the name, DL the
class; stack the stat string's buffer, the flags word; `ret 8`).
`FUN_004365b0` passes record `+0xbd`, `+0x1ee`, `+0xed` and `+0x1ef`.

- It zeroes 0x14f bytes at `0x77ac10`, then writes a save header:
  - `+0` `0xaa55aa55`, `+4` version 0x60, `+8` size 0x14f;
  - `+0x14` the name (`FUN_004135d0`);
  - `+0x24` the flags with bit 0 set (and 0x20 for class 5 or 6);
  - `+0x28` the class, `+0x29` 0x10, `+0x2a` 0x1e, `+0x2b` level 1;
  - `+0x2c` and `+0x30` the time (`_time32`);
  - `+0x88`..`+0x97` the 16 bytes at `0x70ccc8`, `+0x98`..`+0xa7` 0xff.
- The buffer gets a stat string in char-select.md's layout ("`out` gets a
  stat string"):
  - the word 0xe, then 11 bytes from `0x70ccc8`, the class + 1, 11 bytes
    of 0xff;
  - level 1, the header's flags word, the word 0, three 0xff and a 0.
- d2d's writer (`components/d2s/d2s_write.hpp`) writes the same `+0x29`
  and `+0x2a`.

`FUN_0043c6a0` (ECX) sets header bit 2 (`0x77ac34 |= 2`) when ECX is
nonzero. Both calls (`0x43673a`, `0x4367e2`) pass 0, and both come before
`FUN_0043c540`, which zeroes the header (stubs.md).

**The gateway list's OK** (`FUN_00434700`, record 0x4b, and
`FUN_00435d50`, record 0x4a's click when `FUN_00431a60` is nonzero):

- It takes the gateway from the client's `+0x98`, and the list's
  selection (`FUN_004fb990`).
- With a selection, `BNGatewayAccess::SetCurGateway`, and its `Realm`
  written to `Diablo II\Preferred Realm`.
- Then a teardown and the title.

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

- **`FUN_0042f540`** (ECX title, EDX text; stack first label, second
  label, first handler, second handler, keys, Esc; `ret 0x18`) is the
  generic box. It patches the button records in the table before adding
  them:
  - **one button** (second label 0): 0xe7 (modal), then 0x11a with the
    first label and handler, and key 0x1b with Esc set, else 0xd with
    keys set, else none (record `+0x14`);
  - **two buttons:** 0x119 (modal), then 0x11b (first label, handler,
    0xd with keys) and 0x11c (second, 0x1b with keys);
  - then the title in 0x11d when given, and the text in 0x11e, or the
    taller 0x11f without a title.
  - A null first label is fatal (line 0x746).
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
| `FUN_0042e6a0` | Set the box callback `DAT_007795bc` | `FUN_00437550` |
| `FUN_0042e6b0` | `DAT_00779954` = 1 | `FUN_00437550` |
| `FUN_0042f540` | One- or two-button box | the registration and SEND handlers |
| `FUN_0042f6d0` | Returns `DAT_00779974` | `FUN_0043c180`, its three checks |
| `FUN_0042f880` | Free the popup controls (as `FUN_0042f3f0`) | `FUN_0043c180` |
| `FUN_0042fdd0` | Go to character select | `FUN_004361b0`, `FUN_00435030`, the disk box |
| `FUN_0042ff10` | `DAT_00779980` = ECX | `FUN_0043c180` |
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
| `FUN_00434500` | Cinematics button 0x44: disc check, then play | record 0x44 (`0x7099f0`) |
| `FUN_00434700` | Gateway list OK: set the gateway, Preferred Realm | record 0x4b (`0x709b40`), `FUN_00435d50` |
| `FUN_00434e00` | CREATE NEW ACCOUNT: disk space, then `FUN_00433810` | record 0x24 (`0x7093f0`) |
| `FUN_00435030` | Change Password's OK | records 0x59..0x5d |
| `FUN_00435d50` | Gateway list click | record 0x4a (`0x709b10`) |
| `FUN_004361b0` | LOG IN | records 0x22, 0x23, 0x26 |
| `FUN_004365b0` | New character: name check, record fields by mode | `FUN_004369f0` |
| `FUN_00436910` | New account's OK: the checks | records 0x54..0x57 |
| `FUN_00436ad0` | Connecting box: state text and dots | `FUN_00437550`, `LAB_004374f0` |
| `FUN_00436c10` | Email address test | `FUN_00436eb0`, `FUN_004371e0`, `FUN_00437620` |
| `FUN_00436cf0` | REGISTER on when either box has text | `FUN_00436d50`, `LAB_00436d40` |
| `FUN_00436d50` | Email registration screen | `FUN_0043c180` (`0x43c448`) |
| `FUN_00436ea0` | Returns `DAT_007799d8` (registration cancelled) | `FUN_0043c180` (`0x43c45c`) |
| `FUN_00436eb0` | GET NEW PASSWORD: SEND on or off | `LAB_00436f20`, `LAB_004370ab` |
| `FUN_004370c0` | Account Settings screen | `LAB_00432340`, `LAB_00432310`, `FUN_00435030`, `LAB_004374e0`, `LAB_00437710`, the SEND handlers |
| `FUN_004371e0` | CHANGE EMAIL: SEND on or off | `LAB_004372a0`, `LAB_004374d5` |
| `FUN_00437550` | Connecting box | `FUN_0043bf60` |
| `FUN_00437620` | REGISTER | record 0x128 (`0x70c4b0`) |
| `FUN_00437730` | CHANGE EMAIL's SEND | records 0x141..0x14b |
| `FUN_004378d0` | GET NEW PASSWORD's SEND | records 0x12c, 0x131, 0x134 |
| `FUN_0043b8e0` | Login exchange: the create check | `FUN_0043c180` (a screen run) |
| `FUN_0043b940` | Login exchange: the change check | `FUN_0043c180` (a screen run) |
| `FUN_0043bf40` | `{EAX, ECX}` to `FUN_0043b9f0` | `FUN_0043c180` |
| `FUN_0043c180` | The login exchange: create, change, log in, register | `FUN_004361b0`, `FUN_00435030`, `LAB_00436280`, `FUN_00434a00` |
| `FUN_0043c540` | New character: save header and stat string | `FUN_004365b0` |
| `FUN_0043c6a0` | Header bit 2 when ECX is nonzero (always 0) | `FUN_004365b0` |
| `FUN_004f9710` | A control's `+8` bit 0 | the account handlers |
| `FUN_004fb990` | A list's selected line `+0x60` | `FUN_00434700` |
