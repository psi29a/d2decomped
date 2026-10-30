# d2d

The game: Diablo II + Lord of Destruction, rebuilt from game.exe (1.14d),
running on your own game files.

## Run it

```sh
build/apps/d2d/d2d --data ~/Workspace/private/diablo2
```

`--data` is the folder holding your MPQs (`d2data.mpq` and friends).
Or use [the launcher](../launcher/README.md), which finds them for you.

## Why it exists

- It's the point of the project: d2d replaces game.exe and plays the
  same, on macOS, Linux and Windows.
- It also joins games hosted by the original game.exe.

## More uses

| You want | Command |
|---|---|
| Join an original game.exe host (your network only) | `d2d --join 192.168.50.68` |
| Skip the intro movies | `--no-video` |
| Bigger window | `--scale 2` |
| Upscale filter (default `pixelart`; also in the launcher) | `--toggle scalemode=nearest` (or `linear`) |
| Straight into a new character | `--start-screen charcreate --start-class 1 --start-name Test` (class 0..6: Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin) |
| Don't touch your saves | `--no-save` |
| Turn a d2d extra off | `--toggle autoloot=off,trans_roof=off,autoparty=off` |
| A fixed frame rate (default: the display's refresh) | `--fps-cap 60` (or `fps_cap = 60` in `d2d.cfg`) |
| More log detail | `--log-level debug` |
| Drive it from a script, no window | `--headless --devctl /tmp/d2d.sock` (verbs: [docs/control_channel.md](../../docs/control_channel.md)) |
| Every switch | `d2d --help` |

In game, Alt+Enter switches window / borderless fullscreen.

## Gamepad

Any pad SDL knows (Xbox, PlayStation, Switch, Steam Deck) plays with one
default layout, by button position. The first pad connected plays. Its
buttons are shown by the names printed on that pad.

| Pad | In game | In the menus |
|---|---|---|
| Left stick | Walk (run follows the run toggle) | Move the cursor |
| Right stick | Aim: turn to the thing to hit or use, without walking; with a panel open, move the cursor. Click: the skill picker (with a panel open: recentre). In the picker the d-pad hops, A on a skill button switches hands, A on a skill chooses it | Move the cursor |
| A (south) / right trigger | Hit or use what's highlighted: a monster, an item, an NPC, an object or waypoint, a town portal, your corpse ("A  Talk" over the panel says what) | Left click |
| B (east) | Right-hand skill; in a store or dialog: back | Back (Esc) |
| Left trigger | Swap weapons (W) | |
| Y (north) / X (west) | Inventory / character; in a store X buys or sells what the cursor is on | |
| Left / right shoulder | Skill tree / automap; in a store the tab before / after | The screen's button before / after |
| View (back) / Menu (start) | Quest log / game menu (Esc) | — / Enter |
| D-pad | Belt slots 1–4; with a panel open the cursor hops item to item | Up, down, left, right |
| Left stick click | Walk / run toggle | |

Change it in game: Esc, Options, Configure Controls, then Gamepad. It
keeps every button, left-handed sticks (the right stick walks),
swapped confirm and back (a Nintendo pad's A and B), the dead zone and
the cursor speed, as `pad_*` lines in `d2d.cfg`. The same screen sets
the keyboard's keys, as game.exe does (kept beside the saves as
`<name>.key` and `default.key`).

Settings live in `d2d.cfg`: `~/Library/Preferences/d2d` (macOS),
`~/.config/d2d` (Linux), `Documents\My Games\d2d` (Windows). The log,
`d2d.log`, is in the same folder.

## When it breaks

| You see | Fix |
|---|---|
| Can't find the game data | Pass `--data`, or run the launcher once; it writes `data =` into `d2d.cfg` |
| A wrong `--data` stops it with an error | It doesn't fall back: fix or drop the path |
| Join fails: game.exe tables | `--join` needs your own 1.14d game.exe: `--game-exe`, or `D2_GAME_EXE` |
| Join drops after ~20 s of silence | The host's PC slept or quit; d2d returns to character select on purpose |
| Anything else | Run with `--log-level debug` and read `d2d.log` |

Where d2d differs from game.exe on purpose: `docs/research/re/deviations.md`.
