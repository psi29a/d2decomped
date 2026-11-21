# In-game edit box (`UI/editbox.cpp`) — game.exe 1.14d

The client's one-line number box: the stash and trade gold box's amount
([store.md](store.md) "Stash gold, button hovers"). Two classes, each
derived from `UIWidget` (RTTI at the vtables' locators):

- `EditBoxImplementation`, vtable `0x6da77c` (locator `0x70026c`), built
  by `FUN_004bbbb0` (`editbox_ctor`, [ui-module-frontier.md](ui-module-frontier.md)).
- `EditBoxWrapper`, vtable `0x6da7cc` (locator `0x7002b8`), built by
  `FUN_004bbd80` (vtable stored at `0x4bbdb3`). It allocates the
  implementation (0x5a bytes, `FUN_00413020`), passes its eight arguments
  to `FUN_004bbbb0`, keeps it at `+8` (`0x4bbdf7`) and forwards each slot
  to it.

`UIWidget`'s own vtable (`0x6da6c0`) has twelve slots, `+0x00` .. `+0x2c`,
each `0x6877fe` (the pure-call stub). Both classes add seven more, to
`+0x48`. File string `.\UI\editbox.cpp` at `0x6da760`. Read from the
disassembly (capstone over game.exe) and the vtables' bytes; no oracle
was run.

## The implementation's fields

From the constructor `FUN_004bbbb0` and the slots below, in offset order.

| Offset | Field | Evidence |
|---|---|---|
| `+0x04` | a character filter: when set, the key handler calls it with EDX = the event instead of its own test (`0x4bb666`..`0x4bb673`). The constructor sets 0 | `FUN_004bb620` |
| `+0x08`, `+0x0c` | x, y as given; `+0x10` = x + 1, `+0x14` = y | constructor |
| `+0x18` | width: the argument, or (the width of `"0000000000"` / 10) × (max + 2) | constructor; slot `+0x40` |
| `+0x1c` | lines: the argument, or 1 | constructor |
| `+0x20` | digits only: the key handler tests `_isdigit` (`0x686f0f`) when set, else `FUN_00502550`. The constructor sets 1 | `0x4bb677`..`0x4bb68d`; slot `+0x48` |
| `+0x24` | when 0, a key with bit 30 of the event's `+0xc` set (a repeat) is swallowed (`0x4bb62c`..`0x4bb63d`). The constructor sets 0 | slot `+0x44` |
| `+0x28`, `+0x2c` | two text buffers of (max + 1) u16s, zeroed (alloc lines 0x5d, 0x5e; freed at 0x8e, 0x93) | constructor, destructor |
| `+0x30` | the largest value | constructor; slot `+0x2c` |
| `+0x34` | u16: the most characters | constructor |
| `+0x36` | the caret | slots `+0x24`, `+0x28`, `+0x38` |
| `+0x3a` | the text's length | slots `+0x38`, `+0x3c` |
| `+0x3e` | set by a press, cleared by the release | slots `+0x0c`, `+0x10` |
| `+0x42` | the callback for Enter (0xd) and Esc (0x1b). The key handler first swallows a key above 0x7f (and a repeat when `+0x24` is 0), then takes backspace (8) and 0x2e itself. With no filter (`+0x04` 0) and not digits only (`+0x20` 0), Enter or Esc calls `+0x42` and returns 1 (`0x4bb68b`..`0x4bb69e`, `0x4bb91d`). Otherwise the filter or `_isdigit` decides: a key it refuses returns 0 when it is a space, Esc or Enter, so the key stays with the menu, and 1 for any other (`0x4bb75a`, `0x4bb903`..`0x4bb922`) | constructor |
| `+0x46` | a count of draws: the draw adds 1 (`0x4bb21d`); the caret `+0x4a` flips when it is a multiple of 20 while shown and of 10 while hidden (`0x4bb276`..`0x4bb2a8`) | `FUN_004bb210`; slot `+0x10` zeroes it |
| `+0x4a` | the caret shown; the constructor sets 1 | `FUN_004bb210` |
| `+0x4e` | the font (`set_font` at `0x4bb246`) | constructor |
| `+0x52` | the cel `DATA\GLOBAL\ui\menu\textslid` (`FUN_004788b0`; freed by `FUN_00478a00`) | constructor, destructor |
| `+0x56` | one-line: set when lines was below 1; then the draw frames the box (`gfx_rect_size_draw`, `0x4bb229`..`0x4bb23e`) | constructor |

## The vtables (in slot order)

The slots are `__thiscall`. "ret 4" is one stack argument; the zero
returns read none.

| Slot | `EditBoxImplementation` (`0x6da77c`) | `EditBoxWrapper` (`0x6da7cc`) |
|---|---|---|
| `+0x00` | `FUN_004bbd50`: deleting destructor. `FUN_004bb170`, then with bit 0 of the argument `FUN_00412650(this, "delete", -1, 0)` | `FUN_004bba50`: the same around `FUN_004bb9f0` |
| `+0x04` | `FUN_004bb210`: the draw | `FUN_004bbac0` |
| `+0x08` | `FUN_004bb5f0`: 0 (ret 4) | `FUN_004bba80` |
| `+0x0c` | `FUN_004bb5e0`: `+0x3e` = 1, 0 (ret 4). The ListBox's slot here is its press (`FUN_004bf430`, [npc-menu.md](npc-menu.md)) | `FUN_004bba90` |
| `+0x10` | `FUN_004bb600`: when `+0x3e` is set, `+0x46` and `+0x3e` = 0; 0 (ret 4). The ListBox's release slot (`FUN_004bf530`) | `FUN_004bbaa0` |
| `+0x14` | `FUN_004bb950`: 0 (ret 4). The ListBox's mouse move (`FUN_004beb70`) | `FUN_004bbab0` |
| `+0x18` | `FUN_004bb620`: a character | `FUN_004bbad0` |
| `+0x1c` | `FUN_004bb960`: 0 (ret 4) | `FUN_004bbaf0` |
| `+0x20` | `FUN_004bb970`: 0 (ret 4) | `FUN_004bbae0` |
| `+0x24` | `FUN_004bb9b0`: the caret one right while below the length; 1 (ret 4) | `FUN_004bbb00` |
| `+0x28` | `FUN_004bb9d0`: the caret one left while above 0; 1 (ret 4) | `FUN_004bbb10` |
| `+0x2c` | `FUN_004bb560`: the value. 0 for no text; else the text narrowed (`FUN_005263e0`, at most `+0x34` characters, into 0x64 bytes on the stack) through `_atol` (`0x681ebb`), and that when it is not above `+0x30` (signed), else 0 | `FUN_004bbb20` |
| `+0x30` | `FUN_004bb5b0`: the text narrowed into the argument, at most `+0x34` characters (ret 4) | `FUN_004bbb30`: calls the implementation's `+0x34` (no argument), then ret 4 |
| `+0x34` | `FUN_004bb5d0`: 0 (plain `ret`) | `FUN_004bbb50` → `+0x38` |
| `+0x38` | `FUN_004bb530`: a narrow string widened into `+0x28` (`narrow_to_wide`, count `+0x34`); caret and length = its length (ret 4) | `FUN_004bbb40` → `+0x3c` |
| `+0x3c` | `FUN_004bb4d0`: the argument as `"%d"` (`0x6d57bc`) in 0x14 bytes; when longer than `+0x34` the character at `+0x34 − 1` is cut (`0x4bb507`), so it keeps one fewer than the most (bugs.md #129); then as `+0x38` (ret 4) | `FUN_004bbb60` → `+0x30` |
| `+0x40` | `FUN_004bb990`: `+0x18` = the argument (ret 4) | `FUN_004bbb80` |
| `+0x44` | `FUN_004bb9a0`: `+0x24` = the argument (ret 4) | `FUN_004bbb90` |
| `+0x48` | `FUN_004bb200`: `+0x20` = the argument (ret 4) | `FUN_004bbba0` |

A wrapper slot with no arrow calls the implementation's same slot: it
loads `[this+8]`, its vtable and the slot, and jumps (`0x4bba80` ..
`0x4bbba0`). From `+0x30` to `+0x38` each wrapper slot calls the
implementation's next one, and `+0x3c` calls `+0x30`.

## Destructors

- `FUN_004bb170` (the implementation's, only caller `0x4bbd56`): its
  vtable back, `+0x28` and `+0x2c` freed (`FUN_0040b3c0`, lines 0x8e and
  0x93) and cleared, the cel `+0x52` freed (`FUN_00478a00`), then
  `FUN_004be080`.
- `FUN_004bb9f0` (the wrapper's, only caller `0x4bba56`): its vtable
  back; when `+8` is set, that object's slot `+0x00` with 1 (deleting the
  implementation), then `FUN_004be080`.
- `FUN_004be080`: `[this]` = `0x6da6c0`, `UIWidget`'s vtable.

## Not reached

Two setters have no call, jump or pointer to them anywhere in the image
(byte scan of `0x401000`..`0x7d0000` for the address, and of `.text`
for an E8 / E9 to it):

- `FUN_004bb980`: the implementation's `+0x04` (the character filter) =
  the argument (ret 4).
- `FUN_004bbb70`: the wrapper's version, through `[this+8]` (ret 4).

So the filter stays 0, and the key handler makes its own test.
