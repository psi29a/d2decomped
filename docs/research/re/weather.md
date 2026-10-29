# Weather (rain) — game.exe 1.14d

The client's weather, 0x472320..0x473fc3. d2d: `components/rules/weather.hpp`
(`Rain`, test_weather), `Town::update` (the tick and sound), `draw_rain`
(ingame.hpp) and the splashes in `render_world`.

## Where and when

- Levels.txt `Rain` (Act 1: the camp, the wilderness, the graveyard,
  Tristram); `Mud` for bubbles. Act 5 snows instead (levels 109..112, 117,
  120, 121; not built).
- **The cycle** (`FUN_00473e50`, state 0x7a8a24): 0 clear, 1 rising,
  2 full, 3 falling. On entering a state it lasts base + rand(spread)
  ticks (bases 0x7a8970 {7500, 250, 3000, 125}, spreads 0x7a8958 {7500,
  250, 3000, 50}; full rain's base is tripled in Acts 3 and 5). The state
  is a process global, so the first game after launch starts it rising
  on its first tick in a rain level. It only moves while the player is in
  one.
- **Entering a state** (`FUN_00473d00`): 0 no drops; 1 the wind
  (`FUN_00472610`: turn timer 0x7d + r%0x177, lightning delay, angle
  0x5c + r%0x47) and the full density 32 + r(224); 2 the full density;
  3 nothing (lightning off).
- **Density** rises linearly over state 1 and falls over state 3.
  Volume = density / 256 (0x7a89a0).

## Drops (screen space)

- **Spawn** (`FUN_00473090`): x = r(width); lands at y 40 + r(height − 87);
  starts at −20 + r(lands + 20); f = (lands − 40) / (height − 87); length
  4 + ⌊8f⌋, speed 15 + ⌊15f⌋ (nearer drops longer and faster); a colour
  from 12 by the phase type when it fell (`FUN_00472890`): day (98 − c,
  123 − c, 98 − c) drawn half see-through, dusk/dawn (45 − c', 55 − c',
  45 − c'), night (25 − 2i, 30 − 2i, 25 − 2i).
- **Each tick** (`FUN_004732c0`): v = ⌊speed × (0.85 + 0.15 × volume)⌋;
  it moves (cos, sin)(wind) × v (the 512-step sine table at 0x707800) less
  the view's scroll, wraps across the width, and lands past its y. A
  landed drop stays 3 ticks as a dot, then goes; a new one replaces it
  while there are fewer than the density. Missing drops are made up each
  tick (`FUN_004737b0`).
- **The wind** eases 2 a tick toward its target; every 125..499 ticks a
  new target 92..162 (straight down is 128).
- **Drawn** (`FUN_00473470`) over the world, before the UI: a line
  (length × (cos, sin) wind), cut where it lands; not over the bottom 47
  pixels.

## Splashes

Drawing a floor tile whose DT1 material flags have 2 (water: River.dt1)
rolls rand(1000) < density for a splash (`FUN_004de410` → `FUN_00472da0`):
Rain3 or Rain4 (UncompOverlays), a frame every 2 ticks, drawn additive after
the floors. Its x runs from the tile's top corner rightward by the row's
width (bugs.md #10).

## Sound

`FUN_004e42e0`: while it rains, Sounds.txt 64 `scene_rain` (rain2.wav)
loops at volume × 255, easing 6 a sound tick.

## Snow (Act 5)

- **Snow or not** (`FUN_004726f0`, on rising and with a flag from the join
  and every S→C 0x15 reassign, which carries 1 in all logged packets but
  two): in Act 5, rand(100) on the player's seed at least the level's bar
  (25 in 109..112, else 0) snows (0x7a8a14), the flag always; 117 / 120 /
  121 make flakes a size bigger (0x7a8a18); 120 / 121 hold the cycle full
  (0x7a8a1c: `FUN_00473d00` refuses any other state, though the refused
  fall's density is taken once). Elsewhere it rains. Rain ↔ snow clears
  the drops; snow ends the lightning.
- **Full** (`FUN_00472400`) by level: 109..112 density 32 + rand(56), wind
  42 + rand(170); 117 32 + rand(112), wind 170 + rand(56); 120 / 121
  density 256, wind 28 + rand(28) and now 28. Full's base is tripled for a
  game joined in Act 3 or 5 (`FUN_00472890`).
- **Flakes** (`FUN_00473090`): one more seed draw, speed 8 + ⌊28f⌋, shape
  (lands − 40) × 7 / (height − 87) (+1 big), 1 tick landed. They move at
  max(|cos wind|, 0.25) of their speed and sway 2 sin(frame × 512 / 25 +
  phase); the wind eases 1 a tick and turns every 250..1124 ticks. Drawn
  as lines 0x6d6e78[shape] and [shape + 1] (16-byte stride), grey 120 +
  80 i / 12 at index shape × 12 / 8, halved at dusk / dawn, a quarter at
  night (`FUN_00472fb0`, the software modes). No rain sound
  (`FUN_00473c40`).
- **Lines** (`FUN_006c8c80`, GDI; DirectDraw's the same): a solid
  Bresenham in a palette index; the alpha byte (0x7f / 0xff and the
  snow's) is unused in the software modes. The colours are the palette's
  nearest (`FUN_004fb180`), made once at the join.

## Splash and bubble passes

`FUN_004de730` per floor pass: splashes when 3 client frames have passed
since the last such pass, at int(volume × 1000) in 1000; bubbles in a
Levels.txt Mud level when 25 have. Each drawn material-2 floor rolls
rand(1000) once for both: a bubble (`FUN_00472ec0`: bubble3, y 30 higher,
a frame every 2 ticks; ticked by `FUN_00472d10` in Mud levels) under 100.
Splashes and bubbles draw moved by ∓W/4 with a side panel open (bugs.md
#38).

## Lightning

A Thunder Storm strike (skill 57's client do 84, `FUN_004e3c50`) sets off
`FUN_00472c50` (ECX 0: the first strike silent) unless it's on. Each drawn
frame (`FUN_00473910`) with a flash due draws the view white (colour 0xff,
mode 5, at over 9 fps, instead of the rain); a strike's second flash is 3
ticks on, then the next strike 500 + rand(1500) ticks on with
event_thunder_1 (202) 25 + rand(50) sound ticks later at ±200 each way.
Full rain, falling and snow put it out.
