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
