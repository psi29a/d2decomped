# d2d as a network host (step 2)

Goal: d2d hosts an open (TCP/IP) game. Original game.exe clients and d2d clients
can both join. This builds on step 1 (d2d joins a game.exe host).

This doc covers only the host side. Two other docs cover the rest:

- `net-join.md`: the join handshake, save transfer and compression.
- `net-packets.md`: the byte layouts of each packet.

This doc says which packets the host has to *produce* and *consume*, not what
their bytes are. The existing decision in `multiplayer.md` still holds: one
listen server, `GameSession` / `PlayerSession`, and at most 8 players.

## 1. Single-player assumptions that block N players

Line numbers are for `act1-outdoors` at 1458f6f.

### World (`components/game/world.hpp` / `.cpp`)

| Where | What | N-player shape |
|---|---|---|
| world.hpp:5-7 | ponytail note: one player, shared rng | Per-game rng stays. Per-player state moves out. |
| world.hpp:128-132 | one `character`, `level`, `player`, merc | `PlayerSession` gets each one. |
| world.hpp:133-136 | `npc_states` / `other_npcs` keyed by *the* player's level | Per level in play (see below). |
| world.hpp:137 | `Rng rng` | Stays per game. game.exe also has one seed per game. |
| world.hpp:139-140 | `Loot`, `Fight` bound to the single character/player/merc | Per game, with player lookups by id. |
| world.hpp:141-146 | target_x, running, take_warp, interact_npc, pick_item, autoloot_gold | Per player. |
| world.hpp:148-150 | operated objects, doors | Per game. This is already right: object mode is shared (act1-end.md:728). |
| world.hpp:153-159 | `portal[4]`: [0][1] the player's town portal, [2][3] the Cairn Stones | TP pairs per player, keyed by owner (S→C 0x82). The Cairn portal stays per game. |
| world.hpp:162-170 | corpses, last_pmode, exp_lost, arrived_at, `talking` | Per player. |
| world.hpp:173-176 | held, store, hire_offers, `next_item_id` | Per player, except `next_item_id`, which is per game (see item ids below). |
| world.hpp:177-185 | quest records (den/andy/burial/tower/tools/cain), den_left, den_log_at, closed_at_join | Records stay per game. game.exe keeps one record per game and quest flag bits per player (quests.md). |
| world.hpp:187, world.cpp:48 | `quests()` reads the single character's bits | Takes a player argument. |
| world.hpp:338 | set_waypoint on the character | Per player. |
| world.hpp:391 | `tick(cmds)`: commands carry no player | `tick(span<Tagged{player_id, Command}>)`. |
| world.cpp:442-519 | `enter` / `new_game` reset game state on every enter (quest join, loot cleared) | Split into `create_game` (once) and `join(player)`, which runs the per-player quest join callback (+0xd4 in game.exe). |
| world.cpp:532-560 | swap_npcs when the player changes level | Levels load and unload by refcount, not on swap. |
| world.cpp:578-612 | cross_level moves the one player | Moves one `PlayerSession`. |
| world.cpp:1382 | `in_moor = level != town` | Per level. Town levels skip combat. |
| world.cpp:1395-1398 | player_modes / resurrect | Loops over players. |
| world.cpp:1509 | `fight.world(in_moor, ...)` on one level | Runs once for every level in play. |
| world.cpp:1519 | kills trigger quest hooks | The hook gets the killer and party (see party credit below). |
| world.cpp:1525 | `fight.rooms_up(*level, player.x, player.y)` | Runs for each player. Rooms are refcounted. |

### Fight, AI, loot, spawning (`components/game/`)

