// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for world.hpp: the World, the game server and its 25 Hz tick.
// This part: players: join, leave, a new game, ghosts and the merc. The rest of the World
// (world.hpp) is in this directory, by subsystem.
#include "world.hpp"

#include "ai.hpp"
#include "ai/units.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "inventory.hpp"
#include "level.hpp"
#include "log.hpp"
#include "npc.hpp"

#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <monster_ids.hpp>
#include <obj_preset.hpp>
#include <object_ids.hpp>
#include <quest_clock.hpp>
#include <quests.hpp>
#include <replenish.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>
#include <wear.hpp>

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
#include <string_view>
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

// At --start-cam-x/y, else the town start (Level::start), else the map's middle; then
// the nearest free spot, so a game never starts inside a tent.
World::World(const GameData* loaded_data, int start_x, int start_y) : game_data(loaded_data), level(loaded_data ? &loaded_data->town : nullptr) {
    act_as(add_player());
    loot.gold_sharers = [this] {
        std::vector<Character*> others;
        for (const auto& player : players)
            if (player.get() != actor && player->pmode != kModeDT && player->pmode != kModeDD) others.push_back(&player->character);
        return others;
    };
    loot.class_voice = [this](std::string_view name, std::uint32_t now_ms) { fight.class_voice(name, now_ms); };
    const bool have_world = level && !level->dt1s.empty();
    actor->player.x = (start_x >= 0 ? float(start_x) : have_world ? float(level->ds1.width() / 2) : 0.f) + 0.5f;
    actor->player.y = (start_y >= 0 ? float(start_y) : have_world ? float(level->ds1.height() / 2) : 0.f) + 0.5f;
    if (have_world && start_x < 0 && start_y < 0 && level->start.first >= 0)
        std::tie(actor->player.x, actor->player.y) = level->start;
    if (have_world) std::tie(actor->player.x, actor->player.y) = level->nearest_free(actor->player.x, actor->player.y);
    if (game_data) npc_states = npc_start(*level);
    if (game_data) fight.new_game(0);
    actor->target_x = actor->player.x; actor->target_y = actor->player.y;
    actor->player.dir = 4;                    // south, facing the viewer
    // Its monsters open its doors (monster_door).
    fight.open_door = [this](const Monster& monster, std::uint32_t now_ms) { return monster_door(monster, now_ms); };
    // The character's skill levels for the fight, the skill shrine's
    // +all skills (item_allskills) while its boost lasts, and a self
    // state's (Battle Command's aurastat1: its stats on the player,
    // FUN_00644180 adding item_allskills to every skill).
    fight.skill_base = [this](int id) { return skill_base_level(*game_data, actor->character, id); };
    fight.skill_level = [this](int id) {
        if (fight.from_charges(id)) return actor->item_charge.level;   // a charged entry's own level: no +skills (the entry's, SKILLS_AddOrRemoveItemSkill)
        std::vector<d2d::d2s::ItemProp> extra;
        if (now < actor->boost.until)
            for (const auto& [stat, value] : actor->boost.stats) if (stat == d2d::d2s::kAllSkills) extra.push_back({ .stat = d2d::d2s::kAllSkills, .value = value });
        for (const auto& state : actor->self_states)
            if (const auto* skill = game_data->skills.get(state.skill); skill && now < state.until)
                for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                    if (skill->aurastat[k] == d2d::d2s::kAllSkills)
                        extra.push_back({ .stat = d2d::d2s::kAllSkills, .value = d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], {}, skill->id, state.level) });
        return skill_level(*game_data, actor->character, id, extra);
    };
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
    return std::ranges::any_of(players, [](const auto& player) { return !player->own_screen && player->pmode != kModeDT && player->pmode != kModeDD; });
}

auto World::shared_screen() const -> bool {
    return !actor->own_screen && std::ranges::count_if(players, [](const auto& player) { return !player->own_screen; }) > 1;
}

auto World::become_ghost(Player& player) -> void {
    player.ghost = true;
    player.body = Player::Body{ level, player.player.x, player.player.y, player.player.dir, fight.gfx() };
    player.raising = -1;
    d2d::log::info("{} is a ghost", player.character.name);
}

