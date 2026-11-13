# Platform support — feasibility and order

Status: planning note, not a support promise. Written 2026-10-17. Platform
availability, store rules, toolchain support, and console access must be
verified with current official sources before a platform is announced or a
distribution is submitted.

## Purpose

Assess the requested targets and identify the shared engineering work and
gates. Treat "builds" and "supported" as different claims: a successful
cross-compile is not enough. A platform is supported only after installation,
user-data setup, input, rendering, save lifecycle, and release/update paths are
tested on representative hardware.

## Current baseline

- The game simulation in `components/game` is designed to run without SDL or
  client rendering. The end-user game client in `apps/d2d` uses SDL3; the
  desktop launcher in `apps/launcher` uses Qt 6.
- The renderer currently presents an 800×600 logical framebuffer. The display
  mode and high-DPI behavior are not equivalent to a general responsive,
  widescreen UI. See [rendering.md](./rendering.md).
- The project requires C++23; MSVC gets it via `/std:c++latest`.
  Toolchain and language-library support must be checked per target rather
  than assumed.
- Current CI covers Linux/macOS/Windows desktop builds and Linux Flatpak
  bundles for x86_64 and aarch64. These jobs do not by themselves certify
  native Windows ARM64, Android, iOS, iPadOS, Switch, or Switch 2 support.
- Native dependencies include SDL3, OpenAL, StormLib, zlib, and FFmpeg for
  Bink video. Cross-compiling and packaging each dependency is part of target
  feasibility, not an afterthought.
- The current first-run launcher and path assumptions are desktop-oriented.
  Mobile and console targets need an approved, user-driven way to select or
  import the user's own game data; they must not assume unrestricted filesystem
  scanning.
- Controls are currently mouse/keyboard-oriented. The
  [controller plan](./controller-plan.md) defines SDL3 named actions, the
  Deck-first path, Android touch work, and a gated Switch feasibility step.

## Cross-platform principles

1. Keep simulation and game rules independent of SDL, Qt, Steamworks, Android
   Java, UIKit, and console SDK types.
2. Reuse the SDL3 client and the shared action layer where the platform
   officially supports the required SDL3 APIs. Platform-specific window,
   lifecycle, touch, file selection, and packaging belong in adapters.
3. Preserve bring-your-own-game-data. Never bundle Diablo II ISOs, MPQs,
   saves, executables, or other Blizzard assets. Confirm that the target's
   distribution rules permit the proposed user-data workflow before committing
   to a store release.
4. Make data selection explicit. Use platform document/file pickers where
   available, and copy or retain access to user-selected data only through
   supported mechanisms. Do not require broad filesystem permissions or
   weakening platform protections.
5. Keep saves and configuration in platform-appropriate user storage. Define
   migration and backup behavior before moving existing desktop saves.
6. Reuse named actions for gamepad and touch. Do not fork gameplay behavior
   per platform. Preserve desktop mouse/keyboard as a first-class path.
7. Test lifecycle and real hardware: focus, suspend/resume, orientation,
   hotplug, external displays, audio, and exit/relaunch are correctness
   requirements, not polish.
8. Separate technical feasibility, distribution approval, and ongoing
   maintenance. Do not claim a platform supported until a maintainer can build,
   test, publish, and update it.

## Target assessment

### Windows on ARM64

**Assessment: best near-term native-platform experiment.** The existing
Windows desktop path is a useful base, but the repository's Windows build is
not proof of a native ARM64 build. Windows x64 emulation may be a useful
temporary way to run the app, but it is not native ARM64 support.

Investigate a native ARM64 CMake/toolchain and CI runner, then verify ARM64
builds of SDL3, Qt, OpenAL, StormLib, zlib, and the Bink-only FFmpeg path.
Check vcpkg triplet and Qt availability, packaging and signing, installer
behavior, and whether any dependency or handwritten platform code makes
assumptions about x86/x64. Keep the x64 artifact and existing Windows
behavior unchanged.

Do not advertise Windows ARM64 until a native build launches and completes
data import, gameplay, audio, save/reload, and release-package tests on ARM64
hardware or an appropriate validated runner. Record separately whether the
claim is native ARM64, x64 emulation, or both.

