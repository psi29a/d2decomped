// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for world.hpp: the World, the game server and its 25 Hz tick.
// This part: players: join, leave, a new game, ghosts and the merc. The rest of the World
// (world.hpp) is in this directory, by subsystem.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "inventory.hpp"
#include "log.hpp"

#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <monster_ids.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

// `level`'s units states, the ones its rooms made since (populate) added as they start.
void grow_states(std::vector<UnitState>& states, const Level& level) {
    if (states.size() >= level.npcs.size()) return;
    const auto fresh = npc_start(level);
    states.insert(states.end(), fresh.begin() + std::ptrdiff_t(states.size()), fresh.end());
}

// Levels.txt Waypoint: a level's index in the waypoint bits (FUN_00660e00), -1 none.
int waypoint_index(const GameData& game_data, int level_id) {
    for (const auto& act_levels : game_data.waypoint_levels)
        if (const auto found = std::ranges::find(act_levels, level_id, &GameData::WaypointLevel::level); found != act_levels.end()) return found->waypoint;
    return -1;
}

auto World::act_as(Player& player) -> void {
    actor = &player;
    fight.act_as(player);
}

auto World::add_player() -> Player& {
    auto& player = *players.emplace_back(std::make_unique<Player>());
    player.id = next_player_id++;                    // a unit id: never reused in a game, so no quest list holds a leaver's
    // The first player's town portal is [0] / [1]; the others' the first free pair from [8].
    for (std::size_t pair = kGamePortals; players.size() > 1; pair += 2)
        if (std::ranges::none_of(players, [&](const auto& other) { return other.get() != &player && other->portal_pair == pair; })) {
            player.portal_pair = pair;
            if (portal.size() < pair + 2) portal.resize(pair + 2);
            break;
        }
    fight.heroes.push_back(&player);
    return player;
}

auto World::anyone_alive() const -> bool {
    return std::ranges::any_of(players, [](const auto& player) { return player->pmode != kModeDT && player->pmode != kModeDD; });
}

auto World::become_ghost(Player& player) -> void {
    player.ghost = true;
    player.body = Player::Body{ level, player.player.x, player.player.y, player.player.dir, fight.gfx() };
    player.raising = -1;
    d2d::log::info("{} is a ghost", player.character.name);
}

auto World::revive(Player& player, std::uint32_t now_ms) -> void {
    Player& was = *actor;
    act_as(player);
    player.ghost = false;
    player.body.reset();
    player.last_pmode = -1;
    player.player.walking = false; player.player.path.clear();
    player.target_x = player.player.x; player.target_y = player.player.y;
    fight.revive(now_ms);                                // full life and mana, no potions or poison working
    d2d::log::info("{} is alive again", player.character.name);
    act_as(was);
}

auto World::raise_step(float elapsed, std::uint32_t now_ms) -> void {
    for (auto& lying : players) if (lying->body) lying->body->raisers = 0;
    for (auto& raiser : players) {
        if (raiser->raising < 0) continue;
        const auto lying = std::ranges::find(players, raiser->raising, [](const auto& each) { return each->id; });
        const bool hit = raiser->pmode == kModeGH || raiser->pmode == kModeDT || raiser->pmode == kModeDD;   // a flinch (or dying) breaks it
        if (lying == players.end() || !(*lying)->body || (*lying)->body->level != level || hit) { raiser->raising = -1; continue; }
        auto& body = *(*lying)->body;
        if (std::hypot(body.x - raiser->player.x, body.y - raiser->player.y) > kRaiseReach) continue;   // still walking up
        raiser->player.walking = false; raiser->player.path.clear();
        raiser->target_x = raiser->player.x; raiser->target_y = raiser->player.y;
        ++body.raisers;
    }
    for (auto& lying : players) {
        if (!lying->body) continue;
        auto& body = *lying->body;
        body.raised = body.raisers == 0 ? 0 : body.raised + float(body.raisers) * elapsed / kRaiseSeconds;   // nobody on it: lost
        if (body.raised < 1) continue;
        lying->player.x = body.x; lying->player.y = body.y; lying->player.dir = body.dir;
        for (auto& raiser : players) if (raiser->raising == lying->id) raiser->raising = -1;
        revive(*lying, now_ms);
    }
}

