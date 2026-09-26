// What a client asks the game server for: its intents, the shapes of
// game.exe's client -> server packets (docs/research/re/network.md). The
// server validates and applies them in its tick; nothing else from the
// client touches the world. docs/design/multiplayer.md.
#pragma once

#include <variant>

namespace {

namespace cmd {
// 0x01 / 0x03: walk or run to (x, y) cells. `fresh`: a new click, not a
// held button re-aiming (it drops the attack or pick-up under way).
struct Move { float x = 0, y = 0; bool fresh = false; };
// 0x05..0x11: a skill at (x, y), or on `unit` (a monster id; -1 none).
// A plain attack is skill 0.
struct UseSkill { int skill = 0; float x = 0, y = 0; int unit = -1; };
// 0x13: interact with an object or NPC (walk up, then operate / talk).
// ponytail: by its Level::npcs index until objects get unit ids.
struct Interact { int npc = -1; };
// 0x16: pick up a ground item (walk up, then take it).
// ponytail: by its Loot::ground index until items get unit ids.
struct Pickup { int item = -1; };
}  // namespace cmd

using Command = std::variant<cmd::Move, cmd::UseSkill, cmd::Interact, cmd::Pickup>;

}  // namespace
