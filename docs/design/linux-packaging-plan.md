# Linux packaging — Flatpak first, AppImage optional

Status: Flatpak CMake packaging mode implemented; a local ARM64 Flatpak bundle
builds in Docker on macOS. GitHub Actions is configured to build x86_64 and
aarch64 bundles on native runners. Install/run testing and an x86_64 package
build remain pending. AppImage remains a proposal.

## Recommendation

Make Flatpak the primary supported Linux distribution format and submit to
Flathub only when d2d has a stable release and a maintainer can own the
package. A Flatpak uses a shared runtime, so host distributions do not need to
match the build machine's glibc or carry d2d's Qt/SDL dependency bundle. It
also gives users a normal install/update path through app stores such as
Discover. Build against common Flatpak desktop standards and test both KDE and
GNOME hosts; the KDE runtime can be used by applications on non-KDE desktops.

Keep the existing `.tar.gz` as a developer preview until Flatpak is mature.
Add an AppImage later only if users need a single-file download outside
Flathub and someone can own a second Linux build, compatibility, and update
channel. AppImage has real value for direct downloads and does not impose a
sandbox, but it does not provide a central app-store updater and still has
host ABI and graphics-driver constraints. It is optional for Steam Deck; it
does not remove the Desktop Mode / Steam shortcut step.

No Linux package may contain Diablo II ISOs, MPQs, saves from a real user, or
Blizzard executables/assets. The player supplies game data, and d2d imports or
reads that data under the existing project rules.

## Current repository constraints

- The user-facing entry point is `d2d-launcher`; `d2d` sits beside it and is
  launched by the UI. The Linux archive currently stages both, Qt libraries,
  and development tools.
- The launcher can import files from the four original CD ISOs and can use a
  selected existing install. Its current broad install detection assumes
  desktop filesystem visibility, which is not available inside a restrictive
  Flatpak sandbox.
- The shared Linux `userdir` helper now honors `XDG_CONFIG_HOME` (the default
  remains `~/.config`). The current config and save layout still shares that
  directory; a data-directory migration and a safe path for existing Linux
  saves remain open.
- The launcher links Qt 6.5 Widgets and Network. The game links SDL3,
  OpenAL Soft, zlib, StormLib, CLI11, and a small static Bink-only FFmpeg.
- Top-level CMake downloads CLI11 with `FetchContent`; the MPQ component may
  download StormLib with `FetchContent`; the FFmpeg `ExternalProject` fetches
  its source during configure/build. Flathub builds cannot use network during
  the build. Those sources need to be declared as checksummed manifest inputs
  before CMake runs, and CMake must consume local source trees without trying
  to fetch them again.
- The generic Linux CMake package uses Qt's deploy step and ships tools. A
  Flatpak build should use the runtime's Qt, install only the launcher/game
  and desktop metadata, and must not copy host Qt libraries into the sandbox.
- The current Linux artifact relies on host glibc, graphics, and sound
  libraries. A Flatpak runtime addresses much of that host ABI spread; an
  AppImage must instead be built against a deliberately old compatibility
  baseline and tested on target systems.
- `D2D_FLATPAK=ON` now selects the runtime-native install rules, defaults tools
  and tests off, omits `d2proxy`, and skips Qt runtime deployment. It installs
  the launcher, adjacent engine, desktop entry, AppStream metadata, icon, and
  license.
- Flatpak mode requires local CLI11, StormLib, and FFmpeg source directories;
  CMake fails early if they are missing instead of fetching during the
  network-disabled build. StormLib's development install rules are disabled.
- The local candidate `org.kde.Platform` / `org.kde.Sdk` 6.11 build completed
  with GCC 15.2 in C++26 mode. That runtime supplied Qt 6.11.2, SDL3 3.2.30,
  and OpenAL 1.24.3, so no extra modules were needed for this candidate.
- A disposable build recipe and sources are kept under ignored `build/` paths.
  The provisional local ID is `io.github.psi29a.d2decomped`; it still needs
  maintainer confirmation before a Flathub package is authored.

Relevant sources: [app targets](../../apps/CMakeLists.txt),
[launcher dependencies](../../apps/launcher/CMakeLists.txt),
[game dependencies](../../apps/d2d/CMakeLists.txt),
[CLI11 FetchContent](../../CMakeLists.txt), and
[StormLib build choice](../../components/mpq/CMakeLists.txt).

## Flatpak application design

### Runtime, ID, and build shape

