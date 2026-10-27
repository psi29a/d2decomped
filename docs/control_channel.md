# Dev control channel

Local-development only. Speaks over an AF_UNIX stream socket at a path
chosen by the host binary. Line-based text protocol, single client at a
time, non-blocking pump driven from the game's main loop.

**Never enable in shipped builds.** There is no auth, and any local
process that can `connect()` the socket can drive input, dump state, and
in principle write files (via `screenshot`).

## Transport

- Framing: LF-terminated (`\n`) lines, one command per line.
- Encoding: ASCII/UTF-8; whitespace-separated tokens.
- Reply: LF-terminated. Success ends with `ok\n` (either as the sole reply
  or after any payload lines). Failure is `err <message>\n`.
- Second concurrent client is rejected with `err busy\n` and dropped.
- A line longer than 64 KiB drops the client with `err line too long\n`.

## Built-in verbs

| verb            | reply             | notes                                                  |
|-----------------|-------------------|--------------------------------------------------------|
| `ping`          | `ok`              | Liveness probe.                                        |
| *unknown verb*  | `err unknown verb '<name>'` | Handler not registered.                     |

Everything else is registered by the host binary via `Channel::on()`.

## `apps/d2d/` verbs (current)

| verb                        | reply                  | notes                                                     |
|-----------------------------|------------------------|-----------------------------------------------------------|
| `screenshot <path>`         | `ok <bytes>`           | Writes current framebuffer to `<path>` as PNG. Relative paths land in `<user dir>/screenshots/` (e.g. `~/Library/Preferences/d2d/screenshots/`). |
| `info`                      | `w=… h=… frame=…\nok`  | Framebuffer dimensions + current frame counter.           |
| `quit`                      | `ok`                   | Main loop exits after this pump completes.                |
| `click <x> <y>`             | `ok`                   | Pushes real SDL motion + left down/up at window coords; handled next frame. |
| `rclick <x> <y>`            | `ok`                   | Same with the right button (opens the Horadric Cube item). |
| `pad [n] axis <stick> <v>` | `ok`                   | Virtual pad n (0..3, default 0): a stick axis (`leftx`, `lefty`, `rightx`, `righty`) at -1..1. |
| `pad <n> plug [deck]` / `pad <n> unplug` | `ok`     | Plug virtual pad n in (`deck`: with the Steam Deck's own controls' ids, 0x28de / 0x1205) or pull it out (co-op seats). |
| `debug players`            | `<id>\t<name>\t<x>\t<y>\t<seat>\t<pmode>\t<ghost>` per player, `ok` | The game's players (local co-op): cell, co-op seat (-1: the first player), mode (-1 none), a ghost (1). |
| `debug roster`             | `<seat>\t<stage>\t<pick>\t<entries,>\t<name>` per open roster, `ok` | Each joining pad's roster: stage (0 character, 1 class, 2 name), the line picked, entries (`*` greyed), the name typed. |
| `pad [n] <button> down\|up` | `ok`                   | Holds a button of virtual pad n (0..3, default 0; plugged in at its first use) down or lets it go (`a`, `b`, `x`, `y`, `dpleft`, `leftshoulder`, `lefttrigger`, ...: SDL's names; a trigger goes all the way); plugged in on first use. Press = down, a frame, up. |
| `save`                      | `ok` / `err <why>`     | Save the character now (character_store.hpp), as leaving the game does. |
| `cmd <what> ...`            | `ok`                   | A command straight to the World, applied at its next tick (apps/d2d/protocol.hpp): `move <x> <y>`, `skill <id> <x> <y> [unit] [left]`, `interact <npc>`, `pickup <unit>`, `resurrect`, `stat <stat> [n]`, `skillpt <index>`, `select <skill> <left>`, `belt <slot> [merc]` (merc: the merc drinks it, shift held). What a remote client would send. |
| `npcs`                      | `<name>\t<x>\t<y>\t<menu 0/1>\t<index>\t<cell x>\t<cell y>` per named NPC/object, then `ok` | Feet on screen in game pixels; menu = has an NPC menu; index for `cmd interact`. |
| `monsters`                  | `<id>\t<x>\t<y>\t<sx>\t<sy>\t<hp>/<max>\t<mode>\t<boss>\t<mods>\tlvl<n>\t<name>\t#<unit>\t<row>` per monster of the current outdoor level, then `ok` | Cells, feet on screen (while there), life, animation mode; champion / unique / superunique / minion (or -), MonUMod ids, level, name, unit id (what `cmd skill` takes), the skill sequence row drawn (`<mode>:<frame>`, `-` none). |
| `ground`                    | `<code>\t<label>\t<sx>\t<sy>\t#<unit>` per item on the level's ground, then `ok` | Loot: gold is `gld`; feet on screen; unit id (what `cmd pickup` takes). |
| `menu`                      | `<text>\t<x>\t<y>` per line of the open NPC menu, then `ok` | A point inside each line, header first. |
| `debug collision`           | `ok on` / `ok off`     | Toggle the InGame overlay: red dot on every blocked subtile. |
| `debug automap`             | `ok <cells>`           | Reveal the whole level on the automap. |
| `debug statpts <n>` / `debug skillpts <n>` | `ok`    | Set the in-game character's unspent stat / skill points. |
| `debug equip <code> <slot>` | `ok quantity=<n>`      | A normal `<code>` (rolled as a drop) worn in body slot `<slot>`, replacing what's there (shot / ammo tests). |
| `debug wear`                | `ok`                   | Halve the durability of everything worn (repair tests). |
| `debug unid`                | `ok <count>`           | Unidentify every carried item (Cain tests). |
| `debug clearinv`            | `ok`                   | Empty the inventory grid (tests that need room, whatever the save carries). |
| `debug stat <id> <v>`       | `ok`                   | Set character stat 0..15 (12 level, 14 gold, 15 stash gold, ...). |
| `debug attack`              | `ok dmg=<min>-<max> ar=… def=… block=… dr=…%+… mdr=… res=f/l/c/p cb=… ds=… ow=… ll=… ml=… ias=… wsm=… frw=… fhr=… fbr=… thorns=…+…l cold=… fire=… light=… crit=… def_melee=… def_missile=… dodge=… avoid=… evade=…` | The player as combat sees them (components/rules Fighter). |
| `debug die [id]`            | `ok`                   | The player (or player `id`, local co-op) dies where they stand (death penalty, then a corpse; in co-op a ghost). |
| `debug corpses`             | `<level>\t<x>\t<y>\t<items>` per corpse, `ok` | The player's corpses. |
| `debug rain [state]`       | `ok state= density= drops= splashes= wind=` | The weather; with a state (0 clear, 1 rising, 2 full, 3 falling) it starts that state next tick. |
| `debug walls <x> <y>`      | `ok w<type>...`        | The current level's wall tiles at cell (x, y): each wall layer's orientation (h: hidden), or layer:orientation of the tiles game.exe picked. |
| `debug tiles [server]`      | `level <id>`, then `<x> <y> wall\|floor\|shadow o<orientation> <dt1>:<index>` per picked tile, `ok` | The current level's tiles as drawn (the client's order, `Level::drawn`), or with `server` collision's (`Level::picks`), DS1 cells: diff against a live game.exe's (`whisper.py … town`, room-relative there). |
| `debug laid`                | `up <n>`, then `<list index> <x> <y>` per room, `ok` | The order the current level's rooms were laid in (tiles depend on it: edge sharing); the first n were up. |
| `debug relevel <x>,<y> ...` | `ok <rooms>`           | Lay the current level's server tiles again with these rooms (DS1 cells of their corners) up in this order, the rest after; `debug tiles server` shows the result. |
| `debug alt on\|off`         | `ok`                   | Hold "Show Items" (Alt): every ground item's label. |
| `debug day [deg]`          | `ok phase= time= intensity=` | The time of day, set to `deg` degrees (0 sunrise, 90 noon, 180 dusk, 270 midnight). |
| `debug light [more]`       | a row of levels (`0`..`v`, 0..31) per subtile; with `more`, a `light <x> <y> r8 <radius x 8> shadowed\|plain` line per light (eighths of a subtile) and a `#`/`.` row per subtile walls block; then `ok <origin x> <origin y> bonus=<n>` | The light grid as the frame builds it (the player's light where they stand), to diff against game.exe's (0x7b0e68, 48 × 48, intensity at +4; `--toggle light_grid=off` for its size). |
| `debug difficulty <d>`      | `ok`                   | Play on difficulty d (0 normal, 1 nightmare, 2 hell): a new game on d, as a real one starts: the levels built again for d (LvlMaze Rooms(N) / (H)), its monsters, entered in that difficulty's act town. A level-1 character dies at once past Normal; probe with a sturdy save. |
| `debug skill left\|right <id>` | `ok` / `err not usable` | Put Skills.txt skill id on that button (the picker's rules). |
| `debug points <id> <n>`    | `ok` / `err not a class skill` | Give the character's class skill `id` n points (for testing a skill a save lacks). |
| `debug passives`          | `ok <stat>=<value>[/<itype>] ...` | The passive skills' stats on the character (FUN_00646d60), with the weapon type a mastery needs. |
| `debug charges <id> <n>`  | `ok`                   | Hold n (1..3) charges of charge-up skill id (release tests). |
| `debug release`           | `ok <missiles>` / `err no monster` | Release the held charges on the nearest live monster (FUN_005d5220). |
| `debug quest <q>`           | `ok`                   | Mark quest q done on the active difficulty (Act 1: 1 Den of Evil .. 6 Andariel), then run its join and the chain from quest 1 as a new game would. |
| `debug quest <q> show\|reset` | `ok bits=0x.. den=N skillpts=N states=<den>,<burial>,<tools>,<cain>,<tower>,<andy>` | Quest q's 16 flag bits and every Act 1 quest record's state; `reset` clears the bits and restarts the game's Den of Evil. |
| `debug boss <mod>...`       | `ok #<id> <name> aura=<skill> lvl=<n>` | The nearest plain monster becomes a unique with those MonUMod ids (7 cursed, 26 teleport, 30 aura ...). |
| `debug kill [n] [spare <id>]` | `ok` / `err spare <MonStats id>` | Kill the level's monsters but n and those of MonStats id `<id>` (`spare diablo`), as kills (experience, drops, quest deaths; for quest tests). |
| `debug critters`            | `<row>\t<x>\t<y>\t<walking>\t<marked>` per critter, `ok` | The client's critters (critters.md) in the rooms up so far: MonStats row, cell, walking 0/1, marked 0/1 (a bird that's flown, a rat resting at a corpse). |
| `debug level`               | `ok <id> <x> <y> <w> <h> <wx> <wy>` | The player's level (1 town, 2 Blood Moor), position, level size and its act-tile origin. |
| `debug blocked <x> <y>`     | `ok 0\|1`              | Whether a unit can't stand at (x, y) in the player's level (past its edge: the neighbour's collision). |
| `debug grid [n]`           | a `.` / `#` row per row, `ok` | The level at n spots a cell (1..5, default 1; 5 is subtiles): `#` where a unit can't stand at the spot's middle (`debug blocked` for the whole level at once; route planning in probes). |
| `debug warp <x> <y>`        | `ok`                   | Put the player at DS1 cell (x, y); past the edge next to another level, the next frame crosses into it. |
| `debug presets`            | `<i>\t<object id>\t<x>\t<y>\t<hidden>\t<name>` per object / NPC the level placed, `ok` | Hidden (quest-gated) ones too; `i` for `cmd interact`. |
| `debug itemat <id>`        | `ok <x> <y>` / `err ...` | Carried item `id`'s box middle on screen, the inventory panel open (a click there picks it). |
| `debug goto <level>`        | `ok <x> <y>` / `err no such level` | Arrive on Levels.txt id `level` at a free cell by its middle (quest tests). |
| `debug warps`               | `<i>\t<x>\t<y>\t<to>` per warp, `ok` | The level's warps (cave mouths, stairs): cell and the Levels.txt Id they lead to. |
| `debug objects`             | `<i>\t<x>\t<y>\t<shrine\|chest>\t<row\|trap>\t<locked\|->\t<mode>` per object, `ok` | The level's shrines (Shrines.txt row) and chests (trap type 0..8, locked), and their mode now. |
| `debug portals`             | `<which>\t<level>\t<x>\t<y>` per open portal, `ok` | The player's portals: 0 where it was cast, 1 its twin in camp (cells). |
| `debug net`                 | `host <x> <y> local <x> <y> apart <d> units <n> trade <state> self <id>`, then `player\t<id>\t<name>\t<x>\t<y>` per other player, `ok` | A joined game: where the host has the player and where d2d does (act subtiles), how far apart, how many host units d2d knows, the trade's state; the other players (cells). |
| `debug chat <text>`        | `ok` | Shows a chat line as if it came from the host ("name: message"), to look at it; no one is told. |
| `debug typing [text]`      | `ok` | Opens the chat edit box holding `text`, to look at it; nothing is sent. |
| `debug netsend <hex…>`     | `ok` | A joined game: sends the bytes to the host as one C->S packet (live checks). |
| `debug tomb`               | `ok <level>` | The true Tal Rasha's Tomb of this map seed (`roll_act2_tombs`). |
| `debug tradestate <n>`      | `ok` | A joined game: sets the trade's state (0 none, 1 asked, 2 asked of us, 3 open, 5 they accepted, 7 we did) to look at its box or window; the host isn't told. |
| `debug operate <i> [n]`     | `ok life=… mana=… boost=<row>` | Operate shrine / chest i (town.hpp operate) without walking to it; `n` plays that Shrines.txt row / chest trap type instead. |
| `debug enter <i>`           | `ok` / `err no such warp` | Stand by warp i as if it was clicked; the next frame takes it (`debug level` shows where). |
| `key <name>`                | `ok`                   | Pushes SDL key down+up by SDL key name (`Left`, `Home`, `Escape`, `Return`). |
| `text <words>`              | `ok`                   | Pushes the words as typed text (SDL text input), e.g. into the chat box. |
| `move <x> <y>`              | `ok`                   | Pushes an SDL mouse motion to game coords (hover without clicking). |
| `wheel <dy>`                | `ok`                   | Pushes an SDL wheel event (+up / -down); handled next frame. |
| `state`                     | `screen=… save=… scroll=… class=… name=… hardcore=… difficulty=<0..2; "?" while char select's difficulty box is up> cam=x.xx,y.yy walking=… dir=… saves=… stash=0/1 cube=0/1 store=<vendor> gold=… statpts=… str=… life=cur/max mana=cur/max stamina=<cur/max, 8.8 fixed> level=… exp=… pmode=<A1/GH/DT/DD or -> missiles=<in flight> pets=<life/max:mode,… or -> lskill=<Skills.txt id> rskill=<id> picker=<0 closed, 1 left, 2 right> charges=<skill:count,… or -> skillpts=… tree=<tab, 0 closed> waypoint=… items=<count> held=<code or -> unid=<unidentified carried> merc=<x,y:code or -> menu=<lines> automap=<cells when open> speech=<lines> voice=<Sounds.txt index> music=<index> music_old=<the song fading out, 0 none> sfx=<world sounds on: Sounds.txt index:owner:gain, p paused; or ->\nok` | Loop state for scripted asserts. |
| `items`                     | per item `[code loc=… slot=… q=… panel=… size=WxH at=col,row]` then its hover-text lines, indented; `ok` | The in-game character's items as the tooltip shows them. |

Future verbs (as the engine grows): `load <act>/<level>`, etc.

## Enabling

Any host that links `devctl` calls `Channel::listen(path)` — an empty path
is a no-op, so a `--devctl <path>` CLI flag or `D2D_DEVCTL` env var gates
it. Absent that, zero cost — nothing binds, no threads, no cleanup.

## Driving from the shell

```
# Start the game with control enabled (add --headless for no window —
# SDL dummy driver + software renderer; tests/smoke_d2d.py drives it):
d2d --devctl /tmp/d2d.sock

# Drive it from another shell:
nc -U /tmp/d2d.sock <<'EOF'
ping
info
screenshot /tmp/frame.png
quit
EOF
```

`nc -U` speaks AF_UNIX. On macOS `nc` is BSD `netcat`; on Linux use
`ncat -U` from nmap-ncat if the packaged `nc` is missing.

## Testing Sentry crash reporting

For an intentional end-to-end crash test, start d2d with crash reporting
enabled, set `SENTRY_DSN` to the project's DSN, and explicitly enable the
crash-test command:

```
SENTRY_DSN='https://<public-key>@<sentry-host>/<project-id>' \
  d2d --send-crash-reports --enable-crash-test --devctl /tmp/d2d-crash-test.sock
```

Crash dumps may contain sensitive in-memory data, and crash reports include
the most recently captured game frame. This test sends a report to the
configured Sentry project; run it only if you accept that upload.

Then, from another terminal, send the exact confirmation:

```
printf 'crash-test CONFIRM_CRASH\n' | nc -U /tmp/d2d-crash-test.sock
```

This deliberately terminates d2d; only use it when losing the current game
session is acceptable. The verb is disabled unless both opt-in flags are
provided, requires successful Sentry initialization, and requires the
confirmation token.
The event is tagged `crash_test=true` so it can be filtered from real crashes.

## Prior art

Same shape as the third-eye control channel
(`/Users/bret.curtis/Workspace/private/eob3/thirdeye/apps/thirdeye/control.cpp`).
The differences are:

- Handler registration is decoupled — devctl doesn't know about game types.
- Reply framing enforced by the pump (auto-appends `\n` if missing).
- No auto-release ticks (mouse-button state is engine-side when we get there).

## POSIX only

Windows path is a warn-once stub. Call sites stay portable — the header
compiles everywhere; `listen()` on Windows just prints a warning and
`pump()` becomes a no-op.
