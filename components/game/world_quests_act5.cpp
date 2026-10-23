// SPDX-License-Identifier: GPL-3.0-or-later
// Act 5's quests at the World's quest hooks (world.hpp: World::actN_*;
// rules/quests_act5.hpp, docs/research/re/quests-act5.md).
#include "world.hpp"
#include "gamedata.hpp"
#include "log.hpp"
#include <level_ids.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {
constexpr int kCage = 473;                    // cagedwussie1 (InitFn 62)
int subtile(float cells) { return int(std::floor(cells * 5)); }
// FUN_00619790's list (room2 +8): the player's room is the dead one's or on
// its near list.
bool rooms_near(const GameData& game_data, const Level& where, float x, float y, float dead_x, float dead_y) {
    const int room = room_of(where, x, y);
    return room >= 0 && std::ranges::contains(near_rooms(game_data, where, dead_x, dead_y), std::pair{ &where, room });
}
}  // namespace

auto World::act5_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const -> void {
        log[d2d::rules::SiegeQuest::kQuest] = std::uint8_t(siege.log_state(bits));
        log[d2d::rules::RescueQuest::kQuest] = std::uint8_t(rescue.log_state(bits));
        log[d2d::rules::PrisonQuest::kQuest] = std::uint8_t(prison.log_state(bits, carries(d2d::rules::PrisonQuest::kPotionCode)));
        log[d2d::rules::BetrayalQuest::kQuest] = std::uint8_t(betrayal.log_state(bits));
    }

auto World::act5_join() -> void {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        siege = {};
        siege.join(quests(), d2d::rules::Act5Opening::heard(actor->character.header.intros[std::size_t(diff)], d2d::rules::Act5Opening::kMalah));
        rescue = {};
        rescue.join(quests());
        prison = {};
        prison.join(quests(), carries(d2d::rules::PrisonQuest::kPotionCode));
        betrayal = {};
        betrayal.join(quests());
        betrayal_log_at = 0;
        cages_up.clear();
        act5_chain();
    }

// Each record's +0xf0 (a5q1 FUN_00587560 -> a5q2 FUN_005883e0 -> a5q3
// FUN_00589160 -> a5q4 FUN_0058b140): a quest done or shut opens the next;
// a5q4 passes on from state 4 (Nihlathak dead).
auto World::act5_chain() -> void {
        if (siege.open && siege.state != 5) return;
        rescue.opened();
        if (rescue.open && rescue.state != 5) return;
        prison.opened();
        if (prison.open && prison.state != 5) return;
        betrayal.opened();
    }

auto World::act5_enter(const Level& from) -> void {
        siege.enter(quests(), from.id, level->id);
        rescue.enter(quests(), from.id, level->id);
        prison.enter(quests(), level->id);
        betrayal.enter(quests(), from.id, level->id);
    }

auto World::act5_talk_closed(int hc_idx) -> void {
        siege.talk_closed(hc_idx);
        rescue.talk_closed(hc_idx);
        prison.talk_closed(hc_idx);
        // Anya's red portal to Nihlathak's Temple (FUN_0058af90: FUN_0056d130,
        // object 60 at her spot +10, +5 subtiles), its twin at the temple's
        // portal spot.
        if (betrayal.talk_closed(hc_idx)) {
            const auto anya = std::ranges::find(level->npcs, d2d::rules::BetrayalQuest::kAnya, &Npc::hc_idx);
            const Level* temple = game_data->level(d2d::rules::level_ids::kNihlathaksTemple);
            if (anya == level->npcs.end() || !temple || temple->ds1.width() == 0) { d2d::log::warn("Betrayal of Harrogath: no Anya or no temple; her portal stays shut"); return; }
            const auto [spot_x, spot_y] = level->nearest_free(anya->x + 10.f / 5, anya->y + 5.f / 5);
            portal[kTemplePortal] = { level, spot_x, spot_y, now };
            const auto [to_x, to_y] = temple->portal_spot.first >= 0 ? temple->nearest_free(temple->portal_spot.first, temple->portal_spot.second)
                                                                    : temple->nearest_free(float(temple->ds1.width()) / 2, float(temple->ds1.height()) / 2);
            portal[kTemplePortal + 1] = { temple, to_x, to_y, now };
            cues.cue("object_townportal", now, spot_x, spot_y);
            d2d::log::info("Betrayal of Harrogath: Anya opens the way to Nihlathak's Temple");
        }
    }

