// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for world.hpp: the World, the game server and its 25 Hz tick.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "item_text.hpp"
#include "log.hpp"
#include "npc_menu.hpp"
#include "protocol.hpp"

#include <combat.hpp>
#include <cube.hpp>
#include <d2s_items.hpp>
#include <drlg.hpp>
#include <drops.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <merc.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <sound_ids.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

// Levels.txt Waypoint: a level's index in the waypoint bits (FUN_00660e00), -1 none.
// `level`'s units states, the ones its rooms made since (populate) added as they start.
// The client's ticks from an object's OP to its ON (FUN_004bcbb0): the
// counter runs from Start1 at FrameDelta1 a tick (FUN_00624390) until it
// reaches FrameCnt1 - 1; -1: never (FrameCnt1 1, or a counter that doesn't move).
static int op_ticks(const Npc& npc) {
    if (!npc.op_frames) return 0;
    if (npc.frames[1] == 1) return -1;
    const int need = (int(npc.frames[1]) - 1 - int(npc.loop_from[1])) * 256;
    if (need <= 0) return 0;
    if (!npc.frame_delta[1]) return -1;
    return (need + npc.frame_delta[1] - 1) / npc.frame_delta[1];
}

static void grow_states(std::vector<UnitState>& states, const Level& level) {
    if (states.size() >= level.npcs.size()) return;
    const auto fresh = npc_start(level);
    states.insert(states.end(), fresh.begin() + std::ptrdiff_t(states.size()), fresh.end());
}

int waypoint_index(const GameData& game_data, int level_id) {
    for (const auto& act_levels : game_data.waypoint_levels)
        if (const auto found = std::ranges::find(act_levels, level_id, &GameData::WaypointLevel::level); found != act_levels.end()) return found->waypoint;
    return -1;
}

// FUN_00463740: whether subtile (x, y) lies in the room holding subtile
// (from_x, from_y) or in one of that room's near list (FUN_00619790).
// ponytail: rooms as gamedata.cpp's room_rects has them (a preset level's
// as 8x8 tiles), the near list as its closeness (FUN_0066bc20: under 6
// tiles apart on both axes); the level's own rooms only. Rooms are agent
// D's (units off a level's rooms).
static bool room_near_holds(const Level& level, int from_x, int from_y, int x, int y) {
    struct Rect { int x, y, width, height; };
    std::vector<Rect> rects;
    for (const auto& room : level.rooms) rects.push_back({ room.x, room.y, room.width, room.height });
    if (rects.empty())
        for (int top = 0; top < level.ds1.height(); top += 8)
            for (int left = 0; left < level.ds1.width(); left += 8) rects.push_back({ left, top, std::min(8, level.ds1.width() - left), std::min(8, level.ds1.height() - top) });
    const auto holds = [](const Rect& rect, int sub_x, int sub_y) {
        return sub_x >= rect.x * 5 && sub_y >= rect.y * 5 && sub_x < (rect.x + rect.width) * 5 && sub_y < (rect.y + rect.height) * 5;
    };
    const auto own = std::ranges::find_if(rects, [&](const Rect& rect) { return holds(rect, from_x, from_y); });
    if (own == rects.end()) return false;
    return std::ranges::any_of(rects, [&](const Rect& rect) {
        const int gap_x = own->x < rect.x ? rect.x - own->width - own->x : own->x - rect.width - rect.x;
        const int gap_y = own->y < rect.y ? rect.y - own->height - own->y : own->y - rect.height - rect.y;
        return gap_x < 6 && gap_y < 6 && holds(rect, x, y);
    });
}

auto World::act_as(Player& player) -> void {
    actor = &player;
    fight.act_as(player);
}

auto World::add_player() -> Player& {
    auto& player = *players.emplace_back(std::make_unique<Player>());
    player.id = players.size() > 1 ? players[players.size() - 2]->id + 1 : 1;
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

auto World::view() const -> View {
        View view;
        view.level = level;
        view.player = actor->player;
        view.running = actor->running;
        view.dead = fight.dead();
        view.poisoned = std::ranges::any_of(actor->regen, [](const Fight::Regen& regen_entry) { return regen_entry.poison; });
        view.chilled = fight.chill_rate(now) != 0;
        view.stamina_blue = now < actor->stamina_pot.until || (now < actor->boost.until && shrine_code(actor->boost.shrine) == 14);   // 14: the stamina shrine
        view.curse = now < actor->curses[0].until ? actor->curses[0].skill : -1;
        view.stunned = now < actor->stunned;
        view.pmode = actor->pmode;
        view.prate = actor->prate;
        view.walk_percent = walk_percent(*actor, now);
        view.seq.assign(actor->seq.begin(), actor->seq.end()); view.seq_frame_ms = actor->seq_frame_ms; view.seq_loop = actor->seq_loop;
        view.gfx = fight.gfx();
        if (actor->merc && actor->merc_npc) {
            view.merc = View::Merc{ actor->merc->unit, actor->merc_npc, actor->merc->mode };
            const auto fighter = fight.merc_fighter();
            const auto gear = fight.merc_gear();
            const auto& stats = actor->merc_st;
            View::MercSheet sheet;
            sheet.level = stats.level;
            sheet.str = int(stats.str + gear.sum[std::size_t(d2d::d2s::kStr)]);
            sheet.dex = int(stats.dex + gear.sum[std::size_t(d2d::d2s::kDex)]);
            sheet.min = fighter.min; sheet.max = fighter.max; sheet.defense = fighter.defense; sheet.res = fighter.res;
            sheet.life = actor->merc->hit_points; sheet.max_life = fight.merc_max_life();
            sheet.exp = actor->character.header.merc_exp;
            if (const auto* row = d2d::rules::hireling_row(game_data->rules, int(actor->character.header.merc_type), 1)) {
                sheet.next = std::int64_t(stats.level + 2) * row->exp_per_level * (stats.level + 1) * (stats.level + 1);   // the merc_stats level rule's next step
                if (const auto* at_level = d2d::rules::hireling_row(game_data->rules, int(actor->character.header.merc_type), stats.level)) sheet.cls = at_level->cls;
            }
            view.merc_sheet = sheet;
        }
        for (const auto& pet : actor->pets)
            if (pet.where == level) {
                const auto* skill = game_data->skills.get(pet.skill);
                view.pets.push_back({ pet.monster.npc, pet.monster.unit, pet.monster.mode, skill ? std::string_view(skill->pettype) : std::string_view{},
                                      std::clamp(pet.monster.hit_points * 100 / std::max(pet.monster.stats.hit_points, 1), 0, 100) });
            }
        // Run on draws RN only while they walk and have stamina (FUN_0057f090), as our own player.
        const auto running = [](const Player& player) { return player.running && player.player.walking && player.character.stats.values[d2d::d2s::kStamina] > 0; };
        for (const auto& player : players)                    // the others in the game, as their client draws them
            if (player.get() != actor) {
                const int cls = std::max(player->character.character_class, 0);
                std::vector<std::string_view> states;
                for (const auto& state : player->self_states)
                    if (const auto* skill = now < state.until ? game_data->skills.get(state.skill) : nullptr) states.push_back(skill->aurastate);
                view.others.push_back({ player->player, cls, player->character.name, std::uint32_t(player->id),
                                        player->character.appearance ? *player->character.appearance : game_data->starting_gear[std::size_t(cls)],
                                        player->ghost ? -1 : player->pmode, running(*player), std::move(states), player->ghost, walk_percent(*player, now) });
            }
        for (const auto& player : players)
            if (player->body && player->body->level == level)
                view.bodies.push_back({ player->body->x, player->body->y, player->body->dir, player->body->gfx, player->id, std::max(player->character.character_class, 0),
                                        player->body->raised, player->body->raisers });
        view.ghost = actor->ghost;
        view.all_dead = players.size() > 1 && !anyone_alive();
        if (level == fight.mon_level) {
            // What's near the player (D2 sends the units of the rooms round
            // each player). ponytail: a radius of 28 cells, not rooms; waits for
            // networking (what a client is sent).
            for (const auto& monster : fight.monsters)
                if (!monster.gone && std::abs(monster.unit.x - actor->player.x) < 28 && std::abs(monster.unit.y - actor->player.y) < 28) {
                    view.monsters.push_back(monster);
                    view.monsters.back().npc.colour = monster_colour(monster);
                }
            for (const auto& missile : fight.missiles) if (missile.info) view.missiles.push_back({ missile.info, missile.x, missile.y, missile.dir, missile.born, missile.sound });
        }
        if (actor->attack_mon >= 0 && std::size_t(actor->attack_mon) < fight.monsters.size()) view.attack = fight.monsters[std::size_t(actor->attack_mon)].id;
        view.attack_skill = actor->attack_skill;
        if (loot.ground_level == level) view.ground = loot.ground;
        for (const auto& fire : fires) if (fire.level == level) view.fires.push_back({ fire.x, fire.y, fire.npc, fire.born });
        for (std::size_t k = 0; k < actor->corpses.size(); ++k)
            if (actor->corpses[k].level == level) view.corpses.push_back({ actor->corpses[k].x, actor->corpses[k].y, actor->corpses[k].dir, actor->corpses[k].gfx, int(k) });
        for (int k = 0; k < int(portal.size()); ++k)
            if (portal[std::size_t(k)].level == level)
                view.portals.push_back({ portal[std::size_t(k)].x, portal[std::size_t(k)].y,
                                         k == 6 ? d2d::rules::SevenTombsQuest::kLair
                                                : portal[std::size_t(k ^ 1)].level ? portal[std::size_t(k ^ 1)].level->id : d2d::rules::CainQuest::kTristram,
                                         portal[std::size_t(k)].born, k });
        if (cain_portal.level == level) view.portals.push_back({ cain_portal.x, cain_portal.y, 1, cain_portal.born, int(kGamePortals) });
        for (const auto& gate : cage_portal)
            if (gate.level == level) view.portals.push_back({ gate.x, gate.y, 1, gate.born, int(kGamePortals) });
        view.npc_states = npc_states;
        grow_states(view.npc_states, *level);
        view.npc_states.resize(level->npcs.size());
        for (const auto& neighbour : level->nearby) {           // then the neighbours', Level::nearby order
            const auto found = other_npcs.find(neighbour.level);
            auto states = found != other_npcs.end() ? found->second : npc_start(*neighbour.level);
            grow_states(states, *neighbour.level);
            states.resize(neighbour.level->npcs.size());
            view.npc_states.insert(view.npc_states.end(), states.begin(), states.end());
        }
        if (now < actor->boost.until) { view.boost = actor->boost.stats; view.boost_code = shrine_code(actor->boost.shrine); }
        view.aura = actor->aura;
        view.gold_lost = int(actor->gold_lost);
        for (const auto& state : actor->self_states) view.buffs.push_back(state.skill);
        view.day = day;
        view.den_cleared = den.state >= 4;
        {   // FUN_00544190: a record's +0xe8 if it has one (Tools), else FUN_00543f90
            // ponytail: worked out as the view's built, not sent as it changes;
            // the send's skip for done quests (bits 0 / 15 without 13 / 14) reads as 0;
            // waits for networking (deltas).
            const auto& quest_bits = actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))];
            act1_log(view.quest_log, quest_bits);
            act2_log(view.quest_log, quest_bits);
            act3_log(view.quest_log, quest_bits);
            act4_log(view.quest_log, quest_bits);
            act5_log(view.quest_log, quest_bits);
            for (int quest = 1; quest < int(view.quest_log.size()); ++quest) {
                const bool bit0 = d2d::rules::quest::bit(quest_bits, quest, 0) || d2d::rules::quest::bit(quest_bits, quest, 15);
                if (bit0 && !d2d::rules::quest::bit(quest_bits, quest, 13) && !d2d::rules::quest::bit(quest_bits, quest, 14)) view.quest_log[std::size_t(quest)] = 0;
            }
            for (std::size_t quest = 1; quest < view.game_quests.size(); ++quest)
                view.game_quests[quest] = std::uint16_t((game_quest_done(quest) ? 1 << 13 : 0)
                                                        | (closed_at_join[quest] ? 1 << 15 : 0));
        }
        view.den_left = std::max(den_left, 0);
        {
            d2d::rules::StatSum sum{};
            (void)fight.player_fighter(nullptr, nullptr, nullptr, &sum);
            view.light_bonus = sum.size() > d2d::d2s::kLightRadius ? int(sum[d2d::d2s::kLightRadius]) : 0;
            view.attack_lines = actor->character.panel.attack;
            if (actor->store.npc >= 0) {
                view.store = actor->store;
                view.store->reduced = int(sum[d2d::d2s::kReducedPrices]);   // the player's gear: off what the store charges
            }
        }
        view.has_character = true;
        view.header = actor->character.header; view.stats = actor->character.stats; view.items = actor->character.items; view.held = actor->held;
        view.hire_offers = hire_offers(act());
        return view;
    }

auto World::save() -> std::string {
        if (!characters) return "no character store";
        stow_held(*game_data, actor->character.items, actor->held);                  // the item in hand goes back first
        auto header = actor->character.header;
        if (header.right_skill == std::uint32_t(kResurrectSkill)) header.right_skill = actor->right_before;   // co-op's only: never in a save
        if (header.right_swap == std::uint32_t(kResurrectSkill)) header.right_swap = 0;
        header.level = std::uint8_t(std::clamp<std::int64_t>(actor->character.stats.get(d2d::d2s::kLevel), 1, 99));
        header.last_played = std::uint32_t(std::time(nullptr));
        header.map_id = game_data->map_seed;
        if (actor->character.appearance)   // the save's tints leave states out (FUN_0062c100's state loop sets no tint byte)
            header.set_look(game_data->item_pieces.empty() || game_data->comp.empty() ? *actor->character.appearance : game_data->look_of(actor->character.items));
        // The corpse list holds one: the latest corpse's items (PlrSave2.cpp).
        // ponytail: game.exe's pick among several isn't traced.
        const std::vector<d2d::d2s::Item> no_corpse;
        const auto err = characters->save(header, fight.own_stats(), actor->character.items, actor->corpses.empty() ? &no_corpse : &actor->corpses.back().items);
        if (err.empty()) actor->character.header.last_played = header.last_played;
        d2d::log::info("save {}: {}", header.name, err.empty() ? "written" : err);
        return err;
    }

// A blast's damage taken (FUN_0057c6c0): life under one point after it is 0.
static void take_blast(std::int64_t& life, int taken) {
    if (taken <= 0) return;
    life -= taken;
    if (life < 256) life = 0;
}

// FUN_00622b50 (mask 0x804): rules::sight_blocked from the unit to the
// object over the walk grid's missile barrier (0x04) and doors (0x800).
auto World::blast(const Npc& object, float x, float y, int size, std::int64_t life, int unit_level, int dexterity, int defense) -> int {
    if (in_town(level)) return 0;
    const auto wall = [&](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x804); };
    const auto subtile = [](float cell) { return int(std::floor(cell * 5)); };
    if (d2d::rules::sight_blocked(subtile(x), subtile(y), size, subtile(object.x), subtile(object.y), object.size_x, wall)) return 0;
    return d2d::rules::object_blast(life, unit_level, dexterity, defense, object.seed);
}

