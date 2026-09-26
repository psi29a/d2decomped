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
| `npcs`                      | `<name>\t<x>\t<y>\t<menu 0/1>` per named NPC/object, then `ok` | Feet on screen in game pixels; menu = has an NPC menu. |
| `monsters`                  | `<id>\t<x>\t<y>\t<sx>\t<sy>\t<hp>/<max>\t<mode>\t<boss>\t<mods>\tlvl<n>\t<name>` per monster of the current outdoor level, then `ok` | Cells, feet on screen (while there), life, animation mode; champion / unique / superunique / minion (or -), MonUMod ids, level, name. |
| `ground`                    | `<code>\t<label>\t<sx>\t<sy>` per item on the Blood Moor's ground, then `ok` | Loot: gold is `gld`; feet on screen. |
| `menu`                      | `<text>\t<x>\t<y>` per line of the open NPC menu, then `ok` | A point inside each line, header first. |
| `debug collision`           | `ok on` / `ok off`     | Toggle the InGame overlay: red dot on every blocked subtile. |
| `debug automap`             | `ok <cells>`           | Reveal the whole level on the automap. |
| `debug statpts <n>` / `debug skillpts <n>` | `ok`    | Set the in-game character's unspent stat / skill points. |
| `debug wear`                | `ok`                   | Halve the durability of everything worn (repair tests). |
| `debug unid`                | `ok <count>`           | Unidentify every carried item (Cain tests). |
| `debug stat <id> <v>`       | `ok`                   | Set character stat 0..15 (12 level, 14 gold, 15 stash gold, ...). |
| `debug attack`              | `ok dmg=<min>-<max> ar=… def=… block=… dr=…%+… mdr=… res=f/l/c/p cb=… ds=… ow=… ll=… ml=… ias=… wsm=… frw=… fhr=… fbr=… thorns=…+…l cold=… fire=… light=… crit=… def_melee=… def_missile=… dodge=… avoid=… evade=…` | The player as combat sees them (components/rules Fighter). |
| `debug difficulty <d>`      | `ok`                   | Play on difficulty d (0 normal, 1 nightmare, 2 hell): a new game's Blood Moor monsters. |
| `debug skill left\|right <id>` | `ok` / `err not usable` | Put Skills.txt skill id on that button (the picker's rules). |
| `debug points <id> <n>`    | `ok` / `err not a class skill` | Give the character's class skill `id` n points (for testing a skill a save lacks). |
| `debug passives`          | `ok <stat>=<value>[/<itype>] ...` | The passive skills' stats on the character (FUN_00646d60), with the weapon type a mastery needs. |
| `debug charges <id> <n>`  | `ok`                   | Hold n (1..3) charges of charge-up skill id (release tests). |
| `debug release`           | `ok <missiles>` / `err no monster` | Release the held charges on the nearest live monster (FUN_005d5220). |
| `debug quest <q>`           | `ok`                   | Mark quest q done on the active difficulty (Act 1: 1 Den of Evil .. 6 Andariel). |
| `debug level`               | `ok <id> <x> <y> <w> <h> <wx> <wy>` | The player's level (1 town, 2 Blood Moor), position, level size and its act-tile origin. |
| `debug blocked <x> <y>`     | `ok 0\|1`              | Whether a unit can't stand at (x, y) in the player's level (past its edge: the neighbour's collision). |
| `debug warp <x> <y>`        | `ok`                   | Put the player at DS1 cell (x, y); past the edge next to another level, the next frame crosses into it. |
| `debug warps`               | `<i>\t<x>\t<y>\t<to>` per warp, `ok` | The level's warps (cave mouths, stairs): cell and the Levels.txt Id they lead to. |
| `debug objects`             | `<i>\t<x>\t<y>\t<shrine\|chest>\t<row>\t<mode>` per object, `ok` | The level's shrines (Shrines.txt row) and chests, and their mode now. |
| `debug operate <i>`         | `ok life=… mana=… boost=<row>` | Operate shrine / chest i (town.hpp operate) without walking to it. |
| `debug enter <i>`           | `ok` / `err no such warp` | Stand by warp i as if it was clicked; the next frame takes it (`debug level` shows where). |
| `key <name>`                | `ok`                   | Pushes SDL key down+up by SDL key name (`Left`, `Home`, `Escape`, `Return`). |
| `move <x> <y>`              | `ok`                   | Pushes an SDL mouse motion to game coords (hover without clicking). |
| `wheel <dy>`                | `ok`                   | Pushes an SDL wheel event (+up / -down); handled next frame. |
| `state`                     | `screen=… save=… scroll=… class=… name=… hardcore=… cam=x.xx,y.yy walking=… dir=… saves=… stash=0/1 cube=0/1 store=<vendor> gold=… statpts=… str=… life=cur/max mana=cur/max level=… exp=… pmode=<A1/GH/DT/DD or -> missiles=<in flight> pets=<life/max:mode,… or -> lskill=<Skills.txt id> rskill=<id> picker=<0 closed, 1 left, 2 right> charges=<skill:count,… or -> skillpts=… tree=<tab, 0 closed> waypoint=… items=<count> held=<code or -> unid=<unidentified carried> merc=<x,y:code or -> menu=<lines> automap=<cells when open> speech=<lines> voice=<Sounds.txt index> music=<index> music_old=<the song fading out, 0 none>\nok` | Loop state for scripted asserts. |
| `items`                     | per item `[code loc=… slot=… q=… panel=… at=col,row]` then its hover-text lines, indented; `ok` | The in-game character's items as the tooltip shows them. |

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
