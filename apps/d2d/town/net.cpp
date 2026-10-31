// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): a joined game: what's sent, and the host's NPCs, items, overlays and
// quests. The rest is in this directory.
#include "town.hpp"

#include "common.hpp"
#include "ingame.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <cues.hpp>
#include <d2gs/c2s.hpp>
#include <d2s_items.hpp>
#include <light.hpp>
#include <log.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::client {

// A command to the World, and in a joined game to the host as game.exe's
// client sends it: walks and runs (0x01 / 0x03) to the same act subtile
// d2d's player heads for, the run toggle (0x53 / 0x54). The World still
// walks the player here; the host walks it there on the same map.
// Skills go by the host's unit ids (d2d's monster ids are those in a
// joined game); a melee swing walks up here and plays for show, the host
// walks up there and hits. Objects and waypoints by the host unit nearest
// d2d's.
// NPC talk and trade stay local (the host's gold and items don't follow);
// belt, item moves, stat and skill points aren't sent: net-join-plan.md M9.
auto Town::send(const Command& command) -> void {
        if (!net_game || !level) { net.send(command); return; }
        auto subtile_x = [&](float cell_x) { return (cell_x + float(level->world_x)) * 5.f; };
        auto subtile_y = [&](float cell_y) { return (cell_y + float(level->world_y)) * 5.f; };
        if (const auto* move = std::get_if<cmd::Move>(&command)) {
            net_game->move_to(subtile_x(move->x), subtile_y(move->y), view.running);
            net_clicked_walk = true;
            net_clicked_ms = net_game->steady_now();
        } else if (const auto* run = std::get_if<cmd::Run>(&command)) {
            net_game->set_running(run->running);
        } else if (std::holds_alternative<cmd::SwapWeapons>(command)) {
            net_game->swap_weapons();
        } else if (const auto* merc_item = std::get_if<cmd::MercItem>(&command)) {
            net_game->merc_item(std::uint16_t(merc_item->slot));
        } else if (const auto* use = std::get_if<cmd::UseSkill>(&command)) {
            if (d2d::game::skill_refused(*scene, character, use->skill)) {   // game.exe's client checks first (FUN_004610c0): nothing sent
                world.fight.impossible(world_ms);
                return;
            }
            const auto* skill = scene->skills.get(use->skill);
            const bool melee = skill && !self_cast(*skill) && !world.fight.missile_skill(*skill) && !Fight::spot_skill(*skill) && !world.fight.summon_skill(*skill);
            const auto target = use->unit >= 0 ? net_monsters.find(std::uint32_t(use->unit)) : net_monsters.end();
            std::uint32_t item = 0xffffffff;                // a charged entry's item, as the host knows it
            if (use->item >= 0) {
                // No host id for it: nothing sent, or the host would cast the character's own
                // skill of that id, at its own level and for mana.
                const auto local = std::ranges::find(character.items, use->item, &d2d::d2s::Item::id);
                if (local == character.items.end()) return;
                item = net_game->host_item(*local);
                if (item == 0) return;
            }
            if (target != net_monsters.end()) {
                // The host drops a skill that comes mid-swing (FUN_0057edd0:
                // modes A1 / A2 / SC / TH take one only once FUN_0057ed70 says
                // the swing can end), so repeats are harmless; the same
                // target again once the swing here is over, as game.exe's
                // client, which runs its own copy of the mode.
                const bool again = use->unit == net_attack && use->skill == net_attack_skill;
                if (!again || hero.pmode < 0) net_game->skill_on(use->skill, use->left, 1, std::uint32_t(use->unit), item);
                net_attack = use->unit;
                net_attack_skill = use->skill;
                const auto& monster = target->second;
                if (melee) {
                    if (std::hypot(monster.unit.x - hero.player.x, monster.unit.y - hero.player.y) > 1.6f) net.send(cmd::Move{ monster.unit.x, monster.unit.y, use->left });
                    else world.display_swing(use->skill, monster.unit.x, monster.unit.y, world_ms);
                    return;
                }
                net.send(cmd::UseSkill{ use->skill, monster.unit.x, monster.unit.y, -1, use->left, use->item });   // the missile flies at it here, for show
                return;
            }
            net_attack = -1;
            net_game->skill_at(use->skill, use->left, subtile_x(use->x), subtile_y(use->y), item);
            // At the ground nothing is cast here, so the charge is spent here (the monster path's
            // local UseSkill spends it): the shown count drops and the picker drops the entry at 0.
            if (use->item >= 0)
                for (const auto& entry : d2d::game::charged_skills(hero.character.items))
                    if (entry.item == use->item && entry.skill == use->skill) { (void)d2d::game::use_charge(hero.character.items, entry); break; }
        } else if (const auto* pickup = std::get_if<cmd::Pickup>(&command)) {
            // A host item: the host takes it only within 5 subtiles and doesn't
            // walk us up for it (FUN_00548b00 case 4), so we walk there, it
            // with us (0x01), and ask when close (walk()); it lands in the bags
            // when the host says so.
            if (const auto found = net_game->ground.find(std::uint32_t(pickup->item)); found != net_game->ground.end()) {
                const float to_x = (float(found->second.x) + 0.5f) / 5.f - float(level->world_x), to_y = (float(found->second.y) + 0.5f) / 5.f - float(level->world_y);
                net.send(cmd::Move{ to_x, to_y, true });
                net_game->move_to(float(found->second.x), float(found->second.y), view.running);
                net_pick = int(found->first);
            }
            return;
        } else if (const auto* trade = std::get_if<cmd::OpenTrade>(&command); trade && trade->npc >= 0 && std::size_t(trade->npc) < level->npcs.size()) {
            // The host's NPC nearest d2d's (a vendor walks): talk, then trade (0x38).
            const auto& npc = level->npcs[std::size_t(trade->npc)];
            if (const auto* vendor = net_npc(trade->npc)) {
                // The host talks within 6 subtiles only: walk its player 3 from
                // the NPC on our side, then ask (walk()).
                const float from_x = net_game->host_x - vendor->x, from_y = net_game->host_y - vendor->y, apart = std::max(std::hypot(from_x, from_y), 1.f);
                net_game->move_to(vendor->x + from_x / apart * 3.f, vendor->y + from_y / apart * 3.f, view.running);
                net_game->store_items.clear();
                net_store_shown = 0;
                net_interact_pending = -1;                    // a newer NPC action wins
                net_trade_pending = int(vendor->id);
                net_trade_kind = trade->gamble ? 2 : 1; net_trade_extra = 0;
                net_trade_ms = frame_now;
            } else {
                net_game->log.note("no host NPC near " + npc.name + " to trade with");
            }
        } else if (const auto* caravan = std::get_if<cmd::Caravan>(&command); caravan && caravan->npc >= 0 && std::size_t(caravan->npc) < level->npcs.size()) {
            // Travel (Warriv, Meshif): the host's NPC as a trade's, then 0x38
            // kind 0 with the handler's level (0x4b5140 / 0x4b51a0 / 0x4b5260 /
            // 0x4b52c0); the host moves us (FUN_00579d60).
            namespace monster_ids = d2d::rules::monster_ids;
            const int hc_idx = level->npcs[std::size_t(caravan->npc)].hc_idx;
            if (const auto* host_npc = net_npc(caravan->npc)) {
                const float from_x = net_game->host_x - host_npc->x, from_y = net_game->host_y - host_npc->y, apart = std::max(std::hypot(from_x, from_y), 1.f);
                net_game->move_to(host_npc->x + from_x / apart * 3.f, host_npc->y + from_y / apart * 3.f, view.running);
                net_interact_pending = -1;                    // a newer NPC action wins
                net_trade_pending = int(host_npc->id);
                net_trade_kind = 0;
                net_trade_extra = hc_idx == monster_ids::kMeshif ? 0x4b : hc_idx == monster_ids::kWarrivAct2 ? 1 : 0x28;
                net_trade_ms = frame_now;
            } else {
                net_game->log.note("no host NPC near " + level->npcs[std::size_t(caravan->npc)].name + " to travel with");
            }
            return;                                         // the host moves us, not the local World
        } else if ((std::holds_alternative<cmd::Buy>(command) || std::holds_alternative<cmd::Sell>(command) || std::holds_alternative<cmd::Repair>(command))
                   && !net_game->trade_npc) {
            net_game->log.note("a trade with no host NPC: not made (d2d's own store would make it here only)");
            return;
        } else if (const auto* buy = std::get_if<cmd::Buy>(&command); buy && net_game->trade_npc) {
            const auto& tab = hero.store.tabs[std::size_t(std::clamp(buy->tab >= 0 ? buy->tab : store.tab, 0, 3))];
            if (buy->stock >= 0 && std::size_t(buy->stock) < tab.size()) {
                const auto& item = tab[std::size_t(buy->stock)];
                const int cost = d2d::rules::item_price(scene->rules, item, hero.store.npc_id, false, hero.store.header, store.reduced);
                net_game->buying = true;
                net_game->send_items({ d2d::net::d2gs::c2s::buy(net_game->trade_npc, std::uint32_t(item.id), 0, std::uint32_t(cost)) });
            }
            return;                                         // the host's item comes to us; its gold change too
        } else if (const auto* sell = std::get_if<cmd::Sell>(&command); sell && net_game->trade_npc) {
            auto found = std::ranges::find(hero.character.items, sell->item, &d2d::d2s::Item::id);
            const d2d::d2s::Item* item = found != hero.character.items.end() ? &*found : hero.held && hero.held->id == sell->item ? &*hero.held : nullptr;
            if (const std::uint32_t id = item ? net_game->host_item(*item) : 0) {
                const int cost = d2d::rules::item_price(scene->rules, *item, hero.store.npc_id, true, hero.store.header);
                net_game->send_items({ d2d::net::d2gs::c2s::sell(net_game->trade_npc, id, 0, std::uint32_t(cost)) });
                if (found != hero.character.items.end()) hero.character.items.erase(found);
                else hero.held.reset();
                net_cursor = 0;
            }
            return;                                         // gold from the host
        } else if (std::holds_alternative<cmd::CloseTrade>(command) && net_game->trade_npc) {
            net_game->send_items({ d2d::net::d2gs::c2s::npc_chat(false, 1, net_game->trade_npc) });
            if (net_chat_npc == net_game->trade_npc) net_chat_npc = 0;
            net_game->trade_npc = 0;
            net_game->store_items.clear();
        } else if (std::holds_alternative<cmd::UseItem>(command) || std::holds_alternative<cmd::UseBelt>(command) || std::holds_alternative<cmd::ToCursor>(command)
                   || std::holds_alternative<cmd::ToGrid>(command) || std::holds_alternative<cmd::ToBody>(command) || std::holds_alternative<cmd::ToBelt>(command)
                   || std::holds_alternative<cmd::Drop>(command) || std::holds_alternative<cmd::Socket>(command) || std::holds_alternative<cmd::ScrollToBook>(command)) {
            if (!net_items(command)) return;                  // the host can't make it: nor does the local World
        } else if (const auto* interact = std::get_if<cmd::Interact>(&command); interact && interact->npc >= 0 && std::size_t(interact->npc) < level->npcs.size()) {
            const auto& npc = level->npcs[std::size_t(interact->npc)];
            // The host walks us there only for a skill: d2d walks us up and
            // the host's player follows (walk()); it talks only within 6
            // subtiles (FUN_00548b00 case 1), so the trade's 0x13 comes again
            // once the menu's up (OpenTrade).
            if (npc.root == "objects") {
                if (const auto* object = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f)) { net_operate_type = 2; net_operate = object->id; net_operate_ms = frame_now; net_operate_moves = false; net_operate_waypoint = -1; }
                else if (const auto* closest = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 40.f))
                    d2d::log::info("net: no host object within 4 of {} at ({:.0f}, {:.0f}); nearest is {} (row {}) at ({:.0f}, {:.0f})", npc.name, subtile_x(npc.x), subtile_y(npc.y), closest->id, closest->cls, closest->x, closest->y);
                else d2d::log::info("net: no host object near {} at ({:.0f}, {:.0f})", npc.name, subtile_x(npc.x), subtile_y(npc.y));
            } else if (const auto* host_npc = net_npc(interact->npc)) {
                // A talk as a trade's (kind -1): the host takes 0x13 only
                // within 6 and standing, then 0x2f on its NPC info heals
                // (live, 2026-10-18: an 0x13 while walking was dropped).
                net_interact_pending = -1;                    // a newer NPC action wins
                net_trade_pending = int(host_npc->id);
                net_trade_kind = -1; net_trade_extra = 0;
                net_trade_ms = frame_now;
                net_trade_asked = false;
            } else {                                         // the host hasn't sent it (0x0a'd, out of its sight): walk there, 0x13 once it shows
                net_game->move_to(subtile_x(npc.x), subtile_y(npc.y), view.running);
                net_interact_pending = interact->npc;
                net_game->log.note("no host unit for " + npc.name + " yet: walking there");
            }
        } else if (interact && interact->npc <= -3000 && std::size_t(-3000 - interact->npc) < hero.corpses.size()) {
            // Our corpse: the host's corpse unit (0x8e) nearest d2d's, its
            // items back as the host hands them (FUN_00548b00 case 0).
            const auto& corpse = hero.corpses[std::size_t(-3000 - interact->npc)];
            float best = 10.f;
            for (const std::uint32_t id : net_game->corpses)
                if (const auto unit = net_game->units.find(id); unit != net_game->units.end())
                    if (const float apart = std::hypot(unit->second.x - subtile_x(corpse.x), unit->second.y - subtile_y(corpse.y)); apart < best) {
                        best = apart; net_operate_type = 0; net_operate = id; net_operate_ms = frame_now; net_operate_moves = false; net_operate_waypoint = -1;
                    }
        } else if (interact && interact->npc <= -2000 && interact->npc > -2000 - int(d2d::game::World::kGamePortals)) {
            // A town portal: ours by the host's id for this end (0x82), else the
            // host's object nearest d2d's (they open apart: deviations.md) of
            // the slot's class (World::portal: 59 a town portal, 60 the quest
            // portals, 100 the true tomb's way down); operated once the host
            // has us there, as it warps us (0x15); d2d takes its own here.
            const int which = -2000 - interact->npc;
            static constexpr std::array<int, d2d::game::World::kGamePortals> kHostClass{ 59, 59, 60, 60, 60, 60, 100, 59, 60, 60, 60, 60 };
            if (which < 2 && net_game->portal_here != 0) {
                net_operate_type = 2; net_operate = net_game->portal_here; net_operate_ms = frame_now; net_operate_moves = true; net_operate_waypoint = -1;
            } else if (const auto& entry = world.portal[std::size_t(which)]; entry.level) {
                if (const auto* host_portal = net_game->nearest(2, kHostClass[std::size_t(which)], (entry.x + float(entry.level->world_x)) * 5.f, (entry.y + float(entry.level->world_y)) * 5.f, 10.f)) {
                    net_operate_type = 2; net_operate = host_portal->id; net_operate_ms = frame_now; net_operate_moves = true; net_operate_waypoint = -1;
                } else {
                    net_game->log.note(std::format("no host portal near ({:.0f}, {:.0f})", (entry.x + float(entry.level->world_x)) * 5.f, (entry.y + float(entry.level->world_y)) * 5.f));
                }
            }
        } else if (interact && interact->npc <= -4000 && std::size_t(-4000 - interact->npc) < view.others.size()) {
            if (net_game->trade.state != 0) return;            // one trade at a time
            // Another player: walk up, then 0x13 on them asks for a trade
            // (FUN_00548b00 case 0); their game.exe shows the request.
            const auto& other = view.others[std::size_t(-4000 - interact->npc)];
            net.send(cmd::Move{ other.unit.x, other.unit.y, true });
            net_operate_type = 0; net_operate = other.id; net_operate_ms = frame_now; net_operate_moves = false; net_operate_waypoint = -1;
            return;
        } else if (const auto* heard = std::get_if<cmd::QuestMessage>(&command); heard && heard->npc >= 0 && std::size_t(heard->npc) < level->npcs.size()) {
            // The host's quests hear it (0x31): its NPC's id, or for an object
            // (the Moldy Tome) the host's object there.
            const auto& npc = level->npcs[std::size_t(heard->npc)];
            const NetGame::Unit* unit = npc.root == "objects" ? net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f) : net_npc(heard->npc);
            if (unit) net_game->send_items({ d2d::net::d2gs::c2s::quest_message(unit->id, std::uint32_t(heard->string)) });
            else net_game->log.note(std::format("no host unit for {}'s quest message {}", npc.name, heard->string));
        } else if (std::holds_alternative<cmd::Resurrect>(command)) {
            net_game->send_items({ d2d::net::d2gs::c2s::resurrect() });   // the host respawns us in town (0x15)
        } else if (const auto* travel = std::get_if<cmd::Waypoint>(&command); travel && travel->npc >= 0 && std::size_t(travel->npc) < level->npcs.size()) {
            const auto& npc = level->npcs[std::size_t(travel->npc)];
            if (const auto* object = net_game->nearest(2, -1, subtile_x(npc.x), subtile_y(npc.y), 4.f)) {
                net_operate_type = 2; net_operate = object->id; net_operate_ms = frame_now; net_operate_moves = true;   // once the host has us there, stopped
                net_operate_waypoint = travel->level;
                net_warp_to = travel->level;                        // the host's reassign takes us there
                const auto [free_x, free_y] = level->nearest_free(npc.x + 0.5f, npc.y + 1.5f);   // beside it, as d2d's own walk-up
                net_warp_x = subtile_x(free_x); net_warp_y = subtile_y(free_y);
            } else {
                net_game->log.note(std::format("no host waypoint near ({:.0f}, {:.0f})", subtile_x(npc.x), subtile_y(npc.y)));
            }
        }
        net.send(command);
    }

