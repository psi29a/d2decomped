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
not followed. `FUN_00477730` is that six-byte read: `ui_client_helpers.py`
returns nine synthetic values of the global unchanged (including
`0xffffffff`); option setup and d2d are not covered.

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
  skipped** (`-3dfx -w` is a windowed Glide, which plays them). Read from
  the code, not run.

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
  (gamma 155, contrast 0: 128 → 163, 192 → 212). With 256 entries and a
  top of 255, entry i is the truncation of
  (linear x contrast + powered x (100 - contrast)) / 100, where linear is
  min(i x gamma / 100, 255) and powered is (i / 255) ^ (100 / gamma) x 255.
  At contrast 100 the power term is zero and the truncation is the integer
  line above. The converter is `FUN_00682fd0`, which truncates when the
  CRT's SSE2 flag is set. Run, `video_drivers.py gamma`, through game.exe's
  own code. d2d applies it as the frame is shown (palette.hpp
  `Palette::ramp_rgba`: main.cpp's `shown_frame`, a screenshot, the hardware
  cursor), as DirectDraw's present draws the 8-bit frame through the ramped
  palette (below): the frame keeps the palette's own colours, so a blend
  through the PL2 tables finds its indices exactly (blend.hpp). Default 155
  and 100, keys `gamma` and `contrast` in d2d.cfg. Index 0 stays transparent.
