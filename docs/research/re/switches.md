# Command-line switches — game.exe 1.14d

The drop-in goal (`docs/PLAN.md`) is that a switch a player or a mod
already passes to 1.14d keeps that spelling and that meaning. This note
is the table those switches come from, and the pass that fills them in.

Address range: the table is `0x00705040`–`0x00706518` (exclusive) in
`.rdata`. The ini load and the command-line pass sit at `0x00405450`–
`0x004064c0`.

## Table layout

58 entries. Stride `0x5c` (92 bytes). The loader walks `0x14d8` bytes,
which lands on `0x00706518`. Each entry is three inline names, then a
kind, a struct offset, and an integer default. The names are characters
in the entry, not pointers.

| off  | size    | field |
|------|---------|-------|
| 0x00 | 27 bytes | ini section |
| 0x1b | 27 bytes | ini key |
| 0x36 | 27 bytes | token, without the leading `-` |
| 0x51 | 1 byte  | kind: 0 bool, 1 int, 2 string |
| 0x52 | 2 bytes | zero in every entry |
| 0x54 | 4 bytes | offset into the config struct |
| 0x58 | 4 bytes | default for the ini integer read |

The config struct is `0x3cd` bytes (973). It is zeroed before the ini
load. The highest offset in the table is 861 (`SKIPTOBNET`), inside
that size.