auto World::operate(int npc_index, std::uint32_t now_ms, int force ) -> void {   // force (devctl): the shrine row / trap to play
        using namespace d2d::d2s;
        const auto& object = level->npcs[std::size_t(npc_index)];
        const int diff = actor->character.header.active_difficulty();
        if (object.operate_fn == d2d::rules::ToolsQuest::kStand) { malus_stand(npc_index, now_ms); return; }
        if (is_door(object.operate_fn)) { operate_door(npc_index, now_ms); return; }
        if (object.operate_fn == d2d::rules::operate_fn::kExplodingBarrel) { explode(npc_index, now_ms); return; }
        const auto& area_levels = game_data->area_level;
        auto alvl = [&](int id) { return std::size_t(id) < area_levels.size() ? area_levels[std::size_t(id)][std::size_t(std::clamp(diff, 0, 2))] : 1; };
        const int here = alvl(level->id);
        // Open a container (open_container) on the level's object seed, its
        // rounds its level's act's chest class (chest_round, FUN_00585b90) off its own unit seed;
        // the items are made at the area level (FUN_0055a550), the extras too.
        auto open = [&] {
            const int act = d2d::rules::level_act(level->id);
            const auto [low, high] = d2d::rules::kChestLevels[std::size_t(act)];
            const auto treasure_class = d2d::rules::chest_tc(act, diff, here, alvl(low), alvl(high));
            std::vector<d2d::rules::Drop> drops;
            const auto opened = d2d::rules::open_container(object.operate_fn, object.object_id, object.locked, object.sparkle, fight.spawning.objects,
                                                           [&](int forced) { return d2d::rules::chest_round(game_data->rules, treasure_class, object.seed, drops, forced); });
            for (const auto& code : opened.extra) drops.push_back({ .code = code });
            if (!fight.remote_monsters)              // a joined game: the host drops the chest's items (S->C 0x9c)
                for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
            return std::pair{ opened, drops.size() };
        };
        auto to_mode = [&](int mode) {               // FUN_00624690 and its footprint (FUN_00623830); its sound is the client's (Town::object_sounds)
            set_footprint(*level, object, object.collision >> mode & 1);
        };
        // A trap object (30: "a trap", the exploding chest; FUN_00581cd0), in
        // mode 0: a physical then a fire blast on its opener (FUN_005dfa00
        // types 0, 1: blast), then mode 1; its event 1 (FUN_005417d0,
        // FrameCnt1 + 1 frames on) takes it to mode 2, the tick's OP -> ON.
        if (object.operate_fn == d2d::rules::operate_fn::kTrapObject) {
            if (operated.contains({ level, npc_index })) return;
            const auto& fighter = actor->player_combat;
            auto& life = actor->character.stats.values[d2d::d2s::kLife];
            for (int kind = 0; kind < 2; ++kind) {
                const int damage = blast(object, actor->player.x, actor->player.y, 2, life, int(actor->character.stats.get(kLevel)),
                                         int(actor->character.stats.get(kDex) + actor->psum[kDex]), fighter.defense);
                take_blast(life, kind ? d2d::rules::blast_taken(damage, fighter.mdr, fighter.res[0]) : d2d::rules::blast_taken(damage, fighter.dr_flat, fighter.dr_pct));
            }
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            d2d::log::info("trap object {} went off", npc_index);
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kWell) {               // a well (FUN_005858a0): 2 x Parm2 drinks (InitFn 16), NU -> OP -> ON
            auto& well = doors.try_emplace({ level, npc_index }, Door{ 0, 0 }).first->second;
            auto& stat_values = actor->character.stats.values;
            if (well.mode >= 2) return;
            const bool drank = d2d::rules::well_drink(stat_values[kLife], stat_values[kMaxLife], stat_values[kMana], stat_values[kMaxMana],
                                                      stat_values[kStamina], stat_values[kMaxStamina]);
            if (!fight.cure(now_ms) && !drank) return;   // a cure counts as a drink too
            well = { well.mode + 1, now_ms };
            to_mode(well.mode);
            d2d::log::info("well {}: mode {}", npc_index, kObjectModes[std::size_t(well.mode)]);
            return;
        }
        // Containers (open_container): a casket (1) opens only when its round
        // drops. Caskets and barrels raise one of the level's undead
        // (FUN_005474c0 / FUN_00582280); all but the barrel spring their trap
        // (InitFn 2's; FUN_00582510: arm_trap). Opened, mode 1; event 1
        // (FUN_005417d0, FrameCnt1 + 1 frames on) takes it to mode 2: the
        // tick's OP -> ON. A player opening a barrel kicks it (FUN_005868a0:
        // skill 1, Kick, its KK through FUN_00580a70 at the barrel) as it
        // breaks; the exploding barrel (FUN_00584330) has no kick. The kick's
        // hit (FUN_0056caf0 -> FUN_0057ca80) lands on players and monsters
        // only, so on the barrel it does nothing.
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kCasket || operate_id == d2d::rules::operate_fn::kUrn || operate_id == d2d::rules::operate_fn::kBarrel || operate_id == d2d::rules::operate_fn::kCorpse) {
            if (operate_id == d2d::rules::operate_fn::kBarrel && actor->pmode < 0) {
                actor->swing_skill = 0;
                actor->attack_mon = -1;
                actor->seq = {};
                actor->smove = {};
                actor->kicks_left = 0;
                actor->player.dir = direction16(object.x - actor->player.x, object.y - actor->player.y);
                fight.set_pmode(kModeKK, now_ms);
            }
            const auto [opened, drops] = open();
            if (!opened.opened) return;
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            if (operate_id == d2d::rules::operate_fn::kCorpse) cues.cue("object_corpse_loot", now_ms, object.x, object.y);
            if (opened.undead) spring_trap(8, object.x, object.y, here, now_ms, 1);
            if (const int trap = force >= 0 ? force : object.trap; trap && operate_id != d2d::rules::operate_fn::kBarrel) arm_trap(trap, object.x, object.y, here, now_ms);
            d2d::log::info("opened object {} (op {})", npc_index, operate_id);
            return;
        }
        // An armor stand's armor (19) or a weapon rack's weapon (20)
        // (stand_item) off its room's seed at the area level less one, made
        // off that seed too (FUN_00558d90), its quality rolled (Drop quality
        // 0: stand_quality); a bookshelf (26) its book (open_container).
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kArmorStand || operate_id == d2d::rules::operate_fn::kWeaponRack || operate_id == d2d::rules::operate_fn::kBookshelf) {
            operated[{ level, npc_index }] = now_ms;
            to_mode(2);
            const int ilvl = here > 1 ? here - 1 : here;
            if (operate_id == d2d::rules::operate_fn::kBookshelf) open();
            else if (auto& room_seeds = fight.spawning.levels[level].room_seeds; std::size_t(object.room) < room_seeds.size()) {
                auto& room_seed = room_seeds[std::size_t(object.room)];
                if (const auto code = d2d::rules::stand_item(game_data->rules, operate_id == d2d::rules::operate_fn::kWeaponRack, ilvl, room_seed); !code.empty())
                    loot.put({ .code = code, .quality = 0 }, object.x, object.y, ilvl, room_seed, now_ms);
            }
            d2d::log::info("opened object {} (op {})", npc_index, operate_id);
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kChest && object.locked) {           // a key from the inventory (FUN_0055f140: item type key)
            const auto key = std::ranges::find_if(actor->character.items, [](const Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory && item.code == "key"; });
            if (key == actor->character.items.end()) { d2d::log::info("I need a key."); return; }
            if (key->quantity > 1) --key->quantity;
            else actor->character.items.erase(key);
            cues.cue("item_key_used", now_ms, actor->player.x, actor->player.y);
        }
        // The Moldy Tome (OperateFn 6, FUN_00594e70): opens once (mode 0 → 1),
        // read every time while the quest's on: message 127 to the player
        // (FUN_005456a0: S->C 0x27, mode 2, the tome's unit id, +10 the
        // message), which the client plays in the speech box (FUN_004a1600
        // → FUN_004a1320) and answers as heard (cmd::QuestMessage).
        if (object.operate_fn == d2d::rules::operate_fn::kMoldyTome) {
            operated.try_emplace({ level, npc_index }, now_ms);
            if (!closed_at_join[std::size_t(d2d::rules::TowerQuest::kQuest)]) actor->events.push_back(ev::Speech{ npc_index, d2d::rules::TowerQuest::kTome });
            return;
        }
        // A teleport pad (OperateFn 27, FUN_00581bf0): another pad of its row
        // in its own room, else in a room near it (FUN_00619790's list),
        // and the player to a free spot by it (FUN_00581b50: FUN_0064e7b0,
        // size 3), a joined game's by the host (0x15). Used every time.
        if (object.operate_fn == d2d::rules::operate_fn::kTeleportPad) {
            if (fight.remote_monsters) return;
            auto pad_in = [&](int room) {
                for (std::size_t index = 0; index < level->npcs.size(); ++index) {
                    const auto& other = level->npcs[index];
                    if (int(index) != npc_index && other.root == "objects" && other.object_id == object.object_id && other.room == room) return int(index);
                }
                return -1;
            };
            int target = pad_in(object.room);
            for (const auto& [room_level, room] : near_rooms(*game_data, *level, object.x, object.y))
                if (target < 0 && room_level == level && room != object.room) target = pad_in(room);
            if (target < 0) return;
            const auto [spot_x, spot_y] = level->nearest_free(level->npcs[std::size_t(target)].x, level->npcs[std::size_t(target)].y);
            actor->player.x = spot_x; actor->player.y = spot_y;
            actor->player.path.clear(); actor->player.walking = false;
            actor->player.goal_x = spot_x; actor->player.goal_y = spot_y;
            actor->interact_npc = -1;
            bring_party(level, level, spot_x, spot_y, true, false);   // one screen: the party with them (the leash would undo a lone jump)
            d2d::log::info("teleport pad {} -> {}", npc_index, target);
            return;
        }
        // The Horadric Staff's chests (OperateFns 39 / 40 / 41): once (mode 0
        // only, FUN_00545850: mode 1, or 2 without a Mode1), the part, then
        // the act's chest class forced magic and rand(5) + 5 gold piles on the
        // quest seed; the shaft identified (item flag 0x10).
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kCubeChest || operate_id == d2d::rules::operate_fn::kScrollChest || operate_id == d2d::rules::operate_fn::kShaftChest) {
            if (operated.contains({ level, npc_index })) return;
            operated[{ level, npc_index }] = now_ms;
            to_mode(object.op_frames ? 1 : 2);
            if (hosted_quests) return;
            if (const auto part = staff.chest(operate_id, quests(), staff_items()); !part.empty()) {
                loot.put({ .code = part }, object.x, object.y, here, fight.spawning.game, now_ms);
                if (part == "msf") loot.ground.back().item.identified = true;
            }
            const auto [low, high] = d2d::rules::kChestLevels[1];
            std::vector<d2d::rules::Drop> drops;
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(1, diff, here, alvl(low), alvl(high)), object.seed, drops, 4);
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
            d2d::log::info("The Horadric Staff: chest {} opened ({} drops)", operate_id, drops.size());
            return;
        }
        // The Tainted Sun's altar (OperateFn 24, FUN_0059a7e0): mode 1, the
        // sun back, then at it a Viper Amulet each (identified, item flag
        // 0x10), the act's chest class with quality forced magic
        // (FUN_00585b90, EDX 4) and rand(5) + 5 gold piles on the quest seed
        // (FUN_00585970); a joined game's are the host's.
        if (object.operate_fn == d2d::rules::operate_fn::kSunAltar) {
            if (hosted_quests) return;
            const auto use = tainted.use_altar(quests(), game_bits, carries("vip"), carries("hst"));
            if (use.refused) { d2d::log::info("I can't use this yet."); return; }   // voice 0x13
            if (!use.used) return;
            if (use.took_part) music_cue("music_quest_tainted");
            operated.try_emplace({ level, npc_index }, now_ms);
            to_mode(1);
            if (use.took_part) class_cue("act2_complete_tainted", now_ms);
            for (int amulet = 0; amulet < use.amulets; ++amulet) {
                loot.put({ .code = "vip" }, object.x, object.y, here, fight.spawning.game, now_ms);
                loot.ground.back().item.identified = true;
            }
            const auto [low, high] = d2d::rules::kChestLevels[1];
            std::vector<d2d::rules::Drop> drops;
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(1, diff, here, alvl(low), alvl(high)), object.seed, drops, 4);
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
            d2d::log::info("The Tainted Sun: the altar, the sun's back ({} drops)", drops.size() + std::size_t(use.amulets));
            return;
        }
        // Lam Esen's tome on its stand (OperateFn 28, FUN_005b7a60): once, it
        // drops there and the stand goes to mode 2; one done gets nothing.
        if (object.operate_fn == d2d::rules::operate_fn::kLamTome) {
            if (hosted_quests || !lam_esen.take(quests())) return;
            loot.put({ .code = d2d::rules::LamEsenQuest::kTome }, object.x, object.y, here, fight.spawning.game, now_ms);
            doors[{ level, npc_index }] = Door{ 2, now_ms };
            to_mode(2);
            d2d::log::info("Lam Esen's Tome: taken from its stand");
            return;
        }
        // Khalim's chests (OperateFns 57 / 58 / 59): rand(5) + 5 gold piles on
        // the quest seed (FUN_00585970), the part for one who needs it (its
        // Will not made, Khalim's Will not done), a chest's drops (FUN_00585b90).
        // ponytail: "needs it" is not carried (the per-player count's callbacks, LAB_005b8770.., read as that).
        if (const int operate_id = object.operate_fn; operate_id == d2d::rules::operate_fn::kKhalimHeart || operate_id == d2d::rules::operate_fn::kKhalimEye
                                                       || operate_id == d2d::rules::operate_fn::kKhalimBrain) {
            if (operated.contains({ level, npc_index })) return;
            operated[{ level, npc_index }] = now_ms;
            to_mode(object.op_frames ? 1 : 2);
            if (hosted_quests) return;
            using K = d2d::rules::KhalimQuest;
            std::vector<d2d::rules::Drop> drops;
            for (std::uint32_t gold = fight.spawning.quests.next() % 5 + 5; gold > 0; --gold) drops.push_back({ .code = "gld" });
            const char* part = operate_id == d2d::rules::operate_fn::kKhalimHeart ? K::kHeartCode : operate_id == d2d::rules::operate_fn::kKhalimEye ? K::kEyeCode : K::kBrainCode;
            if (!d2d::rules::quest::bit(quests(), K::kQuest, 0) && !carries(part) && !carries(K::kWillCode)) drops.push_back({ .code = part });
            const auto [low, high] = d2d::rules::kChestLevels[std::size_t(std::clamp(act(), 0, 2))];
            d2d::rules::chest_round(game_data->rules, d2d::rules::chest_tc(act(), diff, here, alvl(low), alvl(high)), object.seed, drops, 4);
            for (const auto& dropped : drops) loot.put(dropped, object.x, object.y, here, fight.spawning.game, now_ms);
            d2d::log::info("Khalim's Will: chest {} opened ({} drops)", operate_id, drops.size());
            return;
        }
        // The sewers' lever (OperateFn 45, FUN_005b8530): the stairs (object 366)
        // to mode 1, then 2: the way down to the sewers' second level opens.
        if (object.operate_fn == d2d::rules::operate_fn::kSewerLever) {
            if (hosted_quests || !khalim.pull_lever()) return;
            to_mode(1);
            cues.cue("object_sewer_water", now_ms, object.x, object.y);
            doors[{ level, npc_index }] = Door{ 1, now_ms };
            for (std::size_t i = 0; i < level->npcs.size(); ++i)
                if (level->npcs[i].operate_fn == d2d::rules::operate_fn::kSewerStairs) doors[{ level, int(i) }] = Door{ 2, now_ms };
            d2d::log::info("Khalim's Will: the sewers' stairs open");
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kSewerStairs) return;   // FUN_005b84e0: open, its warp's taken as any
        // The compelling orb (OperateFn 53, FUN_005bb980): hit with Khalim's
        // Will in hand; the second hit breaks it: the Will goes, the orb
        // (0x16e) leaves, the object to mode 1. Else "I can't".
        if (object.operate_fn == d2d::rules::operate_fn::kCompellingOrb) {
            if (hosted_quests || temple.orb_broken) return;
            // FUN_0063bef0: the weapon in hand (either arm, item type 0x2d;
            // the set in use: a switch moves its hands there).
            const bool wields_will = std::ranges::any_of(actor->character.items, [](const d2d::d2s::Item& item) {
                return item.location == d2d::d2s::item_location::kEquipped && item.code == d2d::rules::KhalimQuest::kWillCode
                    && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm);
            });
            if (!wields_will) { d2d::log::info("I can't use this yet."); return; }
            if (!temple.hit_orb(quests())) { d2d::log::info("The Blackened Temple: the orb's hit"); return; }
            music_cue("music_quest_compelling");
            take_carried(d2d::rules::KhalimQuest::kWillCode);
            for (auto& monster : fight.monsters) if (std::size_t(monster.type) < game_data->mon_bin.size() && monster.type == int(game_data->mon_bin[0x16e])) monster.gone = true;
            to_mode(1);
            doors[{ level, npc_index }] = Door{ 1, now_ms };
            fight.spawning.orb_smashed = true;
            d2d::log::info("Khalim's Will: the compelling orb broken");
            return;
        }
        // The Gidbinn's altar (OperateFn 31, FUN_005b9b40): mode 1, and the
        // first use starts a3q3's timer (7 ticks: the guardians).
        if (object.operate_fn == d2d::rules::operate_fn::kGidbinnAltar) {
            if (hosted_quests || operated.contains({ level, npc_index })) return;
            operated[{ level, npc_index }] = now_ms;
            if (!blade.use_altar(quests())) return;
            to_mode(1);
            gidbinn_timer = now_ms + d2d::rules::BladeQuest::kTimerTicks * kTickMs;
            gidbinn_altar = npc_index;
            d2d::log::info("The Blade of the Old Religion: the altar used");
            return;
        }
        // The hellgate (OperateFn 46, FUN_00584750): with word 0x16 bit 13 or 0
        // (Mephisto dead), FUN_00546ac0 from the Durance's third level: unless
        // word 0x17 has bit 0, Khalim's Will closed if open (word 0x12 bits 0, 13;
        // his eye, heart, brain and the flails taken) and word 0x17 bits 0, 13.
        // Then the fortress's waypoint and its start (FUN_0053aec0(0x67)).
        // ponytail: its sounds (0xca7, 0xb56) and FUN_0053d670(6)'s packet aren't sent.
        if (act4_operate(npc_index, now_ms)) return;
        if (act5_operate(npc_index, now_ms)) return;
        if (object.operate_fn == d2d::rules::operate_fn::kHellgate) {
            auto& bits = quests();
            if (!d2d::rules::quest::bit(bits, 0x16, 13) && !d2d::rules::quest::bit(bits, 0x16, 0)) return;
            const Level* fortress = game_data->level(d2d::rules::level_ids::kPandemoniumFortress);
            if (!fortress || fortress->start.first < 0) { d2d::log::warn("The Pandemonium Fortress didn't build; the hellgate stays shut"); return; }
            if (level->id == d2d::rules::level_ids::kDuranceOfHateLevel3 && !d2d::rules::quest::bit(bits, 0x17, 0)) {
                if (!d2d::rules::quest::bit(bits, 0x12, 0)) {
                    d2d::rules::quest::set(bits, 0x12, 0); d2d::rules::quest::set(bits, 0x12, 13);
                    for (const char* code : { "qey", "qhr", "qbr", "qf1", "qf2" }) take_carried(code);
                }
                d2d::rules::quest::set(bits, 0x17, 0); d2d::rules::quest::set(bits, 0x17, 13);
                movie = 4;                                                         // S->C 0x61 {4}: Act04start
            }
            set_waypoint(waypoint_index(*game_data, d2d::rules::level_ids::kPandemoniumFortress));
            arrive(fortress, fortress->start.first, fortress->start.second, "the hellgate");
            return;
        }
        // The orifice (OperateFn 25, FUN_0059dc70): with the Horadric Staff
        // carried it takes it (FUN_0059dd80: the staff and its parts gone,
        // the Horadric Staff done, the way down to Duriel after its timer);
        // without, "I can't" (voice).
        // ponytail: game.exe opens a panel to drop the staff into (C->S 0x44 3); d2d places it on use.
        if (object.operate_fn == d2d::rules::operate_fn::kOrifice) {
            if (operated.contains({ level, npc_index }) || hosted_quests) return;
            if (!carries("hst")) { d2d::log::info("I can't use this yet."); return; }
            if (actor->held && actor->held->code == "hst") actor->held.reset();
            for (const char* code : { "hst", "vip", "msf" })
                if (const auto part = std::ranges::find(actor->character.items, std::string(code), &d2d::d2s::Item::code); part != actor->character.items.end()) actor->character.items.erase(part);
            operated[{ level, npc_index }] = now_ms;
            to_mode(1);
            tombs.staff_in(quests());
            d2d::log::info("The Horadric Staff: placed in the orifice");
            return;
        }
        // Horazon's Journal (OperateFn 42, FUN_0059b970): opened once (mode
        // 1), read every time: its 396 (FUN_005456a0, as the Moldy Tome's),
        // and the quest's (ArcaneQuest::journal).
        if (object.operate_fn == d2d::rules::operate_fn::kJournal) {
            if (operated.try_emplace({ level, npc_index }, now_ms).second) to_mode(1);
            if (!hosted_quests) arcane.journal(quests(), object.room, level->id == d2d::rules::ArcaneQuest::kSanctuary);
            actor->events.push_back(ev::Speech{ npc_index, d2d::rules::ArcaneQuest::kJournal });
            return;
        }
        // The Sanctuary's portal (OperateFn 34, FUN_005846b0): shut, it opens
        // (mode 1); open, Palace Cellar 3 and the Sanctuary take the player to
        // each other's (FUN_0053aec0), by the portal there.
        if (object.operate_fn == d2d::rules::operate_fn::kSanctuaryPortal) {
            if (operated.try_emplace({ level, npc_index }, now_ms).second) { to_mode(1); return; }
            if (fight.remote_monsters) return;
            const int destination = level->id == d2d::rules::level_ids::kArcaneSanctuary ? d2d::rules::level_ids::kPalaceCellarLevel3 : d2d::rules::level_ids::kArcaneSanctuary;
            const Level* there = game_data->level(destination);
            if (!there || there->ds1.width() == 0) { d2d::log::info("not implemented: level {} (the Sanctuary's portal)", destination); return; }
            const auto other = std::ranges::find(there->npcs, object.object_id, &Npc::object_id);
            const float to_x = other == there->npcs.end() ? float(there->ds1.width()) / 2 : other->x, to_y = other == there->npcs.end() ? float(there->ds1.height()) / 2 : other->y;
            const auto [spot_x, spot_y] = there->nearest_free(to_x, to_y + 0.6f);
            arrive(there, spot_x, spot_y, "the Sanctuary's portal");
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kCairnStone || object.operate_fn == d2d::rules::operate_fn::kGibbet || object.operate_fn == d2d::rules::operate_fn::kInifussTree) { cain_operate(npc_index, now_ms); return; }
        operated[{ level, npc_index }] = now_ms;
        if (object.operate_fn == d2d::rules::operate_fn::kChest) {
            const auto drops = open().second;
            d2d::log::info("opened a chest: {} drops{}", drops, object.trap ? std::format(", trap {}", object.trap) : "");
            if (const int trap = force >= 0 ? force : object.trap) arm_trap(trap, object.x, object.y, here, now_ms);
            return;
        }
        const int row = force >= 0 ? force : object.shrine;
        if (std::size_t(row) >= game_data->shrines.size()) return;
        const auto& shrine = game_data->shrines[std::size_t(row)];
        auto& stat_values = actor->character.stats.values;
        d2d::rules::shrine_recharge(shrine, stat_values[kLife], stat_values[kMaxLife], stat_values[kMana], stat_values[kMaxMana]);
        // A booster's state ends the one before (curse = 1, FUN_0056e970);
        // the stamina state's end (FUN_00583a40) and its start fill stamina.
        if (auto boost = d2d::rules::shrine_boost(shrine, actor->player_combat.attack_rating); !boost.empty()) {
            if (shrine.code == 14 || shrine_code(actor->boost.shrine) == 14) stat_values[kStamina] = stat_values[kMaxStamina];
            actor->boost = { row, std::move(boost), now_ms + std::uint32_t(shrine.duration) * 40u };
        }
        if (const auto sound = d2d::rules::shrine_sound(shrine.code); !sound.empty()) cues.cue(sound, now_ms, object.x, object.y);
        if (shrine.code == 17) open_portal_at(actor->player.x + 1, actor->player.y + 1, now_ms);   // portal (FUN_00582a30): 5 subtiles on each axis
        if (shrine.code == 18)                                  // gem: one up, or a chipped gem at the player's feet
            if (const auto code = d2d::rules::gem_shrine(game_data->rules, actor->character.items, rng); !code.empty())
                loot.put({ .code = code }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
        // Storm (FUN_00582da0): everyone about loses Arg0 % of their life.
        // ponytail: "about" as within 30 cells (game.exe's unit search
        // over Arg1 isn't traced).
        if (shrine.code == 19) {
            stat_values[kLife] -= stat_values[kLife] * shrine.arg0 / 100;
            actor->merc->hit_points -= actor->merc->hit_points * shrine.arg0 / 100;
            if (fight.mon_level == level)
                for (auto& monster : fight.monsters)
                    if (monster.alive() && std::abs(monster.unit.x - object.x) < 30 && std::abs(monster.unit.y - object.y) < 30) monster.hit_points -= monster.hit_points * shrine.arg0 / 100;
        }
        // Exploding / Poison: Arg0 .. Arg1 - 1 exploding (opm) / choking gas
        // (gpm) potions at the player's feet (FUN_005830e0 / FUN_00583410).
        if (shrine.code == 21 || shrine.code == 22)
            for (int count = shrine.arg0 + rng(std::max(shrine.arg1 - shrine.arg0, 0)); count > 0; --count)
                loot.put({ .code = shrine.code == 21 ? "opm" : "gpm" }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
        if ((shrine.code == 19 || shrine.code == 21 || shrine.code == 22) && fight.mon_level == level)
            fight.shrine_missiles(shrine.code, object.x, object.y, int(actor->character.stats.get(kLevel)), now_ms);
        if (shrine.code == 20 && fight.mon_level == level) {   // warping (FUN_00583050): the nearest plain monster turns boss
            // ponytail: FUN_00582750's filter read as alive, not a boss, not
            // an NPC; FUN_0065a800's search range isn't traced.
            int best = -1;
            float best_distance = 1e9f;
            for (std::size_t k = 0; k < fight.monsters.size(); ++k) {
                const auto& monster = fight.monsters[k];
                const float distance = std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y);
                if (monster.alive() && monster.boss == d2d::rules::Boss::none && distance < best_distance) { best_distance = distance; best = int(k); }
            }
            if (best >= 0) {                               // FUN_005a4940: FUN_005a0760 (champions allowed), FUN_005a2120
                auto& monster = fight.monsters[std::size_t(best)];
                const auto boss = d2d::rules::roll_boss(game_data->umods, game_data->monsters.types[std::size_t(monster.type)], diff, true, rng);
                make_boss(*game_data, monster, boss.kind, boss.mods, -1, boss.name_seed, diff, rng, int(players.size()));
                cues.cue("object_shrine_jail", now_ms, monster.unit.x, monster.unit.y);
                d2d::log::info("warping shrine: {} is now a {}", monster.npc.name, boss.kind == d2d::rules::Boss::champion ? "champion" : "unique");
            }
        }
        d2d::log::info("shrine {} (code {})", row, shrine.code);
    }

