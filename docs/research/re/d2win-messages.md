# The D2Win window's message handlers (`.\Src\D2WinMain.cpp`) — game.exe 1.14d

The front end's controls (buttons, edit boxes, text boxes, scrollbars) get
their mouse and key messages from one table of window-message handlers.
`FUN_004f8fe0` adds it (`FUN_00506da0`) and `FUN_004fa550` removes it
(`FUN_00506e00`): 0x21 rows of 12 bytes at **`0x72dc48`** (net-join.md
"D2Win shutdown" has both, and the four kinds: 0 any message, 1 a command
id, 3 a key down, 2 a key up). The handlers are in `.\Src\D2WinMain.cpp` by
`game-srcfiles.tsv`. Read from the Ghidra decompile
(`python3 tools/ghidra/fn.py <addr>`) with the register arguments checked in
the asm. No oracle ran: all of it is **cited**.

d2d ports none of these handlers: its front end reads SDL events itself
(apps/d2d/frontend). The one thing a player would notice is missing: d2d
has no Ctrl+M music toggle (`git grep -n "Options Music" -- apps components`
finds nothing).

## The message record

Each handler takes one stack pointer (`ret 4`) to the message record that
`FUN_00420b00` builds (source-module.md) from the window, message, wParam
and lParam it is passed. `FUN_00451880` (net-join.md) passes the window
handle that `FUN_004f59a0` returns, and the message, wParam and lParam from
+4, +8 and +0xc of its own 0x10-byte queue record, a different layout whose
+0 is a frame stamp. The handler record's fields, as these handlers use
them:

- +0: the window, and the slot where a handler writes the control it
  passes the message to (`*record = control`);
- +4: the message, +8 wParam, +0xc lParam (a mouse message's x is the word
  at +0xc, y the word at +0xe);
- +0x18: handled, +0x1c: the result.

A handler that does not set handled ends in `FUN_00420290(record)`, as the
handlers in chat-commands.md do.

## The control list

The globals the handlers walk, set up by `FUN_004f8f10`:

- `DAT_007d55bc`: the first control. Controls are linked through +0x3c;
  +0 is the type, +8 the flags (bit 0 and bit 2 both set: enabled and
  shown, as the other D2Win docs read them).
- `DAT_007d55d4`: the modal control (frontend-mainmenus.md,
  `FUN_004fa470`). When set, the walks start there instead of at
  `DAT_007d55bc`.
- `DAT_007d55cc`: the edit box with the keyboard (`get_7d55cc`,
  unnamed-frontier.md).
- `DAT_007d55d0`: the button Esc presses.
- `DAT_007d55c4`: the depth of callbacks running. Each call into a
  control's callback adds one for its span, and the list free
  `FUN_004f9290` does nothing while it is above 0 (net-join.md).
- `DAT_007d55a4`, `DAT_007d55a8`: the mouse x, y (`FUN_004fa6b0` below).
- `DAT_007d55e4`: Ctrl held. `DAT_0087e974`: an open popup list (type 0xb).

The control's callbacks these handlers call: +0x24 mouse down, +0x28 mouse
up, +0x2c character, +0x30 key down, +0x38 an optional hit test.

## The table, in table order