// The host's NPC for d2d's NPC `index`: a unit of the same MonStats Id
// (Akara for Akara), the nearest if several (rogues); nullptr while the
// host hasn't sent it (out of range).
auto Town::net_npc(int index) const -> const NetGame::Unit* {
        if (!net_game || !level || index < 0 || std::size_t(index) >= level->npcs.size()) return nullptr;
        const auto& npc = level->npcs[std::size_t(index)];
        const float at_x = (npc.x + float(level->world_x)) * 5.f, at_y = (npc.y + float(level->world_y)) * 5.f;
        const NetGame::Unit* found = nullptr;
        float best = 1e9f;
        for (const auto& [unit_key, unit] : net_game->units) {
            if (unit.type != 1 || unit.cls < 0 || std::size_t(unit.cls) >= scene->monsters.types.size() || scene->monsters.types[std::size_t(unit.cls)].id != npc.id) continue;
            if (const float distance = std::hypot(unit.x - at_x, unit.y - at_y); distance < best) { best = distance; found = &unit; }
        }
        return found;
    }

// Our item moves and uses to the host, by its ids for d2d's items (the
// same code in the same place: NetGame::host_item); d2d still makes the
// move here. An item the host doesn't know is skipped and logged.
auto Town::net_items(const Command& command) -> bool {
        using namespace d2d::d2s;
        namespace c2s = d2d::net::d2gs::c2s;
        auto local = [&](int id) -> const Item* {
            const auto found = std::ranges::find(hero.character.items, id, &Item::id);
            return found == hero.character.items.end() ? nullptr : &*found;
        };
        auto host = [&](const Item* item) -> std::uint32_t {
            const std::uint32_t id = item ? net_game->host_item(*item) : 0;
            if (item && !id) net_game->log.note("no host id for our " + item->code + " (not sent)");
            return id;
        };
        const std::uint32_t held_id = hero.held ? (net_cursor ? net_cursor : net_game->host_item(*hero.held)) : 0;
        if (const auto* use = std::get_if<cmd::UseItem>(&command)) {
            const auto* item = local(use->item);
            if (const auto id = host(item); id && item->location == item_location::kBelt) net_game->send_items({ c2s::use_belt(id) });
            else if (id) net_game->send_items({ c2s::use_item(id, std::uint16_t((hero.player.x + float(level->world_x)) * 5.f), std::uint16_t((hero.player.y + float(level->world_y)) * 5.f)) });
        } else if (const auto* belt_use = std::get_if<cmd::UseBelt>(&command)) {
            const auto found = std::ranges::find_if(hero.character.items, [&](const Item& item) { return item.location == item_location::kBelt && item.column == belt_use->slot; });
            if (found != hero.character.items.end()) if (const auto id = host(&*found)) net_game->send_items({ c2s::use_belt(id) });
        } else if (const auto* to_cursor = std::get_if<cmd::ToCursor>(&command)) {
            const auto* item = local(to_cursor->item);
            if (const auto id = host(item)) {
                if (item->location == item_location::kEquipped) net_game->send_items({ c2s::body_to_cursor(std::uint16_t(item->slot)) });
                else net_game->send_items({ c2s::item_id_packet(item->location == item_location::kBelt ? 0x24 : 0x19, id) });
                net_cursor = id;
            }
        } else if (const auto* to_grid = std::get_if<cmd::ToGrid>(&command); to_grid && held_id) {
            // What's under the held item's footprint there: a swap (0x1f).
            const auto info = scene->rules.item_info.find(hero.held->code);
            const int width = info != scene->rules.item_info.end() ? info->second.width : 1, height = info != scene->rules.item_info.end() ? info->second.height : 1;
            const Item* under = nullptr;
            for (const auto& item : hero.character.items) {
                if (item.location != item_location::kStored || item.panel != to_grid->panel) continue;
                const auto item_size = scene->rules.item_info.find(item.code);
                const int item_w = item_size != scene->rules.item_info.end() ? item_size->second.width : 1, item_h = item_size != scene->rules.item_info.end() ? item_size->second.height : 1;
                if (item.column < to_grid->col + width && to_grid->col < item.column + item_w && item.row < to_grid->row + height && to_grid->row < item.row + item_h) { under = &item; break; }
            }
            const std::uint32_t buffer = to_grid->panel == item_panel::kStash ? 4 : to_grid->panel == item_panel::kCube ? 3 : 0;
            if (under) { if (const auto target = host(under)) net_game->send_items({ c2s::swap_grid(held_id, target, std::uint32_t(to_grid->col), std::uint32_t(to_grid->row)) }); net_cursor = net_game->host_item(*under); }
            else { net_game->send_items({ c2s::cursor_to_grid(held_id, std::uint32_t(to_grid->col), std::uint32_t(to_grid->row), buffer) }); net_cursor = 0; }
        } else if (const auto* to_body = std::get_if<cmd::ToBody>(&command); to_body && held_id) {
            const auto worn = std::ranges::find_if(hero.character.items, [&](const Item& item) { return item.location == item_location::kEquipped && item.slot == to_body->slot; });
            const bool swap = worn != hero.character.items.end();
            net_game->send_items({ c2s::equip(held_id, std::uint32_t(to_body->slot), swap) });
            net_cursor = swap ? net_game->host_item(*worn) : 0;
        } else if (const auto* to_belt = std::get_if<cmd::ToBelt>(&command); to_belt && held_id) {
            net_game->send_items({ c2s::cursor_to_belt(held_id, std::uint32_t(to_belt->box)) });
            net_cursor = 0;
        } else if (const auto* to_book = std::get_if<cmd::ScrollToBook>(&command); to_book && held_id) {
            const auto target = host(local(to_book->book));
            if (!target) return false;
            net_game->send_items({ c2s::scroll_to_book(held_id, target) });
            net_cursor = 0;
        } else if (const auto* socket = std::get_if<cmd::Socket>(&command); socket && held_id) {
            const auto target = host(local(socket->item));
            if (!target) return false;
            net_game->send_items({ c2s::insert_socket(held_id, target) });
            net_cursor = 0;
        } else if (std::holds_alternative<cmd::Drop>(command) && held_id) {
            net_game->send_items({ c2s::item_id_packet(0x17, held_id) });
            net_cursor = 0;
        }
        return true;
    }

