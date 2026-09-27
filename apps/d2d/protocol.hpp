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
// 0x05..0x0a (`left`) / 0x0c..0x11: a skill at (x, y), or on `unit` (a
// monster id; -1 none). A plain attack is skill 0. The left one sent
// again while the button's held keeps an attack going.
struct UseSkill { int skill = 0; float x = 0, y = 0; int unit = -1; bool left = false; };
// 0x13: interact with an object or NPC (walk up, then operate / talk).
// ponytail: by its Level::npcs index until objects get unit ids.
struct Interact { int npc = -1; };
// 0x16: pick up a ground item (walk up, then take it).
// ponytail: by its Loot::ground index until items get unit ids.
struct Pickup { int item = -1; };
// 0x41: back in town after dying (let through only when dead).
struct Resurrect {};
// 0x3a: spend `count` stat points on a stat (0 str, 1 energy, 2 dex, 3 vit).
struct StatPoint { int stat = 0, count = 1; };
// 0x3b: spend a skill point on the class's skill `skill` (its index 0..29,
// the save's order; game.exe's packet names the Skills.txt id).
struct SkillPoint { int skill = 0; };
// 0x3c: put a skill on the left or right button (a right-button aura runs).
struct SelectSkill { int skill = 0; bool left = false; };
// 0x26: use (drink) the belt's column `slot`.
struct UseBelt { int slot = 0; };
}  // namespace cmd

using Command = std::variant<cmd::Move, cmd::UseSkill, cmd::Interact, cmd::Pickup, cmd::Resurrect,
                             cmd::StatPoint, cmd::SkillPoint, cmd::SelectSkill, cmd::UseBelt>;

}  // namespace
