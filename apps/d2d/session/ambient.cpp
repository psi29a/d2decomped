// SPDX-License-Identifier: GPL-3.0-or-later
// ClientSession (session.hpp): the Den's ambience, the client's own fill and critters. The rest is in this directory.
#include "session.hpp"

#include "common.hpp"
#include "netgame.hpp"
#include "platform.hpp"
#include "render/ingame.hpp"
#include "scene.hpp"

#include <ambient_dip.hpp>
#include <critters.hpp>
#include <cues.hpp>
#include <level.hpp>
#include <light.hpp>
#include <log.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
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

auto ClientSession::den_ambient() const -> int {
        if (!level || level->id != d2d::rules::DenQuest::kDen || !view.den_cleared || den_lit) return -1;
        return den_flash < 0 ? 80 : int(d2d::rules::cos512(den_flash * 128 / 30) * 80.0f);
    }

auto ClientSession::event_ambient() const -> int {
        if (const int den = den_ambient(); den >= 0) return den;
        if (!level) return -1;
        if (const int red = d2d::rules::release_light(level->id, view.diablo_red || (net_game && net_game->diablo_red)); red >= 0) return red;
        return dip.light(level->id);
    }

// A joined game's 0x89 event 1 starts one (ClientSession's net step, 0x46aee0), as
// does state 153 set on a unit (0xa8 / 0xaa, FUN_004d87e0).
auto ClientSession::dip_tick() -> void {
        if (level) dip.step(level->light >= 0 ? level->light : view.day.intensity(), level->id);   // FUN_00474550 flag 0
    }

