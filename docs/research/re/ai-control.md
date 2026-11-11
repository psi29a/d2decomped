# AI control accessors — game.exe 1.14d

`FUN_0058ec50` (ECX unit, EDX selector) accepts a non-null unit of type 1,
its non-null monster data at unit +0x14, and its non-null control at monster
data +0x28. Selectors 1, 2, and 3 return the control's dwords at +0x14,
+0x18, and +0x1c respectively. Any failed gate or other selector returns 0.

`FUN_0058f000` (unit on the stack, callee pops four bytes) returns the
**address** control +0x38; it does not read the leader ID at +0x2c. Invalid
unit, non-monster unit, or null monster data returns the literal 0x38. A
valid monster with null control also returns 0x38. The field at +0x38 is
released by `FUN_0058f810` via `FUN_00666130` when nonzero; this does not by
itself establish its complete role.

`tools/emu/ai_control_access.py` checks both functions with synthetic records,
including each pointer gate and selectors outside 1..3, against the local
stock executable. It checks the returned values, not the behavior of callers
that dereference the returned address.

`FUN_0058ebc0` takes a game in ECX and allocates 0x40 bytes from the pool at
game +0x1c using the AiGeneral.cpp line 0x1b allocation site. It clears the
block, then sets control +0x0c and +0x2c to -1 and +0x10 and +0x30 to 1.
The oracle uses a prefilled allocation and checks the pool, size, source site,
and every byte of the resulting block. Allocation failure is not tested.

`FUN_005ea080` takes the game in ECX, unit in EDX and think record on the
stack. When control +0x18 is 1 it sets unit flag 0x20000 (if the unit exists)
and calls `FUN_005ddfc0` with three zero stack arguments. Otherwise it calls
that helper with 4, x minus 10, y and sets control +0x18 to 1. A null unit is
only safe on the first branch. Unit types 2, 4 and 5 read coordinates at path
+0x0c / +0x10; other types use the dynamic path getters, or zero for a null
path. The oracle scripts the action helper and dynamic getters and checks both
branches; it does not establish the helper's effects on the rest of the game.

`FUN_005e0160` (game ECX, unit EDX, think record on the stack) is a limited
spawner, not a generic skill application function. Static trace: if control
+0x14 is zero it copies the signed difficulty-indexed MonStats word at +0x56
into control +0x18. When think +0x14 is at most the signed word at +0x5c,
an exhausted count calls `FUN_005ddfc0` with three zeros and returns. With a
positive count and a due frame (control +0x14 <= game +0xa8), it gets the
unit's room, passes row 0x60 or -1 according to the table count, and asks
`FUN_0054dc40` for a location. A successful location and `FUN_005b2f20`
spawn decrement the count and set the next frame to the signed word at +0x62
plus the current game frame. Other paths end at `FUN_005de080` with 15.
The difficulty-indexed words each have stride two. The called helpers are
scripted in the oracle described below.

`FUN_0058ec90` allocates a zeroed 0x1c-byte command node for a valid monster
with a control, and inserts it immediately before control +0x20 in the
circular doubly linked command list. For the first node, +0x20 and +0x24
both point to the self-linked node. When +0x24 equals +0x20 on insertion,
the former is changed to the new node. `FUN_0058ed80` repeatedly removes
the current node and frees it until the ring is empty, clearing both control
pointers on the last node. The first function takes game in ECX and unit in
EDX; the second takes game in EDI and control in ESI. The oracle
`tools/emu/ai_command_ring.py` checks invalid inputs, three insertions and
the free order with synthetic allocations; allocation failure is not tested.

`FUN_0058f350` takes game ECX and the first node of a singly linked minion
list EDX. It frees each node through the pool at game +0x1c, in list order,
using AiGeneral.cpp line 0x246; null is a no-op. `FUN_0058f810` takes game
ECX and control EDX. Null control is a no-op; otherwise, it first passes a
non-null control +0x38 to `FUN_00666130`, then frees the nodes at control
+0x34 in list order, releases the command ring via `FUN_0058ed80`, and
finally frees the control at line 0x19d. The allocator and optional record
release are hooked by `tools/emu/ai_control_free.py`; the command-ring
routine runs for real. The oracle checks an empty control and a populated
control, including release ordering and null inputs.

