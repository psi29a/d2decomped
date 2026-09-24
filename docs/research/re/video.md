# Cinematics — game.exe 1.14d

Bink files in d2video.mpq / d2xvideo.mpq. d2d decodes them with ffmpeg
(binkvideo, binkaudio_rdft: 24 fps, 44.1 kHz stereo) through
`apps/d2d/video.hpp`. The file streams from the MPQ (`mpq::File`); the
D2 intro is 94 MB.

## Startup (`FUN_00435230`)

When videos are enabled (`FUN_004f9050`):
1. `Data\Local\Video\New_BLIZ640x480.bik`, then `BlizNorth640x480.bik`.
   The 640x240 versions play in "interlaced" video mode
   (`FUN_0040eb10`), which uses mode 2.
2. With d2video.mpq, if the D2 intro hasn't been seen (`FUN_0042fa40`):
   `%s\video\%s\d2intro%s.bik` (`FUN_00433640`, e.g.
   `ENG\d2intro640x292.bik`).
3. Otherwise, for LoD, if its intro hasn't been seen (`FUN_0042fab0`):
   `D2x_Intro_%s.bik` (`FUN_004334e0`). "Seen" lives in the registry;
   the code sets a bit in "Diablo II\Aux Battle.net".
4. Then the frontend.

Playback goes through the renderer's vtable (`+0x38`,
`FUN_004f5d90(path, mode, …)`).

Other videos: `Act02start` / `Act03start` / `Act04start` / `Act04end` /
`D2x_Out_%s.bik` (act transitions and the LoD ending), plus the
Cinematics menu (`CinematicsSelection[EXP]` panels).

## d2d

- The startup sequence is the one above. "Seen" is kept in
  `<user dir>/cinematics_seen`.
- Frames: the 640x480 video mode is shown at 800x600, with the 640x292
  intros letterboxed at 800x365.
- Audio: an OpenAL streaming source (8 × 4096-frame buffers).
- Controls: a click or key skips the current video; Esc goes to the
  title.
- The cursor is hidden while a video plays.
- `--no-video` or `video = 0` in d2d.cfg turns the videos off.
- Not yet: act transition videos in game.

## Cinematics menu (LoD: `FUN_00431600` → `FUN_004313d0`)

Built from frontend records by index (`FUN_0042f430(i)` = record
`0x708d10 + i*0x30`). Records use bottom-left y:

| record | what |
|---|---|
| 8 | title background |
| 0x3d | `FrontEnd\CinematicsSelectionEXP` panel: 2×2 frames, 326×427, at (237, 505) |
| 0x3e | text box, (262, 153) 272×35: "SELECT CINEMATICS" (0x13fa, `FUN_004fd060`) |
| 0x3f..0x45 | WideButtons at x 262, bottoms 181/224/268/310/353/396/439, strings 0x5525..0x552b |
| 0x46 | CANCEL (0x13ef), MediumButton at (334, 488) |

The seven entries, their strings (0x5525..0x552b) and handlers
(`0x434320..0x434560`):

| # | string | video |
|---|---|---|
| 1 | The Sister's Lament | d2intro |
| 2 | Desert Journey | Act02start |
| 3 | Mephisto's Jungle | Act03start |
| 4 | Enter Hell | Act04start |
| 5 | Terror's End | Act04end |
| 6 | Search for Baal | D2x_Intro |
| 7 | Destruction's End | D2x_Out |

The first N entries are enabled and the rest disabled (`FUN_004f96f0`).
N comes from the "Aux Battle.net" registry bits:

| bit | N |
|---|---|
| 0x01 (LoD ending seen) | 7 |
| 0x80 (LoD intro) | 6 |
| 0x10 | 5 |
| 0x08 | 4 |
| 0x40 | 3 |
| 0x04 | 2 |
| otherwise | 1 |

d2d applies the same chain to `cinematics_seen`. Choosing an entry
plays it and returns to the menu.
