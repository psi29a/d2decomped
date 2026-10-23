# flatpak

The Linux desktop entry and app info for the Flatpak build (Linux and
Steam Deck). Nothing here runs by itself.

## Run it (install a CI build on Linux / Steam Deck)

1. Download the `d2decomped-x86_64` artifact from a green
   [CI run](https://github.com/psi29a/d2decomped/actions/workflows/ci.yml).
2. Install it:

```sh
unzip -o d2decomped-x86_64.zip
flatpak install --user --bundle ./d2decomped-x86_64.flatpak
```

It starts the launcher. Your own Diablo II + LoD files are still needed.

## Why it exists

- One package that runs on any Linux, including the Steam Deck.
- These two files are templates CMake fills in when `-DD2D_FLATPAK=ON`:

| File | Becomes |
|---|---|
| `desktop.in` | `io.github.psi29a.d2decomped.desktop`: the menu entry |
| `metainfo.xml.in` | `io.github.psi29a.d2decomped.metainfo.xml`: the app info software centres show |

## More uses

| You want | Do |
|---|---|
| Build the Flatpak yourself | `make flatpak` (installs it), `make bundle` (a `.flatpak` file), `make deck` (onto a Deck over ssh); `make` lists them. They build the working tree, uncommitted edits too |
| Change the build recipe | `.github/flatpak/ci-manifest.yml` (CI only, not for Flathub) |
| Play from Steam's Gaming Mode | Exit Steam, then the launcher's **Add to Steam** (shortcut + library art) |
| Update to a newer build | `flatpak install --user --reinstall --bundle ./d2decomped-x86_64.flatpak`: the Steam entry stays |

## When it breaks

| You see | Fix |
|---|---|
| `flatpak install` complains about the runtime | `flatpak install flathub org.kde.Platform//6.11` |
| D2Decomp isn't in Steam after **Add to Steam** | Steam was running and saved over it: exit Steam, press it again. The old list is kept as `shortcuts.vdf.d2d-backup` |
| The launcher can't see your game files | The app gets no folder access by default: pick the folder with **Add folder...** (the system file picker grants it), or `flatpak override --user --filesystem=/path/to/diablo2 io.github.psi29a.d2decomped` |