**Status (2026-11-13, #42):** CI's `Windows ARM64 (MSVC)` leg builds d2d and
the launcher natively on GitHub's `windows-11-arm` runner, runs the tests
there, packages `d2d-<version>-Windows-ARM64.zip`, and fails unless every
`.exe` and `.dll` in it is ARM64 (dumpbin machine `AA64`). The x64 leg, its
package name and its dependencies are unchanged.

| Dependency | ARM64 source |
|---|---|
| MSVC, CMake, Ninja, vcpkg | the runner image (VS 2022 17.14) |
| Qt 6.8 | `install-qt-action`, host `windows_arm64`, `win64_msvc2022_arm64` |
| SDL3 3.4.16 | libsdl-org's VC zip, `lib/arm64` |
| FFmpeg (Bink only) | BtbN's pinned `winarm64-lgpl-shared` build |
| OpenAL Soft | vcpkg `arm64-windows` (kcat's bin zip has no ARM64) |
| zlib, StormLib | vcpkg `arm64-windows` |
| Sentry Native (crashpad) | FetchContent, 0.17.3 |

The code needed no change: the one x86 intrinsic (`__cpuid` in
`apps/d2d/main.cpp`, the Sentry CPU name) is already guarded, so ARM64
sends no CPU name. Not verified yet: a native run on ARM64 hardware (launch,
data import, gameplay, audio, save/reload) and x64 emulation of the x64
package. Until both are done, neither is a supported claim.

### Android phones and tablets

**Assessment: plausible after SDL3 input, resolution, and data-access
foundations.** Treat phones and tablets as one Android codebase, but validate
both form factors. A minimal SDL3 Android shell is already an A1 task in
`controller-plan.md`; A2 covers a package and touch adapter.

The feasibility spike must verify the current NDK/Gradle/SDL3 path and the
actual C++ standard supported by all dependencies. Audit StormLib, zlib,
OpenAL, FFmpeg and file I/O on Android; determine whether Bink video is
available or must be explicitly disabled on a documented platform basis.
Replace the Qt desktop launcher with an Android-appropriate first-run flow.
Use the system document picker or a deliberate app-private import rather than
desktop path scanning; validate MPQ access, storage limits, save paths,
re-import, and permission persistence.

The game must adapt to touch without creating a second rules/input path.
Provide virtual controls for gameplay and touch-friendly UI, while also
supporting physical controllers through the shared SDL action layer. Handle
rotation, cutouts/safe areas, varying aspect ratios and DPI, background/resume,
audio focus, memory pressure, and onscreen text input. Tablets need a distinct
usability pass for large displays, keyboard/mouse, and resizable or
multi-window use where available.

Check current Google Play and alternative-store policies for an app that
requires separately supplied copyrighted game data. A technically viable
APK does not guarantee store acceptance. Do not bundle the original game or
claim a supported distribution channel before this review and a human-owned
release path.

### iPhone and iPad

**Assessment: possible but higher-risk than Android because the Apple build
and distribution path is a separate project.** Treat iPhone and iPad as one
shared iOS/iPadOS codebase where practical, with separate device-class
acceptance checks. Do not assume that Android work automatically ports.

The feasibility spike must verify SDL3 and all native dependencies under the
current Apple SDK/Xcode toolchain, arm64 deployment targets, signing,
entitlements, packaging, and the project's C++ standard. Determine whether
the Qt launcher can be replaced with a native or SDL-owned setup flow; an
iOS-compatible first-run path must use supported document/file APIs and
app-container storage. Test MPQ import, saves, Bink video, audio-session
behavior, app suspension/resumption, controller support, and touch controls.

iPhone checks include safe areas, rotation, on-screen controls, text input,
background/foreground transitions, and performance on a stated minimum
device. iPad checks include larger layouts, external displays, pointer and
keyboard input, multitasking/window resizing where supported, and controller
use. Keep the source data user-supplied; review current App Store rules and
legal/product implications before deciding whether App Store distribution is
viable. TestFlight, direct development builds, and App Store distribution are
distinct channels with distinct requirements; do not conflate them.

### Nintendo Switch and Switch 2

**Assessment: gated feasibility only. Do not start platform implementation
without authorized access.** The existing controller plan already notes that
the SDL Switch port and development path are gated by Nintendo program access
and NDA. Verify the current status independently for each console generation;
do not infer Switch 2 SDK, SDL, compatibility, or certification details from
Switch support.

The first step is a human-owned feasibility decision: confirm developer
program access, applicable NDAs and devkits, permitted SDL3 availability,
toolchain/language-standard support, and rules for distributing an engine
that reads separately supplied game data. Then assess the Qt launcher
replacement, asset import/storage, memory/performance, save lifecycle,
controller glyphs and player assignment, suspend/resume, display modes, and
certification/release obligations. Use only authorized documentation and
hardware.

Until those gates are cleared, deliver a written go/no-go assessment only.
Do not label either console supported, publish a porting branch based on
unverified information, or ask contributors to obtain or share restricted
SDK materials.

## Suggested order

1. **Windows ARM64:** a bounded native build/CI/package validation; desktop
   code and user-data flow already exist.
2. **Shared mobile foundations:** prove a portable build/dependency path and
   define user-selected game-data import, save/config storage, lifecycle, and
   the non-Qt setup path.
3. **Android phone/tablet prototype:** build and install a test package, then
   validate touch, controller, data import, saves, lifecycle, and both form
   factors. Decide distribution after current policy review.
4. **iPhone/iPad feasibility and prototype:** independently prove the Apple
   toolchain, dependencies, data flow, signing and distribution options; reuse
   shared actions and data abstractions only where the platform APIs genuinely
   align.
5. **Switch/Switch 2 feasibility gate:** proceed only after the relevant
   authorized program access and current technical/distribution facts are
   confirmed for each target.

Controller support and flexible resolution work are cross-platform
prerequisites, but do not block a small toolchain/data-access feasibility
spike. See issues #33, #35, #36, #38 and #39 in the repository issue tracker.

## Per-platform support gate

Before marking any target supported, record and pass:

- A reproducible build and a maintained dependency/toolchain matrix.
- Installation, first-run data selection/import, and clear error recovery.
- Save creation, reload, backup/quit behavior, and migration where applicable.
- Full playable path through the content currently claimed as supported.
- Input-only play for the intended default input, plus optional input
  combinations stated by the platform plan.
- Rendering at representative display sizes, DPI, orientation and external
  display configurations.
- Suspend/background, resume/foreground, hotplug and clean exit/relaunch.
- A release artifact, signing/store requirements, update path, support
  ownership, and verified user-facing instructions.
- A legal review of the bring-your-own-game-data and distribution approach;
  no game data or Blizzard binaries in project packages.

## Tracking issues

Platform work is split into reviewable tickets and labeled `platform`:

- [#42 — Native Windows ARM64 builds](https://github.com/psi29a/d2decomped/issues/42)
- [#44 — Shared mobile build, data-import, and storage foundations](https://github.com/psi29a/d2decomped/issues/44)
- [#41 — Android phones and tablets](https://github.com/psi29a/d2decomped/issues/41)
- [#43 — iPhone and iPad](https://github.com/psi29a/d2decomped/issues/43)
- [#45 — Nintendo Switch and Switch 2 feasibility gate](https://github.com/psi29a/d2decomped/issues/45)