auto World::leash(std::span<const std::pair<float, float>> before) -> void {
    if (players.size() < 2 || before.size() != players.size()) return;
    // The party on screen: its spread in screen pixels (isometric: a cell is
    // 160 x 80, x - y across, x + y down).
    const auto spread = [&](std::size_t moved_back) {
        float least_x = 1e9f, most_x = -1e9f, least_y = 1e9f, most_y = -1e9f;
        for (std::size_t i = 0; i < players.size(); ++i) {
            const auto [x, y] = i == moved_back ? before[i] : std::pair{ players[i]->player.x, players[i]->player.y };
            const float screen_x = (x - y) * 80, screen_y = (x + y) * 40;
            least_x = std::min(least_x, screen_x); most_x = std::max(most_x, screen_x);
            least_y = std::min(least_y, screen_y); most_y = std::max(most_y, screen_y);
        }
        return std::pair{ most_x - least_x, most_y - least_y };
    };
    const auto too_far = [&](std::pair<float, float> span) { return span.first > kLeashWidth || span.second > kLeashHeight; };
    if (!too_far(spread(players.size()))) return;
    // Whoever pulled it further than it was stays where they were.
    for (std::size_t i = 0; i < players.size(); ++i) {
        const auto stretched = spread(players.size()), back = spread(i);
        if (back.first < stretched.first || back.second < stretched.second) {
            auto& unit = players[i]->player;
            std::tie(unit.x, unit.y) = before[i];
            unit.walking = false; unit.path.clear();
            players[i]->target_x = unit.x; players[i]->target_y = unit.y;
        }
        if (!too_far(spread(players.size()))) return;
    }
}

auto World::bring_party(const Level* from, const Level* destination, float x, float y, bool keep_map, bool tell) -> void {
    static constexpr std::array<std::pair<float, float>, 4> kAround{ { { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f }, { 0.f, -1.f } } };
    std::size_t around = 0;
    Player* const mover = actor;
    each_player([&](Player& player) {
        if (&player == mover) return;
        const auto [spot_x, spot_y] = destination->nearest_free(x + kAround[around % 4].first, y + kAround[around % 4].second);
        ++around;
        fight.pets_cross(from, destination, player.player.x - spot_x, player.player.y - spot_y);
        if (tell) player.events.push_back(ev::LevelChanged{ from, keep_map });
        player.player.x = spot_x; player.player.y = spot_y;
        player.player.walking = false; player.player.path.clear();
        player.target_x = spot_x; player.target_y = spot_y;
        if (player.merc) {
            player.merc->unit.path.clear();
            std::tie(player.merc->unit.x, player.merc->unit.y) = destination->nearest_free(spot_x + 1, spot_y + 1);
        }
        player.interact_npc = player.pick_item = player.take_warp = player.take_portal = -1;
    });
}

auto World::quests() -> d2d::rules::QuestBits& { return actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))]; }

auto World::item_ids() -> void {
        for (auto& item : actor->character.items) if (item.id < 0) item.id = next_item_id++;
        if (actor->held && actor->held->id < 0) actor->held->id = next_item_id++;
    }

auto World::monster_colour(const Monster& monster) const -> int {
        const auto& npc = monster.npc;
        const int difficulty_index = std::clamp(monster.difficulty, 0, 2);
        int colour = npc.trans_lvl < 8 ? npc.trans_lvl + 2 : 2;
        if ((monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique) && !npc.no_unique_shift)
            colour = int(std::uint32_t(monster.id) * 2654435761u % 30u) + 9;
        if (const int unique_trans = npc.utrans[std::size_t(difficulty_index)]; unique_trans != 0) colour = unique_trans == 255 ? 1 : unique_trans;
        if (monster.boss == d2d::rules::Boss::superunique && monster.super >= 0 && std::size_t(monster.super) < game_data->superuniques.size())
            if (const int unique_trans = game_data->superuniques[std::size_t(monster.super)].utrans[std::size_t(difficulty_index)]; unique_trans != 0) colour = unique_trans;
        return colour >= 30 ? 2 : colour;
    }