| Where | What | N-player shape |
|---|---|---|
| fight.hpp:118-125 | references to the single level/character/player/merc/rng | Per-player state goes into `PlayerSession`. |
| fight.hpp:127,133,137 | `monsters` for `mon_level` only. Other levels are `kept` (frozen). | One monster set per active level. |
| fight.hpp:134 | `amplified` array<2> | Per player plus per merc. |
| fight.hpp:139 | missiles on the current level only | Per level. |
| fight.hpp:147-236 | pmode, player_combat, psum, pets, charges, self_states, absorb, bo, merc_*, regen, boost | All per player. This is the bulk of the split. |
| fight.cpp:1132-1142 | `killed`: all kill exp goes to the one character, then `loot.drop` | Party exp split (see below). The merc takes its own share. |
| fight.cpp:1148-1192 | merc_turn picks targets relative to `player` | Relative to the merc's owner. |
| fight.cpp:2362-2470 | foes = {player, merc} at fixed indices 0 and 1, pets at 2+. Monsters update only within 30 cells of the player. | Foe list with owner ids. Update radius covers any player on the level. |
| ai.hpp:174-191, ai.cpp:502-510 | `Foe`. monster_update picks the nearest live foe. `aware` is a bool (ai.hpp:121). | Nearest-foe is already N-ready. `aware` stays a bool. Nothing is gained until an AI trace shows per-player awareness. |
| loot.hpp:33-35 | bound to the single character/player/rng | Takes a player argument. |
| loot.hpp:39,48 | GroundItem ids come from `next_id`, separate from `World::next_item_id` | One id space. Both are type-4 units on the wire, so ids must be unique across ground items and inventories. |
| loot.hpp:57-62 | floors keyed by the player's level | Keyed by level, independent of players. |
| loot.hpp:85-89 | the quest TC reads the single character's header. "no magic find, one player". | Per killer (FUN_005a6600). `players` comes from the game. |
| loot.hpp:146-167 | `take` puts items into the single character | Takes a player id. Gold split (see below). |
| gamedata.hpp:636-651,656; gamedata.cpp:487-527 | `Spawning` has a single `room_level` / `room`, and a single `player_moved` | One per player. Spawning feeds the room refcount. |
| gamedata (set_map_seed) | the act layout is process-global. Levels are mutated by `set_footprint` (doors). | Fine for one game per process. Doors are already per game. |
| rules/monsters.hpp:362-380 | `monster_stats`: HP and exp have no player scaling | Add the scaling tables (see below). |
| rules/drops.hpp:96-110 | the `players` param uses `1+(players-1)/2`, i.e. near=1 | Pass `near` and `players` separately (drops.md:45-75). |

### Transport and replication

| Where | What | N-player shape |
|---|---|---|
| protocol.hpp:20-93 | `Command`s address NPCs by `Level::npcs` index and store items by index | On the wire, game.exe sends unit ids. The server resolves ids. |
| protocol.hpp:231-242 | `LocalTransport`: a single client | Stays for single player. Network clients get a socket transport. |
| world.hpp:62-120 | `View` is a one-player view (player, merc, own character) | Not used for network clients. See below. |
| replication.hpp:110-142,305 | `ViewEncoder` / `encode_view` / `apply_view`: one encoder per client, d2d-private deltas | Stays for the in-process path only. |
| apps/d2d/town.hpp:120-139, town.cpp:249-260,700-722 | `Town` owns `World`, `LocalTransport` and `ViewEncoder`, and drives the tick | The host owns a `GameSession`. `Town` becomes one client of it. |
| apps/d2d/main.cpp:12,714-732,827 | `--headless` needs `--devctl` and uses the SDL dummy driver | See section 4 (dedicated server). |

## 2. What game.exe does with several players

All addresses are 1.14d game.exe.

**Player count.** FUN_00535790(game):

- Counts players with FUN_005538d0, which walks the game+0x1120 player hash (128 buckets) and skips units where FUN_00639df0(unit,7) is nonzero.
- `/players N` sets DAT_00883d70 through FUN_00535780, capped below 9. The client calls it from FUN_0047c420.
- The `/players` value applies only when game type +0x6a is in 1..3, and only when it is larger than the real count.
- Game type 3 is single player. Type 0 is presumably open/TCP (open question 1).

**Monster scaling.** Stat setup is FUN_00573cb0, which calls FUN_00573930.

- HP% table at 0x6e1590 and exp% table at 0x6e15b4, both `[0,0,50,100,150,200,250,300,350]`, indexed by player count.
- For n > 8: HP% = (n−2)·50 and exp% = (5n+130)·2.
- HP = roll + MulDiv(roll, hp%, 100), capped at 0x7fffff, then <<8, stored in stats 6 and 7.
- Exp (stat 13) = exp + MulDiv(exp, exp%, 100).
- Stat 100 (monster_playercount) is set to n.
- MulDiv is FUN_00483360.
- Monsters with +0x4c set skip scaling and use n = 1.
- Other callers of the scaling: 0x5e8810, 0x5e9750, 0x5f0b00, 0x5fc450.
- Scaling happens when a monster spawns. Monsters already alive keep their stats when players join or leave.