auto World::operate_door(int npc_index, std::uint32_t now_ms) -> void {
        const auto& door = level->npcs[std::size_t(npc_index)];
        auto& state = doors.try_emplace({ level, npc_index }, Door{ mode_index(door.mode), 0 }).first->second;
        int mode = -1;
        if (door.operate_fn == d2d::rules::operate_fn::kSecretDoor) {                   // secret door (FUN_00583ff0): slides open (OP), once
            if (state.mode == 0) mode = 1;
        } else if (door.operate_fn == d2d::rules::operate_fn::kTrapDoor) {            // trap door (FUN_00581eb0): opens, then down its room's warp (FUN_005550b0)
            // ponytail: the level's nearest warp for the room's warp unit (trap
            // doors are past act 1).
            if (state.mode == 0) mode = 2;
            else if (state.mode == 2 && !level->warps.empty())
                actor->take_warp = int(std::ranges::min_element(level->warps, {}, [&](const Level::Warp& warp) { return std::hypot(warp.x - door.x, warp.y - door.y); }) - level->warps.begin());
        } else if (now_ms - state.when >= 500) {       // a door (FUN_00581d40): is anyone in the doorway?
            // ponytail: a unit's own subtile, not its collision pattern.
            auto in_door = [&](float x, float y) {
                const int sub_x = int(x * 5) - (int(door.x * 5) - door.size_x / 2), sub_y = int(y * 5) - (int(door.y * 5) - door.size_y / 2);
                return sub_x >= 0 && sub_y >= 0 && sub_x < door.size_x && sub_y < door.size_y;
            };
            bool occupied = in_door(actor->player.x, actor->player.y) || (actor->merc && in_door(actor->merc->unit.x, actor->merc->unit.y));
            if (fight.mon_level == level)
                for (const auto& monster : fight.monsters) occupied = occupied || (monster.alive() && in_door(monster.unit.x, monster.unit.y));
            mode = d2d::rules::door_mode(state.mode, occupied);
        }
        if (mode >= 0) set_door_mode(npc_index, mode, now_ms);
    }

auto World::reduced_prices() const -> int {
        d2d::rules::StatSum sum{};
        (void)fight.player_fighter(nullptr, nullptr, nullptr, &sum);
        return int(sum[d2d::d2s::kReducedPrices]);
    }

// Rooms laid again (relevel) stamp every door as its preset left it; the
// doors used since go back to their mode's footprint.
// ponytail: every frame, a few doors; on relevel only if it shows.
auto World::reopen_doors() -> void {
        for (const auto& [key, door] : doors)
            if (key.first == level && std::size_t(key.second) < level->npcs.size())
                set_footprint(*level, level->npcs[std::size_t(key.second)], level->npcs[std::size_t(key.second)].collision >> door.mode & 1);
    }

auto World::set_door_mode(int npc_index, int mode, std::uint32_t now_ms) -> void {
        const auto& door = level->npcs[std::size_t(npc_index)];
        auto& state = doors.try_emplace({ level, npc_index }, Door{ mode_index(door.mode), 0 }).first->second;
        if (state.mode == mode) return;
        state = { mode, now_ms };
        set_footprint(*level, door, door.collision >> mode & 1);   // FUN_00623830 out / FUN_00620a70 back in
        d2d::log::info("door {}: mode {}", npc_index, kObjectModes[std::size_t(mode)]);
    }

// A monster's door at a think (FUN_005b0f50): of the level's closed (mode
// 0) IsDoor objects, the nearest under 9 subtiles squared (FUN_005dd0b0
// mode 8, rules::door_pick); with MonsterOK, FUN_00584540 operates it
// (OperateFn 8) when in reach (FUN_00623660, rules::object_reach). Found,
// the monster stands 5 either way.
// ponytail: the level's objects in list order, not the near rooms' unit
// lists (a tie between two doors may go the other way); room unit lists
// are agent D's.
auto World::monster_door(const Monster& monster, std::uint32_t now_ms) -> bool {
        if (fight.mon_level != level) return false;
        const int x = int(std::floor(monster.unit.x * 5)), y = int(std::floor(monster.unit.y * 5));
        std::vector<std::pair<int, int>> spots;
        std::vector<int> door_indices;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& door = level->npcs[i];
            const auto state = doors.find({ level, int(i) });
            if (!door.door || (state != doors.end() ? state->second.mode : mode_index(door.mode)) != 0) continue;
            spots.emplace_back(int(door.x * 5) - x, int(door.y * 5) - y);
            door_indices.push_back(int(i));
        }
        const int pick = d2d::rules::door_pick(spots);
        if (pick < 0 || !level->npcs[std::size_t(door_indices[std::size_t(pick)])].monster_ok) return false;
        const auto& door = level->npcs[std::size_t(door_indices[std::size_t(pick)])];
        const auto [dx, dy] = spots[std::size_t(pick)];
        if (d2d::rules::object_reach(-dx, -dy, game_data->monsters.types[std::size_t(monster.type)].size, door.size_x, door.size_y))
            operate_door(door_indices[std::size_t(pick)], now_ms);
        return true;
    }

auto World::explode(int npc_index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        const auto& barrel = level->npcs[std::size_t(npc_index)];
        operated[{ level, npc_index }] = now_ms;
        // FUN_00584240: a physical blast (FUN_005dfa00 type 0) on each live
        // unit within 3 subtiles.
        // ponytail: the level's units in the order player, merc, monsters,
        // not its room's unit list (the seed's draws go by that order); a
        // monster's and the merc's life in whole points, not the game's
        // 256ths; the merc's dexterity isn't kept (MercStats), so its roll
        // takes 255, the 65 % floor every hireling's own dexterity puts it at.
        auto nearby = [&](float x, float y, float subtiles) { return std::hypot(x - barrel.x, y - barrel.y) * 5 <= subtiles; };
        const auto& fighter = actor->player_combat;
        if (nearby(actor->player.x, actor->player.y, 3) && actor->character.stats.values[kLife] > 0)
            take_blast(actor->character.stats.values[kLife], d2d::rules::blast_taken(blast(barrel, actor->player.x, actor->player.y, 2, actor->character.stats.values[kLife], int(actor->character.stats.get(kLevel)),
                                                                                    int(actor->character.stats.get(kDex) + actor->psum[kDex]), fighter.defense),
                                                                              fighter.dr_flat, fighter.dr_pct));
        if (actor->merc && nearby(actor->merc->unit.x, actor->merc->unit.y, 3) && actor->merc->hit_points > 0) {
            const auto merc_fighter = fight.merc_fighter();
            actor->merc->hit_points -= d2d::rules::blast_taken(blast(barrel, actor->merc->unit.x, actor->merc->unit.y, 2, std::int64_t(actor->merc->hit_points) << 8, actor->merc_st.level, 255, merc_fighter.defense),
                                                       merc_fighter.dr_flat, merc_fighter.dr_pct) >> 8;
        }
        if (fight.mon_level == level)
            for (std::size_t k = 0; k < fight.monsters.size(); ++k) {
                auto& monster = fight.monsters[k];
                if (!monster.alive() || !nearby(monster.unit.x, monster.unit.y, 3)) continue;
                const auto target = monster.target(*game_data);
                const int damage = d2d::rules::blast_taken(blast(barrel, monster.unit.x, monster.unit.y, game_data->monsters.types[std::size_t(monster.type)].size,
                                                                 std::int64_t(monster.hit_points) << 8, monster.stats.level, 0, monster.stats.armor_class), 0, target.res[0]);
                if (hurt(*game_data, monster, damage >> 8, now_ms)) fight.killed(k, now_ms);
            }
        for (std::size_t k = 0; k < level->npcs.size(); ++k)          // the next barrels along (class 11, still NU)
            if (const auto& other = level->npcs[k]; other.object_id == d2d::rules::object_ids::kExplodingBarrel && !other.preoperated && !operated.contains({ level, int(k) }) && std::hypot(other.x - barrel.x, other.y - barrel.y) * 5 < 3)
                explode(int(k), now_ms);
        set_footprint(*level, barrel, barrel.collision >> 1 & 1);
        d2d::log::info("barrel {} exploded", npc_index);
    }

auto World::arm_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms) -> void {
        if (trap == 8 && d2d::rules::trap_undead(level->region[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))], 0) < 0) return;   // FUN_00582250
        traps.push_back({ level, trap, x, y, alvl, now_ms + 35 * kTickMs });
    }

auto World::spring_traps(std::uint32_t now_ms) -> void {
        for (auto armed = traps.begin(); armed != traps.end();) {
            if (now_ms < armed->due) { ++armed; continue; }
            const Trap due = *armed;
            armed = traps.erase(armed);
            if (due.level == level) spring_trap(d2d::rules::trap_on_level(due.trap, level->id), due.x, due.y, due.alvl, now_ms);
        }
    }

auto World::spring_trap(int trap, float x, float y, int alvl, std::uint32_t now_ms, int undead) -> void {
        cues.cue("object_trap_trigger", now_ms, x, y);
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        if (trap == 5 || trap == 7) {                  // FUN_00582380: large at the chest, small a subtile east
            fires.push_back({ level, &game_data->trap_fires[0], x, y });
            fires.push_back({ level, &game_data->trap_fires[1], x + 0.2f, y });
            d2d::log::info("trap {}: fire", trap);
            return;
        }
        if (trap == 8) {                               // FUN_005822f0: 1 or 2 of the level's undead family
            const int fam = d2d::rules::trap_undead(level->region[std::size_t(diff)], 0);
            if (fam < 0 || fight.mon_level != level) { d2d::log::info("trap 8: no undead here"); return; }
            // FUN_0063ec70: the level's own variant of the family.
            // ponytail: from the region, else the family's first (FUN_006510c0's step not traced).
            int type = fam;
            const auto& types = game_data->monsters.types;
            for (const int monstats_row : level->region[std::size_t(diff)]) if (types[std::size_t(monstats_row)].base == types[std::size_t(fam)].base) { type = monstats_row; break; }
            const int count = undead ? undead : int(rng.next() & 1) + 1;
            for (int k = 0; k < count; ++k) {
                const auto [free_x, free_y] = level->nearest_free(x + float(k) * 0.4f, y + 0.4f);
                auto monster = make_monster(*game_data, type, free_x, free_y, rng, diff, true, int(players.size()));
                // Its unit seed: a step of the game seed (FUN_00552df0), what its drops roll off.
                // ponytail: its look doesn't draw on it first, as spawn_monsters' does
                // (monster spawning: agent D's).
                monster.seed = d2d::rules::Rng{ fight.spawning.game.next() };
                monster.aware = true;
                fight.add_monster(std::move(monster));
            }
            d2d::log::info("trap 8: {} x{}", types[std::size_t(type)].id, count);
            return;
        }
        const auto* name = std::size_t(trap) < d2d::rules::kTrapMissile.size() ? d2d::rules::kTrapMissile[std::size_t(trap)] : "";
        const auto found = game_data->missiles.find(name);
        if (!*name || found == game_data->missiles.end()) { d2d::log::info("trap {}: not built", trap); return; }
        const auto& missile_info = found->second;
        const int lvl = d2d::rules::kTrapLevel[std::size_t(diff)];
        d2d::rules::MissileDamage damage;
        if (const auto skill_found = game_data->skills.by_name.find(missile_info.skill); !missile_info.skill.empty() && skill_found != game_data->skills.by_name.end()) {
            d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, lvl, &rng };
            damage = d2d::rules::missile_damage(game_data->skills, *game_data->skills.get(skill_found->second), env, lvl);
        } else {
            damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        }
        d2d::rules::MonStats stats;
        stats.level = alvl;
        stats.to_hit = missile_info.to_hit ? alvl * 10 : 1 << 20;       // ToHit 0: always hits
        stats.a2_min = missile_info.min; stats.a2_max = std::max(missile_info.max, missile_info.min);
        // In 256ths a frame; poison's over its length.
        auto pts = [&](int points) { const std::int64_t value = points; return int(damage.etype == 3 ? value * std::max(damage.elen, 1) >> 8 : value >> 8); };
        if (damage.etype >= 0) stats.elements[0] = { damage.etype, 100, pts(damage.elo), pts(damage.ehi), damage.elen, "A2" };
        auto shoot = [&](float dx, float dy, float vel, const std::shared_ptr<std::vector<int>>& struck) {
            const float speed = missile_speed(int(vel * 256) * 75 / 100), distance = std::max(std::hypot(dx, dy), 0.01f);   // FUN_0059fa30's x 75 / 100
            Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, missile_heading(dx, dy), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats };
            missile.struck = struck;
            fight.missiles.push_back(std::move(missile));
        };
        const auto ring = std::make_shared<std::vector<int>>();
        if (trap == 3) {                               // PrimePoisonNova: 8 at Param1 << 6, 8 between at Param2 << 6
            for (std::size_t k = 0; k < d2d::rules::kPoisonNova.size(); ++k) {
                const auto [offset_x, offset_y] = d2d::rules::kPoisonNova[k];
                shoot(float(offset_x), float(offset_y), float(k % 2 ? missile_info.param2 : missile_info.param1) / 4, ring);   // << 6 of a Vel's << 8
            }
        } else if (trap == 4) {                        // Trap Nova (do 22): the nova's 64
            for (int k = 0; k < 64; ++k) {
                const float angle = float(k) * 6.2831853f / 64;
                shoot(std::cos(angle), std::sin(angle), float(missile_info.vel), ring);
            }
        } else {
            shoot(actor->player.x - x, actor->player.y - y, float(missile_info.vel), std::make_shared<std::vector<int>>());
            fight.missiles.back().to = { actor->player.x, actor->player.y };
        }
        d2d::log::info("trap {}: {} at level {}", trap, name, lvl);
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
        if (from && from != level) { act1_enter(*from); act2_enter(*from); act3_enter(*from); act4_enter(*from); act5_enter(*from); }
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
        const auto spots = d2d::rules::palace_spots(quests(), game_bits);
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

auto World::cross_level() -> void {
        const float world_x = float(level->world_x) + actor->target_x, world_y = float(level->world_y) + actor->target_y;
        bool known = level->inside(actor->target_x, actor->target_y);
        for (const auto& neighbour : level->nearby) known = known || neighbour.level->inside(actor->target_x - float(neighbour.dx), actor->target_y - float(neighbour.dy));
        if (!known)
            for (const auto& placement : game_data->layout_of(level->id))
                if (world_x >= float(placement.x) && world_y >= float(placement.y) && world_x < float(placement.x + placement.width) && world_y < float(placement.y + placement.height)
                    && std::ranges::find(not_there, placement.level) == not_there.end()) {
                    not_there.push_back(placement.level);
                    d2d::log::info("not implemented: level {} (the player headed there from level {})", placement.level, level->id);
                }
        if (level->inside(actor->player.x, actor->player.y)) return;
        for (const auto& neighbour : level->nearby) {
            if (!neighbour.level->inside(actor->player.x - float(neighbour.dx), actor->player.y - float(neighbour.dy))) continue;
            const float dx = float(neighbour.dx), dy = float(neighbour.dy);
            auto shift = [&](UnitState& unit) {
                unit.x -= dx; unit.y -= dy;
                unit.goal_x -= dx; unit.goal_y -= dy;
                for (auto& [step_x, step_y] : unit.path) { step_x -= dx; step_y -= dy; }
            };
            each_player([&](Player& player) {            // the party crosses together (one screen, one level)
                shift(player.player);
                player.target_x -= dx; player.target_y -= dy;
                if (player.merc) shift(player.merc->unit);
                fight.pets_cross(level, neighbour.level, dx, dy);
                player.events.push_back(ev::LevelChanged{ level, true });
                player.interact_npc = -1;
            });
            const Level* from = level;
            level = neighbour.level;
            fight.enter(level);
            loot.enter(level);
            swap_npcs(from);
            d2d::log::info("level: {} at ({:.1f}, {:.1f})", level_name(*level), actor->player.x, actor->player.y);
            return;
        }
    }

auto World::quest_talk(int hc_idx, bool fresh) -> std::vector<d2d::rules::QuestMsg> {
        if (hosted_quests) return {};
        std::vector<d2d::rules::QuestMsg> out;
        act1_talk(out, hc_idx, fresh);
        act2_talk(out, hc_idx, fresh);
        act3_talk(out, hc_idx);
        act4_talk(out, hc_idx);
        act5_talk(out, hc_idx);
        return out;
    }

auto World::holding_malus() const -> bool {
        return carries("hdm");
    }

// The malus stand (OperateFn 21, FUN_00591ac0): at clvl 8 the malus drops
// from it (FUN_00559a30) and it's used (mode 2); too low, "I can't"
// (FUN_00553380).
// ponytail: the refusal's a log line, not the class's voice; the stand
// starts NU where game.exe's InitFn 15 has it ON with the quest off.
auto World::malus_stand(int npc_index, std::uint32_t now_ms) -> void {
        const auto& object = level->npcs[std::size_t(npc_index)];
        const auto stand = tools.operate(quests(), int(actor->character.stats.get(d2d::d2s::kLevel)));
        if (stand == d2d::rules::ToolsQuest::Stand::refuse) d2d::log::info("I can't use this yet.");
        if (stand == d2d::rules::ToolsQuest::Stand::drop || !tools.active) operated.try_emplace({ level, npc_index }, now_ms);
        if (stand != d2d::rules::ToolsQuest::Stand::drop) return;
        const auto& area_levels = game_data->area_level;
        const int alvl = std::size_t(level->id) < area_levels.size() ? area_levels[std::size_t(level->id)][std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))] : 1;
        loot.put({ .code = "hdm" }, object.x, object.y, alvl, fight.spawning.game, now_ms);
        d2d::log::info("Tools of the Trade: the Horadric Malus");
    }

