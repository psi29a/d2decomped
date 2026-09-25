# Multiplayer — impact and routes

Status: design notes, nothing built. Written 2026-09-25 against the code as
of commit ffc527c. The game.exe research this needs (the packet tables, the
server/client tick) is listed under "Later research" in `docs/PLAN.md`.

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

What would have to change:

| Today | For multiplayer |
|---|---|
| The simulation steps on the frame clock (~60 fps, variable `dt`, ms timers) | A fixed 25 Hz tick; timers in ticks; drawing interpolates between ticks |
| `Town` mixes input, UI panels, simulation and drawing | Split: a `World` (units, levels, rng — the server's) and a client (input, panels, camera, drawing) |
| Units are vector indices (`monsters[i]`, hover encoded as `-10 - i`) | Stable unit ids, as D2's unit GUIDs |
| One player, one merc (`Foe` array of two, `cc` the character) | N players and mercs; monster targeting, party experience, loot ownership |
| One shared `Rng` for every roll | Server-owned seeds (D2 keeps per-unit and per-room seeds) so results are reproducible and not client-decided |
| Clicks call game code directly (`fight.attack_mon = ...`) | Clicks become commands ("attack unit id with skill"), applied by the World |
| Sounds and effects are cued locally (`Cues`) | Events sent from the World (hit, death, drop), the client picks sounds and effects |
| Saves are read, never written | The server writes characters (.d2s) |
| Monster stats ignore player count | HP / experience / NoDrop scale with players (D2's /players setting) |

## Routes

1. **Listen server, snapshots (recommended).** One player hosts, running
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
