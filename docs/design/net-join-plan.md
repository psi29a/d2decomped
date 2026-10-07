# Joining a game.exe host — implementation plan (step 1)

Status: built through M10 (PRs #8–#12, 2026-10-06); the live checks on
PR #12's features are still open. M4's menu path (Other Multiplayer → TCP/IP
→ Join) and its error popup landed 2026-10-07: re/tcpip-menu.md. Planned 2026-09-30. Goal: d2d's client joins an open
TCP/IP game hosted by an unmodified 1.14d game.exe, and Act 1 plays
together (multiplayer.md "Order" step 4, the netplay goal's first leg).

Sources: `re/net-join.md` (transport, compression, join, save-back, live
runs), `re/net-packets.md` (layouts; what the client does itself),
`net-host.md` (the shared codec this is the client half of), and
`tools/emu/join_live.py`, which already joined a real host (in the world in
0.28 s, 295 packets split cleanly, save-back identical except the header).

## Constraints

- **No Blizzard bytes in the repo.** The Huffman lengths (VA 0x7076c0) and
  the S→C size table (VA 0x730ae8, 181 u32) are read at runtime from the
  user's own game.exe (next to the MPQs, or `--game-exe` / `D2_GAME_EXE`),
  checked first: Kraft sum exactly 1, lengths 1..15, known sizes (0x01 = 8,
  0x03 = 12, 0x59 = 26, 0x8f = 33). A failed check disables joining with a
  message. Fixtures are hand-built packets over a synthetic Huffman table;
  real captures stay in the user dir and reach tests only through
  `D2_NET_CAPTURE` (skipped when unset, like `D2_MPQ_DIR`).
- **GPL-3.0-or-later**, like the rest of the repo. Community projects
  (d2-clientless, D2MOO) are labels only (re/unverified.md); no code copied.
- **Never Blizzard servers.** Only an address the user types; no default
  host, no gateway list, no BNCS / MCP here (battlenet.md, PvPGN only).
  Live checks run only against hosts on Bret's LAN.
- **C++26** (MSVC /std:c++latest) in PLAN.md's style: meaningful names,
  `auto Class::name() -> Ret` out of line, self-contained headers,
  clang-tidy clean, assert + `int main` tests. Namespaces `d2d::net::d2gs`
  (codec, sans-IO) and `d2d::net`. No game logic in socket code (rule 3).

## Layout

```
components/net/d2gs/    lib d2gs: pure codec, links d2s only (test_d2gs)
  exe_tables.{hpp,cpp}  PE section walk; lengths, S→C sizes (C→S 0x730dc0 later)
  huffman.{hpp,cpp}     canonical build / compress / decompress, AF 81 reload
  frame.{hpp,cpp}       1- / 2-byte frame headers, partial frames, raw phase
  split.{hpp,cpp}       S→C sizes + the 13 variable rules
  s2c.{hpp,cpp}         S→C structs (std::variant; Raw{id, bytes} for the rest)
  c2s.{hpp,cpp}         C→S builders (68, 6c, 6b, 6d, 69, then gameplay)
  items.{hpp,cpp}       net item bitstream over d2s's Bits
components/net/         lib net: links d2gs, game (test_net)
  tcp_transport.{hpp,cpp}  socket, TCP_NODELAY, bounded queues, POSIX / Winsock
  join.{hpp,cpp}           the join state machine (sans-IO, injected clock)
  client_model.{hpp,cpp}   units / rooms / stats / items by (type, id) → View
apps/d2d/               --join <host>, the TCP/IP join screen, devctl "net"
```

`ClientModel` replaces `apply_view` for network games (net-host.md §3);
`Town` takes its `View` from either source, and the in-process `World` /
`LocalTransport` / `ViewEncoder` path is untouched.

## Milestones

Focused days, one developer or agent. Each lands alone, tree green.

### M1. Codec core: exe tables, Huffman, framing, splitter — 2 days

- Files: `components/net/CMakeLists.txt`, `d2gs/exe_tables`, `huffman`,
  `frame`, `split`; `components/CMakeLists.txt`; `tests/test_d2gs.cpp`.
- Check (ctest `test_d2gs`): a fake PE built in the test yields its tables;
  bad Kraft sums and 0 / 16 lengths are refused; synthetic-table round trip
  of 300 random buffers; frame headers at 0xEF / 0xF0 / 0xFFF and split
  across reads; each variable size rule; an unknown id is a `Desync`, never
  a crash. With `D2_GAME_EXE`: net-join.md's two live frames decode to
  `b4 10 00 00 00` and `01 00 04 00 10 00 01 00 | 00 | 02`.

### M2. Join and system packets — 1 day

- Files: `d2gs/s2c`, `d2gs/c2s`; `test_d2gs`.
- C→S 68 (37), 6c (n+7), 6b, 6d (13), 69; S→C 00..06, 59, 5a, 8f, AF, B0,
  B3, B4. Everything else decodes to `Raw`.
- Check: round trip per struct; 68 byte-exact to 0x477f76 (version 0x0e,
  token 1); a 2516-byte save chunks into nine 0x106 packets plus one n+7
  whose data sums to exactly the total.

### M3. Join state machine — 1.5 days

- Files: `components/net/join`; `tests/test_net.cpp`.
- Connecting → Raw (AF xx, xx ≠ 0) → Uploading (68, then every 6c at once)
  → Accepted (01 / 00 / 02, send 6b) → Loading (59, 03, 53) → InGame (04)
  → Leaving (69; B3 chunks; B0; 05 06) → Closed. B4 → Refused with its
  reason, **ignoring the 01 / 00 / 02 the host still flushes after it**
  (live check). 6d every 5 s, zero anti-cheat dword.
- Save-back: reassemble B3 (restart on the first-chunk flag), check magic,
  size and the +0xc checksum (game.exe doesn't), keep a `.d2s.bak`, write
  through `CharacterStore`.
- Refuse before connecting a save over 0x1fff bytes or one d2d can't parse
  back: a chunk past the total kills the host process (net-join.md "Risks").
- Check (`test_net`): a scripted host plays net-join.md's full-join order
  in synthetic frames and asserts every byte sent; B4 0x10 ends Refused; a
  truncated B3 stream leaves the old save untouched.

### M4. TCP transport and `--join` — 1.5 days

- Files: `components/net/tcp_transport`; `apps/d2d/main.cpp` (`--join`,
  `--game-exe`, `--net-log`), `frontend.cpp` (OTHER MULTIPLAYER → TCP/IP →
  Join, address kept in d2d.cfg), `devctl_verbs.hpp` (`net`: state,
  histogram, last B4); `test_net`.
- A socket thread with bounded queues; the game thread drains it at each
  40 ms tick (rule 5). `--net-log` writes the decompressed stream to the
  user dir (for `D2_NET_CAPTURE`).
- Check (ctest): the M3 scripted host behind an in-process 127.0.0.1
  listener; partial reads and writes. Live (LAN host): `d2d --headless
  --join <host>` repeats join_live.py: InGame under 1 s, 15 s, leave,
  save-back differs only at 0x0c, 0x30, 0xa8..0xab; no unknown id.

### M5. Net item bitstream — 3 days

- Files: `d2gs/items`, `components/d2s/d2s_items.hpp` (a `net` switch or a
  shared core); `test_d2gs`, `test_d2s`.
- net-packets.md "Items": no JM, ground x / y 16 bits, no uid / seed /
  realm data, no stat lists when unidentified, sockets as separate
  0x9d / 0x13, header-only flag 0x2000000, save bits (not send bits). Plus
  the 0x9c / 0x9d headers and 0x3e, 0x3f, 0x40, 0x42.
- Check: write → read round trips (identified, unidentified, ground gold,
  a stack, socketed, set, ear). With a capture: every 0x9c / 0x9d parses to
  its end (under 8 padding bits), and **the host's echo of the joiner's
  own items matches the uploaded save**, stat for stat: risk 1's oracle.

### M6. ClientModel and drawing the host's world — 4 days

- Files: `components/net/client_model`; `components/game/gamedata` (Act 1
  at the host's map seed from 0x03); `components/game/world.hpp` (View:
  other players, monster life byte, chat lines); `apps/d2d/town.{hpp,cpp}`
  (the View source), `world_view.cpp`.
- Units by (type, id): 0x59 / 0x15 players, 0xac monsters and NPCs (merc
  via 0x81), 0x51 objects, 0x09 warps, 0x9c ground items, freed on 0x0a;
  rooms 0x07 / 0x08; stats 0x1d..0x1f, 0x94, 0x21..0x23; quests 0x28 /
  0x29 / 0x52; states 0xa8..0xaa. A packet for an unknown unit is dropped,
  as game.exe does. NPCs map to `Level::npcs` by (class, position) for now.
- Check (ctest): a synthetic session yields a View with the player at its
  0x15 spot, NPCs where sent, own items from 0x9d; capture replay has no
  unknown unit. Live: a devctl screenshot of the host's camp at its seed,
  the joiner where the host shows it.

### M7. Other units walking and animating — 3.5 days

- Files: `components/net/client_model`; a path / step helper split out of
  `components/game/ai` (pathing stays local, rule 4).
- The unit command table (`FUN_00480c10`: cmd → mode, goal or target) from
  0x0f / 0x10 / 0x67..0x6d, 0x0c hits, 0x4c / 0x4d swings; the client walks
  the path at the sent velocity; modes animate from the COF / AnimData
  timing d2d has. Corrections at game.exe's tolerances: other players snap
  past 15, monsters re-path past 15 (5 / 7 while moving).
- Includes half a day of RE on `FUN_00480780`, `FUN_004804a0`,
  `FUN_00648cf0` (client path vs server path, per-frame stepping).
- Check (ctest): scripted 0x67 / 0x68 walks reach the goal; a correction
  past tolerance re-paths. Live: `net`'s correction count stays low around
  the camp NPCs and a Blood Moor pack.

### M8. Own movement: prediction and 0x5f — 3 days

- Files: `components/net/client_model`, `d2gs/c2s`, `apps/d2d/town.cpp`
  (a network `Command` sink beside `LocalTransport`).
- Walk / run start locally, then 01 / 03 (u16 subtiles) and 53 / 54;
  repeats inside 200 ms are dropped. 0x95 / 0x96 / 0x18 carry the host's
  position; past (ping + 50) / 128 + 3 / 7 / 5 subtiles send 5f with ours,
  never snap; 0x15 reassigns.
- Check (ctest): a scripted drift sends one 5f, none once back in
  tolerance. Live: five minutes in camp and Blood Moor with **zero 0x15
  reassigns**, the host drawing the joiner where d2d does.

### M9. Commands in game.exe's shapes — 4 days

- Files: `d2gs/c2s`, `components/net/client_model`,
  `components/game/protocol.hpp` (targets as unit type + id),
  `apps/d2d/town.cpp`, `store.cpp`, `panels.cpp`.
- 0x13 interact and NPC menus (talk 0x2f / 0x30, trade, hire, waypoint),
  0x3c select skill then 05.. skills, 0x16 pick up, item moves (grid,
  body, belt, cursor, drop, use), 0x32 buy with item id and cost, sell,
  0x3a / 0x3b stat and skill points (Skills.txt ids), 0x02 / 0x04.
- Check (ctest): each `Command` maps to its layout (net-packets.md "Client
  → server"). Live: buy a potion from Akara, drink it, swap a weapon, spend
  a stat point; the host's view and the save-back agree.

### M10. Act 1 playable together — 4 days

- Files: `components/net/client_model`; `apps/d2d/ingame.cpp`,
  `panels.cpp`, `ui.cpp` (chat line, party screen), `town.cpp`.
- Casts built on the client from 0x4c / 0x4d (Fight's missile rules for
  display only), life bytes (0xab), deaths and corpses (0x8e), other
  players (0x5b / 0x5c / 0x75 / 0x7f), chat and events (0x26 / 0x5a; send
  C→S 0x14), party (0x5e, 0x8b..0x8d), town portals (0x60, 0x82),
  waypoints (0x63), level from position. Levels d2d can't build (Acts 2-5)
  show a message instead of loading.
- Check (live playthrough): join the daughter's game, party, clear the Den
  together, take the Cold Plains waypoint, portal back, leave; the
  save-back loads in game.exe; an hour with no Desync or disconnect.

**Total: about 27.5 days** (the join itself, M1-M4: 6 days).

## Open risks

1. **Network item bitstream.** A wrong width misparses the item silently
   (the packet length still frames it, so the stream survives). Unidentified
   gating, set lists, runewords and ears are the likely traps. M5's
   own-items oracle catches most; captures of a varied stash the rest.
2. **0x5f drift correction.** The own player is never snapped. Too tight
   floods 5f; too loose makes the host reassign (0x15), i.e. rubber-banding;
   ping jitter moves the tolerance; how the host paces an own walk against
   its frames isn't traced. Count 5f and 0x15 per minute in `net`; M8 is
   gated on zero reassigns.
3. **Local pathing and animation from commands.** The host sends goals, not
   frames. If d2d's path or stepping differs from game.exe's, units drift
   until corrected and swings land at other times (M7's RE half day).
   Casts are rebuilt locally, so a mismatch shows as wrong effects, not a
   desync.
4. **Splitter exactness.** One wrong size corrupts the rest of a frame with
   no resync. The runtime table and capture replay cover it; a Desync ends
   the session cleanly.
5. **Save ownership.** The host's B3 copy is authoritative for its game; a
   crash before leaving loses that progress. Keep the upload as a backup.

## Open questions

- Does LODPatch_114d.exe carry game.exe whole, so the tables can come from
  the installer d2d already reads?
- The unexplained bit 0x4 in S→C 0x01's game flags (live check).
- Which packets a joiner gets for units already in its rooms, in what
  order: the first `--net-log` capture in M4 answers it.
- Which flag keeps a long game alive past the 30 s client timeout
  (`FUN_0044eec0`), so d2d times out the same way.
