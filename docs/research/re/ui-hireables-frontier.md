# UI/Hireables.cpp frontier

These six functions are the small frontier called by the ported hireling UI.
Addresses and effects below are from game.exe 1.14d.  The synthetic stock-code
check is `tools/emu/ui_hireables_frontier.py --mutation-check`.

| Address | Name | Behaviour |
|---|---|---|
| `FUN_00492ec0` | hireable resources release | Releases each non-null DC6 handle in `0x7beeb0..0x7beec8` through `FUN_00478a00` and clears the seven slots.  If `0x7beeac` is non-null, it releases the `5 * [tables+0xbf0]` handles in that array, frees the array through `FUN_0040b480` with `UI/Hireables.cpp:0xb7`, then clears `0x7beeac`. |
| `FUN_00492f40` | hireable colours initialize | Fastcall flag in ECX.  Zero initializes only once, guarded by byte `0x7beed3`; nonzero refreshes.  Calls `FUN_004fb180(r,g,b)` for `(0,128,0)`, `(192,192,0)`, `(255,0,0)` and stores their low bytes at `0x7beed0..d2`. |
| `FUN_00493020` | draw hireable name | The hireling-row index is the incoming EAX; x/y are two stack words.  It accepts `0 <= index < [tables+0xbf0]`, reads the 0xe0-byte row from `[tables+0xbe8]`, and draws only when word `+0xc` is neither 0 nor string id `0x1506`.  It constructs a 32-unit string, resolves that id (`FUN_00524a30`), copies it, and passes it to `FUN_00492fa0` with colour 0. |
| `FUN_004930a0` | draw roster hireable name | Converts the incoming ESI UTF-8 roster name into a zeroed 32-unit buffer with `FUN_00526320`, then passes it and the two stack coordinates to `FUN_00492fa0`, colour 0. |
| `FUN_00493100` | draw hireable level | Formats stack argument 1 as decimal, converts it through two 32-unit buffers, truncates from the end until font width (`FUN_00501820`) is at most 46, selects font 6, and draws colour `0x37` at x = stack argument 2 + 8.  It restores the prior font after drawing. |
| `FUN_004934d0` | held item may be equipped by mercenary | Described below. |

`FUN_004934d0` first requires expansion mode, a local player, its inventory
(`FUN_0063c1e0(player+0x60)`), and pet-list type 7 (`FUN_00478f20`) for the
hireling.  The held item must have stat 16 and not stat 256 (`FUN_006280a0`).
Stats 0x4c, 0x50 or 0x51 return 1 immediately (and sound 0x54 when the stack
argument requests feedback).  Stats 3 or 0x25 establish a general allow flag.
It then resolves the merc unit and class (`FUN_00463990`, `FUN_00479120`):

- class `0x10f` accepts stat `0x1b`;
- class `0x152` accepts stat `0x21` or `0x22`;
- class `0x167` additionally accepts stat 2, and accepts stat `0x1e` only when
  `FUN_006289c0(item)` is nonzero;
- class `0x230` accepts stat `0x1c` only when `FUN_006289c0(item)` is zero;
- class `0x231` accepts stat `0x1e`.

If neither a class rule nor the general flag accepts it, feedback sound 0x55
is played and it returns 0.  A stat-16 failure or stat-256 presence uses sound
0x56.  Finally `FUN_0062eaf0(item, merc, 0, 0, 0, 0)` (the common wear-rule
check) can veto the result.  `HirelingPanel::draw` has the d2d counterpart via
`rules::merc_slot`; the resource handles and stock font calls have no direct
d2d counterpart.

## Remaining gaps

The six entry points are traced.  The larger callers that build the hireling
row and render the complete sheet were already ported and are outside this
batch; this trace does not claim equivalence for their unrelated branches.
