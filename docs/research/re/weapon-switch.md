# The weapon switch — game.exe 1.14d

An expansion character's second set of hands (W, "Swap Weapons"). d2d:
`cmd::SwapWeapons` (protocol.hpp), `World::apply`, the inventory's set II
art in `draw_inventory` (panels.cpp), `tests/smoke_d2d.py`.

## The packets

- **C→S 0x60** (1 byte) → `FUN_0054ce70`: only on an expansion game (client
  +0x70); `FUN_00620250` then the mode check `FUN_005541b0` (a player in
  mode 0 death or 0x11 dead can't), then `FUN_005616a0` does it.
- **S→C 0x97** (1 byte): the client toggles `DAT_007bcc4c`, the set its
  inventory shows. Reset to 0 when the panel's art loads (`FUN_004868b0`).
- **S→C 0x9d action 0x17** (`FUN_004c3b00`): an item's new place after the
  switch.

## The switch (`FUN_005616a0`, ItemMode.cpp)

The items trade places; nothing tells "the set in use" apart from where
they are:

| from body slot | to |
|---|---|
| 4 right arm | 0xb |
| 5 left arm | 0xc |
| 0xb | 4 |
| 0xc | 5 |

Each is lifted from the body (`FUN_0063bde0` / `FUN_0063ad90`) and put back at
its new slot (`FUN_0063bdb0`, `FUN_0063b210`, `FUN_00627d70`); the stats are
redone (`FUN_0055dbc0`).

The skills: the left / right ones the other set kept (player data +0x84 /
+0x8c, read and cleared by `FUN_006231a0` / `FUN_00623120`) come back, and
the ones in use are kept in their place (`FUN_00622f80` / `FUN_00622ff0`).
A kept skill that's no longer usable (`FUN_00647960` says 2 or 7: not
usable, or not in the skill list) gives way to the skill list's own
(`FUN_00620190` / `FUN_006201d0` → `FUN_00643930` / `FUN_00643960`,
untraced); d2d gives Attack. The choice goes out as a select skill
(`FUN_00571c60`).

The save: header +0x10 is the set showing (0 I, 1 II); items are saved where
they are, so the hands in body slots 4 / 5 are always the set in use (a
real save on II: the bow in 4, the set I spear and shield in 0xb / 0xc).
The skill pair at +0x80 / +0x84 is what the other set kept.

## The inventory (`FUN_0048edf0`, `FUN_004845a0`)

With set II showing on an expansion game, `Panel\invchar6Tab` (loaded by
`FUN_004868b0` into `DAT_007bce84`) draws frame 0 over the left arm's box
and frame 1 over the right arm's, each at the box's left − 5 and bottom + 4
(`0x722100..0x72210c`: −5, 4, −5, 4). The two hands' items draw 4 right
and 4 up. Set I draws nothing extra (the panel's own art has the I tabs).

A click on the I / II tabs (`FUN_00486860`, from the inventory's click
handler, the cursor empty, an expansion game): within the 0x18 px above
either hand's box (x from its left to its right edge, y from its top
− 0x18 to its top), `FUN_0048a730` switches as W does. Hovering there
shows "Swap Weapons" and its key (untraced; d2d shows nothing).
d2d: `item_cursor_command` (cursor.cpp).

ponytail: another player's switch (0x9d action 0x17) isn't followed.

## What two hands can hold (`FUN_0063dbc0`, from `FUN_0063dd90`)

