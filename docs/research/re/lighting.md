# Lighting and time of day — game.exe 1.14d

d2d: `components/rules/light.hpp` (`Day`, `LightGrid`, test_light),
`frame_light` (town.hpp) and `Lighting` / `blit_dt1_tile_lit` (tile_blit.hpp).

## The day (ENVIRONMENT\Env.cpp)

The environment struct is 0x38 bytes. `FUN_0061be40` makes it, and
`FUN_0061aa60(game)` returns it.

| offset | meaning |
|---|---|
| +0 | phase 0..5 |
| +4 | the phase's type (0 day, 1 dusk, 2 night, 3 dawn) |
| +8 | time (frames) |
| +0xc | intensity 0..255 |
| +0x18..0x1a | ambient R, G, B |
| +0x1c / +0x24 | the sun's direction (−cos, sin; sin halved at night) |
| +0x28 | scale: frames per degree, 128 (0x7443e4; 4 in the eclipse) |
| +0x30 | eclipse |

- **Phases:** each is {start degree, type, RGB}, 12 bytes. Normal (0x7443f0):

  | phase | start | type | RGB |
  |---|---|---|---|
  | 0 | 320° | dawn | (125, 144, 243) |
  | 1 | 340° | dawn | (208, 184, 131) |
  | 2 | 0° | day | (255, 255, 255) |
  | 3 | 160° | dusk | (255, 255, 255) |
  | 4 | 180° | dusk | (194, 152, 193) |
  | 5 | 200° | night | (125, 144, 243) |

  The eclipse steps by 0x744480 (Act 4: past 0x744438's starts, snapping to 0x744480's).
- **A new game** starts in phase 2 at time 0, which is sunrise.
- **The eclipse** (the Tainted Sun, quests-act2.md): S→C 0x53 sets
  phase, time and +0x30; the step then reads 0x744480's phases, and the
  intensity falls by 8 a frame to 32. d2d: `Day::set`.
- **The step** (`FUN_0061bee0`, from the client frame `FUN_0044c790` via
  `FUN_0061bfc0`): time + 1 a frame. Night phases (type 2) add one more;
  Act 4 (out of the eclipse) adds 15, and Act 3 at night 8 more: 10 a frame (the +9 builds on
  the time + 1, over the night's extra one; tools/emu: an Act 3 day is
  29 702 frames, night 1537). d2d: `Day::step(act)`. The time wraps at 360 × scale. When
  the time passes the next phase's start × scale, the phase moves on and
  the time snaps to that start.
- **The day is 340° long** (bugs.md #9): phase 2 starts at 0°, so "0 <
  time" is true the first frame of phase 1, which moves straight on to
  sunrise. With each phase running a frame past its end, a day is 35 846
  frames, about 24 minutes.
- **Intensity** (`FUN_0061bb80`): angle a = time / scale degrees;
  intensity = (int)(sin(a) × 128 + 128 + 0.5), the sine halved for
  a ≥ 180°, clamped to 0 and to 255 (170 in Act 5). Sunrise 128, noon
  255 (256 clamped), midnight 64. Level 120 is fixed at 200. The eclipse
  drops by 8 down to 32.
- **Act 4's intensity** has no sun: `FUN_0061bb80` tests the act first, so
  the clock, the eclipse and level 120's rule are never read there. Each
  frame +0xc moves one step toward its level's target: 103 0x80, 104 0x40,
  105 0x38, 106 0x30, any other 16 (act + 13), so 107 and 108 settle at 16.
  It starts from whatever the last act left, so entering Act 4 is a fade of
  up to 239 frames. `tools/emu/env_light.py` runs it. The ambient most things
  use doesn't show this: all six levels have their own light (below), and
  only what reads +0xc itself does (raised roofs, "Floors and roofs").
- **Colour** (`FUN_0061bce0`): blended from the phase's RGB toward the
  next phase's by how far the time is between them. Level 120 is fixed at
  (245, 240, 255).
- **The level's own light:** `FUN_00619d70` gives the level's Levels.txt
  Intensity / Red / Green / Blue. When any of the colours is non-zero, its
  Intensity replaces the day's (`FUN_00474550` doesn't test the Intensity; no
  Levels.txt row sets one without a colour). Act 1's caves (8..): 0, (255, 255,
  255): dark but for the lights.