auto World::revive(Player& player, std::uint32_t now_ms) -> void {
    if (player.character.header.hardcore() && player.character.header.died()) return;   // a hardcore ghost stays one
    Player& was = *actor;
    act_as(player);
    player.ghost = false;
    player.body.reset();
    player.last_pmode = -1;
    player.player.walking = false; player.player.path.clear();
    player.target_x = player.player.x; player.target_y = player.player.y;
    player.player.goal_x = player.target_x; player.player.goal_y = player.target_y;   // the path's SP3 with it (Hero::target_x)
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
        raiser->player.goal_x = raiser->target_x; raiser->player.goal_y = raiser->target_y;   // the path's SP3 with it (Hero::target_x)
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
    if (before.size() != players.size()) return;
    std::vector<std::size_t> party;                     // the players on this screen (a LAN joiner has its own)
    for (std::size_t slot = 0; slot < players.size(); ++slot) if (!players[slot]->own_screen) party.push_back(slot);
    if (party.size() < 2) return;
    // The party on screen: its spread in screen pixels (isometric: a cell is
    // 160 x 80, x - y across, x + y down).
    const auto spread = [&](std::size_t moved_back) {
        float least_x = 1e9f, most_x = -1e9f, least_y = 1e9f, most_y = -1e9f;
        for (const std::size_t slot : party) {
            const auto [tile_x, tile_y] = slot == moved_back ? before[slot] : std::pair{ players[slot]->player.x, players[slot]->player.y };
            const float screen_x = (tile_x - tile_y) * 80, screen_y = (tile_x + tile_y) * 40;
            least_x = std::min(least_x, screen_x); most_x = std::max(most_x, screen_x);
            least_y = std::min(least_y, screen_y); most_y = std::max(most_y, screen_y);
        }
        return std::pair{ most_x - least_x, most_y - least_y };
    };
    const auto too_far = [&](std::pair<float, float> span) { return span.first > kLeashWidth || span.second > kLeashHeight; };
    if (!too_far(spread(players.size()))) return;
    // Whoever pulled it further than it was stays where they were.
    for (const std::size_t slot : party) {
        const auto stretched = spread(players.size()), back = spread(slot);
        if (back.first < stretched.first || back.second < stretched.second) {
            auto& unit = players[slot]->player;
            std::tie(unit.x, unit.y) = before[slot];
            unit.walking = false; unit.path.clear();
            players[slot]->leap.reset();                  // a leap or a knockback cut off where it was
            players[slot]->knock.reset();
            players[slot]->interact_npc = -1;
            players[slot]->target_x = unit.x; players[slot]->target_y = unit.y;
            players[slot]->player.goal_x = players[slot]->target_x; players[slot]->player.goal_y = players[slot]->target_y;   // the path's SP3 with it (Hero::target_x)
        }
        if (!too_far(spread(players.size()))) return;
    }
}

// The other players come to `destination` round (x, y) with their mercs
// and pets (one screen, one level: docs/design/local-coop.md); `tell`:
// their clients hear of a level change (not for a jump within one).
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
        player.player.goal_x = player.target_x; player.player.goal_y = player.target_y;   // the path's SP3 with it (Hero::target_x)
        if (player.merc) {
            player.merc->unit.path.clear();
            std::tie(player.merc->unit.x, player.merc->unit.y) = destination->nearest_free(spot_x + 1, spot_y + 1);
        }
        player.interact_npc = player.pick_item = player.take_warp = player.take_portal = -1;
    });
}

auto World::quests() -> d2d::rules::QuestBits& { return actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))]; }
// Its congratulations bits (the save's +0x2E5: rules::NpcReturns).
auto World::congrats() -> std::uint64_t& { return actor->character.header.congrats[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))]; }

// New items take the game's next item id (loot.next_id: game +0xa0,
// FUN_00552ee0 from FUN_00555230); one dropped or picked up is the same
// unit (FUN_00563c00, FUN_00563560 make none) and keeps its id.
auto World::item_ids() -> void {
        for (auto& item : actor->character.items) if (item.id < 0) item.id = loot.next_id++;
        if (actor->held && actor->held->id < 0) actor->held->id = loot.next_id++;
    }