// Andariel's death hook (FUN_005965a0): the quest's bits; the player's
// kill for it opens Act 2 (the save's progression, FUN_00538680) and
// drops two chipped gems and a standard one (0x7361dc / 0x736444).
// ponytail: loot's rng, not the game's quest rng (game+0x10f4); quests:
// agent A's.
auto World::andariel_died(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (!andy.killed(quests(), level->id == d2d::rules::level_ids::kCatacombsLevel4)) return;
        class_cue("act1_complete_andariel", now_ms);   // unit event 0x21: participant's class voice
        music_cue("music_quest_andariel");
        open_act(1);
        for (const auto* codes : { d2d::rules::AndyQuest::kChipped, d2d::rules::AndyQuest::kChipped, d2d::rules::AndyQuest::kStandard })
            loot.put({ codes[loot.rng.next() % 7] }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
        d2d::log::info("Sisters to the Slaughter: Andariel is dead, return to Warriv");
    }

// Mephisto's death hook (a3q6, FUN_005bc8b0: record slot +0xc0). The
// killer, and each player in the Durance's third level (FUN_005bc190), not
// done gets word 0x16 bits 0xd, 0 and 0xb and progression diff x 4 (5 in
// LoD) + 3 (FUN_005bc140 -> FUN_00538680), and a Mephisto's Soulstone
// ("mss ", FUN_00559a30) unless it had bit 0xb. The record's state goes
// to 2; the hellgate (InitFn 44 put its id at +4) to mode 1, its event 1
// FrameCnt1 + 1 frames on taking it to mode 2 (FUN_00581490).
// ponytail: one player, the World's; the soulstone falls at his spot (the
// dying unit's), not by FUN_00559a30's own search.
auto World::mephisto_died(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        guardian.killed();
        class_cue("act3_complete_guardian", now_ms);   // FUN_005bc8b0: unit event 0x42
        music_cue("music_quest_mephisto");
        bridge_check = now_ms + 24 * kTickMs;
        auto& bits = quests();
        if (!d2d::rules::quest::bit(bits, 0x16, 0)) {
            const bool had_stone = d2d::rules::quest::bit(bits, 0x16, 0xb);
            for (const int bit : { 0xd, 0, 0xb }) d2d::rules::quest::set(bits, 0x16, bit);
            open_act(3);
            if (!had_stone) loot.put({ .code = "mss" }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
        }
        if (const Level* durance = game_data->level(d2d::rules::level_ids::kDuranceOfHateLevel3)) {
            const auto gate = std::ranges::find(durance->npcs, d2d::rules::object_ids::kHellgate, &Npc::object_id);
            if (gate != durance->npcs.end()) doors[{ durance, int(gate - durance->npcs.begin()) }] = Door{ 1, now_ms };
        }
        d2d::log::info("The Guardian: Mephisto is dead, the hellgate opens");
    }

// The hellgate's event 1 (mode 1 -> 2 at FrameCnt1 + 1 frames) and the
// bridge's event 7 (FUN_005bcac0, every 24 frames): with Mephisto dead and
// a player within 18 of it (FUN_00641530), mode 0 -> 1 (it rises), and at
// the next 1 -> 2, its collision lifted (FUN_00623830: no HasCollision2).
// ponytail: the bridge's checks run while the player's on its level.
auto World::mephisto_bridge(std::uint32_t now_ms) -> void {
        if (!guardian.dead || level->id != d2d::rules::level_ids::kDuranceOfHateLevel3) return;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& gate = level->npcs[i];
            if (gate.object_id == d2d::rules::object_ids::kHellgate && !doors.contains({ level, int(i) })) doors[{ level, int(i) }] = Door{ 2, now_ms };   // InitFn 44: the record's 2 at a join
            const auto state = doors.find({ level, int(i) });
            if (gate.object_id == d2d::rules::object_ids::kHellgate && state != doors.end() && state->second.mode == 1
                && now_ms - state->second.when >= std::uint32_t(gate.op_frames + 1) * kTickMs)
                state->second = Door{ 2, now_ms };
        }
        if (now_ms < bridge_check) return;
        bridge_check = now_ms + 24 * kTickMs;
        for (std::size_t i = 0; i < level->npcs.size(); ++i) {
            const auto& bridge = level->npcs[i];
            if (bridge.object_id != d2d::rules::object_ids::kMephistoBridge) continue;
            auto& state = doors.try_emplace({ level, int(i) }, Door{ 0, 0 }).first->second;
            const int off_x = int(std::floor(actor->player.x * 5)) - int(std::floor(bridge.x * 5)), off_y = int(std::floor(actor->player.y * 5)) - int(std::floor(bridge.y * 5));
            if (state.mode == 2 || d2d::rules::unit_distance(off_x, off_y, 2, bridge.size_x) >= 18) continue;
            state = Door{ state.mode + 1, now_ms };
            if (state.mode == 2) set_footprint(*level, bridge, false);
            d2d::log::info("The Guardian: the bridge {}", state.mode == 1 ? "rises" : "is up");
        }
    }

// Blood Raven's death hook (FUN_00590ec0).
// ponytail: "near" is the player in the Burial Grounds, not the killer's
// room or its neighbours (FUN_00590c40); quests: agent A's.
auto World::blood_raven_died(std::uint32_t now_ms) -> void {
        if (!burial.killed(quests(), level->id == d2d::rules::BurialQuest::kBurial)) return;
        music_cue("music_quest_bloodraven");
        class_cue("act1_complete_burial", now_ms);
        d2d::log::info("Sisters' Burial Grounds: Blood Raven is dead, return to Kashya");
    }

// The Cube's Transmute (FUN_005665f0 / FUN_00565ab0, rules/cube.hpp): the
// first recipe the Cube's items answer; what it makes; every item in the
// Cube gone (FUN_00564f30); the new ones into the Cube (FUN_00560200),
// else at the feet. The Horadric Staff made: its quest's bit 11
// (FUN_0059e5c0).
// ponytail: FUN_00560200's own spot search (first fit by column here);
// the transmute's sound.
auto World::transmute(std::uint32_t now_ms) -> void {
        using d2d::d2s::Item;
        std::vector<const Item*> cube;
        for (const auto& item : actor->character.items)
            if (item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kCube) cube.push_back(&item);
        d2d::rules::CubeMatch match;
        const int diff = actor->character.header.active_difficulty();
        const int row = d2d::rules::cube_recipe(game_data->rules, game_data->cube_recipes, cube, diff, actor->character.expansion, actor->character.header.cls, match);
        if (row < 0) { d2d::log::info("transmute: nothing ({} items)", cube.size()); return; }
        cues.cue(d2d::rules::sound_ids::kCursorConvertReady, now_ms, actor->player.x, actor->player.y);
        const auto& recipe = game_data->cube_recipes[std::size_t(row)];
        // A portal (output kind 1..3, FUN_00565ab0's table 0x6e11c8): made, the
        // inputs go; refused, they stay (rules::CainQuest::cow_portal_opens).
        if (const int kind = recipe.outputs[0].kind; kind >= d2d::rules::cube_out::kCowPortal && kind <= d2d::rules::cube_out::kFinale) {
            const Level* farm = game_data->level(d2d::rules::level_ids::kMooMooFarm);
            if (kind != d2d::rules::cube_out::kCowPortal || !farm || farm->ds1.width() == 0
                || !d2d::rules::CainQuest::cow_portal_opens(quests(), cow_portal_opened, actor->character.expansion, level->id)) {
                d2d::log::info("transmute: {} refused", recipe.description);
                return;
            }
            cow_portal_opened = true;
            // ponytail: FUN_00545340's spot search (3, mask 0x400, 4, 100) is the nearest free spot; its twin the farm's portal spot.
            const auto [spot_x, spot_y] = level->nearest_free(actor->player.x, actor->player.y + 0.8f);
            portal[kCowPortal] = { level, spot_x, spot_y, now_ms };
            const auto [to_x, to_y] = farm->portal_spot.first >= 0 ? farm->nearest_free(farm->portal_spot.first, farm->portal_spot.second)
                                                                  : farm->nearest_free(float(farm->ds1.width()) / 2, float(farm->ds1.height()) / 2);
            portal[kCowPortal + 1] = { farm, to_x, to_y, now_ms };
            std::erase_if(actor->character.items, [](const Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kCube; });
            cues.cue("object_townportal", now_ms, spot_x, spot_y);
            d2d::log::info("transmute: the Cow Portal opens");
            return;
        }
        auto made = d2d::rules::cube_make(game_data->rules, recipe, match, cube, int(actor->character.stats.get(d2d::d2s::kLevel)), actor->character.expansion, diff,
                                          fight.spawning.game, &loot.found_uniques);
        if (!made.made) { d2d::log::info("transmute: {} made nothing", recipe.description); return; }
        cues.cue(d2d::rules::sound_ids::kCursorConvertItem, now_ms, actor->player.x, actor->player.y);
        std::erase_if(actor->character.items, [](const Item& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kCube; });
        const auto& layout = game_data->cube_layout;
        for (auto& item : made.items) {
            if (item.code == "hst") {             // FUN_0059e5c0 -> FUN_0059b660
                music_cue("music_quest_horadric");
                d2d::rules::quest::set(quests(), d2d::rules::HoradricStaffQuest::kQuest, 11);
                staff.staff_made = true;
                arcane.staff_made(quests());
            }
            std::vector<const Item*> placed;
            for (const auto& other : actor->character.items)
                if (other.location == d2d::d2s::item_location::kStored && other.panel == d2d::d2s::item_panel::kCube) placed.push_back(&other);
            const auto [width, height] = d2d::rules::item_size(game_data->rules, item.code);
            std::optional<Item> held_item = std::move(item);
            held_item->id = -1;
            if (const auto [column, cell_row] = d2d::rules::free_spot(game_data->rules, placed, layout.cols, layout.rows, width, height);
                column < 0 || !d2d::rules::put_in_grid(game_data->rules, actor->character.items, held_item, d2d::d2s::item_panel::kCube, layout.cols, layout.rows, column, cell_row))
                loot.place(std::move(*held_item), actor->player.x, actor->player.y, now_ms);
        }
        d2d::log::info("transmute: {} ({} made)", recipe.description, made.items.size());
    }

// The way down to Duriel (FUN_0059d870): object 100 13 subtiles left of the
// orifice and 3 down (FUN_00555230), in the true tomb.
auto World::durielportal_open(std::uint32_t now_ms) -> void {
        const Level* tomb = game_data->level(tombs.true_tomb);
        if (!tomb) return;
        const auto orifice = std::ranges::find(tomb->npcs, d2d::rules::operate_fn::kOrifice, &Npc::operate_fn);
        if (orifice == tomb->npcs.end()) return;
        portal[6] = { tomb, orifice->x - 13.f / 5, orifice->y + 3.f / 5, now_ms };
        d2d::log::info("The Seven Tombs: the way down to Duriel opens");
    }

// The way to the Canyon of the Magi (FUN_0056d130: object 60 to level 46):
// by the player (FUN_00545340's search: the nearest free spot), its twin at
// the Canyon's portal spot.
auto World::canyon_portal(std::uint32_t now_ms) -> void {
        const Level* canyon = game_data->level(d2d::rules::ArcaneQuest::kCanyon);
        const auto [spot_x, spot_y] = level->nearest_free(actor->player.x, actor->player.y + 0.6f);
        portal[4] = { level, spot_x, spot_y, now_ms };
        if (canyon && canyon->ds1.width() > 0) {
            const auto [to_x, to_y] = canyon->portal_spot.first >= 0 ? canyon->nearest_free(canyon->portal_spot.first, canyon->portal_spot.second)
                                                                    : canyon->nearest_free(float(canyon->ds1.width()) / 2, float(canyon->ds1.height()) / 2);
            portal[5] = { canyon, to_x, to_y, now_ms };
        }
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("The Arcane Sanctuary: the way to the Canyon of the Magi");
    }

// Radament's death hook (FUN_00599020): the books due drop at him
// (FUN_00559a30).
// ponytail: "near" is the player in his lair, not the killer's room or its
// neighbours (FUN_00598e20).
auto World::radament_died(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        const auto [books, took_part] = radament.killed(quests(), game_bits, level->id == d2d::rules::RadamentQuest::kLair, carries("ass"));
        for (int i = 0; i < books; ++i) loot.put({ .code = "ass" }, kill.x, kill.y, kill.level, fight.spawning.game, now_ms);
        if (took_part) class_cue("act2_complete_radament", now_ms);
        if (took_part) music_cue("music_quest_radament");
        if (radament.state == 4) d2d::log::info("Radament's Lair: Radament is dead, return to Atma");
    }

auto World::hire_list(int list_act) -> std::vector<d2d::rules::HireEntry>& {
        auto& entries = hire_lists[std::size_t(list_act)];
        if (std::ranges::none_of(entries, d2d::rules::for_hire))      // FUN_00577010 -> FUN_00576070
            entries = d2d::rules::hire_entries(d2d::rules::hire_names(game_data->rules, actor->character.expansion, list_act, actor->character.header.active_difficulty()),
                                               fight.spawning.proxy);
        return entries;
    }

auto World::hire_offers(int list_act) const -> std::vector<d2d::rules::MercOffer> {
        std::vector<d2d::rules::MercOffer> offers;
        const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
        for (const auto& entry : hire_lists[std::size_t(list_act)])
            if (d2d::rules::for_hire(entry))
                if (auto offer = d2d::rules::merc_offer(game_data->rules, actor->character.expansion, list_act, actor->character.header.active_difficulty(), clvl, entry)) offers.push_back(*offer);
        return offers;
    }

// Kashya's reward (FUN_00579180): the first offer of her hire list as the
// merc, free — none if there's a merc already (LoD: even a dead one).
// The offer leaves the list (packet 0x50 subtype 2).
// ponytail: an unopened list is rolled here (game.exe: the client asks
// for it as her menu opens: waits for networking).
auto World::kashya_merc() -> void {
        const auto& header = actor->character.header;
        if (header.merc_seed && (actor->character.expansion || !header.merc_dead)) return;
        auto& entries = hire_list(0);
        const auto offers = hire_offers(0);
        const auto first = std::ranges::find_if(entries, d2d::rules::for_hire);
        if (offers.empty() || first == entries.end()) return;
        auto offer = offers.front();
        offer.cost = 0;
        d2d::rules::hire(offer, actor->character.header, actor->character.stats);
        first->hired = true;
        spawn_merc();
        d2d::log::info("Sisters' Burial Grounds: Kashya's reward, a mercenary");
    }

// The Countess's death hook (FUN_00595710): the quest's bits and, for the
// player's kill, their complete_tower line (unit event 0x25). Her drop is
// her SuperUniques TC's (negative picks: an item and a rune roll).
auto World::countess_died(std::uint32_t now_ms) -> void {
        const bool quest_kill = tower.killed(quests(), level->id == d2d::rules::TowerQuest::kCellar);
        // Her treasure (FUN_005954f0, dead and not yet made: d+0x118 / 0x119):
        // a towerchestspawner (missile 332) at each chest of cellar 5 (object
        // 371, InitFn 47 lists up to 8).
        // The invisible owner (monster 326 at her death spot, FUN_005b2f20)
        // only owns the spawners, which d2d plays without one.
        // ponytail: the chests are the level's, not the ones whose rooms came
        // up (room units: agent D's).
        if (tower.dead && !tower.treasure && level->id == d2d::rules::TowerQuest::kCellar) {
            tower.treasure = true;
            for (std::size_t i = 0; i < level->npcs.size() && treasure.size() < 8; ++i)
                if (level->npcs[i].root == "objects" && level->npcs[i].object_id == d2d::rules::object_ids::kLargeChestR) treasure.push_back({ level, int(i), d2d::rules::TowerQuest::kTreasureFrames });
        }
        if (!quest_kill) return;
        class_cue("act1_complete_tower", now_ms);
        d2d::log::info("The Forgotten Tower: the Countess is dead");
    }

// The towerchestspawner's frame (FUN_005af300; Missiles.txt row 332:
// Range 400, Param1 150, Param2 2, Param3 5). At Range - Param1 left the
// chest opens (FUN_00585e00, mode 0 only): the act's chest class three
// times with quality forced magic (FUN_00585b90, EDX 4), then two each of
// hp / mp by act (+2 past normal: 0x731f4c / 0x731f60, FUN_00558450 /
// FUN_005584c0), then mode 2. From then on every Param2 * 4 frames a gold
// pile at the area level, rand(2r + 1) - r subtiles off (r = Param3).
// The rounds roll off the chest's unit seed (chest_round).
// ponytail: the world's rng stands in for the missile's seed; it runs only while the player's on its level (game.exe: while
// its room's up); the end event (unit event 0x5c at 1 left) isn't sent;
// the missile's lifetime taken as Range (LevRange 1 at level 0).
auto World::tower_treasure(std::uint32_t now_ms) -> void {
        using Tower = d2d::rules::TowerQuest;
        for (auto& spawner : treasure) {
            if (spawner.level != level || spawner.left <= 0) continue;
            const auto& chest = level->npcs[std::size_t(spawner.npc)];
            const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
            const auto& area_levels = game_data->area_level;
            auto alvl = [&](int id) { return std::size_t(id) < area_levels.size() ? area_levels[std::size_t(id)][std::size_t(diff)] : 1; };
            const int here = alvl(level->id);
            if (spawner.left == Tower::kTreasureFrames - Tower::kTreasureOpen && !operated.contains({ level, spawner.npc })) {
                const auto [low, high] = d2d::rules::kChestLevels[0];
                const auto treasure_class = d2d::rules::chest_tc(0, diff, here, alvl(low), alvl(high));
                std::vector<d2d::rules::Drop> drops;
                for (int round = 0; round < 3; ++round) d2d::rules::chest_round(game_data->rules, treasure_class, chest.seed, drops, 4);
                for (const auto* code : { "hp", "hp", "mp", "mp" }) drops.push_back({ std::string(code) + (diff ? "3" : "1") });
                for (const auto& dropped : drops) loot.put(dropped, chest.x, chest.y, here, fight.spawning.game, now_ms);
                operated[{ level, spawner.npc }] = now_ms;
                d2d::log::info("The Forgotten Tower: the Countess's chest opens ({} drops)", drops.size());
            }
            if (spawner.left < Tower::kTreasureFrames - Tower::kTreasureOpen && spawner.left % (Tower::kTreasureEvery * 4) == 0) {
                const int radius = Tower::kTreasureRadius;
                const float off_x = float(rng(2 * radius + 1) - radius) / 5, off_y = float(rng(2 * radius + 1) - radius) / 5;
                const auto [spot_x, spot_y] = level->nearest_free(chest.x + off_x, chest.y + off_y);
                loot.put({ .code = "gld" }, spot_x, spot_y, here, fight.spawning.game, now_ms);
            }
            --spawner.left;
        }
    }

// Tristram Cain's AI (FUN_005e7880), a think each time he stops: the
// first notes where he stands (FUN_005944b0) and heads 3 subtiles on
// (x + 3, y + 3); up to 6 goes while more than 1 off; then the town
// portal (FUN_005943b0: object 189, ECX 2 / EDX 0xbd to FUN_00555230)
// at the noted spot, in the room near his own that holds it
// (FUN_00463740); none does: the spot moves on 3 subtiles on each axis
// and no portal opens. Then back to the portal (FUN_00594450: his own
// spot when there's none) for up to 6 more thinks, stepping in
// (FUN_005944f0: camp Cain due).
// ponytail: the thinks' idles (1, 20 frames) stand in for the AI's own
// tick; the portal stays for the game (FUN_005944f0 doesn't remove it;
// InitFn 61 isn't traced); the Search for Cain: agent A's.
auto World::cain_step(std::uint32_t now_ms, float elapsed) -> void {
        if (cain_walk.npc < 0 || level->id != d2d::rules::CainQuest::kTristram || std::size_t(cain_walk.npc) >= npc_states.size()) return;
        const auto& npc = level->npcs[std::size_t(cain_walk.npc)];
        auto& unit = npc_states[std::size_t(cain_walk.npc)];
        if (unit.walking) {
            unit.walking = walk_on(*level, unit, cells_per_sec(npc.velocity) * elapsed);
            if (unit.walking) return;
            unit.path.clear();
        }
        if (now_ms < cain_walk.next) return;
        const auto cell = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        const int at_x = int(std::floor(unit.x * 5)), at_y = int(std::floor(unit.y * 5));
        const auto walk = [&](int x, int y) {
            unit.walking = set_off(*level, unit, cell(x), cell(y), false);
        };
        const auto off = [&](int x, int y) { return d2d::rules::ai_distance(x - at_x, y - at_y); };
        if (cain_walk.stage < 0) {
            unit.hidden = false;
            cain_walk.spot_x = at_x; cain_walk.spot_y = at_y;
            cain_walk.x = at_x + 3; cain_walk.y = at_y + 3;
            cain_walk.stage = 1;
            cain_walk.next = now_ms + 40;
            return;
        }
        if (cain_walk.stage < 2) {
            if (off(cain_walk.x, cain_walk.y) > 1 && cain_walk.tries < 6) { ++cain_walk.tries; walk(cain_walk.x, cain_walk.y); return; }
            if (room_near_holds(*level, at_x, at_y, cain_walk.spot_x, cain_walk.spot_y)) {
                cain_portal = { level, cell(cain_walk.spot_x), cell(cain_walk.spot_y), now_ms };
                cues.cue("object_townportal", now_ms, cain_portal.x, cain_portal.y);
                d2d::log::info("Search for Cain: Cain opens a portal at subtile ({}, {})", cain_walk.spot_x, cain_walk.spot_y);
            } else {
                cain_walk.spot_x += 3; cain_walk.spot_y += 3;
            }
            cain_walk.stage = 2;
            cain_walk.next = now_ms + 20 * 40;
            return;
        }
        const bool opened = cain_portal.level == level;
        if (++cain_walk.stage < 8 && opened && off(cain_walk.spot_x, cain_walk.spot_y) != 0) { walk(cain_walk.spot_x, cain_walk.spot_y); return; }
        cain.portal_entered();
        unit.hidden = true;
        cain_walk.npc = -1;
        d2d::log::info("Search for Cain: Cain has gone to the camp");
    }

// FUN_00558110: the cursor's item, then all the player's items.
// N-player gap: the quest item's difficulty (stat 0x164, questdiffcheck) isn't checked.
auto World::carries(std::string_view code) const -> bool { return (actor->held && actor->held->code == code) || std::ranges::contains(actor->character.items, code, &d2d::d2s::Item::code); }
// FUN_005466b0(code, ilvl, quality): an item made into the player's
// inventory, left at their feet when there's no room.
auto World::give_item(std::string_view code, std::uint32_t now_ms, int ilvl, int quality) -> void {
        const std::size_t had = loot.ground.size();
        loot.put({ .code = std::string(code), .quality = quality }, actor->player.x, actor->player.y, ilvl, fight.spawning.game, now_ms);
        if (loot.ground.size() > had) loot.take(loot.ground.size() - 1);   // only what was made
    }
auto World::khalim_parts() const -> d2d::rules::KhalimQuest::Parts {
        using K = d2d::rules::KhalimQuest;
        return { carries(K::kEyeCode), carries(K::kBrainCode), carries(K::kHeartCode), carries(K::kFlailCode), carries(K::kWillCode) };
    }
// FUN_00544160: a quest item taken from the player, the cursor's first.
// FUN_00538680: act `act` (0-based) open, the save's progression at
// least difficulty x 4 (5 in LoD) + act.
auto World::open_act(int act) -> void {
        const int diff = actor->character.header.active_difficulty();
        actor->character.header.progression = std::uint8_t(std::max<int>(actor->character.header.progression, diff * (actor->character.header.expansion() ? 5 : 4) + act));
    }

// The player's class says `line` ("act1_complete_den": the sound
// <class>_act1_complete_den).
auto World::game_quest_done(std::size_t quest) const -> bool {
        const bool done[7] = { false, den.state >= 4, burial.state >= 4, tools.game_returned, cain.rescued_flag, tower.dead, andy.log == 0xd };
        return quest < 7 ? done[quest] : d2d::rules::quest::bit(game_bits, int(quest), 13);
    }

// The sound tick (FUN_004dcaa0): 62 ticks after the player's level changed,
// FUN_004cc270 looks it up once; a row's first level this game marks all of
// its levels, then speaks if its quest's to do (FUN_004a4180: open in this
// game, the game's flag 13 and the player's 0, 1 and 14 clear; the Den's
// log short of 5) and the class hasn't spoken for 62 ticks.
// ponytail: "hasn't spoken" is the class's lines (voice_at); game.exe's is
// any sound on the player (+0x7c) and no class line playing (FUN_004b9c20).
auto World::find_voice(std::uint32_t tick_ms) -> void {
        constexpr std::uint32_t kWait = 62 * kTickMs;
        if (level->id != actor->voice_level) { actor->voice_level = level->id; actor->voice_level_at = tick_ms; }
        if (actor->voice_level == actor->voice_checked || tick_ms - actor->voice_level_at < kWait) return;
        actor->voice_checked = actor->voice_level;
        const auto* row = d2d::rules::find_line(actor->voice_level);
        if (!row || std::size_t(actor->voice_level) >= actor->voice_found.size() || actor->voice_found[std::size_t(actor->voice_level)]) return;
        for (const int marked : row->levels)
            if (marked) actor->voice_found.set(std::size_t(marked));
        const auto& bits = actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))];
        const auto quest = std::size_t(row->quest);
        namespace q = d2d::rules::quest;
        if (closed_at_join[quest] || game_quest_done(quest) || q::bit(bits, row->quest, 0) || q::bit(bits, row->quest, 1) || q::bit(bits, row->quest, 14)) return;
        if (row->quest == d2d::rules::DenQuest::kQuest && den.log_state(bits) >= 5) return;
        if (actor->voice_at && tick_ms - actor->voice_at < kWait) return;
        class_cue(row->line, tick_ms);
    }

