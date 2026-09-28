# States: how they look

A state (States.txt) changes a unit's look three ways. d2d:
`Scene::StateInfo` (loaded in `load_composite_data`), `dress` and
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
| InitRadius, Radius, RGB | 0x70, 0x74, 0x7d.. | the overlay's light |
| LoopWaitTime | 0x78 | a pause between loops |
| Trans | 0x7c | the draw mode, as a COF layer's draw effect (3 additive) |

## Worn items

`itemtype` and `itemtrans`: while the state is on, worn items of that type
take that colour (`FUN_0062c100`'s first loop, over the state list at
+0x18c): Enchant's weapons red, Venom Claws' green. It doesn't set the
tint byte, so a save's tints leave it out.

## Not yet

- LoopWaitTime isn't applied.
- The player's own chill and poison aren't modelled by the fight, so
  they don't show.
- An overlay's light is stamped at Radius at once, not grown from
  InitRadius (`FUN_00474290`).