auto World::act5_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx) -> void {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        const bool potion = carries(d2d::rules::PrisonQuest::kPotionCode);
        std::ranges::copy(d2d::rules::Act5Opening::talk(actor->character.header.intros[std::size_t(diff)], hc_idx, actor->character.header.cls), std::back_inserter(out));
        std::ranges::copy(siege.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(rescue.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(prison.talk(quests(), hc_idx, potion), std::back_inserter(out));
        std::ranges::copy(betrayal.talk(quests(), hc_idx), std::back_inserter(out));
    }

auto World::act5_said(int hc_idx, int message_string) -> void {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        if (d2d::rules::Act5Opening::said(actor->character.header.intros[std::size_t(diff)], hc_idx, message_string)) siege.malah_heard();
        if (siege.said(quests(), hc_idx, message_string)) act5_chain();
        // Rescue on Mount Arreat (FUN_00587b00): Qual-Kehk's runes, one by one (FUN_005466b0).
        if (const auto heard = rescue.said(quests(), hc_idx, message_string); heard.runes || heard.chain) {
            for (int rune = 0; rune < heard.runes; ++rune) give_item(d2d::rules::RescueQuest::kRunes[std::size_t(rune)], now);
            if (heard.runes) d2d::log::info("Rescue on Mount Arreat: Qual-Kehk's {} runes", heard.runes);
            if (heard.chain) act5_chain();
        }
        const auto heard = prison.said(quests(), hc_idx, message_string, carries(d2d::rules::PrisonQuest::kPotionCode), carries(d2d::rules::PrisonQuest::kScrollCode));
        if (heard.give) { give_item(heard.give, now); d2d::log::info("Prison of Ice: Malah gives {}", heard.give); }
        if (betrayal.said(quests(), hc_idx, message_string)) act5_chain();
    }

auto World::act5_alert(int hc_idx) -> bool {
        const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        return d2d::rules::Act5Opening::alert(actor->character.header.intros[std::size_t(diff)], hc_idx) || siege.alert(quests(), hc_idx) || rescue.alert(quests(), hc_idx)
            || prison.alert(quests(), hc_idx, carries(d2d::rules::PrisonQuest::kPotionCode)) || betrayal.alert(quests(), hc_idx);
    }

auto World::act5_kill(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (hosted_quests) return;
        const bool about = rooms_near(*game_data, *level, actor->player.x, actor->player.y, kill.x, kill.y);
        // Shenk the Overseer (superunique 0x2a, bound to 0x1f: FUN_00587330).
        if (kill.super == d2d::rules::SiegeQuest::kShenk) {
            siege.killed(quests(), about);
            d2d::log::info("Siege on Harrogath: Shenk is dead");
        }
        // Rescue on Mount Arreat (FUN_00588040, units bound to 0x20): a door
        // broken frees the living prisoners within 15 of it; a prisoner dead.
        if (std::size_t(kill.type) < game_data->monsters.types.size()) {
            const int base = game_data->monsters.types[std::size_t(kill.type)].base;
            if (base == d2d::rules::RescueQuest::kDoor) {
                std::vector<int> near_ids;
                for (const auto& prisoner : fight.monsters)
                    if (prisoner.alive() && game_data->monsters.types[std::size_t(prisoner.type)].base == d2d::rules::RescueQuest::kPrisoner
                        && rescue.cage_of(prisoner.id) >= 0 && !std::ranges::contains(rescue.freed_ids, prisoner.id)
                        && d2d::rules::ai_distance(subtile(prisoner.unit.x) - subtile(kill.x), subtile(prisoner.unit.y) - subtile(kill.y)) < d2d::rules::RescueQuest::kFreeReach)   // FUN_006416d0
                        near_ids.push_back(prisoner.id);
                rescue.door_broken(quests(), near_ids);
                d2d::log::info("Rescue on Mount Arreat: a door broken, {} freed", near_ids.size());
            }
            if (base == d2d::rules::RescueQuest::kPrisoner && rescue.cage_of(kill.id) >= 0) {
                rescue.prisoner_died(quests(), kill.id);
                d2d::log::info("Rescue on Mount Arreat: a prisoner dead ({} caged dead)", rescue.dead);
            }
        }
        // Nihlathak (MonStats base 0x20e, bound to 0x22: FUN_0058b7a0).
        if (std::size_t(kill.type) < game_data->monsters.types.size() && game_data->monsters.types[std::size_t(kill.type)].base == d2d::rules::BetrayalQuest::kNihlathak) {
            if (betrayal.killed(quests(), about)) betrayal_log_at = now_ms + d2d::rules::BetrayalQuest::kLogTicks * kTickMs;
            d2d::log::info("Betrayal of Harrogath: Nihlathak is dead");
        }
    }

// a5q2's cages and prisoners. A cage's InitFn (62, FUN_005886a0) as its
// room comes up: its slot, its five prisoners (FUN_00588600, bound to
// 0x20). Each prisoner's view for its think (ai.cpp's Wussie), then what
// it asked: the log 5, its cage's portal (FUN_005888d0: at the broken
// door's spot +2 subtiles east, FUN_00555230 a town portal; its twin at
// Harrogath's portal spot), gone through it (FUN_00588880).
// ponytail: the prisoners at free spots about the cage, not FUN_005b2f20's
// draws on the room's seed; the portal's fallback spots (x - 2, then
// FUN_00545340) are the nearest free one.
auto World::rescue_update(std::uint32_t now_ms) -> void {
        const auto made_rooms = fight.spawning.levels.find(level);
        const bool rooms_known = made_rooms != fight.spawning.levels.end();
        for (int i = 0; i < int(level->npcs.size()); ++i) {
            const auto& cage = level->npcs[std::size_t(i)];
            if (cage.object_id != kCage || cages_up.contains({ level, i })) continue;
            const int room = room_of(*level, cage.x, cage.y);
            if (!rooms_known || room < 0 || !std::ranges::contains(made_rooms->second.order, std::size_t(room))) continue;
            const int slot = rescue.cage(subtile(cage.x), subtile(cage.y));
            cages_up[{ level, i }] = slot;
            if (slot < 0) continue;
            const int row = int(game_data->mon_bin[std::size_t(d2d::rules::RescueQuest::kPrisoner)]);
            for (int which = 0; which < d2d::rules::RescueQuest::kPerCage; ++which) {
                const auto [at_x, at_y] = level->nearest_free(cage.x + float(which % 3 - 1) * 0.6f, cage.y + float(which / 3) * 0.6f);
                fight.add_monster(make_monster(*game_data, row, at_x, at_y, fight.rng, actor->character.header.active_difficulty(), true, int(players.size())), level);
                rescue.prisoner_made(slot, which, fight.monsters.back().id);
            }
            d2d::log::info("Rescue on Mount Arreat: cage {} up, {} prisoners", slot, d2d::rules::RescueQuest::kPerCage);
        }
        const auto door_down = [&](float x, float y) {                  // FUN_00588e10: a dead prisondoor in the player's rooms
            const auto rooms = near_rooms(*game_data, *level, x, y);
            return std::ranges::any_of(fight.monsters, [&](const Monster& door) {
                return !door.alive() && game_data->monsters.types[std::size_t(door.type)].base == d2d::rules::RescueQuest::kDoor
                    && std::ranges::contains(rooms, std::pair{ level, room_of(*level, door.unit.x, door.unit.y) }); });
        };
        for (auto& prisoner : fight.monsters) {
            const int slot = rescue.cage_of(prisoner.id);
            if (slot < 0) continue;
            auto& view = prisoner.wussie;
            if (std::exchange(view.left, false)) { rescue.went(slot); d2d::log::info("Rescue on Mount Arreat: a prisoner through the portal"); }
            if (!prisoner.alive()) continue;
            const auto& gate = portal[kCagePortals + std::size_t(slot) * 2];
            view.portal = gate.level == level;
            if (view.portal) { view.portal_x = subtile(gate.x); view.portal_y = subtile(gate.y); }
            view.cage_left = rescue.cages[std::size_t(slot)].gone > 0;
            // FUN_005ddf20: the nearest player within 16 (FUN_005dc380).
            const Player* by = nullptr;
            int best = 16;
            for (const auto& other : players) {
                const int dist = d2d::rules::ai_distance(subtile(other->player.x) - subtile(prisoner.unit.x), subtile(other->player.y) - subtile(prisoner.unit.y));
                if (dist < best) { best = dist; by = other.get(); }
            }
            view.player = by != nullptr;
            view.door_broken = by && door_down(by->player.x, by->player.y);
            if (by) view.player_id = by->id;
            if (std::exchange(view.cried, false)) rescue.cried();
            if (std::exchange(view.open_portal, false) && rescue.portal_due(slot)) {
                // FUN_005888d0: the broken door within 15 of it, in the player's rooms.
                const Monster* door = nullptr;
                if (by) {
                    const auto rooms = near_rooms(*game_data, *level, by->player.x, by->player.y);
                    for (const auto& other : fight.monsters)
                        if (!other.alive() && game_data->monsters.types[std::size_t(other.type)].base == d2d::rules::RescueQuest::kDoor
                            && std::ranges::contains(rooms, std::pair{ level, room_of(*level, other.unit.x, other.unit.y) })
                            && d2d::rules::ai_distance(subtile(other.unit.x) - subtile(prisoner.unit.x), subtile(other.unit.y) - subtile(prisoner.unit.y)) < 15) { door = &other; break; }
                }
                const Level* town = game_data->level(d2d::rules::level_ids::kHarrogath);
                if (!door || !town || town->portal_spot.first < 0) { d2d::log::warn("Rescue on Mount Arreat: no portal (door {}, town spot {})", door != nullptr, town ? town->portal_spot.first : -2); continue; }
                const auto [spot_x, spot_y] = level->nearest_free(door->unit.x + 2.f / 5, door->unit.y);
                const auto [town_x, town_y] = town->nearest_free(town->portal_spot.first, town->portal_spot.second);
                portal[kCagePortals + std::size_t(slot) * 2] = { level, spot_x, spot_y, now_ms };
                portal[kCagePortals + std::size_t(slot) * 2 + 1] = { town, town_x, town_y, now_ms };
                cues.cue("object_townportal", now_ms, spot_x, spot_y);
                const auto opener = std::ranges::find(players, view.player_id, [](const auto& player) { return player->id; });
                act_as(opener == players.end() ? lead() : **opener);
                rescue.portal_opened(quests(), slot);
                d2d::log::info("Rescue on Mount Arreat: cage {}'s portal ({} freed, {} dead of {})", slot, rescue.freed, rescue.dead, rescue.made);
            }
        }
    }

auto World::act5_update(std::uint32_t now_ms) -> void {
        if (hosted_quests) return;
        rescue_update(now_ms);
        if (betrayal_log_at && now_ms >= betrayal_log_at) { betrayal_log_at = 0; betrayal.log_timer(); }
        // A log moved with flag 1 (FUN_00544300): sent again (S->C 0x5d) to a
        // player it's for (act5_detail::told).
        for (auto [news, quest] : { std::pair{ &siege.news, d2d::rules::SiegeQuest::kQuest }, { &rescue.news, d2d::rules::RescueQuest::kQuest },
                                    { &prison.news, d2d::rules::PrisonQuest::kQuest }, { &betrayal.news, d2d::rules::BetrayalQuest::kQuest } })
            if (std::exchange(*news, false) && d2d::rules::act5_detail::told(quests(), quest)) actor->events.push_back(ev::QuestNews{ quest });
    }

}  // namespace d2d::game