Token text starts at `0x00705076` (the first entry's `+0x36`).

## How a command line is applied

`FUN_004064c0` (`0x004064c0`) picks the command line. When more than the
program name was passed, the command line is the single string
`argv[argc - 1]`, not each argv slot. When only the program name was
passed, another command line is used. What that line is was not read.

`FUN_00405f00` (`0x00405f00`) may replace that string before parsing.

- A null or empty line, with `UseCmdLine` set, loads `CmdLine`.
- A line that is already present is written back to `CmdLine` with
  ` -skiptobnet` appended.
- `UseCmdLine` is then cleared.
- When the service command line is selected, the line is replaced with
  `SvcCmdLine`.

The helpers that read those keys were not opened. The key names sit
under the label `Diablo II`. Whether `CmdLine`, `UseCmdLine`, and
`SvcCmdLine` are registry values or a file is open.

`FUN_004059a0` (`0x004059a0`) zeros the struct, loads `D2.ini`, calls
`FUN_006114e0` (no arguments; not traced), then applies the command
line.

`FUN_00405450` (`0x00405450`) is the ini load. The path is whatever
`FUN_00413800` builds from the name `D2.ini`. The directory that helper
adds was not traced. For each entry it reads that section and key into
the struct field:

- Bool: the profile int becomes 0 or 1.
- Int: the profile int, default taken from the entry's `+0x58`.
- String: the profile string, empty default, 27-byte destination. The
  `+0x58` dword is not used. Every string entry's dword is 0.

The walk adds `0x5c` and stops at `0x14d8`.

`FUN_004058a0` (`0x004058a0`) scans the command line for `-`.
`FUN_00405710` (`0x00405710`) takes the text after the dash, lowercases
`A`–`Z`, and shortens it from the end until `FUN_00405570`
(`0x00405570`) finds an exact token. `-windowed` matches `windowed`.
`-act2` matches `act` and leaves `2`. The unmatched tail is the
argument: leading space, tab, newline, and colon are skipped, and the
argument ends at the next one of those. `FUN_00405570` walks the 58
tokens and returns the index, or −1.

On a match the field is written by kind:

- Bool stores 1 and ignores the argument.
- Int stores `atol` of the argument.
- String copies the argument.

The command line runs after the ini load, so it overrides the same
field. This pass does not write `D2.ini`.

## The 58 switches

Ini default below is the `+0x58` dword for a bool or an int. A string's
ini read uses an empty default; its dword is 0 and unused.

d2d status is from the 2026-10-20 map of this table onto d2d, rechecked
against `main` at `f2906d3` for the options that status depends on
(`--seed`, `--no-save`, `--join`, `--start-class`, vsync forced on):

- `ready` — d2d already does this; the token only needs translating.
- `partial` — something similar exists, and the behavior differs.
- `missing` — d2d has nothing for a switch that still applies.
- `n/a` — Battle.net, a retired video backend, Blizzard debug, or an
  old client flag that does not apply.

| token | section | key | kind | off | default | d2d |
|---|---|---|---|---:|---:|---|
| `-crash` | DEBUG | CRASHAPP | bool | 4 | 0 | n/a, Blizzard debug |
| `-w` | VIDEO | WINDOW | bool | 8 | 0 | partial, d2d's fullscreen is a separate borderless switch |
| `-window` | VIDEO | WINDOW | bool | 8 | 0 | partial, same byte as `-w` |
| `-windowed` | VIDEO | WINDOW | bool | 8 | 0 | partial, same byte as `-w` |
| `-nofixaspect` | VIDEO | ASPECT | bool | 9 | 0 | missing |
| `-3dfx` | VIDEO | 3DFX | bool | 10 | 0 | n/a, 3dfx |
| `-opengl` | VIDEO | OPENGL | bool | 11 | 0 | n/a, old renderer pick |
| `-d3d` | VIDEO | D3D | bool | 13 | 0 | n/a, Direct3D |
| `-rave` | VIDEO | RAVE | bool | 12 | 0 | n/a, Rave |
| `-per` | VIDEO | PERSPECTIVE | bool | 14 | 0 | n/a, 3D-card perspective |
| `-lq` | VIDEO | QUALITY | bool | 15 | 0 | missing |
| `-gamma` | VIDEO | GAMMA | int | 16 | 0 | missing |
| `-vsync` | VIDEO | VSYNC | bool | 20 | 0 | partial, d2d always turns vsync on |
| `-fr` | VIDEO | FRAMERATE | int | 21 | 0 | missing |
| `-s` | NETWORK | SERVERIP | string | 55 | empty | partial, `--join` is a host, not this field |
| `-gametype` | NETWORK | GAMETYPE | int | 25 | 0 | n/a, Battle.net |
| `-arena` | NETWORK | ARENA | int | 519 | 0 | n/a, Battle.net |
| `-joinid` | NETWORK | JOINID | int | 29 | 0 | n/a, Battle.net |
| `-gamename` | NETWORK | GAMENAME | string | 31 | empty | n/a, Battle.net |
| `-bn` | NETWORK | BATTLENETIP | string | 79 | empty | n/a, Battle.net |
| `-mcpip` | NETWORK | MCPIP | string | 103 | empty | n/a, Battle.net |
| `-ama` | CHARACTER | AMAZON | bool | 133 | 1 | partial, `--start-class` can preselect Amazon |
| `-pal` | CHARACTER | PALADIN | bool | 134 | 0 | partial, `--start-class` |
| `-sor` | CHARACTER | SORCERESS | bool | 135 | 0 | partial, `--start-class` |
| `-nec` | CHARACTER | NECROMANCER | bool | 136 | 0 | partial, `--start-class` |
| `-bar` | CHARACTER | BARBARIAN | bool | 137 | 0 | partial, `--start-class` |
| `-i` | CHARACTER | INVINCIBLE | bool | 140 | 0 | n/a, Blizzard debug |
| `-name` | CHARACTER | NAME | string | 189 | empty | partial, `--start-name` fills a new character |
| `-realm` | CHARACTER | REALM | string | 213 | empty | n/a, Battle.net |
| `-ctemp` | CHARACTER | CTEMP | int | 493 | 0 | n/a, not traced past the struct |
| `-nm` | MONSTER | NOMONSTERS | bool | 497 | 0 | n/a, Blizzard debug |
| `-m` | MONSTER | MONSTERCLASS | int | 498 | 0 | n/a, Blizzard debug |
| `-minfo` | MONSTER | MONSTERINFO | bool | 502 | 0 | n/a, Blizzard debug |
| `-md` | MONSTER | MONSTERDEBUG | int | 503 | 0 | n/a, Blizzard debug |
| `-rare` | ITEM | RARE | bool | 507 | 0 | n/a, Blizzard debug |
| `-unique` | ITEM | UNIQUE | bool | 508 | 0 | n/a, Blizzard debug |
| `-act` | INTERFACE | ACT | int | 511 | 1 | missing |
| `-log` | DEBUG | LOG | bool | 534 | 0 | partial, d2d always logs and `--log-level` sets verbosity |
| `-msglog` | DEBUG | MSGLOG | bool | 535 | 0 | n/a, Blizzard debug |
| `-safe` | DEBUG | SAFEMODE | bool | 536 | 0 | n/a, Blizzard debug |
| `-nosave` | DEBUG | NOSAVE | bool | 537 | 0 | ready, `--no-save` |
| `-seed` | DEBUG | SEED | int | 538 | 0 | partial, `--seed` fixes d2d's map seed; this field was not traced into the generator |
| `-nopk` | NETWORK | NOPK | bool | 131 | 0 | n/a, Battle.net |
| `-cheats` | DEBUG | CHEATS | bool | 542 | 0 | n/a, Blizzard debug |
| `-teen` | DEBUG | TEEN | bool | 543 | 0 | n/a, Blizzard debug |
| `-ns` | DEBUG | NOSOUND | bool | 544 | 0 | partial, no mute switch |
| `-npl` | FILEIO | NOPREDLOAD | bool | 515 | 0 | n/a, old client preload flag |
| `-direct` | FILEIO | DIRECT | bool | 516 | 0 | missing |
| `-lem` | FILEIO | LOWEND | bool | 517 | 0 | n/a, old client IO flag |
| `-questall` | DEBUG | QuEsTs | bool | 545 | 0 | n/a, Blizzard debug |
| `-comint` | NETWORK | COMINT | int | 549 | 0 | n/a, old network interface index |
| `-skiptobnet` | NETWORK | SKIPTOBNET | bool | 861 | 0 | n/a, Battle.net |
| `-openc` | NETWORK | OPENC | bool | 132 | 0 | n/a, Battle.net |
| `-nocompress` | FILEIO | NOCOMPRESS | bool | 518 | 0 | missing |
| `-txt` | TXT | TXT | bool | 533 | 0 | missing |
| `-build` | BUILD | BUILD | bool | 547 | 0 | n/a, Blizzard debug |
| `-nosound` | DEBUG | NOSOUND | bool | 544 | 0 | partial, same byte as `-ns` |
| `-sndbkg` | DEBUG | SOUNDBKG | bool | 548 | 0 | n/a, Blizzard debug |

Counts: 1 ready, 15 partial, 8 missing, 34 n/a.

## Quirks

`-ns` and `-nosound` write DEBUG/NOSOUND at offset 544. `-w`,
`-window`, and `-windowed` write VIDEO/WINDOW at offset 8.

There is no `-dru` and no `-ass` in this table. Whether another table
has a druid or assassin switch was not checked. `-ama` is the only
class bool whose ini default is 1. The other four class bools default
to 0.

`-set` and `-lng` are not in the table. They show up only in a message
box from `FUN_0044b4e0` (`0x0044b4e0`) and `FUN_0044b600`
(`0x0044b600`). That box is shown only for one Windows account.

## Plan for d2d

From `docs/PLAN.md` and the notes on
[#61](https://github.com/psi29a/d2decomped/issues/61#issuecomment-6312881548):

- A one-dash switch keeps game.exe's spelling and meaning. A two-dash
  option is d2d's own. `-seed` behaves the way game.exe has it,
  `--seed` stays as it is, and neither one changes the other. Ideally
  they call the same code.
- A switch d2d cannot support yet is accepted and logged as ignored.
  It is not rejected, so an existing command line still starts.
- This note is the research step. The translator, then the wiring, are
  each their own change.

d2d parses with CLI11 2.6.2. A single dash plus letters is a short
option of the first character only, and d2d registers no short options,
so a game.exe token has to be translated before `app.parse`. `-s`,
`-w`, `-i`, and `-m` must never be registered as CLI11 short options.
A longer token that starts with that letter (`-seed`, `-window`,
`-name`) would be read as the one-letter option.

## Open

- DEBUG/SEED at offset 538 is stored as an int. d2d's `--seed` fixes
  the map seed. This pass does not show that the struct field is that
  seed.
- What CHARACTER/CTEMP (offset 493) does past the struct.
- `-ama` defaults to 1. How that bool becomes the starting class,
  beside the other class bools, was not traced.
- What `FUN_006114e0` does between the ini load and the switch apply.
- What directory `FUN_00413800` puts on `D2.ini`.
- Whether `CmdLine`, `UseCmdLine`, and `SvcCmdLine` are registry values
  or a file.
- What command line is used when only the program name is passed.