// A joined game: the host's monsters stand in for the World's (it makes
// none: Fight::remote_monsters) and its other players are drawn; act
// subtiles to the level's cells (a subtile's centre: d2d's x.5 is a cell's). The host's NPCs are left out: d2d's own
// walk the camp (ponytail: matched by class and position later, M6).
auto Town::net_overlay() -> void {
        if (!level || !scene) return;
        auto cell_x = [&](float subtile_x) { return (subtile_x + 0.5f) / 5.f - float(level->world_x); };
        auto cell_y = [&](float subtile_y) { return (subtile_y + 0.5f) / 5.f - float(level->world_y); };
        view.monsters.clear();
        view.others.clear();
        auto host_states = [&](const std::vector<int>& rows) {
            std::vector<std::string_view> names;
            for (const int row : rows) if (std::size_t(row) < scene->state_names.size()) names.emplace_back(scene->state_names[std::size_t(row)]);
            return names;
        };
        // A host monster's (or our merc's) unit command as a mode
        // (DAT_006da4d8, net-packets.md "The unit command"): dying plays DT
        // once, then lies DD; an act plays its mode, then stands or walks again.
        // ponytail: an act lasts 600 ms, not its animation's length.
        auto host_mode = [&](const NetGame::Unit& unit) -> std::string_view {
            static constexpr std::array<std::string_view, 0x1e> kCommandMode = {
                "WL", "WL", "", "", "SC", "SC", "GH", "NU", "DT", "DD", "A1", "A1", "S1", "S1", "S2", "S2",
                "A2", "A2", "BL", "", "KB", "SQ", "SQ", "RN", "RN", "S1", "S3", "S3", "S4", "S4" };
            const auto since = net_game->steady_now() - unit.mode_ms;
            const std::string_view commanded = unit.mode >= 0 && std::size_t(unit.mode) < kCommandMode.size() ? kCommandMode[std::size_t(unit.mode)] : "";
            if (unit.mode == 8 || unit.mode == 9) return unit.mode == 8 && since < 1500 ? "DT" : "DD";
            if (unit.life == 0) return "DD";
            if (!commanded.empty() && commanded != "WL" && commanded != "NU" && since < 600) return commanded;
            return unit.moving ? "WL" : "NU";
        };
        for (const auto& [unit_key, unit] : net_game->units) {
            if (unit.type == 0) {
                if (unit.id == net_game->self_id) continue;
                View::OtherPlayer other{ .cls = unit.cls, .name = unit.name, .id = unit.id };
                std::vector<d2d::d2s::Item> worn;
                for (const auto& entry : unit.worn) worn.push_back(entry.second);
                other.gfx = scene->look_of(worn);
                other.unit.x = cell_x(unit.x);
                other.unit.y = cell_y(unit.y);
                other.unit.walking = unit.moving;
                other.running = unit.moving && unit.running;
                if (unit.moving) other.unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
                // A skill (0x4c / 0x4d) plays its Skills.txt anim once, facing
                // where it went (A1 / TH an attack, else a cast, as ours).
                auto& shown = net_other_modes[unit.id];
                if (const auto* skill_row = unit.skill >= 0 ? scene->skills.get(unit.skill) : nullptr) {
                    const int mode = skill_row->anim == "A1" || skill_row->anim == "TH" ? kModeA1 : kModeSC;
                    if (net_game->steady_now() - unit.skill_ms < scene->composite(unit.cls, mode, other.gfx).length_ms()) {
                        other.mode = mode;
                        other.unit.dir = direction16(unit.skill_x - unit.x, unit.skill_y - unit.y);
                    }
                }
                if (other.mode != shown.mode || (other.mode >= 0 && unit.skill_ms != shown.skill_ms)) shown.since = world_ms;   // the animation starts over
                shown.mode = other.mode;
                shown.skill_ms = unit.skill_ms;
                other.unit.mode_ms = shown.since;
                other.states = host_states(unit.states);
                // Its walk's % from its class's run / walk and the host's chill (state 11);
                // ponytail: its FRW isn't sent, so a faster run / walk's legs don't show.
                other.walk_percent = World::walk_percent(*world.game_data, unit.cls, other.running, 0, std::ranges::find(other.states, std::string_view("cold")) != other.states.end());
                view.others.push_back(std::move(other));
                continue;
            }
            if (unit.type == 1 && unit.id == net_game->merc_id && unit.id != 0) {   // ours: the World's merc, drawn where the host has it
                if (view.merc) {
                    if (unit.moving) view.merc->unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
                    view.merc->unit.x = cell_x(unit.x);
                    view.merc->unit.y = cell_y(unit.y);
                    view.merc->unit.walking = unit.moving;
                    if (const auto mode = host_mode(unit); mode != view.merc->mode) { view.merc->mode = mode; view.merc->unit.mode_ms = world_ms; }
                }
                continue;
            }
            if (unit.type != 1 || unit.cls < 0 || std::size_t(unit.cls) >= scene->monsters.types.size()) continue;   // objects and warps: d2d's own
            if (std::size_t(unit.cls) < scene->mon_is_npc.size() && scene->mon_is_npc[std::size_t(unit.cls)]) continue;
            auto found = net_monsters.find(unit.id);
            if (found == net_monsters.end()) {
                found = net_monsters.emplace(unit.id, make_monster(*scene, unit.cls, cell_x(unit.x), cell_y(unit.y), rng, net_game->difficulty)).first;
                // The host's boss as its 0xac says: name, colour, mods. An act
                // boss (Andariel, Blood Raven) comes as unique with name seed 0:
                // a boss still, under its MonStats name.
                // ponytail: a rolled unique with seed 0 keeps its plain name too (1 in 65536).
                using d2d::rules::Boss;
                const Boss kind = unit.boss_flags & 4 ? Boss::superunique : unit.boss_flags & 2 ? Boss::unique : unit.boss_flags & 1 ? Boss::champion : unit.boss_flags & 8 ? Boss::minion : Boss::none;
                if (kind != Boss::none) {
                    const auto plain_name = found->second.npc.name;
                    make_boss(*scene, found->second, kind, unit.mods, unit.super, unit.name_seed, net_game->difficulty, rng);
                    if (kind == Boss::unique && unit.name_seed == 0) found->second.npc.name = plain_name;
                }
            }
            auto& monster = found->second;
            monster.id = int(unit.id);
            if (unit.moving) monster.unit.dir = direction16(unit.goal_x - unit.x, unit.goal_y - unit.y);
            monster.unit.x = cell_x(unit.x);
            monster.unit.y = cell_y(unit.y);
            monster.unit.walking = unit.moving;
            monster.mode = host_mode(unit);
            if (monster.mode != monster.last_mode) monster.unit.mode_ms = world_ms;   // the animation starts over
            monster.hit_points = unit.life == 0 ? 0 : std::max(1, monster.stats.hit_points * unit.life / 128);
            monster.host_states = host_states(unit.states);
            monster.align = std::ranges::contains(monster.host_states, std::string_view("conversion")) ? 2 : 0;   // the host converted it: no target, as Fight::monster_states
            // The host sends no MonSounds: game.exe's client plays them itself, from the unit's
            // mode (FUN_004cc410, FUN_004cb890), its first showing (FUN_004cc380), Neutral
            // (FUN_004cb460) and footsteps (FUN_004caf60). The same path as single player's,
            // which moves last_mode on.
            world.fight.monster_sounds(monster, world_ms);
            view.monsters.push_back(monster);
        }
        // A unit the host has removed is gone here too: one that comes back is
        // a new unit, its Init again, as game.exe frees and makes it anew.
        std::erase_if(net_monsters, [&](const auto& entry) { return !net_game->units.contains(std::uint64_t(1) << 32 | entry.first); });
        std::erase_if(net_other_modes, [&](const auto& entry) { return !net_game->units.contains(entry.first); });
        std::ranges::move(world.cues.due, std::back_inserter(cues.due));
        world.cues.due.clear();
        // The host's ground items, named as d2d names its own (labels kept by id).
        view.ground.clear();
        for (const auto& [id, lying] : net_game->ground) {
            auto& shown = net_ground[id];
            if (shown.id < 0) {
                shown.id = int(id);
                shown.item = lying.item;
                shown.gold = lying.gold;
                shown.now_ms = world_ms;
                if (lying.item.code == "gld") shown.label = std::to_string(lying.gold) + " Gold";
                else if (const auto lines = item_lines(*scene, lying.item, int(character.stats.get(d2d::d2s::kLevel))); !lines.empty()) {
                    shown.label = lines[0].text;
                    shown.rgb = lines[0].rgb;
                }
            }
            shown.x = cell_x(float(lying.x));
            shown.y = cell_y(float(lying.y));
            view.ground.push_back(shown);
        }
        std::erase_if(net_ground, [&](const auto& entry) { return !net_game->ground.contains(entry.first); });
        // The host monster being attacked: held, the attack goes on (input); not once it's converted.
        const int attacked = net_attack >= 0 ? view.monster(net_attack) : -1;
        if (attacked >= 0 && view.monsters[std::size_t(attacked)].alive() && !view.monsters[std::size_t(attacked)].ally_not_interact(*scene)) {
            view.attack = net_attack;
            view.attack_skill = net_attack_skill;
        } else {
            net_attack = -1;
            view.attack = -1;
        }
    }

