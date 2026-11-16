# Small UI frontier functions

These thirteen functions split into text-overlay teardown, quest-log resource
handling, and the controls/key-bindings mouse dispatcher.  The callers and
global clusters establish those subsystems; the stock-code oracle is
`tools/emu/ui_small_frontier.py --mutation-check`.

| Address | Subsystem and behaviour |
|---|---|
| `FUN_004a1730` | Text overlay: calls `FUN_0049f960`, clears dword `0x7bf1fa`, and, when `0x7bf250` is non-null, frees that text node with `FUN_006612a0` and clears it.  `FUN_004b3c20` calls it during the in-game overlay close. |
| `FUN_004a1760` | A five-byte tail jump to `FUN_004a08c0`, the text/dialog close dispatcher.  That dispatcher sets `0x7bf1bc`, closes speech or NPC text, runs callback `0x7bf258` when installed, clears `0x7bf20a` and `0x7bf23f`, and closes queued UI 7 through `FUN_004f59a0`/`FUN_00451e10`; its alternate branch releases `0x7bf1a8` and clears `0x7bf1c4/1c8`. |
| `FUN_004a2550` | Quest log: releases and clears DC6 handles `0x7bf2d5`, `d9`, `34d`, `d1`, `dd`, `351`, then every dword in the half-open array `0x7bf2e1..0x7bf34d`. |
| `FUN_004a2600` | A five-byte tail jump to `FUN_004a2550`; the global UI shutdown caller uses this entry. |
| `FUN_004a26b0` | Quest-log close: if state `0x7bf298` is 2, clears it plus bytes `0x7bf2b0` and `0x7bf2b3`; releases the resources above; calls `FUN_00455f20` with ECX 15, EDX 1 and its stack argument; returns 1 and pops that word. |
| `FUN_004a47c0` | Controls: with x in ESI and y in EDX, walks three 0x1a-byte button records at `0x7246e2`.  Each rectangle has inclusive x0/x1/y0/y1.  Returns button 0..2, or 3 when none contains the point. |
| `FUN_004a47f0` | Controls list hit test: x in EDI, y in EBX.  Inclusive list bounds are `left+0x12 .. left+(column+6)*3` and `top+0x28 .. top+0x136`, with `left=(screen_width-0x26c)/2`, `top=(screen_height-0x1a4)/2`, `column=(screen_width-2*left-0x31)/3`. |
| `FUN_004a4860` | Controls scrollbar up arrow: x in ESI is in the inclusive 14-pixel bar, y in EDI is `top+0x23 .. top+0x2f`. |
| `FUN_004a48b0` | Controls scrollbar down arrow: the same x range, y `top+0x125 .. top+0x131`, inclusive. |
| `FUN_004a4900` | Controls page-above region: same x range, `top+0x2f < y < 0x7c02cc` (thumb top). |
| `FUN_004a4950` | Controls thumb: same x range, `thumb_top <= y <= thumb_top + 12 * 0x7c0278`; the latter global is the thumb row count. |
| `FUN_004a49a0` | Controls page-below region: same x range, `thumb_top + 12 * thumb_rows < y < top+0x125`. |
| `FUN_004a4a10` | Controls release match: EAX is y and ECX is x.  It requires the point in the list (`FUN_004a47f0`), the row from `FUN_004a4720` to equal selected row `0x7c02b0`, and the computed column side (`x < left+0x12+2*column`) to equal `0x7246d4`; returns 1 only then. |

`ControlsUI::mouse_frame` implements all eight controls predicates and the
press/release state machine.  `quest_log_close` implements the quest close
state, but d2d owns decoded sprites rather than the stock DC6 handles, so the
three release entry points have no separate d2d function.  The stock text-node
allocator likewise has no direct d2d counterpart.

## Remaining gaps

All thirteen entry points are traced.  The full bodies of the larger text
dispatcher (`FUN_004a08c0`) and controls dispatcher (`FUN_004a4ad0`) are cited
only to explain these leaves and are not newly claimed as traced by this batch.
