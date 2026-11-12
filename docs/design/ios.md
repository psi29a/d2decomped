# iPhone and iPad prototype

Status: development prototype notes from one Mac, 2026-11-11. This is not a
support claim. iOS and iPadOS are not supported. A store submission was not
attempted.

The iPhone 17 simulator reached the main menu, created a character, and
entered the game. The document picker is shown when `d2d.cfg` has no `data`
line, or that line does not point at a folder containing `d2data.mpq`. A
folder copied into the app container's `Documents/gamedata`, with `data =`
set to that folder, skips the picker. No Diablo II files are in the repo or
the app bundle. The folder used for this run had no `patch_d2.mpq` and no
`LODPatch_114d.exe`, so the log said it was using CD-era tables and strings.

## Toolchain

| Item | Observed |
|---|---|
| Xcode | 27.2 (build 27B52) |
| SDK | iphonesimulator 27.2 |
| Architectures | arm64 |
| Deployment target | 16.3 |
| C++23 | The Release simulator build compiled. No `std::expected` error was printed. |

SDL 3.4.16's iOS notes say it deploys to iOS 11.0. This Xcode rejected 11.0:
"The iOS deployment target 'IPHONEOS_DEPLOYMENT_TARGET' is set to 11.0, but
the range of supported deployment target versions is 15.0 to 27.2.x."
At 15.0 the simulator SDK then failed in `<format>` with
"`to_chars` is unavailable: introduced in iOS 16.3 simulator." 16.3 is the
floor that compiled. It is not lower than SDL's documented target.

Configure and iPhone build:

```sh
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
xcodebuild -project build-ios/D2Decomp.xcodeproj -scheme d2d -configuration Release \
  -destination 'platform=iOS Simulator,name=iPhone 17' -sdk iphonesimulator \
  CODE_SIGNING_ALLOWED=NO
```

The iPad build used the same project and
`-destination 'platform=iOS Simulator,name=iPad (A16)'`. Both `xcodebuild`
runs printed `BUILD SUCCEEDED`. One `d2d.app` was produced
(`build-ios/apps/d2d/Release-iphonesimulator/d2d.app`).

## Dependencies

| Dependency | This prototype |
|---|---|
| SDL3 | Used. Static build of tag `release-3.4.16`, fetched during configure. |
| OpenAL | Used. iOS SDK `OpenAL.framework`. openal-soft was not fetched. |
| StormLib | Used. Existing source build, tag v9.30. |
| zlib | Used. Simulator SDK `libz.tbd` (and `libbz2.tbd` for StormLib). |
| FFmpeg / Bink | Deliberately absent. Not linked. |
| Sentry | Deliberately absent. `D2D_NO_SENTRY` on this target only. |
| Qt launcher | Deliberately absent. The iOS configure does not add the launcher. |

The SDL fetch is a new download. It needs a maintainer `security-reviewed`
label before a pull request. openal-soft was not added.

## Simulator

Launched on iPhone 17 (iOS 27.2 simulator, UDID
`30FB820C-E77A-4525-81FD-9101890AFC5D`). With `data =` pointed at a copied
folder, `d2d.log` recorded `Menus loaded` and `Scene loaded`. A blank
experience-table cell used to abort that load (`stoi: no conversion`) and
the first save then crashed. Blank cells are now skipped, and the save
checks the monster list length. After that change, creating a character and
pressing OK entered the game. The person running the simulator confirmed
both that OK no longer crashes and that the keyboard can be dismissed.

The iPad (A16) simulator used the same app. It was booted, the same kind of
copied folder was written into its container, and the process stayed up
(`UIKitApplication:io.github.psi29a.d2d`, pid 64200). `d2d.log` recorded
`Menus loaded in 202 ms` and `Scene loaded in 6482 ms`. A screenshot showed
the main menu letterboxed in landscape, with black bars above and below the
800x600 picture. The person running it then created a character and pressed
OK. Stage Manager, an external display, and a pointer were not tried.

The name keyboard, on the iPhone, opens on the character screen. Enter, or a
tap outside the name field, closes it and leaves it closed. Tapping the name
field opens it again. While the Mac keyboard is connected, the on-screen
keyboard does not have to appear. Typing on the Mac keyboard did reach the
name field in the logged iPhone run.

| Check | iPhone 17 simulator | iPad (A16) simulator |
|---|---|---|
| Build | pass | pass |
| Launch | pass, process stayed up | pass, process stayed up |
| Document picker | shown when `data` is missing or stale | not tested (data was already copied) |
| Menu | reached | reached, letterboxed |
| New character | reached the game after OK | a character was created and OK was pressed |
| Save | the OK path completed and the game opened | not separately checked |
| Quit | not tested | not tested |
| Relaunch | not tested | not tested |
| Gameplay past entering the game | not tested | not tested |

Device rows, including a plugged-in iPhone or iPad, stay not tested.

| Check | Result |
|---|---|
| Safe area inset on the touch controls | not tested |
| Stage Manager resizing | not tested |
| External display | not tested |
| Pointer hover | not tested |
| Leaving mid-game creates a new autosave | not tested. The background path does not write a save. |
| Bink cinematics | absent. The log says they are not in this build. |

## Distribution

Development signing, TestFlight, and the App Store are different channels.
This run used `CODE_SIGNING_ALLOWED=NO` and did not sign, upload, or submit.
The recommendation is human review required. Do not call iOS or iPadOS
supported. The per-platform gate in `platform-support.md` was not run on
hardware.

On this Mac, the iPhone 17 simulator is a go for a development prototype:
it can load a user-supplied folder, show the menu, create a character, and
enter the game. The iPad (A16) simulator shows that same menu, and a
character was created there and OK was pressed. Neither is a go for
"supported". No device was tested. Distribution is still human review
required.