// A store's stock items are units too (the vendor's inventory, the player's gamble list): each its id
// as it's made, which C->S 0x32 names (FUN_00577830 -> FUN_00552f60).
auto World::store_ids() -> void {
        for (auto& page : actor->store.tabs)
            for (auto& stocked : page) if (stocked.id < 0) stocked.id = loot.next_id++;
    }

// What the client is told (View).
// A monster's colour as the client picks it (FUN_00466360): its
// TransLvl + 2 (8 and up: 2); a unique's (and superunique's) roll from
// own seed, rand(30) + 9, unless noUniqueShift; MonStats2 Utrans for the
// difficulty; a superunique's own Utrans; 30 and up fall back to 2.
// Utrans 0xff (necroskeleton, necromage) is 1 for a pet of the viewer
// or of a player not hostile to them, else 0 (FUN_004791b0: the 0x7a pet
// list's owner, the roster's flag 8; 0x46665c): 0 here, where pets aren't
// (Player::pets). Both draw untinted.
// That seed (FUN_00477620 via FUN_00650e40): {unit id + the name seed's
// low byte, 666}, one step (the 0xac handler keeps only the low byte of
// the 16-bit name seed, 0x45f387: bugs.md).
auto World::monster_colour(const Monster& monster) const -> int {
        const auto& npc = monster.npc;
        const int difficulty_index = std::clamp(monster.difficulty, 0, 2);
        int colour = npc.trans_lvl < 8 ? npc.trans_lvl + 2 : 2;
        if ((monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique) && !npc.no_unique_shift)
            colour = int(d2d::rules::Rng{ std::uint32_t(monster.id) + std::uint32_t(monster.name_seed & 0xff) }.next() % 30u) + 9;
        if (const int unique_trans = npc.utrans[std::size_t(difficulty_index)]; unique_trans != 0) colour = unique_trans == 255 ? 0 : unique_trans;
        if (monster.boss == d2d::rules::Boss::superunique && monster.super >= 0 && std::size_t(monster.super) < game_data->superuniques.size())
            if (const int unique_trans = game_data->superuniques[std::size_t(monster.super)].utrans[std::size_t(difficulty_index)]; unique_trans != 0) colour = unique_trans;
        return colour >= 30 ? 2 : colour;
    }

auto World::take_character(const Character& entering) -> void {
        actor->character.character_class = entering.character_class; actor->character.name = entering.name;
        actor->character.appearance = entering.appearance; actor->character.items = entering.items; actor->character.stats = entering.stats; actor->character.panel = entering.panel;
        actor->character.expansion = entering.expansion; actor->character.header = entering.header; actor->character.merc_items = entering.merc_items;
        actor->character.fresh = entering.fresh;
        for (auto& item : actor->character.items) item.id = -1;   // made anew in this game, each a new unit (FUN_00558cb0)
        const int kept = d2d::rules::wield_settle(game_data->rules, actor->character.items);
        item_ids();                                    // the kept weapon is its unit id (FUN_006233a0)
        actor->kept_weapon = kept >= 0 ? actor->character.items[std::size_t(kept)].id : 0;
        for (auto& item : actor->character.items) d2d::rules::replenish_arm(game_data->rules, item);   // each saved item made: FUN_00558cb0 -> FUN_00558530 / FUN_00558580
        for (auto& item : actor->character.merc_items) d2d::rules::replenish_arm(game_data->rules, item);   // the merc's "JM" list: the same item loader
        settle_requirements(*game_data, actor->character);   // the join: FUN_0052d440 -> FUN_0055df00 -> FUN_0055dbc0
        // A new game keeps the life and mana the save held, exactly, fraction
        // and all, even above the maximum: game.exe's loader FUN_0056b180 reads
        // stats 6 and 8 after the stat block (0x56b283, 0x56b28f) and sets them
        // back at its end (FUN_0056af80, 0x56b0ff, 0x56b10d) with no clamp. The
        // client's copy is whole points (its 0x18 packet, `value << 8`). The
        // same function sets stamina (0xa) to the maximum (0xb), 0x56b01d.
        actor->character.stats.values[d2d::d2s::kStamina] = actor->character.stats.values[d2d::d2s::kMaxStamina];
        // The same restore (FUN_0056af80, 0x56afb4..0x56b00f) puts gold and stash gold back through
        // FUN_0053ff00, which sets a stat to 0 when it would pass its cap: the purse's clvl x 10000
        // (FUN_00622e70), the stash's 2,500,000 (FUN_00623460).
        {
            using namespace d2d::d2s;
            auto& values = actor->character.stats.values;
            constexpr std::int64_t kStashGoldMax = 2500000;
            if (values[kGold] > values[kLevel] * 10000) values[kGold] = 0;
            if (values[kGoldBank] > kStashGoldMax) values[kGoldBank] = 0;
        }
        // The save's maxima are its own (Fight::own_stats): the last game's
        // gear and Battle Orders shares go, or the next refresh adds none and
        // the next save takes them off again.
        actor->item_max = {}; actor->bo_life = actor->bo_mana = 0;
        actor->held.reset();
        actor->store = {};
    }

