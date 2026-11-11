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
