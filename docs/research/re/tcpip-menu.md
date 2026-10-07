# Other Multiplayer and TCP/IP menus — game.exe 1.14d

The front end's path to a TCP/IP game: Title → OTHER MULTIPLAYER →
TCP/IP GAME → JOIN GAME → the IP box → char-select. What happens on the
wire after that is in net-join.md. d2d: `apps/d2d/frontend.cpp`
(`other_multiplayer_ui`, `tcpip_ui`, `render_tcpip`), input in `main.cpp`.

Records are in the frontend table at `0x708d10 + index * 0x30`
(frontend-menu-table.md has the layout; `y` is the bottom). A screen adds
them with `FUN_0042f430(index)` (index in ECX; the decompiler drops it).
Text records (kind 4) hold a font pair at +0x24 (`{font id, colour}`,
table 0x7089ac..) and an alignment at +0x28. Font ids: 0 Font8, 1 Font16,
2 Font30, 3 Font42, 4 FontFormal10, 5 FontFormal12, 6 Font6, 7 Font24,
8 FontFormal11 (names at 0x6dc978 down to 0x6dc8d0).

## Other Multiplayer — FUN_00430c50

The title record 0x7090a0 (OTHER MULTIPLAYER, 0x13f4) calls it.
`FUN_0043c4f0` clears the screen and adds records 8, 6, 7 (TitleScreen
and the two logo halves, Sky palette); then:

| Index | Record | What |
|---|---|---|
| 0xff | WideButton (264, 310) 272x35 | OPEN BATTLE.NET (0x13fb), `FUN_00435d10` |
| 0x100 | WideButton (264, 350) | TCP/IP GAME (0x13fc), `FUN_00431a40` → `FUN_0042ffe0` |
| 0x101 | WideButton (264, 568), flags 0x1b | CANCEL (0x13ef), `FUN_00434df0` → the title |

## TCP/IP Options — FUN_0042ffe0

Clears, Sky palette, then:

| Index | Record | What |
|---|---|---|
| 0x102 | kind 2 (0, 599) 800x600 | `tcpipbckg` (handle 0x7797d0) |
| 0x103 | text (100, 76) 600x40, Font42 | "TCP/IP Options" (0x13fd) |
| 0x104 | text (270, 156) 272x40, Font16 | "Your IP Address is: " (0x1401) |
| 0x105 | text (265, 171) 272x40, Font16 | the address, `FUN_0042ff20` |
| 0x106 | WideButton (265, 206) | HOST GAME (0x13fe), `FUN_00435d80` (game type 8); hover `FUN_00430120` |
| 0x107 | WideButton (265, 264) | JOIN GAME (0x13ff), `FUN_00431d20`; hover `FUN_00430160` |
| 0x108 | text (265, 520) 272x210, FontFormal11 | host help (0x1402), hidden until HOST is hovered |
| 0x109 | same rect | join help (0x1403), hidden until JOIN is hovered |
| 0x10a | MediumButton (39, 571) 128x35 | CANCEL → `FUN_00430c50`; `FUN_004fa450` makes it Esc's |
| 0x10b | kind 8, 5000 | `FUN_0042ffd0` every 5 s: `FUN_0042ff20` again |

`FUN_0042ff20` puts `FUN_0040df60`'s address into 0x105. If it is
"127.0.0.1", 0x105 says "Cannot detect a valid TCP/IP address." (0x1404)
and HOST, JOIN and the 0x104 label are disabled (`FUN_004f96f0(…, 0)`).

`FUN_0040df60`: `gethostname` → `gethostbyname`, first address; if none,
a UDP socket "connected" to 24.105.29.30:7 and `getsockname`. The result
goes through `inet_ntoa`, so no address at all prints "0.0.0.0" (and the
buttons stay live).

## The join box — FUN_00431d20

