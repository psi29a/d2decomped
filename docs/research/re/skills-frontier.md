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
