// SPDX-License-Identifier: GPL-3.0-or-later
// Act 1's quests at the World's quest hooks (world.hpp: World::actN_*).
// A new act adds its own file like this one and a call at each hook.
#include "world.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "log.hpp"
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <vector>

namespace d2d::game {

auto World::act1_log(d2d::rules::QuestLogStates& log, const d2d::rules::QuestBits& bits) const -> void {
        const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
        log = { 0, std::uint8_t(den.log_state(bits)), std::uint8_t(burial.log_state(bits)),
                std::uint8_t(tools.log_state(bits, holding_malus(), clvl)), std::uint8_t(cain.log_state(bits)),
                std::uint8_t(tower.log_state(bits)), std::uint8_t(andy.log_state(bits)) };
    }

auto World::act1_join() -> void {
        cow_portal_opened = false;
        den = {};
        den.join(quests());
        andy = {};
        andy.join(quests());
        burial = {};
        burial.join(quests());
        tower = {};
        tower.join(quests());
        tools = {};
        tools.join(quests(), carries("hdm"));
        cain = {};
        cain.join(quests(), carries("bks"), carries("bkd"));
        chain(1);
        cain.camp_spawn();
    }

auto World::act1_enter(const Level& from) -> void {
        andy.enter(quests(), from.id, level->id);
        burial.enter(quests(), from.id, level->id, actor->id);
        if (tower.enter(quests(), from.id, level->id, actor->id)) each_player([&](Player&) { tower.mark(quests()); });
        tools.enter(quests(), from.id);
        const bool cain_open = cain.state != 7;
        cain.enter(quests(), from.id, level->id, actor->id);
        if (cain_open && cain.state == 7) each_player([&](Player& player) { cain.missed_it(quests(), player.id); });   // FUN_00592b10 over the players
    }

auto World::act1_talk(std::vector<d2d::rules::QuestMsg>& out, int hc_idx, bool fresh) -> void {
        if (hc_idx == d2d::rules::monster_ids::kFlavie) {
            // Flavie's chat (quest record 0x1d's hook, FUN_00596a90): with the
            // Den not cleared in this game and the player's not done (bits 0,
            // 13, 1), "Turn back" / "Halt" (rand 2), else a "Beware" (rand 3 + 2).
            // Block n of the record's table (0x738778) is message 59 + n for
            // Flavie, and the client's speech box (FUN_004a10e0) shows message m
            // as string m: RogueSignpostGossip1..5.
            namespace quest = d2d::rules::quest;
            const bool warn = !quest::bit(game_bits, 1, 13) && den.active && !quest::bit(quests(), 1, 0) && !quest::bit(quests(), 1, 13) && !quest::bit(quests(), 1, 1);
            if (fresh) flavie_line = 59 + (warn ? int(fight.spawning.quests.next() % 2) : 2 + int(fight.spawning.quests.next() % 3));
            out.push_back({ flavie_line, true });
        }
        std::ranges::copy(den.talk(quests(), hc_idx), std::back_inserter(out));
        std::ranges::copy(andy.talk(quests(), hc_idx, actor->id), std::back_inserter(out));
        std::ranges::copy(burial.talk(quests(), hc_idx, actor->id), std::back_inserter(out));
        std::ranges::copy(tower.talk(quests(), hc_idx, actor->id), std::back_inserter(out));
        std::ranges::copy(tools.talk(quests(), hc_idx, holding_malus(), int(actor->character.stats.get(d2d::d2s::kLevel))), std::back_inserter(out));
        std::ranges::copy(cain.talk(quests(), hc_idx, carries("bks"), actor->id), std::back_inserter(out));
    }

auto World::act1_talk_closed(int hc_idx) -> void {
        andy.talk_closed(hc_idx);
        burial.talk_closed(quests(), hc_idx);
        tools.talk_closed(quests(), hc_idx);
        cain.talk_closed(quests(), hc_idx);
    }

auto World::act1_said(int hc_idx, int message_string) -> void {
        if (tower.said(quests(), hc_idx, message_string, actor->id)) { d2d::log::info("The Forgotten Tower: done"); chain(5); }   // FUN_00594960
        if (andy.said(quests(), hc_idx, message_string, actor->id)) {
            d2d::log::info("Sisters to the Slaughter: done, Warriv's caravan goes east");
        }
        const int tools_was = tools.state;
        if (tools.said(quests(), hc_idx, message_string, holding_malus())) {   // FUN_00544160: the malus goes
            class_cue("act1_complete_tools", now);   // Charsi's completion voice, unit event 0x24
            const int returner = actor->id;          // the party's share (local co-op: everyone's the party)
            each_player([&](Player& player) {
                if (player.id != returner) d2d::rules::ToolsQuest::party_credit(quests(), int(player.character.stats.get(d2d::d2s::kLevel)));
            });
            if (tools_was == 4 && tools.state == 5) chain(3);   // FUN_00591490: its own +0xf0
            else if (tools.active && tools.carried_in) andy.chain();   // data+0xa1: quest 6's +0xf0
            take_carried("hdm");
            d2d::log::info("Tools of the Trade: the malus is back, Charsi's imbue is due");
        }
        using Said = d2d::rules::CainQuest::Said;
        const auto scroll = std::ranges::find(actor->character.items, std::string_view("bks"), &d2d::d2s::Item::code);
        const bool cain_flagged = cain.rescued_flag;
        const auto cain_said = cain.said(quests(), hc_idx, message_string, scroll != actor->character.items.end(), actor->id);
        if (cain_said == Said::ring && !cain_flagged && d2d::rules::quest::bit(quests(), 4, 13)) chain(4);   // FUN_00592250: the game's (4, 13) goes on
        if (cain_said == Said::decipher) {        // ponytail: bks becomes bkd where it lies (same size), not a new item (agent A's)
            scroll->code = "bkd";
            d2d::log::info("Search for Cain: Akara deciphered the scroll");
        }
        if (cain_said == Said::ring) {            // FUN_005466b0: into the inventory, else at the feet
            static constexpr int kIlvl[3] = { 7, 30, 60 };
            const int diff = std::clamp(actor->character.header.active_difficulty(), 0, 2);
            loot.put({ .code = "rin", .quality = diff ? 6 : 4 }, actor->player.x, actor->player.y, kIlvl[diff], fight.spawning.game, now);
            loot.take(loot.ground.size() - 1);
            cues.cue("item_ring", now, actor->player.x, actor->player.y);   // FUN_005458e0(4): S->C 0x5d flags 2, the client's item_ring (FUN_004a2cb0)
            d2d::log::info("Search for Cain: done, Akara's ring");
        }
        const int den_was = den.state, burial_was = burial.state;
        if (den.said(quests(), hc_idx, message_string)) {
            ++actor->character.stats.values[d2d::d2s::kSkillPts];
            d2d::log::info("Den of Evil: Akara's reward, a skill point");
            if (den_was != 5 && den.state == 5) chain(1);   // FUN_0058fdd0
        }
        if (burial.said(quests(), hc_idx, message_string, actor->id)) {
            kashya_merc();
            if (burial_was != 5 && burial.state == 5) chain(2);   // FUN_00590980
        }
    }

auto World::act1_tick() -> void {
        if (andy.tick() && level->id == d2d::rules::level_ids::kCatacombsLevel4) actor->portal_due = true;   // tick 10 of her death (FUN_00596490)
        burial.tick();
        tower.tick();
        tower_treasure(day_at);
    }

auto World::act1_alert(int hc_idx) -> bool {
        return den.alert(quests(), hc_idx)
            || andy.alert(quests(), hc_idx, actor->id)
            || burial.alert(quests(), hc_idx)
            || tower.alert(quests(), hc_idx, actor->id)
            || tools.alert(quests(), hc_idx, holding_malus(), int(actor->character.stats.get(d2d::d2s::kLevel)))
            || cain.alert(quests(), hc_idx, carries("bks"), actor->id);
    }

auto World::act1_kill(const Fight::Kill& kill, std::uint32_t now_ms) -> void {
        if (kill.type == d2d::rules::monster_ids::kAndariel) {
            // The client death effect is independent of quest reward eligibility.
            each_player([](Player& player) { player.events.push_back(ev::QuestQuake{}); });
            andariel_died(kill, now_ms);
        }   // a pet's kill too, the player dead
        if (kill.type == d2d::rules::monster_ids::kBloodRaven) blood_raven_died(now_ms);
        if (kill.super == d2d::rules::TowerQuest::kCountess) countess_died(now_ms);
    }

}  // namespace d2d::game