Drawn over TCP/IP Options (the screen isn't cleared); the 5 s timer is
removed while it is up.

| Index | Record | What |
|---|---|---|
| 0x10f | kind 2 (268, 350) 264x176 | `PopUpOKCancel2` (0x779784) |
| 0x10e | kind 2 (291, 270) 218x26 | `IPAddressBox` (0x7797d4) |
| 0x10c | text (300, 250) 200x50, Font16 | "Enter Host IP Address to Join Game" (0x2b1f) |
| 0x10d | edit (300, 268) 213x20, FontFormal12 | the address; Enter runs `FUN_00434790` |
| 0x110 | (281, 337) 96x32 | CANCEL (`CancelButtonBlank` 0x7797b4) → `FUN_00431e00`; Esc's |
| 0x111 | (421, 337) 96x32 | OK (same chrome) → `FUN_00434790` |

The edit line starts with the last address used (registry
`Diablo II\LastTcpIp`, read only when this session has none yet).

## Join — FUN_00434790

1. Read the edit line (up to 0x100). Empty: nothing happens.
2. Save it to `LastTcpIp`. If it isn't `%d.%d.%d.%d`, resolve it
   (`FUN_0040e0d0`); a failure shows an error and stops.
3. Copy it to config +0x37 and connect once (`FUN_0052a750(0, ip)`) behind
   a "connecting" screen. On failure an error popup (`FUN_00433380(1)`).
4. On success, disconnect, then set game type 9 (config +0x19), +0x1d = 1,
   `DAT_007795ec` = 2, and go to char-select (`FUN_0043b080`). The real
   join happens when a character is picked (net-join.md).

## Join errors

**The popup** (`FUN_00433380(text in ECX, on-OK in EDX, stack flag)`) is
built from frontend records. The set depends on the palette: `DAT_007795f0`
is 1 when the screen's palette is `palette\fechar\pal.dat` (`FUN_0042f2e0`).

| Palette | Box | Message | OK |
|---|---|---|---|
| Sky (0) | 0xde `PopUpOK` (268, 350) 264x176 | 0xe0 (268, 300) 264x100, Font24 | 0xe1 (351, 337) 96x32, `CancelButtonBlank`, "OK" 0x13ee |
| fechar (1) | 0xf7 `PopUpOK2` | 0xf9 (268, 320) 264x120, Font24 | 0xf8, `OkCancelButtonBlank` |

The stack flag picks OK (0xe1 / 0xf8) over CANCEL (0xdf / 0xfa).
`FUN_004330a0` writes the message, starting a new line at each '\n'.

**When the test connect fails,** `FUN_00434790` shows "Cannot Connect to
Server" (0x145b). Its OK goes back to TCP/IP Options (`FUN_0042ffe0`).

**When the host refuses with `[B4, reason]`,** `FUN_0044e380` keeps the
reason (any reason over 0x1c becomes 9) in `DAT_007a05d4`. `FUN_0044cb60`
then draws the reason's message over the game screen: Font42, centred,
wrapped to 600 when it is wider than the screen less 40, from y 200. The
message is the string id from the u16 table at 0x70f384:

| Reason | Id | Text |
|---|---|---|
| 0 | 0x14f5 | Unable to enter game. Bad character version |
| 1..4 | 0x14f6..0x14f9 | … bad quest / waypoint / stats / skills data |
| 5, 0x1c | 0x14fb | Unable to enter game |
| 6, 0x1b | 0x14fa | failed to join game |
| 7 | 0x14ed | Your connection has been interrupted |
| 8 | 0x14ee | The Host of this game has left |
| 9 | 0x14fc | unknown failure |
| 0xa..0x11 | 0x14fd..0x1504 | Unable to enter game, bad inventory / dead bodies / header / hireables / intro / item / dead body item / generic bad file |
| 0x12 | 0x1505 | Game is full |
| 0x13 | 0x14f0 | Versions do not match … |
| 0x14 | 0x14f4 | … must kill Diablo to play in a Nightmare game |
| 0x15 | 0x14f3 | … must kill Diablo in Nightmare to play in a Hell game |
| 0x16 | 0x14f2 | A normal character cannot join a game created by a hardcore character |
| 0x17 | 0x14f1 | A hardcore character cannot join a game created by a normal character |
| 0x18 | 0x14ef | A dead hardcore character cannot join or create any games |
| 0x19, 0x1a | 0x2775, 0x2776 | classic / expansion character in the other kind's game |

For an expansion character (bit 0x20 at +0x1ef of `DAT_007a0438`), reasons
0x14 and 0x15 use 0x5522 / 0x5521 instead ("must kill Baal …").

## d2d

- OPEN BATTLE.NET and HOST GAME do nothing. d2d has no Battle.net and only
  joins a game.exe host.
- The address is kept in d2d.cfg as `last_tcp_ip`.
- Step 3's test connect isn't done. A host that doesn't answer shows up when
  char-select joins. Char-select shows the Sky popup with 0x145b, and its OK
  goes back to TCP/IP Options (after a `--join`, it stays on char-select).
- A B4 refusal's message goes in the same popup on char-select. game.exe
  draws it over the game screen, because its join runs inside the game loop.
- Anything game.exe never meets shows d2d's own words in that popup: a name
  a game.exe host won't take, a host that doesn't finish the join, a desync.
- The address is read once when the screen opens, not every 5 s.
