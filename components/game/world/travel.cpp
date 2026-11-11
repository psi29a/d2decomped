// SPDX-License-Identifier: GPL-3.0-or-later
// The World (world.hpp): levels, warps, arrival, death, corpses and portals. The rest is in
// this directory.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "item_text.hpp"
#include "log.hpp"

#include <d2s_items.hpp>
#include <drlg.hpp>
#include <level_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>

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
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::game {

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
            if (split_levels) {                          // stage 9: this player crosses (with the host's screen's party, if theirs); that level has its own turn
                Player* const mover = actor;
                each_player([&](Player& player) {
                    if (&player != mover && (mover->own_screen || player.own_screen)) return;
                    shift(player.player);
                    player.target_x -= dx; player.target_y -= dy;
                    if (player.merc) shift(player.merc->unit);
                    fight.pets_cross(level, neighbour.level, dx, dy);
                    player.events.push_back(ev::LevelChanged{ level, true });
                    player.interact_npc = -1;
                    player.attack_mon = player.merc_target = -1;   // the monsters stay with the level left (Fight::enter)
                    for (auto& pet : player.pets) pet.target = -1;
                    act1_enter(*level); act2_enter(*level); act3_enter(*level); act4_enter(*level); act5_enter(*level);
                    player.on_level = neighbour.level;
                    d2d::log::info("{}: level {} at ({:.1f}, {:.1f})", player.character.name, level_name(*neighbour.level), player.player.x, player.player.y);
                });
                return;
            }
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
            leave_town(from);
            swap_npcs(from);
            d2d::log::info("level: {} at ({:.1f}, {:.1f})", level_name(*level), actor->player.x, actor->player.y);
            return;
        }
    }

// Stage 9: the acting player leaves for `destination` alone, mid turn; the
// level stays in for the others on it, and theirs comes in on its own turn
// (swap_to). What arrive does for the level itself (its rooms, objects,
// doors) happens there and in that turn's tick.
auto World::go_alone(const Level* destination, float arrive_x, float arrive_y, const char* how) -> void {
        Player* const mover = actor;
        const Level* from = mover->on_level ? mover->on_level : level;   // theirs (a debug goto acts between turns)
        // A player with a screen of their own goes alone; the host's screen's
        // party goes together (one screen, as bring_party), round the mover.
        std::vector<Player*> travellers{ mover };
        if (!mover->own_screen)
            for (auto& player : players) if (player.get() != mover && !player->own_screen && here(*player)) travellers.push_back(player.get());
        static constexpr std::array<std::pair<float, float>, 4> kAround{ { { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f }, { 0.f, -1.f } } };
        std::size_t around = 0;
        for (Player* traveller : travellers) {
            act_as(*traveller);
            const auto [near_x, near_y] = traveller == mover ? std::pair{ arrive_x, arrive_y } : std::pair{ arrive_x + kAround[around % 4].first, arrive_y + kAround[around % 4].second };
            if (traveller != mover) ++around;
            const auto [free_x, free_y] = destination->nearest_free(near_x, near_y);
            fight.pets_cross(from, destination, actor->player.x - free_x, actor->player.y - free_y);
            actor->events.push_back(ev::LevelChanged{ from, true });
            act1_enter(*from); act2_enter(*from); act3_enter(*from); act4_enter(*from); act5_enter(*from);   // swap_npcs' hooks, theirs alone
            if (const int to_act = d2d::rules::level_ids::act_of(destination->id); to_act != d2d::rules::level_ids::act_of(from->id)) set_act(to_act);
            actor->on_level = destination;
            actor->player.x = free_x; actor->player.y = free_y;
            actor->player.walking = false; actor->player.path.clear();
            actor->target_x = free_x; actor->target_y = free_y;
            if (actor->merc) {
                actor->merc->unit.path.clear();
                std::tie(actor->merc->unit.x, actor->merc->unit.y) = destination->nearest_free(free_x + 1, free_y + 1);
            }
            actor->warp_spot = { int(std::floor(free_x * 5)), int(std::floor(free_y * 5)) };
            actor->interact_npc = actor->pick_item = actor->take_warp = actor->take_portal = -1;
            actor->attack_mon = actor->merc_target = -1;     // the level's monsters stay behind (Fight::enter): an index into another level's isn't a target
            for (auto& pet : actor->pets) pet.target = -1;
            actor->store = {};                               // a trip ends the trade (leave_town); the town's stock stays for those still in it
            if (destination->id == d2d::rules::DenQuest::kDen) den.enter_den(quests());
            d2d::log::info("{}: level {} at ({:.1f}, {:.1f}), through {} from {}", actor->character.name, level_name(*destination), free_x, free_y, how, level_name(*from));
        }
        act_as(*mover);
    }