**Party exp.** FUN_0057e990(game, killer, monster), called from monster death FUN_005a4ef0.

1. Resolve the killer. A pet resolves to its owner (FUN_0057e7b0).
2. The merc (FUN_00574ec0(7)) gets its own share (FUN_0057e860).
3. If the killer has no party, it gets the normal award.
4. If the killer is in a party (FUN_00554630 != 0xffff), FUN_0057e6c0 collects members that are:
   - on the same level (FUN_005405a0)
   - alive (FUN_005541b0 == 0)
   - within dist² ≤ 0x1900 subtiles of the monster (80 subtiles, 16 cells)
   - at most 8
5. One member gets the normal award. Otherwise total = exp + (((n−1)·exp·89) >> 8), and each member gets total · clvl_i / Σclvl (float).
6. Each award goes through FUN_0057e480 (level-difference penalty, plus the stat 0x55 exp bonus), then FUN_0057e510 (adds to stat 13, caps at max, sets stat 0x1d, handles level-up).

**Gold split.** FUN_0055c850 calls FUN_00540900 when a player picks up gold.

- The split applies when the item has no player owner (FUN_00552fd0) and at least 2 party members are on the level.
- The gold is divided evenly among alive members on the level (mode != 0x11).
- Each share is capped at that member's max gold (FUN_00622e70).

**NoDrop.** n = near + (players−near)/2 (drops.md:45-75).

- `near` comes from FUN_005408e0, which counts party members on the killer's level (FUN_005405a0, FUN_005404f0).
- `players` comes from FUN_00535790.

**Death gold penalty.** FUN_005357d0 checks game type == 3. The multiplayer rule differs, and the corpse carries the gold (open question 1).

**Parties.**

- The party list lives at game+0x1d2c.
- FUN_00540510(game, partyId, cb, arg) iterates it. FUN_00540290 looks up a party. FUN_00540890 sums member levels.
- On the wire: C→S 0x5d (hostile) and 0x5e (party), S→C 0x8b-0x8e (relationship, party, corpse). See network.md.

**Quest credit.**

- One quest record per game, with flag bits per player (quests.md).
- Party credit goes through FUN_00540510 callbacks at 0x58a2a0, 0x58c7e0, 0x591490, 0x593130, 0x593710 and others (quests-act1.md).
- Some quests use area credit instead. The Countess gives credit to anyone in cellar 5.
- The quest drop TC is per killer. FUN_005a6600 checks the killer's own quest bits (+0x9e / +0x9f, bits 15 and 1).

**Room activation.** FUN_0061b6f0(new, old):

- Refcounts rooms per depth (room2 +0xc, one u16 per depth 0..3; status at +0x44). It walks the near list three deep (FUN_0061b390 / FUN_0061b490).
- Acquire callbacks at 0x744384. Depth 1 (61b2d0) builds the room1.
- Release goes through FUN_0061b5b0, then the callbacks at 0x744394, then FUN_0061b4f0. Status 4 frees the tiles (FUN_0066f1a0).
- Every player's movement adds and removes refs, so this is N-player by design.
- Every 12 frames FUN_0052d240 counts idle checks per room1 (FUN_0061a790). A room is idle while room1+0x78, its player count, is zero.
- After more than 10 idle checks (about 4.8 s), each unit in room+0x74 (next +0xe8) goes to FUN_005433f0, and the room1 is freed (FUN_0061a910).
- FUN_005433f0 sends monsters to FUN_005431f0, believed to be the unit proxy store that brings them back when the room reloads (unverified).
- So levels with no players cool down and unload. No level is frozen while occupied.

**Client interest.**

- FUN_0053b000 walks each act's rooms, then each room's +0x1c client list (next +0xe0), calling FUN_00553220. This is believed to be the per-room client update (unverified).
- FUN_0053a820 runs FUN_0061a2c0 per room.
- Clients get updates only for rooms they have loaded. This is the model the host's packet emitter should copy.

