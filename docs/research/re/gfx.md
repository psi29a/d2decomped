# Gfx.cpp and GfxUtil.cpp — game.exe 1.14d

The client's per-unit graphics: the COF cache, the overlay list, the
little helpers around the unit draw (`FUN_00470ec0`, see
[cof-draw-order.md](cof-draw-order.md)) and the motion record. Source
files from the asserts (`.\ENGINE\Gfx.cpp`, `.\ENGINE\GfxUtil.cpp`,
`ENGINE\Particle.cpp`; `game-srcfiles.tsv`); the unnamed run at
`0x4da0b0..0x4da8e0` sits between Particle.cpp and GfxUtil.cpp. Re-check
any row with `tools/ghidra/fn.py <address>`; register arguments are where
the decompile reads them (`unaff_` / `in_EAX`). "No caller" rows were
byte-scanned (rel32 calls, jumps, raw pointers) and are in
[stubs.md](stubs.md).

Most functions here first take the unit's type, class and mode through
the transform state (`FUN_00645270`, [compcode.md](compcode.md));
`FUN_0046d9f0`, `FUN_0046dac0` and `FUN_0046da60` (Engine.h) are those
three getters.

## The unit's graphics record

Unit `+0x54` (`FUN_00463e10`), 0x3c bytes from `FUN_0046ebb0` (line 0x804):
the function zeroes the record and sets `+0x34` and `+0x38` to 2; allocation
failure is fatal at line 0x805 (`tools/emu/engine_gfx_alloc.py`, pool hooked).

| Offset | What |
|---|---|
| `+0x00` | 20 two-byte slots, byte 0 a "loaded" flag per COF layer (`FUN_004db1a0` get, `FUN_004db1c0` set) |
| `+0x28` | COF list: 0x14-byte nodes, `+0` mode, `+4` weapon-class token, `+8` the loaded COF graphic (`+0x14` its data), `+0x10` next |
| `+0x2c` | overlay list: 0xa8-byte nodes, next at `+0xa4` (the overlay draw is `FUN_00470c30`, `tools/emu/overlays.py`) |
| `+0x30` | the motion record below |
| `+0x34` / `+0x38` | two values, 2 each when made (`FUN_0046f1c0` / `FUN_0046f220` set, `FUN_0046f1f0` / `FUN_0046f250` get) |

## The COF caches

Two caches of 0x80c bytes, `0x7a8100` and `0x7a78f0` (players use the
second: `FUN_0046d9f0` non-zero): `+0` loaded, `+4` the block, `+8` its
size, `+0xc` 512 hash buckets. The bucket hash is
[chat.md](chat.md)'s `FUN_0046db90`; the bytes in use are summed in
`0x7a8918`.

