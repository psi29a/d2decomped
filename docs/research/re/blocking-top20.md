# The 2026-11-15 Blocking functions

The gap map generated from `d2a69f1b` put these twenty functions at the top
of its Blocking table.  They cross subsystem boundaries, so this page records
the batch and points back to the subsystem behavior that each listed gap uses.
All addresses below were run from stock game.exe 1.14d (1.14.3.71), SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.

`tools/emu/blocking_top20.py` builds synthetic records, runs each entry point,
and hooks only its out-of-scope callees.  It checks the significant guards,
call order and arguments, global/record writes, and return value.  Its twenty
mutations each change one claimed behavior and must all fail:

```
cd tools/emu
uv run python blocking_top20.py --exe /path/to/game.exe --mutation-check
```

The good first line is `ok: 20 Blocking functions ...`; it is followed by one
`failed as required` line for every mutation.  No MPQ data is read.

## Results

The table is in address order.  Register arguments are named where Ghidra's
ordinary C signature hides them.

| Address | Verified behavior |
|---|---|
| `FUN_0042f6e0` | Clears the connecting-screen state, sets `0x779954 = 1` and `0x779974 = 0`, makes three controls, hides the first, enables all three, initializes strings and drawing, then stores callback `0x42f6c0` in `0x7795bc`. Both branches on `0x7795f0` make the same calls. This is the screen omitted by the ponytail in `apps/d2d/main.cpp`. |
| `FUN_0044ec80` | Sets classic-ending global `0x7a0604` to 1. It replaces only bits 8..12 of the word at screen `+0x1ef` with `(0x7a060c + 1) * 0x400`, preserving mask `0xe0ff`. This confirms why the LoD-only `endgame2` branch in `stubs.md` is unreachable through this setter. |
| `FUN_00452af0` | Takes the unit in ECX, width pointer in EAX, flag pointer in EBX, and destination/limit on the stack. Mode `0x11` produces width 0, flag 1 and formats string `0x90b` with the widened roster name. Other modes use `(roster_width << 7) / 100`, widen the name directly, and set the flag only when `FUN_0047a070(local_id, unit_id)` reports a relation. Null uses id `-1`. |
| `FUN_00469ae0` | The complete 16-bit key predicate matches `keyconfig.hpp`: mouse buttons 1..4 and the fixed navigation/system exclusions return 0; the listed editing, function, numpad and OEM keys return 1; values `0x100..0x104` return 1; everything else falls through `_isalpha` then `_isdigit`. All keys `0..0x1ff` are checked. |
| `FUN_004b9c20` | Walks the list at `0x7c5458` and returns 1 for the first sound whose type at `+0x30` is not 2 and whose id at `+4` is inclusively `0xb76..0x1230`; otherwise 0. Thus “hasn't spoken” is not merely a class-line test, as `shared.cpp` notes. |
| `FUN_004e9870` | Sets the font and lazily caches the vertical offset in `0x72d8d4`: languages 6..9 use `-7`, all others use `-6`. It draws both values through `FUN_004a7080(0x7a2858 + y, value, style)`. The cached byte at `0x7c8c9c` prevents later recomputation. |
| `FUN_004ed480` | If global `0x7c8ca0` is already nonzero it immediately returns 1. Otherwise it walks every Skills row, skips rows of another player class, asks for that indexed skill and its level, and on the first nonzero level sets the global permanently and returns 1. No match returns 0. This reproduces bugs.md #25, including the cross-character cache. |
| `FUN_004eda80` | Null player, missing selected skill, missing skill stats or missing SkillDesc row returns 0. Otherwise SkillDesc `descatt` is accepted for every value below `0x48`; a non-null pointer at `0x72d7f8 + 4*descatt` receives `(stats, level, &rating, ...)`, and the first output word is returned. Values 18..71 really dispatch when populated, confirming the byte-count/entry-count bound in bugs.md #65. |
| `FUN_00515890` | Null is fatal at line `0x2ef`. Pan below 128 passes that value as the third float to `FUN_005165f0`; pan above 128 passes `255 - pan`; both convert the result and call vtable slot `+0x40`. Exactly 128 calls the slot with zero and does not compute an angle. This is the far-speaker attenuation modeled by `sound_mix.hpp`. |
| `FUN_0052d240` | Walks the five act pointers at game `+0xbc`. For every room it saves `+0x7c` before doing work. An age from `FUN_0061a790` must be greater than 10 and `FUN_0061a3f0` must approve; it then calls `FUN_005433f0(game, unit)` along the room's `+0x74` / unit `+0xe8` chain and finally `FUN_0061a910(act, room)`. Age 10 and rejected rooms stay up. |
| `FUN_00532340` | Clears an existing client save-upload record, serializes into an 8192-byte stack buffer, and fatals at line `0xb32` when the result length is less than 1. A nonempty result calls begin, append with `(length, length, 0, 0, 0)`, then finish, in that order. This is the server-side caller named by bugs.md #96. |
| `FUN_0053e8f0` | Builds and sends the 26-byte S->C `0x59` packet: id, class, 16-byte name field, x and y. It really passes bound `0x7fffffff` to the name copy; x/y overwrite name overflow bytes 16..19 before send. It then gets the client's unit, looks up the id, resolves that unit and assigns the pair when found. This runs the overflow premise in bugs.md #104 rather than relying on decompile alone. |
| `FUN_0053fc70` | Calls `FUN_0053fb90(game, 1)` first. It then gets the client's unit with flag 1 and calls `FUN_0053fb30(client)` only when the client type at `+4` is not 4 and the unit is non-null; ESI carries that unit into the refresh. This is the callback used by the orphan bit-2 setter in `stubs.md`. |
| `FUN_005408e0` | Initializes a stack count to zero, preserves incoming ECX/EDX, and calls `FUN_005405a0` with callback `FUN_005404f0` and the count pointer. It returns the callback-updated count. This is the nearby-party count in the NoDrop formula. |
| `FUN_0054ef50` | Reads the act index from incoming EAX `+3`; indexes the start/count byte pairs at `0x731b30`; uses region EDI's seed at `+0x6c`; and picks a class from the dword table at `0x731b2c`. A successful spot and successful spawn are both required before `FUN_005b1990(game, spawned, 0, 8)` and the stack argument's `+0x2c4` count increment. Failed spot/spawn and invalid/empty act ranges do not increment. |
| `FUN_0056c600` | Null player, missile or skill is a no-op. Otherwise it reads missile stat `0x13`, adds mastery kind 0 from `FUN_00645720(player, skill, 0, 0, 0)`, and writes the sum to stat `0x13`; mastery kind 1 is assigned to stat `0x19`. The original to-hit is added, not replaced. |
| `FUN_0057f410` | Takes owner in EDI and item in ESI. It verifies the item is exactly the inventory lookup result (fatal ids `0x25d`/`0x25e` otherwise), removes the slot, runs the class-`0x13` special callback, clears item flag `0x2`, updates owner/item flags and modes, unlinks it, optionally calls the empty stub for a previously wielded item, queues the item event, and refreshes the owner. `FUN_0055d930` remains an intentional empty leaf. |
| `FUN_005d92d0` | Takes the player in EAX. A null inventory returns 0. It gets body slots 6 then 5 and returns 1 only when both items exist and both report item flag `0x2e`; every missing slot or flag returns 0. This is the exact two-weapon predicate referenced by `fight.hpp`. |
| `FUN_005e77a0` | Requires quest data 3 with both spot fields (`+0xc`, `+0x10`) zero. It stores random coordinates plus 3, chooses `FUN_0058a940/0058a980` only for class `0x20f` and `FUN_005944b0/005944f0` otherwise, and spawns/clears stat 6/queues event `0xc` only when the find callback returns 0. Both paths set `+0x14 = 1`, `+0x18 = 0`, finish with argument 1 and return 1; an absent/already initialized record returns 0. |
| `FUN_0064a6d0` | Takes unit in ECX and value in EDX. Only a non-null type-3 unit with non-null data at `+0x14` gets the value at data `+0x30`; every other input is an untouched no-op. Together with the already verified getter this confirms the dead non-missile allocation leak described by bugs.md #82. |

The four already ported behaviors in this set (key assignment, learned-skill
cache, sound pan and Cain's callbacks) agree with their current d2d models on
the branches above.  The oracle confirms the three documented original bugs;
it found no new disagreement requiring a code fix.