// Stage 9: `destination`'s turn: its NPCs, monsters, missiles and floor in,
// the last level's kept (no one's level change: nobody moved).
auto World::swap_to(const Level* destination) -> void {
        if (destination == level) return;
        const Level* from = level;
        level = destination;
        fight.take_turn(level);
        loot.enter(level);
        swap_npc_states(from);
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
        actor->attack_charge = {};                    // the host spends the charge; this swing is for show
        actor->interact_npc = actor->pick_item = actor->take_warp = -1;
        actor->player.walking = false;
        actor->player.dir = direction16(x - actor->player.x, y - actor->player.y);
        fight.start_swing(now_ms, true);
    }

auto World::use_warp() -> void {
        if (actor->take_warp < 0 || std::size_t(actor->take_warp) >= level->warps.size()) return;
        const auto warp = level->warps[std::size_t(actor->take_warp)];
        if (!warp_close(warp, actor->player.x, actor->player.y)) {
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
        if (split_levels) { go_alone(destination, arrive_x, arrive_y, how); return; }
        const auto [free_x, free_y] = destination->nearest_free(arrive_x, arrive_y);
        const float dx = actor->player.x - free_x, dy = actor->player.y - free_y;
        fight.pets_cross(level, destination, dx, dy);
        actor->events.push_back(ev::LevelChanged{ level, true });
        const Level* from = level;
        const int from_act = act();
        level = destination;
        leave_town(from);
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
        init_new_objects();
        reopen_doors();
        std::tie(actor->player.x, actor->player.y) = level->nearest_free(actor->player.x, actor->player.y);   // off what the room just made
        loot.enter(level);
        swap_npcs(from);
        actor->interact_npc = actor->pick_item = actor->take_warp = actor->take_portal = -1;
        if (level->id == d2d::rules::DenQuest::kDen) den.enter_den(quests());
        d2d::log::info("level: {} at ({:.1f}, {:.1f}), through {} from {}", level_name(*level), free_x, free_y, how, level_name(*from));
    }

// The party left `from` (FUN_005380d0 -> FUN_00537340: from one of the five
// towns, none left in it, FUN_00537230's last argument set): each vendor a
// trade has stocked (its record's +0x1c and +0x20) loses its stock
// (FUN_00536d10 -> FUN_00536580), rolled anew at the next opening. The
// party moves together, so the town is always left empty: the 4-minute
// restock (+0x27, players still in town) doesn't come up. A store still
// open ends with the trip (FUN_00555500, an interaction cut short), so no
// later deal saves the old stock back.
auto World::leave_town(const Level* from) -> void {
        if (!in_town(from) || from == level) return;
        for (auto& player : players) player->store = {};
        stocks.clear();
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
        if (purse - lost > 0 && !fight.remote_monsters) {     // a joined game: the host drops it (0x9c)
            const auto before = loot.ground.size();
            loot.put({ .code = "gld", .gold = int(purse - lost) }, actor->player.x, actor->player.y, 1, fight.spawning.game, now_ms);
            if (loot.ground.size() > before) loot.ground.back().owner = actor->id;   // FUN_00535510: the player's own
        }
        stat_values[kGold] = 0;
        actor->gold_lost = lost;
        d2d::log::info("died: {} experience and {} gold lost; {} gold on the ground", actor->exp_lost, lost, purse - lost);
    }

auto World::make_corpse() -> void {
        Corpse corpse{ level, actor->player.x, actor->player.y, actor->player.dir, {}, actor->exp_lost * 75 / 100, fight.gfx() };
        for (auto item_it = actor->character.items.begin(); item_it != actor->character.items.end();)
            if (item_it->location == d2d::d2s::item_location::kEquipped) { corpse.items.push_back(std::move(*item_it)); item_it = actor->character.items.erase(item_it); }
            else ++item_it;
        // The item in hand goes first (FUN_0057f700's -1 pass), into the
        // corpse's own inventory grid (FUN_0063b950 on its inventory page,
        // mode 0): stored, at the empty grid's first cell.
        if (actor->held) {
            auto& stored = *corpse.items.insert(corpse.items.begin(), std::move(*actor->held));
            stored.location = d2d::d2s::item_location::kStored; stored.panel = d2d::d2s::item_panel::kInventory;
            stored.slot = 0; stored.column = 0; stored.row = 0;
            actor->held.reset();
        }
        if (actor->corpses.size() >= 16) actor->corpses.erase(actor->corpses.begin());
        actor->corpses.push_back(std::move(corpse));
    }

auto World::take_corpse_items(std::size_t corpse_index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& corpse = actor->corpses[corpse_index];
        actor->character.stats.values[kExp] += corpse.exp;
        corpse.exp = 0;
        const auto& layout = game_data->inv_layout[std::size_t(actor->character.header.cls % 7)];
        std::ranges::stable_sort(corpse.items, {}, [](const Item& item) { return item.location == d2d::d2s::item_location::kEquipped ? item.slot : 100; });
        for (auto item_it = corpse.items.begin(); item_it != corpse.items.end();) {
            namespace body = d2d::d2s::body_location;
            const auto worn_in = [&](int slot) { return std::ranges::find_if(actor->character.items, [&](const Item& worn_item) { return worn_item.location == d2d::d2s::item_location::kEquipped && worn_item.slot == slot; }); };
            const int slot = item_it->slot, base_slot = slot >= body::kRightArmSwitch ? slot - (body::kRightArmSwitch - body::kRightArm) : slot;
            const auto worn_by = wearer(*game_data, actor->character.header.cls, actor->character.items, actor->character.stats);
            // FUN_00562f30: its requirements (FUN_0062eaf0), then FUN_0055f2d0: its slot,
            // or the other side's when only its own is taken, paired with the other hand's.
            const auto other = d2d::rules::other_side(slot);
            const bool taken = worn_in(slot) != actor->character.items.end(), other_taken = other && worn_in(other) != actor->character.items.end();
            const int to_slot = d2d::rules::corpse_slot(slot, taken, other_taken);
            bool fits = slot >= body::kFirst && slot <= body::kLeftArmSwitch && to_slot > 0
                        && d2d::rules::can_wear(game_data->rules, *item_it, base_slot, worn_by, required_level(*game_data, *item_it, worn_by.cls));
            if (fits && (base_slot == body::kRightArm || base_slot == body::kLeftArm))
                if (const auto paired = worn_in(d2d::rules::other_side(to_slot)); paired != actor->character.items.end()) fits = d2d::rules::hands_ok(game_data->rules, *item_it, *paired, worn_by);
            if (fits) {
                item_it->slot = to_slot;
                actor->character.items.push_back(std::move(*item_it));
                d2d::rules::wield_enters(game_data->rules, actor->character.items, actor->character.items.back());   // FUN_00562f30 -> FUN_0063d1d0
                item_it = corpse.items.erase(item_it);
                continue;
            }
            std::optional<Item> held_item = *item_it;
            held_item->location = d2d::d2s::item_location::kStored;
            std::vector<const Item*> inv;
            for (const auto& x : actor->character.items) if (x.location == d2d::d2s::item_location::kStored && x.panel == d2d::d2s::item_panel::kInventory) inv.push_back(&x);
            const auto [width, height] = d2d::rules::item_size(game_data->rules, held_item->code);
            if (const auto [column, row] = d2d::rules::free_spot(game_data->rules, inv, layout.cols, layout.rows, width, height, true); column >= 0
                && d2d::rules::put_in_grid(game_data->rules, actor->character.items, held_item, d2d::d2s::item_panel::kInventory, layout.cols, layout.rows, column, row)) { item_it = corpse.items.erase(item_it); continue; }
            ++item_it;
        }
        settle_requirements(*game_data, actor->character);   // FUN_00562f30 -> FUN_0055dbc0
        if (corpse.items.empty()) actor->corpses.erase(actor->corpses.begin() + std::ptrdiff_t(corpse_index));
        cues.cue("item_pickup", now_ms, actor->player.x, actor->player.y);
    }

auto World::read_portal(std::vector<d2d::d2s::Item>::iterator scroll, std::uint32_t now_ms) -> void {
        // FUN_005be290 opens none in town nor in Uber Tristram (level 0x88),
        // FUN_0057eec0's mode gate none in state 0x36; held: FUN_0055e170's
        // FUN_0063c1e0.
        if (in_town(level) || (level && level->id == d2d::rules::level_ids::kUberTristram) || fight.dead() || fight.uninterruptable() || actor->held) return;
        const bool book = scroll->code == "tbk";
        if (book && scroll->quantity <= 0) return;
        close_portal_pair(std::size_t(actor->portal_pair));   // FUN_00535430, before the new one
        if (!open_portal_at(actor->player.x, actor->player.y, now_ms)) return;
        if (level->id == d2d::rules::RiteQuest::kSummit && rite.portal_opened()) rite_put_back(now_ms);   // FUN_0058cf00
        if (book) --scroll->quantity;
        else actor->character.items.erase(scroll);
        cues.cue_on(unit_key(kSoundOnPlayer, actor->id), "player_townportal_cast", now_ms, actor->player.x, actor->player.y);
    }

auto World::open_portal(std::uint32_t now_ms) -> void { open_portal_at(actor->player.x, actor->player.y, now_ms); }

auto World::close_portal_pair(std::size_t pair) -> void {
        for (const std::size_t end : { pair, pair + 1 })
            if (portal[end].level && portal[end].level->id == d2d::rules::RiteQuest::kSummit) rite.portal_closed();
        // Both ends are freed (FUN_00555600): a walk queued to either has lost its target (LAB_00481697).
        for (const auto& player : players)
            if (player->take_portal == int(pair) || player->take_portal == int(pair + 1)) player->take_portal = -1;
        portal[pair] = portal[pair + 1] = {};
    }

auto World::crowd_now() -> Crowd {
        Crowd crowd;
        for (const auto& player : players) {
            crowd.units.push_back(&player->player);
            if (player->merc) crowd.units.push_back(&player->merc->unit);
            if (player.get() != actor)                                  // the actor's pets come with fight.crowd
                for (const auto& pet : player->pets)
                    if (pet.monster.alive() && pet.where == level) crowd.units.push_back(&pet.monster.unit);
        }
        if (fight.mon_level == level) fight.crowd(crowd);
        return crowd;
    }

auto World::portal_spot(const Level& where, float x, float y) -> std::optional<std::pair<float, float>> {
        const int width = where.ds1.width() * 5, height = where.ds1.height() * 5;
        if (where.walk.empty()) return std::pair{ x, y };
        const Crowd crowd = &where == level ? crowd_now() : Crowd{};
        static constexpr UnitShape kProbe{ 0, 1, 0x3000 };
        const int from_x = int(std::floor(x * 5)), from_y = int(std::floor(y * 5));
        const auto spot = field_spot(game_data->field, from_x, from_y, from_x, from_y, 3, 0x3e01, 0xc01, [&](int at_x, int at_y) {
            if (at_x < 0 || at_y < 0 || at_x >= width || at_y >= height) return 0x27;
            int flags = where.walk[std::size_t(at_y) * std::size_t(width) + std::size_t(at_x)] | loot.items_at(&where, at_x, at_y);
            if (crowd.at((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, kProbe, nullptr)) flags |= 0x1000;
            return flags;
        });
        if (!spot) return std::nullopt;
        return std::pair{ (float(spot->first) + 0.5f) / 5, (float(spot->second) + 0.5f) / 5 };
    }

auto World::open_portal_at(float portal_x, float portal_y, std::uint32_t now_ms, int pair) -> bool {
        const Level* act_town = game_data->town_of(level->id);
        if (in_town(level) || !act_town || act_town->portal_spot.first < 0) return false;
        const auto& town = *act_town;
        const auto by_caster = portal_spot(*level, portal_x, portal_y);
        if (!by_caster) { d2d::log::info("town portal: no room by ({:.1f}, {:.1f})", portal_x, portal_y); return false; }
        const auto [spot_x, spot_y] = *by_caster;
        const auto [town_x, town_y] = town.nearest_free(town.portal_spot.first, town.portal_spot.second);
        const std::size_t here = pair < 0 ? actor->portal_pair : std::size_t(pair);
        portal[here] = { level, spot_x, spot_y, now_ms };
        portal[here + 1] = { &town, town_x, town_y, now_ms };
        cues.cue("object_townportal", now_ms, spot_x, spot_y);
        d2d::log::info("town portal: {} ({:.1f}, {:.1f}) <-> {} ({:.1f}, {:.1f})", level_name(*level), spot_x, spot_y, level_name(town), town_x, town_y);
        return true;
    }

auto World::use_portal(std::uint32_t now_ms) -> void {
        if (actor->take_portal < 0 || !portal[std::size_t(actor->take_portal)].level) return;
        const auto& taken = portal[std::size_t(actor->take_portal)];
        if (taken.level != level || std::hypot(taken.x - actor->player.x, taken.y - actor->player.y) > 2.f) {
            if (!actor->player.walking) actor->take_portal = -1;
            return;
        }
        if (host_warps) { actor->take_portal = -1; return; }   // joined: the host's portal moves us (it may refuse: a quest's level), its reassign brings us
        cues.cue_on(unit_key(kSoundOnPlayer, actor->id), "player_townportal_enter", now_ms, actor->player.x, actor->player.y);
        if (actor->take_portal == 2) {                       // to Tristram, by its own portal back (object 60)
            actor->take_portal = -1;
            const Level* tristram = game_data->level(d2d::rules::CainQuest::kTristram);
            if (!tristram || tristram->ds1.width() == 0) { d2d::log::info("not implemented: level 38 (the Cairn Stones' portal)"); return; }
            // Its twin (object 60, FUN_0056d130 -> FUN_0056cf40): at Tristram's type 11 spawn
            // tile (FUN_0061b060, + 3 subtiles; it has one, drlg.md "Spawn tiles and the start
            // spot"), twin_spot round it; the map's centre if d2d found none.
            if (portal[3].level != tristram) {
                float back_x = float(tristram->ds1.width()) / 2, back_y = float(tristram->ds1.height()) / 2;
                if (!tristram->starts.empty()) {
                    const int width = tristram->ds1.width() * 5, height = tristram->ds1.height() * 5;
                    const auto flags = [&](int sub_x, int sub_y) -> int {
                        if (sub_x < 0 || sub_y < 0 || sub_x >= width || sub_y >= height || tristram->walk.empty()) return 0x27;
                        return tristram->walk[std::size_t(sub_y) * std::size_t(width) + std::size_t(sub_x)];
                    };
                    const auto [spot_x, spot_y] = twin_spot(tristram->starts.front().first * 5 + 3, tristram->starts.front().second * 5 + 3, flags);
                    back_x = (float(spot_x) + 0.5f) / 5; back_y = (float(spot_y) + 0.5f) / 5;
                }
                portal[3] = { tristram, back_x, back_y, now_ms };
            }
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
        if (arrival == actor->portal_pair) close_portal_pair(arrival);
        arrive(other_end.level, other_end.x, other_end.y + 0.6f, "a town portal");
    }

}  // namespace d2d::game