auto World::class_cue(std::string_view line, std::uint32_t at_ms) -> void {
        static constexpr std::array<const char*, 7> kClass{ "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
        actor->voice_at = std::max(actor->voice_at, at_ms);
        if (actor->character.header.cls < kClass.size()) cues.cue(std::format("{}_{}", kClass[actor->character.header.cls], line), at_ms, actor->player.x, actor->player.y);
    }

auto World::music_cue(std::string_view sound) -> void {
        if (const auto found = game_data->sound_index.find(std::string(sound)); found != game_data->sound_index.end())
            actor->events.push_back(ev::MusicCue{ found->second });
    }

auto World::take_carried(std::string_view code) -> void {
        if (actor->held && actor->held->code == code) { actor->held.reset(); return; }
        if (const auto found = std::ranges::find(actor->character.items, code, &d2d::d2s::Item::code); found != actor->character.items.end()) actor->character.items.erase(found);
    }

// The tree (OperateFn 12) drops the scroll by it; the stones (9) light in
// the deciphered scroll's order, the fifth opening the way to Tristram at
// StoneLambda (x + 6, y - 3); the Gibbet (10) frees Cain.
// ponytail: the world's rng stands in for the game's quest rng
// (game+0x10f4); CairnStones' missile and the portal's red look aren't
// drawn. Cain walks out (cain_step); with no room for him the portal
// FUN_0056d130 opens at x + 6, y + 6 is the player's own pair. Quests:
// agent A's.
auto World::cain_operate(int npc_index, std::uint32_t now_ms) -> void {
        using Cain = d2d::rules::CainQuest;
        const auto& object = level->npcs[std::size_t(npc_index)];
        const bool fresh = !operated.contains({ level, npc_index });
        auto& quest_bits = quests();
        if (object.operate_fn == d2d::rules::operate_fn::kInifussTree) {
            if (!cain.tree(quest_bits, carries("bks") || carries("bkd"))) return;
            class_cue("act1_find_tree", now_ms);   // FUN_00593af0: event 0x2d as the first scroll drops
            operated[{ level, npc_index }] = now_ms;
            loot.put({ .code = "bks" }, object.x, object.y, 1, fight.spawning.game, now_ms);
            d2d::log::info("Search for Cain: the Scroll of Inifuss");
            return;
        }
        if (object.operate_fn == d2d::rules::operate_fn::kGibbet) {
            if (!cain.gibbet(quest_bits, fresh)) return;
            class_cue("act1_rescue_cain", now_ms);   // FUN_00593290: unit event 0x30
            operated[{ level, npc_index }] = now_ms;
            const auto found = std::ranges::find(level->npcs, d2d::rules::monster_ids::kCain, &Npc::hc_idx);
            const bool spawned = found != level->npcs.end();
            cain.rescued(spawned);
            if (spawned) cain_walk = { .npc = int(found - level->npcs.begin()), .next = now_ms + std::uint32_t(object.op_frames) * 40 };
            else open_portal_at(object.x + 6, object.y + 6, now_ms);
            d2d::log::info("Search for Cain: Cain is free");
            return;
        }
        if (!cain.ordered) {
            cain.stone_order(rng.low, rng.high);
            d2d::log::info("Search for Cain: the stones' order {} {} {} {} {}", cain.order[0], cain.order[1], cain.order[2], cain.order[3], cain.order[4]);
        }
        const int voice_throttle = cain.throttle;
        const auto lit = cain.stone(quest_bits, object.object_id, carries("bkd"), fresh);
        if (cain.throttle != voice_throttle && (voice_throttle & 0x3f) == 0
            && !d2d::rules::quest::bit(quest_bits, Cain::kQuest, 3) && !d2d::rules::quest::bit(quest_bits, Cain::kQuest, 4))
            class_cue("act1_find_cairn", now_ms);   // FUN_00593710: event 0x27, every 64th missing-scroll hint
        if (lit == Cain::Stone::none) return;
        operated[{ level, npc_index }] = now_ms;
        if (lit == Cain::Stone::lit) return;
        actor->character.items.erase(std::ranges::find(actor->character.items, std::string_view("bkd"), &d2d::d2s::Item::code));
        const auto lambda = std::ranges::find(level->npcs, 21, &Npc::object_id);
        const auto [spot_x, spot_y] = level->nearest_free(lambda == level->npcs.end() ? object.x : lambda->x + 6, lambda == level->npcs.end() ? object.y : lambda->y - 3);
        portal[2] = { level, spot_x, spot_y, now_ms };
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("Search for Cain: the way to Tristram at ({:.1f}, {:.1f})", spot_x, spot_y);
    }

auto World::den_count(std::uint32_t now_ms) -> void {
        if (level->id != d2d::rules::DenQuest::kDen || fight.mon_level != level) return;
        const int left = int(std::ranges::count_if(fight.monsters, &Monster::alive));
        if (den_left >= 0 && left < den_left) {
            const auto kill = den.killed(quests(), left);
            if (kill == d2d::rules::DenQuest::Kill::few) d2d::log::info("Den of Evil: {}", left == 1 ? "one monster left" : std::format("monsters remaining: {}", left));
            if (kill == d2d::rules::DenQuest::Kill::cleared) {
                d2d::log::info("Den of Evil: cleared");
                music_cue("music_quest_den");
                den_log_at = now_ms + 8 * kTickMs;     // the log's "Return to Akara" 8 ticks on (LAB_00590230)
                if (d2d::rules::quest::bit(quests(), 1, 13))
                    class_cue("act1_complete_den", now_ms);
            }
        }
        den_left = left;
    }

auto World::level_name(const Level& level) -> const char* {
        return !level.name.empty() ? level.name.c_str() : level.id == d2d::rules::level_ids::kRogueEncampment ? "Rogue Encampment" : "?";   // Levels.txt LevelName
    }

// A joined game's swing at a host monster (apps/d2d netgame): the
// player turns to (x, y) and plays the skill's swing; no monster here to
// hit, the host resolves it (Fight's hits guard on attack_mon < 0).
auto World::display_swing(int skill, float x, float y, std::uint32_t now_ms) -> void {
        if (actor->pmode >= 0 || fight.dead()) return;
        actor->attack_mon = -1;
        actor->attack_skill = skill;
        actor->interact_npc = actor->pick_item = actor->take_warp = -1;
        actor->player.walking = false;
        actor->player.dir = direction16(x - actor->player.x, y - actor->player.y);
        fight.start_swing(now_ms);
    }

auto World::use_warp() -> void {
        if (actor->take_warp < 0 || std::size_t(actor->take_warp) >= level->warps.size()) return;
        const auto warp = level->warps[std::size_t(actor->take_warp)];
        if (std::hypot(warp.x + 0.5f - actor->player.x, warp.y + 0.5f - actor->player.y) > 2.f) {
            if (!actor->player.walking) actor->take_warp = -1;                                // stopped short
            return;
        }
        actor->take_warp = -1;
        if (host_warps) return;
        // The sewers' stairs down (a3q2's, FUN_0059d9d0 -> FUN_005550b0): shut
        // till the lever's opened them, or once Khalim's Will's done.
        if (level->id == d2d::rules::level_ids::kKurastSewersLevel1 && warp.destination == d2d::rules::level_ids::kKurastSewersLevel1 + 1 && !khalim.stairs_open) {
            d2d::log::info("the stairs are shut");
            return;
        }
        const Level* destination = game_data->level(warp.destination);                               // built by now, or waited for
        if (!destination || destination->ds1.width() == 0) {
            d2d::log::info("not implemented: level {} (a warp from level {})", warp.destination, level->id);
            return;
        }
        // FUN_005550b0: the other side's warp tile unit (FUN_006195a0), the
        // free spot nearest it, then a walk of its LvlWarp ExitWalk from there.
        // The tile's linked (FUN_0066c220) to the other side's k-th slot back
        // here, k its own rank among its row's slots there; none placed: the
        // lowest slot back that is.
        // ponytail: several tiles of one slot take the first built (game.exe:
        // the first room of its list, then the first unit of its room): room
        // unit lists are agent D's.
        auto back = std::ranges::find_if(destination->warps, [&](const Level::Warp& other) { return other.destination == level->id && other.pair == warp.pair; });
        if (back == destination->warps.end())
            for (auto it = destination->warps.begin(); it != destination->warps.end(); ++it)
                if (it->destination == level->id && (back == destination->warps.end() || it->slot < back->slot)) back = it;
        const float arrive_x = back == destination->warps.end() ? float(destination->ds1.width()) / 2 : back->unit_x;
        const float arrive_y = back == destination->warps.end() ? float(destination->ds1.height()) / 2 : back->unit_y;
        arrive(destination, arrive_x, arrive_y, "a warp");
        if (back != destination->warps.end()) {
            actor->target_x = actor->player.x + back->exit_x; actor->target_y = actor->player.y + back->exit_y;
            actor->player.walking = true;
        }
    }

auto World::arrive(const Level* destination, float arrive_x, float arrive_y, const char* how) -> void {
        const auto [free_x, free_y] = destination->nearest_free(arrive_x, arrive_y);
        const float dx = actor->player.x - free_x, dy = actor->player.y - free_y;
        fight.pets_cross(level, destination, dx, dy);
        actor->events.push_back(ev::LevelChanged{ level, true });
        const Level* from = level;
        const int from_act = act();
        level = destination;
        if (act() != from_act) {                      // a new act (the caravan, a waypoint): its vendors stock anew; the save starts there
            stocks.clear();
            set_act(act());
        }
        actor->player.x = free_x; actor->player.y = free_y;
        actor->player.walking = false; actor->player.path.clear();
        actor->target_x = free_x; actor->target_y = free_y;
        if (actor->merc) {
            actor->merc->unit.path.clear();
            std::tie(actor->merc->unit.x, actor->merc->unit.y) = level->nearest_free(free_x + 1, free_y + 1);
        }
        fight.footstep(int(std::floor(free_x * 5)), int(std::floor(free_y * 5)), now);   // an arrival is a footstep (FUN_00554ea0)
        actor->warp_spot = { int(std::floor(free_x * 5)), int(std::floor(free_y * 5)) };
        bring_party(from, level, free_x, free_y, true);
        fight.enter(level);
        fight.rooms_up(*level, actor->player.x, actor->player.y, true);
        act3_bind_new_bosses();
        reopen_doors();
        std::tie(actor->player.x, actor->player.y) = level->nearest_free(actor->player.x, actor->player.y);   // off what the room just made
        loot.enter(level);
        swap_npcs(from);
        actor->interact_npc = actor->pick_item = actor->take_warp = actor->take_portal = -1;
        if (level->id == d2d::rules::DenQuest::kDen) den.enter_den(quests());
        d2d::log::info("level: {} at ({:.1f}, {:.1f}), through {} from {}", level_name(*level), free_x, free_y, how, level_name(*from));
    }

// The save's act (its difficulty byte: 0x80 and the act in the low bits):
// game.exe starts the next game in that act's town.
auto World::set_act(int act) -> void {
        auto& difficulty = actor->character.header.difficulty[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))];
        difficulty = std::uint8_t((difficulty & ~7) | 0x80 | act);
    }

