# apps/

End-user binaries.

- `launcher/` — one Qt6 app that installs D2 from the 4 CD ISOs today,
  and will pick profiles / launch the engine once it can run. Target:
  `d2-launcher`.
- `d2/` — the game itself. Ships when phase 5 is real.

Each app is its own CMake target under `apps/<name>/CMakeLists.txt`.
