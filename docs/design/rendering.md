# Rendering — what d2d draws, and what game.exe's 3D drivers could add

Status: design notes. First written 2026-09-25; rewritten 2026-11-11 once
game.exe's drivers were traced. What game.exe does is in
[video-drivers.md](../research/re/video-drivers.md) (the four drivers and
their 54 slots) and [lighting.md](../research/re/lighting.md) (the software
light); this page is only what d2d could do with it.

## What game.exe has

Four drivers: GDI (`-w`) and DirectDraw (the default) draw 8-bit pixels
through PL2 tables; Glide (`-3dfx`) and Direct3D (`-d3d`) draw the same
cels as textures. OpenGL and RAVE are names with no driver. The engine
above the drivers is the same for all four, except a handful of slots the
software drivers leave empty: those are the whole of "the 3D look".

## Where d2d stands

d2d copies the software drivers, onto an RGBA framebuffer:

- Floors, walls and roofs lit from the light grid (per pixel, a little
  smoother than game.exe's 32-pixel blocks: deviations.md approximation 2),
  floors drawn in bands on up to four threads (`draw_in_bands`).
- Tile shadows at a quarter, unit shadows, the COF draw modes (all but mode
  6), state colour shifts and item colours. Blends are done in RGB, not
  through the PL2 tables' nearest colour (approximation 1).
- Additions game.exe never had: widescreen (`--widescreen`, the world wider
  than 800, the panels at the centre), borderless fullscreen, the window's
  scale filter (`scalemode`), cinematics in a window (game.exe's GDI driver
  has no cutscene mode, so `-w` skips them).
- Not there: Gamma and Contrast (the rows move, nothing applies them),
  Light Quality (always high), the 640×480 resolution, and everything 3D
  below.

## Features we want, eventually

Wanted, not scheduled (2026-11-11; PLAN.md "Later research"). Ranked by
what a player sees for the work. All but the last fit the CPU path d2d
has now.

1. **Gamma and Contrast that work.** game.exe's DirectDraw driver runs the
   palette through one ramp (`FUN_006be190`; at contrast 100, entry i =
   min(255, i × gamma / 100)), and its default gamma is 155: a default
   install shows every colour at 1.55 times, the top third clipped. d2d
   converts palette to RGB at each blit, so this is one 256-entry table
   rebuilt when a slider moves. Start the rows where game.exe does
   (Gamma at 10 of 0..20, Contrast at 99), save them in d2d.cfg, and freeze
   the oracle's ramps (`tools/emu/video_drivers.py gamma`) as a test.
   - **Open, decided when it's picked up:** the default. 155 is the default fullscreen look
     (DirectDraw); 100 is `-w`'s, which is what every live check so far
     compared against (`make run og` runs `-w`). Suggested: 155 as
     game.exe ships, the checks pinned to 100. First take one live
     DirectDraw screenshot to confirm the brightening on screen.
   - Cost: half a day with the test.
2. **The hit silhouette** (Glide / Direct3D only). A monster that's hit
   or blocks gets its sprite drawn in its life colour (green, yellow, red,
   or blue for some) for 3 draws, offset a pixel, behind it
   (video-drivers.md "What the 3D drivers add"). Small: a counter on the
   unit, set by the get-hit and block commands, and one solid-colour blit
   of the COF layers. It's a 3D-mode feature, so a toggle,
   `--toggle hit_flash=on|off`, with deviations.md naming the driver it
   comes from. Direct3D's offset is still to read.
   - Cost: half a day, plus a look at Glide under a wrapper if one runs.
3. **Coloured light** (Glide / Direct3D only). The light records already
   carry RGB (lighting.md: the environment's colour, missiles' and
   objects' lights); the software path uses the intensity alone. d2d's
   RGB framebuffer can tint by it. How the 3D drivers mix the colour per
   vertex wasn't followed, so until it is, this would be d2d's own
   approximation, behind a toggle.
   - Cost: a day or two, more to trace the drivers' mix.
4. **Perspective** (Glide / Direct3D only, the Perspective row). The floor
   tilts and sprites move (and some shrink) with depth. The projection
   itself is short and traced to its functions (Glide `0x50dd60`,
   `0x50de10`), but drawing tilted floors and walls needs a GPU back end,
   or a CPU warp of every tile. This is the one item that needs the draw
   list below.
   - Cost: weeks; only after the draw list.

Not worth doing: OpenGL and RAVE (never built), a live windowed switch
(no game.exe driver has one; d2d has Alt+Enter), the 1344×700 size
(nothing found selects it; widescreen covers it), the texture caches.

## The draw list (for perspective)

Instead of blitting straight into pixels, the world renderer would produce
a list (cel, position, depth, draw mode, corner light, tint) and a back end
would turn it into pixels: the CPU one as now, a GPU one (SDL3's GPU API)
for perspective, larger scales and smooth light. Depth sorting would move
into one place. Nothing above needs it except perspective, so it waits
until perspective is wanted.

## Widescreen (done, opt-in)

The framebuffer is always 600 high and its width follows the window's
aspect (800–1400). The world draws on the wide buffer around its centre;
the HUD and panels run unchanged on a copy of the centre 800×600 and are
pasted back, and mouse x counts from that centre, so the UI's hit tests
are untouched. Not yet: panels anchored to the screen's edges, a wider
monster bar and chat, an edge-aware minimap.
