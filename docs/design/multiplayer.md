# Multiplayer — impact and routes

Status: decided 2026-09-27, being built (docs/PLAN.md "Networking-shaped
core"). First written 2026-09-25 against commit ffc527c; the rules below
merge a networking design review (2026-09-27) into it.

## How Diablo II does it

D2 is client/server even alone: single player runs the server (the old
D2Game.dll, now inside game.exe) and the client (D2Client) in one process,
talking through the same packet layer an open or closed Battle.net game
uses. The server owns the simulation at 25 ticks a second: units,
monsters, items, drops, damage, experience. The client sends intentions
("walk to x, y", "use skill 64 on unit 1234", "pick up item 77") and
draws what the server tells it, with a little prediction of its own
movement. Levels aren't sent: the server sends the act's map seed and each
client generates the same rooms (d2d already does this, deterministically).
Monster HP, experience and NoDrop scale with the number of players in the
game.

## Where d2d stands

What already helps:
- **Game rules are pure** (`components/rules`, `components/drlg`): combat,
  drops, spawning, skills and the level generator take tables and seeds and
  return results, with tests. They can run on a server unchanged.
- **Levels come from seeds** (`--seed`, `act1_layout`, `generate_outdoor`):
  a client can build the same Blood Moor from a map seed, as D2's does.
- **The devctl channel** (`devctl_verbs.hpp`) is already a text command
  interface into a running game; the scripted tests drive the game through
  it. It shows where a command boundary would sit.

Done (2026-09-27, docs/PLAN.md "Networking-shaped core"):

| Was | Now |
|---|---|
| The simulation stepped on the frame clock (~60 fps, variable `dt`) | `World::tick` at 25 Hz (`kTickMs` 40); the camera slides between ticks; other units move at the tick rate, as in game.exe |
| `Town` mixed input, UI, simulation and drawing | `World` (server.hpp: levels, units, monsters, missiles, ground items, objects, rng) and `Town`, its client |
| Monsters and ground items were vector indices | Unit ids (`Monster::id`, `GroundItem::id`, the character's items' runtime `Item::id`); objects and NPCs by their level index (the same list on every machine) |
| Clicks called game code directly | `Command`s (protocol.hpp), applied in the tick; a busy player's movement and skills dropped, as game.exe's dispatcher does. Movement, skills, interact, pick-up, resurrect; stat / skill points, select skill, belt potions; the cursor (item moves); NPC deals (trade, gamble, buy, sell, repair, identify, hire) |
| The client read the World's state to draw (monsters, the player, the ground, the character) | The World fills a `View` after each tick (server.hpp: units, missiles, ground, fires, NPC states, the player's mode and look, its own character in its save form with unit ids, the item in hand, the store's stock, the hire list, sounds, events); it crosses as bytes (replication.hpp) and the client reads nothing else. The World owns its own character copy (`World::enter`) |
| A whole View every tick (25 KB in the Blood Moor) | What changed since the client's last one (replication.hpp `ViewEncoder`): monsters within 28 cells, each one's look when it changes and its state when that does; sections (the character's header, stats, items, the ground, fires, NPCs) only when they change; a keyframe on a new level. About 0.4 KB a tick on average (≈10 KB/s) |
| Commands were C++ values handed across | They cross as bytes (a codec with game.exe's packet ids where they match, float32 positions) through a `LocalTransport`; test_protocol round-trips every one |
| The World told the client things by poking its fields | `Event`s: level changed, open UI (stash, waypoint, NPC) |
| Saves were read, never written | The World writes the character through a `CharacterStore` (character_store.hpp: the .d2s via components/d2s/d2s_write.hpp; every real save writes back byte for byte) on leaving the game or quitting; new characters get CharStats' start and are written at once |

Still to change:

| Today | For multiplayer |
|---|---|
| One player, one merc (`Foe` array of two, `cc` the character) | N players and mercs; monster targeting, party experience, loot ownership |
| Compact records (positions as floats, a unit's look as strings; rooms rather than a radius) | Quantised positions, look by table index, D2's room-based interest |
| One shared `Rng` for every roll | Server-owned seeds (D2 keeps per-unit and per-room seeds) |
| Sounds are cued by the World into `Cues`, which the client plays | Events (hit, death, drop); the client picks sounds and effects |
| Monster stats ignore player count | HP / experience / NoDrop scale with players (D2's /players setting) |

## Decision

Route 1, a **listen server**, and D2's own shape: every game is a
`GameSession` with an authoritative server simulation and one or more
clients. Single player is one client talking to an in-process server,
as in game.exe. Multiplayer is the same session with more clients.
The same server code runs every mode: a local game, a TCP-hosted game,
and a dedicated server with nobody at the keyboard.

Rules:

1. **The server is authoritative.** Damage, hits, skill effects, items
   (made, destroyed, owned), monsters (spawn, AI, death), quests, NPC and
   map state, experience, party. A client sends intents (walk to x, y;
   use skill s on unit u; operate object o; pick up item i; talk to NPC
   n; leave) and shows what the server sends back.
2. **One implementation of the rules**, with no separate single-player
   path. `components/rules` and `components/drlg` stay pure (tables and
   seeds in, results out) and run on the server.
3. **The transport is a detail.** A `LocalTransport` passes messages
   in-process, in order, with no sockets and no loopback TCP for single
   player. `TcpTransport` comes later, with explicit framing, partial
   reads and writes, bounded buffers and backpressure, and no game logic
   in the socket code. A loopback-TCP test mode then exercises it.
4. **Don't network the engine.** Rendering, animation, sounds, UI,
   pathfinding internals, caches: all local. Only inputs (commands) and
   outputs (state updates, events) cross the boundary.
5. **A fixed tick.** Network input is queued as commands and applied at
   the start of a tick; the tick then produces the updates. No network
   callback touches the world directly. The tick is game.exe's 25 Hz.
6. **The message model is D2's.** game.exe's client/server packets (the
   research in step 2) give the message shapes. Where cheap, the internal
   messages use them, so a D2GS-compatible codec is a codec rather than a
   second design. Packet layouts belong to the codec, not to the
   simulation.
7. **The session and the save are separate.** `GameSession`: the world,
   the clock, units, monsters, items, NPCs, quests, party, membership,
   `max_players`. `PlayerSession`: the connection, the player's unit id,
   its character reference, replication state. A `CharacterStore` loads
   and saves characters: `.d2s` locally, anything later for a realm.
8. **No hard-coded 8.** `max_players` defaults to 8 (classic); nothing
   in the data structures assumes it. More than 8 is a later,
   experimental mode: D2's balance (monster HP, experience, NoDrop by
   player count) is built round 8.
9. **Layers above the game stay above it.** A lobby, Realm (MCP) and
   Battle.net (BNCS) sit over the game server. None of it goes into the
   simulation. Not planned yet.

Useful references for the protocol: d2-clientless and
d2-dedicated-server (jaenster, 1.14d D2GS / Realm), D2MOO (engine
structures), OpenD2.

## Order

1. Research: game.exe's packet tables (client → server, server → client),
   their handlers, and the server tick. Results go in
   `docs/research/re/network.md`.
2. The networking-shaped core, in-process only:
   - `World` (the server side: levels, units with stable ids, monsters,
     missiles, ground items, objects, the tick, server rng)
   - a command queue in (`Command`), events and state out
   - `Town` becomes the client: input, panels, camera, drawing, sounds
   - the devctl verbs send commands too
3. Single player as `GameSession` with one player and `.d2s` persistence
   (saving is part of this).
4. `TcpTransport`: host plus clients. Then player-count scaling and
   party.
5. Later: the D2GS codec, a dedicated server, then Realm and Battle.net.

## The other routes considered

1. **Listen server, snapshots (chosen).** One player hosts, running
   the World; others are thin clients: they send commands and receive
   compact updates for the units near them, interpolating between ticks.
   Single player is the same thing with the host alone, as in D2. It
   tolerates latency and drop-in/drop-out, keeps the rules in one place,
   and a dedicated server later is just the host without a screen.
2. **D2-protocol compatible.** Speak game.exe's own packets so d2d and the
   original client can share a game. Most faithful, most work: every
   packet traced and reproduced exactly, the original's quirks included.
   Worth keeping possible (use D2's message shapes where cheap) rather than
   targeting first.
3. **Deterministic lockstep.** Every machine runs the whole simulation from
   the same inputs; only commands travel. Very little bandwidth, but one
   non-deterministic float or iteration order desyncs the game, everyone
   waits for the slowest player, and joining mid-game means replaying or
   snapshotting anyway. D2's seed-heavy rules make it tempting; the
   renderer-side floats and variable frame step make it fragile. Not
   recommended.
4. **Rollback.** Prediction with re-simulation, as fighting games do.
   Overkill for D2's pace.

## Steps worth taking before any networking

Cheap now, expensive later, and each one also makes single player more
faithful:
1. **The fixed 25 Hz tick** (game.exe's own rate, already used for
   animation frames and sound delays): simulation in ticks, drawing
   interpolated.
2. **Unit ids** instead of vector indices.
3. **A command layer** between input and the World (click → command),
   which the devctl verbs and tests can use too.
4. **World / client split** of `Town`: the `Fight`, `Loot` and monster AI
   subsystems already hold the simulation side; panels, camera and drawing
   are the client side.
5. **Per-unit and per-room seeds** instead of one shared `Rng`.
