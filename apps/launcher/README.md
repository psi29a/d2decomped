# d2d-launcher

A small window that finds (or installs) your Diablo II files, saves the
settings, and starts the game.

## Run it

```sh
open build/apps/launcher/d2d-launcher.app      # macOS
build/apps/launcher/d2d-launcher               # Linux / Windows
```

Pick your Diablo II install from the list, then press **Launch**.

## Why it exists

- d2d needs your own game files. The launcher finds them, so you don't
  have to type paths.
- No install? It copies the game files from the 4 disc images (.iso)
  into a folder of your choosing.
- It's the one place to set fullscreen and crash reports.

## More uses

| You want | Do |
|---|---|
| Install from discs | **Install Diablo II** → **Add ISO files...** → pick a destination |
| Use a non-1.14d install | **Fix: use LODPatch_114d.exe...**, or **Fetch patch...** to download it |
| Start fullscreen | tick **Fullscreen** |
| Play it from Steam (Linux, Steam Deck) | exit Steam, then **Add to Steam**: adds or updates the shortcut and its library art |
| Send crash reports | tick the crash-reports box (off by default) |
| Check where things went | the settings are in `d2d.cfg`: `data =`, `patch =`, `fullscreen =` (folder per OS: [d2d's README](../d2d/README.md)) |

## When it breaks

| You see | Fix |
|---|---|
| "No Diablo II install found" | **Search more places...**, or **Add folder...** at the folder with `d2data.mpq`, or install from the ISOs |
| "Can't use this install" | Usually an old version: use **Fix: use LODPatch_114d.exe...** |
| "Download failed" | Download LODPatch_114d.exe yourself, then use the Fix button |
| Missing or stuttering sounds, "MPQ read failed" in the log | **Verify files...**: reads every file of every MPQ and names the damaged ones. Copy those MPQs again. When everything fails, the cause is the StormLib it names, not your files |
| "d2d not found beside the launcher." | Build d2d too (`cmake --build build`); the two must sit side by side |

Your install is read in place and never changed.
