# SKILLS frontier: 32 entries from the local gap map

The starting map `build/gaps/functions.tsv` (before this branch) listed 32
`SKILLS` rows with `frontier = true`. This is that fixed list, not a claim
that it remains the whole current frontier. `SKILLS/?` below is the map's
range attribution: it does not identify the original source filename.

The local 1.14d `game.exe` was read with the Ghidra decompile and selected
instruction ranges; its SHA-256 is
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
The table describes only what the code shows, without claiming a d2d match.
`tools/emu/skills_frontier.py` runs all 30 callable entries with scripted callees. These are scoped
stock-code checks, not exhaustive verification. Two apparent function
entries are branch targets within a surrounding frame,
so their real enclosing entry and the label are given instead.

To re-check the stock-code runs:

```sh
cd tools/emu
uv run python skills_frontier.py --exe /path/to/Game.exe --mutation-check
```

The script validates the executable hash, bounds each top-level call and
checks the return sentinel and stack cleanup. It uses existing `unit` views
from `structs.tsv`. Its model checks call order as well as results; all
callees listed in its hook table are scripted. No game data or Windows IME,
network, UI, allocator or native process is used. The oracle reports case counts per group (3,960 calls in total), and six
negative controls require deliberate errors in the model to be caught. The tests are not comparisons with a d2d port.

## Entries in address order

| Entry | Reading from game.exe | Evidence and coverage |
|---|---|---|
| `FUN_004c57b0` | Clears a 0x5c-byte request, sets kind 0x21 and the unit, gets its position by type (static path for 2, 4, 5; dynamic path otherwise), then adds the two caller offsets. Copies two further values into the request. If either of the supplied pair is zero it asks `FUN_004c52e0` for the pair; failure or either resulting zero returns 0. Otherwise it returns `FUN_004cd540`'s result. | 56 cases with position, supplied versus fallback pair and failure gates; request bytes, return and call order checked. Destination selection and spawn are hooked. |
| `FUN_004c8890` | Uses ESI's unit and its path +0x2c; with a nonzero path and `FUN_00620bb0` result it optionally calls `FUN_0064ec10` when EBX and the stack argument are nonzero. Monster type 1 uses collision 0x100, mask 0x3c01, path mode 2; other types use 0x80, 0x1c09, mode 7. If `FUN_00464820` is nonzero it calls `FUN_00649f70(unit, 0)`. | 96 combinations of type, path/room presence, coordinates and `FUN_00464820` with callees hooked. Despite a nominal null-type fallback, the next access dereferences ESI +0x2c; null is not run. |
| `FUN_0056bbc0` | Builds a five-dword search context: zero, 0x7fffffff, unit x, unit y, and the fourth argument; gets position by unit type as above. Calls `FUN_0056b7e0` with mask 0x8783, callback `LAB_0056bb20`, source line 0x2b8, then returns the first context dword. | 9 combinations of unit type and path presence, with the walker hooked; checks registers, arguments, search context and returned context value. The callback behavior and meaning of its selection remain outside this trace. |
| `FUN_0056cd50` | Calls `FUN_00646ca0` with the unit, record +0x128 and two inputs, clamps the result to at least 5, computes `((game_frame - 1 + period) / period) * period + 1` with signed x86 division and 32-bit arithmetic. | 63 cases with formula calc hooked, periods -1 to 20 and nonnegative frames through 1001; calc arguments checked. Overflow and negative-frame cases were not run and require the CPU's signed semantics. |
| `FUN_0056dab0` | Converts an item code to class; -1 returns. It asks for unit position and a destination via `FUN_00555da0`; failure returns. Otherwise zeroes a 0x84-byte item-creation request, sets owner, game, class, position, item level, mode 3, a caller-supplied value and a word from game +0x78, then calls `FUN_00558d90`. | 12 cases cover class -1, placement failure and successful creation; position, placement, item-level and creation helpers are hooked, and the complete request bytes are checked. |
| `FUN_0056e000` | With EDI and ESI as inputs, gets a level via `FUN_00644aa0`, then writes physical min and max shifted arithmetically right 8 to output +0/+4 and the to-hit result to +8. | 63 cases with four helpers hooked, including negative values; checks arithmetic shifts, output guards, call order and arguments. The level helper pops its two arguments, which Ghidra omitted. |
| `FUN_0056e580` | Asks `FUN_0058f0d0` for an owner. If it returns a type-1 unit equal to ECX, tail-calls `FUN_0058f260` with that unit; for a different type-1 owner, tail-calls `FUN_0058f160`; otherwise returns. | `skills_frontier.py`: 9 cases with owner lookup and both side effects hooked. |