| Row | Kind, code | Handler | What it does |
|---|---|---|---|
| 0 | 0, 0x200 mouse move | `FUN_004fa6b0` | Arms `TrackMouseEvent` (leave, flags 2) once (`DAT_007d55f0`); sets `DAT_0072dc3c` = 1 and calls `FUN_004f59f0(1)` (`os_cursor_hide` in client-frontier.md), which hides the Windows cursor only in fullscreen with video mode `DAT_007c8cb0` not 4, 5 or 6 (Glide, OpenGL, Direct3D) and otherwise does nothing (D2Win draws its own); stores the mouse in `DAT_007d55a4` / `DAT_007d55a8`. With a 3D driver (`FUN_004f6270`) a point past the window size − 1 is clamped and the OS cursor moved there (`SetCursorPos`) |
| 1 | 0, 0xa0 (non-client mouse move) | `FUN_004fa4c0` | `DAT_0072dc3c` = 0; when wParam is 2, `FUN_004f59f0(0)` shows the Windows cursor (under row 0's guard); handled 0, result 0 |
| 2 | 0, 0x2a3 (mouse leave) | `FUN_004fa4f0` | Clears `DAT_007d55f0` (tracking re-arms on the next move); `DAT_0072dc3c` = 0 unless `DAT_007d5590` is 4; `FUN_004f59f0(0)` (shows it, under row 0's guard); handled 0, result 0 |
| 3–6 | 0, 0x201 / 0x202 / 0x204 / 0x205 (left and right down, up) | `FUN_004fa860` | "Mouse buttons" below |
| 7 | 0, 0x102 (character) | `FUN_004f9b60` | "Characters and keys" below |
| 8–21, 23, 24, 26 | 3, keys 8, 0x25, 0x27, 0x24, 0x23, 0x2e, 0x26, 0x28, 0x21, 0x22, 0xd, 0x52, 0x4e, 0x48, 9, 0x20, 0x70 | `FUN_004f9bf0` | "Characters and keys" below |
| 22 | 3, key 0x1b (Esc) | `FUN_004fa760` | "Esc" below |
| 25 | 3, key 0x11 (Ctrl) | `FUN_004f9c80` | `DAT_007d55e4` = 1, then `FUN_00420290` |
| 27 | 3, key 0x4d (M) | `FUN_004fa160` | "Ctrl+M" below |
| 28 | 0, 0x1c (activate app) | `FUN_004f9f20` | Keeps whether the app is active in `DAT_0072dc44` (wParam non-zero); runtime-support.md and frontier-ring.md cite parts of it |
| 29 | 0, 0x112 (system command) | `FUN_004f9cc0` | By wParam & 0xfff0: 0xf100 and 0xf130 get handled 1 and nothing else; 0xf010 the same when `FUN_004f5a30` is non-zero; 0xf030 with `FUN_004f5a30` 0 resizes the window (`SetWindowPos`), the rest of that branch not covered here |
| 30 | 2, key 0x2c (Print Screen up) | `FUN_004fa840` | `FUN_004fa7a0` (the screenshot key, d2d: apps/d2d/session/update.cpp), then handled 1, result 0 |
| 31 | 2, key 0x11 (Ctrl up) | `FUN_004f9ca0` | `DAT_007d55e4` = 0, then `FUN_00420290` |
| 32 | 0, 0x20a (mouse wheel) | `FUN_004fa340` | "The wheel" below |

The message and key numbers are Windows' (WM_ and VK_ values by their
documented numbers); the table holds only the numbers.

### Characters and keys (`FUN_004f9b60`, `FUN_004f9bf0`)

The two have the same shape; the character handler uses +0x2c and
`FUN_004feae0`, the key handler +0x30 and `FUN_004ff050`.

1. With an edit box holding the keyboard (`DAT_007d55cc`), and either no
   modal control or the box in the modal's list: the box's own handler
   gets the character or key (ECX = the box, EDX = wParam). A non-zero
   result ends it.
2. Else each control from the modal (or the first control) with a
   callback at +0x2c / +0x30 gets the record (+0 = the control) until one
   returns non-zero.
3. Then `FUN_00420290`. Neither sets handled.

- **`FUN_004feae0`** (ECX = an edit box, EDX = the character) asserts a
  control (line 0x1f6) of type 1 (0x1f8). It needs the box enabled and
  shown, or it returns 0. A Tab on a box with flag +0x260 bit 2 only goes
  to the box's filter (+0x268), and the result is 0.
  - A character is refused (0) when `FUN_00502550` turns it down, unless
    it is one of 1, 3, 0x16 or 0x18, Enter on a box with flag bit 3, 0xe
    with bit 4, or 0xff with bit 5 (`FUN_004fda80`, ECX = the character's
    word: 1 for exactly 0xff). The filter can
    refuse any but those four control characters.
  - With a selection (`editbox_has_selection`, src-frontier.md), any
    character but 1, 3 and 0x18 first deletes it (**`FUN_004fe9f0`**: ESI
    = the box, asserted at lines 0x1c2 / 0x1c4; the text between the two
    selection ends goes, and the caret +0x25c moves to the lower end).
  - 1 calls `FUN_004fdd00`, the call a double click makes
    (src-frontier.md, `editbox_mouse`).
  - 3 copies the selection to the clipboard (**`FUN_004fdbb0`**, ECX =
    the text: it empties the clipboard and sets it to a `GlobalAlloc` copy
    as format 1, `CF_TEXT` by its documented value; it returns 0 when the
    clipboard can't be opened or the block allocated). 0x18 copies the
    same way and then deletes the selection.
  - 0x16 pastes: **`FUN_004fdb40`** returns the clipboard's format-1 text,
    up to 0x200 bytes copied to `0x841b80`, or an empty string. The paste
    is refused when it would pass the box's length limit (+0x48 − 1), and
    the filter sees each character. CR and LF are dropped unless bit 3 is
    set.
  - Any other character is inserted at the caret while the text is
    shorter than +0x48 − 1 and 0xff, Enter as CR LF.
  - Then the caret moves and the change callback (+0x26c) runs. It returns
    1.
- **`FUN_004ff050`** (ECX = an edit box, EDX = the key) asserts a control
  (0x300) of type 1 (0x302) and returns 0 when the box is not enabled and
  shown. Shift is `GetKeyState(0x10)`.
  - Backspace and Delete remove a character, or the selection when there
    is one (`FUN_004fe9f0`); a CR LF pair goes together.
  - Home / End / Left / Right / Up / Down move the caret, extending the
    selection with Shift. Up and Down use line moves only when bit 3 is
    set; else they act as Home and End.
  - Tab moves the keyboard to the next enabled box (+0x27c), or the
    previous one (+0x278) with Shift, through `focus_control_set`
    (unnamed-frontier.md). It returns 1.
  - Enter calls the box's enter callback (+0x264) with the text; a box
    without bit 1 returns 1 there. Esc calls it with 0 and returns 1.
  - F1 (0x70) returns 0. The other keys update the caret and the change
    callback, and return 1.

### Esc (`FUN_004fa760`)

When the Esc button `DAT_007d55d0` is set (and in the modal's list when
there is a modal), it presses it with `FUN_00500ab0`. That function asserts
type 6 (a button, line 0xbc), and when the button has a +0x24 callback, sets
+0x4c = 1 and calls it with the record; then the +0x28 callback, when set,
with a fresh record whose +0 is the button. Then `FUN_00420290`. With no Esc
button the key goes to `FUN_004f9bf0` like any other (a tail jump).

### Ctrl+M (`FUN_004fa160`)

Only with Ctrl held (`DAT_007d55e4`) and `FUN_00515060` non-zero (it
returns `DAT_008817fc`, the flag `ui_sounds_init` tests before it loads the
menu sounds, frontier-ring.md):

- music on (`FUN_00514d80`, `DAT_008817c0`): **`FUN_00514960`** stops it.
  With a song (`DAT_00881794`) it clears the playlist flag `DAT_00881798`,
  and when the song streams (`FUN_00515c30`), fades it with
  `FUN_00515c60(song, 0, 200)`.
- music off: `FUN_005148f0(1)` turns it on (music.md).
- Either way **`FUN_00514d90`** stores the new state (0 or 1) in
  `DAT_008817c0` and the registry value `Diablo II` / `Options Music`
  (`FUN_004150e0`). Then handled 1, result 0.

**`FUN_00515c60`** (ECX = a sample, EDX = a target, stack = a time) asserts
the sample (line 0x39e). Once sound is up (`FUN_00513b50`), under the lock
`0x88174c` it sets +0x4c = 1, +0x50 = the current volume (+0x38), +0x54 =
the target, +0x58 = `GetTickCount()` and +0x5c = that + the time: a volume
ramp from now to now + time.

### Mouse buttons (`FUN_004fa860`)

First the mouse-move handler `FUN_004fa6b0` with the same record. Then,
with a popup list open (**`FUN_005085c0`**: `DAT_0087e974` set, asserted
type 0xb at line 0x62, enabled and shown):

- **`FUN_005086a0`** (asserts type 0xb, line 0x90) runs the popup's
  selected row: row index +0x464, the callback the dword at index
  row × 0x21 + 0x31. It returns 1 when one ran.
- **`FUN_005085a0`** closes the popup (`FUN_004f9310`, then
  `DAT_0087e974` = 0).
- When a row ran, the handler returns there without `FUN_00420290`.

Then by message:

- down (0x201, 0x204): `DAT_007d55e0` = 1. Each control in turn is hit by
  its +0x38 test, or with none by the box test x (+0xc) ≤ mouse x <
  x + width (+0x14) and bottom (+0x10) − height (+0x18) ≤ mouse y < bottom.
  The first control hit whose +0x24 callback returns non-zero ends the
  walk; a hit control without one does not.
- up (0x202, 0x205): `DAT_007d55e0` = 0, and every control with a +0x28
  callback gets the record.
- The switch has an empty 0x203 (left double click) case, but the table
  has no 0x203 row, so it never comes here. Both paths above end in
  `FUN_00420290`.

### The wheel (`FUN_004fa340`)

The notches are the signed high word of wParam / 120 (0x78). With a popup
open (`FUN_005085c0`) the handler returns at once, without `FUN_00420290`.
Else:

1. Of the enabled, shown type-5 controls (scrollbars) whose range is above
   0 (`FUN_00507ef0`, `scrollbar_range`), **`FUN_004fa200`** keeps the one
   nearer the mouse.
2. That scrollbar moves one step (**`FUN_00508030`**): if one was found
   and it returns non-zero, the handler ends. `FUN_00508030` always returns
   0, so this never happens.
3. So each button (type 6) in turn gets **`FUN_00500b50`** (ECX = the
   button, EDX = the notches) until one returns non-zero. That function
   also always returns 0, so every button is tried.
4. Then `FUN_00420290`.

- **`FUN_004fa200`** (stack: the control kept so far, then the new one;
  `ret 8`): either null gives the other. For each it takes dx, how far the
  mouse is outside [x, x + width]: negative to the left, positive to the
  right, 0 inside. dy is the same for [bottom − height, bottom]. A control
  is "level" when dy is 0 and dx ≤ 0.
  - Both level: the one with the larger dx; a tie goes to the new one.
  - One level: that one.
  - Neither: the kept one when its dx² + dy² is smaller, else the new one.
- **`FUN_00508030`** (ECX = the record, EDX = a scrollbar, stack = the
  notches) asserts type 5 (line 0x99). With +0x58 clear, a positive count
  takes one off the position (+0x4c) and sets +0x40, and a negative count
  adds one and sets +0x44, whatever the count's size. The position stays
  in 0 .. +0x48. Then the linked text box (+0x54) scrolls to the position
  (`FUN_004fba30`) and the +0x5c callback runs with the record. Returns 0.
- **`FUN_00500b50`** (ECX = a button, EDX = the notches) asserts type 6
  (line 0xe2). An enabled, shown button with +0x44 and +0x48 clear, and
  +0x40 bit 3 for a positive count or bit 4 for a negative one, runs its
  +0x34 callback with a fresh record. Returns 0.

## The cursor draw (`FUN_004f97e0`)

Called from the per-frame draw `FUN_004f98e0` after the controls' draws
(0x4f9a5d). With a cursor cel (`DAT_007d5594`, set by `set_7d5594`:
`FUN_004359d0` passes the `ui\CURSOR\ohand` handle `0x77971c`,
frontend-menu-table.md) and the mouse in the window (`DAT_0072dc3c`, set by
row 0 and cleared by rows 1 and 2):

- it keeps the mouse position in `DAT_007d5598` / `DAT_007d559c`;
- it zeroes a 0x48-byte draw record with +0 = `DAT_007d55a0` (only
  `FUN_004f8f10` writes it, with 0) and +0x34 = the cel;
- it calls `FUN_004f6480(record, x, y − 1, 0xffffffff, 5, 0)`, with a
  negative x or y taken as 0 first.

## A getter (`FUN_004fa490`)

`FUN_004fa490(p)` returns `*p`, or 0 for a null `p`. Its two calls are at
0x430458 and 0x4304c8, in code after `0x430400` that `game-functions.tsv`
does not list as a function.

## Re-check

```sh
python3 tools/ghidra/fn.py 004f9b60 004f9bf0 004fa760 004fa160 004fa860 004fa340 004fa200 004f97e0
uv run --project tools/emu python tools/emu/disasm.py 4fa160 30   # the Ctrl+M branches
# the 0x72dc48 table: 33 dwords triples (kind, code, handler) in game.exe's .rdata
```
