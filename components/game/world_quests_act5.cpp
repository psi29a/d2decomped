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
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {
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
        // Nihlathak (MonStats base 0x20e, bound to 0x22: FUN_0058b7a0).
        if (std::size_t(kill.type) < game_data->monsters.types.size() && game_data->monsters.types[std::size_t(kill.type)].base == d2d::rules::BetrayalQuest::kNihlathak) {
            if (betrayal.killed(quests(), about)) betrayal_log_at = now_ms + d2d::rules::BetrayalQuest::kLogTicks * kTickMs;
            d2d::log::info("Betrayal of Harrogath: Nihlathak is dead");
        }
    }

auto World::act5_update(std::uint32_t now_ms) -> void {
        if (hosted_quests) return;
        if (betrayal_log_at && now_ms >= betrayal_log_at) { betrayal_log_at = 0; betrayal.log_timer(); }
        // A log moved with flag 1 (FUN_00544300): sent again (S->C 0x5d) to a
        // player it's for (act5_detail::told).
        for (auto [news, quest] : { std::pair{ &siege.news, d2d::rules::SiegeQuest::kQuest }, { &rescue.news, d2d::rules::RescueQuest::kQuest },
                                    { &prison.news, d2d::rules::PrisonQuest::kQuest }, { &betrayal.news, d2d::rules::BetrayalQuest::kQuest } })
            if (std::exchange(*news, false) && d2d::rules::act5_detail::told(quests(), quest)) actor->events.push_back(ev::QuestNews{ quest });
    }

}  // namespace d2d::game