auto World::take_character(const Character& entering) -> void {
        actor->character.character_class = entering.character_class; actor->character.name = entering.name;
        actor->character.appearance = entering.appearance; actor->character.items = entering.items; actor->character.stats = entering.stats; actor->character.panel = entering.panel;
        actor->character.expansion = entering.expansion; actor->character.header = entering.header; actor->character.merc_items = entering.merc_items;
        // A new game starts at full life, mana and stamina, whatever the
        // save held (D2's "save and exit to heal").
        static constexpr std::pair<d2d::d2s::StatId, d2d::d2s::StatId> kRefill[] = {
            { d2d::d2s::kLife, d2d::d2s::kMaxLife }, { d2d::d2s::kMana, d2d::d2s::kMaxMana }, { d2d::d2s::kStamina, d2d::d2s::kMaxStamina } };
        for (const auto& [cur, max] : kRefill)
            actor->character.stats.values[cur] = actor->character.stats.values[max];
        actor->held.reset();
        actor->store = {};
    }

auto World::lay_save_corpse(const Character& entering, float x, float y) -> void {
        actor->corpses.clear();
        if (entering.corpse.empty()) return;
        const auto [spot_x, spot_y] = level->nearest_free(x, y);
        actor->corpses.push_back({ level, spot_x, spot_y, 4, entering.corpse, 0, entering.appearance ? *entering.appearance : game_data->starting_gear[std::size_t(entering.header.cls % 7)] });
    }

auto World::join(const Character& entering) -> Player& {
        Player& was = *actor;
        Player& player = add_player();
        act_as(player);
        take_character(entering);
        // They play the game's difficulty (the first player's), in its act.
        auto& difficulty = player.character.header.difficulty;
        const auto game_difficulty = std::size_t(std::clamp(lead().character.header.active_difficulty(), 0, 2));
        for (auto& byte : difficulty) byte &= 0x7f;
        difficulty[game_difficulty] = std::uint8_t(0x80 | (difficulty[game_difficulty] & ~7 & 0x7f) | act());
        std::tie(player.player.x, player.player.y) = level->nearest_free(lead().player.x + 1, lead().player.y);
        player.target_x = player.player.x; player.target_y = player.player.y;
        player.player.dir = 4;
        lay_save_corpse(entering, player.player.x + 1, player.player.y + 1);
        spawn_merc();
        item_ids();
        d2d::log::info("{} joins the game (player {})", player.character.name, player.id);
        act_as(was);
        return player;
    }

auto World::leave(int id) -> std::string {
        const auto found = std::ranges::find(players, id, [](const auto& player) { return player->id; });
        if (found == players.end()) return "no such player";
        if (players.size() == 1) return "the last player stays";
        Player* const was = actor;
        act_as(**found);
        const auto why = save();
        d2d::log::info("{} leaves the game (player {})", actor->character.name, id);
        portal[(*found)->portal_pair] = portal[(*found)->portal_pair + 1] = {};   // their town portal closes
        if (found == players.begin()) {
            // The first player: the next one takes their place (slot [0], what
            // the client's `hero` is), the game goes on.
            Player* const next = players[1].get();
            std::erase(fight.heroes, next);
            *players.front() = std::move(*next);
            players.erase(players.begin() + 1);
            act_as(was == next || was == players.front().get() ? lead() : *was);
            d2d::log::info("{} leads the game now", lead().character.name);
            return why;
        }
        std::erase(fight.heroes, found->get());
        const bool was_leaving = was == found->get();
        players.erase(found);
        act_as(was_leaving ? lead() : *was);
        return why;
    }