- Continue with the locally built `org.kde.Platform` / `org.kde.Sdk` 6.11
  candidate because it supplies Qt, SDL3, and OpenAL. The KDE runtime is a
  runtime dependency, not a requirement that the user's desktop be KDE.
  Verify the branch is still supported when the human-owned manifest is
  authored, and schedule an upgrade before it reaches end of life.
- Choose the permanent Flatpak application ID only after confirming control
  of the matching domain or upstream code-host namespace. Use the same ID in
  the desktop file, AppStream MetaInfo, icon paths, and Flathub repository.
  Avoid implying Blizzard affiliation in the app name, icon, or description.
- Package the launcher as the visible application command. Install `d2d`
  beside it or in a stable libexec location and make the launcher's engine
  path independent of the working directory. Do not ship `d2proxy`, test
  binaries, or developer-only tools in the end-user Flatpak.
- Keep upstream-owned desktop integration upstream: desktop entry, app icon,
  AppStream MetaInfo, GPL license installation, and CMake install rules. The
  Flathub manifest belongs in the Flathub app repository and is maintained
  separately from release binaries.
- Build `x86_64` first for Steam Deck and desktop Linux. Keep `aarch64` as a
  target if all native dependencies and CI can build and run it; Flathub
  builds both architectures by default unless the app declares otherwise.
  Do not advertise an architecture until it passes the same functional gate.

### Offline, reproducible dependency build

The package build must be hermetic after source acquisition. A human package
maintainer should author a manifest that pins each source to an immutable
release/commit and verifies an archive checksum. The manifest downloads
sources before entering the network-disabled build; CMake configure and build
must not perform network access.

Completed build-system work:

1. CMake accepts pre-fetched CLI11 and StormLib through
   `FETCHCONTENT_SOURCE_DIR_CLI11` and `FETCHCONTENT_SOURCE_DIR_STORMLIB`.
2. `D2D_FFMPEG_SOURCE_DIR` supplies a prepared FFmpeg tree; CMake builds the
   static libraries out of tree without running its download or install
   steps.
3. The C++23 project builds with warnings-as-errors and optimized release
   settings in the KDE 6.11 SDK.
4. Flatpak install rules omit Qt deployment and developer tools, and include
   the launcher, game binary, desktop/AppStream metadata, icon, and license.

Remaining work: the human-authored manifest must pin dependency sources and
checksums; validate the app ID; preserve/migrate existing saves; check the
runtime's x86_64 build; and run the installed app through portals, audio, and
controller input on Steam Deck and a desktop session.

If the CMake changes are useful beyond Flatpak, use them to make the normal
Linux package reproducible too. Do not maintain a fork with packaging-only
patches where an upstream build option can do the job.

## Local prototype and GitHub Actions

The local ARM64 build currently produces
`build/flatpak/d2decomped-aarch64.flatpak`; it has not yet been installed or
launched. The CI workflow uses a pinned-source recipe at
`.github/flatpak/ci-manifest.yml` to build both x86_64 and aarch64 bundles. That
recipe is AI-assisted CI material, not a Flathub manifest. A human maintainer
must author and own a separate Flathub manifest, using the then-current runtime
and policy requirements.

The disposable recipe under ignored `build/flatpak/` is also AI-assisted and
exists only to exercise the local build. Do not submit either recipe to
Flathub. On macOS, keep Flatpak Builder's state and repo in a Docker-managed
volume rather than the Mac bind mount. The nested build sandbox corrupted
generated FFmpeg files when its build tree lived on that bind mount. Copy only
the completed bundle back to the workspace.

1. The maintainer creates the local Flatpak build recipe and a local source
   entry that points to the working tree. Keep this recipe out of Flathub
   submission until the human-authored manifest passes the policy check.
2. Build with `flatpak-builder` or Flathub's `org.flatpak.Builder` SDK
   environment, install to a user-local Flatpak repo, and run it with
   `flatpak run`. Resolve offline dependencies, runtime, display/audio/input
   permissions, portal access, launcher-to-game path, save/config location,
   and game-data import before tuning GitHub Actions.