| Address | What |
|---|---|
| `FUN_0046ddf0` | Loads a bundle (EBX the cache) once: reads the file (lines 0x10b, 0x10c), then walks its entries (0x18-byte header, `+8` size, name at `+0xc`, at most 7 characters (fatal 0x79), data at `+0x18`; a layer count above 0x40 is fatal, 0x126), hashing each into its bucket |
| `FUN_0046f310` | `DATA\GLOBAL\` + the bundle name at `0x6d6c30[n]` (`cmncof_a1.d2`, ...) into `FUN_0046ddf0`. Caller `FUN_0044e100` |
| `FUN_0046f370` | Frees a cache's separately loaded entries (flag bit 0; lines 0xe3c, 0xe3d) |
| `FUN_0046f3f0` | Frees the monster cache (`0x7a8100`): entries, then the block (0xe50), then zeroes it. No caller |
| `FUN_0046f440` | The same for the player cache (`0x7a78f0`, 0xe61). Caller `FUN_0044e100` |
| `FUN_0046f490` | A monster's COF path (EAX MonStats row, mode, out): `DATA\GLOBAL\MONSTERS\<token>\COF\<token><mode><wclass>.COF`, the token from MonStats `+0x10` (MonStats2 flag `+6` bit 0 clear in modes 0 and 0xc: `hth`), a mode override from `0x712b04` |
| `FUN_0046f870` | (MonStats row, arg): for each mode in `0x712ac4` (count `0x712b00`) the monster has (MonStats2 `+0xf0` bits), loads its COF (`FUN_0046f650`). Callers `FUN_004a2cb0`, `FUN_004a30a0`, `FUN_004a30c0` |
| `FUN_0046f980` / `FUN_0046f990` / `FUN_0046f9a0` | Return `0x7a8914`, `0x7a8918` (bytes in use), `0x7a891c` |
| `FUN_0046f9d0` | Row n of `0x7a80fc` (n × 0xa0) |
| `FUN_00470200` | Start: zeroes both caches and `0x7a7490` (0x460 bytes), allocates `0x7a80fc` (0xa0 bytes per MonStats row, line 0x7e1), `0x7a8928` = d2char.mpq missing (`FUN_004fac90`), `0x7a891c` = arg; then `FUN_004da790`. Caller `FUN_00457300` |
| `FUN_00470290` | Both of the above, then frees the per-class table `0x7a80fc` (line 0x7f9). Caller `FUN_00457420` |

### The unit's COF list

| Address | What |
|---|---|
| `FUN_0046e570` | The COF data (`+8` → `+0x14`) of the list node for mode ECX |
| `FUN_0046e5a0` | The node for (mode, token); mode -1 means the unit's (`FUN_0046da60`) |
| `FUN_0046e610` | Unlinks and frees the node whose graphic is EAX (line 0x3d0); 1 when found, 0 when not; fatal 0x3c8 without a graphics record |
| `FUN_0046e690` | Is the mode's node current (EAX mode, -1 the unit's): its token equal to arg 1 gives it in `*arg 2` and 0; another token sets `*arg 2` = 0, puts its old data in `*arg 3`, frees it (`FUN_0046e610`) and gives 1; no node: 1 (fatal 0x3f0 without a graphics record; the fatal 0x406 after the free cannot be reached) |
| `FUN_0046e8d0` | Loads a monster's COF for mode EDI unless the layer flag says it's loaded (MonStats2 check `FUN_0046c140`, fatal 0x486; line 0x464). It returns 1 only when the load succeeds and marks the flag, else 0 |
| `FUN_0046e980` | Loads an object's COF unless the layer flag says it's loaded. A missing Objects.txt row is fatal 0x4c2, a failed load fatal 0x4d4, and `+0x13f + mode` other than 1 fatal 0x4dc. A successful load sets the flag and returns 1; an already loaded flag returns 0 |
| `FUN_0046ea50` | The same with no checks (a player). No caller |
| `FUN_0046eac0` | Clears 16 layer flags and frees the COF list (lines 0x65f..0x66d); n is only checked (>= 16, signed, is fatal 0x65f) and the flags 0..15 are always cleared. Caller `FUN_0046f000` |
| `FUN_0046eb70` | Frees the overlay list (an overlay's sound at `+0x40` first, `FUN_004743d0`; line 0x690) |
| `FUN_0046ec10` | Frees the whole record: COF list, overlays (`FUN_0046eb70`), the record (0x814..0x820) |
| `FUN_0046ec80` | Frees the COF list (0x82a, 0x830) |
| `FUN_0046ece0` | Frees the list's nodes except modes 0 and 0x11 (death and neutral stay) |
| `FUN_0046f000` | `FUN_0046eac0(0..15)`. One caller |
| `FUN_0046f020` | Sets the 20 layer flags to DL |
| `FUN_0046f280` | Clears the layer flags: 20 for a player, 16 for a monster, 8 for an object; another type is fatal (0xd3e); nothing is done when n (EDX, signed) is 16 or more |
| `FUN_0046f9e0` | A monster's: mode 0xe means the current one; mode 0xc first clears the list (`FUN_0046ec80`) and the flags (`FUN_0046f020`) |
| `FUN_0046fa80` | Loads by unit type (0 player, 1 monster, 2 object), then returns the node (`FUN_0046e5a0`) |

### Preloading

| Address | What |
|---|---|
| `FUN_0046fb60` | Loads every layer's cel file of a unit's COF for mode EBX (`FUN_004db2e0` row, `FUN_004db110` order, `FUN_006001f0` load); 1 when all are in, 0 when one is pending, -1 on a hard fail. A missing COF prints `sAsyncLoadBlock failed on async:%d unittype:%d ...` |
| `FUN_0046fda0` | A monster's mode once: the slot `0x7a80fc[(mode + class × 0x28)]` set when done, the pending flag at `+0x50` (a queued load, `FUN_005ff300`) |
| `FUN_0046feb0` | Every mode in `0x712ac4` the monster has, through `FUN_0046fda0` |
| `FUN_0046ff50` | The same for a player in `0x7a7490` / `0x7a74e0` |
| `FUN_00470070` | Over the rooms near the player (`d2d_act5_rooms_near`): every monster (`FUN_0046feb0`, skipping mode 0xc when `+0x30` is set) and player (`FUN_0046ff50` per mode in `0x712ac0`), stopping when more than 0x1d loads are queued (`FUN_005ff2a0`) |
| `FUN_00470350` | Runs `FUN_00470070` at most every 600 ms (unless `0x7a8920`). Caller `FUN_0044c790` |

## Overlays

| Address | What |
|---|---|
| `FUN_0046e100` | Has the unit an overlay of row EDX (fatal 0x1ea) |
| `FUN_0046e150` | Sets `+8` of the unit's overlays of row EDX; a kind other than 1 is fatal (0x205). No caller |
| `FUN_0046e1b0` | Draws the unit's overlays of row EDX at (x, y) in two passes (front, back by `+0x34`), skipping kind 6 without `+8` and kind 8 without `+0x3c`; Overlay.txt row at `0x744304 + 0xbbc`, 0x84 bytes. Caller `FUN_00453470` |
| `FUN_0046e300` | Draws all of a unit's overlays at its screen spot (or its automap spot in the perspective modes, shifted a quarter screen for a side panel as [bugs.md](bugs.md) #38 says), plus the motion offsets; Overlay.txt `+0x81` may trigger `FUN_00477680` |
| `FUN_0046f0c0` | Removes (`FUN_0046e040`) the first overlay of row n; for either kind 4 or 9, a match at either `+8` or `+0xc` also qualifies. A bad row is fatal (0xca9) after a unit and graphics record are found (`tools/emu/engine_overlay_remove.py`; removal hooked) |
| `FUN_0046f170` | Removes all of a unit's overlays, saving the next pointer before each removal; a missing graphics record is fatal (0xccf; `tools/emu/engine_overlay_clear.py`, removal hooked) |
| `FUN_004dad50` | Overlay.txt `+0x80` of row EAX (1 when out of range) |

## Drawing helpers

`FUN_0046f950` asks `FUN_00464820` whether the unit is dead; when it
returns zero and the unit's type is player (0), it calls `FUN_00461250`
with action 7, the unit, and stack arguments 0 and 1. The predicate is
called before the null check and itself dereferences the unit. The oracle
`tools/emu/engine_action.py` hooks both callees, so its null case covers
only this caller with a synthetic predicate, not the real predicate.

| Address | What |
|---|---|
| `FUN_0046ed50` | A tint by life: blue `(0,0,0xc0)` for a monster with MonStats `+0xd` bit 1, else green `(0,0xc0,0)`, yellow at 33 % to 65 %, red at 32 % and under (palette indices from `FUN_004fb180`, cached once) |
| `FUN_0046ee80` | An item cel at (x, y): gold by stack (stat 0xe: 100..499 frame 1, to 4999 frame 2, else 3), the item's colour (`FUN_0062c100`), draw mode 1 for an ethereal item (flag 0x400000, else 5), then its overlays (`FUN_0046e300`). Eight callers, the inventory panels |
| `FUN_004706e0` | A unit's frame width and height (`FUN_00601920`, `FUN_00601950`) into two outputs; a missing cel is fatal (0xd7a) |
| `FUN_00470860` | Is (x, y) inside the unit's COF box (`+8..+0xc`, `+0x10..+0x14`) grown by 16 |
| `FUN_004709a0` | Is the unit's COF box on screen (`0x71146c` × `0x711470`, an optional half-width shift) |
| `FUN_00471a30` | Draws a non-unit-type or object in that solid tint (renderer slot 0x94, `FUN_004f6570`); objects need Objects.txt `+0x150` and `+0x118 + mode`, and offset by `+0x148` / `+0x14c` |
| `FUN_00471bd0` | The same for a player, monster or object, layer by layer through the COF; called by the unit draw `FUN_00471ec0` while `FUN_00464c40` counts (unit `+0x58`, state 0x8f) |
| `FUN_004dab40` | Is a cel at (x, y) on screen: adds the cel x/y metrics, adjusts x and width for a flip, then checks x ≤ screen width, x + width ≥ 0, y − radius ≤ screen height and y + radius ≥ 0. The signed comparisons use 32-bit wrapping arithmetic (`tools/emu/engine_cel_bounds.py`; metric getters hooked) |
| `FUN_004db1e0` | When its second stack argument is nonzero, turns the unit's path to a new direction (`FUN_006488a0`) only in neutral / death (players modes 0, 0x11; monsters 0, 0xc) and when EBX ≠ EDI (`tools/emu/engine_neutral_direction.py`; transform and path calls hooked) |
| `FUN_004db290` | Converts a packed direction from 8 to 4 (EAX 8, ECX 4) or 16 to 8 steps (`FUN_00600cb0`), writing through ESI; other pairs leave it alone. The wrapper extracts `(word & 0x1fffffff) >> 3` or `(word & 0x0fffffff) >> 2` respectively, then masks the lookup result to 26 bits and shifts it left by 3 or 2 (`tools/emu/engine_direction.py`; lookup hooked) |
| `FUN_004db570` | A layer's tint: the item drawn on that component (or on TR for the armour layers, `FUN_0064f420`) unless broken or unusable (flags 0x100, 0x4000) or `lit `; `FUN_0062c100`'s colour, else arg 3 |
| `FUN_004db670` | Is the unit on screen with its COF loaded, then `FUN_004db0d0` |

## The COF parameters (GfxUtil.cpp)

`FUN_004db7b0` (cited in [compcode.md](compcode.md)) fills the cel request;
these are its pieces:

| Address | What |
|---|---|
| `FUN_004da720` | Copies a token of up to three characters, space-padded to four (EAX out). Callers `FUN_004db7b0`, `FUN_004dbb80` |
| `FUN_004da770` | A monster's mode byte at MonStats2 extension `+4 + n` (n < 16); 0 unless the record's first dword is 1 and its `+0x14` pointer is non-null. Caller `FUN_004db7b0` |
| `FUN_004da790` | Caches the type tokens: 16 player (`FUN_0065b5e0`), 20 player-mode (`FUN_0065b480`), 16 monster-mode (`FUN_0065b500`), 8 object-mode (`FUN_0065b570`), 3 more (`FUN_0065b620`) at `0x7c8920`, `0x7c88d0`, `0x7c8980`, `0x7c8960`, `0x7c89c0` (fatal 0x36..0x5a) |
| `FUN_004da8e0` | Monster: the request's `+0x2c` = MonStats2 row `+0x138` |
| `FUN_004daa00` | The base token: player (`FUN_0065b480`), monster (MonStats `+0x10`), object (`FUN_0065b570`). Invalid monster indices, missing player/object rows, and other types return zero (`tools/emu/engine_mode_token.py`; player/object lookups hooked) |
| `FUN_004daa70` | Folds a draw type: 0, 1, 2, 4, 6 → 4; 3, 5 → 3 |
| `FUN_004daab0` | The torso item's six armour tier bytes (Items `+0x116..+0x11b`), zeros when bypassed or unusable. A null inventory or missing item-table row leaves the output unchanged (`tools/emu/engine_armor_tiers.py`; equipment and table lookups hooked) |
| `FUN_004dabc0` | An item's cel name (EDI the request, ESI the item): on the ground (`+0x10` 3) a unique's / set's own `+0x3a` / `+0x42`; in the inventory (4) a set's `+0x62` invfile (flag 0x10 in `+4`) or the base's `+0x60`, a unique's `+0x5a` (flag 1) or the base's `+0x40`, else the base `+0x20` or a variant `FUN_0062e920` |
| `FUN_004dad80` | A layer's weapon token (EBX the layer, ESI the unit): `lit ` past 15; with a crossbow (`xbw `) the right hand's alternategfx / code for layers 5, 6; with a bow (`bow `) `lit ` on 5; layer 7 `shh ` when state 0x65 and a shield (`FUN_0063c8f0`); else 0 |
| `FUN_004dae60` | Has the unit layer n: player 0..4, 8, 9; monster the MonStats2 `+0xe8` bit; object Objects.txt `+0x151 + n`; missile layer 1 |
| `FUN_004daf70` | A layer's draw type by EAX input: 0 gives 0x10 (the local player, d2char.mpq present) or 8; 1 uses MonStats2 `+0xf4 + layer`, 4 when 8 and the class slot is clear; 3 uses the record's `+0x1a0`; 6 uses the overlay's. A negative layer returns 1 |
| `FUN_004db090` | COF layer n's byte 1 (9-byte layer records from `+0x1c`, byte 0 the layer id; 0 for a null header, n >= 16 signed, or no match, so a negative n never matches). `FUN_004db050` returns byte 3 the same way. Caller `FUN_00471620` |
| `FUN_004db0d0` | COF layer n's byte 2 |
| `FUN_004db180` | Is it a player, monster or object (type 0..2) |

### Unused list helpers (GfxUtil.cpp)

16-byte nodes (`+0` .. `+8` data, `+0xc` next) with a count and a byte
total: `FUN_004da920` adds one (line 0x81), `FUN_004da990` finds by
(`+4`, `+8`), `FUN_004da9c0` frees all (line 0xa8). None has a caller.

## The motion record (unit graphics `+0x30`)

0x4c bytes (`FUN_004da000`, line 0x4a; `FUN_004da080` frees it, line 0x64;
`FUN_0046f060` / `FUN_0046f070` get and set the pointer),
coordinates in 21.11 fixed point. `FUN_004da000` zeroes the record whether
it allocated it or found it already there (calling it again resets a live
motion); a unit with no graphics record still gets a block allocated, but
`FUN_0046f070` has nowhere to store it, so the block is lost. Every function
below that takes a unit does nothing, or returns 0, when the unit, its
graphics record or its motion record is missing, except `FUN_004da6b0`,
which returns 1:

| Offset | What |
|---|---|
| `+0x00` | flags: 1 stops the step (set on arrival, a finished throw, the last bounce and by `FUN_004da640`), 2 thrown (`FUN_004da5b0`), 4 target set (`FUN_004da2f0`), 8 done, 0x10 follow the owner's frame, 0x20 arrive when at or above the limit |
| `+0x04..+0x0c` | position x, y, z |
| `+0x10..+0x18` | velocity |
| `+0x1c..+0x24` | acceleration (z defaults to `-0x1000`) |
| `+0x28..+0x30` | a third vector: the arrival limit in whole units (`FUN_004da350`) |
| `+0x34..+0x3c` | draw offsets x, y, z |
| `+0x40` / `+0x44` | target (`FUN_004da2f0`). The step reads them as a count that falls by one on each ground hit and a percent of the vertical velocity kept (see the step) |
| `+0x48` | flight time (ticks left, at least 1 when thrown) |

| Address | What |
|---|---|
| `FUN_004da0b0` / `FUN_004da0d0` / `FUN_004da0f0` | Draw offset x, y, z (0 without a record) |
| `FUN_004da110` / `FUN_004da130` / `FUN_004da150` | Position x, y, z in whole units (>> 11) |
| `FUN_004da170` / `FUN_004da190` / `FUN_004da1b0` | Position x, y, z raw. x and y have no caller |
| `FUN_004da1d0` | Sets the position (<< 11) |
| `FUN_004da200` / `FUN_004da250` / `FUN_004da2a0` | Set velocity / acceleration / the third vector, shifted when arg 5 |
| `FUN_004da2f0` | Sets the target, flag 4: `+0x40`, `+0x44` from its two arguments |
| `FUN_004da320` | `+0x40` (the target's first value) |
| `FUN_004da340` | Clears flag 2 |
| `FUN_004da350` | The per-tick step (stack: unit; the record's flag 1 stops it): position += velocity, velocity += acceleration, all three axes (the old velocity moves the position). Flag 2: `+0x48` counts down, then flag 1 is set and x and y are zeroed. Flag 4 (ground bounce): once `z >> 11` is at or below `+0x30`, vz becomes `+0x44 × vz / −100` (truncated), z becomes the raw `+0x30` (not shifted, so a hit leaves z tiny), and `+0x40` counts down; at 0 flag 1 is set. Else flag 0x10: the owner unit (`FUN_004639d0`) gives the frame size (`FUN_004706e0`, mode 0xe for a monster-type unit (type 1) or a missile (type 3), 8 otherwise; for a missile the owner must be a monster, fatal 0x1a9, and an owner mode of 0 or 0xc ends the step with nothing stored). `+0x34` = width + the owner's draw x (`FUN_004da0b0`), `+0x3c` = height (+10 for a missile) + the owner's draw z (`FUN_004da0f0`); then `+0x34` / `+0x38` are copied, unshifted, into position x / y, `FUN_00643510` converts them in place (it maps the draw offset back to a position), and only z is shifted: z = −(`+0x3c` << 11). Else arrival: with flag 0x20 clear, if x, y, z (each `>> 11`) are all at or below `+0x28`, `+0x2c`, `+0x30` the position is set to that limit and flag 1 is set; with flag 0x20 all must be at or above. Unless flag 0x10, the tail sets `+0x34` / `+0x38` to x / y `>> 11`, `FUN_00643290` converts them, `+0x3c` = −(z `>> 11`) |
| `FUN_004da5b0` | A throw from height h (EDX) over t ticks (stack): flag 2, t kept in `+0x48` and raised to 1 if below (signed), position (0, 0, h << 11), az `+0x24` set to `-0x1000` when zero, vx and vy zeroed, velocity z = (−(h << 11) − az·t²/2) / t. All in 32-bit wraparound; the half rounds toward zero and the divide truncates (idiv) |
| `FUN_004da620` / `FUN_004da6e0` | Set (EDX non-zero) or clear flag 0x10 / 0x20 |
| `FUN_004da640` | Sets flag 1. `FUN_004da660` tests it (no caller) |
| `FUN_004da690` | Done: clears 1, sets 8. `FUN_004da6b0` returns 0 only when flag 1 is set and flag 8 is not, else 1 (also with no record) |

## Checked against game.exe

Two oracles run the real code on random synthetic records with the pool,
owner and screen-conversion calls hooked (what each prevents is in the
script's docstring), and compare return value, callee order and arguments,
every byte of the records, the fatal line and the bytes `ret n` pops. They
check game.exe's behaviour for these functions, not d2d's code: nothing in
`apps/` or `components/` cites them yet.

| Oracle | Functions |
|---|---|
| `tools/emu/engine_motion.py` | `FUN_0046f060`, `FUN_0046f070`, `FUN_004da000`, `FUN_004da080`, `FUN_004da0b0` ... `FUN_004da1b0`, `FUN_004da1d0`, `FUN_004da200`, `FUN_004da250`, `FUN_004da2a0`, `FUN_004da2f0`, `FUN_004da320`, `FUN_004da340`, `FUN_004da350` (every branch of the step is reached), `FUN_004da5b0`, `FUN_004da620`, `FUN_004da640`, `FUN_004da660`, `FUN_004da690`, `FUN_004da6b0`, `FUN_004da6e0` |
| `tools/emu/engine_gfx_small.py` | `FUN_0046e100`, `FUN_0046e570`, `FUN_0046e5a0`, `FUN_0046e610`, `FUN_0046e690`, `FUN_0046eac0`, `FUN_0046ec80`, `FUN_0046ece0`, `FUN_0046f000`, `FUN_0046f020`, `FUN_0046f1c0` ... `FUN_0046f250`, `FUN_0046f280`, `FUN_0046f980` ... `FUN_0046f9d0`, `FUN_004da720`, `FUN_004da770`, `FUN_004da8e0`, `FUN_004da920`, `FUN_004da990`, `FUN_004da9c0`, `FUN_004daa70`, `FUN_004dad50`, `FUN_004db040`, `FUN_004db050`, `FUN_004db090`, `FUN_004db0d0`, `FUN_004db180`, `FUN_004db1a0`, `FUN_004db1c0` |

Changelog of what the checks changed in the rows above (each is also applied to its row):

- `FUN_004da350` was not in this page; it is the step above.
- `FUN_004da000` re-zeroes an existing record, and the block is lost when the unit has no graphics record.
- `FUN_0046eac0` uses n only for its check (n >= 16, signed, is fatal 0x65f); it always clears flags 0 to 15. `FUN_0046f280` does nothing at all when n >= 16 (signed).
- `FUN_0046e610` returns 0 (not found) as well as 1; `FUN_0046e690`'s fatal 0x406 cannot be reached because the node it frees was just found, and its other-token path writes `*arg 2` = 0 before the old data goes to `*arg 3`.
- `FUN_004db050` returns byte 3 of the layer record (`FUN_004db090` byte 1, `FUN_004db0d0` byte 2); for all three, a layer number of 16 or more (signed, so a negative number is looked up and never matches) gives 0.
- `FUN_004da770` also needs the record's first dword to be 1, and the data pointer at `+0x14` to be non-null.
- `FUN_004da920` adds one to the caller's count and 0x10 to its byte total; `FUN_004da9c0` takes 0x10 off the total per node but leaves the count alone.

Not checked: the transform-state branch of `FUN_0046da60` / `FUN_0046d9f0` (unit
`+0xc8` bit 3 set), the loaders and drawing functions of Gfx.cpp
(`FUN_0046ddf0`, `FUN_0046f490`, `FUN_0046f650`, `FUN_0046fb60`, `FUN_00470070`, `FUN_00470390` ...),
`FUN_004da790`, `FUN_004daa00`, `FUN_004dab40` ... `FUN_004db7b0` (the ones not listed above).
