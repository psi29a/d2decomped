# d2d for D2Vita

[D2Vita](https://github.com/Franckrst/D2Vita) runs Diablo II on the PS Vita.
It runs game.exe 1.14d through its own PE loader and an x86-to-ARMv7 dynarec,
out of `ux0:data/d2vita/1.14d/`. d2d comes in two builds for it, both d2d
alone (`-DD2D_GAME_ONLY=ON`: no launcher, d2proxy, Sentry, tools or tests).
FFmpeg's Bink decoder is in both, for the cinematics.

| Build | Files | Made by |
|---|---|---|
| Win32 | `Game.exe`, `SDL3.dll`, `OpenAL32.dll` | `tools/d2vita.sh win32` (MinGW i686) |
| PS Vita | `eboot.bin`, `d2d.vpk` | `tools/d2vita.sh vita` (VitaSDK) |

CI builds both on every code change: the `D2Vita (Win32)` and
`D2Vita (PS Vita)` jobs, as the `d2vita-win32` and `d2vita-vita` artifacts. A
release has them as zips. Locally, with Docker: `make d2vita` and
`make d2vita-win32` (the Makefile has what they fetch).

## Win32: Game.exe

A 32-bit d2d in place of game.exe, with SDL3 and OpenAL Soft beside it.
libstdc++, winpthread and zlib are linked in. It runs on Windows (and Wine).
In D2Vita it doesn't run yet: D2Vita's loader serves game.exe's imports
(`docs-site/shims.md` there) and draws through Glide. SDL3.dll needs Win32 and
graphics imports beyond those. `i686-w64-mingw32-objdump -p` lists what these
three DLLs import. That list is the work left on D2Vita's side.

## PS Vita: d2d.vpk

d2d built natively for the Vita's ARMv7 by VitaSDK: SDL3's Vita backend
(GXM), OpenAL Soft, no dynarec. `eboot.bin` is the unsafe self; `d2d.vpk`
installs it as its own app (title `DDEC00001`, in VitaShell). It asks for the
extended-memory app mode (`ATTRIBUTE2=12`), as D2Vita does.

It uses D2Vita's folder: the MPQs come from `ux0:data/d2vita/1.14d/`, and
`d2d.cfg`, `d2d.log` and `save/` go there too, beside game.exe's `Save`.
A `data =` line in `d2d.cfg` points it at other MPQs.

Not there yet:

- It hasn't run on a Vita. The newlib heap (300 MiB, `apps/d2d/main.cpp`) is
  a guess.
- No network: the Vita needs `sceNetInit` before sockets, and d2d doesn't
  call it. LAN and TCP/IP join fail.
- devctl is off: the Vita has no Unix sockets.
- No LiveArea art.