auto World::enter(const Character& entering) -> void {
        act_as(lead());                               // a new game: its first player alone
        players.resize(1);
        fight.heroes.assign(1, &lead());
        lead().portal_pair = 0;
        take_character(entering);
        stocks.clear();
        for (auto& list : hire_lists) list.clear();
        // A game starts in the town of the save's act (its difficulty byte:
        // 0x80 and the act in the low bits); the camp when that isn't built.
        const auto act_byte = actor->character.header.difficulty[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))];
        const int save_act = act_byte & 0x80 ? act_byte & 7 : 0;
        const Level* act_town = save_act > 0 && save_act < int(d2d::rules::level_ids::kActTowns.size()) ? game_data->level(d2d::rules::level_ids::kActTowns[std::size_t(save_act)]) : nullptr;
        level = act_town && act_town->start.first >= 0 ? act_town : &game_data->town;   // set_map_seed may have rebuilt the camp
        // A corpse in the save lies by the town's start.
        lay_save_corpse(entering, level->start.first + 1.f, level->start.second + 1.f);
        other_npcs.clear();
        npc_states = npc_start(*level);
        actor->interact_npc = actor->pick_item = actor->take_warp = -1;
        wanted_near = nullptr;
        if (level->start.first >= 0) std::tie(actor->player.x, actor->player.y) = level->nearest_free(level->start.first, level->start.second);
        actor->target_x = actor->player.x; actor->target_y = actor->player.y;
        spawn_merc();
        new_game();
        act5_hide();                                  // a5q3's join: Anya home, Nihlathak gone (Anya's state grown in)
        grow_states(npc_states, *level);
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
            if (level->npcs[i].quest == d2d::rules::CainQuest::kQuest) npc_states[i].hidden = !cain.camp_cain;
        show_palace();                                // a save left in act 2 starts in Lut Gholein
    }

auto World::new_game() -> void {
        fight.new_game(actor->character.header.active_difficulty());
        set_waypoint(0);                          // the town's is always active (FUN_00661030 forces index 0 as the save loads)
        day = {};                                 // a new game starts at sunrise
        day_at = now;
        loot.ground.clear();
        loot.kept.clear();
        loot.ground_level = level;
        cues.due.clear();
        // The first join (FUN_00546270): quests the player's done or closed
        // are shut for the game (FUN_00544410, game flag 15), every quest's
        // join runs, then the chain from quest 1.
        for (int quest = 1; quest < int(closed_at_join.size()); ++quest)
            closed_at_join[std::size_t(quest)] = d2d::rules::quest::bit(quests(), quest, 0) || d2d::rules::quest::bit(quests(), quest, 15);
        game_bits = {};
        act1_join();
        act2_join();
        act3_join();
        act4_join();
        act5_join();
        den_left = -1;
        den_log_at = 0;
        actor->voice_found.reset();               // FUN_004ca280: a new game's find lines
        actor->voice_checked = 0;
        operated.clear();
        // The levels as built: no room populated (the units come again as
        // they do), doors back as they were made.
        for (const auto& [id, built] : game_data->levels) {
            if (!built) continue;
            auto& level_rw = const_cast<Level&>(*built);         // GameData owns its levels mutable
            level_rw.trap_kind = -1;
            if (level_rw.npcs.size() > level_rw.npc_base && level_rw.walk.size() == level_rw.tile_walk.size()) {
                level_rw.npcs.resize(level_rw.npc_base);
                level_rw.walk = level_rw.tile_walk;
                stamp_footprints(level_rw);
            }
            for (const auto& npc : built->npcs)
                if (is_door(npc.operate_fn)) set_footprint(*built, npc, npc.collision >> mode_index(npc.mode) & 1);
        }
        npc_states.resize(std::min(npc_states.size(), level->npcs.size()));
        other_npcs.clear();
        doors.clear();
        fires.clear();
        afterglow_made = false;
        afterglow_due = 0;
        traps.clear();
        treasure.clear();
        cain_walk = {};
        cain_portal = {};
        std::ranges::fill(portal, Portal{});
        actor->take_portal = -1;
        actor->take_corpse = -1;
        actor->last_pmode = -1;
        actor->pick_item = -1;
    }

auto World::spawn_merc() -> void {
        actor->merc.reset();
        const auto& header = actor->character.header;
        if (const auto found = game_data->mercs.find(header.merc_type); header.merc_seed && !header.merc_dead && found != game_data->mercs.end()) {
            actor->merc = Monster{};
            actor->merc->npc = found->second.npc;
            actor->merc->unit.shape = actor->merc->npc.shape;
            actor->merc->difficulty = header.active_difficulty();
            std::tie(actor->merc->unit.x, actor->merc->unit.y) = level->nearest_free(actor->player.x + 1, actor->player.y + 1);
            actor->merc_npc = &found->second.npc;
            fight.merc_joins();
        }
    }

