# d2d for Android

d2d built natively for Android's arm64 by the NDK, as `d2d.apk`. It is d2d
alone (`-DD2D_GAME_ONLY=ON`, as in `docs/d2vita.md`), with FFmpeg's Bink
decoder for the cinematics. This is the build proof that
[#44](https://github.com/psi29a/d2decomped/issues/44) asks for: it runs, and
it is not a supported platform yet.

| Build | Files | Made by |
|---|---|---|
| Android | `d2d.apk` (arm64-v8a, Android 8 up, debug-signed) | `tools/d2vita.sh android` |

Locally: `make android`, with the Android SDK in `$ANDROID_HOME` (default
`~/Library/Android/sdk`) holding an `ndk/`, a `build-tools/` and a
`platforms/android-35` or later (what SDL 3.4.16's Java is built against),
and a JDK. No CI job builds it yet.

## How it is put together

- SDL's own Java activity (`org.libsdl.app.SDLActivity`, from SDL's source)
  loads `libSDL3.so`, then the game as `libmain.so`: on Android `d2d` is a
  shared library, and its `main` is SDL's `SDL_main`.
- SDL3 3.4.16 and OpenAL Soft 1.25.2 (static, on OpenSL ES) are built from
  source, pinned by SHA-256, as for the Switch.
- No Gradle: the script calls the SDK's `aapt2`, `d8`, `zipalign` and
  `apksigner`, and `javac`. The key is a debug key made in the build folder.
- The libraries are aligned for 4 KB and 16 KB pages.

## Where its files go

The app's own folder, `Android/data/io.github.psi29a.d2decomped/files/` on
the shared storage, which needs no permission. The one permission the app
has is `INTERNET`, for TCP/IP games. `d2d.cfg`, `d2d.log` and
`save/` go there, and so do the MPQs from your own Diablo II until there is
a first-run screen to pick them:

```sh
adb install build/d2vita/android/d2d.apk
adb shell am start -n io.github.psi29a.d2decomped/org.libsdl.app.SDLActivity   # once: makes the folder
adb push /path/to/diablo2/*.mpq /sdcard/Android/data/io.github.psi29a.d2decomped/files/
```

A `patch =` line in `d2d.cfg` there names a pushed `LODPatch_114d.exe`, as on
a PC.

## What was checked

On the SDK's emulator (Pixel 7, Android 14, arm64, 4 KB pages), 2026-11-14:
the intro cinematics play, touch works as the mouse through the menus, the
software keyboard types a character's name, a new character walks in the
Rogue Encampment at 60 frames a second, and its save is there after the app
is stopped.

## What's missing

- A first-run screen that picks the game's folder (the launcher's job on a
  PC). Without the MPQs the app closes at once; the reason is only in
  `d2d.log`.
- The game draws at 800x600 with black bars, under the status bar.
- Touch is a left mouse button: no right click, no virtual controls.
- Pause and resume, rotation, audio focus, joining a TCP/IP game and a
  16 KB device are untested.
  So is every real phone and tablet.
- No crash reports (as every `D2D_GAME_ONLY` build).
