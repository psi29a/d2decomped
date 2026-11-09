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

The skills: the ones the other set kept come back: `FUN_00623120` reads the
right switch pair (player data +0x80 / +0x88) and `FUN_006231a0` the left
pair (+0x84 / +0x8c). Neither clears the pair it reads; each resets the
in-use pair of its hand (+0x70 / +0x78 for the right, +0x74 / +0x7c for the
left) to (0, -1) and sets +0x90 = -1, +0x94 = 0. The ones in use are kept in
the switch pair (`FUN_00622f80` right, `FUN_00622ff0` left).
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
the switch's 11, 12) `FUN_0063dd90` (EAX the item in the slot, ECX the
other hand's, EDX the cursor's; the disassembly at 0x63dee5..0x63deff).
With an item on the cursor: 1 the empty hand takes it, 2 it takes it but
the other hand's item doesn't pair (that one goes to the cursor), 5 a
swap, 6 a stack, 7 a swap whose other hand's item goes to the inventory,
0 never. With none: 3 the hand's item is picked up, 4 an empty hand
beside a two-hander picks the two-hander up. items.md "Into a hand" has
the table and the server function for each. The client's hand handlers
`FUN_00490780` / `FUN_00490ba0` (body-loc table `0x721e58`) send codes
1 to 7 on; the other slots' `FUN_00490fc0` only 1, 3, 5.

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
  `FUN_004c3070`, `FUN_004c35f0` (the client's), and on the server the
  handlers below.
- `FUN_0063d2b0(inventory, item)` is the other half, for a weapon coming
  off: it clears `+0x1c` if that is the item's id, then, the item's data
  byte 4 or 5 naming its hand, the other hand's item (`FUN_0063bde0`), if
  usable (`FUN_0062a4e0`; any item, a shield too), takes `+0x1c`. The
  item is still in its cell when it's called.
- `FUN_0063d190` sets or clears `+0x1c` by a flag; nothing calls it.

Among game.exe's `mov [reg+0x1c], reg` writes these are the only ones in
the inventory code (0x63xxxx); the others were not each read. On the
server each item move calls them in this order:

| Server function | What moves | Calls |
|---|---|---|
| `FUN_005606b0` (code 1) | into an empty hand | `FUN_0063d1d0` |
| `FUN_00563d20` (code 2) | the other hand's to the cursor, the new one in | `FUN_0063d2b0`, `FUN_0063d1d0` |
| `FUN_00560f00` (code 5) | a swap | `FUN_0063d2b0` (the other hand still full), `FUN_0063d1d0` |
| `FUN_00561220` (code 7) | the other hand's to the inventory, this hand's to the cursor, the new one in | `FUN_0063d2b0` twice (the other hand's first, while this hand's is still in), `FUN_0063d1d0` |
| `FUN_00560cd0` | a hand's item to the cursor (and a sale, `FUN_00579510`) | `FUN_0063d2b0` |
| `FUN_005616a0` | the switch: hands 4 / 5 to 0xb / 0xc, then 0xb / 0xc to 4 / 5 | `FUN_0063d2b0` right then left, then `FUN_0063d1d0` right then left |
| `FUN_00562e00` | an item worn from the ground or a store (`FUN_0055d710`) | `FUN_0063d1d0` |
| `FUN_00562a30` | the next stack into an emptied hand | `FUN_0063d1d0` |
| `FUN_00562f30` | the corpse's items back on | `FUN_0063d1d0` |
| `FUN_005606b0` from `FUN_00531210` (a save's items, `FUN_005335e0` mode 0) or `FUN_00534bb0` (a new character's gear, `FUN_00534d60`) | a worn item, requirements skipped (its 5th argument 1) | `FUN_0063d1d0` for slots 4 / 5, not 0xb / 0xc |
| `FUN_0055f850` | an item breaks (flag 0x100) | `FUN_0063d2b0` |
| `FUN_0055f900` (from `FUN_005761c0`, a repair of a broken item) | the break mended | `FUN_0063d1d0` |

So the first weapon in gets the id and keeps it while it stays, and a
swap of the wielded weapon in a dual-wield hands the id to the other
hand's. `FUN_0055dbc0` (the stats redone) calls the pair only for a
body item whose requirements stop or start being met (flag 0x4000 set,
then `FUN_0063d2b0`; cleared, then `FUN_0063d1d0`) and, in its first
pass, for charms (ItemTypes row 0xd, char: `FUN_0062ff70`).

A save's items go in in the file's order: all four readers of the player's
list (`FUN_00533c70`, `FUN_00534020`, `FUN_00566cc0`, `FUN_0056a7e0`) call
`FUN_005337f0` with mode 0 (the 4th stack argument, pushed as 0 at
0x533f05, 0x534235, 0x566d37 and 0x56a7f1), so each item goes through
`FUN_00531210`; a worn one (item mode 1) through `FUN_005606b0`. Mode 1
(`FUN_00531520`, placed with no `FUN_0063d1d0`) is only the corpse's own
list (`FUN_00533850`). So a loaded character's wielded weapon is the
first usable one in a hand in the save's order.

d2d: the item's `wielded` (d2s_items.hpp, not saved; the View carries
it) is the id. `rules::wielded_weapon` is `FUN_0063bef0`,
`rules::wield_enters` `FUN_0063d1d0`, `rules::wield_leaves`
`FUN_0063d2b0`, called where the table's moves are made
(`rules::equip`, `store_buy`, `pick_up`, `store_repair_all`, the World's
sale, repair, switch, cursor and corpse, `Fight`'s breaks and refill).
A loaded or new character's hands take it in the items' order
(`rules::wield_settle`, from `World::take_character`). The 0x4000 flag
`FUN_0055dbc0` sets after each of those (requirements not met) is
`rules::settle_requirements` (items.md "Requirements kept"). `components/game/world/objects.cpp` (Khalim's
Will in either arm) doesn't use it. `FUN_0063add0` is not ported.

### The other hand and the dual-wield stats (`FUN_0063bf90`, `FUN_00535e20`, `FUN_004e9260`, `FUN_004e9300`)

Cited. `FUN_0063bf90(inventory)` is the weapon **not** in hand: it reads
cells 5 and 4 of the body grid as `FUN_0063bef0` does (weap items only);
when one has id `+0x1c` it returns the other; no wielded id gives 0.
Throw doesn't use it: its "other hand" is `FUN_00643d60` (EAX the
inventory, the argument a hand's body slot), the item in the other body
slot (`FUN_0063bde0(inventory, 4)` for 5, else 5), whatever it is.
throw.md has how Throw picks.

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

## Dropping hand skills a change of gear made unusable (`FUN_0055c5c0` chain)

Cited, not proven. Several of these take arguments in registers the
decompile does not show (the asm was not read), so a hand's mapping to a
local is read from the order of the calls only.

### The wielded weapon and the other hand's weapon (`FUN_0055c470`)

`FUN_0055c470(unit)` (unit in EAX, two outputs in EBX and EDI) zeroes both
outputs and, for a unit with an inventory (`unit+0x60`), sets EDI to the
wielded weapon (`FUN_0063bef0`, d2d `wielded_weapon`) and EBX to the
item in body slot 4 or 5 (`FUN_0063bde0`) that is not that weapon: slot 5's
when the wielded one is slot 4's, else slot 4's. EBX is cleared unless
it is of ItemTypes row 0x2d (`FUN_00629bb0`, weap).
Evidence: `python3 tools/ghidra/fn.py 0055c470`, `python3 tools/ghidra/calls.py 0055c470`.
d2d: not ported; no mention in `components/` or `apps/`.

### Is the skill still usable (`FUN_0055c4d0`)

`FUN_0055c4d0(unit, entry)` (unit in ECX, entry in EAX) runs `FUN_00647960`
(skills.md "Can the skill go now") and returns 0 for status 2 (the weapon-type
gate or a spent charge) or 7 (level 0), else 1. Every other reason, mana and
cooldown included, counts as usable. Callers: `FUN_0055c560`. The same
2-or-7 test is in `FUN_0055c5c0` (inline) and d2d's `skill_refused`
(`components/game/fight.hpp`), which keeps status 2 and 7 only.
Evidence: `python3 tools/ghidra/fn.py 0055c4d0`, `python3 tools/ghidra/fn.py 00647960`.
d2d: reads the same, cited from the decompile, no oracle run (`skill_refused` ports the two statuses; `fight.hpp:146`).
