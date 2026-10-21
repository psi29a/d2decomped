# Key bindings and Configure Controls — game.exe 1.14d

The key config (`.\UI\CmdTbl.cpp`, 0x469000..0x46ad00) and its screen,
Configure Controls (UI 0xb, 0x4a43e0..0x4a6800), which the game menu's
Options → CfgOptions (`0x47f400`) opens. d2d: `apps/d2d/keyconfig.hpp`
(the table, its rules, the files), `apps/d2d/controls.{hpp,cpp}` (the
screen), wired in `town.cpp`. Checked against game.exe's own code:
`tools/emu/keys.py` against `tests/test_keyconfig --dump`. Every
assignment from the defaults (57 functions × 2 slots × keys 0..0x104)
and every key's name match.

## The table

114 entries of 10 bytes at `0x7a6f90`: `{i32 function, u16 key, i32 slot}`,
two per function. Slot 1 is the screen's first key column, slot 0 the
second. Keys are Windows virtual keys. `0xffff` is none, and
0x100..0x104 are the mouse's third, fourth and fifth buttons, then wheel
up and wheel down. A copy at `0x7a6b18` is what Cancel goes back to
(`FUN_00469d20` saves it, `FUN_00469d50` restores it).

The functions (0..0x38) have handlers in a 12-byte table at `0x712698`:
`{press, release, flag}`. Run (0x22), Stand Still (0x24) and Show Items
(0x25) have a release handler and are held; Screen Shot (0x2a) has only
a release handler. 0x38 is Esc (the game menu). It isn't on the screen
and can't be assigned. `keyconfig.hpp`'s `Action` names them all.

The defaults are the static table at `0x712220` (`keys::defaults()`,
byte for byte). `FUN_004698d0` tries `DATA\LOCAL\CMD\<lang>\default.key`
from the MPQs first, but 1.14d's is version 0x24 / size 0x466 and the
header check (`WS`, 0x25, 0x47a) refuses it.

## Files

- `<save dir>\<name>.key`: u16 0x25, then the table (0x476 bytes),
  written by `FUN_00469780` on Accept.
- `<save dir>\default.key`: `WS`, u16 0x25, u16 0x47a, then the table.
  The same Accept writes it: the last accepted table becomes a new
  character's start.
- At a game's start (`FUN_0046aae0`) the character's file is read. It
  must be 0x476 bytes and version 0x25. Every function 0..0x38 must be
  in it, and no key may sit on two entries. Otherwise default.key from
  the save dir is used, else the static table, and the result is
  written as `<name>.key`.

## Assigning (FUN_00469c20)

`(function, slot, key, &message)`:

1. Function 0x38 or more: refused.
2. The key isn't assignable (`FUN_00469ae0`): message 0xf8b ("Cannot
   assign to this key"). Not assignable: the left and right mouse
   buttons, Escape, the arrows, the Windows keys, Num Lock, and the other
   codes listed there. Letters, digits, F-keys, the numpad and
   punctuation are assignable.
