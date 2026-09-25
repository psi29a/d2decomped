# Rendering — looking better, the Glide way

Status: design notes, nothing built. Written 2026-09-25 against the code as
of commit ffc527c. The research into game.exe's render paths (the
DirectDraw, Direct3D, Glide and OpenGL back ends) is listed under "Later
research" in `docs/PLAN.md`.

## What D2's renderers did

game.exe carries several back ends (strings: `glide3x.dll`,
`grGlideInit`, "Error 23 … initializing Glide", "Error 24 … initializing
OpenGL", DirectDraw and Direct3D paths). The software one (DirectDraw)
draws 8-bit palettised pixels, doing blends and light through PL2 lookup
tables. The 3D ones (Glide above all) drew the same sprites as textured
quads and got, for free or cheaply:

- **Real transparency**: 25 / 50 / 75 % blends, additive (fire, spells,
  missiles), darken (shadows) done by the card instead of by table.
- **Smooth lighting**: a light level per tile corner, interpolated across
  each floor and wall, so light pools and fades instead of stepping; light
  from the player's radius, torches, fires, missiles and spells, coloured
  where the source is (Missiles.txt Red / Green / Blue, objects' lights).
- **Shadows**: units cast a darkened, skewed copy of their sprite (MonStats2
  `Shadow`), and tile shadow layers blend at 50 % instead of drawing
  opaque.
- **Tints**: frozen units blue, poisoned green, cursed and item colours,
  through colormaps.
- **Perspective mode**: the floor tilted into a slight 3D perspective
  (Glide / Direct3D only).
- **Smoother weather and effects**: rain, snow and spell overlays blended.

## Where d2d stands

- A CPU framebuffer, 800×600 RGBA; sprites are palette-indexed and
  converted to RGB at blit time (`blit_sprite`, `blit_dt1_tile`,
  `blit_dcc_frame`).
- PL2 (`components/palette`) parses only the additive and 50 % blend
  tables; used by the frontend fire, not in game.
- Shadow tiles draw opaque; no unit shadows; no light (the whole screen at
  full brightness); no tints; no blend modes on units or missiles; walls
  sort per cell.

## The key decision: a draw list

Everything above wants the same change: instead of blitting straight into
pixels, the world renderer produces a **draw list** — sprite, position,
depth, blend mode, light (per corner), tint — and a back end turns it into
pixels. Then:

- A **CPU back end** (today's code, extended) keeps the authentic 8-bit
  look through PL2 tables: blends, light levels and tints as D2's software
  renderer did them.
- A **GPU back end** (SDL3's GPU API, or OpenGL) does what Glide did:
  hardware blending, interpolated light, perspective, higher resolutions
  and widescreen, all from the same list.
- Depth sorting (units against walls per subtile, not per cell) moves into
  one place.

## Steps, in order

1. **Blend modes** on the existing CPU path: the rest of PL2
   (Transparency25/75, light levels, hue variations), and COF / animdata /
   Missiles.txt draw effects (missiles and spells additive, ghosts
   translucent).
2. **Shadows**: tile shadow layers at 50 %; unit shadows (MonStats2
   `Shadow`, players always) as a darkened, skewed copy.
3. **Light**: a light map per subtile from the player's light radius
   (stat), objects (objects.txt), missiles; levels dark outside it
   (Levels.txt, act palettes' light tables); day and night outdoors.
4. **Tints**: unit states (cold, poison) and item colormaps (the save's
   tints, already read).
5. **The draw list**, then a **GPU back end** offering the Glide look:
   smooth light, perspective, larger resolutions. Authentic 800×600 stays
   the default.

Each step is visible on its own and testable against screenshots of the
original (the headless `screenshot` verb already captures frames).
