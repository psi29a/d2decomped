# Five client frontier helpers — game.exe 1.14d

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
