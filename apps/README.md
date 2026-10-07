# apps/

End-user binaries.

- `launcher/` — one Qt6 app. It finds or installs D2 (from the 4 CD ISOs),
  writes `d2d.cfg` and launches d2d. Target: `d2d-launcher`.
- `d2d/` — the game itself. Target: `d2d`.

`cmake --build build --target package` makes the shipping package: the
launcher with d2d beside it and every library both need (macOS: one
`.app` in a `.dmg`, d2d inside it; Linux: `.tar.gz`; Windows: `.zip`).

Each app is its own CMake target under `apps/<name>/CMakeLists.txt`.