auto ClientSession::den_tick(std::uint32_t frame_ms) -> void {
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

auto ClientSession::critter_tick(std::uint32_t frame_ms) -> void {
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
                        // Its unit seed: {the room seed's next step, 666}, as the client unit maker takes it (FUN_00465fd0 at 0x46606a).
                        critters.push_back({ critter.row, x, y, x, y, 4, false, false, 0, 0, frame_ms, d2d::rules::Rng{ room_seed.next() } });
                        break;
                    }
            }
        };
        if (level->id != critters_level) {
            critters.clear();                              // the last level's go at once
            critters_level = level->id;
            critter_rooms.clear();
            client_rooms = {};
        }
        // FUN_0044c750: a room's critters as it comes up on the client, when
        // the host adds it; d2d's rooms come up the same way (Spawning's `up`).
        // The camp keeps no rooms: a room's seed is its DRLG room's (by
        // level_room's index, as keep_camp_seeds), made from the map seed.
        if (const auto state = world.fight.spawning.levels.find(level); state != world.fight.spawning.levels.end()) {
            const auto& rooms_up = state->second.rooms_up;
            if (critter_rooms.size() < rooms_up.size()) critter_rooms.resize(rooms_up.size(), false);
            std::optional<std::vector<d2d::drlg::Outdoor::RoomSeed>> camp;
            for (std::size_t index = 0; index < rooms_up.size(); ++index) {
                if (!rooms_up[index] || critter_rooms[index]) continue;
                if (level != &game_data.town) {
                    critter_rooms[index] = true;
                    if (index < level->rooms.size()) room_up(level->rooms[index]);
                    continue;
                }
                if (!camp) camp = d2d::game::camp_room_seeds(game_data);
                if (!camp) break;                          // the builder's busy: next frame
                critter_rooms[index] = true;
                const auto rect = d2d::game::level_room(*level, index);
                if (!rect) continue;
                const auto seeded = std::ranges::find_if(*camp, [&](const d2d::drlg::Outdoor::RoomSeed& room) { return room.x == rect->x && room.y == rect->y; });
                if (seeded != camp->end()) room_up(*seeded);
            }
        }
        const float player_x = view.player.x, player_y = view.player.y;
        // The client's room lists: a unit is filed at its room's head as it comes in or moves rooms.
        enum : int { kFiledPlayer = 0, kFiledMonster = 1, kFiledBody = 2 };
        const auto file = [&](int kind, int id, float x, float y) -> const d2d::rules::ClientRooms::Filed& {
            return client_rooms.file(kind, id, level->rooms.empty() ? -1 : d2d::game::room_of(*level, x, y));
        };
        for (const auto& monster : view.monsters) (void)file(kFiledMonster, monster.id, monster.unit.x, monster.unit.y);
        for (const auto& other : view.others) (void)file(kFiledPlayer, int(other.id), other.unit.x, other.unit.y);
        for (const auto& body : view.bodies) (void)file(kFiledBody, body.id, body.x, body.y);
        client_rooms.drop_unseen();                         // the ones gone from view: new when they come back
        // FUN_006416d0: subtiles apart less half of each one's size (a critter's 0, a unit's 2), (smaller + 2 x larger) / 2.
        auto apart = [](float from_x, float from_y, float to_x, float to_y) {
            const int across = std::max(std::abs(int(std::lround((to_x - from_x) * 5))) - 1, 0), down = std::max(std::abs(int(std::lround((to_y - from_y) * 5))) - 1, 0);
            return (std::min(across, down) + 2 * std::max(across, down)) / 2;
        };
        auto stand = [&](Critter& critter, int frames) {
            critter.wait = frames;
            if (critter.walking) critter.mode_ms = frame_ms;
            critter.walking = false;
            critter.path.clear();
        };
        auto flies = [](int row) { return row == ids::kBird1 || row == ids::kBird2 || row == ids::kParrot; };
        // A critter's collision (FUN_0064d910 with its path's mask +0x50 and pattern +0x48: its
        // MonStats flying / opendoors, MonStats2 SizeX): its subtile, the plus, or the 3x3 box.
        auto blocks = [&](const Critter& critter, int sub_x, int sub_y) {
            const bool known = std::size_t(critter.row) < game_data.mon_npc.size();
            const auto shape = known ? game_data.mon_npc[std::size_t(critter.row)].shape : d2d::game::UnitShape{ 1, 2, 0x3c01 };
            auto blocked_off = [&](int off_x, int off_y) { return level->blocked_here((float(sub_x + off_x) + 0.5f) / 5, (float(sub_y + off_y) + 0.5f) / 5, std::uint16_t(shape.mask & 0x0fff)); };
            if (blocked_off(0, 0)) return true;
            if (shape.pattern == 0) return false;
            if (blocked_off(-1, 0) || blocked_off(1, 0) || blocked_off(0, -1) || blocked_off(0, 1)) return true;
            return shape.box() && (blocked_off(-1, -1) || blocked_off(1, -1) || blocked_off(-1, 1) || blocked_off(1, 1));
        };
        // FUN_004aff60, command 1: the walk's path (FUN_004804a0 -> FUN_00649970,
        // rules::critter_path; steps +0x91: 0xe, a bird's 0x4d), to a spot in its
        // room or one near it (FUN_00463740; the camp keeps no rooms: its level).
        // No path: neutral (LAB_004b0d33), the wait unset.
        auto walk_to = [&](Critter& critter, float x, float y, int after) {
            const int from_x = int(std::floor(critter.x * 5)), from_y = int(std::floor(critter.y * 5));
            const int to_x = int(std::floor(x * 5)), to_y = int(std::floor(y * 5));
            bool roomed = level->inside(x, y);
            if (roomed && !level->rooms.empty()) {
                const int room = d2d::game::room_of(*level, x, y);
                const auto close_by = d2d::game::near_rooms(game_data, *level, critter.x, critter.y);
                roomed = std::ranges::any_of(close_by, [&](const auto& entry) { return entry.first == level && entry.second == room; });
            }
            auto path = roomed ? d2d::rules::critter_path(from_x, from_y, to_x, to_y, flies(critter.row) ? 0x4d : 0xe,
                                                          [&](int sub_x, int sub_y) { return blocks(critter, sub_x, sub_y); })
                               : std::vector<std::pair<int, int>>{};
            if (path.empty()) { stand(critter, 0); return; }
            critter.path = std::move(path);
            critter.goal_x = (float(critter.path.front().first) + 0.5f) / 5;
            critter.goal_y = (float(critter.path.front().second) + 0.5f) / 5;
            critter.after = after;
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
        // FUN_0046c600: the nearest player, or monster other than a chicken or bunny (the town's NPCs too),
        // dead or alive (critter_neighbour). d2d's critters don't die.
        auto nearest_other = [&](const Critter& critter) {
            int best = apart(critter.x, critter.y, player_x, player_y);
            for (const auto& other : view.others) best = std::min(best, apart(critter.x, critter.y, other.unit.x, other.unit.y));
            for (const auto& monster : view.monsters)
                if (d2d::rules::critter_neighbour(false, d2d::rules::kCritterMonster, monster.type)) best = std::min(best, apart(critter.x, critter.y, monster.unit.x, monster.unit.y));
            for (std::size_t i = 0; i < view.npc_states.size() && i < level->npcs.size(); ++i)
                if (level->npcs[i].root != "objects" && !view.npc_states[i].hidden) best = std::min(best, apart(critter.x, critter.y, view.npc_states[i].x, view.npc_states[i].y));
            return best;
        };
        for (auto& critter : critters) {
            if (critter.walking) {                          // its path's points at its Velocity (MonStats), stopped by what's in the way
                const float speed = std::size_t(critter.row) < game_data.mon_npc.size() ? d2d::game::cells_per_sec(game_data.mon_npc[std::size_t(critter.row)].velocity) * 0.04f : 0.1f;
                const float dx = critter.goal_x - critter.x, dy = critter.goal_y - critter.y, left = std::hypot(dx, dy);
                if (left > 0.01f) critter.dir = d2d::game::direction16(dx, dy);
                const float step = std::min(speed, left);
                const float next_x = critter.x + (left > 0 ? dx / left * step : 0), next_y = critter.y + (left > 0 ? dy / left * step : 0);
                if (left > speed && blocks(critter, int(std::floor(next_x * 5)), int(std::floor(next_y * 5)))) { stand(critter, critter.after); continue; }
                critter.x = next_x; critter.y = next_y;
                if (left > speed) continue;
                critter.path.erase(critter.path.begin());   // the point reached: on to the next, or done
                if (critter.path.empty()) { stand(critter, critter.after); continue; }
                critter.goal_x = (float(critter.path.front().first) + 0.5f) / 5;
                critter.goal_y = (float(critter.path.front().second) + 0.5f) / 5;
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
                // FUN_0046c570 over the critter's room's near list (FUN_00463740's), each room's
                // units newest first (+0x74, next +0xe8), FUN_0046c660 on each: a player in mode
                // 0 or 0x11 or a monster in 0 (DT) or 0xc (DD) 3..25 off and over 7 from the
                // player, a coin flip each; the last one wins.
                std::optional<std::pair<float, float>> corpse;
                auto consider = [&](float x, float y) {
                    const int gap = apart(critter.x, critter.y, x, y);
                    if (gap >= 3 && gap <= 25 && apart(x, y, player_x, player_y) > 7 && critter.seed(2) != 0) corpse = { x, y };
                };
                if (player_gap > 7) {
                    std::vector<std::pair<float, float>> spots;
                    std::vector<d2d::rules::RoomUnit> dead;
                    for (const auto& monster : view.monsters)
                        if (!monster.gone && (monster.mode == "DT" || monster.mode == "DD")) {
                            const auto& filed = file(kFiledMonster, monster.id, monster.unit.x, monster.unit.y);
                            spots.emplace_back(monster.unit.x, monster.unit.y);
                            dead.push_back({ filed.room, true, filed.stamp });
                        }
                    for (const auto& other : view.others)
                        if (other.mode == 0 || other.mode == 0x11) {
                            const auto& filed = file(kFiledPlayer, int(other.id), other.unit.x, other.unit.y);
                            spots.emplace_back(other.unit.x, other.unit.y);
                            dead.push_back({ filed.room, true, filed.stamp });
                        }
                    for (const auto& body : view.bodies) {
                        const auto& filed = file(kFiledBody, body.id, body.x, body.y);
                        spots.emplace_back(body.x, body.y);
                        dead.push_back({ filed.room, true, filed.stamp });
                    }
                    std::vector<int> rooms{ -1 };                   // no rooms kept: one list
                    if (!level->rooms.empty()) {
                        rooms.clear();
                        for (const auto& [near_level, near_room] : d2d::game::near_rooms(game_data, *level, critter.x, critter.y))
                            if (near_level == level) rooms.push_back(near_room);
                    }
                    for (const int index : d2d::rules::room_order(dead, rooms)) consider(spots[std::size_t(index)].first, spots[std::size_t(index)].second);
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
                // other, each signed by a coin; it flies to 3 x that less 2 x where
                // it is, on its path (walk_to: 0x4d steps, FUN_00648e70(0x5a)).
                int across = 40, down = 40;
                if (critter.seed.next() & 1) down = critter.seed(40); else across = critter.seed(40);
                if (critter.seed.next() & 1) across = -across;
                if (critter.seed.next() & 1) down = -down;
                const float to_x = 3 * (player_x + float(across) / 5) - 2 * critter.x, to_y = 3 * (player_y + float(down) / 5) - 2 * critter.y;
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
