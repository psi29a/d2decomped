# apps/

End-user binaries.

- `launcher/` — one Qt6 app. It finds or installs D2 (from the 4 CD ISOs),
  writes `d2d.cfg` and launches d2d. Target: `d2d-launcher`.
- `d2d/` — the game itself. Target: `d2d`.
- `d2proxy/` — a dev tool: sits between a client and a game.exe host and
  logs every packet both ways, decoded. Target: `d2proxy` (not in the
  Flatpak).
- `whisper/` — a dev tool: a remote control loaded into the original
  game.exe (an `ijl11.dll` stand-in), to read its state and drive its
  player for live checks. Built on its own (`build.sh`), not by CMake.

The launcher has an opt-in setting for automatically sending crash reports
through Sentry. It is off by default. When enabled, Sentry Native and its
Crashpad handler collect and send minidumps with CPU model, architecture,
logical CPU count, memory, OS, GPU model when reported by the driver, graphics
backend, recent warning and error breadcrumbs, optional notes entered in the
launcher, and the latest 800x600 game framebuffer copied about once per second.
The frame is encoded and attached asynchronously. The image is of the game
framebuffer only, not the desktop, and may show names or other visible in-game
information. Log breadcrumbs are sent only with crash events. Notes are not
saved by the launcher.
The handler is packaged beside `d2d`; the SDK's public DSN is embedded in the
launcher. Minidumps may contain sensitive data from the game's memory.
Release builds use `RelWithDebInfo`. Tag builds upload native symbols to Sentry
when the repository Actions secret `SENTRY_AUTH_TOKEN` is configured; without
it, reports can arrive without symbolicated stack traces.
To intentionally test the Sentry path, use the gated `crash-test` devctl verb
documented in [the control-channel guide](../docs/control_channel.md). It
deliberately terminates the running game.

`cmake --build build --target package` makes the shipping package: the
launcher with d2d beside it and every library both need (macOS: one
`D2D.app` in a `.dmg`, d2d inside it; Linux: `.tar.gz`; Windows: `.zip`).
The dev tools (`d2proxy`, `mpq-cat`, `drlg-dump`) sit beside d2d
(macOS: in `D2D.app/Contents/MacOS`).

Each app but `whisper` is its own CMake target under `apps/<name>/CMakeLists.txt`.
