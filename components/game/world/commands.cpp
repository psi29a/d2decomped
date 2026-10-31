// SPDX-License-Identifier: GPL-3.0-or-later
// The World (world.hpp): the commands players send. The rest is in this directory.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "item_text.hpp"
#include "log.hpp"
#include "npc_menu.hpp"
#include "protocol.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <merc.hpp>
#include <monster_ids.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

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
                if (arcane.said(quests(), game_bits, -1, message->string, room_of(*level, actor->player.x, actor->player.y), actor->id)) canyon_portal(now);
                return true;
            }
            if (level->npcs[std::size_t(message->npc)].operate_fn == d2d::rules::operate_fn::kMoldyTome) {   // the tome's 127 (FUN_00594960: no NPC check)
                if (message->string == d2d::rules::TowerQuest::kTome && !closed_at_join[std::size_t(d2d::rules::TowerQuest::kQuest)]) { tower.read_tome(quests(), actor->id); d2d::log::info("The Forgotten Tower: read the Moldy Tome"); }
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
        // A charged entry's cast (UseSkill::item): only while the entry is
        // there, its item active with charges left (FUN_00647840; a client
        // with none starts nothing, FUN_004c6140). Its level and cost go
        // with the cast, so they change only between casts; a queued attack
        // keeps its own (attack_charge), which start_swing takes up.
        ChargedSkill entry;
        if (use_skill.item >= 0) {
            const auto entries = charged_skills(actor->character.items);
            const auto found = std::ranges::find_if(entries, [&](const ChargedSkill& had) { return had.item == use_skill.item && had.skill == use_skill.skill; });
            if (found == entries.end()) {
                if (actor->pmode < 0) actor->item_charge = {};
                if (actor->attack_charge.item == use_skill.item) { actor->attack_mon = -1; actor->attack_charge = {}; }   // a held attack from it stops
                return;
            }
            entry = *found;
        }
        if (actor->pmode < 0) actor->item_charge = entry;
        const auto* skill = game_data->skills.get(use_skill.skill);
        const int monster_index = fight.monster_index(use_skill.unit);
        // A converted monster is the player's side: the cursor never picks it, so no attack.
        // unverified (source: d2d's hover rule, cursor-units.md): a click with no unit under the
        // cursor walks, so a held attack drops it; the hover itself is FUN_00465c60's.
        const auto* target = monster_index >= 0 ? &fight.monsters[std::size_t(monster_index)] : nullptr;
        const bool live = target && target->targetable() && !target->ally_not_interact(*game_data);
        if (skill && !skill->in_town && !in_moor) {
            if (fight.sound_rng(2)) class_cue("not_in_town_1", now_ms);
            else class_cue("not_in_town_2", now_ms);
        } else if (skill_refused(*game_data, actor->character, use_skill.skill)) {     // FUN_004610c0: status 2, the client sends nothing
            actor->attack_mon = -1;
            fight.impossible(now_ms);
        } else if (skill && self_cast(*skill)) {                                            // Holy Shield: where the player stands
            if (fight.cast(use_skill.skill, now_ms)) actor->player.walking = false;
        } else if (skill && skill->srvdofunc == ServerDoFunction::kWhirlwind && monster_index < 0) {                      // Whirlwind to that point (FUN_005d8f50)
            actor->move_x = use_skill.x; actor->move_y = use_skill.y;
            actor->attack_mon = -1;
            actor->attack_skill = use_skill.skill;
            actor->attack_charge = entry;
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
            actor->attack_charge = entry;
        }
    }

}  // namespace d2d::game
