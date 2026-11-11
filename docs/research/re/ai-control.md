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
The difficulty-indexed words each have stride two. No runtime oracle has
been run for this trace or for the called helpers.

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