- **Who applies it.** DirectDraw: `+0x58` (`0x510300`) keeps the gamma and
  rebuilds the palette through the ramp (`FUN_00510160`, contrast from
  option 11), and its present (`FUN_00511870`) loads entries 1..255 into
  the DirectDraw palette. Glide and Direct3D set the card's gamma:
  Glide's `+0x58` (`FUN_005090b0`) loads this ramp unless the contrast is
  below 1, Direct3D's (`FUN_006b3570`) always when the card supports gamma
  (the drivers' sections below).
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
| `+0x78` | `FUN_004f6430` | The environment's RGB (`FUN_0061c0b0`), each frame from `FUN_0044c990` while the window is active (`FUN_004f6070` 0). 3D only | `0x510320` `ret 4` |
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
| `+0xd0` | `FUN_004f65a0` | An 80×80 lens at (x, y), inside a 0x50 margin (below); no caller | `0x5123b0` |
| `+0xd4` | `FUN_004f5d50` | Flush the 3D floor, subtile and texture caches. 3D only | `0x510130` `ret` |

**The lens (`+0xd0`).** DirectDraw's entry (`0x5123b0`) and GDI's
(`0x6c88b0`) check that 0 < x < width − 0x50 and 0 < y < height − 0x50
(else fatal: lines 0x186 / 0x187 in DirectDraw, 0x1ab / 0x1ac in GDI), then call `FUN_00608c00(buffer +
y × width + x, width)`. That function runs a table over an 80×80 block:
each pixel is replaced by the one its offset names, working from the
edges toward the middle, top half then bottom half. `FUN_006089f0`
(fastcall pitch) builds the table. For each pixel within radius 40 (d² <
0x640) it stores at `0x8e9fc8` the offset to the pixel it shows, pulled
toward the centre (a square-root curve, at least half the distance),
clamped to 0..0x4f. It also keeps each column's first and last inside
row in `0x8e9d48` / `0x8e9e88`. Nothing calls `FUN_006089f0` (byte
scan), and the slot's thunk has no caller either, so the lens never
shows in 1.14d.

**Frame pacing (`FUN_004f6190`, slots `+0x1c` and `+0x20`).** With a clock
`FUN_004f6130` (a double), a last-frame time `DAT_007c9368`, a carry
`DAT_007c9360` and a first-call flag (bit 0 of `DAT_007c9370`; the first call
reads the clock to set the last time): elapsed = now - last. When elapsed is
not above the double at `0x6dc670` (5.0), v = 5.0 - elapsed + carry, the
whole milliseconds are `trunc(v)` and the carry becomes v minus them. The
call sleeps (`Sleep(ms)`) only for 1..5 ms, then reads the clock again and
subtracts the overshoot `(after - now) - ms` from the carry; below 1 or above
5 ms the carry is cleared and it does not sleep. Elapsed above 5.0 skips the
sleep and the carry update. Then last = the clock, slot `+0x1c`, slot
`+0x20`, return 1. It reads no video mode. `tools/emu/ui_client_helpers.py`
runs it for 130 clock scripts (the x87 maths and the CRT's float to int run
for real; the clock, `Sleep` and the two driver slots are hooked). Real
timing, the clock's own conversion and d2d's pacing are not checked.

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

## Inside the Glide driver

The Glide functions behind the slots above, by source file (the files
are the asserts' and the alloc calls' `.\Src\gl*.cpp`). Nothing here is
ported: d2d draws through its own renderer (docs/design/rendering.md),
so these rows say what the 3D look is made of, not what to copy. Read in
the decompiler and, for the register arguments and jump tables, in the
disassembly; none of it is run.

**The Glide entry points.** `FUN_00508970` (the slot `+0x00` path, which
loads `glide3x.dll`) stores each of its 45 `GetProcAddress` results in a
global. Those the rows below use: `0x87e978` `grDrawLine`, `0x87e97c`
`grDepthMask`, `0x87eab0` `grGlideShutdown`, `0x87eab4`
`grDitherMode`, `0x87eab8` `grTexDownloadMipMap`, `0x87eabc`
`guGammaCorrectionRGB`, `0x87eac4` `grDrawVertexArrayContiguous`,
`0x87eacc` `grAlphaBlendFunction`, `0x87ead0` `grTexCombine`, `0x87ead8`
`grLfbUnlock`, `0x87eadc` `grLfbLock`, `0x87eae0` `grLoadGammaTable`,
`0x87eaf0` `grAlphaCombine`, `0x87eaf4` `grSstWinOpen`, `0x87eafc`
`grTexFilterMode`, `0x87eb04` `grGlideInit`, `0x87eb14` `grFinish`, `0x87eb1c` `grGet`, `0x87eb24`
`grTexMinAddress`, `0x87eb2c` `grColorCombine`, `0x87eb30` `grColorMask`, `0x87eb34`
`grChromakeyValue`, `0x87eb38` `grCoordinateSpace`, `0x87eb3c`
`grSstSelect`, `0x87eb40`
`grTexDownloadTable`, `0x87eb44` `grTexMaxAddress`, `0x87eb88`
`grBufferClear`, `0x87eb8c` `grChromakeyMode`, `0x87eb90`
`grBufferSwap`, `0x87eb98` `grTexSource`, `0x87eb9c` `grVertexLayout`,
`0x87efa0` `grConstantColorValue`, `0x87efa4` `grSstWinClose`. Re-check: pair each `push <name>` with the next
`mov [global], eax` in `0x508970..0x508f60`. The rows name the call;
what a numeric Glide argument means is the Glide SDK's, not traced here.

**Start and stop.** Slot `+0x04` (`0x50a010`, table-only) is fatal when
called twice (string 0x2a6), keeps ECX / EDX as the other drivers do,
then makes the buffers: `FUN_0050f870`, `FUN_0050dc80`, `FUN_0050db20`,
`grGlideInit`, `grSstSelect(0)`, hands `0x509fd0` (a `jmp` to
`FUN_00509e10`) to the CRT call `0x683824`, then `FUN_0050db70`,
`FUN_0050dcd0`, `FUN_0050c760`, `FUN_0050d540`, `FUN_0050df10` and the
critical section `DAT_007c9134`. The cutscene slot `+0x34` (`0x509fe0`)
reopens the window at 640×480 (`FUN_00509f70`, EDX 0) and on success sets
gamma 100 (`FUN_005090b0(0x64)`): videos play with a flat ramp.

### `Src/glCore.cpp`

The draw functions don't call Glide's state functions directly: four
setters keep the last state and skip the call when it hasn't changed.

| Function | What |
|---|---|
| `FUN_00508fe0` | Slot `+0x18`, begin a frame: 0 while inactive (`DAT_007c9144`); sets `DAT_0087efe4`, fatal (0x401) without the critical section `DAT_007c9134`, enters it; with ECX set `grBufferClear(((EDX << 4 \| a) << 4 \| b) << 4, 0, 0)` of its two byte arguments; zeroes the three cached states, `grChromakeyMode(1)`, `FUN_0050dbc0(0xff, …)`; returns 1 |
| `FUN_005090b0` | Slot `+0x58`, set gamma (ECX). With the contrast `DAT_0087efd4` (option 11 of slot `+0x30`, `0x509430`, which answers option 10 from `DAT_0087efe4`) below 1 (Glide's default, above): `guGammaCorrectionRGB(g / 100.0)` for all three. Otherwise `grGet(5)` and `grGet(0x2a)` for the table's entries and bits, the ramp `FUN_006be190` (contrast, 4, entries, `(1 << bits) − 1`), `grLoadGammaTable`; buffer freed with the assert `glCore.cpp` line 0x43a. Also from the cutscene slot |
| `FUN_00509210` | Slot `+0x28`, the frame as 24-bit RGB: `grLfbLock(0, 1, 0xff, 0, 0, info)` (0 on failure), each 16-bit pixel `p` to `p >> 8 & 0xf8`, `(p >> 5) << 2`, `p << 3` (low bytes), width `DAT_007c9138` × height `DAT_007c913c`, the lock's stride; `grLfbUnlock(0, 1)`, 1 |
| `FUN_00509300` | Slot `+0x70`, set palette: only while the window is open (`DAT_0087efc8`), 256 entries of 4 bytes to `0xffRRGGBB` at `0x87eba0`, then `grTexDownloadTable(2, 0x87eba0)` |
| `FUN_005093f0` | Palette entry ECX shifted left 8 (from `FUN_0050bf50`, `FUN_0050c0f0`, `FUN_0050c270`) |
| `FUN_00509400` | Slot `+0x74`, copies the 0x48 PL2 table pointers to `0x87e980` and points `DAT_007c9150` at the copy |
| `FUN_00509460` | Tex-combine state `DAT_0072f254`: 1 is `grTexCombine(0, 3, 8, 3, 8, 0, 0)`, 0 is `(0, 1, 0, 1, 0, 0, 0)`; any other value is only stored |
| `FUN_005094b0` | Colour-combine state `DAT_0087efd8`: 1 is `grColorCombine(3, 1, 0, 1, 0)`, 2 is `(1, 0, 1, 2, 0)` (12 callers) |
| `FUN_00509500` | Blend state `DAT_0087efdc`, `grAlphaCombine` then `grAlphaBlendFunction`: 1 `(0,0,1,2,0)` + `(4, 0)`; 2 `(1,0,1,2,0)` + `(4, 4)`; 3 the same + `(0, 2)`; 4 the same + `(1, 5)` (12 callers) |
| `FUN_005095d0` | Filter state `DAT_0087efe0`: 1 is `grTexFilterMode(0, 0, 0)`, 2 is `(0, 1, 1)`, and the same for TMU 1 when there are two or more |
| `FUN_00509650` | Returns `DAT_0087efe8` (below), for `FUN_0050b7b0` |
| `FUN_00509670` | Shares out the card's texture memory, from the open. Sums each TMU's `grTexMaxAddress − grTexMinAddress` (kept at `0x87eae4` / `0x87eaa4`); under 3.5 MB (`3670016.0`) logs "Doesn't have enough video memory (3.5 MB) to adequately run Diablo II!" and returns 0. With `DAT_0087efe8` 0 (and only at 800×600): the video area `FUN_0050dfe0`, then three texture caches `FUN_0050e890` of 0x10000, 0x4000 and 0x1000-byte slots (each rounded up to the alignment `DAT_0087efbc`) ending at 50 %, 75 % and the top of TMU 0 (two TMUs: 50 %, the top, then TMU 1). With it set: `FUN_0050b860`'s area, the floor cache `FUN_0050f320` (0x8000-byte slots, half the rest, at most 3 MB; with two TMUs three quarters, at least 0x1ccccc), the subtile cache `FUN_0050ed90` (0x400-byte slots, a quarter, at most 1 MB), then the three texture caches at 25 %, 65 % and the top. The sizes go to the stats record (`0x7c90c8`, `+0xc`, `+0x18..+0x20`, slot `+0x4c`). Then `FUN_0050b7b0`, `FUN_00604a30(FUN_0050fcb0)`, 1 |
| `FUN_00509aa0` | Slot `+0x10`, close: when open, `grFinish`; with `DAT_0087efe8` 0 `FUN_0050e030`, else the floor and subtile caches' frees (`FUN_0050f7c0`, `FUN_0050f210`); the texture caches' free `FUN_0050ecb0` three times, `FUN_0050a0c0`, `grSstWinClose` up to 100 times until it returns 1, `FUN_0050bb70`, `DAT_007c9140` = 0 |
| `FUN_00509b60` | Slot `+0x38`: unless paused (`DAT_007c9144`), `grBufferClear(0, 0, 0)`, `grBufferSwap` of whether the record at `DAT_007c9148` (the start's ECX) has `+0x10` set, then `FUN_0050fdf0(arg)` with `DAT_0087efd0` set around it |
| `FUN_00509bc0` | Opens the Glide window (from `FUN_00509e90`, slot `+0x0c`, and `FUN_00509f70`, slot `+0x24`). Fatal (string 0x143) when already open. EAX 0 is 640×480; anything else 800×600, and `DAT_0087efe8` is set for 0 and 2. "Opening Glide window failed!" when `grSstWinOpen` returns 0. The first time: `grGet` 0x13 into `DAT_0087efb4` (the TMU count the rest reads; capped at 3, 0 fails the open), 0x24 into the alignment `DAT_0087efbc`, 0xe into `DAT_0087efb8` (the 2 MB rule below), 0x11 (above 1 sets `DAT_0087efc0`), 0xa and 0xb. Then the vertex layout (`grCoordinateSpace(0)`, four `grVertexLayout`, six with two TMUs), `grDepthMask(0)`, `grColorMask(1, 0)`, the tex-combine and filter defaults per TMU, `grChromakeyValue(0xff)`, and `FUN_00509670` |
| `FUN_00509e10` | Slot `+0x08`, shut down: when started, frees the video buffers `FUN_0050df70`, closes (`FUN_00509aa0`), `grGlideShutdown`, frees `FUN_0050f8c0`, `FUN_0050dca0`, `FUN_0050db40`; deletes the critical section; `FreeLibrary(glide3x)`. Also reached at exit through `0x509fd0` |
| `FUN_00509ee0` | Slot `+0x14`, activated / deactivated: fatal (0x3a3) without the critical section; inside it, a non-zero argument closes (`FUN_00509aa0`) and sets `DAT_007c9144`, zero reopens (`FUN_00509e90`, slot `+0x0c`) and clears it |

### `Src/glSprite.cpp`

A sprite record on the caller's stack: `+0` the cel, `+4` width, `+8`
height, `+0xc` / `+0x10` the cel's offsets, `+0x14` / `+0x18` the
texture's size, `+0x1c` its area, `+0x20..+0x2c` the s / t extents,
`+0x30` the larger side's log2, `+0x34` the aspect index. A quad is four
0x1c-byte vertices at `0x87eff8`, drawn with
`grDrawVertexArrayContiguous(5, 4, 0x87eff8, 0x1c)`.

| Function | What |
|---|---|
| `FUN_0050a0c0` | Frees `DAT_0087f0c8` and `DAT_0087f0cc`, from the close |
| `FUN_0050a260` | Fills the record from the cel ESI: `FUN_006001f0(cel, 0, 1)` (0 fails), fatal (0xce) when the cel's `+0x3c` is 0, then the four sizes `FUN_006018c0`, `FUN_006018f0`, `FUN_00601920`, `FUN_00601950` of it |
| `FUN_0050a2d0` | The texture's size: width and height each rounded up to a power of two from 8 to 256; `+0x30` the larger's log2 (3..8); the smaller side doubled at most three times toward the larger, and `+0x34` from `0x72f258` (taller) or `0x72f268` (wider) by the count, 0 when square. Without perspective (`FUN_0050dd30` 0) s runs to width / texture width and t from 1 − height / texture height to 1; with it the whole 0..1 |
| `FUN_0050a7b0` | The shadow quad, from `FUN_0050b690`: halves the record's `+0x10` and the height, the top edge half the (halved) height left of the bottom one (a slant), scaled by `DAT_00880b64` / 256 under perspective |
| `FUN_0050a9a0` | The sprite quad at (x, y): the colour and filter states; the colour is `FUN_0050dc30`'s (the light) when EAX and the last argument are 0, else the RGB of entry EAX in PL2 table `+0x114`; size scaled by `DAT_00880b64` / 256 under perspective; then `grDitherMode(2)` |
| `FUN_0050abf0` | Draw mode ESI to blend (jump table `0x50ac70`): 3 is blend 2 (`(4, 4)`) with constant alpha 0xff; 4 blend 3 (`(0, 2)`) with 0xff; 5 and 7 blend 1 (`(4, 0)`); 0, 1, 2 blend 4 (`(1, 5)`) with constant alpha 0x40, 0x80, 0xc0; 6 blend 4 with no constant set. `grDitherMode(0)` except for 5 and 7 |
| `FUN_0050acc0` | Slot `+0x80`, a unit's cel: projects the four corners (`FUN_0050dd60`); the fourth stack argument 1 moves them left by a quarter of the width, 2 right. Drawn only if a corner lies in 0..width × 0..height − 47 (above the control panel), through the texture `FUN_0050a450(arg, 0, 1)`, the blend, the light colour. Returns 1 when culled too |
| `FUN_0050b0d0` | Slot `+0x84`: 0 when the cel is off the screen (x + offset > width, x + offset + width < 0, y + offset < 0, y + offset − height ≥ height). Mode 7 asks `FUN_0050a450(table, 0, 2)`, the others `(table, 0, 1)`; then the blend and the quad |
| `FUN_0050b190` | Slot `+0x88`, the fonts: the same test, `FUN_0050a450(0, 0, 1)`, the quad in the colour argument |
| `FUN_0050b230` | Slot `+0x8c`: y minus the fourth argument when it isn't 0 (in both perspective branches, the same code); quad colour −1 |
| `FUN_0050b390` | Clips the cel to the rectangle `{x0, x1, y0, y1}` (x0 at least 0, y0 + 2 at least 0, x1 / y1 at most the screen): returns 1 when it is wholly outside, 0 with `{left skip, width, top skip, height}` filled. A clipped height of 0 or less is fatal (0x424) |
| `FUN_0050b500` | Slot `+0x98`, the automap: clips (`FUN_0050b390`), then moves the quad by the left and top skips; the texture coordinates aren't cut |
| `FUN_0050b690` | Slot `+0x90`, a unit's shadow: `FUN_0050a450(0, 0, 4)`, blend 4 with constant 0xc0 when `FUN_0050bf40` is non-zero, else the blend state alone, then `FUN_0050a7b0` |

### Glide's debug text, rectangles and tiles

No assert names the file of `0x50b9e0..0x50dbe0`. The tile slots read a
tile's block records through `FUN_005fdea0` (dt1.md): the count at
`+0x50`, 0x14-byte blocks at `+0x54` (`+0` x and `+2` y as shorts, `+4`
the block's subtile slot id, `+8` flags, `+0x10` the pixels); the
tile's floor slot id is its `+0x42`. Without perspective a block is one
32×32 quad. With it, a block's four projected corners are tested
against the view rectangle and, when one is inside, the block becomes a
3 × 5 grid of vertices (`0x880458`, steps of 8) drawn as two strips of
10, `grDrawVertexArray(4, 10, …)` with the index lists `0x72f350` and
`0x72f378`.

| Function | What |
|---|---|
| `FUN_0050b9e0` | Builds the debug font, from the window open `FUN_00509bc0` (`0x509bf3`): GDI "Courier" height 10 into an 8×16 bitmap, text colour `DAT_0072f2b0` (white); fatal (0x98) when `DAT_0088044c` is already set, else allocates it (0x3000 bytes); each of the 96 characters `' '..0x7f` drawn with `DrawTextA` and read back with `GetPixel` as bytes 0xff / 0 (0x80 a character); the widths from `GetCharWidthA` at `DAT_008802c8` |
| `FUN_0050bba0` | One debug character (in AL) at (x, y): nothing past x 0x280; x below 0 returns only its width; for `' '..0x7f` an 8×16 quad at `0x880258`, `grTexSource(DAT_00880450, DAT_008800d8[c − 0x20], 3, 0x72f2b4)`, `grDrawVertexArrayContiguous(5, 4, …)`, returns the width |
| `FUN_0050bc90` | Slot `+0xc8`, debug text: `wvsprintfA` into 512 bytes, the colour, blend, filter and tex-combine states, then `FUN_0050bba0` per character; `'\n'` goes down 0xc and back to x |
| `FUN_0050bd30` | One dot: `grConstantColorValue(EAX)`, then 8 `grDrawPoint`s at (EBX, EDI) plus the offsets `0x72f2c8` {1, 2, 0, 1, 2, 3, 1, 2} / `0x72f2e8` {0, 0, 1, 1, 1, 1, 2, 2}, a 4×3 diamond |
| `FUN_0050bd80` | Draws the 32×32 block `0x87f0d0` as dots: entry v's colour `0x72f308[v]` at x = 2 × (0x113 − row + column), y = 100 + 2 × (row + column), with `grColorCombine(1, 0, 1, 2, 0)` and chroma key off, restored after. No call, jump or pointer to it in game.exe (byte scan) |
| `FUN_0050be20` | Draws the 48×48 block as dots: row r's pointer in `*DAT_00880454`, 8-byte entries whose bytes 5, 6, 7 times byte 4, over 256, make the colour; the centre (24, 24) is black; at x = 100 + 2 × (column − row), y = 2 × (row + column + 0x32) |
| `FUN_0050bef0` | Slot `+0xcc`: keeps its two stack arguments at `0x8800d0` / `0x8800d4`, copies 0x1000 bytes to `0x87f0d0` and 0x4800 to `*DAT_00880454`, then draws only the 48×48 block (`FUN_0050be20`) |
| `FUN_0050c3a0` | Slot `+0xa8`: rectangle ECX `{x0, y0, x1, y1}`, colour EDX; fatal (0xb9, 0xba) when inverted, nothing when flat or off the screen; four lines (`FUN_0050c270`, alpha 0xff) through the sides' mid points, a diamond inside the rectangle |
| `FUN_0050c490` | Slot `+0xac`: the corners clamped to 0..width − 1 and 0..height − 1 (fatal 0xdf, 0xe0, 0xf0), nothing when flat; four lines round the box |
| `FUN_0050c630` | Slot `+0xb4`: size n (a byte, 0 draws nothing), the square from (x − (n − 1), y − (n − 1)) to (x + n, y + n) through slot `+0xb8` (`FUN_0050bf50`) in mode 5 |
| `FUN_0050c690` | The view rectangle into ECX, for side ESI (1 or 2, half the screen) and perspective (the argument): `{0, 0, width, height − 0x2f}`; flat, the bottom 0x20 lower and the left at −0x20, side 1 drops the right half and side 2 starts at width / 2 − 0x20; in perspective, a quarter of the width comes off both edges and goes to `*EDI` (negative for side 1). Fatal for a null rectangle (0x54) or EDI (0x68) |
| `FUN_0050c890` | A tile's blocks without perspective (ECX the tile, EDX x, then y and the side): a block is drawn when x + dx is inside the side's columns (from −0x20 or width / 2 − 0x20) and y + dy + 0x20 in 0..height − 0xf; `FUN_0050fbd0`, the quad, `FUN_0050dbe0`'s colour; 1 when any is drawn |
| `FUN_0050ca00` | The same with perspective: the view rectangle (`FUN_0050c690`), the corners through `FUN_0050dd60`, the grid coloured by `FUN_0050dbe0` |
| `FUN_0050cd40` | Slot `+0xa4`, a shadow tile: 0 without blocks; the colour, blend (with `grConstantColorValue(0xc0)` when `FUN_0050bf40` is non-zero) and filter states; `FUN_0050ca00` when perspective is on (`FUN_0050dd30`), else `FUN_0050c890` |
| `FUN_0050cdd0` | A wall tile's blocks without perspective (ECX the side, EDX the tile): as `FUN_0050c890`, but each vertex lit by `FUN_0050dc30` |
| `FUN_0050cf70` | A wall tile's blocks with perspective: orientation `+0` up to 9 (fatal 0x1b4) and column dx >> 5 up to 4 (fatal 0x1bc) pick the block's two ends, the words at `0x72f3a8` and `0x72f3b4` + 12 × (orientation × 6 + column), less dy; the four corners are the two ends and 0x20 above each; the grid's rows are end, middle, end, stepping 8 at a time, lit by `FUN_0050dc30` |
| `FUN_0050d410` | A wall tile (ESI): 0 without blocks; the states, with `grConstantColorValue(alpha)` unless the alpha argument is −1; `FUN_0050cf70` in perspective, else `FUN_0050cdd0` |
| `FUN_0050d490` | Slot `+0xa0`: `FUN_0050d410` with the sixth argument as the alpha |
| `FUN_0050d4c0` | Slot `+0x9c`: the same with no constant colour |
| `FUN_0050d6a0` | Slot `+0x7c`, a floor or roof tile: nothing unless `FUN_0050fad0` binds it; the states, with `grConstantColorValue` of the fifth stack argument unless it is −1. EDX is a 6×6 grid of 12-byte points: byte 0 a light row, bytes 1..3 the channels, then x and y as ints; a vertex colour is `0xff000000 \| T[b0][b1] << 16 \| T[b0][b2] << 8 \| T[b0][b3]`, T the table at `DAT_00880b5c`. When the start record's `+8` (`0x7c9148`) is set only the four corners (points 0, 5, 35, 30) go, as one fan (`grDrawVertexArray(5, 4, 0x72f670)`); otherwise all 36, as five strips of 12 (`0x880a68`) |
| `FUN_0050dbc0` | Slot `+0x78`, the environment's RGB: ECX, EDX and the argument to `DAT_00880b5a`, `DAT_00880b58`, `DAT_00880b59` |
| `FUN_0050dbe0` | A vertex's colour (ECX the vertex, EDX's low byte a light row): `0xff000000 \| T[row][b5a] << 16 \| T[row][b58] << 8 \| T[row][b59]`, at the vertex's `+8` |

### `Src/glPerspective.cpp`, `Src/glBlocks.cpp`

| Function | What |
|---|---|
| `FUN_0050dc80` | Allocates the projection table `DAT_00880b6c` (above, "Perspective"), from slot `+0x04` |
| `FUN_0050dca0` | Frees it, from the shutdown |
| `FUN_0050f870` | Allocates `DAT_00880bfc` and `DAT_00880c00` and puts them in the texture records at `0x72f6b8` / `0x72f6cc`, from slot `+0x04` |
| `FUN_0050f8c0` | Frees them, from the shutdown |

### `Src/glSmack.cpp`

The video record is GDI's `{ smack, kind }` (gdi-driver.md "Videos").
Glide decodes into one 512×64 8-bit buffer and shows it as two 256×64
RGB565 textures.

| Function | What |
|---|---|
| `FUN_0050df10` | From slot `+0x04`: the three 0x8000-byte buffers, `DAT_00880b70` / `DAT_00880b74` (the two textures) and `DAT_00880bf0` (the 8-bit frame), and the quad's colours (`0x880b80`, `+0x1c` × 3) to −1 |
| `FUN_0050df70` | Frees the three, from the shutdown |
| `FUN_0050dfe0` | From `FUN_00509670`: the two textures' card addresses `DAT_00880be8` (aligned) and `DAT_00880bec` (0x8000 on, rounded), their records `0x72f690` / `0x72f6a4` pointed at the buffers; returns the space used |
| `FUN_0050e030` | A bare `ret` (the close's call when `DAT_0087efe8` is 0) |
| `FUN_0050e040` | Slot `+0x48`, close: `_SmackClose_4` when the kind is `'s'` and there is a handle, then kind 0. The buffers stay |
| `FUN_0050e070` | The 8-bit frame through the palette ECX (3 bytes an entry) to RGB565, `(r & 0xf8) << 8 \| (g & 0xfc) << 3 \| b >> 3`: columns 0..255 into `DAT_00880b70`, 256..511 into `DAT_00880b74` |
| `FUN_0050e330` | `grTexDownloadMipMap` of both textures (TMU `DAT_00880bf4`, mode 3, records `0x72f680` / `0x72f694`) |
| `FUN_0050e370` | Draws the frame, from slot `+0x44`: the four states, `grChromakeyMode(0)`, the left texture as a quad up to 256 wide, the right one for the rest (width − 256), `grChromakeyMode(1)` |
| `FUN_0050e5d0` | Slot `+0x40`, open. Fatal (0x8e) without the frame buffer; clears all three and, unless paused, downloads them. `'p'`: loads and decodes as GDI does (type 2, assert `glSmack.cpp` line 0xdb), only 468×60 at 8 bits, then moves each row to a 512-byte stride, converts and downloads. `'s'`: when the record has no handle, `_SmackOpen_12(name, 0xfe000, −1)` and `_SmackToBuffer_28(h, 0, 0, 0x200, 0x40, frame, 0)`. A failure closes as `FUN_0050e040` |

### `Src/glTextureCache.cpp`, `Src/glSubtileCache.cpp`, `Src/glFloorCache.cpp`

Three copies of one slot pool for the card's texture memory, each slot
on a used list (least recently used first) or a free list. The header is
eight words: `+0` the slot's bytes, `+4` the slot count, `+8` the used
count, `+0xc` / `+0x10` the used list's head and tail, `+0x14` / `+0x18`
the free list's, `+0x1c` the slots. A texture slot is 0x1c bytes (`+0`
its index, `+4` its card address, `+8` / `+0xc` the owner table and
index, `+0x14` / `+0x18` next and previous); a subtile or floor slot 0x14
(`+0` index, `+4` address, `+8` owner, `+0xc` / `+0x10` next and
previous). An id is the index plus 1.

**The 2 MB rule.** When `DAT_0087efb8` is 0, each init unlinks the first
slot that straddles card address 0x200000 (counting it used, so it is
never given out) and drops a last slot that does.

| Texture | Subtile | Floor | What |
|---|---|---|---|
| `FUN_0050e890` | `FUN_0050ed90` | `FUN_0050f320` | Init (ECX the header, EDX start, then end and slot size): count = (end − start) / size, every slot on the free list at start + i × size, the 2 MB rule; returns start + count × size. The subtile and floor ones are the same text and are fatal (0x2a) when the allocation fails |
| `FUN_0050ea40` | `FUN_0050ef40` | `FUN_0050f4d0` | Append slot ECX to the used tail |
| `FUN_0050eae0` | `FUN_0050efe0` | `FUN_0050f570` | Unlink slot (the index) from the used list and return it. Subtile: index in EAX, fatal (0x17d) without slots, the head through `FUN_0050ef80` |
| — | `FUN_0050ef80` | — | Pop the used list's head |
| `FUN_0050eb60` | `FUN_0050f070` | `FUN_0050f5f0` | Take the free head onto the used tail and return its id; 0 when all are used; fatal (0xc1, 0x9d, 0x9f) when the free list is empty anyway |
| `FUN_0050ebe0` | `FUN_0050f0f0` | `FUN_0050f670` | Mark id EDX used (move it to the tail), only with two or more in use; fatal for id 0, above the count, or negative |
| `FUN_0050ec40` | — | `FUN_0050f6d0` | Evict the head to the free list, clearing the owner's entry: the texture's 8 bytes at owner + index × 8; the floor's 16-bit `+0x42` (fatal 0xd3 with no owner) |
| — | `FUN_0050f1b0` | `FUN_0050f750` | Release one slot to the free list (subtile: the owner's 16-bit `+4` cleared; floor: `+0x42`, fatal 0xf1 with no owner) |
| `FUN_0050ecb0` | `FUN_0050f210` | `FUN_0050f7c0` | Free: with EDX set evict every slot first, free the slots, zero the header. From the close and from `FUN_00508970` |
| `FUN_0050ed10` | `FUN_0050f2b0` | `FUN_0050f820` | Get a slot: take a free one; while that gives 0 and some are used, the texture and subtile ones evict the used head (clearing the owner's entry) and try again; the floor one evicts once (`FUN_0050f6d0`), tries again and is fatal (0x8b) |
| — | `FUN_0050fbd0` | `FUN_0050fad0` | Bind: the id (subtile: the block's `+4`; floor: the tile's `+0x42`, after `FUN_005fdea0`, 0 without blocks) is fatal when negative (0x105, 0xd8); 0 decodes the pixels, gets a slot (fatal 0x112 / 0x113, 0xe5 / 0xe6 for a bad id), makes the block or tile its owner and `grTexDownloadMipMap(DAT_00880bf8, address, 3, 0x72f6a8 / 0x72f6bc)`; otherwise marks it used. Then `grTexSource` and the tex-combine state |
| — | `FUN_0050f910` | `FUN_0050f980` | Decode: subtile, 32 rows of {skip, count, bytes} runs ({0, 0} ends a row) into 32×32 at EAX; floor, every block with flag bit 0 into a 256-wide 0x8000-byte buffer at (x, y): bit 2 clear a diamond of 15 rows, 4, 8, … 32, … 4 bytes from x + 0xe (fatal 0xa8 / 0xa9 for null data or buffer), bit 2 set 15 rows of runs |

## Inside the Direct3D driver

The Direct3D twins of the Glide files, by `.\Src\d3d*.cpp`. Quads go into
a 44-vertex ring: the vertex buffer `DAT_00970f3c` (its vtable `+0xc`
with flags 0x21 to get a pointer, `+0x10` to give it back), then the
device `DAT_00970e78`'s vtable `+0x88` with (6, buffer, `DAT_00970f40`,
4, 0x1c); `DAT_00970f40` steps by 4 and wraps to 0 past 0x2b. A vertex is
0x20 bytes: x, y, 0.5, 1.0, the colour at `+0x10` (alpha in the top
byte), s and t at `+0x18`. What each COM call is was not looked up.

### `Src/d3dCore.cpp`

The core's slots and helpers that the sections below lean on (the file
table above names `d3dCore.cpp` for this range). The colour and filter
states cache the last value, as Glide's do; the device calls are by
vtable offset.

| Function | What |
|---|---|
| `FUN_006b1a10` | A bare `ret`. Its address is a dword at `0x6faffc`, in data nothing references |
| `FUN_006b1a20` | Stores EDX at `[ECX]`. No call, jump or pointer to it in game.exe (byte scan) |
| `FUN_006b1c70` | Slot `+0x70`, set palette: each of the 256 4-byte entries becomes `0xff` then its bytes 0, 1, 2 (high to low) at `0x9702c0` and a 15-bit colour with the top bit set at `0x96fa80`, each channel's top 5 bits rounded up when the low 3 are above 3 (and the 5 bits below 31); a black entry is 0 in both |
| `FUN_006b1d60` | Slot `+0x74`: copies the 0x48 PL2 table pointers to `0x96f280` and points `DAT_007c9150` at the copy |
| `FUN_006b1d80` | Copies rows (ECX source, EDX destination, then bytes a row, rows, destination pitch). No call, jump or pointer to it in game.exe (byte scan) |
| `FUN_006b1dc0` | 8-bit rows to 16-bit through `0x96fa80` (ECX source, EDX destination, then width, rows, destination pitch in pixels); from the sprite upload (`0x6b6107`) and the subtile bind (`0x6bbae5`) |
| `FUN_006b1e30` | The same to 32-bit through `0x9702c0`. No call, jump or pointer to it in game.exe (byte scan) |
| `FUN_006b1ea0` | Colour state `DAT_00970e54`: 1 is the device's `+0xa0(0, 1, 4)`, `+0xa0(0, 4, 4)`, `+0x58(0xf, 1)`; 2 is `(0, 1, 3)`, `(0, 4, 3)`, `+0x58(0xf, 0)`; any other value is only stored |
| `FUN_006b2030` | Filter state `DAT_00970e5c`: 1 is `+0xa0(0, 0x10, 1)` and `(0, 0x11, 1)`, 2 the same with 2; any other value is only stored |
| `FUN_006b3420` | The cutscene window, jumped to from slot `+0x34` (`0x6b3530`, at `0x6b355c`) after it closes (`FUN_006b3320`), and only when the device has the bit in `0x6ce284` or `0x6ce288`; otherwise the slot returns 1 without reopening. Fatal (0x7cb) when it is already open; 640×480 at 16 bits (32 with `DAT_0096f3a8` bit 0x80, 24 with 0x100), `DAT_00970e60`'s `+0x54`, then `+0x18` makes `DAT_00970e68` (the surface the Bink player draws into); on failure logs "Opening Direct3D window for cutscenes failed!" and returns 0 |
| `FUN_006b3570` | Slot `+0x58`, set gamma: only when the device supports it (`[0x6ce278] & [0x96f3a8]`, the `+0x5c` test); the ramp `FUN_006be190` with ECX, the gamma, passed through and the contrast `DAT_0074c020` (option 11, stored by slot `+0x30` at `0x6b20c8`), 2-byte entries, 256 of them, top 0xffff; the same ramp for red, green and blue, then `DAT_00970e74`'s `+0x10(0, ramps)` |
| `FUN_006b3610` | Slot `+0x28`, the frame as 24-bit RGB: locks `DAT_00970e6c` (`+100`, flags 0x11) and writes each 32-bit pixel as its bytes 2, 1, 0; returns 1, or 0 when the lock or unlock fails |
| `0x6b4c42`, `0x6b4c73` | Catch blocks in the enumeration code (they resume at `0x6b43ef` / `0x6b4456`): log " ** Caught a failure in sEnumerateD3DDevices ** " (and clear the device record being filled, `DAT_0096f83c` or `DAT_0096f95c`) or " ** Couldn't clean up sEnumerateD3DDevices ** " |
| `FUN_006b4c90` | Direct3DDetect, from `FUN_006b4e80` (slot `+0x00`, `0x6b4f0d` and `0x6b4f4c`): logs "Detecting boards which support Direct3D...", registers "Enumeration Class", opens a 640×480 "Direct3D Enumeration" window, `DirectDrawEnumerateA(FUN_006b42f0)`, destroys it ("Destroyed D3D window."); with no device found logs "Found no valid Direct3D devices to use!" and returns 0; returns 2 (after `FUN_006b2f60`) when either device record is 2 |
| `0x6b4d4b` | `FUN_006b4c90`'s catch block (resumes at `0x6b4d5e`): logs " ** Caught a failure in Direct3DDetect ** " |
| `FUN_006b55e0` | Slot `+0x0c`, open: unless the cutscene window is open (`DAT_00970e40`), `FUN_006b4fd0` (0 on failure), the clear `FUN_006b3800`, then `DAT_00970e68`'s `+0x2c(0, 1)` when the start record's `+0x10` is set, else `(0, 9)` |
| `FUN_006b5630` | Slot `+0x14`: as `FUN_00509ee0` (fatal 0x7b0), closing with `FUN_006b3320` and clearing the Bink flag `DAT_007c9140`; reopens with `FUN_006b55e0` |

### `Src/d3dSprite.cpp`

| Function | What |
|---|---|
| `FUN_006b5b10` | From `FUN_006b32b0`: releases the vertex buffer and the six objects at `0x970f1c` / `0x970f28` (vtable `+8`), frees `DAT_00970f34` and `DAT_00970f38` |
| `FUN_006b5c80` | `FUN_0050a260`'s twin, but a cel with no `+0x3c` logs "error loading sprite unit:%d  class:%d  mode:%d  %s" and returns 0 instead of the fatal |
| `FUN_006b5cf0` | The texture's size: square, 32 when both sides fit, else 128, else 256 (a larger cel leaves it unset); s and t always span 0..1 |
| `FUN_006b61a0` | The shadow quad (`FUN_0050a7b0`'s shape), the alpha byte argument in all four colours |
| `FUN_006b63f0` | The sprite quad: the colour and filter states; the light colour (`FUN_006b9780`) when the index and the last argument are 0, else PL2 table `+0x114`'s RGB, with the alpha byte |
| `FUN_006b6680` | Draw mode ESI to blend (`FUN_006b1f40`, ECX; jump table `0x6b66e4`) and an alpha byte: the same mapping as `FUN_0050abf0`, with mode 6 alpha 0xff; the alpha goes in the vertex colour |
| `FUN_006b6730` | Slot `+0x80`: as `FUN_0050acc0` (the quarter-width shift, the test above the panel, 1 when culled), the texture `FUN_006b5d40(arg, 0)` |
| `FUN_006b6b40` | Slot `+0x84`: as `FUN_0050b0d0`, but every mode asks `FUN_006b5d40(table, 0)`: no mode 7 case |
| `FUN_006b6bf0` | Slot `+0x88`: as `FUN_0050b190` |
| `FUN_006b6c90` | Slot `+0x8c`: as `FUN_0050b230` |
| `FUN_006b6d70` | The clip, `FUN_0050b390`'s text (fatal 0x45a) |
| `FUN_006b6ee0` | Slot `+0x98`: as `FUN_0050b500` |
| `FUN_006b7000` | Slot `+0x90`: alpha 0xc0 when `FUN_006b77a0` is non-zero, else 0xff, then `FUN_006b61a0` |

### Direct3D's debug text, rectangles and tiles

The twins of Glide's unnamed file (`0x6b7250..0x6b9a00`). Where Glide
draws a perspective block as two strips of 10, Direct3D makes one indexed
call: the device's `+0x74(5, 0x1c4, 0x971450, 15, 0x74c0d0, 0x16, 0x1c)`.
Single quads go straight to the device's `+0x70(6, 0x1c4, vertices, 4,
0x1c)`, not through the ring. The alpha that Glide sets with
`grConstantColorValue` is the vertices' alpha byte here.

| Function | What |
|---|---|
| `FUN_006b7250` | Releases (vtable `+8`) and clears the 96 objects at `0x9712d0` and the 96 at `0x971150`, from `FUN_006b32b0` (`0x6b32b9`) |
| `FUN_006b72a0` | The debug font: `FUN_0050b9e0`'s GDI part (text colour `DAT_0074c0cc`); per character a staging object (`FUN_006b3730(8, 0x10)`) locked (`+100`, 0x21), filled with 16-bit 0xffff / 0, unlocked (`+0x80`), and a texture `FUN_006b58a0(8, 0x10)` at `0x9712d0` (fatal 0xbf when one can't be made) copied from it (`+0x14`, or `FUN_006b38a0(0x10)` when `DAT_0096f3a8` has 0x400); widths at `DAT_00970fc8` |
| `FUN_006b7530` | One debug character: as `FUN_0050bba0`, the device's `+0x98(0, texture)` then `+0x70` |
| `FUN_006b7630` | Slot `+0xc8`: as `FUN_0050bc90` |
| `FUN_006b76d0` | From `FUN_006b3080` (`0x6b314a`): the font (`FUN_006b72a0`) and its four vertices (z 0.5, w 1.0, colour 0xffffffff, s / t 0 and 1); returns ECX + 0x6000 |
| `FUN_006b77b0` | Slot `+0xb8`: the rectangle (ECX, EDX, then x1, y1, colour, mode) clamped to the screen, nothing when flat, fatal (0x2b) when inverted; colour `FUN_006b1d50`'s RGB with alpha 0x40 for mode 0, 0x80 for 1, 0xc0 for 2, else 0xff |
| `FUN_006b7980` | Slot `+0xbc`: the same (fatal 0x68), with the alpha byte argument as the vertices' alpha |
| `FUN_006b7c90` | Slot `+0xa8`: as `FUN_0050c3a0` (fatal 0xc5, 0xc6), lines through `FUN_006b7b30` |
| `FUN_006b7d80` | Slot `+0xac`: as `FUN_0050c490` (fatal 0xeb, 0xec, 0xfc) |
| `FUN_006b7f20` | Slot `+0xb4`: as `FUN_0050c630`, through `FUN_006b77b0` in mode 5 (alpha 0xff) |
| `FUN_006b7f70` | Slot `+0xc4`, clear: `FUN_006b3800` when `DAT_0096f3a8` has 0x400, else `DAT_00970e7c`'s `+0x50(1, {0, 0, width, height}, 1, 0xff000000, 0, 0)` |
| `FUN_006b7fd0` | The view rectangle: as `FUN_0050c690` (fatal 0x53, 0x69) |
| `FUN_006b80a0` | From slot `+0x04` (`0x6b5ace`): the quad's vertices at `0x971630` and the grid's 15 at `0x971450` (s = column × 0.984375 × 0.5 + 1/64, t = row × 0.984375 × 0.25 + 1/64); shifts the first two words of each 12-byte entry of the wall table `0x74c100` (10 orientations of 6) left 16 bits |
| `FUN_006b8220` | A shadow tile's blocks without perspective: as `FUN_0050c890`, bound by `FUN_006bba00`, the four colours from `FUN_006b9730(alpha)` |
| `FUN_006b83a0` | The same with perspective: as `FUN_0050ca00` (projection `FUN_006b9860`), one indexed call |
| `FUN_006b86f0` | Slot `+0xa4`: alpha 0xff, 0xc0 when `FUN_006b77a0` is non-zero; `FUN_006b83a0` in perspective (`FUN_006b9830`), else `FUN_006b8220` |
| `FUN_006b87a0` | A wall tile's blocks without perspective: as `FUN_0050cdd0`, lit by `FUN_006b9780(alpha)` |
| `FUN_006b8950` | With perspective: as `FUN_0050cf70` (fatal 0x213, 0x21b; ends at `0x74c108` / `0x74c114`, the words `FUN_006b80a0` leaves alone), lit by `FUN_006b9780(alpha)`, one indexed call |
| `FUN_006b8df0` | A wall tile (ESI, alpha in BL): 0 without blocks; the states; `FUN_006b8950` in perspective, else `FUN_006b87a0` |
| `FUN_006b8e60` | Slot `+0xa0`: `FUN_006b8df0` with the sixth argument as the alpha |
| `FUN_006b8e90` | Slot `+0x9c`: the same with alpha 0xff |
| `FUN_006b8f10` | The floor's vertex buffer, from `FUN_006b4fd0` (`0x6b5510`): `DAT_00970e64`'s `+0x24({0x10, 0x10800, 0x1c4, 0x24}, &DAT_00971734, 4, 0)` (`FUN_006b39b0` on failure); 6×6 vertices with s = (column − row) × (160 / W) / 10 + (0x50 ± 1) / W and t = (column + row) × (80 / H) / 10 + (±1) / H, the ±1 only on the edges (W × H the floor slot, `DAT_0096f4dc` × `DAT_009702b8`); then five 13-index strips (`0x9716b0`) |
| `FUN_006b9150` | Releases that buffer, from `FUN_006b32b0` (`0x6b332f`) |
| `FUN_006b9170` | Slot `+0x7c`: as `FUN_0050d6a0` (bind `FUN_006bb870`, the table at `0x971740`, the alpha byte as is), writing into the locked buffer: all 36 points then the device's `+0x8c(5, buffer, 0x9716b0, 0x41, 0x1c)`, or with the start record's `+8` set only the four corners and `+0x8c(6, buffer, 0x74c3d0, 4, 0x1c)` |
| `FUN_006b96c0` | From slot `+0x04` (`0x6b5ac4`): the 256×256 table at `0x971740`, row a column b = a × b / 255 |
| `FUN_006b9710` | Slot `+0x78`: as `FUN_0050dbc0`, into `0x97173a`, `0x971738`, `0x971739` |
| `FUN_006b9730` | A vertex's colour (ECX the vertex, EDX's low byte the light row, the alpha argument): as `FUN_0050dbe0`, at `+0x10` |
| `FUN_006b97d0` | From slot `+0x04` (`0x6b5ac9`): the projection table, 0x2000 ints at `0x981748`, for d = −0x1000..0xfff the value 0x2ee000 / (d clamped to ±2999, minus 3000); the sprite size `DAT_00989748` = 0x100 |
| `FUN_006b9830` | Perspective on: the start record's first word when its second is non-zero, else 0 |
| `FUN_006b9860` | Slot `+0x64`, project (x, y, z): dx = (x − `0x981740`) × 0x18 >> 16 and dy the same from `0x981744`, s = dx + dy, k = table[(s × 0x9c9b8 + z × 0x99800 >> 20) − 0x1000, wrapped to 0x2000]; screen x = ((dy − dx) × k × 0x2d4 >> 20) + width / 2, y = (((z × 0x109a08 − s × 0x5a800) >> 10) × k >> 20) − 0x20 + height / 2 |
| `FUN_006b9910` | Slot `+0x68`: the same at z 0 with y − 0x10, and the sprite size `DAT_00989748`: the y term less (the y term's numerator + 0x37400) × the table entry 0x80 further on, >> 20; then × 0x4cc >> 10 when the last argument is set, else × 0x466 >> 10 |

### `Src/d3dSmack.cpp`

Named by `FUN_006ba010`'s free (`FUN_00412650`, line 0xda); the rows
before it share its globals. Like Glide, two 256×64 textures, but 16-bit
with the top bit set, from a 0x10000-byte frame `DAT_009897d0`.

| Function | What |
|---|---|
| `FUN_006b9a10` | From slot `+0x04` (`0x6b5ad3`): the four video vertices' colours 0xffffffff, the frame (`FUN_0040b380(0x38)`) cleared |
| `FUN_006b9a50` | From `FUN_006b3080` (`0x6b31e4`), for each half: `FUN_006b3730(0x100, 0x40)` into `0x9897d4` / `0x9897dc` (+4 for the second; fatal 0x4a, 0x4b when empty) and `FUN_006b58a0(0x100, 0x40)` with `0x9897e4` / `0x9897ec` (fatal 0x4e, 0x4f, and 0x50 when it returns 0); returns ECX + 0x10000 |
| `FUN_006b9b00` | Frees the frame, from slot `+0x08` (`0x6b555b`) |
| `FUN_006b9b30` | Releases the eight objects, from `FUN_006b32b0` (`0x6b32de`) |
| `FUN_006b9bb0` | Slot `+0x48`: as `FUN_0050e040` |
| `FUN_006b9bd0` | Copies the staging objects to the textures (`+0x14`), or `FUN_006b38a0(0x40)` twice when `DAT_0096f3a8` has 0x400; nothing before `FUN_006b9a50` |
| `FUN_006b9c40` | Draws the frame (from `0x6ba3ff` and `0x6ba417`, slot `+0x44`): ECX's `+8` x and `+0xc` bottom, EDX the height, EAX the width; the first texture as a 256-wide quad, the second for the rest with s up to (width − 0x100) / 256 |
| `FUN_006b9eb0` | Copies 64 rows of 256 pixels from ECX (pitch 0x400) to EAX (the pitch argument), each with bit 15 set |
| `FUN_006b9f00` | Locks each staging object (`0x9897d4`, then `0x9897d8`, fatal 0x1db when that one is null), `FUN_006b9eb0`, unlocks |
| `FUN_006ba010` | Slot `+0x40`, open: unless inactive, fatal (0x94) without the frame, which it clears and shows. `'p'`: the file (`FUN_0041b680`) decoded by `FUN_006c6a80` (type 2), used only at 468×60×8, each pixel `(r & 0xf8) << 7 \| (g & 0xf8) << 2 \| b >> 3` (15-bit) at pitch 0x400, then shown. `'s'` with no handle yet: `_SmackOpen_12(name, 0xfe000, −1)` and `_SmackToBuffer_28(…, 0, 0, 0x400, 0x80, frame, 0x80000000)`. A failed open closes |

### `Src/d3dTextureCache.cpp`, `Src/d3dSubtileCache.cpp`, `Src/d3dFloorCache.cpp`

The same pool with seven header words (no slot size: `+0` the count,
`+4` used, `+8` / `+0xc` the used list, `+0x10` / `+0x14` the free list,
`+0x18` the slots) and a texture object per slot made at init
(`FUN_006b58a0(w, h)`). A texture slot is 0x20 bytes (`+4` / `+8` owner
and index, two objects at `+0x10` / `+0x14`, next / previous at `+0x18` /
`+0x1c`); a subtile or floor slot 0x18 (`+4` owner, objects at `+8` /
`+0xc`, next / previous at `+0x10` / `+0x14`). There is no 2 MB rule.
The three inits are called from `FUN_006b3080`, the frees from
`FUN_006b1b60` and `FUN_006b32b0`.

| Texture | Subtile | Floor | What |
|---|---|---|---|
| `FUN_006ba420` | `FUN_006ba9a0` | `FUN_006bafc0` | Init: count = (end − start) / slot size, capped at 0x400 (subtile 0x800); texture slots are `DAT_00970e80` × w × h bytes, subtile textures 32×32, floor `DAT_0096f4dc` × `DAT_009702b8`. The first object that can't be made ends the count there (fatal 0x5a / 0x49 when it is the first) |
| `FUN_006ba550` | `FUN_006baac0` | `FUN_006bb0e0` | With EDX set and more than one slot: calls the first object's vtable `+0x7c(EDX)` for slots 0 to count − 2 and on the first non-zero answer calls `FUN_006b20e0`. No caller, no pointer to them in game.exe |
| `FUN_006ba5c0` | `FUN_006bab30` | `FUN_006bb150` | Append to the used tail |
| `FUN_006ba660` | `FUN_006babd0` | `FUN_006bb1f0` | Unlink index EDX from the used list |
| `FUN_006ba6d0` | `FUN_006bac50` | `FUN_006bb270` | Take a free slot; texture fatal 0xd7 (empty free list) and 0xd8 (index past the count). Subtile and floor have no check: an empty free list with free count left would read through a null slot |
| `FUN_006ba760` | `FUN_006bacb0` | `FUN_006bb2d0` | Mark id EDX used, as Glide's |
| `FUN_006ba7c0` | — | — | Evict the head (the owner's 8 bytes cleared) |
| — | `FUN_006bad70` | `FUN_006bb390` | Release one slot (owner's 16-bit `+4`; floor `+0x42`) |
| `FUN_006ba830` | `FUN_006badc0` | `FUN_006bb3e0` | Free: fatal for a null header (0x71 / 0x60) or used slots with no array (0x74 / 0x63); releases both objects of every slot, evicts all with EDX, frees the array, zeroes the header |
| `FUN_006ba920` | `FUN_006baf00` | `FUN_006bb520` | Get a slot. Texture: as `FUN_0050ed10`. Subtile and floor: take a free one; when none, evict used heads (the owner's `+4` / `+0x42` cleared) while all are used, move the free head to the used tail and repeat while that slot's `+0` is −1 (a null free head is read through) |
| — | `FUN_006bba00` | `FUN_006bb870` | Bind: as Glide's (fatal 0x1ef, 0x1fc / 0x1fd; floor 0x191, 0x19c / 0x19d, 0 without blocks), but the pixels go through a staging object: lock (vtable `+100`, flags 0x21; fatal 0x20b / 0x1ad without flag bit 3 back), decode, unlock (`+0x80`), copy to the slot's texture (`+0x14`, or `FUN_006b38a0` when `DAT_0096f3a8` has 0x400), then the device's `+0x98(0, texture)` |
| — | `FUN_006bb6b0` | `FUN_006bb720` | Decode: subtile as `FUN_0050f910`; floor as `FUN_0050f980` but 16-bit through the palette `DAT_0096fa80`, at the locked pitch, and without the null checks |

Around the pools: `FUN_006bb5e0`, from `FUN_006b4fd0` (`0x6b550b`),
makes two object pairs with `FUN_006b3730` (ECX and EDX the two
places): `0x989bf8` / `0x989bfc` at the floor size (fatal 0x27) and
`0x989c00` / `0x989c04` at 32×32 (fatal 0x2a). The binds lock the first
of a pair and copy from the second. `FUN_006bb640` releases all four.
`FUN_006bbb90`, whose address `FUN_006b3080` takes at `0x6b329a`,
releases a tile's floor slot (`+0x42`) and each block's subtile slot
(`+4`).

### After the caches

| Function | What |
|---|---|
| `FUN_006bbbf0` | One Bink frame (from `FUN_006bbce0`), while `DAT_007c9140` is set: `_BinkDoFrame_4`; inside the critical section (fatal 0x3d without it) locks `DAT_00970e68` (`+100`) and `_BinkCopyToBuffer_28` into it at (`DAT_00989c0c`, `DAT_00989c08`); at the last frame sets the done flag `DAT_00989c1c`, else `_BinkNextFrame_4` |
| `FUN_006bbce0` | Plays a Bink file (from `FUN_006b56c0`, slot `+0x38`, at `0x6b5709`) unless one is open or the window is inactive: `_BinkDDSurfaceType_4`, the DirectSound sound system, the file through `FUN_004192f0`, `_BinkOpen_8` with flags 0x800000 (argument 1 adds 0x30000000, 2 adds 0x10000000, 3 adds 0x40000000), centred (x rounded up to a multiple of 4). It loops until the film is done and 500 ms have passed since it began, the window goes inactive, or a click or key comes (`WM_LBUTTONDOWN`, `WM_RBUTTONDOWN`, `WM_MBUTTONDOWN`, `WM_CHAR`); `WM_QUIT` ends it and the game exits after the close. Each pass calls the callback argument, then a message, or `_BinkWait_4` and a frame, or `Sleep(1)` |
| `FUN_006bbf60` | Four bytes as a number in base 255, each byte a digit plus 1. No call, jump or pointer to it in game.exe (byte scan) |
| `FUN_006bbfa0` | The reverse: a number into four such bytes. No call, jump or pointer to it in game.exe (byte scan) |