auto World::lay_save_corpse(const Character& entering) -> void {
        actor->corpses.clear();
        if (entering.corpse.empty()) return;
        const auto* town = act() > 0 && act() < int(d2d::rules::level_ids::kActTowns.size()) ? game_data->level(d2d::rules::level_ids::kActTowns[std::size_t(act())]) : nullptr;
        if (!town || town->start.first < 0) town = &game_data->town;
        const int width = town->ds1.width() * 5, height = town->ds1.height() * 5;
        const Crowd crowd = town == level ? crowd_now() : Crowd{};
        static constexpr UnitShape kFootprint{ 0, 0, 0x1000 };                  // a unit's 0x1000 on that very subtile
        const auto flags = [&](int sub_x, int sub_y) -> int {
            if (sub_x < 0 || sub_y < 0 || sub_x >= width || sub_y >= height || town->walk.empty()) return 0x27;
            const int unit = crowd.at((float(sub_x) + 0.5f) / 5, (float(sub_y) + 0.5f) / 5, kFootprint, nullptr) ? 0x1000 : 0;
            return town->walk[std::size_t(sub_y) * std::size_t(width) + std::size_t(sub_x)] | unit;
        };
        const auto [spot_x, spot_y] = corpse_spot(int(std::floor(town->start.first * 5)), int(std::floor(town->start.second * 5)), flags);
        actor->corpses.push_back({ town, (float(spot_x) + 0.5f) / 5, (float(spot_y) + 0.5f) / 5, 4, entering.corpse, 0, entering.appearance ? *entering.appearance : game_data->starting_gear[std::size_t(entering.header.cls % 7)] });
        for (auto& item : actor->corpses.back().items) item.id = loot.next_id++;   // made anew in this game, as the bags' are
    }

// Another player joins the game under way (docs/design/local-coop.md):
// their character (full life, as entering), beside the first player,
// their merc along. The one acting stays.

// The quests hear of them through their +0xd8s (join_more), not the
// +0xd4s the game's first player ran (FUN_00546270).
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
        // The save's corpse loads with the save (FUN_00534020 -> FUN_005339a0 ->
        // FUN_00533850), before the join's quest calls (0x53458b): each of its
        // items raises event 4, the corpse its player (FUN_00531520, 0x531627).
        for (const auto& item : entering.corpse) item_event(item.code, now, true);
        join_more();                                   // the +0xd8s see the game's difficulty's bits
        std::tie(player.player.x, player.player.y) = level->nearest_free(lead().player.x + 1, lead().player.y);
        player.target_x = player.player.x; player.target_y = player.player.y;
        player.player.goal_x = player.target_x; player.player.goal_y = player.target_y;   // the path's SP3 with it (Hero::target_x)
        player.player.dir = 4;
        spawn_merc();
        lay_save_corpse(entering);
        item_ids();
        d2d::log::info("{} joins the game (player {})", player.character.name, player.id);
        act_as(was);
        return player;
    }