3. `.github/workflows/ci.yml` now has a dedicated Flatpak job using
   [`flatpak/flatpak-github-actions/flatpak-builder@v6`](https://github.com/flatpak/flatpak-github-actions)
   and `.github/flatpak/ci-manifest.yml`. It builds both architectures on
   native GitHub-hosted x86_64 and ARM64 runners and uploads each bundle as a
   CI artifact on pull requests, main, and tags. The workflow archives the
   tracked checkout into a clean source tree so local build products are not
   copied into the Flatpak builder. Flathub publication stays separate.
4. The local ARM64 bundle is built. The x86_64 manifest reached the build
   step under Docker emulation, but this Mac's container kernel cannot create
   bubblewrap namespaces, so a complete emulated build is unavailable. Verify
   x86_64 and both native CI jobs after this branch is pushed and a pull
   request is opened. Do not advertise an architecture until its build and
   functional gates pass.
5. Test each CI artifact on Deck and one GNOME/KDE desktop. A maintainer should
   author a Flathub-compliant manifest separately. Normal Flathub updates are
   manifest pull requests and are built/tested by Flathub; a project workflow
   should not try to upload to Flathub directly. Use `flat-manager` only if
   the project separately runs its own Flatpak repository.

### Rough effort estimate

For one experienced C++/Linux maintainer, assuming x86_64 first and no major
launcher redesign:

| Work | Estimate | Main uncertainty |
|---|---:|---|
| Finish local Flatpak install/run flow | 1–3 engineering days remaining | Portal access, save migration, first-run game-data import, and target-system validation |
| GitHub Actions build/cache/artifact job | Configured; allow 0.5–1 day for first-run fixes and artifact checks | Runtime and runner compatibility, cache effectiveness, and artifact smoke test |
| Data portal, controller, Deck and GNOME/KDE validation | 1–3 additional days | Persisted directory grants, actual Deck input permissions, and any first-run UI fixes |
| Human-owned Flathub submission and maintenance setup | 0.5–2 days of maintainer work, plus review wait | Stable release readiness, metadata/legal cleanup, ownership and volunteer review queue |

The first offline configure, C++26 compile, and bundle export completed in the
local ARM64 builder. The remaining estimate is for install/run behavior and
data handling. The GitHub Action itself is the small part; getting d2d's
build/install/data paths into a clean Flatpak sandbox is where most effort
lies.

### Permissions and host integration

Start from the minimum manifest permissions and add one only when a tested
feature needs it. Flatpak portals provide a way to request specific files
without granting the sandbox broad access to the user's home directory.

| Capability | Initial approach | Gate / caution |
|---|---|---|
| Window and graphics | Wayland plus X11 fallback; `dri` device only if required by SDL's renderer | Test GNOME/KDE sessions, X11 and Wayland, game mode, high DPI, suspend/resume. Use host graphics drivers through Flatpak's graphics stack. |
| Audio | PulseAudio-compatible socket for OpenAL/SDL audio | Test PipeWire-backed desktops, mute/resume, and device changes. Do not grant host filesystem or system-bus access for sound. |
| Game data import | Qt `QFileDialog` through the file-chooser portal; allow choosing an install folder or ISO files | Qt portal support requires using the standard dialog; do not opt out with `DontUseNativeDialog`. Verify directory selection and repeated access on each target desktop. |
| Data storage and saves | XDG config/data directories inside the Flatpak app's private per-user storage | Do not hard-code `~/.config`, `~/Documents`, or a SteamOS path. Show the configured data/save location and provide a documented backup/export route. |
| Existing game install | Ask the user to select the folder; retain a portal grant if supported, otherwise offer an explicit import/copy into app storage | Never add `--filesystem=home` or `host` merely to preserve automatic scanning. Test MPQs on internal storage, SD card, removable media, and network mounts where supported. |
| Controller | SDL3 gamepad API; first test Steam Input's virtual controller and directly attached pads | Add `--device=input` only if measured controller access requires it and Flathub accepts the permission. Do not request `--device=all` as a shortcut. Record behavior on older Flatpak versions before using conditional permissions. |
| Multiplayer / downloads | Network permission because TCP/IP play and optional patch/update retrieval need networking | Explain the reason in the manifest review and test joining an original-host LAN game, DNS, and launcher downloads. No inbound server permission is needed for client-only play unless later functionality changes. |
| Desktop integration | Export the launcher desktop entry, app icon, MIME/URL handlers only if actually used | Validate visibility in GNOME/KDE app launchers and Steam's non-Steam picker. Gaming Mode still needs a one-time Steam library shortcut. |

Do not add `--filesystem=home`, `host`, broad D-Bus access, or all-device
access to make a failed test pass. Prefer portals or fix the application. If a
required portal or input feature is unavailable on a target system, document
that limitation and decide whether to raise the minimum Flatpak version.

### Data, save, and first-launch flow

The user-facing journey should be:

1. Install d2d from Discover/Flathub and launch the exported app.
2. The first-run screen explains that users need their own classic Diablo II
   plus Lord of Destruction data and that d2d contains no game files.
3. Let the user select an existing data folder or import from their own media
   using file-picker portal access. Explain expected files and unsupported
   D2R data before copying. Never scan the whole home directory.
4. Store config, imported patch data, and saves in app-private XDG locations;
   do not place game data in the Flatpak install tree, which can be replaced
   during updates. Keep the game-data path explicit and recoverable.
   The Linux `userdir` now honors `XDG_CONFIG_HOME`; add a safe migration or
   import path for existing characters/configuration, and decide whether saves
   should move to `XDG_DATA_HOME`.
5. Launch the game with the controller-ready layout on Deck; provide a
   keyboard/mouse path for desktop setup. On the first Deck use, explain how
   to add the installed app to Steam from Desktop Mode for Gaming Mode.

This path must work without shell commands, disabling SteamOS protections,
or granting a blanket home-directory permission. Verify it with selected
folders as well as ISO import; the portal may expose removable and remote
files differently from ordinary host paths.

## Flathub release and maintenance gates

Flathub is a maintained upstream distribution, not a place to upload the
existing `.tar.gz`. The app must build from source, declare the correct
license, use minimum permissions, include valid desktop and AppStream
metadata, and be maintained by the upstream project. Stable Flathub is for
stable software; a new app cannot start in Flathub beta. The project should
wait until it can publish tagged, usable releases and support runtime/dependency
updates.

Flathub's current policy prohibits AI-generated or AI-assisted manifest
content and AI-agent submission/review activity. This document is planning
material, not a manifest. A project maintainer must write and review any
actual manifest, own the submission, and re-check the policy before packaging;
do not delegate manifest authoring or Flathub PR interaction to an AI agent.
The policy also requires disclosure of AI-generated code or documentation
included in the application or its packaging. Before submission, the maintainer
must check which source/docs/build inputs are included and make any required
disclosure. Do not claim this planning document is Flathub-ready material.

Submission checklist for a human maintainer:

- Confirm the d2d license and all packaged dependency licenses; install
  license texts. Do not bundle proprietary Diablo II content or binaries.
- Confirm upstream control of the chosen app ID namespace and use a distinct
  icon/name that does not imply Blizzard endorsement.
- Integrate a valid desktop file, AppStream MetaInfo, and a suitable icon
  upstream; validate the metadata before submission.
- Submit the first release only when stable and usable, provide screenshots
  and a user-data setup note, then maintain dependency source pins, runtime
  updates, architecture builds, and security fixes.
- Human maintainers install Flathub test builds and verify each update before
  merging it. Keep release notes and a support contact current.

## AppImage decision

AppImage is worth considering as an **optional direct-download artifact** once
the Linux release process is stable. It gives a user one file to download,
mark executable, and run without a Flatpak remote or app-store install. It
also avoids Flatpak's sandbox restrictions, so existing-install detection
and user-selected paths are simpler. It still needs a desktop entry or a
Steam shortcut for Gaming Mode, and updates are a separate project problem.

It is not the primary answer for Deck or “runs anywhere” compatibility:

- AppImages still depend on host glibc and graphics/driver interfaces. The
  AppImage project recommends building on the oldest target Linux baseline;
  this repository currently builds Linux packages on a very new Ubuntu runner.
- Type 2 AppImages use FUSE to run in place. Some systems lack a working FUSE
  setup; extraction is a fallback but adds steps. Verify Deck behavior rather
  than promising that SteamOS has FUSE configured.
- AppImage updates need a published update location and a maintained path,
  such as a user's manual replacement or AppImageUpdate metadata/tooling.
  Existing AppImage update support does not appear automatically just because
  the artifact is one file.
- AppImage is not sandboxed. The app must continue to use explicit user
  selection for Diablo II data and avoid unexpected directory scans.

If demand supports AppImage, reuse the Flatpak-independent CMake install
staging where practical, then produce separate signed/checksummed artifacts
per supported architecture. Build from a documented older baseline, bundle
Qt/SDL/OpenAL and project-owned dependencies, leave glibc/GPU drivers to the
host, test AppImage FUSE and extraction behavior on Steam Deck, and provide a
clear update mechanism. If no maintainer can support that matrix, keep the
existing archive and focus Linux packaging effort on Flatpak.

## Agent-sized work packages

These are research and implementation tickets; each should deliver its
findings, changed files, commands run, and remaining risks.

### P0 — Packaging readiness and policy audit

Inventory the build graph and all third-party licenses, identify current
source-fetch behavior and required install files, and assess Flathub stable
readiness. Return a dependency/module table, source pins/checksum strategy,
and app ID namespace requirements. Research only: do not author a Flatpak
manifest or contact Flathub on behalf of the project.

### P1 — Offline Flatpak build support

Implement CMake options and install rules so the app can build only from
locally provided sources in a Flatpak SDK, without network access during
configure/build, without deploying Qt libraries, and without developer tools.
Keep normal Linux/Windows/macOS packaging behavior intact. The actual Flathub
manifest remains human-maintainer authored under current policy.

### P2 — Portal-based game-data setup

Verify the Qt folder/file chooser uses XDG Desktop Portal on KDE, GNOME, and
SteamOS. Make import from an existing install and original media robust with
portal-provided access; persist only supported grants or copy user-selected
content into app-private storage. Keep `d2d.cfg`, save paths, and import
diagnostics inside the Flatpak data model, and never require blanket `home`
access.

### P3 — Runtime, permissions, and architecture matrix

Select a currently supported KDE or Freedesktop runtime after a prototype.
Audit display, GPU, sound, network, file chooser, controller, and suspend
permissions on Wayland/X11 and Steam Deck. Build x86_64 and investigate
aarch64. Document any minimum Flatpak version. Use only the permissions proven
necessary by the audit.

### P4 — Human-owned Flathub release

Once P0–P3 and the stable-release gate pass, the project maintainer creates
the manifest/repository and submits it under current Flathub policy. Maintain
test-build installs, tagged source updates, runtime upgrades, and user-facing
release notes. This ticket is not delegated to an AI agent.

### P5 — AppImage proof of value

After Flatpak is maintainable, compare actual user demand with the extra
release and support burden. If proceeding, prove the artifact on old and new
distros plus Steam Deck, verify FUSE and extraction fallback, define update
and checksum/signature policy, and share only the common CMake install
staging—not Flatpak-only sandbox logic.

## Acceptance gates

### Flatpak / Flathub

- A clean Flatpak build uses only pinned, checksummed local/module sources
  during the network-disabled build and is reproducible from a tagged source
  release.
- Install and launch work through Discover/Flathub on SteamOS and representative
  GNOME and KDE Flatpak hosts; no host package install or read-only bypass is
  needed.
- A new user can select their own game-data folder or ISO through the portal,
  finish setup, relaunch without losing access, and find config/saves in the
  documented app data path.
- Wayland, X11 fallback, graphics, audio, controller hotplug, LAN joining,
  update downloads, and graceful exit are verified with no unnecessary broad
  permissions.
- Release installs only project code, required runtime dependencies, and
  metadata; no game assets or Blizzard binaries are present.
- Both x86_64 and aarch64 pass before both are advertised. If only x86_64 is
  supported, architecture metadata says so accurately.
- A maintainer accepts ownership of the Flathub repository, stable updates,
  runtime EOL migration, dependency CVEs, and support requests.

### AppImage, if selected

- The executable runs from a clean download on every claimed distro baseline;
  FUSE failure and extract-and-run behavior are documented and tested.
- Update instructions are clear and checksums/signatures are published for
  every architecture.
- It adds a real user benefit beyond a second way to download the same build,
  and has an assigned maintainer.

## References

- [Flatpak runtimes](https://docs.flatpak.org/en/latest/available-runtimes.html)
- [Flatpak Qt guidance](https://docs.flatpak.org/en/latest/qt.html)
- [Flatpak portals](https://docs.flatpak.org/en/latest/portals.html)
- [Flatpak sandbox concepts](https://docs.flatpak.org/en/latest/basic-concepts.html)
- [Flatpak source modules](https://docs.flatpak.org/en/latest/module-sources.html)
- [Flathub requirements](https://docs.flathub.org/docs/for-app-authors/requirements)
- [Flathub submission](https://docs.flathub.org/docs/for-app-authors/submission)
- [Flathub maintenance](https://docs.flathub.org/docs/for-app-authors/maintenance)
- [AppImage concepts and compatibility](https://docs.appimage.org/introduction/concepts.html)
- [AppImage compatibility best practices](https://docs.appimage.org/reference/best-practices.html)
- [AppImage updates](https://docs.appimage.org/packaging-guide/optional/updates.html)
- [AppImage FUSE troubleshooting](https://docs.appimage.org/user-guide/troubleshooting/fuse.html)