`FUN_005e7dc0` takes game ECX, unit EDX and a think record on the stack. It
passes copies of think +0x14 and +0x18 to `FUN_005ddc30`. A non-null result
and a signed returned distance below 25 call `FUN_005ddf90` with mode 4 and
the target; otherwise `FUN_005de080` stands for 50. The oracle
`tools/emu/ai_town_rogue.py` hooks these three callees, checks their arguments
and tests the no-target and boundary-distance branches. This does not check
the target search or attack effects themselves.

`FUN_0058f160` takes a minion in ECX. For a monster with data, control and
control +0x28 game, it resolves the owner using control +0x30 type and +0x2c
ID. If the owner is a monster, it scans the owner's +0x34 list for the
minion's unit +0x0c ID. On finding it, it resolves that ID as a monster;
if the result exists and is type 1, its control gets owner ID -1, owner type
1 and game copied from the owner's control. It unlinks and frees the matching
node regardless of that second lookup's result. A missing owner or list entry
leaves the list alone. The function assumes resolved monster data and control
exist when it accesses them; the oracle does not test malformed records.
`tools/emu/ai_minion_remove.py` scripts both lookups and the allocator, and
tests removal at each list position, a missing ID and a missing second lookup.

`FUN_0058f260` disbands a leader's minion list. It takes the prospective
leader ECX, resolves its control's owner, and proceeds only when the result
is the same unit. Each node is resolved as a monster; a found monster's control
gets owner ID -1, owner type 1 and the leader's game pointer. Each node is
freed, including one whose lookup failed, and the list head becomes null.
`tools/emu/ai_minion_remove.py` checks this with two resolved members, one
missing member and the owner-not-self no-op; lookups and free are hooked.

`FUN_0058f440` takes a unit in ECX, the first output pointer in EDX and four
more output pointers on the stack. A null or non-monster unit, or a monster
whose control +0x30 is not 1, writes -1 to the first output and leaves the
others untouched. Otherwise it copies control +0x2c and +0x30 to the first
two outputs, calls `FUN_005dd220` with the control and flags 1 and 2 for the
next two outputs, and copies the minion-list head to the fifth. It resets
owner ID to -1 and clears the list head. Valid monster data and control are
assumed; `tools/emu/ai_minion_detach.py` tests the valid and early-return
paths with the flag helper hooked, not malformed monster data.

`FUN_0058f4d0` takes game ECX, unit EDX and five stack arguments. For a
monster unit it frees the existing minion list using `FUN_0058f350`, passes
the first four stack arguments with game and unit to `FUN_0058f030`, and
then stores the fifth as the new list head. A null or non-monster unit is a
no-op. The oracle `tools/emu/ai_minion_detach.py` checks the call ordering,
arguments and replacement head, with `FUN_0058f030` and pool free hooked;
the node-list free runs for real. It does not test malformed monster data.

`tools/emu/ai_limited_spawn.py` checks eight normal-difficulty cases of
`FUN_005e0160`, including initialization, range, exhaustion, the due-frame
boundary, failed location and spawn, and successful spawn with either class
row selection. Its room, class, location, spawn and action helpers are hooked;
it does not check those helpers or the other two difficulty rows.

`FUN_005e0260` is a skill-driven spawn think. Its think record supplies the
target at +8 and MonStats row at +0x1c. Within the signed difficulty-indexed
range at row +0x5c, a control +0x18 count greater than the signed limit at
row +0x56 sets unit flag 0x20000 (for a non-null unit) and calls
`FUN_005ddfc0` with zeros. Otherwise, with skill 1 at row +0x170 nonnegative
and control +0x14 strictly before the game frame, it asks `FUN_005fd350` for
a placement using class 0x14e when the table count exceeds 0x14e, else -1.
Successful placement increments the count, sets the next frame from the
signed difficulty-indexed delay at row +0x62, and calls `FUN_005dead0` with
the mode byte at +0x180, skill ID and target. Other paths stand 25 frames.
`tools/emu/ai_sarcophagus_spawn.py` checks seven normal-difficulty decisions
with coordinate, room, placement and action helpers hooked; it does not check
their implementations or the other difficulty rows.

`FUN_005e7f80` reads the current AI command's ID at +8. ID 8 immediately
unlinks the unit and calls `FUN_005b0e00` with its control and zero. Otherwise
it scans the group through `FUN_0058f380` and queries `FUN_005dd2b0`; if either
reports nonzero, or the signed think distance is below 20, it broadcasts a
command 8 before the same unlink and switch. All paths call `FUN_005de080`
with 10. `tools/emu/ai_minion_retreat.py` checks six branch combinations with
these helpers scripted; it does not check the helpers' effects.