// A quest message for the player plays at once (FUN_004a10e0 on the first
// kind-0 one; hearing it is what the server acts on), the menu after it.
auto Town::take_npc_quest(int npc, std::vector<d2d::rules::QuestMsg> lines, std::uint32_t frame_ms) -> void {
        npc_quest = std::move(lines);
        for (const auto& message : npc_quest)
            if (message.greet) {
                npc_menu = {};
                speech = start_speech(*scene, npc, std::uint16_t(message.string), frame_ms);
                send(cmd::QuestMessage{ npc, message.string });
                menu_after_speech = npc;
                return;
            }
    }

// A joined game's quest talk (the host runs the quests): each NPC's lines
// as it sends them (0x27 mode 1) — the one we're talking to takes them
// now, one that comes first waits for d2d's menu — and its "!" (0x8a):
// npcalert over that NPC, as game.exe's client puts the looping overlay
// on (FUN_004b3380 -> FUN_00470390, overlay 0x48).
// ponytail: 0x8a's palace guard check (class 0x14b, FUN_004b1620) isn't
// here, and its alert still expires locally after 16 * 111 ms.
auto Town::net_quests(std::uint32_t frame_ms) -> void {
        for (auto& host_quest : net_game->npc_quests) {
            const auto* host_npc = npc_menu.npc >= 0 ? net_npc(npc_menu.npc) : nullptr;
            if (host_npc && host_npc->id == host_quest.id) take_npc_quest(npc_menu.npc, std::move(host_quest.lines), frame_ms);
            else { net_early_quest = std::move(host_quest); net_early_ms = world_ms; }
        }
        net_game->npc_quests.clear();
        // The log as the host has it (0x52 / 0x5d / 0x29): the View's are d2d's own records'.
        if (net_game->quest_log_known) {
            view.quest_log = net_game->quest_log;
            view.game_quests = net_game->game_quests;
        }
        view.den_left = net_game->den_left;
        if (net_game->sky) { world.day.set(net_game->sky->phase, net_game->sky->time, net_game->sky->eclipse); net_game->sky.reset(); }
        // The Cube: the host opens it (0x77 0x15); shut here, the host's shuts too (0x4f 0x17).
        if (std::exchange(net_game->cube_opened, false)) { cube_open = net_cube_open = true; inv_open = true; stash_open = char_open = false; }
        if (net_cube_open && !cube_open) { net_game->send_items({ d2d::net::d2gs::c2s::button(0x17) }); net_cube_open = false; }
        for (const std::uint32_t id : net_game->npc_alerts) net_alert_ms[id] = world_ms;
        net_game->npc_alerts.clear();
        for (std::size_t i = 0; level && i < view.npc_states.size() && i < level->npcs.size(); ++i) {
            const auto* host_npc = level->npcs[i].root != "objects" ? net_npc(int(i)) : nullptr;
            const auto shown = host_npc ? net_alert_ms.find(host_npc->id) : net_alert_ms.end();
            view.npc_states[i].alert = shown != net_alert_ms.end() && world_ms - shown->second < 16 * 111;
        }
    }

}  // namespace d2d::client
