# d2d hosting d2d over a LAN

A fork-side route to d2d hosting d2d clients, ahead of net-host.md's
game.exe-compatible host. It is net-host.md option (a): the in-process wire
forms (`Command`, protocol.hpp; `View`, replication.hpp) carried over TCP.
d2d clients only; game.exe can't join it. When the D2GS host (option (b))
lands, this can go.

## Why

The N-player World is done (PLAN.md "The N-player World": up to four
`World::Player`s, tagged commands, `join` / `leave`, scaling, party exp,
gold and quest credit). Local co-op already drives it in one process. What
a LAN of d2d machines lacks is only the transport.

## Stages

1. **Transport** (components/net/lan.hpp, tests/test_lan.cpp). Frames of
   u32 length, u8 kind, payload over `TcpConnection`, polled from the game
   loop. Kinds: Hello (version, name, the joiner's .d2s), Welcome (player
   id), Refuse, Command, View, Ping / Pong (liveness), Bye. Port 6113.
   The host takes three joiners (four players), times out a silent one after
   5 s, refuses another protocol version.
2. **`d2d --host`**: the host's World takes each joiner as a
   `World::Player` from its save, applies its commands tagged with its
   player, and sends it its own View each tick.
3. **`d2d --connect <ip>`**: the client renders from the host's View and
   sends its commands; the save comes back from the host on leave.
4. Disconnects, rejoins, saves, latency under impairment (`tc netem`), a
   30 minute four-player soak with devctl-driven clients.
5. Host cost with players on different levels: profile, then thread.
6. The TCP/IP menu's Host / Join for d2d hosts.

## Open questions

- Which side owns the joiner's save while it plays, and when does the host
  send it back (on leave only, or periodically)?
- View size with four players on four levels: today's deltas are about
  0.4 KB a tick for one player (PLAN.md "Views as deltas").
