# d2d for the Nintendo Switch

d2d built natively for the Switch's AArch64 by devkitPro's devkitA64 and
libnx, as `d2d.nro` for the homebrew menu. It is d2d alone
(`-DD2D_GAME_ONLY=ON`, as in `docs/d2vita.md`), with FFmpeg's Bink decoder for
the cinematics.

| Build | Files | Made by |
|---|---|---|
| Switch | `d2d.nro` | `tools/porter.sh switch` (devkitA64) |

CI builds it on every code change: the `Nintendo Switch` job, as the `switch`
artifact. A release has it as a zip. Locally, with Docker: `make switch`.

## Libraries

devkitPro packages SDL2 alone, and its `openal-soft` plays through SDL2,
whose `SDL_` names clash with SDL3's in one binary. So `tools/porter.sh`
builds two of them from source, pinned by SHA-256:

- SDL3 from devkitPro's `switch-sdl-3.4` branch (unreleased): video on
  OpenGL ES through mesa, audio, Joy-Cons, touch, the software keyboard.
- OpenAL Soft 1.25.2, on SDL3's audio.

## Where its files go

Copy `d2d.nro` to `sdmc:/switch/d2d/` and the game's MPQs from your own
Diablo II beside it. `d2d.cfg`, `d2d.log` and `save/` go there too. A
`data =` line in `d2d.cfg` points it at other MPQs.

## Switch 2

There is no separate build. A Switch 2 that runs homebrew runs Switch
homebrew through backward compatibility, `d2d.nro` included. A build for
the Switch 2 itself would need Nintendo's SDK.

## Not there yet

- It hasn't run on a Switch.
- No network: libnx needs `socketInitializeDefault` before sockets, and d2d
  doesn't call it. LAN and TCP/IP join fail.
- devctl is off: no Unix sockets.
- libnx's default icon.