// A player leaves: their character saved (save's message, "" written),
// gone from the game. The first player's place goes to the next player
// (their state moves into slot [0]). The last player can't leave this way.
auto World::leave(int id) -> std::string {
        const auto found = std::ranges::find(players, id, [](const auto& player) { return player->id; });
        if (found == players.end()) return "no such player";
        if (players.size() == 1) return "the last player stays";
        Player* const was = actor;
        act_as(**found);
        const auto why = save();
        d2d::log::info("{} leaves the game (player {})", actor->character.name, id);
        close_portal_pair((*found)->portal_pair);   // their town portal closes
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

// A player enters with their character (a save loaded, or made): the
// World takes its copy, the merc comes along, a fresh game; the Act
// 1 quest-gated NPCs (Cain) are there once their quest is done.
auto World::enter(const Character& entering) -> void {
        act_as(lead());                               // a new game: its first player alone
        players.resize(1);
        fight.heroes.assign(1, &lead());
        lead().portal_pair = 0;
        loot.next_id = 1;                             // a new game's counters start at 0 (FUN_00530930: game +0x90 .. +0xa4)
        take_character(entering);
        stocks.clear();
        kept_gambles.clear();
        for (auto& list : hire_lists) list.clear();
        // A game starts in the town of the save's act (its difficulty byte:
        // 0x80 and the act in the low bits); the camp when that isn't built.
        const auto act_byte = actor->character.header.difficulty[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))];
        const int save_act = act_byte & 0x80 ? act_byte & 7 : 0;
        const Level* act_town = save_act > 0 && save_act < int(d2d::rules::level_ids::kActTowns.size()) ? game_data->level(d2d::rules::level_ids::kActTowns[std::size_t(save_act)]) : nullptr;
        level = act_town && act_town->start.first >= 0 ? act_town : &game_data->town;   // set_map_seed may have rebuilt the camp
        lead().on_level = nullptr;                    // not the last game's level (split levels set it on the first tick)
        other_npcs.clear();
        npc_states = npc_start(*level);
        actor->interact_npc = actor->pick_item = actor->take_warp = -1;
        wanted_near = nullptr;
        if (level->start.first >= 0) std::tie(actor->player.x, actor->player.y) = level->nearest_free(level->start.first, level->start.second);
        actor->target_x = actor->player.x; actor->target_y = actor->player.y;
        actor->player.goal_x = actor->target_x; actor->player.goal_y = actor->target_y;   // the path's SP3 with it (Hero::target_x)
        spawn_merc();
        lay_save_corpse(entering);                    // by the town's start, the player standing there
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
        quest_clock = {};                         // and its frame and quest count from 0 (game +0xa8, +0x10f4 +0x14)
        day_at = now;
        loot.ground.clear();
        loot.kept.clear();
        loot.ground_level = level;
        cues.due.clear();
        // The first join (FUN_00546270): quests the player's done or closed
        // are shut for the game (FUN_00544410, game flag 15), every quest's
        // join (+0xd4) runs, then the chain from quest 1. A character made
        // new (fresh, 0x53266d / 0x56a06c) runs no +0xd4: of them only a4q3's
        // (FUN_005b64e0, state 1 with no soulstone) changes a new one's
        // records (tools/emu/quest_joins.py), so act4_join skips that.
        for (int quest = 1; quest < int(closed_at_join.size()); ++quest)
            closed_at_join[std::size_t(quest)] = d2d::rules::quest::bit(quests(), quest, 0) || d2d::rules::quest::bit(quests(), quest, 15);
        game_bits = {};
        act1_join();
        act2_join();
        act3_join();
        act4_join();
        act5_join();
        den_left = -1;
        den_log_in = 0;
        stones_portal_in = 0;
        actor->voice_found.reset();               // FUN_004ca280: a new game's find lines
        actor->voice_checked = 0;
        operated.clear();
        wirt_gold.clear();
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
        durance_stairs_due = {};
        objects_inited.clear();
        fires.clear();
        afterglow_made = false;
        afterglow_in = 0;
        afterglow_due = false;
        traps.clear();
        treasure.clear();
        cain_walk = {};
        cain_portal = {};
        cain_portal_closes = 0;
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
            actor->merc_listed = ++actor->list_serial;   // FUN_005b1900 (hire, join, FUN_00579aa0's revive): the newest in the player's list
            fight.merc_joins();
        }
    }