- **Ambient sound:** day while the phase index is 1..3 (`FUN_004e42e0`
  via `FUN_0061c220`), so 0° to 180°; else night.

### Environment accessors

`FUN_0061aa60(game)` returns the environment record at game `+4` and asserts
on a null game. Two small wrappers consume it:

- `FUN_0061c0b0(game, &r, &g, &b)` copies ambient RGB from environment
  `+0x18..+0x1a`. With no environment record it leaves all three destinations
  unchanged. `FUN_0044c990` forwards these bytes to the renderer's ambient
  color setter, and `FUN_004ddef0` uses them for ambient-only light tables.
- `FUN_0061c0e0(game)` returns environment `+0xc`, the current intensity, or 0
  with no environment record. `FUN_004ddef0` fills each ambient-only table
  entry with this intensity and the three bytes above.

The combined 63-case original-code check described in drlg.md "Room lifecycle
and LvlPrest accessors" covered both wrappers, including game forwarding,
unchanged RGB destinations and zero intensity without an environment record.
The environment lookup was hooked; no d2d parity claim is made.

## The light grid (client, 0x4744b0 .. 0x475b20)

- **The grid:** 48 × 48 subtiles at 0x7b0e68, 8 bytes each: {u32 blocks
  light, intensity, R, G, B}. `FUN_00475800` centres it on the player
  each frame (origin 0x7b0a54 / 0x7b0a58 = the player's subtile − 24),
  fills it with the ambient (`FUN_00474610` → `FUN_004744b0`), marks
  subtiles whose collision has 0x22 (`FUN_004756d0`), then updates each
  light in the list at 0x7b5668 (`FUN_004755a0`).
- **A light** (`FUN_00474160(unit, type, radius, intensity, r, g, b)`,
  0x44 bytes): type, unit id, position in eighths of a subtile (the
  unit's 16.16 position >> 13, + 4), radius × 8 (capped at 18 subtiles),
  the radius it eases to (8 a frame), intensity, RGB.
  - the player: radius 13 + the light radius bonus (`FUN_00460930`), 255,
    white;
  - monsters: MonStats2 Light / light-r/g/b (a unit's own light-radius
    stat if higher);
  - objects: half of objects.txt Lit<mode> (`FUN_004bc580`: Lit >> 1,
    type 2), Red / Green / Blue, 255. Read live in Lut Gholein: its
    torches (Lit 17 / 18) light radius 8 / 9;
  - missiles: Missiles.txt Light / Red / Green / Blue.
- **Stamping** (`FUN_004748d0`): over the light's square, each subtile
  corner takes (r − d) × (intensity << 16) / r >> 16, d the distance
  `FUN_004740d0` (0.96 · the longer side + 0.4 · the shorter, in
  1/1024ths). `FUN_004747c0` adds it in, capped at 255; with coloured light
  on, the RGB is the intensity-weighted mean. At quality 0 other players'
  lights are capped at radius 16, type 1 at 8, type 3 skipped.
- **Shadows** (quality 2, `FUN_00475780` by frame rate): the player's
  light (type 0) is stamped by `FUN_00474d70`, static ones (type 2) by
  `FUN_004750f0`. Both first fill a 64 × 64 table from the blocked
  subtiles, spread it outward ring by ring (`FUN_00474b50` /
  `FUN_00474c00`: each subtile takes a blend of the two nearer to the
  light), and scale the light by (8 − blocked / 2) / 8.
- **Light Quality** (`FUN_00475780`, each frame from `FUN_00475800`): it starts
  at 2. The Video Options row sets 0x72da50 on Low only (LAB_0047cfcf; Medium and
  High clear it, LAB_0047cfc1, LAB_0047cfb1), and with 0x72a348 clear, as Low leaves it, the answer is
  0 (LAB_004757a7). Then the frame rate, 0x7a04a8, the frames of the last 3 s over 3
  (`FUN_0044cce0`): 9 or less 0, 10 to 12 gives 1, 13 to 15 keeps a level of 1 or more
  (LAB_004757ba) or gives 1, more keeps the option's. A change waits 2000 ms from the
  last (LAB_004757da). Read from the code, no oracle. d2d has the row (`gamemenu.cpp`), which
  changes nothing here, and no drop by frame rate.
- **Sampling:** `FUN_00475aa0(x, y)` reads the entry at (x >> 3, y >> 3),
  clamped to 0..47.

### What fills it

`FUN_00474610` calls `FUN_00474550` with its last argument 1, so
`FUN_0046bdd0` answers first and any colour from it beats the level's light
and the day's:

- **Level 8:** the Den's flash (quests.md "The Den lights up").
- **Levels 107, 108: Diablo's release.** The server's `FUN_005b5230` sends
  S→C 0x89 event 12 (`FUN_00545760`) to the game when the sanctuary is
  cleared. Its client entry, 0x46b290 in the table at 0x7129d8, sets 0x7a7460 in
  any level. While it's set and 0x7129d0 is −1: intensity 160, (255, 64, 48), so River
  of Flame goes from 120 to 160 and the Chaos Sanctuary from 80 to 160. Event
  13 (0x46b3a0) sets the count 0x7129d0 to 0, which ends the red at once;
  `FUN_0046beb0` clears both 30 frames on. Diablo's death sends 13 only in a
  classic game (LAB_005b5369, game +0x70 zero), so in Lord of Destruction it holds
  until the next game (`FUN_0046bf90`).
- **A dip to black, in any level but 8, 107 and 108** (`level_ambient.py`: 107 and 108 keep their light through one): `FUN_0046bdd0`
  returns the bytes `FUN_0046ad10` keeps at 0x7a7430: the fill's own light and
  RGB, an intensity that falls on the cosine over the fade in, is 0 for the
  hold, and rises over the fade back. While it runs (0x7a742c), floors, walls
  and units follow; raised roofs don't. `FUN_0046ae50(in, hold, back, level,
  force)` starts it; a change of level ends it. Two callers: States.txt
  setfunc 2 (`FUN_004d87e0`, from the state's table at 0x72a690), which only
  `cloak_of_shadows` (state 153) has: in 5, hold 3/5 of the skill's auralen,
  back 2/5, so level 1's 200 frames are about 5 s black then 3 s back. And
  event 1 in level 4 (0x46aee0, sent by `FUN_00593710` as the Cairn Stones
  open): in 40, hold 220, back 40, from missile `cairnstones` (288).
- **The dip, frame by frame** (`FUN_0046ad10`, from `FUN_0046beb0` while
  0x7a742c is set): the fill with no event (`FUN_00474550` flag 0: the
  level's light, else the day's) times the cosine (the table at 0x707800)
  of the angle the frame before left, truncated; then the fade in's angle
  is frame × 128 / in, the hold's intensity 0, and the fade back's angle
  128 − (frame − (total − back)) × 128 / back. The hold keeps the fade in's
  last angle, so the fade back's first frame shows cos((in − 1) × 128 / in)
  of the fill (Cloak's at 68: 21) before 0 and the climb. Past the total,
  or a frame with the client in another level, its 0x2c bytes clear. A fade
  back of 0 is 25 (`FUN_0046ae50`), a fade in of 0 a fatal error; 0x7a7454,
  which `force` gets past, is never set. Cloak's length is its auralencalc
  (Skills.txt +0x60, `FUN_00646ca0` at the state's skill and level, stats
  0x15e / 0x15f); the Cairn Stones' frames are `cairnstones`' Param1 (+0x38)
  in and back, its Range (+0x96) less twice that held.
  `tools/emu/ambient_dip.py` runs `FUN_0046ae50`, `FUN_0046ad10` and
  `FUN_00474550` for real: 40,300 dips as `rules::AmbientDip`'s (the
  fixture's 300, `gen 300 1`, and 20,000 each for seeds 7 and 11;
  `test_ambient_dip`).

`tools/emu/level_ambient.py` runs events 12 and 13 and Cloak's dip with Levels.txt's
values; the Den's flash and the Cairn Stones' event 1 aren't run. d2d fills
from the level's light (`frame_light`), ahead of it the Den's case and
Diablo's release (`Town::event_ambient`, `rules::release_light`: the View's
`diablo_red`, or a joined game's 0x89 12 / 13); its colour is dropped, as
every light's. The dip (`rules::AmbientDip`, `Town::dip_tick` each client
frame) starts from Cloak of Shadows' state on the player or a Shadow
(`Fight::cloaks`, `World::cloak_dips`: every player gets it, as each client
sees the state set), and from the Cairn Stones opening (`World::cain_operate`,
`rules::cairn_dip` from Missiles.txt's row: every player in Stony Field gets
it, where game.exe's client needs a StoneAlpha, object 17, in its near rooms,
`FUN_0065a620`; `test_cairn_dip`). A joined game's state 153 (0xa8 / 0xaa)
and event 1 don't start one.

The fill runs over the whole grid for the player's room, then for each other
room in its near list (`FUN_00619790`, `FUN_00619730`) over the cells under
that room, with that room's level. So across an edge each level keeps its own
ambient, where d2d's single fill gives the player's level's (todo.md #45). In
Act 4, whose list from `FUN_006194a0` holds five of its six levels (not 107,
River of Flame), three pairs touch and differ: 103 / 104 (140, 120), 104 / 105
(120, 80), 105 / 106 (80, 40). River of Flame (120) between 106 (40) and 108
(80) likely makes two more.
`level_ambient.py borders` lists these; the other acts' lists hold only some of
their levels, so they aren't counted. Read from the code, the fill by room
not run; a border's cells only.

### Who reads the intensity

+0xc goes out through `FUN_0061c0e0` alone (`FUN_0061bb80` is its writer). Its
callers: `FUN_00474550` (the grid's fill, which a level's light overrides),
`FUN_004ddef0` (raised roofs, software), `FUN_004de070` and `FUN_004de260`
(3D modes), `FUN_004ded20` (bytes nothing reads). `FUN_0044c990` takes only
the RGB. Units, objects and their shadows don't read it: `FUN_004df1c0`
(LAB_004df343, LAB_004df416) and `FUN_004df510` (LAB_004df572) hand
`FUN_004dc7b0` the grid's entry from `FUN_00475aa0`. d2d's `Lighting::ambient`
was read in one place besides the grid's fill, raised roofs (world_view.cpp);
they now take `Lighting::environment` (+0xc, `Day::environment`), so in Acts
1 to 4 no draw differs from +0xc (level 103 was todo.md #42, #218). Read from
the code; `test_roof_light` checks d2d's side. Act 5's cap of 170 and level
120's 200 (`env_light.py`) aren't in d2d's `Day::sun`, which fills the grid
there; Act 5 is outside todo.md.

## Drawing with it (DirectDraw, video mode 3; GDI, mode 1, the same)

- **Video modes** (`DAT_007c8cb0`; drivers at 0x72da80 by mode): 1
  software GDI (0x74c4a8, what `-w` runs: read live under Wine), 3
  DirectDraw (0x72f6d0), 4 Glide (0x72f178), 6 Direct3D (0x74bf28); 0,
  2, 5 and 7 are empty (all 54 slots: video-drivers.md). GDI's tile entries (+0x9c 0x6c94b0, +0xa4 0x6c9290)
  are copies of DirectDraw's and draw through the same callbacks
  (`DAT_007c914c`), so what follows holds for both.
- **Floors and roofs** (`FUN_004de410`, `FUN_004dea70`): in the software
  modes (`DAT_00712ccc` < 4; 1 under `-w`) an 8 × 8 table
  (`FUN_004ddef0`), entry (col, row) the grid at subtile (x − 1 + col,
  y − 1 + row) from the tile's cell. A roof with a roof height hands it
  in as the third argument and the table is all the environment's light:
  raised roofs take the ambient alone, no lamps. That is +0xc itself
  (`FUN_0061c0e0`), not `FUN_00474550`, so a level's own light doesn't reach
  them. Of the levels with their own light, only Pandemonium Fortress (103)
  places any: 57, all `Exterior.dt1`, at 128 settled where its level light is
  140. d2d lights them from `Lighting::environment` (#218). A floor whose DT1
  material has 0x100 gets 0xff throughout. The block callback
  (`FUN_004f8b80`) takes block (bx, by) (its grid bytes, +6 / +7) with
  e = table + bx + 8·by: A = e[9], B = e[10], C = e[17], D = e[18]; flat
  at A >> 3 when |B − A| + |C − A| + |D − B| < 10, else four corners,
  each the mean of four entries: v0 = (C + e[16] + A + e[8]) / 4, v1 =
  (e[1] + e[2] + B + A) / 4, v2 = (e[19] + C + e[11] + B) / 4 (C, not D:
  game.exe's own pick), v3 = (e[25] + e[26] + D + C) / 4. They go to the
  block's 32 × 16 rectangle top-left, top-right, bottom-right,
  bottom-left (`FUN_004f8850`): each of its 15 rows runs L from v0 toward
  v3 and R from v1 toward v2 ((v · 16 + Δ · row) >> 7), then across
  through the ramp table 0x7ca320 (read live: floor(L + (R − L) · u /
  32)). Iso blocks go through `FUN_004f6bb0`, RLE ones `FUN_004f71a0`,
  the same way. A floor tile's diamond is at its buffer's top (block
  (0, 0) at x 64, y 0).
- **The 3D path** (`FUN_004de070`, `FUN_004de260`, and for a 0x100 material
  `FUN_004ddfd0`, `FUN_004de190`): floors (LAB_004de507) and roofs
  (LAB_004dec42) take these only when `DAT_00712ccc` is 4 or more. It starts
  at 3 (.data); `FUN_0044d9f0` runs the option setters at 0x70f488: window 1,
  3dfx 4, opengl 5, d3d 6, rave 7. So neither software mode gets here. They
  fill 6 × 6 vertices of 12 bytes, the raised-roof rule the same: a non-zero
  height gives each +0xc and the environment's RGB. `FUN_004de260` adds
  `FUN_004f6760`, which asserts driver 4 or more and perspective on. d2d copies
  the software path, so it isn't affected. The driver has its own test
  (`FUN_00405c30`: 3dfx 4, window 1, d3d 6, else 3), so opengl, rave or d3d
  with window send this table to a software driver. A default setup doesn't: the config starts
  zeroed, D2.ini's VIDEO keys default to 0 (switches.md), and the registry's `Render` is
  only written (`FUN_006b2f60`, its one reference LAB_006b2fe6, RegSetValueExA). It
  takes `-opengl`, `-rave` or `-d3d` with `-w`, by switch or D2.ini key. What the driver does
  with the table wasn't followed. Read from the code, no oracle.
- **Walls** (`FUN_004df1c0`) and **lower walls** (`FUN_004dedf0`, from
  `FUN_004df480`) take 0x6db9d8[direction] lights (6 for DT1 directions 1..9) at spots
  from the tile's cell origin (its subtile, not where it's drawn), by
  direction from 0x72a9e8 (0x72abc8 while the wall is in front, tile
  +0x24 bit 0), 6 {x, y} subtile pairs each, filled at start-up:

  | direction | 0x72a9e8 | 0x72abc8 (in front) |
  |---|---|---|
  | 1 | (2, 5) (2, 3) (2, 1) (2, −1) (2, −3) (2, −5) | x −2 |
  | 2 | (−5, 2) (−3, 2) (−1, 2) (1, 2) (3, 2) (5, 2) | y −2 |
  | 3 | (2, 5) (2, 3) (2, 2) (2, 2) (3, 2) (5, 2) | each 2 → −2 |
  | 4 | (−5, 2) (−3, 2) (−1, 2) (2, −1) (2, −3) (2, −5) | each 2 → −2 |
  | 0, 5.. | (0, 0) × 6 | (0, 0) × 6 |

  The driver (+0x9c) gives a 32-pixel block at x the lights [x >> 5] and
  [(x >> 5) + 1] as its left and right corners, top and bottom alike.
  The driver (0x72f6d0 +0x9c / +0xa0: 0x5131b0 / 0x5130a0) draws each
  32-pixel block with its corner lights through the game's callbacks
  (0x72da60).
- **A wall block** (`FUN_004f8120`): flat at the left light >> 3 when
  its corners differ by under 10 in all (`FUN_004f8050`; level 31
  unlit, `FUN_004f7ea0`), else the ramp from left >> 3 to right >> 3
  across its 32 columns (`FUN_004f71a0`), each pixel through the light
  table.
- **Order:** lower walls, floors, rain splashes, the shadow pass (tile
  and unit shadows), walls with the units among them (`FUN_004df1c0`),
  then roofs over all of it (`FUN_004dea70`).
- Checked live (Lut Gholein, `--toggle light_grid=off`, the same spot
  and time): walls and the raised roofs pixel-exact, floors 98.6 %
  (sand), the frame 94 % (the rest moving units and the palm shadow's
  RGB blend, deviations.md approximation 1).
- **The light table** (0x7d2348): 32 levels × 256, the act's PL2 +0x400.
  Level 31 is the identity, 0 black.
- **Colour** isn't drawn here: the software path reads the intensity
  only. The RGB is for Glide / Direct3D (coloured light is on by default,
  0x712b8c, a debug key toggles it).

### The bytes nothing reads

`FUN_004ded20` (from the scene's draw, LAB_00476c62) writes the environment's
RGB and +0xc to 0x7c8a6f, 0x7c8a6d, 0x7c8a6c and 0x7c8a6e, then the level's own
light over them when it has a colour. Nothing reads them: searching game.exe's
file for those four addresses as immediates finds only that function's
own writes, and the same search for 0x7c8a70 (read at LAB_004dfb10) finds both
its uses, so the search works. Nothing addresses them from below: 0x7c8a60..0x7c8a6b
have no reference, and 0x7c8a28, the floor pass's struct (`FUN_004ded10`),
is read only to +0x40 (`FUN_004de410`, `FUN_004de730`, `FUN_004dde80`), which
leaves +0x44, the first of them, untouched. d2d has no equivalent and needs none. Read
from the code, no oracle.

## Shadows

- **Options:** Video Options "Blended Shadows" (registry, default on;
  `FUN_004f5200` sets 0x72da5c) and "Light Quality" (the grid's shadows).
- **The alpha tables:** PL2 +0x3500, three 256 × 256 tables (D2WinPalette
  copies them to 0x7ee468.. and hands the driver pointers in that order,
  `FUN_004fb010`). Table k mixes T[a][b] = a · (1 − w) + b · w with
  w = 0xbf, 0x7f, 0x3f / 255 (`FUN_00605880`), nearest palette colour.
- **Tile shadows** (orientation 13): driver +0xa4 (0x512f90) draws each
  block through callback `FUN_004f82d0` at alpha 0xc0 → table 0 as
  T[ground][tile] (the screen byte picks the row): a quarter of the ground
  under three quarters of the tile, unlit. With Blended Shadows off,
  opaque. They go in the shadow pass `FUN_004df510`, with the units'
  shadows, after every floor (`FUN_004ded10`) and the rain's splashes: a
  shadow reaching over the next cells isn't painted over by their floors.
- **Unit shadows:** driver +0x90 (0x5122e0 → `FUN_00601730` →
  `FUN_00608d60`) draws the frame from its bottom row up, every other row,
  each output row one up and one left: half as tall, leaning up-left from
  (x + xoff + yoff / 2, y + yoff / 2). Each pixel becomes T0[ground][0]:
  a quarter of the ground (black when not blended). MonStats2 Shadow says
  which monsters cast one (all of them in 1.14d). Objects cast one too:
  `FUN_004dc710` marks every object drawn (unit +0xc8 bit 0x80), and
  `FUN_004dd7c0` lists each such unit for the shadow pass (`FUN_004dd6e0`
  → `FUN_00471620`); the COF's layers say which parts do (Lut Gholein's
  braziers, read live). An object is drawn, and its shadow leans, from
  its position: half a subtile before d2d's object cell, which holds the
  light's spot (measured live: the braziers and the waypoint's flames).

## d2d

- The grid is sized to the view plus the widest light, not 48 (bugs.md
  #11, deviations.md #1).
- `Day` steps in the World a tick at a time and goes to the client in the
  View; `frame_light` fills a `LightGrid` round the player with the
  level's light or the day's, and stamps the player (13), objects (Lit by
  mode), monsters and missiles.
- `render_world` draws floors and flat roofs through `blit_dt1_tile_lit`
  (the 8 × 8 table and block callback above; a pixel's block from where
  it lies under the diamond's top, tall floors' pixels past the 5 × 5 in
  the nearest block), raised roofs at the environment's intensity
  (`Day::environment`, +0xc), last; walls through
  `blit_dt1_wall_lit` (the six lights, the wall block's flat / ramp). Units take the level at their
  feet; additive missiles and overlays aren't lit.
  `test_roof_light` checks the real renderer with synthetic tiles and
  distinct environment, level and local-grid palettes: raised roofs use
  the environment, while flat roofs keep the grid's light.

- Tile shadows keep a quarter of the ground (`blit_dt1_shadow`), drawn
  after every floor; players', NPCs'
  and monsters' composites cast shadows after the floor pass
  (`shadow_composite`), each pixel once however many layers cover it.

- Walls shadow the player's and objects' lights (`stamp_shadowed`, as
  `FUN_00474d70`: the rings, the blend toward the light, 16 behind a
  blocked subtile); monsters' and missiles' lights (type 1) aren't.
  Light Quality is taken as high.
- Light types from their callers of `FUN_00474160`: the player
  (0x460cf0) and a monster's light-radius stat (0x4ae2ee,
  `FUN_004ae210`: a hireling with a light-radius item) type 0; missiles
  (0x470555) and MonStats2 lights (0x4c56d7, 0x4cdac5) type 1; objects
  (0x4bc5bc, `FUN_004bc580`) and 0x4d6f85 type 2 (cached shadows,
  `FUN_004750f0`). `FUN_004d6d40`'s 0x4d6f85 wasn't followed; 0x4accef, 0x4acd2c (type 0) and
  0x4f3630 (type 1) are three more callers, not followed.
- Type 2 is shaded at any quality (`FUN_004755a0`): `FUN_004750f0` fills its table once
  (+0x2c, LAB_0047540b), `FUN_00475420` stamps from it, and a new radius frees it
  (LAB_004755ff). Type 0 is shaded only above quality 1 (LAB_00475661); otherwise it's a plain
  `FUN_004748d0`. d2d shades objects' lights by reading the map each frame (`stamp_shadowed`),
  so at quality 2 only the cache differs: a blocked subtile that changes after the first stamp.
  How many lights have a door or the like in reach wasn't measured. Read from the code, no oracle.
- Checked live (Lut Gholein, game.exe under Wine, light grid at
  0x7b0e68 vs `debug light` with `--toggle light_grid=off`, the same
  spot and time of day): all 48 × 48 entries and the blocked subtiles
  equal, with objects at half their Lit and units' lights + 4 eighths.

- The player's radius is 13 + item_lightradius (stat 89) summed over what's
  worn, charms and sockets (the View's `light_bonus`), capped at 18.

- The Den of Evil's light once cleared: quests.md "The Den lights up".

ponytail, not yet: type 2's cached shadows (deviations.md approximation 5).

## The unit under the cursor

`FUN_00471ec0` (the unit draw): when the unit is the one under the cursor
(`FUN_00467a10`), its light is doubled and held to 0x40..0xff before it's
drawn, so a hovered monster stands out in the dark. Objects then also
draw in mode 7 instead of 5 (3 / 4 by their draw type); mode 7's table
isn't traced. d2d: `Unit::highlight`, last frame's hovered unit.

Found on the way: `FUN_00476ea0` loads `Data\Global\Monsters\RandTransforms.dat`
(0x1e00: 30 colour tables for monsters) and `GreenBlood.dat` (256, the
censored blood, when a unit's +0x1a2 is set and a setting file says '1').
