# tests/

One `test_<subject>.cpp` per subject, each an independent executable and
one ctest entry; a subject with several (game/, monsters/) gets a
directory. `CMakeLists.txt` picks up every `test_*.cpp` by itself; a
new test names what it links on a `// test-links:` line under its SPDX line
(and `// test-sources:` / `// test-includes:` if it needs them), so adding
one touches no shared file. No fixtures, no
framework — a `main()` that returns 0 on success, checking with plain
`assert` (CMakeLists.txt keeps NDEBUG off for this directory, so asserts
stay on in Release builds); nothing fancier is warranted yet.
`main` is `int main() try { ... } catch (const std::exception& error)`
(tests/.clang-tidy checks it). A `mon_think` test points the `ThinkIn`
scratch with `ThinkScratch` (think_scratch.hpp), or a checker reads a
null write in another AI.

Tests that need real assets (D2 ISOs, extracted MPQs, game.exe) locate
them at runtime; when they're absent the test prints a `SKIP:` line and
returns `D2D_TEST_SKIPPED` (77, defined in `CMakeLists.txt`; the .py tests
`sys.exit(77)`).
ctest lists those as skipped, not passed, so a bare checkout stays green
and its summary shows which data-backed tests didn't run. A test that
skips only part of its checks (test_d2s's real saves, test_mpq's
installer) prints a `SKIP` line for that part and still passes.
test_game and test_game_tables skip without d2data.mpq; the game's checks
that need no data are in test_game_rules, which always runs.

`test_iso9660` generates primary ISO 9660 and Joliet images with a known
payload. It checks the real install disc too when present at the usual
Downloads path. The generated fixtures need no game files.

Run: `ctest --test-dir build --output-on-failure`.

`test_overlays` needs no assets: its 288 frame cases were captured from
stock 1.14d's `FUN_00470c30` by `tools/emu/overlays.py`. They cover
ordinary and state loops, cast endings, tick boundaries, a stopped
animation and a rate faster than one loop per tick. The test also checks
independent alert starts, restarts, new-level resets and clock wrap.

`pad_skill_use_d2d` uses a virtual pad with the skill-button preset: assigning
Inventory from the wheel, using identify and portal tomes, buying with X,
selling with Hold A, and Attack/Use on a portal and waypoint. It shares
`pad_use_d2d.py` with the default-layout check; both need MPQs and the patch.

`pad_controls_ux_d2d` checks the skill-panel entry, remapped destination labels,
both banks, panel cancellation and holding Might without moving. The two
`touch_*_d2d` tests inject SDL finger events for assignment, page changes during
movement, aim/cancel, panel interruption, Teleport and corpse casting.
`test_touch_input` covers finger ownership and gesture invalidation without
assets; `test_direct_controls` covers saved clears, legacy RT fallback, both
LT-chord release orders and item-use availability. None is a hardware test.

Fast act checks (need the MPQs and `D2_PATCH_INSTALLER`): the quests of
Acts 1–5 played headless, an act per test, side by side:
`ctest --test-dir build -R quests_act -j3`. Each act's levels against
game.exe (~15 s for 2 seeds): `ACT=2 tools/emu/sweep.sh 1-2 tiles units monsters`.
Without `D2_PATCH_INSTALLER` the quest checks SKIP, so a green run may not
include them. A quest check that fails on a run left overnight is usually
the machine sleeping: look for a time gap in its `$TMPDIR/d2d_quests_*/d2d.log`
(kept after the run) before blaming the code. A `--devctl` socket path must
be short (AF_UNIX limit), so put it in `/tmp`.

`test_shake` checks screen-shake envelopes and quake-loop instance volumes
against 175 captured stock 1.14d cases, plus 26 control-missile frame cases,
skipped render frames, timer wraparound and offset bounds. Rebuild the
fixtures with `tools/emu/shake.py` and its `--controls` option (see the emu
README). `test_game` checks both effect kinds in the view codec and the
quest effects reaching local co-op participants.
The Act 1 and Act 4 headless quest tests also wait for the real quest
effects to reach the client offsets and OpenAL quake channel, check that a
level change stops the loop, and observe Diablo's delayed arrival taunt.

Controller regressions: `test_pad_stop` exercises an exact-position fresh Move
through World and preserves facing/position; `pad_portal_d2d` loads a saved RT
portal binding and uses a carried tome with Inventory closed. Both use the
existing local 1.14d data gate. `test_direct_controls` covers the modern radial
filter, retaining class kick skills. Native touch hardware remains unverified.

`test_panel_navigation` covers radial pages, centre/boundary stability,
column-first tree movement and shoulder mapping. `pad_controls_ux_d2d`
cycles all six actual panels in both directions with simulated SDL input.

`test_skill_seed` covers deterministic hand/hotkey setup, duplicates, disabled
destinations, round trips and legacy clears. `pad_seed_d2d` uses a temporary
character and actual save/reload to check initialization, excluded skills,
cleared slots, preset toggling, legacy preservation and skill-tree close input.
It also captures the open shortcut strip with controller input active.

`test_retail_keyboard` compares the close rectangle against frozen retail
output for every class/tab and checks 0x51 hotkey packet bytes.
`keyboard_bindings_d2d` uses actual picker clicks and F-keys to check binding
stability, duplicate reassignment, selection and local save/reload. It also
checks conditional New Stats and retail tree close/drag-off behavior.
Retail re-check commands and limits are in skill-tree.md and skills.md.

Controller Identify review regressions: `pad_identify_classic_d2d` and
`pad_identify_d2d` exercise real A pickup of Identify scrolls/tomes, both before
and after unidentified targets are present. Skill Buttons also checks Use to
activate Identify and A to confirm. These require local MPQs and the 1.14d patch;
empty-tome availability is covered by `test_direct_controls`.
