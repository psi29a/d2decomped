# apps/

End-user binaries.

- `launcher/` — one Qt6 app. It finds or installs D2 (from the 4 CD ISOs),
  writes `d2d.cfg` and launches d2d. Target: `d2d-launcher`.
- `d2d/` — the game itself. Target: `d2d`. Ships when phase 5 is real.

Each app is its own CMake target under `apps/<name>/CMakeLists.txt`.
