# The Help screen — game.exe 1.14d

UI 0x21. The key config's Help Screen (`0x4689d0`, H by default)
toggles it. It also sets the "Help Menu" registry flag the first time.
d2d: `apps/d2d/help.{hpp,cpp}`, opened from `town.cpp`.

## Drawing (FUN_00456ee0 → FUN_00494320, FUN_00494eb0, FUN_004966c0)

- `FUN_00494320` loads `UI\Menu\800helpborder` (`helpborder` at
  640×480), `helpwhitebullet` and `helpyellowbullet`.
- `FUN_00494eb0` darkens everything above the control panel,
  W × (H − 0x31), to half (`FUN_0046efd0`, mode 1).
- `FUN_004966c0` brings the mini-panel (UI 0x15) up if it's down and
  remembers that (`0x7bef00`). `FUN_00494fc0` puts it away again when
  Help closes. At 800×600 it then draws:
  - `FUN_004944f0`: the border's seven frames, bottoms (0, 0x100),
    (0, 0x200), (0x42, 0x14), (0x142, 0x14), (0x217, 0x100),
    (0x30c, 0x100), (0x30c, 0x200).
  - `FUN_00494e40`: the close button, `buysellbtn` frame 10 (11
    pressed), at (0x2ad, bottom 0x38).
  - `FUN_00494620`: eight yellow bullets at x 0x68, bottoms 0x49 + 20i.
  - `FUN_00494a00`: eleven white bullets. Each has a two-pixel line in
    palette colour 0xff (`FUN_004f6380`) up to the control panel part it
    names.
  - `FUN_00495410`: the labels. Their positions came from running the
    function in the emulator with the text calls hooked; the numbers are
    in `help.cpp` `kLabels`. "Diablo II Help" (0x1066) is centred at
    0x17c, baseline 0x11. "Close" (0x1030) is centred at 701, baseline
    0x49. Eight lines sit at x 117, baselines 78 + 20i. Five of them
    take a function's first key by name through `%s`: Run (0x1067),
    Show Items (0x1068), Stand Still (0x1069), Automap (0x106a) and
    this screen (0x106e). The rest are the control panel's labels,
    centred, in FontFormal12 (font 5), white.

## Input (FUN_00494f30)

A press on the close button (0x21 square from (0x2ad, 0x18)) holds it,
and its release closes the screen. Every other click goes nowhere.

## d2d's ceilings

- The title and Close are drawn in the font current before
  `FUN_00495410` sets font 5. That font isn't traced; d2d uses Font16.
- 800×600 only. The 640×480 layout (`FUN_004943b0`, `FUN_00494680`,
  `FUN_00495dc0`) isn't ported.
