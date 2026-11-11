# The video drivers — game.exe 1.14d

game.exe draws through one of four drivers, each a table of 54 function
pointers. The engine's drawing goes through thunks at
`0x4f5050..0x4f6980` that jump through the current table, `DAT_007c8cc0`.
This page is the pick, the window, the whole slot map, and which slots only
the 3D drivers fill (the "3D only" features). Re-check:

- `cd tools/emu && uv run python video_drivers.py` prints the mode table and
  all 54 slots of the four drivers; `... video_drivers.py gamma` runs the
  Gamma / Contrast functions below.
- `tools/ghidra/fn.py <address>` for a thunk or a driver function. 26 of
  GDI's 54 entries, 22 of DirectDraw's, 18 of Glide's and 21 of
  Direct3D's are only ever reached through the table, so Ghidra made no
  function for them: read those with `uv run python disasm.py <address>`
  (in `tools/emu`). Many are a bare `ret` or `xor eax, eax`.

The software drivers' own pages: [gdi-driver.md](gdi-driver.md) (the slots
it reads in depth), [lighting.md](lighting.md) "Drawing with it" (floors,
walls, the light table).

## The four drivers

`0x72da80` holds a table pointer per mode, 0 to 7:

| Mode | Driver | Table | Its code | Source files (asserts) |
|---|---|---|---|---|
| 1 | GDI, software | `0x74c4a8` | `0x6c7a60..0x6c9900` | `Src/gdiCore.cpp`, `Src/gdiSmack.cpp` |
| 3 | DirectDraw, software | `0x72f6d0` | `0x510020..0x5135f0` | `Src/ddCore.cpp`, `ddDebugFont.cpp`, `ddSmack.cpp` |
| 4 | Glide | `0x72f178` | `0x508f60..0x50e7f0` | `Src/glCore.cpp`, `glSprite.cpp`, `glPerspective.cpp`, `glSmack.cpp`, `glTextureCache.cpp`, `glSubtileCache.cpp`, `glFloorCache.cpp`, `glBlocks.cpp` |
| 6 | Direct3D | `0x74bf28` | `0x6b1bc0..0x6ba390` | `Src/d3dCore.cpp`, `d3dSprite.cpp`, `d3dSmack.cpp`, `d3dTextureCache.cpp`, `d3dSubtileCache.cpp`, `d3dFloorCache.cpp` |

- **0, 2, 5 and 7 are empty.** OpenGL (5) and RAVE (7) are only names: the
  debug overlay `FUN_00477980` labels modes 1 and 2 "Software", 5
  "OpenGL" and 7 "RAVE", and `FUN_004f5050` has "Error 24 ... OpenGL".
  The window class gets `CS_OWNDC` only for mode 5 (`FUN_004f52e0`), and
  the fullscreen window adds `WS_CLIPCHILDREN | WS_CLIPSIBLINGS` only for
  mode 5 (`FUN_004f5610`). A mode with no table ends in "Unsupported video
  mode - %d".
- **`gl*.cpp` is Glide, not OpenGL**: those files' asserts sit inside the
  Glide table's code range, beside `glide3x.dll` and "Opening Glide window
  failed!".

## Picking one (`FUN_00405c30`)

From the startup config (offsets as in [switches.md](switches.md)):

1. `+10` (`-3dfx`) set: mode 4.
2. Else `+8` (`-w`, `-window`, `-windowed`) set: mode 1.
3. Else `+13` (`-d3d`) set: mode 6.
4. Else mode 3.

`+11` (`-opengl`) and `+12` (`-rave`) are not read here. The windowed flag
is `+8` whatever the mode (cleared when `DAT_0074c704` is 2), so `-3dfx -w`
is a windowed Glide. `+14` (`-per`) turns perspective on
(`FUN_004f51e0(1)`) only when the mode is above 3.

`FUN_004fa520` hands the mode to `FUN_004f52e0` (instance, the window
procedure `FUN_004f9a80`, mode, windowed), which keeps the mode in
`DAT_007c8cb0` (read by `FUN_004f5140`), the windowed flag in
`DAT_007c8cb4`, and the table in `DAT_007c8cc0`; calls slot `+0x30` (its
registers not traced), then `+0x00` (0 is "Error 1"), clears perspective
below mode 4, then `+0x04`, trying `+0x00` and `+0x04` once more if that
fails. It doesn't try another mode.

