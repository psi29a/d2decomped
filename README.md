# D2Decomp

Cross-platform, GPL-3.0 re-implementation of Diablo II + LoD, driven by a
Ghidra decompilation of the 1.14d binaries.

**Not affiliated with Blizzard.** You bring your own ISOs.

See [`docs/PLAN.md`](docs/PLAN.md) for the roadmap.

## Build (once code lands)

```
cmake -S . -B build -G Ninja
cmake --build build
```

Requires a C++26 compiler (Clang ≥ 18, GCC ≥ 14, MSVC 17.10+).

## Status

Phase 1 — repo skeleton. Nothing to run yet.