**Frame loop.**

- FUN_0052fc20 runs frames. FUN_0052d870 is one frame. FUN_0052fd90 flushes client queues every 40 ms.
- FUN_0052d440 updates units and contains the per-client join state machine ("[JOIN 6] SCMD_STARTACT"). game+0x88 is the client list, with client state at +4 (3/4/5).
- d2d's 25 Hz `kTickMs` already matches.

## 3. Staged plan

### Options

**(a) d2d↔d2d first, over a compact d2d protocol.** Put `Command` plus `ViewEncoder` deltas on a socket. Add game.exe compatibility later as a second codec.

- For: fast demo, and it reuses replication.hpp as-is.
- Against:
  - The View is a one-player snapshot. It would need redesigning for N players anyway.
  - It adds a second wire protocol to keep in sync.
  - It gives no help with goal 1 (joining the daughter's game.exe host) or goal 2's game.exe clients.
  - d2d-only bugs would hide behind a lenient protocol.

**(b) game.exe's D2GS protocol from the start.** It is the only network protocol.

- For:
  - Step 1 already needs S→C decode and C→S encode, and a real game.exe host is a free oracle for testing them.
  - Step 2 adds the other two halves (C→S decode, S→C encode) and tests them against a real game.exe *client*.
  - One codec, one set of packet tests, and d2d clients behave exactly like game.exe clients.
- Against:
  - The first d2d↔d2d game arrives later.
  - Item bitstreams and unit ids must be exact before anything works with game.exe.

### Recommendation: (b)

Build one symmetric D2GS codec. Keep the in-process `LocalTransport` / `View` path for single player. Converge the two later only if it pays for itself.

How the codec is shared with step 1:

```
components/net/d2gs/        one library, both directions per packet
  c2s.{hpp,cpp}   encode (step 1 client)      decode (step 2 host)
  s2c.{hpp,cpp}   decode (step 1 client)      encode (step 2 host)
  items.{hpp,cpp} item bitstream read (step 1) / write (step 2)
  frame.{hpp,cpp} framing + compression (from net-join.md)
components/net/tcp_transport.{hpp,cpp}   socket, used by both
```

- Every packet gets a round-trip test: `decode(encode(x)) == x`, plus fixtures captured from real game.exe sessions. Fixtures must not contain Blizzard data bytes. Record ids and unit fields only.
- **Client side (step 1):** a `ClientModel` folds the S→C stream into the state that `Town` renders. It replaces `apply_view` for network games.
- **Host side (step 2):** one `PacketEmitter` per client. It walks the rooms that client has loaded, the same way FUN_0053b000 does, and turns World changes into S→C packets. It replaces `ViewEncoder` for network clients.
- A d2d client joining a d2d host uses exactly the same `ClientModel`. Nothing on that path is d2d-specific.
- The single-player path is untouched until the N-player World lands.

### Stages

1. **N-player World, still in process.** Split into `GameSession` and `PlayerSession`, with tagged commands. Test with two local players driven by devctl, rendering player 1 only. No networking yet.
2. **Game rules for N.** Refcounted room activation, scaling, party exp and gold split, per-killer quest TC, party credit.
3. **Host a d2d client.** Use the step 1 codec and `ClientModel` plus the new `PacketEmitter`.
4. **Host a game.exe client.** Same code. Only exactness bugs should remain.
5. **Dedicated server** (section 4).

## 4. Dedicated / headless server (`d2ds`)

- New app `apps/d2ds`: no SDL and no renderer.
  - `load_game_data` already works without graphics (`Scene : GameData`).
  - It owns a `GameSession`, a TCP listener, and the 25 Hz tick (sleep until the next 40 ms boundary).
  - Command line: `--port 4000` (game.exe's TCP/IP port), `--difficulty`, `--expansion`, game name, and a password (see net-join.md).
- One game per process to start with.
  - `set_map_seed` is process-global, so a second game needs either its own process or that global state moved into the game.
  - A lobby supervisor that spawns `d2ds` per game is enough for step 3 (Battle.net).
- Saves: the joining client supplies its character at join time (net-join.md), as open games do. The host writes nothing back beyond what game.exe's open host does (open question 4).
- Admin: reuse the devctl socket for kick, say, `/players` and state dumps. There is no separate admin protocol.
- The listen server is `d2d --host`: the same `GameSession` with `Town` connected as a local client over `LocalTransport`.
- `--headless` stays a testing tool for the client and is not the server.

## 5. Work items (in order, rough sizes)

Sizes assume one focused developer (or agent) with the existing test harness.

| # | Item | Size |
|---|---|---|
| 1 | `PlayerSession` / `GameSession` split. Move per-player Fight state (fight.hpp:147-236) and World targets/portals/corpses/held/store/talking into `PlayerSession`. Foe list with owners. Tagged commands. `quests(player)`. Split `create_game` from `join`. | 1.5-2 weeks. It touches most of world.cpp and fight.cpp (~160 `player.` / ~180 `character.` sites). |
| 2 | Room activation for N: refcounted rooms from each player's `Spawning`, all occupied levels simulated, idle unload after about 4.8 s, per-level monsters and missiles (the `kept` freeze becomes cool-down). | 1 week |
| 3 | Unify item ids (loot.hpp:48 and world.hpp:176) into one per-game unit id allocator shared with monsters, objects and missiles as game.exe numbers them (check against net-packets.md). | 2-3 days |
| 4 | Player-count scaling: HP/exp tables, stat 100, NoDrop with near and players, `/players` for game types 1-3. Tests against the tables in section 2. | 2 days |
| 5 | Parties: C→S 0x5d/0x5e state, party exp split (FUN_0057e6c0), merc share, gold split (FUN_00540900), party and area quest credit, per-killer quest TC, per-player TP ownership. | 1 week |
| 6 | D2GS codec plus TCP transport, done as step 1. Here only add the missing halves (C→S decode, S→C encode, item bitstream write). | Step 1 cost, plus 1-1.5 weeks |
| 7 | `PacketEmitter`: per-client room interest, unit enter/leave, stat/mode/item deltas, join sequence (state machine as in FUN_0052d440). | 1.5-2 weeks |
| 8 | Host join/leave: accept, handshake, character load, drop the player on disconnect (corpse and items per game.exe rules). Depends on net-join.md. | 1 week |
| 9 | Guard content d2d can't build yet: refuse waypoints/warps into Acts 2-5 for every client, with a server message. | 1-2 days |
| 10 | `apps/d2ds` plus `d2d --host`: listener, tick loop, devctl admin. | 3-4 days |

Items 1-5 are independent of networking and can land on their own with in-process tests.

## 6. Risks

1. **Exactness toward a game.exe client.** Item bitstreams, unit ids, object and monster placement, and DRLG must match or the client desyncs or crashes. Act 1 layout parity is proven. Item write and id allocation are not.
2. **Content gaps.** A game.exe client can reach Acts 2-5 (waypoints, quest portals) that d2d can't build. They must be blocked on the server (item 9).
3. **Join handshake and compression.** Those are owned by net-join.md. The host is blocked on them.
4. **Client prediction and timing.** game.exe predicts its own walking and expects the server's corrections at specific moments. Wrong timing looks like rubber-banding. Check with net-packets.md and the C→S prediction research.
5. **Simulation cost.** Up to 8 players on 8 different levels means 8 levels of monsters, missiles and AI per tick. Profile after item 2.

## 7. Open questions

1. What exactly is game type 0 (+0x6a)? Does `/players` stay ignored (it applies only for 1-3), and what are the death gold rule and corpse handling for open games (FUN_005357d0 checks 3 only)?
2. Is FUN_005431f0 really the unit proxy store? Unloaded rooms' monsters need to come back with the same HP and mode.
3. Is FUN_00553220 the per-room client update, and what does a client get when a room enters or leaves its interest?
4. What does an open-game host send back to the client's save (exp, items, quests), and when? This is in net-join.md's scope. The host needs the answer.
5. When a player leaves, does game.exe rescale anything live? The expectation is no, since scaling happens only at spawn.
6. Do monsters pick targets by anything besides nearest foe (threat, last attacker) in multiplayer? This needs an AI trace before `aware` changes.