auto World::death_penalty(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& stat_values = actor->character.stats.values;
        const int lvl = int(stat_values[kLevel]);
        const int difficulty = actor->character.header.active_difficulty();
        static constexpr int kDeathExp[3] = { 0, 5, 10 };
        actor->exp_lost = 0;
        if (lvl > 1 && std::size_t(lvl) < game_data->exp_next.size()) {
            const auto low = game_data->exp_next[std::size_t(lvl - 1)], high = game_data->exp_next[std::size_t(lvl)];
            if (const auto loss = kDeathExp[std::clamp(difficulty, 0, 2)] * (high - low) / 100; loss > 0) {
                const auto experience = std::max(stat_values[kExp] - loss, low + 1);
                actor->exp_lost = std::max<std::int64_t>(stat_values[kExp] - experience, 0);
                stat_values[kExp] = experience;
            }
        }
        const std::int64_t purse = stat_values[kGold], total = purse + stat_values[kGoldBank];
        std::int64_t lost = std::min(lvl, 20) * total / 100;
        if (total - lost < std::int64_t(lvl) * 500) lost = std::max<std::int64_t>(0, total - std::int64_t(lvl) * 500);
        lost = std::min(lost, purse);
        if (purse - lost > 0 && !fight.remote_monsters)       // a joined game: the host drops it (0x9c)
            loot.put({ .code = "gld", .gold = int(purse - lost) }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
        stat_values[kGold] = 0;
        actor->gold_lost = lost;
        d2d::log::info("died: {} experience and {} gold lost; {} gold on the ground", actor->exp_lost, lost, purse - lost);
    }

auto World::make_corpse() -> void {
        Corpse corpse{ level, actor->player.x, actor->player.y, actor->player.dir, {}, actor->exp_lost * 75 / 100, fight.gfx() };
        for (auto item_it = actor->character.items.begin(); item_it != actor->character.items.end();)
            if (item_it->location == d2d::d2s::item_location::kEquipped) { corpse.items.push_back(std::move(*item_it)); item_it = actor->character.items.erase(item_it); }
            else ++item_it;
        if (actor->held) { corpse.items.push_back(std::move(*actor->held)); corpse.items.back().location = 1; actor->held.reset(); }   // ponytail: the held item's slot
        if (actor->corpses.size() >= 16) actor->corpses.erase(actor->corpses.begin());
        actor->corpses.push_back(std::move(corpse));
    }

auto World::take_corpse_items(std::size_t corpse_index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& corpse = actor->corpses[corpse_index];
        actor->character.stats.values[kExp] += corpse.exp;
        corpse.exp = 0;
        const auto& layout = game_data->inv_layout[std::size_t(actor->character.header.cls % 7)];
        for (auto item_it = corpse.items.begin(); item_it != corpse.items.end();) {
            const bool worn = std::ranges::any_of(actor->character.items, [&](const Item& worn_item) { return worn_item.location == d2d::d2s::item_location::kEquipped && worn_item.slot == item_it->slot; });
            if (!worn && item_it->slot >= d2d::d2s::body_location::kFirst && item_it->slot <= d2d::d2s::body_location::kLeftArmSwitch) { actor->character.items.push_back(std::move(*item_it)); item_it = corpse.items.erase(item_it); continue; }
            std::optional<Item> held_item = *item_it;
            held_item->location = d2d::d2s::item_location::kStored;
            std::vector<const Item*> inv;
            for (const auto& x : actor->character.items) if (x.location == d2d::d2s::item_location::kStored && x.panel == d2d::d2s::item_panel::kInventory) inv.push_back(&x);
            const auto [width, height] = d2d::rules::item_size(game_data->rules, held_item->code);
            if (const auto [column, row] = d2d::rules::free_spot(game_data->rules, inv, layout.cols, layout.rows, width, height); column >= 0
                && d2d::rules::put_in_grid(game_data->rules, actor->character.items, held_item, d2d::d2s::item_panel::kInventory, layout.cols, layout.rows, column, row)) { item_it = corpse.items.erase(item_it); continue; }
            ++item_it;
        }
        if (corpse.items.empty()) actor->corpses.erase(actor->corpses.begin() + std::ptrdiff_t(corpse_index));
        cues.cue("item_pickup", now_ms, actor->player.x, actor->player.y);
    }

auto World::read_portal(std::vector<d2d::d2s::Item>::iterator scroll, std::uint32_t now_ms) -> void {
        if (in_town(level) || fight.dead() || actor->pmode >= 0) return;
        const bool book = scroll->code == "tbk";
        if (book && scroll->quantity <= 0) return;
        if (!fight.cast_scroll(book ? 220 : 219, now_ms)) return;
        if (book) --scroll->quantity;
        else actor->character.items.erase(scroll);
        cues.cue("player_townportal_cast", now_ms, actor->player.x, actor->player.y);
    }

auto World::open_portal(std::uint32_t now_ms) -> void { open_portal_at(actor->player.x, actor->player.y + 0.6f, now_ms); }

auto World::open_portal_at(float portal_x, float portal_y, std::uint32_t now_ms) -> void {
        const Level* act_town = game_data->town_of(level->id);
        if (in_town(level) || !act_town || act_town->portal_spot.first < 0) return;
        const auto& town = *act_town;
        const auto [spot_x, spot_y] = level->nearest_free(portal_x, portal_y);
        const auto [town_x, town_y] = town.nearest_free(town.portal_spot.first, town.portal_spot.second);
        portal[actor->portal_pair] = { level, spot_x, spot_y, now_ms };
        portal[actor->portal_pair + 1] = { &town, town_x, town_y, now_ms };
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("town portal: {} ({:.1f}, {:.1f}) <-> {} ({:.1f}, {:.1f})", level_name(*level), spot_x, spot_y, level_name(town), town_x, town_y);
    }

auto World::use_portal(std::uint32_t now_ms) -> void {
        if (actor->take_portal < 0 || !portal[std::size_t(actor->take_portal)].level) return;
        const auto& taken = portal[std::size_t(actor->take_portal)];
        if (taken.level != level || std::hypot(taken.x - actor->player.x, taken.y - actor->player.y) > 2.f) {
            if (!actor->player.walking) actor->take_portal = -1;
            return;
        }
        if (host_warps) { actor->take_portal = -1; return; }   // joined: the host's portal moves us (it may refuse: a quest's level), its reassign brings us
        cues.cue("player_townportal_enter", now_ms, actor->player.x, actor->player.y);
        if (actor->take_portal == 2) {                       // to Tristram, by its own portal back (object 60)
            actor->take_portal = -1;
            const Level* tristram = game_data->level(d2d::rules::CainQuest::kTristram);
            if (!tristram || tristram->ds1.width() == 0) { d2d::log::info("not implemented: level 38 (the Cairn Stones' portal)"); return; }
            const auto back = std::ranges::find(tristram->npcs, 60, &Npc::object_id);
            // ponytail: no object 60 among Tristram's presets (the server makes it); its twin stands at the map's centre (agent A's).
            const float back_x = back == tristram->npcs.end() ? float(tristram->ds1.width()) / 2 : back->x, back_y = back == tristram->npcs.end() ? float(tristram->ds1.height()) / 2 : back->y;
            if (portal[3].level != tristram) portal[3] = { tristram, back_x, back_y, now_ms };
            arrive(tristram, portal[3].x, portal[3].y + 0.6f, "the Cairn Stones' portal");
            return;
        }
        if (actor->take_portal == 6) {                       // down to Duriel (OperateFn 43: into level 73)
            actor->take_portal = -1;
            const Level* lair = game_data->level(d2d::rules::SevenTombsQuest::kLair);
            if (!lair || lair->ds1.width() == 0) { d2d::log::info("not implemented: level 73 (Duriel's Lair)"); return; }
            const auto [to_x, to_y] = lair->portal_spot.first >= 0 ? lair->nearest_free(lair->portal_spot.first, lair->portal_spot.second)
                                                                  : lair->nearest_free(float(lair->ds1.width()) / 2, float(lair->ds1.height()) / 2);
            arrive(lair, to_x, to_y, "the way down to Duriel");
            return;
        }
        if (actor->take_portal >= 3 && std::size_t(actor->take_portal) < kGamePortals) {   // back to the Cairn Stones; the Canyon's, Act 5's pairs
            const auto& twin = portal[std::size_t(actor->take_portal ^ 1)];
            actor->take_portal = -1;
            if (!twin.level) { d2d::log::info("not implemented: the portal's far level isn't built"); return; }
            arrive(twin.level, twin.x, twin.y + 0.6f, "a quest portal");
            return;
        }
        const std::size_t arrival = std::size_t(actor->take_portal ^ 1);
        const auto other_end = portal[arrival];
        actor->take_portal = -1;
        // Back at the end its owner cast it from (player data +0x48,
        // FUN_005353f0): the pair closes (FUN_00584870). Anyone else's trip
        // keeps it.
        if (arrival == actor->portal_pair) portal[arrival] = portal[arrival + 1] = {};
        arrive(other_end.level, other_end.x, other_end.y + 0.6f, "a town portal");
    }

auto World::deal(const Command& command, std::uint32_t now_ms) -> bool {
        const auto& item_rules = game_data->rules;
        const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
        if (const auto* trade = std::get_if<cmd::OpenTrade>(&command)) {
            if (std::size_t(trade->npc) >= level->npcs.size()) return true;
            if (trade->gamble) actor->store = d2d::rules::open_gamble(item_rules, level->npcs[std::size_t(trade->npc)].id, clvl);
            else if (const auto kept = stocks.find(trade->npc); kept != stocks.end()) { actor->store = kept->second; actor->store.mode = 0; actor->store.first_tab(); actor->store.pressed = {}; }
            else actor->store = open_store(*game_data, *level, trade->npc, act(), actor->character.header.active_difficulty(), clvl, actor->character.expansion,
                                    fight.spawning.proxy, fight.spawning.game, &loot.found_uniques);
            actor->store.npc = trade->npc;
            actor->store.header = actor->character.header;
            actor->events.push_back(ev::OpenUI{ ev::OpenUI::trade, trade->npc });
            return true;
        }
        if (const auto* hire = std::get_if<cmd::OpenHire>(&command)) {
            hire_list(act());                         // ten offers (FUN_00576070)
            actor->events.push_back(ev::OpenUI{ ev::OpenUI::hire, hire->npc });
            return true;
        }
        if (const auto* buy = std::get_if<cmd::Buy>(&command)) {
            if (actor->store.npc < 0 || buy->stock < 0) return true;
            actor->store.reduced = reduced_prices();
            if (buy->tab >= 0 && buy->tab < 4) actor->store.tab = buy->tab;   // the client's tab: the world's stays where the store opened
            if (std::size_t(buy->stock) >= actor->store.tabs[std::size_t(actor->store.tab)].size()) return true;
            if (actor->store.gamble) d2d::rules::store_gamble(item_rules, actor->store, buy->stock, actor->character.items, actor->character.stats, rng);
            else d2d::rules::store_buy(item_rules, actor->store, buy->stock, actor->character.items, actor->character.stats);
            return true;
        }
        if (const auto* sell = std::get_if<cmd::Sell>(&command)) {
            if (actor->store.npc < 0) return true;
            if (actor->held && actor->held->id == sell->item) { actor->character.items.push_back(std::move(*actor->held)); actor->held.reset(); }
            const auto found = std::ranges::find(actor->character.items, sell->item, &d2d::d2s::Item::id);
            if (found != actor->character.items.end()) d2d::rules::store_sell(item_rules, actor->store, std::size_t(found - actor->character.items.begin()), actor->character.items, actor->character.stats);
            return true;
        }
        if (const auto* repair = std::get_if<cmd::Repair>(&command)) {
            if (actor->store.npc < 0) return true;
            actor->store.reduced = reduced_prices();
            if (repair->item < 0) {
                if (d2d::rules::store_repair_all(item_rules, actor->store, actor->character.items, actor->character.stats) > 0)
                    cues.cue(d2d::rules::sound_ids::kCursorRepairItem, now_ms, actor->player.x, actor->player.y);
                return true;
            }
            const auto found = std::ranges::find(actor->character.items, repair->item, &d2d::d2s::Item::id);
            if (found != actor->character.items.end() && d2d::rules::store_repair(item_rules, actor->store, *found, actor->character.stats))
                cues.cue(d2d::rules::sound_ids::kCursorRepairItem, now_ms, actor->player.x, actor->player.y);
            return true;
        }
        // Cain's identify (FUN_00578460): 100 gold an item, all at once, unless
        // quest 4's done or its reward's due; paid purse first, then the stash
        // (FUN_00576d90).
        if (std::holds_alternative<cmd::Identify>(command)) {
            const bool any = d2d::rules::unidentified(actor->character.items) > 0;
            const int fee = d2d::rules::quest::bit(quests(), 4, 0) || d2d::rules::quest::bit(quests(), 4, 1) ? 0 : d2d::rules::unidentified(actor->character.items) * 100;
            if (!d2d::rules::pay(actor->character.stats, fee)) return true;
            d2d::rules::identify_all(actor->character.items);
            if (any) cues.cue(d2d::rules::sound_ids::kCursorIdentifyItem, now_ms, actor->player.x, actor->player.y);
            return true;
        }
        // A Scroll of Identify (isc, used up) or a Tome (ibk, a charge, stat
        // 0x46) on an unidentified item (FUN_00561ed0).
        // ponytail: no sound or message for an empty tome.
        if (const auto* with = std::get_if<cmd::IdentifyWith>(&command)) {
            auto& items = actor->character.items;
            const auto scroll = std::ranges::find(items, with->scroll, &d2d::d2s::Item::id);
            const auto item = std::ranges::find(items, with->item, &d2d::d2s::Item::id);
            if (scroll == items.end() || item == items.end() || scroll == item || item->identified) return true;
            if (scroll->code == "ibk" && scroll->quantity > 0) --scroll->quantity;
            else if (scroll->code != "isc") return true;
            item->identified = true;
            cues.cue(d2d::rules::sound_ids::kCursorIdentifyItem, now_ms, actor->player.x, actor->player.y);
            if (scroll->code == "isc") items.erase(scroll);
            return true;
        }
        if (const auto* hire = std::get_if<cmd::Hire>(&command)) {
            auto offers = hire_offers(act());
            if (hire->offer >= 0 && std::size_t(hire->offer) < offers.size() && d2d::rules::hire(offers[std::size_t(hire->offer)], actor->character.header, actor->character.stats)) {
                // The entry's taken (packet 0x50 subtype 2); none left, the list rolls anew (FUN_00577010).
                int seen = 0;
                for (auto& entry : hire_lists[std::size_t(act())])
                    if (d2d::rules::for_hire(entry) && seen++ == hire->offer) { entry.hired = true; break; }
                hire_list(act());
                spawn_merc();
            }
            return true;
        }
        // A hire NPC brings the dead merc back (FUN_00579c00): paid, full
        // life, beside the player (FUN_00579aa0).
        // ponytail: no 0x5b..0x5a messages (refused / done) back to the client.
        if (const auto* resurrect = std::get_if<cmd::ResurrectMerc>(&command)) {
            auto& header = actor->character.header;
            if (std::size_t(resurrect->npc) >= level->npcs.size() || std::ranges::find(d2d::rules::kMercNpcs, level->npcs[std::size_t(resurrect->npc)].hc_idx) == d2d::rules::kMercNpcs.end())
                return true;
            const int merc_level = d2d::rules::merc_stats(game_data->rules, header.merc_type, header.merc_exp).level;
            if (d2d::rules::resurrect_merc(merc_level, header, actor->character.stats)) {
                spawn_merc();
                d2d::log::info("the merc is back");
            }
            return true;
        }
        if (std::holds_alternative<cmd::CloseTrade>(command)) { actor->store = {}; return true; }
        // Akara's reset (the 0x38 handler for her, hcIdx 0x94): while quest
        // 41 (the Den of Evil's reward) is open, stats and skills go back,
        // then it's used (FUN_0058fd50: bit 0 on, 1 off).
        if (const auto* respec = std::get_if<cmd::Respec>(&command)) {
            namespace quest = d2d::rules::quest;
            if (std::size_t(respec->npc) >= level->npcs.size() || level->npcs[std::size_t(respec->npc)].hc_idx != d2d::rules::monster_ids::kAkara
                || !quest::bit(quests(), d2d::rules::kRespecQuest, 1)) return true;
            const auto cls = std::size_t(std::clamp<int>(actor->character.header.cls, 0, 6));
            const auto& start = game_data->class_start[cls];
            d2d::rules::respec(actor->character.stats, { start.str, start.ene, start.dex, start.vit }, game_data->class_gains[cls]);
            d2d::rules::quest::set(quests(), d2d::rules::kRespecQuest, 0);
            d2d::rules::quest::set(quests(), d2d::rules::kRespecQuest, 1, false);
            d2d::log::info("Akara reset the stat and skill points");
            return true;
        }
        // Warriv's caravan (FUN_00579d60): hcIdx 0x9b, once quest 6 is done,
        // to Lut Gholein (FUN_0054b830(0x28, 0)); Act 2 opens (FUN_005467e0:
        // quest 7 bits 0 and 13) and its town's waypoint is active. hcIdx 0xaf
        // back to the camp (FUN_0054b830(1, 5): spawn index 5, DS1 special 31).
        if (const auto* caravan = std::get_if<cmd::Caravan>(&command)) {
            namespace quest = d2d::rules::quest;
            if (std::size_t(caravan->npc) >= level->npcs.size()) return true;
            const int hc_idx = level->npcs[std::size_t(caravan->npc)].hc_idx;
            // Meshif (FUN_00579d60 hcIdx 0xd2): once quest 14 is done, to Kurast
            // Docks (FUN_0054b830(0x4b, 0)); FUN_005467e0: unless Act 3 is open
            // (quest 15 bit 0), quest 10's bits 0 and 13 if not yet (and
            // FUN_00544160 takes "msf " / "vip " off the player), then quest
            // 15's bits 0 and 13; the town's waypoint. hcIdx 0x108 back to
            // Lut Gholein (FUN_0054b830(0x28, 0)).
            if (hc_idx == d2d::rules::monster_ids::kMeshifAct3) {
                if (const Level* lut = game_data->level(d2d::rules::level_ids::kLutGholein); lut && lut->start.first >= 0) arrive(lut, lut->start.first, lut->start.second, "Meshif's boat");
                return true;
            }
            if (hc_idx == d2d::rules::monster_ids::kMeshif) {
                if (!quest::bit(quests(), d2d::rules::SevenTombsQuest::kQuest, 0)) return true;
                const Level* docks = game_data->level(d2d::rules::level_ids::kKurastDocks);
                if (!docks || docks->start.first < 0) { d2d::log::warn("Kurast Docks didn't build; the boat stays"); return true; }
                if (!quest::bit(quests(), 15, 0)) {
                    if (!quest::bit(quests(), 10, 0)) {
                        d2d::rules::quest::set(quests(), 10, 0); d2d::rules::quest::set(quests(), 10, 13);
                        for (const char* code : { "msf", "vip" }) take_carried(code);
                    }
                    d2d::rules::quest::set(quests(), 15, 0); d2d::rules::quest::set(quests(), 15, 13);
                    movie = 3;                                                     // S->C 0x61 {3}: Act03start
                }
                set_waypoint(waypoint_index(*game_data, d2d::rules::level_ids::kKurastDocks));
                arrive(docks, docks->start.first, docks->start.second, "Meshif's boat");
                return true;
            }
            if (hc_idx == d2d::rules::monster_ids::kWarrivAct2) {
                const auto& camp = game_data->town;
                const auto spot = camp.spawn_at[5].first >= 0 ? camp.spawn_at[5] : camp.start;
                arrive(&camp, spot.first, spot.second, "Warriv's caravan");
                return true;
            }
            if (hc_idx != d2d::rules::monster_ids::kWarriv || !quest::bit(quests(), d2d::rules::AndyQuest::kQuest, 0)) return true;
            const Level* lut_gholein = game_data->level(d2d::rules::level_ids::kLutGholein);
            if (!lut_gholein || lut_gholein->start.first < 0) { d2d::log::warn("Lut Gholein didn't build; the caravan stays"); return true; }
            if (!quest::bit(quests(), 7, 0)) { d2d::rules::quest::set(quests(), 7, 0); d2d::rules::quest::set(quests(), 7, 13); movie = 2; }   // S->C 0x61 {2}: Act02start
            set_waypoint(waypoint_index(*game_data, d2d::rules::level_ids::kLutGholein));
            // The arrival's level change runs the quests' enter hooks: Andariel's
            // (the Rogues get Cain if he's still caged, FUN_00596de0 / FUN_00597310).
            // Act 1's levels stay built: game.exe's server keeps every act till the game ends (FUN_0052c7f0).
            arrive(lut_gholein, lut_gholein->start.first, lut_gholein->start.second, "Warriv's caravan");
            return true;
        }
        // Charsi's imbue (FUN_00579d60, kind 0 at hcIdx 0x9a): while it's
        // due (quest 3 bit 1), the item in hand made anew, rare; the
        // reward's used (FUN_00591790).
        // ponytail: no S->C 0x58 result (waits for networking); the new item stays in hand.
        // The same C->S 0x38 kind 0 at Larzuk (0x1ff: quest 0x23 bit 1) sockets
        // the item in hand (socket_item, FUN_005877c0), at Anya (0x200: quest
        // 0x26 bit 1) puts the player's name on it (flag 0x1000000, FUN_0058bc00).
        if (const auto* imbue = std::get_if<cmd::Imbue>(&command)) {
            if (std::size_t(imbue->npc) >= level->npcs.size() || !actor->held) return true;
            const int service_npc = level->npcs[std::size_t(imbue->npc)].hc_idx;
            if (service_npc == d2d::rules::SiegeQuest::kLarzuk) {
                if (!d2d::rules::quest::bit(quests(), d2d::rules::SiegeQuest::kQuest, 1) || !d2d::rules::socketable(item_rules, *actor->held)) return true;
                actor->held = d2d::rules::socket_item(item_rules, *actor->held);
                siege.socketed(quests());
                d2d::log::info("Siege on Harrogath: Larzuk put {} sockets in the {}", actor->held->sockets, actor->held->code);
                return true;
            }
            if (service_npc == d2d::rules::BetrayalQuest::kAnya) {
                if (!d2d::rules::quest::bit(quests(), d2d::rules::BetrayalQuest::kQuest, 1) || !d2d::rules::nameable(item_rules, *actor->held)) return true;
                actor->held->personalized = true;
                actor->held->owner = actor->character.header.name;
                betrayal.personalized(quests());
                d2d::log::info("Betrayal of Harrogath: Anya personalized the {}", actor->held->code);
                return true;
            }
            if (service_npc != d2d::rules::monster_ids::kCharsi
                || !d2d::rules::quest::bit(quests(), d2d::rules::ToolsQuest::kQuest, 1) || !d2d::rules::imbuable(item_rules, *actor->held)) return true;
            auto made = d2d::rules::imbue_item(item_rules, *actor->held, int(actor->character.stats.get(d2d::d2s::kLevel)), rng);
            made.location = actor->held->location; made.panel = actor->held->panel; made.column = actor->held->column; made.row = actor->held->row;
            actor->held = std::move(made);
            tools.imbued(quests());
            d2d::log::info("Tools of the Trade: Charsi imbued the {}", actor->held->code);
            return true;
        }
        if (const auto* run = std::get_if<cmd::Run>(&command)) { actor->running = run->running; return true; }
        if (const auto* chat = std::get_if<cmd::Chat>(&command)) {
            if (chat->npc < 0 && std::size_t(actor->talking[0]) < level->npcs.size()) {
                const int hc_idx = level->npcs[std::size_t(actor->talking[0])].hc_idx;
                act1_talk_closed(hc_idx);
                act2_talk_closed(hc_idx);
                act3_talk_closed(hc_idx);
                act4_talk_closed(hc_idx);
                act5_talk_closed(hc_idx);
            }
            actor->talking = { chat->npc, -1, -1 };
            return true;
        }
        // FUN_0054c5d0 / FUN_00584f60: not within 10 s of the last level
        // change, only to another active waypoint; arrival by its preset
        // (tile + 3 subtiles, FUN_0066ad80), which lights up if it's dark.
        if (const auto* travel = std::get_if<cmd::Waypoint>(&command)) {
            const int index = waypoint_index(*game_data, travel->level);
            if (now - actor->arrived_at < 10000 || std::size_t(travel->npc) >= level->npcs.size() || level->npcs[std::size_t(travel->npc)].operate_fn != d2d::rules::operate_fn::kWaypoint
                || travel->level == level->id || !actor->character.header.waypoint(actor->character.header.active_difficulty(), index)) return true;
            if (host_warps) return true;                       // joined: the host's 0x49 moves us, its reassign brings us
            const Level* destination = game_data->level(travel->level);
            if (!destination || destination->ds1.width() == 0) { d2d::log::info("not implemented: level {} (a waypoint)", travel->level); return true; }
            // Its preset (objects.txt SubClass 0x40): its room makes it as the player lands.
            const auto& subclass = game_data->obj_subclass;
            const auto unit = std::ranges::find_if(destination->units, [&](const auto& preset) {
                return preset.type == d2d::rules::unit_type::kObject && preset.id >= 0 && std::size_t(preset.id) < subclass.size() && (subclass[std::size_t(preset.id)] & d2d::rules::object_ids::kSubclassWaypoint);
            });
            const bool known = unit != destination->units.end();
            const float arrive_x = known ? std::floor((float(unit->x) + 0.5f) / 5) + 0.6f : float(destination->ds1.width()) / 2;
            const float arrive_y = known ? std::floor((float(unit->y) + 0.5f) / 5) + 0.6f : float(destination->ds1.height()) / 2;
            arrive(destination, arrive_x, arrive_y, "a waypoint");
            if (const auto found = std::ranges::find(destination->npcs, 23, &Npc::operate_fn); found != destination->npcs.end() && found->mode == "NU")
                operated.try_emplace({ destination, int(found - destination->npcs.begin()) }, now);
            return true;
        }
        if (const auto* message = std::get_if<cmd::QuestMessage>(&command)) {   // only what that NPC has to say
            if (hosted_quests || std::size_t(message->npc) >= level->npcs.size()) return true;
            if (level->npcs[std::size_t(message->npc)].operate_fn == d2d::rules::operate_fn::kJournal) {     // the journal's 396 (a2q4's +0xcc: no NPC)
                if (arcane.said(quests(), game_bits, -1, message->string, room_of(*level, actor->player.x, actor->player.y))) canyon_portal(now);
                return true;
            }
            if (level->npcs[std::size_t(message->npc)].operate_fn == d2d::rules::operate_fn::kMoldyTome) {   // the tome's 127 (FUN_00594960: no NPC check)
                if (message->string == d2d::rules::TowerQuest::kTome && !closed_at_join[std::size_t(d2d::rules::TowerQuest::kQuest)]) { tower.read_tome(quests()); d2d::log::info("The Forgotten Tower: read the Moldy Tome"); }
                return true;
            }
            const int hc_idx = level->npcs[std::size_t(message->npc)].hc_idx;
            if (!std::ranges::contains(quest_talk(hc_idx, false), message->string, &d2d::rules::QuestMsg::string)) return true;
            act1_said(hc_idx, message->string);
            act2_said(hc_idx, message->string);
            act3_said(hc_idx, message->string);
            act4_said(hc_idx, message->string);
            act5_said(hc_idx, message->string);
            return true;
        }
        return false;
    }

auto World::apply(const Command& command, std::uint32_t now_ms) -> void {
        // The character's own: through whatever the player's doing.
        if (const auto* stat_point = std::get_if<cmd::StatPoint>(&command)) {
            if (stat_point->stat < 0 || stat_point->stat > 3 || stat_point->count < 1) return;
            (void)d2d::rules::spend_stat_points(actor->character.stats, stat_point->stat, std::min<int>(stat_point->count, int(actor->character.stats.get(d2d::d2s::kStatPts))),
                                          game_data->class_gains[std::size_t(actor->character.header.cls)]);
            return;
        }
        if (const auto* skill_point = std::get_if<cmd::SkillPoint>(&command)) {
            if (actor->character.stats.get(d2d::d2s::kSkillPts) > 0
                && d2d::rules::can_learn(game_data->rules, actor->character.header.cls, skill_point->skill, actor->character.stats.skills, int(actor->character.stats.get(d2d::d2s::kLevel))))
                (void)d2d::rules::learn_skill(game_data->rules, actor->character.header.cls, skill_point->skill, actor->character.stats.skills, actor->character.stats);
            return;
        }
        // The weapon switch (C->S 0x60, FUN_0054ce70 -> ItemMode.cpp's
        // FUN_005616a0): an expansion character's hands (body slots 4 / 5)
        // and the switch's (0xb / 0xc) trade places, and the left / right
        // skills the pair the other set kept (header +0x80 / +0x84); a kept
        // one no longer usable gives way (FUN_00647960 says 2 or 7). Not
        // while dying or dead (FUN_005541b0).
        // ponytail: it gives way to Attack, not the skill list's own
        // (FUN_00643930 / FUN_00643960, untraced).
        if (std::holds_alternative<cmd::SwapWeapons>(command)) {
            namespace body = d2d::d2s::body_location;
            if (!actor->character.expansion || fight.dead()) return;
            for (auto& item : actor->character.items) {
                if (item.location != d2d::d2s::item_location::kEquipped) continue;
                if (item.slot == body::kRightArm || item.slot == body::kLeftArm) item.slot += body::kRightArmSwitch - body::kRightArm;
                else if (item.slot == body::kRightArmSwitch || item.slot == body::kLeftArmSwitch) item.slot -= body::kRightArmSwitch - body::kRightArm;
            }
            auto& header = actor->character.header;
            if (header.right_skill == std::uint32_t(kResurrectSkill)) header.right_skill = actor->right_before;   // co-op's Resurrect stays with no set
            header.weapon_set ^= 1;
            const auto kept = [&](std::uint32_t id, bool left) {
                const auto* skill = game_data->skills.get(int(id));
                return skill && !skill->passive && (!left || skill->leftskill) && skill_level(*game_data, actor->character, int(id), {}) > 0 ? id : 0u;
            };
            std::swap(header.left_skill, header.left_swap);
            std::swap(header.right_skill, header.right_swap);
            header.left_skill = kept(header.left_skill, true);
            header.right_skill = kept(header.right_skill, false);
            const auto* right = game_data->skills.get(int(header.right_skill));
            actor->aura = right && right->aura ? int(header.right_skill) : 0;
            return;
        }
        if (const auto* select = std::get_if<cmd::SelectSkill>(&command)) {
            if (select->skill == kResurrectSkill && (select->left || players.size() < 2)) return;   // the right button's, in co-op
            if (select->skill == kResurrectSkill && actor->character.header.right_skill != std::uint32_t(kResurrectSkill)) actor->right_before = actor->character.header.right_skill;
            (select->left ? actor->character.header.left_skill : actor->character.header.right_skill) = std::uint32_t(select->skill);
            if (!select->left) if (const auto* skill = game_data->skills.get(select->skill)) actor->aura = skill->aura ? select->skill : 0;
            return;
        }
        if (std::holds_alternative<cmd::Transmute>(command)) { transmute(now_ms); return; }
        if (const auto* drop = std::get_if<cmd::DropGold>(&command)) {        // FUN_00535510
            auto& carried = actor->character.stats.values[d2d::d2s::kGold];
            if (drop->amount > 0 && drop->amount <= carried && !fight.dead()) {
                carried -= drop->amount;
                loot.enter(level);
                const auto before = loot.ground.size();
                loot.put({ .code = "gld", .gold = drop->amount }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
                if (loot.ground.size() > before) loot.ground.back().dropped_by = actor->id;
            }
            return;
        }
        if (const auto* gold = std::get_if<cmd::StashGold>(&command)) {         // FUN_00564d50
            using namespace d2d::d2s;
            constexpr std::int64_t kStashGoldMax = 2500000;                    // FUN_00623460
            auto& values = actor->character.stats.values;
            const std::int64_t amount = gold->amount, carried = values[kGold], bank = values[kGoldBank];
            if (gold->deposit && amount > 0 && amount <= carried) {
                const std::int64_t moved = std::min(amount, std::max<std::int64_t>(kStashGoldMax - bank, 0));
                values[kGold] -= moved; values[kGoldBank] += moved;
            } else if (!gold->deposit && amount > 0 && amount <= bank && carried + amount <= values[kLevel] * 10000) {
                values[kGoldBank] -= amount; values[kGold] += amount;
            }
            return;
        }
        if (const auto* use = std::get_if<cmd::UseItem>(&command)) {
            const auto found = std::ranges::find(actor->character.items, use->item, &d2d::d2s::Item::id);
            if (found != actor->character.items.end() && (found->code == "tsc" || found->code == "tbk")) read_portal(found, now_ms);
            else if (found != actor->character.items.end() && found->code == d2d::rules::GoldenBirdQuest::kPotion) {   // the Potion of Life (0x55e44e)
                if (!hosted_quests && d2d::rules::GoldenBirdQuest::drink(quests())) {
                    actor->character.stats.values[d2d::d2s::kMaxLife] += std::int64_t(d2d::rules::GoldenBirdQuest::kLifeBonus) << 8;
                    actor->character.items.erase(found);
                    d2d::log::info("The Golden Bird: the Potion of Life, +20 life");
                }
            }
            else if (found != actor->character.items.end() && found->code == "ass") {   // the Book of Skill (0x55e3d0)
                if (!hosted_quests && d2d::rules::RadamentQuest::read_book(quests())) {   // a joined game's: the host's (the use went to it)
                    ++actor->character.stats.values[d2d::d2s::kSkillPts];
                    actor->character.items.erase(found);
                    d2d::log::info("Radament's Lair: the Book of Skill, a skill point");
                }
            }
            else fight.drink_item(use->item, now_ms);
            return;
        }
        if (const auto* merc_item = std::get_if<cmd::MercItem>(&command)) {   // 0x61 (FUN_0054d430)
            if (fight.dead() || !actor->merc || actor->merc->hit_points <= 0 || !actor->character.expansion) return;
            auto& worn = actor->character.merc_items;
            if (!actor->held && merc_item->slot >= 1 && merc_item->slot <= 10) {          // FUN_0054d130: off the merc into the hand
                if (const auto found = std::ranges::find(worn, merc_item->slot, &d2d::d2s::Item::slot); found != worn.end()) {
                    actor->held = std::move(*found);
                    worn.erase(found);
                    actor->held->location = d2d::d2s::item_location::kStored;
                    actor->held->slot = 0;
                }
                return;
            }
            if (!actor->held || merc_item->slot != 0) return;
            if (fight.feed_merc_item(*actor->held, now_ms)) { actor->held.reset(); return; }   // FUN_005bf240: a potion it drinks
            // FUN_0062eaf0 reads the merc's stats with its gear, as the panel shows them.
            const auto& merc_row = actor->merc_st;
            const auto gear = fight.merc_gear();
            const int merc_str = int(merc_row.str + gear.sum[std::size_t(d2d::d2s::kStr)]), merc_dex = int(merc_row.dex + gear.sum[std::size_t(d2d::d2s::kDex)]);
            const auto* hireling = d2d::rules::hireling_row(game_data->rules, int(actor->character.header.merc_type), merc_row.level);
            const int slot = hireling ? d2d::rules::merc_slot(game_data->rules, *actor->held, hireling->cls, merc_str, merc_dex, merc_row.level) : 0;
            if (slot == 0) return;                                                         // FUN_00553380: no
            cues.cue(item_sound(*game_data, *actor->held, false), now_ms, actor->player.x, actor->player.y);   // FUN_0048b7c0: its dropsound
            auto put = std::move(*actor->held);
            actor->held.reset();
            if (const auto found = std::ranges::find(worn, slot, &d2d::d2s::Item::slot); found != worn.end()) {   // what was there into the hand
                actor->held = std::move(*found);
                worn.erase(found);
                actor->held->location = d2d::d2s::item_location::kStored;
                actor->held->slot = 0;
            }
            put.location = d2d::d2s::item_location::kEquipped;
            put.slot = slot;
            worn.push_back(std::move(put));
            return;
        }
        if (const auto* use_belt = std::get_if<cmd::UseBelt>(&command)) {
            if (use_belt->slot >= 0 && use_belt->slot < 4 && !fight.dead()) { if (use_belt->merc) fight.feed_merc(use_belt->slot, now_ms); else fight.drink(use_belt->slot, now_ms); }
            return;
        }
        // The cursor (cursor.hpp works out which from a click).
        const auto& tables = game_data->rules;
        const int cls = actor->character.header.cls;
        // Picked up: the item's usesound (FUN_00487740, FUN_00498a90); put in a
        // grid, the belt or a body slot: its dropsound (FUN_004c1d60's callers).
        // The put_* calls move the held item away, so its name is read first.
        const auto sound_cue = [&](std::string_view sound) { cues.cue(sound, now_ms, actor->player.x, actor->player.y); };
        const auto item_cue = [&](const d2d::d2s::Item& item, bool use) { sound_cue(item_sound(*game_data, item, use)); };
        if (const auto* to_cursor = std::get_if<cmd::ToCursor>(&command)) {
            const auto found = std::ranges::find(actor->character.items, to_cursor->item, &d2d::d2s::Item::id);
            if (!actor->held && found != actor->character.items.end()
                && d2d::rules::pick_up(actor->character.items, actor->held, std::size_t(found - actor->character.items.begin())))
                item_cue(*actor->held, true);
            return;
        }
        if (const auto* drop = std::get_if<cmd::Drop>(&command)) {           // FUN_0054ab40: the item in hand, not while dead
            if (actor->held && actor->held->id == drop->item && !fight.dead()) {
                loot.enter(level);
                loot.place(std::move(*actor->held), actor->player.x, actor->player.y, now_ms);
                actor->held.reset();
                cues.cue(d2d::rules::sound_ids::kCursorPointDrop, now_ms, actor->player.x, actor->player.y);
            }
            return;
        }
        if (const auto* to_grid = std::get_if<cmd::ToGrid>(&command)) {
            const auto* layout = to_grid->panel == d2d::d2s::item_panel::kInventory ? &game_data->inv_layout[std::size_t(cls)] : to_grid->panel == d2d::d2s::item_panel::kCube ? &game_data->cube_layout
                          : to_grid->panel == d2d::d2s::item_panel::kStash ? &game_data->stash_layout[actor->character.expansion ? 1 : 0] : nullptr;
            if (actor->held && layout) {
                const auto sound = item_sound(*game_data, *actor->held, false);
                if (d2d::rules::put_in_grid(tables, actor->character.items, actor->held, to_grid->panel, layout->cols, layout->rows, to_grid->col, to_grid->row)) sound_cue(sound);
            }
            return;
        }
        // Refused, the class says so (FUN_00490fc0 and the other body-slot
        // handlers -> FUN_004cb9c0): "can't use this yet" (voice 0x14) when
        // only the stats / level are short, else "impossible" (0x13).
        if (const auto* to_body = std::get_if<cmd::ToBody>(&command)) {
            if (actor->held && to_body->slot >= d2d::d2s::body_location::kFirst && to_body->slot <= d2d::d2s::body_location::kLast) {
                const auto sound = item_sound(*game_data, *actor->held, false);
                const int level_needed = required_level(*game_data, *actor->held);
                if (d2d::rules::equip(tables, actor->character.items, actor->held, to_body->slot, wearer(*game_data, cls, actor->character.items, actor->character.stats), level_needed))
                    sound_cue(sound);
                else if (const auto& put = *actor->held;                         // refused: equip left it in hand
                         !d2d::rules::can_wear(tables, put, to_body->slot, wearer(*game_data, cls, actor->character.items, actor->character.stats), level_needed))
                    class_cue(d2d::rules::can_wear(tables, put, to_body->slot, { cls, std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max() }) ? "cantuseyet" : "impossible_1", now_ms);
            }
            return;
        }
        if (const auto* to_book = std::get_if<cmd::ScrollToBook>(&command)) {   // FUN_0055ef20
            const auto book = std::ranges::find(actor->character.items, to_book->book, &d2d::d2s::Item::id);
            if (actor->held && book != actor->character.items.end() && d2d::rules::tome_takes(tables, *actor->held, *book)) {
                ++book->quantity;
                actor->held.reset();
            }
            return;
        }
        if (const auto* socket = std::get_if<cmd::Socket>(&command)) {        // FUN_00562660
            const auto target = std::ranges::find(actor->character.items, socket->item, &d2d::d2s::Item::id);
            if (actor->held && target != actor->character.items.end() && (target->location == d2d::d2s::item_location::kStored || target->location == d2d::d2s::item_location::kEquipped)
                && d2d::rules::takes_socket(tables, *actor->held, *target)) {
                actor->held->location = d2d::d2s::item_location::kSocketed;
                actor->held->column = int(target->socketed_items.size());
                actor->held->row = actor->held->panel = 0;
                target->socketed_items.push_back(std::move(*actor->held));
                actor->held.reset();
                const auto& type = tables.item_info.at(target->socketed_items.back().code).type;   // misc.txt's usesound by kind
                cues.cue(d2d::rules::type_is(tables, type, "rune") ? "item_rune" : d2d::rules::type_is(tables, type, "jewl") ? "item_jewel" : "item_gem", now_ms, actor->player.x, actor->player.y);
            }
            return;
        }
        if (const auto* to_belt = std::get_if<cmd::ToBelt>(&command)) {
            const auto& belt = game_data->belts[std::size_t(belt_index(*game_data, actor->character.items))];
            if (actor->held && to_belt->box >= 0 && to_belt->box < belt.boxes) {
                const auto sound = item_sound(*game_data, *actor->held, false);
                if (d2d::rules::put_in_belt(tables, actor->character.items, actor->held, to_belt->box, belt.boxes)) sound_cue(sound);
            }
            return;
        }
        if (deal(command, now_ms)) {
            if (actor->store.npc >= 0 && !actor->store.gamble) stocks[actor->store.npc] = actor->store;   // what the trade left in the vendor's stock
            return;
        }
        if (actor->pmode >= 0 && !actor->ghost) return;   // busy (a ghost, dead, only walks: the tick lets nothing else through)
        const bool in_moor = !in_town(level);
        auto walk_to = [&](float x, float y, bool fresh) {
            actor->target_x = x; actor->target_y = y;
            actor->player.walking = true;
            actor->interact_npc = -1;
            if (fresh) actor->take_portal = actor->take_corpse = -1;
            if (!fresh) return;
            actor->attack_mon = actor->pick_item = -1;
            actor->take_warp = -1;                                                  // a click on a warp: go through it
            for (std::size_t i = 0; i < level->warps.size(); ++i)
                if (std::hypot(level->warps[i].x + 0.5f - x, level->warps[i].y + 0.5f - y) < 2.f) actor->take_warp = int(i);
        };
        if (const auto* move = std::get_if<cmd::Move>(&command)) { actor->raising = -1; walk_to(move->x, move->y, move->fresh); return; }
        if (const auto* use = std::get_if<cmd::UseSkill>(&command); use && use->skill == kResurrectSkill) {   // co-op: up to the body, then the channel
            const auto lying = std::ranges::find_if(players, [&](const auto& other) { return other->id == use->unit && other->body && other->body->level == level; });
            if (lying == players.end() || fight.dead()) return;
            actor->raising = use->unit;
            walk_to((*lying)->body->x, (*lying)->body->y, true);
            return;
        }
        if (std::holds_alternative<cmd::UseSkill>(command)) actor->raising = -1;
        if (const auto* pickup = std::get_if<cmd::Pickup>(&command)) {                  // walk to it, pick it up
            const int ground_index = loot.index_of(pickup->item);
            if (ground_index < 0) return;
            walk_to(loot.ground[std::size_t(ground_index)].x, loot.ground[std::size_t(ground_index)].y, true);
            actor->pick_item = pickup->item;
            return;
        }
        if (const auto* interact = std::get_if<cmd::Interact>(&command)) {               // walk to it; operate or talk on arrival
            if (interact->npc <= -3000 && interact->npc > -3000 - int(actor->corpses.size())) {   // one's corpse (-3000 - k)
                const auto& body = actor->corpses[std::size_t(-3000 - interact->npc)];
                if (body.level != level) return;
                walk_to(body.x, body.y, true);
                actor->take_corpse = -3000 - interact->npc;
                return;
            }
            if (interact->npc <= -2000 && interact->npc > -2000 - int(portal.size())) {                       // a town portal (the client names them -2000 - k)
                const auto& entry_portal = portal[std::size_t(-2000 - interact->npc)];
                if (entry_portal.level != level) return;
                walk_to(entry_portal.x, entry_portal.y, true);
                actor->take_portal = -2000 - interact->npc;
                return;
            }
            if (std::size_t(interact->npc) >= level->npcs.size()) return;
            const auto& npc = level->npcs[std::size_t(interact->npc)];
            const auto& state = npc_states[std::size_t(interact->npc)];
            const float npc_x = npc.path.empty() ? npc.x : state.x, npc_y = npc.path.empty() ? npc.y : state.y;
            walk_to(npc_x, npc_y, true);
            const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& menu_entry) { return menu_entry.hc_idx == npc.hc_idx; });
            const bool usable = is_door(npc.operate_fn) || npc.operate_fn == d2d::rules::operate_fn::kWell || npc.operate_fn == d2d::rules::operate_fn::kMoldyTome || (operable(npc.operate_fn) && !npc.preoperated && !operated.contains({ level, interact->npc }));
            if (npc.operate_fn == d2d::rules::operate_fn::kStash || npc.operate_fn == d2d::rules::operate_fn::kWaypoint || npc.operate_fn == d2d::rules::ToolsQuest::kStand || usable || quest_object(npc.operate_fn) || (npc.root == "monsters" && menu)) actor->interact_npc = interact->npc;
            return;
        }
        const auto& use_skill = std::get<cmd::UseSkill>(command);
        const auto* skill = game_data->skills.get(use_skill.skill);
        const int monster_index = fight.monster_index(use_skill.unit);
        const bool live = monster_index >= 0 && fight.monsters[std::size_t(monster_index)].targetable();
        if (skill && !skill->in_town && !in_moor) {
            if (fight.sound_rng(2)) class_cue("not_in_town_1", now_ms);
            else class_cue("not_in_town_2", now_ms);
        } else if (skill && self_cast(*skill)) {                                            // Holy Shield: where the player stands
            if (fight.cast(use_skill.skill, now_ms)) actor->player.walking = false;
        } else if (skill && skill->srvdofunc == ServerDoFunction::kWhirlwind && monster_index < 0) {                      // Whirlwind to that point (FUN_005d8f50)
            actor->move_x = use_skill.x; actor->move_y = use_skill.y;
            actor->attack_mon = -1;
            actor->attack_skill = use_skill.skill;
            actor->interact_npc = actor->pick_item = -1;
            actor->player.walking = false;
            actor->player.dir = direction16(actor->move_x - actor->player.x, actor->move_y - actor->player.y);
            (void)fight.start_swing(now_ms);
        } else if (skill && !live && (fight.missile_skill(*skill) || fight.spot_skill(*skill)) && (in_moor || skill->in_town)) {
            actor->interact_npc = actor->pick_item = -1;                                   // a missile, Teleport, Corpse Explosion at the spot
            if (fight.cast_missile(use_skill.skill, use_skill.x, use_skill.y, now_ms)) actor->player.walking = false;
        } else if (skill && !live && fight.summon_skill(*skill) && in_moor) {        // Raise Skeleton: the corpse there
            actor->interact_npc = actor->pick_item = -1;
            if (fight.cast_summon(use_skill.skill, use_skill.x, use_skill.y, now_ms)) actor->player.walking = false;
        } else if (live) {                                                   // walk up to it, then attack
            actor->target_x = use_skill.x; actor->target_y = use_skill.y;
            actor->player.walking = true;
            actor->interact_npc = actor->pick_item = -1;
            actor->attack_mon = monster_index;
            actor->attack_skill = use_skill.skill;
        }
    }