**The lighting copy.** The floor and roof light path does not read
`DAT_007c8cb0`: it reads `DAT_00712ccc` (`FUN_00477730`), which starts at
3 and is set by the option setters at `0x70f488` (`FUN_0044d9f0`): window
1, 3dfx 4, opengl 5, d3d 6, rave 7 ([lighting.md](lighting.md) "The 3D
path"). The two disagree for `-opengl`, `-rave` and `-d3d -w`: the driver
is software (3 or 1) and the light path is the 3D one (4 or more).
[bugs.md](bugs.md) #106. What a software driver draws with that table was
not followed.

## The window and the resolution

- **Sizes** (`FUN_004f5570`, the resolution index in `DAT_007c8cb8`, read
  by `FUN_004f5160`): 0 is 640×480, 1 and 2 are 800×600, 3 is 1344×700,
  anything else is fatal. GDI's buffer (`FUN_006c7b00`) and its
  `FUN_006c82d0` know the same three widths. Start-up asks for index 2 with
  the expansion (`FUN_00408f20`) and 0 without (`FUN_004f8f10`, from
  `FUN_00405c30`). The constant calls (`FUN_004f90a0(1)` ×8,
  `FUN_004f9060(1)` ×3, `FUN_004f5bc0(1)` ×2) pass 1. Nothing found passes
  3: the variable calls, `FUN_004f5bc0(uVar3)` in `FUN_004f9cc0` and
  `FUN_004f9060(uVar2)` ×3, were not traced.
- **The window** (`FUN_004f5610`): one copy only ("Only one copy of
  Diablo II may run at a time."). Windowed: a caption, system menu and
  minimise / maximise boxes (`0xcb0000`), centred on the desktop.
  Fullscreen: a popup (`0x80080000`); under Glide it covers the whole
  screen, at least 800×600. A desktop narrower than 800 is switched to
  800×600 at 8 bits first (`ChangeDisplaySettingsA`).
- **GDI scales.** Its present (`+0x20`, `FUN_006c7e30`) `StretchBlt`s the
  8-bit buffer onto the whole client rectangle, whatever its size.
- **No live switch.** Slot `+0x2c` (`FUN_004f5a50`: windowed = !arg, then
  the slot) returns 0 in all four drivers.
- **Cutscenes.** Slot `+0x34` puts the driver in its cutscene mode;
  `FUN_004f9050` returns its answer and `FUN_00435230` plays the startup
  videos only when it is non-zero. DirectDraw's (`0x5114d0`) sets
  640×480 at 16, 24 or 32 bits ("Opening DirectDraw window for cutscenes
  failed!"), Glide's (`0x509fe0`) returns 1 unless paused, and GDI's
  (`0x6c7cc0`) is `xor eax, eax`: **under `-w` the startup videos are
  skipped.** Read from the code, not run.

## Gamma and Contrast

Video Options' two sliders, both kept in the registry under "Diablo II".

- **Start-up** (`FUN_004f8ea0`, after the window): Contrast from the
  registry (default 100, or 0 under Glide) goes to slot `+0x30` as option
  11; Gamma (default `0x9b`, 155) goes to `DAT_0072da54` and slot `+0x58`
  (`FUN_004f5230`).
- **The rows.** Gamma (`FUN_0047d230`): 21 steps over 55..255 (`0x37`..
  `0xff`), so 155 is position 10, the middle. Its enabled test (menu.md)
  is slot `+0x5c` (`FUN_004f5260`): GDI 0, DirectDraw 1, Glide 1,
  Direct3D `[0x6ce278] & [0x96f3a8]` (the device's gamma support, going
  by its "Supports gamma calibration" log; not traced). Contrast
  (`FUN_0047d2b0` / `FUN_0047d2f0`): 100 steps over 0..100, so 100 is
  position 99. The step counts are the menu table's (`0x71875c`, as
  `gamemenu.cpp` has them). The slider maths: position = (steps − 1) × (value − min +
  1) / (max − min), truncated (`FUN_0047cc90`); value = min + (max − min) /
  (steps − 1) × position, truncated (`FUN_0047cd00`). Contrast 100 comes
  back as 99, which is what the row saves.
- **The ramp** (`FUN_006be190`: ECX gamma, EDX the output, then a double
  contrast in 0..100, the entry size, 256 entries, 255): at contrast 100
  it is linear, entry i = min(255, i × gamma / 100). So **gamma 155 shows
  every colour at 1.55 times** (128 → 198, and everything from 165 up is
  255); 100 leaves the palette alone. A lower contrast bends the ramp up
  (gamma 155, contrast 0: 128 → 163, 192 → 212). Run, `video_drivers.py
  gamma`, through game.exe's own code.
- **Who applies it.** DirectDraw: `+0x58` (`0x510300`) keeps the gamma and
  rebuilds the palette through the ramp (`FUN_00510160`, contrast from
  option 11), and its present (`FUN_00511870`) loads entries 1..255 into
  the DirectDraw palette. Glide and Direct3D set the card's gamma, by
  their imports and strings (`grLoadGammaTable`, `guGammaCorrectionRGB`,
  "Failed to set gamma ramp"); their slots were not followed.
  **GDI does nothing**: its `+0x58` is a bare `ret` and `+0x30` keeps no
  options. So the default fullscreen game (DirectDraw) is brighter than a
  `-w` one, which is the one d2d's live checks are taken against (`make
  run og` passes `OG_FLAGS ?= -w -ns`, Makefile; lighting.md "Checked
  live"). Read and run on the functions; not compared on screen.

## The slot map

The engine thunk is the function that calls the slot (`FUN_004f52e0` and
`FUN_004f5050` call `+0x00`, `+0x04`, `+0x08` themselves). "SW" is what
the two software drivers do; GDI's entries are copies of DirectDraw's
([gdi-driver.md](gdi-driver.md)) except where the row says. "3D only"
marks a slot that is empty in software and filled in Glide and Direct3D:
those are what the 3D modes add.

| Slot | Thunk | What | SW (DirectDraw entry) |
|---|---|---|---|
| `+0x00` | (`FUN_004f52e0`) | The first start-up call: 0 is "Error 1" | `0x510020`; GDI returns 1 |
| `+0x04` | (`FUN_004f52e0`) | Start: ECX to `0x7c9148`, EDX the game's callback table `DAT_007c914c` (lighting.md); GDI makes the critical section `DAT_007c9134` | `0x511ce0` |
| `+0x08` | `FUN_004f5050` | Shut down; then "Error 21..25" when start-up had failed | `0x511b80` |
| `+0x0c` | `FUN_004f5610` | Open the window's surfaces, after `CreateWindowExA` (GDI: an 8-bit DIB section of the resolution and a palette, `FUN_006c7c10`) | `0x5115e0` |
| `+0x10` | `FUN_004f5900` | Close them, before `DestroyWindow` | `0x511b30` |
| `+0x14` | `FUN_004f5fa0` | Activated / deactivated (GDI: sets `DAT_007c9144`, which makes `+0x18` refuse) | `0x511c20` |
| `+0x18` | `FUN_004f60f0` | Begin a frame (gdi-driver.md) | `0x511720` |
| `+0x1c` | `FUN_004f6190` | End it, after the frame pacing | `0x5117f0` |
| `+0x20` | `FUN_004f6190` | Present (GDI `StretchBlt`s to the client rectangle; DirectDraw also loads a changed palette) | `0x511870` |
| `+0x24` | `FUN_004f5aa0` | Change resolution (GDI makes a new DIB) | `0x511b60` |
| `+0x28` | `FUN_004f6250` | Copy the frame out as 24-bit RGB (GDI `FUN_006c7ef0`); callers `FUN_004fa0c0`, `FUN_004fa7a0` | `0x511970` |
| `+0x2c` | `FUN_004f5a50` | Switch windowed / fullscreen: 0 in every driver | `0x510380` |
| `+0x30` | `FUN_004f52b0` | Set option ECX to EDX: 11 is Contrast (Glide also answers 10, a query); GDI keeps none | `0x510390` |
| `+0x34` | `FUN_004f5d60` | Enter cutscene mode (GDI: 0, so no videos) | `0x511b10` |
| `+0x38` | `FUN_004f5d90` | The cutscene call video.md names; GDI `ret 4` | `0x511660` |
| `+0x3c` | `FUN_004f5dd0` | In cutscene mode? (GDI 0), read by `FUN_004f9f20` | `0x510140` |
| `+0x40` / `+0x44` / `+0x48` | `FUN_004f5e00` / `FUN_004f5e40` / `FUN_004f5e80` | A video's open / frame / close (gdi-driver.md) | `0x513400` / `0x5135f0` / `0x513330` |
| `+0x4c` | `FUN_004f5290` | A stats record for the debug overlays `FUN_00477760`, `FUN_00477c00`: 3D only (Glide and Direct3D return `0x7c90c8`, software 0) | `0x510370` |
| `+0x50` | `FUN_004f59b0` | The buffer's width and height, through ECX / EDX | `0x510150` |
| `+0x54` | `FUN_004f65c0`, `FUN_004f65f0` | Around the world draw `FUN_00476bc0`, with `DAT_0072db44` = `DAT_0072db40` at its start and 0x100 at its end. 3D only (what it carries not followed) | `0x5102f0` `ret` |
| `+0x58` | `FUN_004f5230` | Set gamma (above). GDI `ret` | `0x510300` |
| `+0x5c` | `FUN_004f5260` | Can it do gamma (above). GDI 0 | `0x510310` |
| `+0x60` | `FUN_004f6670` | Perspective: the view's centre, from `FUN_00476bc0` (the player's spot plus the screen shake). 3D only | `0x510330` `ret` |
| `+0x64` | `FUN_004f6760` | Perspective: project a point (10 callers, among them the unit draw `FUN_00471620`, overlays `FUN_0046e300`, the Alt labels `FUN_004c0810` and the 3D floor path). 3D only | `0x510340` `ret 0xc` |
| `+0x68` | `FUN_004f67c0` | Perspective: project, and set the size of the next sprites (Glide `DAT_00880b64`, 0x100 full size), from `FUN_004c9060`, `FUN_004c9990`, `FUN_004dc7b0`. 3D only | `0x510350` `ret 0x10` |
| `+0x6c` | `FUN_004f6830` | Perspective: size back to 0x100, from `FUN_004dc7b0`. 3D only | `0x510360` `ret` |
| `+0x70` | `FUN_004f63d0` | Set the palette (256 entries, kept at `0x7c9378`) | `0x510160` |
| `+0x74` | `FUN_004f6410` | Install the PL2 tables (0x48 pointers; tools/emu/cof_effects.py) | `0x5102d0` |
| `+0x78` | `FUN_004f6430` | The environment's RGB (`FUN_0061c0b0`), every frame from `FUN_0044c990`. 3D only | `0x510320` `ret 4` |
| `+0x7c` | `FUN_004f68e0` | Floor / roof tile (gdi-driver.md) | `0x5132c0` |
| `+0x80` | `FUN_004f6450` | A unit's cel, from `FUN_00470ec0` | `0x512410` |
| `+0x84` | `FUN_004f6480` | A cel in a draw mode | `0x511fb0` |
| `+0x88` | `FUN_004f64b0` | A cel through a colour table (the fonts) | `0x5120a0` |
| `+0x8c` | `FUN_004f64e0` | A cel through a draw mode's table, only when arg 5 isn't 0 (callers `FUN_00496f80`, `FUN_00497110`, `FUN_00501df0`) | `0x5121a0` |
| `+0x90` | `FUN_004f6540` | A unit's shadow (lighting.md "Unit shadows") | `0x5122e0` |
| `+0x94` | `FUN_004f6570` | A cel as one solid colour (below). 3D only | `0x5123a0` `ret 8` |
| `+0x98` | `FUN_004f6510` | A cel inside a clip rectangle (the automap) | `0x512250` |
| `+0x9c` / `+0xa0` | `FUN_004f6920` / `FUN_004f6950` | Wall tiles, block by block (dt1.md; the wall passes `FUN_004dedf0`, `FUN_004def80`, `FUN_004df1c0`) | `0x5131b0` / `0x5130a0` |
| `+0xa4` | `FUN_004f6980` | Shadow tile (lighting.md "Tile shadows") | `0x512f90` |
| `+0xa8` / `+0xac` | `FUN_004f6280` / `FUN_004f62a0` | A rectangle's outline, two forms | `0x512c20` / `0x512ce0` |
| `+0xb0` | `FUN_004f62c0` | A filled rectangle, one colour; the thunk has no caller ([stubs.md](stubs.md)) | `0x512d90` |
| `+0xb4` | `FUN_004f62e0` | A square dot round a point; no caller | `0x512e80` |
| `+0xb8` | `FUN_004f6300` | A rectangle in a draw mode (the dark strips, the lightning flash) | `0x512710` |
| `+0xbc` | `FUN_004f6340` | A rectangle with an alpha byte: 0xc0 and up solid, 0x80 mode 2, 0x40 mode 1, above 0 mode 0, 0 nothing | `0x5128b0` |
| `+0xc0` | `FUN_004f6380` | A line (the rain, weather.md) | `0x512930` |
| `+0xc4` | `FUN_004f63b0` | Clear the buffer (gdi-driver.md) | `0x512ed0` |
| `+0xc8` | `FUN_004f6890` | Debug text ("%d FPS", `FUN_00477980`) | `0x512680` |
| `+0xcc` | `FUN_004f68b0` | Glide only: copies two blocks (0x1000 and 0x4800 bytes) in, from `FUN_00475b20` when `DAT_007b9564` is set; Direct3D and software `ret 8`. Not followed | `0x5126f0` `ret 8` |
| `+0xd0` | `FUN_004f65a0` | One pixel, inside a 0x50 margin; no caller | `0x5123b0` |
| `+0xd4` | `FUN_004f5d50` | Flush the 3D floor, subtile and texture caches. 3D only | `0x510130` `ret` |

## What the 3D drivers add

The engine's side of each, so d2d can tell what is the driver's look and
what is game logic:

- **Perspective** (`+0x60..+0x6c`, the Perspective row, `-per`):
  `FUN_004f51d0` is "perspective on" (`DAT_0072da48` while `DAT_0072da4c`,
  which `FUN_004f5190` and start-up keep 0 below mode 4). Floors and roofs
  then go through the 3D vertex path (lighting.md), units, overlays and
  the rest through `FUN_004f6760`'s projection. Glide's projection
  (`0x50dd60`, `FUN_0050de10`) measures from the centre `+0x60` set, in
  16.16 fixed point, through a 0x2000-entry table at `DAT_00880b6c`.
  [bugs.md](bugs.md) #38 is about the view shift that comes with it.
- **The hit silhouette** (`+0x94`): a monster's get-hit (unit command 6)
  and block (0x12) in `FUN_004aff60` set the unit's `+0x58` to 3
  (`FUN_00464c90`). Each unit draw (`FUN_00471ec0`) counts it down
  (`FUN_00464c40`, not while state 0x8f, the riding imp) and first draws
  the unit through `FUN_00471bd0`: every COF layer as one solid colour,
  from `FUN_0046ed50` (green over 65 % life, yellow 33..65 %, red at 32 %
  and under, blue for MonStats `+0xd` bit 1; gfx.md). Glide draws it 1
  pixel left and 1 down (`FUN_0050b720`); Direct3D's (`0x6b7090`) was not
  read. Both software drivers' slot is `ret 8`, so it never shows there.
- **Coloured light** (`+0x78`, and the light's RGB): the software path
  reads the intensity only (lighting.md "Colour").
- **Gamma by the card** (`+0x58`): no palette rebuild.
- **Texture caches** (`+0xd4`, `+0x4c`): driver bookkeeping, nothing to
  port.

What d2d could take from these is in
[docs/design/rendering.md](../../design/rendering.md).
