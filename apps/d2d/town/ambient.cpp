// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): the Den's ambience and critters. The rest is in this directory.
#include "town.hpp"

#include "common.hpp"
#include "ingame.hpp"
#include "platform.hpp"
#include "scene.hpp"

#include <cues.hpp>
#include <log.hpp>
#include <monster_ids.hpp>
#include <outdoor.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <weather.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

auto Town::den_ambient() const -> int {
        if (!level || level->id != d2d::rules::DenQuest::kDen || !view.den_cleared || den_lit) return -1;
        return den_flash < 0 ? 80 : int(d2d::rules::cos512(den_flash * 128 / 30) * 80.0f);
    }

auto Town::den_tick(std::uint32_t frame_ms) -> void {
        if (view.den_cleared && !den_seen && level && level->id == d2d::rules::DenQuest::kDen) den_flash = 0;
        den_seen = view.den_cleared;
        if (!view.den_cleared) { den_flash = -1; den_lit = false; den_beams.clear(); return; }
        if (!den_lit && den_flash >= 0 && ++den_flash > 29) den_lit = true;
        if (!level || level->id != d2d::rules::DenQuest::kDen) { den_beams.clear(); return; }
        if (!den_lit || !den_beams.empty()) return;
        const auto found = scene->missiles.find("denofevillight");
        if (found == scene->missiles.end()) return;
        for (const auto& room : level->rooms)
            for (int tries = 0, made = 0; tries < 25 && made < 3; ++tries) {
                const int subtile_x = room.x * 5 + rain.rng(room.width * 5), subtile_y = room.y * 5 + rain.rng(room.height * 5);
                const float x = (float(subtile_x) + 0.5f) / 5, y = (float(subtile_y) + 0.5f) / 5;
                if (level->blocked_here(x, y, 0x05)) continue;
                den_beams.push_back({ &found->second, x, y, 0, frame_ms });
                ++made;
            }
        d2d::log::info("Den of Evil: {} light beams in {} rooms", den_beams.size(), level->rooms.size());
    }

