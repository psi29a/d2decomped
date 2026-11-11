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
| Draw the hand into the frame, as game.exe (not the OS cursor) | `--toggle hwcursor=off` |
| Attack or cast from a mouse's extra buttons | Configure Controls' first two rows, Attack / Use and Right-hand Skill (`--toggle click_keys=off` hides them) |
| Cap the frame rate (default: the display's refresh; 60 where no refresh is known) | `--fps-cap 60` (or `fps_cap = 60` in `d2d.cfg`) |
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

Skill buttons is a third preset on that page (`pad_skill_buttons`). Fresh
layouts are initialized once per character: the current left-click ability goes
to RT and the right-click ability to RB (or their remapped destinations). Saved
skill hotkeys fill available A/B/X/Y/LB positions, then alternate-bank positions,
in saved order, skipping duplicates and unavailable skills. Existing files and
explicit clears are preserved; switching presets never reseeds them. LT holds the
alternate bank; a short tap swaps weapons, but a skill chord never swaps.

In the skill panel, highlight a learned skill and press the north button
(Y on Xbox) for **Assign to button**. View opens the editor from the field.
D-pad left/right selects Skills or Actions; up/down changes pages. The editor shows
actual destination glyphs, current occupants and bank I/II. Hold LT to preview
bank II. Press a destination to assign; the resulting binding is shown and
the editor stays open. View or Start finishes; held buttons cannot cast on exit.
Inventory is available in Actions and through Start's panel menu.

In this preset B closes panels; RS uses the hovered item. Tome prompts reflect
charges and availability. Identify waits for an unidentified target; B cancels.
Shop stock shows Buy (X by default), and sellable bag items show Hold to sell
(A by default). Labels follow remapped bindings. Attack / Use shows its action
and target: a fresh press can use or pick up, while a held attack stays combat.
Loot under a dead enemy requires a fresh press. Auras and self-buffs do not
chase enemies; the active aura stays labelled across banks.

Assignments remain `<name>.padskills` beside the save. Older files with only
one RT skill use it on LT+RT until assigned separately. Item-granted charged
skills stay on the mouse fan. The original controller preset, mouse fan,
keyboard bindings and `.d2s` hotkeys retain their existing behavior.

For the touch prototype, pass `--touch-controls` (or set `touch_controls = 1`).
It has a movement area, fixed Attack / Use, four assigned skills, a latching
I/II page switch and belt access on both pages. In Skills, tap a learned skill,
then Assign, then a destination; Done returns. Touch assignments use separate
`<name>.touchskills` files. Tap for the previewed target, or drag an aimed skill
and lift to cast. Drag to Cancel, interrupt the finger, or open a panel to
cancel without casting. Movement and skill fingers are independent. Teleport
shows its landing point; corpse skills preview eligible corpses. Tap a usable
tome to use it; tap stock to buy and hold a sellable carried item to sell.

See [design, sources and the playtest script](../../docs/design/controller-touch.md).
This is a landscape input prototype; physical phone/controller testing remains
necessary before claiming device support.

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

Controller follow-up: D-pad character selection hides the pointer until mouse or
analog movement; A confirms the highlighted character. In Inventory, the remapped Use input (RS by default in Skill Buttons mode)
activates an Identify tome/scroll. A or Use confirms an unidentified target;
A otherwise picks up or moves inventory items, including usable tomes. B cancels. Identify items are not controller spell
bindings. A bound Town Portal uses a charged tome carried in inventory directly from the field; opening the Inventory panel is unnecessary. An empty/missing tome or town restriction disables it.

The touch prototype also accepts a single mouse/Pencil-style pointer over its
controls, including drag/release aiming. This does not provide simultaneous
movement and aiming: that requires independent SDL finger events. Sidecar
hardware and OS-specific event delivery remain unverified. Skill and attack
buttons use rounded outlines; the larger rectangular hit regions remain forgiving.

The controller radial excludes generic Kick and Identify scroll/tome entries.
Dragon Talon and other class kick skills remain assignable. Potion labels use
drawn D-pad arrows when mapped to directions, and actual button names when
remapped. Releasing movement stops without planning a new walk to a subtile
centre, preserving facing. Physical touch tests are unavailable; Sidecar is
not a native multitouch validation device.

Controller Skill Buttons navigation: the run/walk control shows its actual
remapped stick-click binding and current Run/Walk state. With Party, Quests,
Character, Inventory, Skills or Options open, the configured previous/next
shoulder inputs cycle that order with wraparound. A tab strip shows the current
panel. Held items, Identify, Imbue and gold confirmation block switching with
a visible explanation. Store tabs and classic controller shortcuts retain
their existing behavior. Offline Party is informational.

The radial uses D-pad Up/Down for pages, Left/Right for Skills/Actions and the
aiming stick for selection. A separate 30% centre deadzone (or the user's
larger configured deadzone) preserves selection at rest; a small angular
margin stabilizes wedge boundaries. Destinations and Done remain unchanged.
Skill-tree Up/Down prefers a node in the same column across gaps before
moving sideways. Tree opening focuses a skill rather than a tab button.
These are controller UX decisions, not newly traced original-game claims.

First-time setup is stored in `<name>.padskills` with `initialized 1`. Existing
files, including old one-RT layouts and deliberately cleared slots, count as
already initialized and are never reseeded. Touch bindings remain separate.
The open panel-shortcut strip stays visible while using a controller. The
skill tree has a clickable close button as well as controller B-to-close.

The untraced permanent Character/Stats shortcut has been removed; the original
HUD art and conditional New Stats notification are retained. Character remains
available through its normal key and mini-panel action. The tree close control
now uses retail class/tab placement, rather than the earlier fixed corner.
