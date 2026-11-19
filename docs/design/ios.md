# iOS development shell

This is an unsupported development prototype for iPhone and iPad. It adds
an iOS build target, SDK library selection, a native game-data folder picker,
character-name keyboard handling, and platform settings/save-and-exit wiring.
Existing upstream touch controls are used unchanged.

## Build

Use Xcode with an arm64 simulator SDK and an iOS 16.3 deployment target.
The deployment floor follows the SDK availability of the C++ formatting
facilities used by d2d.

```sh
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64
xcodebuild -project build-ios/D2Decomp.xcodeproj -scheme d2d \
  -configuration Release -sdk iphonesimulator CODE_SIGNING_ALLOWED=NO
```

Device signing is opt-in with `D2D_IOS_DEVELOPMENT_TEAM`. Use `iphoneos`
as the sysroot and your own development team for a physical device.

## Dependencies and local data

The iOS target fetches static SDL 3.4.16, uses the SDK OpenAL framework when
available, and falls back to openal-soft 1.25.2 otherwise. StormLib uses its
existing source build; zlib and bzip2 come from the selected SDK. FFmpeg,
Sentry and the Qt launcher are omitted. Build downloads and SDK lookup via
`xcrun` require maintainer security review.

Supply your own Diablo II install and 1.14d patch. No game files are bundled.
When the configured data path is missing, the app adopts an existing
`Documents/gamedata` folder or opens the native folder picker. Replacement
imports are staged and validated before replacing the old directory, with
rollback on failure. A failed startup load offers the picker again.

The name keyboard closes on Enter or a tap outside the name field. Save and
Exit saves the game and settings before requesting termination. Backgrounding
does not create a new save.

## Validation limits

Earlier simulator/device observations belong to the larger prototype and
are not validation of this reduced branch. Recheck configure/build, folder
picker cancellation/replacement, startup recovery, name entry, Save and Exit,
and relaunch on the intended platform. Screenshots are omitted at the
contributor's request.

The existing queued background-event handling still needs an immediate
lifecycle callback design and device validation. This prototype is not ready
for supported-platform status or store distribution.
