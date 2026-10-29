# tests/

One `test_<subject>.cpp` per subject, each an independent executable and
one ctest entry (see `d2d_add_test` in `CMakeLists.txt`). No fixtures, no
framework — a `main()` that returns 0 on success, checking with plain
`assert` (CMakeLists.txt keeps NDEBUG off for this directory, so asserts
stay on in Release builds); nothing fancier is warranted yet.

Tests that need real assets (D2 ISOs, extracted MPQs, game.exe) locate
them at runtime; when they're absent the test prints a `SKIP:` line and
returns `D2D_TEST_SKIPPED` (77, defined by `d2d_add_test`; the .py tests
`sys.exit(77)`).
ctest lists those as skipped, not passed, so a bare checkout stays green
and its summary shows which data-backed tests didn't run. A test that
skips only part of its checks (test_d2s's real saves, test_mpq's
installer) prints a `SKIP` line for that part and still passes. The one
exception is test_game: it runs its few data-free checks, then returns
`D2D_TEST_SKIPPED` without d2data.mpq, since the rest of it runs on the game's
data; `ctest -V` shows its "the checks above ran" line.

Run: `ctest --test-dir build --output-on-failure`.

`test_overlays` needs no assets: its 288 frame cases were captured from
stock 1.14d's `FUN_00470c30` by `tools/emu/overlays.py`. They cover
ordinary and state loops, cast endings, tick boundaries, a stopped
animation and a rate faster than one loop per tick. The test also checks
independent alert starts, restarts, new-level resets and clock wrap.

Fast act checks (need the MPQs and `D2_PATCH_INSTALLER`): the quests of
Acts 1–5 played headless, an act per test, side by side:
`ctest --test-dir build -R quests_act -j3`. Each act's levels against
game.exe (~15 s for 2 seeds): `ACT=2 tools/emu/sweep.sh 1-2 tiles units monsters`.