auto World::swap_npcs(const Level* from) -> void {
        if (from && from != level)                           // each player's level change (event 3): the party moved too (bring_party)
            each_player([&](Player&) { act1_enter(*from); act2_enter(*from); act3_enter(*from); act4_enter(*from); act5_enter(*from); });
        swap_npc_states(from);
    }

auto World::swap_npc_states(const Level* from) -> void {
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
        camp_cain_state();
    }

// Camp Cain's state at the spot place_camp_cain chose (a fresh one, as npc_start makes), once
// the town's NPC states are the current ones: act1_enter places him before swap_npc_states
// brings the town's kept states back.
auto World::camp_cain_state() -> void {
        if (!camp_cain_placed || level != &game_data->town) return;
        camp_cain_placed = false;
        for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
            if (const auto& npc = level->npcs[i]; npc.hc_idx == d2d::rules::monster_ids::kCampCain)
                npc_states[i] = { .x = Fine(npc.x), .y = Fine(npc.y), .shape = npc.shape };
    }

// The stones as their room comes up (InitFn 6, FUN_005935e0, as each is
// made): the stones lit and, at StoneAlpha with the portal not made
// (+0x45) nor due (+0x44), the portal's 1-tick timer (0x59366e). A
// level's objects come with their rooms, so it waits for StoneAlpha.
auto World::stones_up() -> void {
        if (!stones_pending || level->id != d2d::rules::CainQuest::kStony) return;
        if (std::ranges::find(level->npcs, d2d::rules::object_ids::kStoneAlpha, &Npc::object_id) == level->npcs.end()) return;
        stones_pending = false;
        if (!cain.stones_init()) return;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& stone = level->npcs[i];
            if (stone.operate_fn != d2d::rules::operate_fn::kCairnStone) continue;
            operated.try_emplace({ level, int(i) }, 0u);
            if (stone.object_id == d2d::rules::object_ids::kStoneAlpha && !portal[2].level && stones_portal_in == 0)
                stones_portal_in = d2d::rules::quest_timer_steps(1);
        }
    }

// The stones' timer (FUN_00592d50): the red portal to Tristram
// (FUN_0056d130, object 0x3c) at StoneAlpha + 4 subtiles; +0x45 made.
// No StoneAlpha (FUN_00552f60): +0x44 cleared, done.
auto World::stones_portal() -> void {
        const Level* stony = game_data->level(d2d::rules::CainQuest::kStony);
        if (!stony || portal[2].level) return;
        const auto alpha = std::ranges::find(stony->npcs, d2d::rules::object_ids::kStoneAlpha, &Npc::object_id);
        if (alpha == stony->npcs.end()) return;
        const auto [spot_x, spot_y] = stony->nearest_free(alpha->x + 4 / 5.f, alpha->y + 4 / 5.f);   // subtiles: a tile is 5
        portal[2] = { stony, spot_x, spot_y, now };
        d2d::log::info("Search for Cain: the stones' portal");
    }

