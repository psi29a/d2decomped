# Icon

`d2d.svg` is the master. Goat skull & pentagram: **Designed by dgim-studio /
Freepik**, used under the Freepik license (see `LEGAL.md`), not the GPL. The
binary half is ours.

The rest are rendered from it (rsvg-convert, ImageMagick, macOS iconutil):

| file | used by |
|---|---|
| `d2d.icns` | D2D.app (macOS bundle icon), inset to the 824/1024 macOS grid |
| `d2d.ico` + `d2d.rc` | d2d-launcher.exe, d2d.exe (Windows) |
| `d2d.png` (256) | the launcher's window icon (Qt resource) |
| `d2d.bmp` (64, BMP4 with alpha) | d2d's window/dock icon (SDL_LoadBMP, embedded at configure) |
| `hero.png` (1120 wide) from `hero.svg` | the launcher's banner, 2x its 560 px width |

`hero.svg` draws `d2d.svg` beside the wordmark: AvQest (freeware, GemFonts / Imitation Warehouse; the Exocet look-alike famfonts offers for Diablo),
converted to outlines, so no font is needed to render it.