auto Town::critter_tick(std::uint32_t frame_ms) -> void {
        namespace ids = d2d::rules::monster_ids;
        const auto& game_data = *world.game_data;
        if (!level) return;
        const auto& types = game_data.monsters.types;
        auto room_up = [&](const d2d::drlg::Outdoor::RoomSeed& room) {   // FUN_0046c460
            d2d::rules::Rng room_seed{ room.seed };
            for (const auto& critter : level->critters) {
                if (critter.row < 0 || std::size_t(critter.row) >= types.size()) break;
                if (int(room_seed.next() % 100) >= critter.percent) continue;
                const auto& type = types[std::size_t(critter.row)];
                int count = room_seed(type.max_grp - type.min_grp) + type.min_grp;
                if (critter.amount != 0) count *= critter.amount;
                for (; count > 0; --count)                 // FUN_0046c1a0: 10 tries at a free spot
                    for (int tries = 0; tries < 10; ++tries) {
                        const int subtile_x = room.x * 5 + room_seed(room.width * 5 - 1), subtile_y = room.y * 5 + room_seed(room.height * 5 - 1);
                        const float x = (float(subtile_x) + 0.5f) / 5, y = (float(subtile_y) + 0.5f) / 5;
                        if (level->blocked_here(x, y, 0x3f11)) continue;
                        // ponytail: its unit seed is the room's on from here; game.exe's comes with the unit (FUN_00465fd0), not traced.
                        critters.push_back({ critter.row, x, y, x, y, 4, false, false, 0, 0, frame_ms, d2d::rules::Rng{ room_seed.next() } });
                        break;
                    }
            }
        };
        if (level->id != critters_level) {
            critters.clear();                              // the last level's go at once, even while the camp waits
            std::optional<std::vector<d2d::drlg::Outdoor::RoomSeed>> camp;
            if (level == &game_data.town) {                // the camp keeps no rooms: made from the map seed
                camp = d2d::game::camp_room_seeds(game_data);
                if (!camp) return;                         // the builder's busy: next frame
            }
            critters_level = level->id;
            critter_rooms.assign(level->rooms.size(), false);
            // ponytail: the camp's come all at once (it keeps no rooms to come up); it's small.
            if (camp) for (const auto& room : *camp) room_up(room);
        }
        // FUN_0044c750: a room's critters as it comes up on the client, when
        // the host adds it; d2d's rooms come up the same way (Spawning's `up`).
        if (const auto state = world.fight.spawning.levels.find(level); state != world.fight.spawning.levels.end())
            for (std::size_t index = 0; index < state->second.up.size() && index < critter_rooms.size(); ++index)
                if (state->second.up[index] && !critter_rooms[index]) { critter_rooms[index] = true; room_up(level->rooms[index]); }
        const float player_x = view.player.x, player_y = view.player.y;
        // FUN_006416d0: subtiles apart less half of each one's size (a critter's 0, a unit's 2), (smaller + 2 x larger) / 2.
        auto apart = [](float from_x, float from_y, float to_x, float to_y) {
            const int across = std::max(std::abs(int(std::lround((to_x - from_x) * 5))) - 1, 0), down = std::max(std::abs(int(std::lround((to_y - from_y) * 5))) - 1, 0);
            return (std::min(across, down) + 2 * std::max(across, down)) / 2;
        };
        auto walk_to = [&](Critter& critter, float x, float y, int after) {   // FUN_004aff60: walk there, then wait
            critter.goal_x = x; critter.goal_y = y; critter.after = after;
            if (!critter.walking) critter.mode_ms = frame_ms;
            critter.walking = true;
        };
        auto wander = [&](Critter& critter, int radius, int after) {         // FUN_0046c960: a random corner `radius` subtiles off
            const float step = float(radius) / 5;
            const float dx = critter.seed.next() & 1 ? -step : step, dy = critter.seed.next() & 1 ? -step : step;
            walk_to(critter, critter.x + dx, critter.y + dy, after);
        };
        auto flee = [&](Critter& critter, int radius, int after) {           // FUN_0046c7d0: straight away, when the next subtile's free
            const float away_x = critter.x < player_x ? -1.f : critter.x > player_x ? 1.f : 0.f, away_y = critter.y < player_y ? -1.f : critter.y > player_y ? 1.f : 0.f;
            if (level->blocked_here(critter.x + away_x / 5, critter.y + away_y / 5, 0x3c01)) return false;
            walk_to(critter, critter.x + away_x * float(radius) / 5, critter.y + away_y * float(radius) / 5, after);
            return true;
        };
        auto stand = [&](Critter& critter, int frames) {
            critter.wait = frames;
            if (critter.walking) critter.mode_ms = frame_ms;
            critter.walking = false;
        };
        // FUN_0046c600: the nearest player, or monster other than a chicken or bunny (the town's NPCs too).
        auto nearest_other = [&](const Critter& critter) {
            int best = apart(critter.x, critter.y, player_x, player_y);
            for (const auto& other : view.others) best = std::min(best, apart(critter.x, critter.y, other.unit.x, other.unit.y));
            for (const auto& monster : view.monsters)
                if (monster.alive()) best = std::min(best, apart(critter.x, critter.y, monster.unit.x, monster.unit.y));
            for (std::size_t i = 0; i < view.npc_states.size() && i < level->npcs.size(); ++i)
                if (level->npcs[i].root != "objects" && !view.npc_states[i].hidden) best = std::min(best, apart(critter.x, critter.y, view.npc_states[i].x, view.npc_states[i].y));
            return best;
        };
        auto flies = [](int row) { return row == ids::kBird1 || row == ids::kBird2 || row == ids::kParrot; };
        for (auto& critter : critters) {
            if (critter.walking) {                          // at its Velocity (MonStats), stopped by what's in the way
                const float speed = std::size_t(critter.row) < game_data.mon_npc.size() ? d2d::game::cells_per_sec(game_data.mon_npc[std::size_t(critter.row)].velocity) * 0.04f : 0.1f;
                const float dx = critter.goal_x - critter.x, dy = critter.goal_y - critter.y, left = std::hypot(dx, dy);
                if (left > 0.01f) critter.dir = d2d::game::direction16(dx, dy);
                const float step = std::min(speed, left);
                const float next_x = critter.x + (left > 0 ? dx / left * step : 0), next_y = critter.y + (left > 0 ? dy / left * step : 0);
                if (left <= speed || (!flies(critter.row) && level->blocked_here(next_x, next_y, 0x3c01))) { stand(critter, critter.after); if (left <= speed) { critter.x = next_x; critter.y = next_y; } }
                else { critter.x = next_x; critter.y = next_y; }
                continue;
            }
            if (critter.wait > 0) { --critter.wait; continue; }
            const int player_gap = apart(critter.x, critter.y, player_x, player_y);
            if (player_gap > 30) { stand(critter, std::min(player_gap - 15, 200)); continue; }   // FUN_0046cbd0
            if (std::size_t(critter.row) < types.size() && types[std::size_t(critter.row)].zoo) {   // FUN_0046d660: MonStats zoo (no shipped critter)
                const bool walks = std::size_t(critter.row) < game_data.mon_npc.size() && game_data.mon_npc[std::size_t(critter.row)].modes >> 2 & 1;   // FUN_0046c140: has WL
                const int roll = walks ? critter.seed(100) : 0;
                if (!walks || roll < 40) stand(critter, 20);
                else if (roll < 60) walk_to(critter, player_x, player_y, 6);   // FUN_0046c770: to the player
                else if (roll < 80) flee(critter, 8, 4);
                else wander(critter, 8, 4);
                continue;
            }
            if (critter.row == ids::kRat || critter.row == ids::kScorpion || critter.row == ids::kLarva || critter.row == ids::kMiniSpider) {   // FUN_0046d110
                if (player_gap < 2 && int(critter.x * 5) == int(player_x * 5) && int(critter.y * 5) == int(player_y * 5)) { stand(critter, 0); continue; }   // FUN_0046cbf0
                if (player_gap < 4) { flee(critter, 6, 0); continue; }
                // FUN_0046c660 over the units near: a corpse 3..25 off and over 7
                // from the player, a coin flip each, the last one wins.
                // ponytail: d2d's monsters then the players' bodies; game.exe's goes by its rooms' unit lists.
                std::optional<std::pair<float, float>> corpse;
                auto consider = [&](float x, float y) {
                    const int gap = apart(critter.x, critter.y, x, y);
                    if (gap >= 3 && gap <= 25 && apart(x, y, player_x, player_y) > 7 && critter.seed(2) != 0) corpse = { x, y };
                };
                if (player_gap > 7) {
                    for (const auto& monster : view.monsters)
                        if (!monster.alive() && !monster.gone) consider(monster.unit.x, monster.unit.y);
                    for (const auto& body : view.bodies) consider(body.x, body.y);
                }
                if (corpse && !critter.marked) { walk_to(critter, corpse->first, corpse->second, 6); critter.marked = true; continue; }   // FUN_0046c770
                if (!critter.marked) { if (critter.seed(100) > 74) wander(critter, 5, 0); else stand(critter, 30); continue; }
                if (critter.seed(100) < 20) critter.marked = false;   // resting by a corpse, till one in five
                stand(critter, 32);
                continue;
            }
            if (flies(critter.row)) {                       // FUN_0046ce40: gone once it's flown and the player's past 25
                if (critter.marked && player_gap > 25) { critter.row = -1; continue; }
                critter.marked = true;
                // A spot 40 subtiles off the player on one axis, rand(40) on the
                // other, each signed by a coin; it flies 3 x that from where it is,
                // its path's distance capped at 77 (FUN_00648e70: 0x5a -> 0x4d).
                int across = 40, down = 40;
                if (critter.seed.next() & 1) down = critter.seed(40); else across = critter.seed(40);
                if (critter.seed.next() & 1) across = -across;
                if (critter.seed.next() & 1) down = -down;
                float to_x = 3 * (player_x + float(across) / 5) - 2 * critter.x, to_y = 3 * (player_y + float(down) / 5) - 2 * critter.y;
                const float length = std::hypot(to_x - critter.x, to_y - critter.y), most = 77.f / 5;
                // ponytail: cut short along the line; game.exe's path stops at its 77th subtile, same line for a flyer.
                if (length > most) { to_x = critter.x + (to_x - critter.x) * most / length; to_y = critter.y + (to_y - critter.y) * most / length; }
                walk_to(critter, to_x, to_y, 0);
                continue;
            }
            const int chance = int(critter.seed.next() % 100);
            switch (critter.row) {
            case ids::kChicken: case ids::kBug: case ids::kBunny: {   // FUN_0046ccd0 / FUN_0046d310
                const int flee_by = critter.row == ids::kBunny ? 6 : 4;
                if (nearest_other(critter) < flee_by) { if (!flee(critter, flee_by, 0x78)) wander(critter, flee_by, 0x78); }
                else if (chance < 30) wander(critter, 2, 0);
                else stand(critter, 5);
                break;
            }
            case ids::kBat:                                             // FUN_0046cdc0
                if (player_gap < 5) { if (chance < 66) flee(critter, 8, 0x1e); else wander(critter, 12, 0x1e); }
                else wander(critter, 9, 0);
                break;
            case ids::kMaggot:                                          // FUN_0046d070
                if (chance < 40) stand(critter, 20); else wander(critter, 4, 0);
                break;
            case ids::kSnake:                                           // FUN_0046d280
                if (player_gap < 2) flee(critter, 4, 0);
                else if (chance < 75) stand(critter, 32);
                else wander(critter, 4, 0);
                break;
            default: stand(critter, 25); break;                         // the fish: it stays
            }
        }
        std::erase_if(critters, [](const Critter& critter) { return critter.row < 0; });
    }

}  // namespace d2d::client