// A frame's stamina (FUN_00580c20 / FUN_00580810 event 3): a run outside
// town drains it (FUN_0057f240), at 0 the run is a walk (FUN_0057f090: no
// run at stamina 0); then it regens by mode (FUN_00580500).
// ponytail: the run stays a run while stamina lasts and walks at 0 each
// frame, not at the next move as game.exe; the same since a walk at 0 never
// regens.
// A walking or running player's footsteps (FUN_004caf60): two a cycle of the
// walk's animation, from its start, on the floor tile under them (its DT1
// material, else the level's SoundEnviron Material 1), each at least two
// thirds of a step after the last. The animation's rate is the walk's
// (rules::moving_anim_rate), as the client's footsteps read it (unit +0x4c).
auto World::greet_sound(const Npc& npc, int kind) -> int {
        const auto* greeting = d2d::rules::sound_ids::greeting_of(npc.hc_idx);
        if (!greeting) return 0;
        auto& last = greeted_with[npc.hc_idx];
        int row = 0;
        // ponytail: the member is a plain draw; FUN_00482680 also keeps from the group's last ones.
        for (int tries = 0; tries < 20; ++tries) {
            row = d2d::rules::sound_ids::greeting_row(*greeting, kind, day.phase, [&](int bound) { return int(fight.sound_rng(bound)); });
            if (row > 0 && std::size_t(row) < game_data->sounds.size())
                if (const int group = game_data->sounds[std::size_t(row)].group; group > 1) row += int(fight.sound_rng(group));
            if (row != last) break;
        }
        last = row;
        return row;
    }

auto World::footstep_frame(std::uint32_t tick_ms) -> void {
        using namespace d2d::rules::sound_ids;
        const int cls = actor->character.character_class;
        const bool walking = actor->player.walking && actor->pmode < 0;
        if (walking && !actor->stepping) actor->walk_from_ms = tick_ms;   // a walk starting: its cycle from here
        actor->stepping = walking;
        if (!walking || fight.dead() || cls < 0 || cls >= 7 || actor->ghost) return;
        const bool run_on = actor->running && actor->character.stats.values[d2d::d2s::kStamina] > 0;   // RN as drawn, in town too
        const bool run = run_on && !in_town(level);
        const auto& anim = fight.player_anim(run_on ? kModeRN : in_town(level) ? kModeTW : kModeWL);
        const int rate = std::max(d2d::rules::moving_anim_rate(run_on, walk_percent(*actor, tick_ms)), 1);
        const int length = int(anim.frames ? anim.frames : anim.cof_frames) * 256, count = 2;
        const int pos = int((tick_ms - actor->walk_from_ms) / 40) * rate % std::max(length, 1);
        if (!footfall(pos, rate, length, count, 0)) return;
        if (tick_ms - actor->step_sound_ms < std::uint32_t(2 * length / (count * rate) / 3) * 40) return;
        actor->step_sound_ms = tick_ms;
        cues.cue(footstep(kPlayerVoice[std::size_t(cls)].step, fight.floor_material_at(actor->player.x, actor->player.y), run), tick_ms, actor->player.x, actor->player.y);
    }

// FUN_00623f50's walk %: velocitypercent (100; running, run x 100 / walk),
// faster run/walk's effective % (150 x v / (150 + v)) and chill's -50, at
// least 25. It scales both the walk's velocity and its animation.
auto World::walk_percent(const Player& player, std::uint32_t now_ms) const -> int {
        const auto save_class = std::size_t(std::max(player.character.character_class, 0));
        const int walk = std::max(game_data->walk_velocity[save_class], 1);
        const int base = player.running && player.character.stats.values[d2d::d2s::kStamina] > 0 ? game_data->run_velocity[save_class] * 100 / walk : 100;
        const int chill = now_ms < player.chilled ? -50 : 0;   // Fight::chill_rate, for any player
        return std::max(base + d2d::rules::effective_speed(player.player_combat.frw, 150) + chill, 25);
    }

