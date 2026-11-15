# d2d for Android

d2d built natively for Android's arm64 by the NDK, as `d2d.apk`: the Qt
launcher, as on a PC, and d2d (`-DD2D_GAME_ONLY=ON`, as in `docs/d2vita.md`),
with FFmpeg's Bink decoder for the cinematics. This is the build proof that
[#44](https://github.com/psi29a/d2decomped/issues/44) asks for: it runs, and
it is not a supported platform yet.

| Build | Files | Made by |
|---|---|---|
| Android | `d2d.apk` (arm64-v8a, Android 9 up, debug-signed) | `tools/porter.sh android` |

Locally: `make android`, with the Android SDK in `$ANDROID_HOME` (default
`~/Library/Android/sdk`) holding an `ndk/` 27 or later (16 KB pages), a
`build-tools/` 35 or later (its `zipalign` aligns for them) and a
`platforms/android-35` or later (what SDL 3.4.16's Java is built against),
a JDK, and Qt 6.12.0 twice: for Android in `$QT_ANDROID` (default
`~/Qt/6.12.0/android_arm64_v8a`) and for this machine in `$QT_HOST_PATH`
(default Homebrew's `qt`), the two at the same version:

```sh
uvx --from aqtinstall aqt install-qt all_os android 6.12.0 android_arm64_v8a -O ~/Qt
```

The script stops at once on an older NDK or build-tools.
`$ANDROID_NDK_HOME`, when set, names the NDK to use.

CI builds it on every code change: the `Android` job, as the `android`
artifact. A release has it as `d2d-<version>-android.apk`.

Android installs a build over another only when one key signed both.
CI's builds of `main` and of a tag are signed with the release key, the
repository's `ANDROID_KEYSTORE_B64` (a PKCS12 file, base64) and
`ANDROID_KEYSTORE_PASSWORD` secrets; `tools/porter.sh` takes the file's path
in `$ANDROID_KEYSTORE` and the password in `$ANDROID_KEYSTORE_PASSWORD`.
Without them (a local build, a pull request's) it is a debug key of that
build's own. Going from one key to another means uninstalling first, which
deletes the app's folder with the MPQs and the saves in it (copy `save/` off
with `adb pull` before).

## How it is put together

- The app opens on the launcher: Qt's `QtActivity` loads
  `libd2d-launcher_arm64-v8a.so` and the Qt libraries listed in
  `res/values/libs.xml` (Core, Gui, Widgets, Network, the Android platform
  and style plugins). Its Launch starts SDL's activity by an explicit intent.
- SDL's own Java activity (`org.libsdl.app.SDLActivity`, from SDL's source),
  in a process of its own (`:game`), loads `libSDL3.so`, then the game as
  `libmain.so`: on Android `d2d` is a shared library, and its `main` is SDL's
  `SDL_main`. SDL calls it on a Java thread with a 1 MB stack, and d2d's
  frames want more, so it runs itself again on a thread of 16 MB.
- Qt 6.12 for Android is prebuilt, from Qt's own mirrors; its Android floor
  (API 28) is the APK's.
- SDL3 3.4.16 and OpenAL Soft 1.25.2 (static, on OpenSL ES) are built from
  source, pinned by SHA-256, as for the Switch.
- No Gradle: the script calls the SDK's `aapt2`, `d8`, `zipalign` and
  `apksigner`, and `javac`; Qt's `androiddeployqt --aux-mode` only gathers the
  launcher's libraries and writes `libs.xml`. The key is a debug key made in
  the build folder.
- The libraries are aligned for 4 KB and 16 KB pages.

## Where its files go

The app's own folder, `Android/data/io.github.psi29a.d2decomped/files/` on
the shared storage, which needs no permission. The one permission the app
has is `INTERNET`, for TCP/IP games. `d2d.cfg`, `d2d.log` and
`save/` go there, and so do the MPQs from your own Diablo II until there is
the launcher's "Copy game files from a folder..." on the phone, or adb:

```sh
adb install build/ports/android/d2d.apk
adb shell am start -n io.github.psi29a.d2decomped/org.qtproject.qt.android.bindings.QtActivity   # once: makes the folder
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

On the same emulator, 2026-11-15: the launcher opens, takes taps, finds
the MPQs in the app's folder, and its Launch starts d2d, which plays the
intro and reaches the main menu. The launcher's layout fits landscape;
portrait is wider than the screen.

## What's missing

- The game draws at 800x600 with black bars, under the status bar.
- Touch is a left mouse button: no right click, no virtual controls.
- Pause and resume, rotation, audio focus, joining a TCP/IP game and a
  16 KB device are untested.
  So is every real phone and tablet.
- No crash reports (as every `D2D_GAME_ONLY` build).
