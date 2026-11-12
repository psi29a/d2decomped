# States: how they look

A state (States.txt) changes a unit's look three ways. d2d:
`GameData::StateInfo` (loaded in `load_tables`, gamedata_load.cpp), `dress` and
`monster_states` / `player_states` (town.hpp), drawn in `render_world`.

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