`FUN_005e5730` looks up the unit's owner with `FUN_0058f0d0` and stores the
unit's x and y minus the owner's in control +0x14 and +0x18. Unit types 2, 4
and 5 use their path's +0x0c / +0x10 coordinates; other types use dynamic
path getters, with a null path contributing zero. When owner and unit are the
same, it also advances the unit seed once and stores its low word modulo 100
in control +0x1c. A missing owner reaches an assertion and is not tested.
`tools/emu/ai_formation_offset.py` checks five owner, coordinate and seed
cases with owner lookup and dynamic getters hooked.

`FUN_005de330` handles a 16-bit dialogue line. Line 0 does nothing. With its
last argument zero, it formats the unsigned line as decimal, releases a
non-null unit's old chatter at +0xa4, and asks `FUN_00661110` for a new node.
On success it stores that node when the unit exists, calls `FUN_0064c040`,
sets unit flag 0x100 when possible and schedules sound event 6. If allocation
returns null after an old node was released, the unit's +0xa4 is not cleared
by this function. With the last argument nonzero it instead builds a packet
of type 0x27 containing the unit ID (or -1) and line and calls the packet
helper. `tools/emu/ai_dialogue.py` checks seven local and packet decisions
with formatting, allocation, sound and packet helpers scripted; it does not
exercise their implementations or send network data.

`FUN_005e7c10` idles 5 frames for a null unit or a unit whose mode is not 1.
For an active unit outside town it searches using `FUN_005dd7f0`. With a
target it draws `FUN_0045c390` in [0, 100): outside melee, a draw below 30
calls `FUN_005dec80(target, 7)`; in melee, a draw below 80 calls
`FUN_005ddf90(4, target)`. Other target cases idle 10. In town or without a
target it advances the unit seed once and calls the wander helper for 5 when
the low word modulo 100 is below 20, otherwise idles 10.
`tools/emu/ai_npc_melee.py` checks eight cases, including both roll boundaries,
with room, town, search, bounded-roll and action helpers hooked.

`FUN_005e7ac0` idles 5 frames for a null or non-mode-1 unit. Outside town it
searches for a target and, at distance strictly below 20, draws [0, 100): a
draw below 30 attacks (or, for class 0x10f, uses skill 1 and its mode). A
first draw of 30 or more triggers a second draw; below 30, it calls
`FUN_005df7d0(target, 4, 0)`, otherwise it idles 10. Town, no target and
distance 20 or more instead advance the unit seed and wander for 5 when the
low word modulo 100 is below 20, else idle 10. `tools/emu/ai_npc_ranged.py`
checks eight cases with search, random and action helpers scripted; the
helpers and downstream actions are not checked.

`FUN_005e7e20` passes monster data +0x30 to `FUN_00572dc0`; a nonzero result
idles 10 frames. Otherwise it asks `FUN_005ddf20` for a player and an output
flag. For a player unit whose `FUN_00535060` result is zero, a nonzero draw
from `FUN_0045c390(seed, 3)` and a nonzero output flag enter a timer branch.
Control +0x18 zero becomes 60 and `FUN_00553380(unit, 0x12, player)` is
called; a negative timer becomes zero, and a positive timer decrements by 1.
This branch idles 20. Other cases search using `FUN_005ddc30`; a target at
signed distance below 25 attacks with mode 4, else the unit idles 50.
`tools/emu/ai_good_npc.py` checks eight timer, gate and fallback cases with
the lookup, status, random and action helpers scripted.

`FUN_005e8340` obtains an owner from `FUN_00553540`. A missing owner or one in
town calls `FUN_005b0e00` with the unit's control and zero, `FUN_00540e60`
with 2 and 0, then schedules sound event 2 at game frame +1. Otherwise a zero
control +0x14 becomes 1 and calls `FUN_005ded40(owner, 7)`. With a nonzero
value, `FUN_00622c40` succeeding attacks the owner in mode 4; otherwise the
search `FUN_005de890` can return a target and a nonzero output flag, causing
the control word to clear and a mode-4 attack on that target. Failing that,
the word becomes 1, `FUN_00620c10` receives the unit and owner, and the unit
calls `FUN_005ded40(owner, 7)`. `tools/emu/ai_stationary_npc.py` checks six
branches with owner, room, targeting and action helpers scripted.