// Lut Gholein's palace NPCs as the quests stand (rules::palace_spots).
// Camp Cain made (FUN_00592960, from the start object's InitFn 54, FUN_005940e0): from the start
// object's spot (+0x84 / +0x88), stepped a subtile down-right at a time (0x15 tries) till it's inside
// its room's rect less a subtile on the far sides; a size-2 spot free of 0x100 near it
// (FUN_00545340); then MonStats 0x109 within 5 of it (FUN_005b2f20 -> FUN_005b2a00, on the room's
// seed: spawn_spot); failing, 20 tries a subtile further down-right, each in the room there with a
// new free spot, within 10; at last within 15 of the start spot itself.
// All failing, no Cain (0x592ad8 returns): the quest's +0x51 / +0x52 stay, so he's still due at the
// next try (the start object's init, a player into the camp). Made, CainQuest::camp_made.
// The camp's room seeds are keep_camp_seeds' (its own ponytail on the room1 seed).
auto World::place_camp_cain() -> bool {
        auto& town = const_cast<Level&>(game_data->town);              // GameData owns its levels mutable
        const auto start = std::ranges::find_if(town.ds1.objects(), [](const auto& object) {
            return object.type == d2d::rules::unit_type::kObject && object.id >= 0 && object.id < 150
                && kObjPreset[0][std::size_t(object.id)] == d2d::rules::object_ids::kCainStart;
        });
        const auto camp_cain = std::ranges::find_if(town.npcs, [](const Npc& npc) { return npc.hc_idx == d2d::rules::monster_ids::kCampCain; });
        auto& in_play = fight.spawning.levels[&town];
        if (start == town.ds1.objects().end() || camp_cain == town.npcs.end() || !keep_camp_seeds(*game_data, town, in_play)) return false;
        if (in_play.rooms_up.size() < in_play.room_seeds.size()) in_play.rooms_up.assign(in_play.room_seeds.size(), true);
        const Crowd crowd;
        const auto room_at = [&](int x, int y) { return room_of(town, (float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5); };
        const auto try_at = [&](int x, int y, int radius, int room) -> bool {
            int out_x = 0, out_y = 0;
            if (room < 0 || !spawn_spot(*game_data, town, crowd, &in_play, d2d::rules::monster_ids::kCampCain, x, y, radius, room, out_x, out_y)) return false;
            camp_cain->x = (float(out_x) + 0.5f) / 5; camp_cain->y = (float(out_y) + 0.5f) / 5;
            cain.camp_made();
            camp_cain_placed = true;
            return true;
        };
        const int start_x = start->x, start_y = start->y;
        const int room = room_at(start_x, start_y);
        int x = start_x, y = start_y;
        if (const auto rect = room >= 0 ? level_room(town, std::size_t(room)) : std::nullopt) {
            const int left = rect->x * 5, top = rect->y * 5, width = rect->width * 5, height = rect->height * 5;
            int step = 0;
            for (; step < 0x15 && !(left <= x && top <= y && x < left + width - 1 && y < top + height - 1); ++step) { ++x; ++y; }
            if (step == 0x15) x = y = start_y;                       // none inside: local_14 = param_3 (0x5929e4), as game.exe
        }
        if (const auto spot = town.free_area(x, y, 2, 0x100, 100)) std::tie(x, y) = *spot;
        if (try_at(x, y, 5, room)) return true;
        for (int tries = 0; tries < 20; ++tries) {
            ++x; ++y;
            int here = room_at(x, y);
            if (here < 0) { x = start_x; y = start_y; here = room; }
            if (const auto spot = town.free_area(x, y, 2, 0x100, 100)) std::tie(x, y) = *spot;
            if (try_at(x, y, 10, here)) return true;
        }
        return try_at(start_x, start_y, 15, room);
    }

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
        if (split_levels && !in_town(level)) {               // stage 9: back to their act's town, their screen's party with them (go_alone's rule)
            const Level* act_town = game_data->town_of(level->id);
            const Level* town = act_town ? act_town : &game_data->town;
            Player* const dead = actor;
            for (auto& player : players) {
                if (player.get() != dead && (dead->own_screen || player->own_screen)) continue;
                act_as(*player);
                actor->events.push_back(ev::LevelChanged{ level, false });
                actor->on_level = town;
                actor->interact_npc = actor->pick_item = actor->take_warp = -1;
                actor->attack_mon = actor->merc_target = -1;     // the level's monsters stay behind (Fight::enter)
                for (auto& pet : actor->pets) pet.target = -1;
                std::tie(actor->player.x, actor->player.y) = town->nearest_free(town->start.first, town->start.second);
                actor->target_x = actor->player.x; actor->target_y = actor->player.y;
                actor->player.goal_x = actor->target_x; actor->player.goal_y = actor->target_y;   // the path's SP3 with it (Hero::target_x)
                actor->player.walking = false; actor->player.path.clear();
            }
            act_as(*dead);
            fight.revive(now_ms);
            d2d::log::info("{} respawned in {}", actor->character.name, level_name(*town));
            return;
        }
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
        actor->player.goal_x = actor->target_x; actor->player.goal_y = actor->target_y;   // the path's SP3 with it (Hero::target_x)
        fight.revive(now_ms);
        d2d::log::info("respawned in {}", level_name(*level));
    }

}  // namespace d2d::game
