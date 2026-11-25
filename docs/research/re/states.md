# States: how they look

A state (States.txt) changes a unit's look three ways. d2d:
`GameData::StateInfo` (loaded in `load_tables`, gamedata_load.cpp), `dress` and
`monster_states` / `player_states` (session.hpp), drawn in `render_world`.

## Colour shift

`colorpri` (record +0x20) and `colorshift` (+0x21). Of the unit's states,
the one with the highest colorpri colours it: every pixel goes through a
PL2 colour-shift table before the light table.

- The tables start at PL2 +0x53500: 24 hue rotations built by
  `FUN_00605f00` (when it makes a PL2), then the rest, 111 in all (the
  PL2 is 0x6c327 bytes: base, 32 light, 16 inventory, the selected-unit
  shift at +0x3400, 3 alpha, additive +0x33500, multiply +0x43500,
  shifts +0x53500, ...).
- `colorshift` counts from 1. The states named for their colour show it:
  "blue", "cold" and "freeze" (108) are table 107, blue (108 is purple);
  "red" (100) is 99, red; "poison" (104) is 103, green. The reader wasn't
  traced; confidence medium.

## Overlays

`overlay1..4` loop while the state lasts; `castoverlay` plays once as it
starts (d2d: from when the client first sees the state, `StateClock`).
Overlay.txt (record 0x84, `FUN_00470390` makes one):

| Column | +off | Use |
|---|---|---|
| Frames | 0x44 | frames of the DCC `data\global\overlays\<Filename>.dcc` |
| PreDraw | 0x48 | 1: drawn before (behind) the unit |
| Xoffset, Yoffset | 0x54, 0x58 | from the unit's feet |
| Height1..4 | 0x5c..0x68 | added to Yoffset by the unit's class (`FUN_006223a0`): players 1, monsters MonStats2 OverlayHeight − 1, others 0; −1 adds 75 |
| AnimRate | 0x6c | × 16: 1/256ths of a frame a tick |
| InitRadius, Radius, RGB | 0x70, 0x74, 0x7d.. | the overlay's light: starts at InitRadius, grows a subtile a frame to Radius (`FUN_004755a0`) |
| LoopWaitTime | 0x78 | a pause in milliseconds between loops, only for overlay kind 6 |
| Trans | 0x7c | the draw mode, as a COF layer's draw effect (3 additive) |

`FUN_00470c30` advances the 8.8 frame counter by `AnimRate * 16`
per 40 ms tick, preserving fractional frames on a wrap. Kinds 3 and 8
loop without consulting LoopWaitTime; kind 4 ends on the first wrap
and may create successor overlays (`FUN_004d9920`). Kind 6 alone stops
advancing between loops until GetTickCount has reached its wait.

d2d uses `rules::overlay_frame` for drawing and one-shot light lifetime,
with whole ticks and a 64-bit intermediate so the multiplication cannot
wrap in a long session. `frame_light` already stamps overlay lights:
the old note saying Radius was not stamped was stale. The 288 cases in
`tests/overlay_cases.inc` run the original update for kinds 3, 4 and 8
overlays; `tools/emu/overlays.py` also checks kind 6's wait boundary and
the duplicate-clock guard. Captured from stock 1.14d, SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
REA dossier: `ev_9c7994bddc33d76bd8f3b6013765032d1dc9160ba35344f3e90da8a6fe8263cd`
(static trace; the emulator supplies execution evidence).

## Rotating overlays (kind 8)

A state's overlay maker (`FUN_004d9920`) makes
its overlay1 / overlay2 as kind 3, or as kind 8 tagged with the state
(+0x38) when the state is in the flag list at data tables +0xd0 (bit 1,
`FUN_0063a210`): States.txt `aura`, the loader's (`FUN_00618100`) bit 1
column, its name at 0x6e6cf8 (bit 2 `hide` 0x6e7c48, bit 4 `pgsv`
0x6e7c34). 32 rows have it: the Paladin's auras, the resistance auras,
Conviction, the warcries Shout, Battle Orders and Battle Command, the
Druid's spirits, Quickness. New overlays go at the list's head
(`FUN_00470390`, +0x2c). A unit shows one
state's kind-8 overlays at a time (+0x3c visible, +8 a frame count):

- A new one (`FUN_0046def0`) takes the visibility and count of another
  kind-8 overlay of the same state; else, with another state's showing,
  it starts hidden; else it shows from count 0.
- Each tick (`FUN_00470c30` case 8) an overlay whose state is gone ends;
  the shown one counts to 50 (0x32), then `FUN_0046df40` hides that
  state's and shows the next higher state id's (the lowest when none is
  higher). Removing a kind-8 overlay (`FUN_0046e040`) rotates too.
- The draw (`FUN_0046e1b0` / `FUN_0046e300`) skips a kind 8 without +0x3c.
- `FUN_0046e040` passes the removed node itself (EAX = ESI, 0x46e0d3), so
  it rotates from the removed one's state even when that was hidden.

The rules are `tools/emu/engine_overlay_frames.py`'s model, checked there
against game.exe. d2d: `rules::aura_overlay_made` / `aura_overlays_rotate` /
`aura_overlays_tick` (overlays.hpp, test_overlays), run by `dress` for a unit
with a `StateClock` (the player); other units draw every state's overlays
together. Ported from the model, not diffed against game.exe.

## Worn items

