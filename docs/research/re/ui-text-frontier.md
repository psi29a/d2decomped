# UI/text.cpp frontier — game.exe 1.14d

## Six text and speech helpers

| Address | Name | Stock behavior |
|---|---|---|
| `FUN_0049d3c0` | `text_list_measure` | EDI is a record. It clears dword `+8`, copies unsigned word `+0x14` to dword `+0xc`, walks nodes from `+0` (`node +0` wide text, `+4` next), and calls `FUN_00501820` once to compare and again only when that width is at least the running maximum. It stores `max_width + 0x14` as word `+0x10`, and `(signed-low-word(+4) * 15 + 6)` as word `+0x12`; both truncate to 16 bits. Empty lists therefore measure 20 wide. Callers `FUN_004a05e0`, `FUN_004a0a00`, and `FUN_004a1510` lay out speech/text controls. |
| `FUN_0049e5c0` | `screen_message_add` | Fastcall ECX is wide text and DL is a kind byte. It saves the current font and selects font 13; allocates and zeros a 0x26-byte node; stores `GetTickCount()+10000` at `+0x1e`; wraps the text into at most six allocated wide strings, repeatedly reducing a candidate until `FUN_00501730` says it fits `screen_width-70`, then (after the first segment) backing up until `FUN_00526d30` accepts the following character. It copies each accepted segment and advances by that length. Word `+0x18` is line count, byte/dword `+0x1a` the kind, and `+0x22` next. It verifies the old chain length equals count minus one, appends, increments `0x7bf1ee`, lays out the node with `FUN_0049dbc0`, and if the queue exceeds two nodes detaches and frees the oldest with `FUN_0049d490`. If UI flag 0x18 is up and `0x7bf1b4` is nonnull it invokes virtual slot `+0x3c` then `FUN_0049e340`. Finally it restores the old font and calls `FUN_004b9a00(0,0,0)`. |
| `FUN_0049e7e0` | `text_anchor_set` | Fastcall ECX and EDX are stored unchanged at `0x7bf258` and `0x7bf25c`. No branches or calls. Ten call sites in quest/NPC UI set the pair. |
| `FUN_0049ff10` | `item_text_dispatch` | If selected item `0x7bf228` is null, returns. It reads its Items row code at `+0x80`: `bks ` calls `FUN_0049fba0(0)`; `bkd ` calls `FUN_0049fba0(1)` while `0x7bf254` is zero, otherwise falls through to `FUN_0049fa10`; `tr1 ` calls `FUN_0049fa10`; every other code returns. The callees build the applicable book/scroll or general item text. |
| `FUN_0049ff90` | `item_text_select` | Fastcall ECX is an optional owner/context and EDX the selected item. It stores EDX at `0x7bf228`, calls `FUN_00455f20` with ECX 16 and stack zero, then `FUN_0044da40`. For code `bkd ` it sets `0x7bf254=1`, sends `FUN_00478680(CL=0x3e, EDX=owner+0xc or -1)`, and clears `0x7bf1e0`. Otherwise it clears `0x7bf254` and sets `0x7bf1e0=1`. The item pointer is assumed nonnull. |
| `FUN_004a0e70` | `speech_text_update` | ECX is the active text-control object. It increments `0x7bf20e`, gets its bounds through `FUN_00476070` and origin through `FUN_004760c0`, and writes rectangle `0x7bf214` as `(origin_x, origin_y, origin_x + right-left, origin_y + bottom-top)`. Speech state 1 also updates the four bounds at `0x7bf0a8..b4` and `0x7bf224`. It registers/updates callbacks `FUN_004a0b30` and `FUN_004a0d20`. With an eligible active speech record (`0x7bf0a4`, kind at `+0x16` equal 0 or 1), it updates the line/control, optionally sends packet 0x30 and closes a related menu, and clears UI 7 when requested. An active timed text requires nonnull `0x7bf1c8`; through the unsigned tick comparison it draws with `FUN_0049d9a0` while `tick <= 0x7bf1ac`, otherwise tears down with `FUN_0049e300` and clears active flags. If neither speech path remains, it clears speech/activity state and UI 7. It always clears `0x7bf1bc`; when UI flag 0x18 is down it runs `FUN_0049dc40` then `FUN_0049dee0`; a nonnull `0x7bf1b0` is drawn by `FUN_004b8100`. |

The machine-code ranges are `0049d3c0..0049d408`, `0049e5c0..0049e7d8`,
`0049e7e0..0049e7ec`, `0049ff10..0049ff51`, `0049ff90..0049fffc`, and
`004a0e70..004a10cf`. The oracle uses synthetic list/item/message records and
hooks only allocation, rendering, packet, platform, and object-method
boundaries. It executes stock branching and stores and checks a negative
control for every entry.

### Port assessment and remaining gaps

There is no one-to-one d2d counterpart for these stock UI record managers.
d2d draws owned strings, panels, NPC menus, and item descriptions from typed
state rather than the client DLL's linked records and globals. The relevant
ported caller is the frame draw in `apps/d2d/ingame.cpp`; its comment cites the
stock dispatcher and text controller. The trace did not substantiate a d2d
output difference, so no C++ fix or regression test was added. Exact internals
of the called render/control helpers (`FUN_0049d9a0`, `FUN_0049dc40`,
`FUN_0049dee0`, `FUN_004a0770`, and `FUN_004a0880`) are not traced here; their
arguments, ordering, and effects visible in these six functions are traced.

Re-check from `tools/emu`:

```sh
.venv/bin/python ui_text_frontier.py --mutation-check
```