auto World::swap_npcs(const Level* from) -> void {
        actor->arrived_at = now;
        if (from && from != level)                           // each player's level change (event 3): the party moved too (bring_party)
            each_player([&](Player&) { act1_enter(*from); act2_enter(*from); act3_enter(*from); act4_enter(*from); act5_enter(*from); });
        if (from) other_npcs[from] = std::move(npc_states);
        if (const auto found = other_npcs.find(level); found != other_npcs.end()) {
            npc_states = std::move(found->second);
            other_npcs.erase(found);
        } else {
            npc_states = npc_start(*level);
        }
        grow_states(npc_states, *level);
        show_palace();
        act5_hide();
        if (level->id == d2d::rules::SevenTombsQuest::kLair && tombs.tyrael_gone)   // Tyrael has left (FUN_0059df50: record +0x3d)
            for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
                if (level->npcs[i].hc_idx == d2d::rules::SevenTombsQuest::kTyrael) npc_states[i].hidden = true;
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)   // camp Cain (FUN_00592960)
            if (level->npcs[i].quest == d2d::rules::CainQuest::kQuest)
                npc_states[i].hidden = level->npcs[i].hc_idx == d2d::rules::monster_ids::kCain && level->id == d2d::rules::CainQuest::kTristram
                                           ? !(cain_walk.npc == int(i) && cain_walk.stage >= 0)   // Tristram's Cain: only while he walks out
                                           : !cain.camp_cain;                                     // the camp's: once he's come (FUN_00592960)
        stones_pending = level->id == d2d::rules::CainQuest::kStony;
        stones_up();
    }

// The stones as their room comes up (InitFn 6, FUN_005935e0, as each is
// made): the portal again, the stones lit. A level's objects come with
// their rooms, so it waits for StoneAlpha.
auto World::stones_up() -> void {
        if (!stones_pending || level->id != d2d::rules::CainQuest::kStony) return;
        if (std::ranges::find(level->npcs, d2d::rules::object_ids::kStoneAlpha, &Npc::object_id) == level->npcs.end()) return;
        stones_pending = false;
        if (!cain.stones_init()) return;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& stone = level->npcs[i];
            if (stone.operate_fn != d2d::rules::operate_fn::kCairnStone) continue;
            operated.try_emplace({ level, int(i) }, 0u);
            if (stone.object_id == d2d::rules::object_ids::kStoneAlpha && !portal[2].level) {
                const auto [spot_x, spot_y] = level->nearest_free(stone.x + 4, stone.y + 4);
                portal[2] = { level, spot_x, spot_y, now };
            }
        }
    }

// Lut Gholein's palace NPCs as the quests stand (rules::palace_spots).
auto World::show_palace() -> void {
        const auto spots = d2d::rules::palace_spots(game_bits, tombs.open, tombs.state);
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
            switch (level->npcs[i].palace) {
            case 1: npc_states[i].hidden = !spots.outside; break;
            case 2: npc_states[i].hidden = !(spots.palace && spots.palace_west); break;
            case 3: npc_states[i].hidden = !(spots.palace && !spots.palace_west); break;
            case 4: npc_states[i].hidden = !spots.guard; break;
            default: break;
            }
    }

auto World::respawn(std::uint32_t now_ms) -> void {
        if (!in_town(level)) {
            actor->events.push_back(ev::LevelChanged{ level, false });
            const Level* from = level;
            const Level* act_town = game_data->town_of(level->id);
            level = act_town ? act_town : &game_data->town;   // Lut Gholein not built: the camp
            swap_npcs(from);
            actor->interact_npc = actor->pick_item = -1;
            std::tie(actor->player.x, actor->player.y) = level->start.first >= 0 ? level->start : std::pair{ actor->player.x, actor->player.y };
            bring_party(from, level, actor->player.x, actor->player.y, false);
        }
        actor->take_warp = -1;
        std::tie(actor->player.x, actor->player.y) = level->start.first >= 0 ? level->start : std::pair{ actor->player.x, actor->player.y };
        std::tie(actor->player.x, actor->player.y) = level->nearest_free(actor->player.x, actor->player.y);
        actor->target_x = actor->player.x; actor->target_y = actor->player.y;
        fight.revive(now_ms);
        d2d::log::info("respawned in {}", level_name(*level));
    }

}  // namespace d2d::game