`FUN_0063de60(player, body loc, item, …)` asks of a hand slot (4, 5, or
the switch's 11, 12) `FUN_0063dd90(other hand's item, new item, item in
the slot)`: 1 the empty hand takes it, 2 it takes it but the other hand's
item doesn't pair (it comes off), 5 a swap, 6 a stack, 7 a swap that
also unseats the other hand, 0 never. The client's hand handlers
`FUN_00490780` / `FUN_00490ba0` (body-loc table `0x721e58`) send codes
1, 2, 5, 6, 7 on; the other slots' `FUN_00490fc0` only 1, 3, 5.

The pairing, `FUN_0063dbc0` (ESI, EDI the two items, EBX the unit):

1. Either hand empty: yes.
2. One item's ItemTypes **Shoots** (`+0xc` of its own type's 0xe4-byte
   record, `FUN_0062e6f0`: bow, xbow and abow have bowq / xboq) and the
   other is of that type along the Equiv chain (`FUN_00629bb0`): yes.
3. Either has a **Quiver** (`+0xe`, `FUN_0062e740`: bowq, xboq, mboq,
   mxbq): no. So arrows go beside a bow and nothing else; bolts beside a
   crossbow; neither beside a staff or a sword.
4. Either is two-handed (`FUN_006289c0`) and not a Barbarian's 1or2handed
   (`FUN_0062a1e0`, the base's `+0x13d`): no.
5. Both weapons (type 0x2d): a Barbarian, or an Assassin with two claws
   (0x43); else no. One weapon: yes. No weapon (two shields): no.

d2d: `rules::hands_ok`, `ItemType::shoots` / `quiver`;
`tests/test_rules.cpp` (issue #139: arrows beside a staff).

## The wielded weapon (`FUN_0063bef0`, `FUN_0063d1d0`, `FUN_0063add0`)

Cited, not run in an oracle (the register arguments are read from the
decompile; `tools/emu/auto_belt.py` already lays out the same inventory).

The inventory record (magic `0x1020304`) keeps a **wielded id** at `+0x1c`
(`-1` none): the unit id (`+0xc`) of the weapon in hand.

- `FUN_0063add0(dims)` (EAX the grid index, EDI the inventory; the
  asserts `.\INVENTORY\Inventory.cpp` lines 0x2d1, 0x2e4 and 0x2eb) finds
  grid *index* in the array at `+0x14` (count `+0x18`, 16-byte records:
  width `+8`, height `+9`, cells `+0xc`, a `w*h*4`-byte zeroed array). A
  `dims` of NULL only looks; else it allocates the cells on first use, grows
  the array (zeroed) when *index* is past the count, and returns 0 when the
  stored size differs from `dims`. It returns the record.
- `FUN_0063bef0(inventory)` is the weapon in hand: 0 when `+0x1c` is -1;
  else the item in cell 5 (`+0x14` of the body grid, `DAT_0074479c`; the
  left arm) if it is of type weap (0x2d, `FUN_00629bb0`) and its id is
  `+0x1c`; else the same test on cell 4 (the right arm). So the cell 5 hand
  wins only when it is the selected one; the id decides, not the hand.
- `FUN_0063d1d0(inventory, item)` keeps `+0x1c` right when an item changes.
  It acts only on a weapon whose item data byte `+0x44` (`FUN_00627d40`)
  is 4 or 5. Unusable (`FUN_0062a4e0`: flag 0x100 or 0x4000): it clears
  `+0x1c` if that is the item's id. Usable: `+0x1c` takes the item's id when
  `FUN_0063bef0` is 0, not a weapon, or ItemTypes row 0x26 (tpot);
  otherwise (another real weapon is wielded) it clears `+0x1c` if that is the
  item's id. Callers: `FUN_004c12f0`, `FUN_004c1350`, `FUN_004c2e90`,
  `FUN_004c3070`, `FUN_004c35f0`.

d2d: reads the same, cited from the decompile, no oracle run, as `components/game/fight.hpp` (`throw_ready` ponytail:
the primary is `+0x1c`) and `components/game/world/objects.cpp` (Khalim's
Will in either arm). One refinement for that ponytail: the "primary" is
the selected id, tested in cell 5 then cell 4. `FUN_0063add0` and
`FUN_0063d1d0` are not ported.

### The other hand and the dual-wield stats (`FUN_0063bf90`, `FUN_00535e20`, `FUN_004e9260`, `FUN_004e9300`)

Cited. `FUN_0063bf90(inventory)` is the weapon **not** in hand: it reads
cells 5 and 4 of the body grid as `FUN_0063bef0` does (weap items only);
when one has id `+0x1c` it returns the other; no wielded id gives 0. This
is the "other hand" the `throw_ready` ponytail names (it calls it
`FUN_00643d60`, which this trace did not read: unchecked whether it is
this function or a wrapper of it).

`FUN_00535e20(unit, flag)` (flag non-zero): for the wielded weapon, if
`FUN_00625820(item, 0)` (the item's data record `+0x34` has bit 0x80000000;
returns its `+0x44`) is non-zero, `FUN_00627910(unit, item, 1)`; then the
same with flag 0 for the other hand's weapon. `FUN_00627910` is the
item-stat toggle: it applies or removes an item's stat list by its data
flag 0x40000000 (`FUN_00627a40` / `FUN_00627860`), or sockets props
(`d2d_rules_socket_props`) when the owner differs. A 11 / 12 item data
byte (`FUN_00627d40`) goes to `FUN_006269f0` instead.

`FUN_004e9260` / `FUN_004e9300` (EBX the unit, only for a dual-wielder,
`FUN_006235a0`): take the wielded weapon, and the item in the body slot
that is the other hand (`FUN_0063bde0(inventory, 4 + (data byte == 4))`).
If that is a different weapon and both are usable (`FUN_0062a4e0`), the
roles swap: `FUN_004e9260` toggles the stats of the old wielded one with
flag 0 and the new one with flag 1 and returns the new one;
`FUN_004e9300` toggles the other one with 0, the wielded one with 1 and
returns the wielded one (else it still toggles the wielded with 1 when
usable). The ids `+0x1c` are not changed by either.

d2d: not ported (no mention under `components/` or `apps/`).
