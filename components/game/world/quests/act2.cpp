// SPDX-License-Identifier: GPL-3.0-or-later
// Act 2's quests at the World's quest hooks (world.hpp: World::actN_*).
// A new act adds its own file like this one and a call at each hook.
#include "world.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "log.hpp"
#include <drlg.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <object_ids.hpp>
#include <outdoor_data.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <vector>

namespace d2d::game {

auto World::act2_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const -> void {
        log[d2d::rules::RadamentQuest::kQuest] = std::uint8_t(radament.log_state(bits));
        log[d2d::rules::TaintedSunQuest::kQuest] = std::uint8_t(tainted.log_state(bits));
        log[d2d::rules::HoradricStaffQuest::kQuest] = std::uint8_t(staff.log_state(bits, staff_items(), tainted.used));
        log[d2d::rules::ArcaneQuest::kQuest] = std::uint8_t(arcane.log_state(bits));
        log[d2d::rules::SummonerQuest::kQuest] = std::uint8_t(summoner.log_state(bits));
        log[d2d::rules::SevenTombsQuest::kQuest] = std::uint8_t(tombs.log_state(bits));
    }

auto World::act2_join() -> void {
        d2d::rules::ActTwoOpening::join(quests(), game_bits);
        radament = {};
        radament.join(quests(), game_bits, carries("ass"));
        tainted = {};
        tainted.join(quests(), game_bits);
        arcane = {};
        arcane.join(quests(), game_bits, carries("hst"));
        summoner = {};
        summoner.join(quests());
        tombs = {};
        { auto act = d2d::drlg::act_seed(game_data->map_seed); tombs.true_tomb = d2d::drlg::roll_act2_tombs(act).boss; }   // FUN_0061aeb0: act +0x94
        tombs.join(quests());
        staff = {};
        staff.join(quests(), staff_items(), false);
        if (radament.state == 5 || !radament.open) tombs.chain();   // Act 2's root, Radament's +0xf0 (FUN_005991c0): on to record 13's
    }

auto World::act2_enter(const Level& from) -> void {
        radament.enter(quests(), from.id, actor->id);
        if (from.id == d2d::rules::level_ids::kLutGholein) staff.rewarded.remove(actor->id);   // LAB_00599a00
        arcane.enter(quests(), from.id, level->id, actor->id);
        summoner.enter(from.id, level->id, actor->id);
        tombs.enter(quests(), from.id, level->id, actor->id);
        if (level->id == tombs.true_tomb)   // the orifice's InitFn 21 (FUN_0059db50): the level's rooms all come up
            for (std::size_t index = 0; index < level->npcs.size(); ++index)
                if (level->npcs[index].operate_fn == d2d::rules::operate_fn::kOrifice && tombs.orifice_seen()) operated.try_emplace({ level, int(index) }, now);
        if (tainted.enter(from.id, level->id, actor->id)) tainted.dark_in = int(fight.spawning.quests(17));
        if (level->id == d2d::rules::TaintedSunQuest::kTemple)   // the altar's InitFn 20 (FUN_0059a3f0): the level's rooms all come up
            for (const auto& object : level->npcs) if (object.root == "objects" && object.object_id == d2d::rules::TaintedSunQuest::kAltar) tainted.altar_seen();
    }

auto World::act2_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx, bool fresh) -> void {
        std::ranges::copy(d2d::rules::ActTwoOpening::talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(radament.talk(quests(), hc_idx, actor->id), std::back_inserter(out));
        std::ranges::copy(tainted.talk(quests(), hc_idx, actor->id), std::back_inserter(out));
        std::ranges::copy(staff.talk(quests(), hc_idx, staff_items(), actor->id), std::back_inserter(out));
        std::ranges::copy(arcane.talk(quests(), hc_idx, [&] {          // the guard's line: one roll a talk on the quest seed
                              if (fresh) guard_line = 187 + int(fight.spawning.quests.next() % 3);
                              return std::uint32_t(guard_line - 187);
                          }, actor->id), std::back_inserter(out));
        std::ranges::copy(summoner.talk(quests(), hc_idx, actor->id), std::back_inserter(out));
        std::ranges::copy(tombs.talk(quests(), hc_idx, d2d::rules::quest::bit(game_bits, d2d::rules::SummonerQuest::kQuest, 13), actor->id), std::back_inserter(out));
    }

auto World::act2_talk_closed(int hc_idx) -> void {
        radament.talk_closed(quests(), hc_idx);
        arcane.talk_closed(quests(), hc_idx);
        tombs.talk_closed(quests(), hc_idx);
    }

auto World::act2_said(int hc_idx, int message_string) -> void {
        d2d::rules::ActTwoOpening::said(quests(), game_bits, hc_idx, message_string);
        const int radament_was = radament.state;
        radament.said(quests(), game_bits, hc_idx, message_string, actor->id);
        if (radament_was != 5 && radament.state == 5) tombs.chain();        // Radament's +0xf0: on to record 13's
        const int tainted_was = tainted.state;
        tainted.said(quests(), game_bits, hc_idx, message_string, actor->id);
        if (tainted_was != 5 && tainted.state == 5 && arcane.chain()) tombs.chain();   // the Tainted Sun's +0xf0: on to record 11's, then 13's
        arcane.said(quests(), game_bits, hc_idx, message_string, room_of(*level, actor->player.x, actor->player.y), actor->id);
        summoner.said(quests(), game_bits, message_string, actor->id);
        if (const auto heard = tombs.said(quests(), game_bits, hc_idx, message_string, level->id == d2d::rules::SevenTombsQuest::kLair, actor->id); heard.town_portal) {
            // Tyrael's (FUN_0056d130: object 59 to level 40), searched from the
            // free spot by the player (the caller's own spot isn't traced).
            const auto [from_x, from_y] = level->nearest_free(actor->player.x, actor->player.y + 0.6f);
            open_portal_at(from_x, from_y, now);
            if (heard.act3) open_act(2);                                  // Act 3 open
            d2d::log::info("The Seven Tombs: Tyrael's portal");
        } else if (heard.take_staff) {
            for (const char* code : { "hst", "vip", "msf" }) take_carried(code);
        }
        if (hc_idx == d2d::rules::ArcaneQuest::kDrognan || hc_idx == d2d::rules::ArcaneQuest::kJerhyn) show_palace();   // the guard steps aside
        if (staff.said(quests(), hc_idx, message_string, actor->id)) take_carried("tr1");   // the scroll read: it goes
    }

auto World::act2_tick() -> void {
        radament.tick();
        tainted.tick(quests());
        if (tombs.tick()) durielportal_open(day_at);
        // Tyrael's think (NpcStationary, FUN_005e73a0): once his portal is
        // open (FUN_0059df50: record +0xf) and no player is within 12 of
        // him (FUN_006416d0, the sweep at 0x59df30), not in a talk
        // (FUN_00572dc0), he leaves (FUN_005ddfc0 mode 0), for good.
        // ponytail: the takers' unit event 0x36 (LAB_0059c710) unread.
        if (level->id == d2d::rules::SevenTombsQuest::kLair && tombs.tyrael_portal && !tombs.tyrael_gone)
            for (std::size_t i = 0; i < level->npcs.size() && i < npc_states.size(); ++i)
                if (level->npcs[i].hc_idx == d2d::rules::SevenTombsQuest::kTyrael && actor->talking[0] != int(i)
                    && std::hypot(level->npcs[i].x - actor->player.x, level->npcs[i].y - actor->player.y) * 5 >= 12) {
                    npc_states[i].hidden = true;
                    tombs.tyrael_gone = true;
                    class_cue("act2_complete_tombs", day_at);   // LAB_0059c710: unit event 0x36
                    d2d::log::info("The Seven Tombs: Tyrael leaves");
                }
        if (summoner.tick() && d2d::rules::quest::bit(quests(), d2d::rules::SummonerQuest::kQuest, 13) && level->id == d2d::rules::SummonerQuest::kSanctuary)
            class_cue("act2_complete_summoner", day_at);   // voice 0x33
        // The act's sky (S->C 0x53, FUN_0059a100 / FUN_0059a170): dark
        // while the Tainted Sun has it out and the player's in Act 2; a
        // joined game's comes from the host.
        // One clock, not one an act: d2d's players share a level (local
        // co-op), a joined game's sky is the host's.
        if (const bool dark = tainted.dark && act() == 1; !hosted_quests && dark != day.eclipse) day.set(dark ? 5 : 2, 0, dark);
    }

auto World::act2_alert(int hc_idx) -> bool {
        return d2d::rules::ActTwoOpening::alert(quests(), hc_idx)
            || radament.alert(quests(), hc_idx)
            || tainted.alert(quests(), hc_idx)
            || d2d::rules::HoradricStaffQuest::alert(quests(), hc_idx, staff_items())
            || arcane.alert(quests(), hc_idx)
            || d2d::rules::SummonerQuest::alert(quests(), hc_idx)
            || tombs.alert(quests(), hc_idx);
    }

auto World::act2_kill(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (kill.type == d2d::rules::monster_ids::kRadament) radament_died(kill, now_ms);
        if (kill.type == d2d::rules::SevenTombsQuest::kDuriel) {     // FUN_0059d050: each player in the Lair (FUN_0059cfb0)
            each_player([&](Player&) { tombs.duriel_died(quests(), level->id == d2d::rules::SevenTombsQuest::kLair); });
            d2d::log::info("The Seven Tombs: Duriel is dead");
        }
        // The Summoner (FUN_0059c150): each player by him (his room or one next to it) takes part
        // (FUN_0059be70), their party in Act 2, the rest heard of it (LAB_0059c0f0).
        if (kill.type == d2d::rules::monster_ids::kSummoner && summoner.open) {
            const auto by_kill = [&](const Player& player) { return level->id == d2d::rules::SummonerQuest::kSanctuary && near_room(kill.x, kill.y, player.player.x, player.player.y); };
            kill_credit(d2d::rules::SummonerQuest::kQuest, 1, { 13, 1 }, by_kill, [&](Player&) { summoner.killed(quests(), true); }, [&](Player&) { summoner.killed(quests(), false); });
            d2d::log::info("The Summoner: dead");
        }
    }

auto World::act2_update() -> void {
        // The Summoner goes for the player (FUN_005f85c0's first think -> FUN_0059c330).
        // His first think (FUN_005f85c0: AI control +0x14 0 -> 1) opens it (FUN_0059c330).
        if (!summoner.started && !hosted_quests && level->id == d2d::rules::SummonerQuest::kSanctuary)
            for (const auto& monster : fight.monsters)
                if (monster.type == d2d::rules::monster_ids::kSummoner && monster.alive() && monster.ai_state != 0) {
                    summoner.engaged(quests());
                    each_player([&](Player&) { summoner.mark(quests()); });   // every player's bit 2 (FUN_0059c330's mark)
                    break;
                }
    }

}  // namespace d2d::game