`itemtype` and `itemtrans`: while the state is on, worn items of that type
take that colour (`FUN_0062c100`'s first loop, over the state list at
+0x18c): Enchant's weapons red, Venom Claws' green. It doesn't set the
tint byte, so a save's tints leave it out.

## A shattered death

The States.txt loader (`FUN_00618100`) puts each flag's states in a list
at data tables +0xcc + 4 x bit: `hide` is bit 2 (+0xd4, asked by
`FUN_0063a320`), `shatter` bit 31 (+0x148, `FUN_0063a6f0`); both are set
on freeze (1), shatter (107) and uberminion (184). The client's monster
death (`FUN_004aff60`, unit command 8 from `LAB_004b053e`):

- any hide state: +0xc4 loses 2 (not selectable) and gains 0x20, +0xc8
  gains 0x40000, as Corpse Explosion's client hides its corpse
  (`LAB_004d923a`);
- any shatter state: `FUN_004afc10`, then the hit class's high nibble
  becomes 0xa (`LAB_004b05be`).

`FUN_004afc10` (ESI the unit) does nothing but for a monster: under
uberminion without freeze it makes Missiles.txt rows 0x1ab and 0x1aa,
else 0x10f + its MonStats2 Height (byte +0xb, the bin table's `Height`
at `0x6571f4`) and then icebreaksmoke 0x113, each through `FUN_004cd540`
with create flags 0x2020 and the unit as owner (+4, +8), +0x30 = 1.
Heights 1..3 are icebreaksmall / medium / large, whose TravelSound is
impact_shatter_1; Height 0 (townsfolk, critters) gives monglacialspike.
Flags 0x2020 in `FUN_004cd540`: no 1, so the start is the unit (+8); 0x20
with +0x1c / +0x20 zero, so the target is subtile (0, 0); no 4, so the
speed is Vel + VelLev x 1 / 8 (+0x9a, +0x9b), << 8, x 75 / 100. With a
speed, a target 100 or more subtiles from the start on either axis
returns 0, no missile (`0x4cd7d8`): monglacialspike (Vel 18) is made only
near (0, 0). The icebreak rows (no Vel) stand on the corpse for Range +
LevRange x 1 frames; 0x2000 sets the path direction to a draw below 64
from the new missile's seed (+0x20, `0x4cd9a5`). d2d:
`rules::shatter_row_made`.
Oracle `tools/emu/shatter_missiles.py`. d2d: `rules::shatter_rows`; in
`Fight::boss_events` a monster going into DT with shatter (107) or a
freeze still on is `corpse_used` (not drawn, not picked) and makes both
rows as visual-only missiles on its corpse.

## Not yet

- Kind 8's visible state group rotates after 50 update ticks
  (`FUN_0046def0`, `FUN_0046df40`, `FUN_00470c30`); d2d still draws the
  active states together. The timing cases leave the rotation flag off
  to isolate the frame counter.
- Kind 6 delayed overlays are not instantiated by the state renderer;
  the state's ordinary loops do not need that delay.
- The player's own chill ("cold") and poison show as on a monster
  (View chilled / poisoned, `player_states`). The chill's −50 on
  other_animrate (stat 69) isn't applied.

## What a death keeps

`FUN_00639fb0` (stdcall, unit and a boss flag) is run by the player's death
mode (`FUN_0057f330`, from `FUN_00580ec0` and `FUN_0057fca0`: life stat 6 to 0,
`FUN_00627540`, this, `FUN_00649f70`) and by a monster's death start
(`FUN_005a6520`: with `FUN_0063e9f0`'s boss test, a MonStats boss). A unit's stat
block (`+0x5c`, flag `0x80000000`) holds two state bitmaps, (states + 31) / 32
dwords each: the first at `+0x58` (`FUN_00625bb0`, the one `FUN_00625b70`
tests), the second behind it (`FUN_00625be0`, `FUN_00639e30` / `FUN_00639e90`).
Per dword: second |= ~keep & first, then first &= keep, with keep the States.txt
mask data tables `+0x108` (`bossstaydeath`) for a nonzero boss flag, else `+0x104`
(`monstaydeath`) for a unit whose type is not 0, else `+0x100` (`plrstaydeath`);
then `FUN_0064c040(unit)`. Null unit, no stat block, no flag or a null bitmap:
nothing. The three column names are the loader `FUN_00618100`'s strings; which
bit of the data tables each is built from was not traced (names.tsv, the use in
`FUN_0063a4a0`: `+0x100`, `+0x104` for a unit of type 1). The player's call has the
boss flag 0 at `0x57f330`'s `FUN_00639fb0(unit, 0)`.

d2d: `rules::states_leave_on_death` (state_death.hpp), the States.txt columns in
`GameData::StateInfo`, and `Fight::die` runs the player's skill states
(`self_states`) through it, so a state not marked plrstaydeath ends with the death
(`tests/game/test_death_states.cpp`, game data). Not done: potion states, curses,
charges and the merc's states (separate containers in d2d), and the monsters'
side (`monstaydeath`, `bossstaydeath`: d2d's monsters keep no state bitmap), so
`FUN_005a6520`'s call is not ported. Oracle: `tools/emu/state_death.py`
(`--mutation-check`), against `tests/test_state_death.cpp`. `FUN_00627540`, which
then drops the stat lists of the states gone, is a separate claim
(frontier-ring.md).