3. The wheel on a function with a release handler: 0xf8a ("Cannot
   assign to mouse wheel", `FUN_00469420`).
4. Print Screen on anything but Screen Shot: 0xf8b.
5. The key leaves the first entry that had it, then goes to
   (function, slot). That entry is always there, so the call returns 1.

Delete clears (function, slot) (`FUN_00469d70`).

## Key names (FUN_00469de0)

A string id per key, or the key's own character for letters and digits.
Unassigned is 0xeb2 ("None"). The rest run 0xeb3..0xf14: Mouse 1..5,
the wheel, Enter, Shift, F1..F24, Num Pad 0..9 and the punctuation
(`keys::name_id`). The skill picker's hotkey labels use these names
too (`FUN_00495410`, `FUN_00495dc0`).

## The screen

The panel sits at `((W-0x26c)/2, (H-0x1a4)/2)`, (90, 90) at 800×600. It
is drawn in font 0xd (FontInGameChat):

- A dark box, 0x26c × 0x171 (`FUN_0046efd0`, mode 1). A `boxpieces`
  frame (`FUN_00452e50`) runs round `{90, 93, 707, 458}` and round the
  buttons' strip `{90, 413, 707, 458}`.
- The headers "Function", "Key/Button One" and "Key/Button Two" (0xf51..0xf53)
  sit at x 108, 298 and 488, baseline 120, in gold (4). Each column is
  `(W - 180 - 0x31) / 3` = 190 wide.
- 15 rows, 18 apart, baselines from 142. The row lists are at
  `0x724268` (classic: 0x33 rows) and `0x724468` (expansion: 0x3e
  rows), `{function, name string, pad}`; 0x39 marks a gap. The
  selected row has a dark bar (x 108, 570 × 18). A name is white, or
  blue while it's being edited. The keys sit at x 300 (slot 1) and
  500 (slot 0). Their colour (`FUN_004a4fa0`): blue on the selected
  row's chosen column (or both while editing), red when the function
  has no key at all, gold when the key is set, grey when it's empty.
  While editing, the edited key blinks: it is hidden when
  `frame & 0xf <= 4`.
- The scroll bar is `textslid`. The trough is frame 0xd ×21 at x 679,
  bottoms from 149. The thumb is frame 0xe, 6 cels classic or 5
  expansion, at y `(0xf6 - 12n) * scroll / (rows - 15) + 0x2f + 90`.
  The arrows are frames 0xb / 9 (pressed) at bottom 137 and 0xa / 8
  at bottom 395.
- Three buttons (`0x7246d8`, 26 bytes each: string, handler, colour,
  box): Cancel (0xf86, red), Default (0xf84, blue) and Accept
  (0xf85, green). Each centres in a third of the panel's width, with
  its text plus 10 either side, y 419..457 and baseline 441. A button
  is gold until the mouse is over it, then its own colour.
- A refusal shows for 2 s, red, centred, at baseline 489.

Input while not editing: Up / Down move the selection past the gaps,
Left picks slot 1 and Right slot 0, Delete clears, Enter edits, and Esc
is Cancel. Enter, Delete and Esc only act on a fresh press: after an
edit ends with Enter, Delete, Backspace or Space, the next press only
re-arms them (`0x724728`). The wheel scrolls. With the mouse, a press
in the list selects the row and column (x < 488 is slot 1). Releasing
on the same row and column edits it. Dragging past the list's edges
moves the selection. The arrows scroll 1 row and the trough scrolls 14,
every 50 ms while held. The thumb follows the drag.

While editing: the next key goes in, and Esc stops without a change.
The mouse's third, fourth and fifth buttons and the wheel go in too.
A left click ends the edit (`0x4a6000`).

The buttons' handlers:

- Cancel (`0x4a5040`): restores the copy, closes UI 0xb and goes back
  to the menu (`FUN_0047e090(0, 1)`).
- Default (`0x4a5010`): `FUN_00469a60`, the defaults.
- Accept (`0x4a5020`): `FUN_00469d40`, which saves both files and
  rebuilds the mouse bindings (`FUN_004694a0`); then UI 0xb closes and
  the game goes on (`FUN_0047e200(1)`).

A local game pauses while UI 0xb is up (menu.md).

## d2d's ceilings

- Every function acts:
  - the screens: Party (party.md), Hireling (hireling.md), Help
    (help.md), the Message Log (a flag 1.14d never draws) and the Skill
    Speed Bar (the right hand's skill picker, `0x7c07fc` = 0);
  - the automap's options (automap.md "Options"); Center Automap does
    nothing, since d2d's mini map box never moves;
  - Run (`0x469000` / release `0x4691c0`): runs while held, back to a
    walk on release unless the toggle runs (C→S 0x53 / 0x54).
  - Stand Still (`0x469080`): a flag (`0x7a0664`). While it's held, a
    click on the ground uses the left skill there and doesn't step.
  - Clear Screen (`FUN_0044c6b0`): every closable panel and the automap
    shut.
  - Clear Messages (`FUN_004a01e0`): every chat line's time runs out.
  - Previous / Next Skill (`FUN_004aa740`, ∓1): from the hotkey last used
    (`0x7c0708`), round the 16 slots to the next right-hand skill that can
    be used.
  - Screen Shot (release `0x4fa7a0`): at most once a second,
    `Screenshot%03d.jpg`. d2d writes PNG to its screenshots folder.
  - Show Portraits (`0x493840`): toggles the party portraits.
  - Say 1..7 and Retreat (`0x468f80..0x468ff0`): C→S 0x3f with phrase
    0x19..0x20. The server's handler (`0x54c070` → `FUN_00553380`) sets
    it on the player (+0x6e) and sends it to everyone as S→C 0x2c. Each
    client plays the class's voice (`FUN_004cb9c0`): the class sound
    table at `0x72a008`, +0x20 (amazon_chat_a_1, 2937), `- 0x32 +
    phrase * 2`, one of its group's two takes. English shows no text
    (`FUN_004a0200` only does for languages 6, 7 and 9).
- Default is the static table. game.exe's is whatever `0x712220`
  holds, which the save dir's default.key overwrites when a character's
  own file was refused (`FUN_004698d0(1)`).
- Unverified: the skill picker shows a hotkey's first key, else its
  second. Which one game.exe shows isn't traced.
- d2d plays the Say voice itself and sends 0x3f to a host. game.exe's
  own player hears it back from the server. The 75-tick repeat guard on
  the voice isn't kept.
- Screen Shot fires on the key's press, not its release.
- Auto-repeat: game.exe skips a held key's repeats (lParam bit 30).
  d2d gets no repeats on the screen's keys anyway.
