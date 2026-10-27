# whisper

A remote control inside the **original game.exe (1.14d)**: read its
state and drive its player from another machine on your network.

## Run it

```sh
cd apps/whisper
./whisper.py 192.168.50.68 TOKEN state
```

Reply: `id 1 type 0 class 4 mode 5 x 5118 y 5168 life 59/59 mana 12/12`.
`TOKEN` is line 1 of `whisper.txt` on the game's PC (see Install).

## Why it exists

- d2d's ground truth is game.exe, and the mode that matters is d2d
  joining a game.exe host. Checking that used to need a person at each
  keyboard. With whisper, one session (or one script) plays both sides.
- `send` refuses any packet game.exe's own sizer would frame
  differently. That's how d2d's chat packet was caught missing a byte.
- On 2026-10-18 it drove a full Act 1 and Act 2 run with d2d.

## Install (once, on the game.exe PC)

1. On the Mac: `./build.sh` (needs `brew install mingw-w64`). It makes `ijl11.dll`.
2. In the Diablo II folder, rename `ijl11.dll` to `ijl11_real.dll`.
   With extensions hidden in Explorer, check you didn't get `ijl11_real.dll.dll`.
3. Copy the built `ijl11.dll` in.
4. Make `whisper.txt` beside `game.exe`: a token on line 1
   (`openssl rand -hex 12`), the port on line 2 (`4100`).
   **No file, nothing listens.**
5. Start the game, host a TCP/IP game, allow it through the firewall.

Undo: delete `ijl11.dll`, rename `ijl11_real.dll` back.

## More uses

| You want | Command (after `./whisper.py HOST TOKEN`) |
|---|---|
| A screenshot | `screenshot /tmp/host.bmp` |
| Every unit the client knows | `units` (`type id class mode x y life maxlife` a line) |
| Click / right-click / hover (800x600) | `click 400 300`, `rclick 400 300`, `move 400 300` |
| Talk to an NPC, take a warp, use a waypoint | `realclick X Y` (a real mouse click; the game must be the front window) |
| A key | `key 27` (Esc), `key 0x49` ('I', inventory) |
| Type text | `text hello` (into the open chat box) |
| A raw packet | `send 13 02 00 00 00 9a 00 00 00` (interact with object 0x9a) |
| Party up | `party accept 6` / `party invite 6` (the other player's id) |
| Follow another player like a mercenary | `follow` (Ctrl-C stops) |
| Read memory | `peek 463dd0 2` |
| The host's level tiles, room by room, against d2d | `town` prints `level L seed S` + rooms; `town d2d.txt` checks each loaded room against `drlg-dump DIR S L tiles > d2d.txt` (set `D2_PATCH_INSTALLER` as `tools/emu/diff_drlg.py` does). Only rooms near the player are loaded: walk the town first |
| The host player's quests (done / OPEN, this difficulty): check before any quest run | `quests` |
| Is it alive, which build | `ping` (`ok` + `git describe` + build date; a bare `ok` is a build from before versions) |

### The follower

`follow` keeps the host's player beside the nearest other player:
fights monsters near you both, drinks belt potions under 45% life,
accepts and makes party invites, and follows through warps and town
portals.

## When it breaks

| You see | Fix |
|---|---|
| `0xc0000142` when game.exe starts | The real DLL isn't named `ijl11_real.dll` (check for `.bak` or `.dll.dll`) |
| Connection refused / timed out | No `whisper.txt`, wrong port, or the firewall. Read `whisper.log` beside game.exe |
| `refused (token?)` | The token doesn't match line 1 of `whisper.txt` |
| `err no player unit` | game.exe is at a menu, not in a game |
| `state` fails, `peek 463dd0 2` isn't `7a6a70a1 ccccc300` | Not 1.14d: whisper's addresses are 1.14d's |
| A posted `click` walks but doesn't open the NPC / warp | Use `realclick`, or `send 13 ...` once close |
| `realclick` says `err` | game.exe isn't the front window on its PC |
| The game quit to the menu | A stray `key 27` opened the game menu and a later click hit **Save and Exit**. Send Esc only to close something you know is open |

## How it loads

game.exe imports `IJL11.DLL` (Intel's JPEG library) by ordinals 2, 3
and 5, from its own folder. whisper is a stand-in `ijl11.dll` that
forwards those three to the real one (`ijl11_real.dll`) and starts its
socket thread. No injector and no patched bytes; undoing it is a rename.
Every command runs on the game's own thread (a `WM_APP` message to the
game window).

Keep it on your own network: anyone with the token can drive the game.