auto World::stamina_frame() -> void {
        using namespace d2d::d2s;
        if (fight.dead()) return;
        const bool town = in_town(level);
        auto& stamina = actor->character.stats.values[kStamina];
        const bool run = actor->player.walking && actor->pmode < 0 && actor->running && stamina > 0;
        if (run && !town) {
            int armor_speed = 0;
            for (const auto& item : actor->character.items)
                if (item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kTorso)
                    if (const auto found = game_data->rules.item_base.find(item.code); found != game_data->rules.item_base.end()) armor_speed = found->second.speed;
            stamina = std::max<std::int64_t>(0, stamina - d2d::rules::stamina_drain(game_data->run_drain[std::size_t(std::max(actor->character.character_class, 0))],
                                                                                  armor_speed, int(actor->psum[kStaminaDrainPercent])));
        }
        const int mode = actor->pmode >= 0 ? -1 : actor->player.walking ? (run ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        stamina = d2d::rules::stamina_regen(stamina, actor->character.stats.values[kMaxStamina], mode, int(actor->psum[kStaminaRecoveryBonus]));
    }

auto World::tick(std::span<const Order> orders, std::uint32_t now_ms, std::uint32_t last_ms) -> void {
        const float elapsed = float(now_ms - last_ms) / 1000.f;
        now = now_ms;
        // The look follows what's worn (compcode::look, the save header's bytes).
        if (!game_data->item_pieces.empty() && !game_data->comp.empty())
            each_player([&](Player& player) {
                std::vector<std::string_view> active_states;
                for (const auto& state : player.self_states) if (const auto* skill = game_data->skills.get(state.skill)) active_states.push_back(skill->aurastate);
                player.character.appearance = game_data->look_of(player.character.items, active_states);
            });
        // Levels: finished builds come in; the ones next to the player's
        // start building when it changes (GameData::level).
        game_data->poll_levels();
        fight.spawning.game_quests = game_bits;          // the town markers' inits read them as rooms come up
        // a3q7: the Dark Wanderer gone (ai.cpp) marks every player in act 3 (FUN_005bd260: word 0x20 bit 0, if missing).
        if (d2d::rules::level_act(level->id) == 2
            && std::ranges::any_of(fight.monsters, [](const Monster& monster) { return monster.gone && monster.type == d2d::rules::monster_ids::kDarkWanderer; }))
            each_player([&](Player&) { if (!d2d::rules::quest::bit(quests(), 0x20, 0)) d2d::rules::quest::set(quests(), 0x20, 0); });
        // FUN_005bd390, once a game: round where he went (0x741538's
        // (dx, dy), from index `quest seed & 1`: 7 or 8; index 7 repeats
        // index 2, bugs.md #20) object 131 at a free spot near each, mode 1,
        // gone 27 frames on (InitFn 59).
        // ponytail: made the tick he goes, not two on; the free spot is
        // nearest_free's, not FUN_00545340's size 3 / mask 0x3f11 search.
        if (const auto wanderer = std::ranges::find_if(fight.monsters, [](const Monster& monster) { return monster.gone && monster.type == d2d::rules::monster_ids::kDarkWanderer; });
            !afterglow_made && wanderer != fight.monsters.end() && !game_data->afterglow.code.empty()) {
            afterglow_made = true;
            cues.cue("object_wanderer_fissure", now_ms, wanderer->unit.x, wanderer->unit.y);
            static constexpr std::array<std::pair<int, int>, 8> kAround{ { { -3, -3 }, { -3, 0 }, { -3, 3 }, { 0, -3 }, { 0, 3 }, { 3, -3 }, { 3, 0 }, { -3, 3 } } };
            for (auto at = std::size_t(fight.spawning.quests.next() & 1); at < kAround.size(); ++at) {
                const auto [x, y] = level->nearest_free(wanderer->unit.x + float(kAround[at].first) / 5, wanderer->unit.y + float(kAround[at].second) / 5);
                fires.push_back({ level, &game_data->afterglow, x, y, now_ms, now_ms + 27 * kTickMs });
            }
            d2d::log::info("the Dark Wanderer's afterglow: {}", std::ranges::count(fires, &game_data->afterglow, &Fire::npc));
        }
        std::erase_if(fires, [&](const Fire& fire) { return fire.gone && now_ms >= fire.gone; });
        if (level != wanted_near) { want_nearby(*game_data, *level); wanted_near = level; }
        each_player([&](Player&) { item_ids(); });
        // The day moves a frame a tick (40 ms); a stall doesn't fast-forward it.
        if (now_ms - day_at > 1000) day_at = now_ms;
        for (; now_ms - day_at >= kTickMs; day_at += kTickMs) {
            day.step(act());
            credit_party([&] { act1_tick(); act2_tick(); });
            each_player([&](Player&) { stamina_frame(); footstep_frame(day_at); find_voice(day_at); });
        }
        each_player([&](Player& player) {
            if (player.boost.until && now_ms >= player.boost.until) {     // the state runs out
                if (shrine_code(player.boost.shrine) == 14) player.character.stats.values[d2d::d2s::kStamina] = player.character.stats.values[d2d::d2s::kMaxStamina];
                player.boost = {};
            }
            fight.update_fighters(now_ms);
        });
        spring_traps(now_ms);
        // Used shrines and chests: OP while it plays, then ON; a shrine back
        // to NU after its reset time (Shrines.txt Reset x 1200 + 1 frames;
        // 0 never).
        for (auto entry = operated.begin(); entry != operated.end();) {
            const auto& [key, when_ms] = *entry;
            const auto npc_index = std::size_t(key.second);
            const auto& npc = key.first->npcs[npc_index];
            const int reset = npc.operate_fn == d2d::rules::operate_fn::kShrine && std::size_t(npc.shrine) < game_data->shrines.size() ? game_data->shrines[std::size_t(npc.shrine)].reset : 0;
            const bool back = reset > 0 && now_ms - when_ms >= d2d::rules::shrine_reset_frames(reset) * kTickMs;
            if (key.first == level && npc_index < npc_states.size()) {
                const auto on_ticks = op_ticks(npc);
                const std::uint32_t on_ms = on_ticks < 0 ? 0 : when_ms + std::uint32_t(on_ticks) * kTickMs;
                const bool opening = on_ticks < 0 || now_ms < on_ms;
                const std::string_view mode = back ? std::string_view{} : opening ? "OP" : "ON";
                // Its footprint in the new mode (ON: FUN_00623830 when HasCollision2 is off; jugs q4 / q5, xj).
                // ponytail: on this level's tick only; one left mid-OP keeps OP's until it's back in view.
                if (npc_states[npc_index].mode != mode) set_footprint(*level, npc, npc.collision >> mode_index(mode.empty() ? npc.mode : mode) & 1);
                npc_states[npc_index].mode = mode;
                npc_states[npc_index].mode_ms = back ? now_ms : opening ? when_ms : on_ms;   // its animation runs from the mode's start
                // Its message overhead (ShrMsgN, 3683 + row: FUN_00661110 on
                // "%d", 4 chars x 8 + 125 frames).
                npc_states[npc_index].says = std::uint16_t(npc.operate_fn == d2d::rules::operate_fn::kShrine && !back && now_ms - when_ms < 157u * kTickMs ? 3683 + npc.shrine : 0);
            }
            entry = back ? operated.erase(entry) : std::next(entry);
        }
        for (const auto& [key, door] : doors)                  // doors in the mode they were left in
            if (key.first == level && std::size_t(key.second) < npc_states.size())
                std::tie(npc_states[std::size_t(key.second)].mode, npc_states[std::size_t(key.second)].mode_ms) = std::pair{ kObjectModes[std::size_t(door.mode)], door.when };
        // The Rogue camp's bonfire (and three sound emitters): the client sets
        // EnvEffect objects NU by day, OP otherwise, at birth and each time the
        // day's type changes (FUN_004bc5e0 via FUN_004bc720 / FUN_004646c0).
        const bool daylight = (day.eclipse ? d2d::rules::kEclipse : d2d::rules::kDay)[std::size_t(day.phase)].type == 0;
        grow_states(npc_states, *level);
        for (std::size_t index = 0; index < level->npcs.size() && index < npc_states.size(); ++index)
            if (level->npcs[index].env_effect) npc_states[index].mode = daylight ? "NU" : "OP";
        Crowd crowd;                           // who's in whose way this frame
        for (const auto& player : players) {
            crowd.units.push_back(&player->player);
            if (player->merc) crowd.units.push_back(&player->merc->unit);
        }
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (level->npcs[i].root != "objects" && !npc_states[i].hidden) crowd.units.push_back(&npc_states[i]);
        const bool in_moor = !in_town(level);     // outside: this level's monsters are about
        if (in_moor) fight.crowd(crowd);       // the monsters around the player
        // Dead: the death plays out, then Resurrect respawns in camp; a
        // swing or a flinch holds the player in place until it ends, and
        // an attack goes on while the button's held (the left skill sent
        // again).
        std::vector<std::pair<float, float>> before;    // where each player stood (the leash)
        for (const auto& player : players) before.emplace_back(player->player.x, player->player.y);
        each_player([&](Player& player) {
            // Local co-op: players walk through each other (one screen; a
            // partner in the way would wall them in). Monsters still meet every player.
            Crowd walk_crowd = crowd;
            std::erase_if(walk_crowd.units, [&](const UnitState* unit) {
                return unit != &player.player && std::ranges::any_of(players, [&](const auto& other) { return &other->player == unit; });
            });
            std::vector<Command> cmds;                  // this player's
            for (const auto& order : orders) if (order.player == player.id) cmds.push_back(order.command);
            const bool button = std::ranges::any_of(cmds, [](const Command& command) {
                const auto* use_skill = std::get_if<cmd::UseSkill>(&command);
                return std::holds_alternative<cmd::Move>(command) || (use_skill && use_skill->left);
            });
            const bool resurrect = std::ranges::any_of(cmds, [](const Command& command) { return std::holds_alternative<cmd::Resurrect>(command); });
            // Back in camp after dying: alone, or in co-op once no one's left alive.
            if (fight.player_modes(button, now_ms, elapsed) && resurrect && (players.size() == 1 || !anyone_alive())) respawn(now_ms);
            if (actor->pmode != actor->last_pmode) {                 // death's stages: the penalty as it starts, the corpse as it ends
                if (actor->pmode == kModeDT) death_penalty(now_ms);
                if (actor->pmode == kModeDD) { if (players.size() > 1) become_ghost(player); else make_corpse(); }   // co-op: a ghost, the gear kept
                actor->last_pmode = actor->pmode;
            }
            if (!fight.dead() || actor->ghost) {
            const bool busy = actor->pmode >= 0 && !actor->ghost;
            for (const auto& command : cmds) {
                if (std::holds_alternative<cmd::Resurrect>(command)) continue;
                if (actor->ghost && !std::holds_alternative<cmd::Move>(command) && !std::holds_alternative<cmd::Run>(command)) continue;   // a ghost only walks
                apply(command, now_ms);
            }
            // Close enough to the stash: open it with the inventory.
            // ponytail: 2 cells, not D2's per-object operate range.
            if (actor->interact_npc >= 0) {
                const auto& npc = level->npcs[std::size_t(actor->interact_npc)];
                const auto& state = npc_states[std::size_t(actor->interact_npc)];
                const float npc_x = npc.path.empty() ? npc.x : state.x, npc_y = npc.path.empty() ? npc.y : state.y;
                if (std::hypot(npc_x - actor->player.x, npc_y - actor->player.y) < 2.f) {
                    if (operable(npc.operate_fn) || npc.operate_fn == d2d::rules::operate_fn::kMoldyTome || npc.operate_fn == d2d::rules::ToolsQuest::kStand || quest_object(npc.operate_fn)) {
                        credit_party([&] { operate(actor->interact_npc, now_ms); });
                    } else if (npc.operate_fn == d2d::rules::operate_fn::kStash) {
                        actor->events.push_back(ev::OpenUI{ ev::OpenUI::stash, actor->interact_npc });
                    } else if (npc.operate_fn == d2d::rules::operate_fn::kWaypoint) {
                        // FUN_00584e30: touching activates this level's waypoint
                        // (the town's, index 0, always is); an unlit one lights
                        // up (mode 1 → 2) and the panel waits for the next touch.
                        set_waypoint(0);
                        set_waypoint(waypoint_index(*game_data, level->id));
                        if (npc.mode == "NU" && !operated.contains({ level, actor->interact_npc })) operated[{ level, actor->interact_npc }] = now_ms;
                        else actor->events.push_back(ev::OpenUI{ ev::OpenUI::waypoint, actor->interact_npc });
                    } else {
                        if (d2d::rules::is_healer(npc.hc_idx)) { d2d::rules::heal(actor->character.stats); fight.cure(now_ms); }
                        // In Hell, a Den of Evil done before the reset existed
                        // opens it on meeting Akara (FUN_0058fd20: quest 41 bits
                        // 13 and 1).
                        if (npc.hc_idx == d2d::rules::monster_ids::kAkara && actor->character.header.active_difficulty() == 2) {
                            auto& quest_bits = quests();
                            namespace quest = d2d::rules::quest;
                            constexpr int kRespec = d2d::rules::kRespecQuest;
                            if (quest::bit(quest_bits, d2d::rules::DenQuest::kQuest, 0) && !quest::bit(quest_bits, kRespec, 1) && !quest::bit(quest_bits, kRespec, 0)) { d2d::rules::quest::set(quest_bits, kRespec, 13); d2d::rules::quest::set(quest_bits, kRespec, 1); }
                        }
                        auto lines = quest_talk(npc.hc_idx);
                        // Its greeting as the menu opens (FUN_004b66b0, kind 0), unless it has news to tell.
                        // ponytail: kind 2's "return" (the talk record's +0x12 flag) isn't traced.
                        if (!std::ranges::any_of(lines, &d2d::rules::QuestMsg::greet)) cues.cue(greet_sound(npc, 0), now_ms, actor->player.x, actor->player.y);
                        actor->events.push_back(ev::OpenUI{ ev::OpenUI::talk, actor->interact_npc, std::move(lines) });
                    }
                    actor->player.walking = false; actor->interact_npc = -1;
                } else if (!actor->player.walking) {
                    actor->interact_npc = -1;                         // blocked on the way
                } else {
                    actor->target_x = npc_x; actor->target_y = npc_y;              // follow a walking NPC
                }
            }
            if (actor->pick_item >= 0 && loot.index_of(actor->pick_item) < 0) actor->pick_item = -1;   // someone took it
            if (actor->pick_item >= 0 && !busy) {
                const auto ground_index = std::size_t(loot.index_of(actor->pick_item));
                const auto& ground_item = loot.ground[ground_index];
                if (std::hypot(ground_item.x - actor->player.x, ground_item.y - actor->player.y) <= 1.f) {
                    const bool malus = ground_item.item.code == "hdm";
                    loot.take(ground_index);
                    if (malus && holding_malus() && tools.picked_up(quests())) d2d::log::info("Tools of the Trade: the malus picked up (the player's line, FUN_00553380)");
                    actor->pick_item = -1; actor->player.walking = false; actor->player.path.clear();
                } else {
                    actor->target_x = ground_item.x; actor->target_y = ground_item.y; actor->player.walking = true;
                }
            }
            if (autoloot_gold && !fight.dead() && !actor->ghost)  // d2d: walking over gold picks it up (game.exe wants a click)
                for (std::size_t i = loot.ground.size(); i-- > 0;) {
                    auto& ground_item = loot.ground[i];
                    if (ground_item.item.code != "gld") continue;
                    const bool underfoot = std::hypot(ground_item.x - actor->player.x, ground_item.y - actor->player.y) <= 0.5f;
                    if (ground_item.dropped_by == actor->id) { if (!underfoot) ground_item.dropped_by = 0; continue; }
                    if (underfoot) {
                        if (loot.index_of(actor->pick_item) == int(i)) actor->pick_item = -1;
                        loot.take(i);
                    }
                }
            bool to_unit = actor->interact_npc >= 0 && std::size_t(actor->interact_npc) < level->npcs.size() && level->npcs[std::size_t(actor->interact_npc)].root == "monsters";
            if (const auto target = fight.engage(now_ms)) { std::tie(actor->target_x, actor->target_y) = *target; actor->player.walking = true; to_unit = true; }
            if (actor->player.walking && (actor->pmode < 0 || actor->ghost)) {
                // A route to the target (path type 7), re-planned when the
                // target moves off its end (dragging, a walking NPC) and when
                // it ran out short of it (FUN_006503f0's last test).
                // ponytail: a target unit re-paths at 0.3 cells, not game.exe's
                // 5 subtiles off SP2.
                const auto subtile = [](float value) { return int(std::floor(value * 5)); };
                if (actor->player.path.empty() || std::hypot(actor->player.goal_x - actor->target_x, actor->player.goal_y - actor->target_y) > 0.3f) {
                    actor->player.path = player_walk(*level, actor->player.x, actor->player.y, actor->target_x, actor->target_y, to_unit, walk_crowd, &actor->player);
                    actor->player.goal_x = actor->target_x; actor->player.goal_y = actor->target_y;
                }
                const auto save_class = std::size_t(std::max(actor->character.character_class, 0));
                // FUN_00623f50: the walk velocity x a % (at least 25) of 100 (run: run x 100 / walk),
                // faster run/walk's effective % (150 x v / (150 + v)) and chill's -50.
                const int walk = std::max(game_data->walk_velocity[save_class], 1);
                const float vel = float(walk * walk_percent(*actor, now_ms)) / 100.f;
                const float before_x = actor->player.x, before_y = actor->player.y;
                actor->player.walking = follow_path(*level, actor->player, cells_per_sec(vel) * elapsed, walk_crowd);
                // The merc's think reads the player's footsteps (FUN_00580c20): walking or
                // running, 25 ms on from the last, over 45 (squared) from it.
                if (const int step_x = int(std::floor(actor->player.x * 5)), step_y = int(std::floor(actor->player.y * 5)); now_ms > actor->footstep_ms + 25) {
                    const auto [last_x, last_y] = actor->footsteps[std::size_t(actor->footstep_cursor == 0 ? 0x13 : actor->footstep_cursor - 1)];
                    if ((step_x - last_x) * (step_x - last_x) + (step_y - last_y) * (step_y - last_y) > 0x2d) fight.footstep(step_x, step_y, now_ms);
                }
                if (!actor->player.walking) {
                    actor->player.path.clear();
                    // Short of the target with a step made: it paths again from here.
                    actor->player.walking = (subtile(actor->player.x) != subtile(actor->target_x) || subtile(actor->player.y) != subtile(actor->target_y))
                                     && (actor->player.x != before_x || actor->player.y != before_y);
                }
            }
            }
        });
        leash(before);
        // The world goes on with the player dead (game.exe runs the rooms
        // around the corpse till the respawn): monsters see the dead player
        // as not alive (Fight::world) and think on.
        std::array<int, 3> talking{ -1, -1, -1 };    // the NPCs a player has a menu, speech or store open with
        for (const auto& player : players)
            for (std::size_t k = 0; k < talking.size(); ++k) if (player->talking[k] >= 0) talking[k] = player->talking[k];
        // ponytail: an NPC with news greets the first player.
        npc_patrol(*game_data, *level, npc_states, talking, now_ms, elapsed, crowd, &lead().player, arcane.guard_state == 2);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (std::exchange(npc_states[i].greets, false)) cues.cue(greet_sound(level->npcs[i], 1), now_ms, lead().player.x, lead().player.y);
        cain_step(now_ms, elapsed);
        for (const auto& neighbour : level->nearby) {           // over the edge, still in play
            auto& states = other_npcs[neighbour.level];
            if (states.size() > neighbour.level->npcs.size()) states = npc_start(*neighbour.level);   // a new game's
            grow_states(states, *neighbour.level);
            npc_patrol(*game_data, *neighbour.level, states, { -1, -1, -1 }, now_ms, elapsed, Crowd{});
        }
        // The "!" (FUN_00544590, from the NPC's think): only the NPC's own act's
        // quests are asked, and objects have no think.
        using ActAlert = bool (World::*)(int);
        static constexpr std::array<ActAlert, 5> kActAlerts{ &World::act1_alert, &World::act2_alert, &World::act3_alert, &World::act4_alert, &World::act5_alert };
        const ActAlert act_alert = kActAlerts[std::size_t(act())];
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            npc_states[i].alert = level->npcs[i].root != "objects" && (this->*act_alert)(level->npcs[i].hc_idx);
        // The player's mode as the merc's think reads it: 2 walk, 3 run, 6 walk in town.
        for (auto& player : players)
            player->owner_mode = !player->player.walking ? 1 : player->running && player->character.stats.values[d2d::d2s::kStamina] > 0 ? 3 : in_moor ? 2 : 6;
        fight.world(in_moor, now_ms, elapsed, crowd);
        credit_party([&] { den_count(now_ms); });                // the merc's or a missile's kill counts with the player dead
        if (den_log_at && now_ms >= den_log_at) { den.log = 5; den_log_at = 0; }
        each_player([&](Player&) {
            if (!fight.dead()) {
                use_warp();
                if (actor->portal_due) { actor->portal_due = false; open_portal(now_ms); }
                if (actor->take_corpse >= 0 && std::size_t(actor->take_corpse) < actor->corpses.size() && actor->corpses[std::size_t(actor->take_corpse)].level == level) {
                    const auto& corpse = actor->corpses[std::size_t(actor->take_corpse)];
                    if (std::hypot(corpse.x - actor->player.x, corpse.y - actor->player.y) < 2.f) { take_corpse_items(std::size_t(actor->take_corpse), now_ms); actor->take_corpse = -1; }
                    else if (!actor->player.walking) actor->take_corpse = -1;
                }
                use_portal(now_ms);
            }
        });
        for (const auto& kill : hosted_quests ? std::vector<Fight::Kill>{} : fight.kills) {
            const auto killer = std::ranges::find(players, kill.killer, [](const auto& player) { return player->id; });
            act_as(killer == players.end() ? lead() : **killer);   // the hooks read the killer's quests
            credit_party([&] {
                act1_kill(kill, now_ms);
                act2_kill(kill, now_ms);
                act3_kill(kill, now_ms);
                act4_kill(kill, now_ms);
                act5_kill(kill, now_ms);
            });
        }
        act_as(lead());
        fight.kills.clear();
        credit_party([&] {
            act2_update();
            act3_update(now_ms);
            act4_update(now_ms);
            act5_update(now_ms);
        });
        each_player([&](Player&) { cross_level(); });
        for (const auto& player : players) fight.rooms_up(*level, player->player.x, player->player.y, false);
        act3_bind_new_bosses();
        reopen_doors();
        grow_states(npc_states, *level);
        stones_up();
        each_player([&](Player&) { if (!fight.dead()) fight.apply_regen(now_ms, last_ms); });
        raise_step(elapsed, now_ms);
        if (in_town(level))                                      // the party in a town: its ghosts are up again
            each_player([&](Player& player) { if (player.ghost) revive(player, now_ms); });
    }

}  // namespace d2d::game
