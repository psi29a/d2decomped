# Client frontier helpers — game.exe 1.14d

## UI DC6 path and cursor item

| Address | Name | Observed behavior |
|---|---|---|
| `FUN_004520c0` | `ui_dc6_load` | Fastcall ECX is a NUL-terminated relative name. The function formats `DATA\\GLOBAL\\UI\\` into a 260-byte stack buffer, obtains its length through `FUN_00413750` (the stock `strlen` implementation), copies the input including its terminating NUL at that offset, clears EDX, and calls `FUN_004788b0` with ECX pointing at the completed path. It returns that loader's EAX unchanged. There is one branch: an empty input skips the copy loop but still writes the NUL and loads `DATA\\GLOBAL\\UI\\`. `FUN_004788b0` appends `.dc6` for flag zero, reads the archive member, builds its cel-file object, and returns that object (see [mpq-archives.md](mpq-archives.md)). The wrapper neither checks the input nor bounds the copy; 245 or more input bytes overrun its 260-byte local buffer. Its 17 callers supply UI asset names; 11 are ported. |
| `FUN_004680a0` | `client_cursor_item` | No arguments, branches, calls, or normalization: `MOV EAX,[0x7a6abc]; RET`. The returned pointer is the client unit currently carried by the cursor, or zero. `FUN_00468010` and `FUN_00468070` store their ECX item pointer there; `FUN_00468040` clears it. Callers test cursor occupancy to gate inventory, trade, NPC and click actions; `FUN_00453ec0`, `FUN_00490780`, `FUN_00490ba0` and `FUN_00490fc0` also read the returned unit's class id at `+0xc` before passing it to `FUN_004786a0`. The other seven call sites use only null/non-null. |

The machine-code boundaries are `004520c0..00452124` (101 bytes) and
`004680a0..004680a5` (6 bytes). `tools/emu/client_ui_frontier.py` runs both
stock entries: the UI loader's final callee is hooked to record ECX/EDX and
return a sentinel, while the cursor getter reads controlled dwords. Empty,
nested and maximum in-bounds UI names are covered. `--mutation-check` proves
that a local-UI prefix, a nonzero loader flag, and boolean-normalizing the
cursor pointer are each detected.

### Port assessment

There is no one-to-one d2d function for either entry. `load_ui_sprites` reads
the same complete `data\\global\\ui\\...dc6` member names through the typed MPQ
API instead of constructing a prefix and returning a raw cel pointer. The
server/client view carries the cursor item as `std::optional<Item> held`, so
callers test typed state rather than call a global-pointer getter. The traced
semantics agree at those use sites; no substantiated divergence or new C++
test is required. The stock wrapper's unchecked overlong-input behavior is
not reproduced because d2d uses owned strings and fixed asset names.

Re-check from `tools/emu`:

```sh
.venv/bin/python client_ui_frontier.py --mutation-check
```

Expected: `ok: 4 UI DC6 path cases, 7 cursor-item getter cases`, then three
`failed as required` mutation lines.

## Earlier five helpers

These are five small entry points selected from the gap map's client-side
frontier. Disassembly establishes their calling conventions; the executable
oracle `tools/emu/client_frontier.py` runs the five entry points on synthetic data.
It checks 207 cases and has one planted, detected defect per function.

## Trace

| Address | Name | Observed behavior |
|---|---|---|
| `FUN_00453a90` | `ui_flag_get` | ECX is an index. It returns the exact dword at `DAT_007a27c0[index]`; it does not normalize the value and has no bounds check. The array has 38 entries in the UI setup and panel walkers. `FUN_00455f20` writes the flags; `FUN_00453910`, the panel drawer and the Esc/menu paths walk them. |
| `FUN_00457460` | `unit_flags_c4_toggle` | ECX is a unit, EDX a mask, and the one stack argument is `enabled`. A null unit does nothing. For a nonnull unit, any nonzero `enabled` ORs the mask into unit `+0xc4`; zero ANDs its inverse. No other byte changes. Callers use it after client unit-mode changes, including `FUN_004bc5e0` and `FUN_004bcbb0`. |
| `FUN_004a7080` | `centered_glyph_draw` | ECX is inclusive left x, EDX is the colour, and stack arguments are inclusive right x, wide text, and the final draw argument. It calls `FUN_00501820(text)` for width. If `width < right - left + 1`, x becomes `left + (right - left + 1 - width) / 2`; otherwise x remains left. It then calls `FUN_00502320` with ECX text, EDX x and stack arguments `(colour, final, 0)`. The strict comparison means a string exactly as wide as the range stays at left, which is also its centred position. |
| `FUN_004b3500` | `store_gamble_mode` | No arguments or branches: it returns the exact dword at `DAT_007c0db0`. Store pricing, item actions and button setup test it as a boolean. The store transition at `0x4b433f` writes the mode and the exit paths in `FUN_004b6dd0` clear it. |
| `FUN_004b9590` | `sound_handle_playing` | ECX is a sound handle. Zero returns false without walking. Otherwise it walks nodes from `DAT_007c5458`; node `+0` is a sound record and node `+4` is next. It stops at the first record whose `+8` equals the handle and returns whether that record's state at `+0x30` is exactly 1. Missing handles return false. Duplicate handles therefore use the first match, not any live match. `FUN_004b95c0`, `FUN_004dcaa0` and `FUN_004dcd40` use it in active-sound decisions. |

The relevant machine-code boundaries are `00453a90..00453a97`,
`00457460..00457480`, `004a7080..004a70b9`, `004b3500..004b3505`, and
`004b9590..004b95ba`. The xrefs for the three globals are the Ghidra
`XrefsTo` results for `007a27c0`, `007c0db0`, and `007c5458`.

## Port assessment

No new C++ behavior is needed.

- `centered_glyph_draw` is already reproduced exactly in the character and
  skill panels (`apps/d2d/panels.cpp` and `apps/d2d/skilltree.cpp`), including
  the inclusive `+1`, strict fit comparison and left fallback.
- d2d owns UI flags and store gamble mode as typed application state, so the
  two raw-global getters have no useful standalone port.
- d2d's client/server unit models apply the `+0xc4` effects at the mode-change
  sites rather than exposing a raw record-mask helper.
- `Audio::speaking` checks the backend's playing/paused channels and merged
  unit ownership. That is the d2d equivalent of the stock sound-state query;
  stock list handles and the `+0x30` backend state are not part of d2d's audio
  representation.

Thus the trace found no mismatch to fix and no missing behavior that should be
introduced as otherwise-unused code.

## Re-check

```sh
cd tools/emu
uv run python client_frontier.py --exe /path/to/game.exe --mutation-check
```

Expected: `ok: 207 client frontier cases over 5 functions`, followed by five
`failed as required` mutation lines. The oracle loads no MPQs; the text width
and glyph draw callees of `FUN_004a7080` are the only hooks.

## The hand cursor and the OS cursor

Helpers of the hand cursor's code (cursor_anim.hpp, `FUN_00468310`'s
family). Read from the decompile; the two row getters also ran in
`tools/emu/cursor_rows.py`. Rows in address order.

| Address | Name | Reading | d2d |
|---|---|---|---|
| `FUN_00467e40` | `cursor_row_loops` | The row in EAX. Asserts the row count at `0x7120d4` is 7 (line 0x8f) and the row under it (line 0x90), then returns the row's `+4` in the table at `0x712010` (0x1c bytes a row): whether it loops. Callers the forward step `FUN_004681c0` and the backward one `FUN_004682c0`, at a row's end | matches: `row_loops` (cursor_anim.hpp), oracle `cursor_rows.py`, test `test_cursor_rows` |
| `FUN_00467e90` | `cursor_row_length` | The same asserts (lines 0x9a, 0x9b); returns the row's `+8` (its frames) shifted left 8, the length in the 8.8 frames the position `0x7a6ae0` counts. Callers `FUN_004681c0`, `FUN_004682c0` and the mouse move `FUN_00468840` (ohand backward from its length less 0x100) | matches: `row_length`, the same oracle and test |

Re-check:

```sh
python3 tools/ghidra/fn.py 00467e40 00467e90 00468840
cd tools/emu && uv run python cursor_rows.py
```

Expected from the oracle: the seven rows (Gaunt .. buysell) and
`ok: 7 cursor rows`; `cursor_rows.py inc` writes `tests/cursor_rows_cases.inc`.
