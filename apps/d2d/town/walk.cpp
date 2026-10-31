// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): walking, and handling the frame's events. The rest is in this directory.
#include "town.hpp"

#include "audio.hpp"
#include "common.hpp"
#include "controls.hpp"
#include "ingame.hpp"
#include "internal.hpp"
#include "panels.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "store.hpp"
#include "ui.hpp"

#include <cues.hpp>
#include <d2gs/c2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <log.hpp>
#include <quests.hpp>
#include <shake.hpp>
#include <skills.hpp>
#include <weather.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::client {

auto Town::walk(const Mouse& mouse, bool over_ui, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms) -> void {
        frame_now = frame_ms;
        if (net_game) {
            net_game->pump(frame_ms, frame_ms - last_ms);
            // The host moved us (a correction, later warps and waypoints): there we are.
            // Only a spot on d2d's level: a warp's or waypoint's lands as d2d
            // takes it too (the same exit spot, FUN_005550b0).
            world.host_warps = true;
            if (net_game->take_reassign() && level) {
                net_moving_until = 0;
                rain.reassign(rain.rng, level->id, d2d::rules::level_ids::act_of(level->id));
                const float to_x = (net_game->self_x + 0.5f) / 5.f - float(level->world_x), to_y = (net_game->self_y + 0.5f) / 5.f - float(level->world_y);
                const auto inside = [](const Level& where, float cell_x, float cell_y) {
                    return cell_x >= 0 && cell_y >= 0 && cell_x < float(where.ds1.width()) && cell_y < float(where.ds1.height());
                };
                if (!inside(*level, to_x, to_y))
                    d2d::log::info("net: the host put us at {}, {}, off level {} (the warp asked for: {})", net_game->self_x, net_game->self_y, level->id, net_warp_to);
                if (inside(*level, to_x, to_y)) {
                    hero.player.x = to_x;
                    hero.player.y = to_y;
                    hero.player.walking = false;
                } else if (const Level* landed = [&]() -> const Level* {
                               // The warp asked for, else the last 0x03's area, else the
                               // act's level that holds the spot (a quest's door: Duriel's lair).
                               for (const int id : { net_warp_to, net_game->area })
                                   if (const Level* asked = id >= 0 ? world.game_data->level(id) : nullptr; asked && asked->ds1.width() > 0
                                       && inside(*asked, (net_game->self_x + 0.5f) / 5.f - float(asked->world_x), (net_game->self_y + 0.5f) / 5.f - float(asked->world_y))) return asked;
                               for (int id = 1; id < 137; ++id)
                                   if (d2d::rules::level_ids::act_of(id) == d2d::rules::level_ids::act_of(level->id))
                                       if (const Level* holder = world.game_data->level(id); holder && holder->ds1.width() > 0
                                           && inside(*holder, (net_game->self_x + 0.5f) / 5.f - float(holder->world_x), (net_game->self_y + 0.5f) / 5.f - float(holder->world_y))) return holder;
                               return nullptr;
                           }(); landed) {
                    // The host took the warp we asked for: there with it; else
                    // where its last 0x03 said (a caravan, a boat: a new act).
                    const float there_x = (net_game->self_x + 0.5f) / 5.f - float(landed->world_x), there_y = (net_game->self_y + 0.5f) / 5.f - float(landed->world_y);
                    if (inside(*landed, there_x, there_y)) world.arrive(landed, there_x, there_y, "the host's warp");
                    net_warp_to = -1;
                    level = world.level;
                }
            }
            // The host's player is the truth: d2d's walked on where the host's
            // couldn't (a door only d2d opened, a wall the host's path won't
            // take); 2 s that far apart and d2d goes back to where the host has us.
            // Counted from when d2d stops: mid-walk the host's place (0x95 / 0x96,
            // about every 2 s) lags 30+ subtiles behind (live 2026-10-25).
            if (level && net_game->host_x > 0) {
                const float drift = std::hypot((hero.player.x + float(level->world_x)) * 5.f - net_game->host_x, (hero.player.y + float(level->world_y)) * 5.f - net_game->host_y);
                const float back_x = net_game->host_x / 5.f - float(level->world_x), back_y = net_game->host_y / 5.f - float(level->world_y);
                if (hero.player.walking || drift < 15.f || back_x < 0 || back_y < 0 || back_x >= float(level->ds1.width()) || back_y >= float(level->ds1.height())) net_drift_ms = 0;
                else if (net_drift_ms == 0) net_drift_ms = frame_ms;
                else if (frame_ms - net_drift_ms > 2000) {
                    d2d::log::info("net: {:.0f} subtiles from the host's player; back to it", drift);
                    hero.player.x = back_x; hero.player.y = back_y; hero.player.walking = false;
                    net_drift_ms = 0;
                }
            }
            // The host killed us: d2d's death plays (DT, DD, the corpse); a
            // click then sends 0x41 and respawns here too.
            if (net_game->take_death() && !world.fight.dead()) world.fight.die(frame_ms);
            // The host's quest log news (0x5d): the Quest Log button, as the
            // single-player path raises it on a log change (FUN_004a2cb0).
            // Our quest words as the host has them (0x28): the quest log
            // and the NPCs' talk read these.
            if (net_game->quest_words) {
                hero.character.header.quests[std::size_t(std::clamp(hero.character.header.active_difficulty(), 0, 2))] = *net_game->quest_words;
                net_game->quest_words.reset();
            }
            for (const int quest : net_game->quest_news) quest_log_notify(quest_log, quest);
            net_game->quest_news.clear();
            for (const auto kind : std::exchange(net_game->quest_quakes, {}))
                if (kind != d2d::rules::QuestQuake::diablo || level->id == d2d::rules::level_ids::kChaosSanctuary)
                    quest_quakes.push_back({ kind, frame_ms });
            if (net_game->movie >= 0) world.movie = std::exchange(net_game->movie, -1);
            // Our stats as the host sets them: attributes, points, life / mana /
            // stamina and their maxima (8.8 fixed point, as in a save), level,
            // experience, gold. The host's word wins over d2d's own regen.
            // The maxima (7, 9, 11) are the character's own, as a save keeps
            // them (Miriam: 714 / 409 / 423): our items' and Battle Orders'
            // shares stay on top (Fight::own_stats).
            for (const auto& change : net_game->stat_changes) {
                if (change.id < 0 || change.id > 15) continue;
                auto& value = hero.character.stats.values[std::size_t(change.id)];
                value = change.add ? value + change.value : change.value;
                if (!change.add && (change.id == d2d::d2s::kMaxLife || change.id == d2d::d2s::kMaxMana || change.id == d2d::d2s::kMaxStamina)) {
                    const auto which = std::size_t(change.id - d2d::d2s::kMaxLife) / 2;   // life, mana, stamina
                    value += hero.item_max[which] + std::array<std::int64_t, 3>{ hero.bo_life, hero.bo_mana, 0 }[which];
                }
                if (std::getenv("D2D_NET_STATS")) d2d::log::info("net stat {} = {}", change.id, value);
            }
            net_game->stat_changes.clear();
            // What the host put in our bags (a pick-up, a buy): where the host put
            // it, so it keeps matching the host's item.
            for (auto& item : net_game->picked) {
                item.id = world.next_item_id++;
                hero.character.items.push_back(std::move(item));
                world.cues.cue("item_pickup", 0, hero.player.x, hero.player.y);
            }
            net_game->picked.clear();
            // A trade's end: the host sends our items again (a cancel, all
            // of them, new ids; live 2026-10-05): one item a spot, the last.
            if (net_game->trade_settling()) {
                auto& items = hero.character.items;
                for (std::size_t later = items.size(); later-- > 0;)
                    for (std::size_t earlier = 0; earlier < later; ++earlier)
                        if (items[earlier].location == items[later].location && items[earlier].panel == items[later].panel && items[earlier].column == items[later].column
                            && items[earlier].row == items[later].row && items[earlier].slot == items[later].slot && items[earlier].location != d2d::d2s::item_location::kEquipped) {
                            items.erase(items.begin() + std::ptrdiff_t(earlier));
                            --later;
                            break;
                        }
            }
            // The open store shows the host's stock (its ids, for 0x32), by tab.
            if (hero.store.npc >= 0 && net_game->trade_npc && net_game->store_items.size() != net_store_shown) {
                net_store_shown = net_game->store_items.size();
                for (auto& tab : hero.store.tabs) tab.clear();
                for (const auto& [id, stocked] : net_game->store_items) {
                    auto item = stocked;
                    item.id = int(id);
                    hero.store.tabs[std::size_t(std::clamp(item.panel - 1, 0, 3))].push_back(std::move(item));   // page 2 weapons, 4 misc (Akara's, recorded)
                }
            }
            // Once, a while in: how many of our items the host's match (the net
            // log says which don't).
            if (!net_items_checked && net_game->steady_now() > 3000 && !net_game->own_items.empty()) {
                net_items_checked = true;
                int matched = 0;
                for (const auto& item : hero.character.items) {
                    if (net_game->host_item(item)) ++matched;
                    else net_game->log.note("our " + item.code + " (location " + std::to_string(item.location) + ", panel " + std::to_string(item.panel) + " at "
                                            + std::to_string(item.column) + "," + std::to_string(item.row) + ", slot " + std::to_string(item.slot) + ") has no host item");
                }
                d2d::log::info("net: {} of our {} items match the host's ({} it has)", matched, hero.character.items.size(), net_game->own_items.size());
            }
            // d2d's walks of its own (up to an NPC, an object, an item, a melee
            // target): the host's player follows where ours is, every 300 ms
            // (a target the host can't stand on, an NPC's spot, it'd refuse).
            // Not while a warp or portal is being taken: ours may already
            // be in the next area, the host's isn't yet. Nor on a click's walk: the
            // host has its target, and our spot sent then (live 2026-10-25: 40 ms
            // after the click, before ours moved) stopped the host's where it stood.
            const bool area_change = (net_operate != 0 && net_operate_moves) || frame_ms < net_moving_until || net_game->holding();
            if (!hero.player.walking && net_game->steady_now() - net_clicked_ms > 500) net_clicked_walk = false;
            if (hero.player.walking && !net_clicked_walk && frame_ms - net_follow_ms >= 300 && !area_change) {
                net_follow_ms = frame_ms;
                net_game->move_to((hero.player.x + float(level->world_x)) * 5.f, (hero.player.y + float(level->world_y)) * 5.f, view.running);
            }
            // A trade walked to: asked for once the host has us within 6.
            if (net_trade_pending >= 0) {
                const auto vendor = net_game->units.find(std::uint64_t(1) << 32 | std::uint32_t(net_trade_pending));
                if (vendor == net_game->units.end() || frame_ms - net_trade_ms > 8000) {
                    net_game->log.note("the trade's NPC couldn't be reached on the host");
                    net_trade_pending = -1;
                    net_trade_asked = false;
                } else if (const float apart = std::hypot(net_game->host_x - vendor->second.x, net_game->host_y - vendor->second.y); apart > 6.f) {
                    if (frame_ms - net_follow_ms >= 500) {                      // she walks: after her, 2 out
                        net_follow_ms = frame_ms;
                        net_game->move_to(vendor->second.x + (net_game->host_x - vendor->second.x) / apart * 2.f,
                                          vendor->second.y + (net_game->host_y - vendor->second.y) / apart * 2.f, view.running);
                    }
                } else if (!net_trade_asked && net_game->steady_now() - net_game->walked_ms >= 500) {
                    // A walking player is busy: the host drops all but chat,
                    // skill picks, 0x43, 0x66 (FUN_0054d750, FUN_0057eec0). As a
                    // real client (recorded through d2proxy): where we see the
                    // NPC (0x59), then 0x13; the host answers with NPC info.
                    namespace c2s = d2d::net::d2gs::c2s;
                    net_game->npc_info = 0;
                    net_game->send_items({ c2s::unit_position(1, std::uint32_t(net_trade_pending), std::uint32_t(vendor->second.x), std::uint32_t(vendor->second.y)),
                                           c2s::interact(1, std::uint32_t(net_trade_pending)) });
                    net_trade_asked = true;
                } else if (net_trade_asked && net_game->npc_info == std::uint32_t(net_trade_pending)) {
                    // Talking: start the chat, then the trade (1), gamble (2: unverified) or travel (0).
                    namespace c2s = d2d::net::d2gs::c2s;
                    const auto host_npc = std::uint32_t(net_trade_pending);
                    if (net_trade_kind > 0) net_game->trade_npc = host_npc;
                    if (net_chat_npc != host_npc || net_trade_kind < 0) net_game->send_items({ c2s::npc_chat(true, 1, host_npc) });   // a talk: again, its first came before the 0x13 landed
                    net_chat_npc = host_npc;
                    if (net_trade_kind >= 0) net_game->send_items({ c2s::npc_action(std::uint32_t(net_trade_kind), host_npc, std::uint32_t(net_trade_extra)) });
                    net_trade_pending = -1;
                    net_trade_asked = false;
                }
            }
            // An object operated here: the host operates it only near
            // (FUN_00548b00 → FUN_00584540, range < 0x33) and drops a walking
            // player's 0x13 (FUN_0054d750), so it goes once the host has us
            // stopped close by; until then the host's player walks there.
            if (net_operate != 0) {
                const auto object = net_game->units.find(std::uint64_t(net_operate_type) << 32 | net_operate);
                // A warp: to its mouth (the host takes 0x13 within 0x33 and walks
                // the rest, FUN_00548b00 case 5; under 5 it warps).
                const bool warp = net_operate_type == 5 || net_operate_waypoint >= 0;   // a waypoint too: its object fills its tile
                const float spot_x = warp ? net_warp_x : object == net_game->units.end() ? 0.f : object->second.x;
                const float spot_y = warp ? net_warp_y : object == net_game->units.end() ? 0.f : object->second.y;
                if (object == net_game->units.end() || frame_ms - net_operate_ms > (warp ? 15000u : 8000u)) {
                    net_game->log.note("object " + std::to_string(net_operate) + " couldn't be reached on the host");
                    net_operate = 0;
                    net_warp_to = -1;
                } else if (std::hypot(net_game->host_x - spot_x, net_game->host_y - spot_y) > (warp ? 2.f : 4.f)) {
                    if (frame_ms - net_follow_ms >= 500) {
                        net_follow_ms = frame_ms;
                        net_game->move_to(spot_x, spot_y, view.running);
                    }
                } else if (net_game->steady_now() - net_game->walked_ms >= 500) {
                    if (net_operate_type == 0 && net_game->units.contains(net_operate) && net_game->corpses.end() == std::ranges::find(net_game->corpses, net_operate))
                        net_game->trade_request(net_operate);            // a player, not our corpse: a trade
                    else net_game->interact(net_operate_type, net_operate);
                    if (net_operate_waypoint >= 0) net_game->waypoint(net_operate, net_operate_waypoint);   // in reach and 10 s on from a level change, it goes at once
                    if (net_operate_waypoint >= 0) { net_waypoint_object = net_operate; net_waypoint_level = net_operate_waypoint; net_waypoint_ms = frame_ms; net_waypoint_tries = 0; net_game->waypoint_menu = 0; }
                    net_operate = 0;
                    net_operate_waypoint = -1;
                    if (net_operate_moves) net_moving_until = frame_ms + 5000;
                }
            }
            // Until the host moves us: its menu open (0x63) takes the level
            // (0x49); out of reach it walks us up and stops, so both again
            // (live 2026-10-18: a 0x49 before the menu, or within 10 s of a
            // level change, is dropped).
            if (net_waypoint_level >= 0) {
                if (net_warp_to < 0) {                               // there: the reassign took us
                    net_waypoint_level = -1;
                } else if (net_game->waypoint_menu == net_waypoint_object) {
                    net_game->waypoint(net_waypoint_object, net_waypoint_level);   // pending till the reassign: the retries below if it's dropped
                    net_game->waypoint_menu = 0;
                    net_waypoint_ms = frame_ms;
                } else if (frame_ms - net_waypoint_ms > 3000 && net_waypoint_tries < 3 && net_game->steady_now() - net_game->walked_ms >= 500) {
                    // Out of its reach the host walks us up (FUN_00548a50) and
                    // stops: again from there.
                    net_game->interact(2, net_waypoint_object);
                    net_game->waypoint(net_waypoint_object, net_waypoint_level);
                    net_waypoint_ms = frame_ms;
                    ++net_waypoint_tries;
                } else if (frame_ms - net_waypoint_ms > 3500) {
                    net_game->log.note("the host didn't open waypoint " + std::to_string(net_waypoint_object));
                    net_waypoint_level = -1;
                    net_warp_to = -1;
                }
            }
            // The host's doors (0x0e: opened by anyone, a monster too): d2d's
            // door nearest the host's object, to its mode and footprint.
            for (const auto& [id, mode] : net_game->object_modes) {
                const auto object = net_game->units.find(std::uint64_t(2) << 32 | id);
                if (object == net_game->units.end() || mode < 0 || mode > 2) continue;
                int door = -1;
                float best = 4.f;
                for (std::size_t i = 0; i < level->npcs.size(); ++i) {
                    const auto& npc = level->npcs[i];
                    if (npc.root != "objects" || !World::is_door(npc.operate_fn)) continue;
                    if (const float apart = std::hypot((npc.x + float(level->world_x)) * 5.f - object->second.x, (npc.y + float(level->world_y)) * 5.f - object->second.y); apart < best) { best = apart; door = int(i); }
                }
                if (door >= 0) world.set_door_mode(door, mode, frame_ms);
            }
            net_game->object_modes.clear();
            // A pick-up walked to: asked for once both of us are there.
            if (net_pick >= 0) {
                const auto found = net_game->ground.find(std::uint32_t(net_pick));
                if (found == net_game->ground.end()) {
                    net_pick = -1;
                } else if (std::hypot(net_game->host_x - float(found->second.x), net_game->host_y - float(found->second.y)) < 4.f
                           || (!hero.player.walking && std::hypot((hero.player.x + float(level->world_x)) * 5.f - float(found->second.x),
                                                                   (hero.player.y + float(level->world_y)) * 5.f - float(found->second.y)) < 4.f)) {
                    net_game->pick_up(found->first);
                    net_pick = -1;
                }
            }
            // A warp d2d's player set off for: the host's warp unit there (0x13).
            if (hero.take_warp >= 0 && hero.take_warp != net_warp_sent && std::size_t(hero.take_warp) < level->warps.size()) {
                const auto& warp = level->warps[std::size_t(hero.take_warp)];
                if (const auto* unit = net_game->nearest(5, -1, (warp.unit_x + float(level->world_x)) * 5.f, (warp.unit_y + float(level->world_y)) * 5.f, 15.f)) {
                    net_operate_type = 5; net_operate = unit->id; net_operate_ms = frame_ms; net_operate_moves = true; net_operate_waypoint = -1;   // as an object's: once the host has us there, stopped
                    // Its mouth: where one comes out (the free spot nearest the unit,
                    // then ExitWalk, FUN_005550b0). The unit sits in the rocks, and
                    // the host's walk to it (FUN_00548a50) stalls from the wrong side;
                    // game.exe's client walks round to the mouth first (live 2026-10-17).
                    const auto [free_x, free_y] = level->nearest_free(warp.unit_x, warp.unit_y);
                    net_warp_x = (free_x + warp.exit_x + float(level->world_x)) * 5.f;
                    net_warp_y = (free_y + warp.exit_y + float(level->world_y)) * 5.f;
                    net_warp_to = warp.destination;
                }
            }
            net_warp_sent = hero.take_warp;
        }
        // The skill buttons: a change goes to the World (0x3c), which runs a
        // right-button aura (a Paladin's).
        if (std::uint32_t(skillbar.left) != character.header.left_skill) net.send(cmd::SelectSkill{ skillbar.left, true });
        if (std::uint32_t(skillbar.right) != character.header.right_skill || (view.aura != 0) != (scene->skills.get(skillbar.right) && scene->skills.get(skillbar.right)->aura))
            net.send(cmd::SelectSkill{ skillbar.right, false });
        // The skill shrine's +all skills while its boost lasts.
        skillbar.extra.clear();
        for (const auto& [id, value] : view.boost) if (id == d2d::d2s::kAllSkills) skillbar.extra.push_back({ .stat = d2d::d2s::kAllSkills, .value = value });
        // The NPC the client's talking with (it stands meanwhile), when that changes.
        if (const int talk = npc_menu.npc >= 0 ? npc_menu.npc : speech.npc >= 0 ? speech.npc : store.npc; talk != talking_sent) {
            net.send(cmd::Chat{ talk });
            talking_sent = talk;
        }
        // A joined game, as game.exe's client: 0x2f opens a talk, 0x30 shuts
        // it (the host's quests' "talk closed", FUN_00543d50). Checked every
        // frame, so a talk opened before the host's NPC is known opens once it is.
        if (net_game) {
            namespace c2s = d2d::net::d2gs::c2s;
            if (const auto* waited = net_interact_pending >= 0 ? net_npc(net_interact_pending) : nullptr) {
                net_trade_pending = int(waited->id);
                net_trade_kind = -1; net_trade_extra = 0;
                net_trade_ms = frame_now;
                net_trade_asked = false;
                net_interact_pending = -1;
            }
            const int talk = talking_sent;
            const auto* host_npc = talk >= 0 && level && std::size_t(talk) < level->npcs.size() && level->npcs[std::size_t(talk)].root != "objects" ? net_npc(talk) : nullptr;
            if (net_chat_npc && (!host_npc || host_npc->id != net_chat_npc)) { net_game->send_items({ c2s::npc_chat(false, 1, net_chat_npc) }); net_chat_npc = 0; }
            if (host_npc && host_npc->id != net_chat_npc) { net_chat_npc = host_npc->id; net_game->send_items({ c2s::npc_chat(true, 1, net_chat_npc) }); }
        }
        for (const auto& command : input(mouse, over_ui)) send(command);
        // The screen's pad plays the screen's player (local co-op: a partner's while they have it).
        const Send to_world = [this](const Command& command) { send(command); };
        const bool field = pad_field() && !game_menu.open && !chat_typing && !gold_box;
        pad_walk(pad, focused().player, view.dead && !view.ghost, field, to_world);   // a ghost walks
        pad_act(pad, focused().player, view.dead, pad_field(), skillbar.left, skillbar.right, to_world, skillbar.left_item, skillbar.right_item);
        // Fixed ticks of kTickMs; after a stall, a few to catch up, then the
        // clock skips ahead (game.exe catches up one frame at most).
        if (world_ms == 0 || frame_ms - world_ms > 1000) world_ms = frame_ms - std::min<std::uint32_t>(frame_ms - last_ms, kTickMs);
        // A local game pauses under the game menu (FUN_0044efa0: game mode
        // 0 / 1, UI 9 or 0xb up, the player in a room): it draws and plays
        // sounds, the server's frame (FUN_0052fc20) doesn't run.
        if (!net_game && (game_menu.open || controls.open)) world_ms = frame_ms;
        bool ticked = false;
        for (int ticks = 0; frame_ms - world_ms >= kTickMs && ticks < 5; ++ticks) {
            prev_x = view.player.x; prev_y = view.player.y;
            std::vector<Order> orders;
            for (auto& command : net.receive()) orders.push_back({ focus_id(), std::move(command) });   // the screen's player's
            for (auto& command : lead_orders) orders.push_back({ hero.id, std::move(command) });       // the first player's field play meanwhile
            lead_orders.clear();
            for (auto& coop_player : coop) {                // the other co-op players' (their pads')
                for (auto& command : coop_player.orders) orders.push_back({ coop_player.id, std::move(command) });
                coop_player.orders.clear();
            }
            world.tick(orders, world_ms + kTickMs, world_ms);
            world_ms += kTickMs;
            ticked = true;
        }
        if (!ticked && view.level == world.level) return;
        publish();
        // What the World said, handled once the View it came in is here
        // (the hire list's offers come with it).
        for (const auto& event : view.events) handle(event, audio, frame_ms);
        // A partner's: a window to open (an NPC talked to, the stash) takes
        // the screen when it's free; the rest is the screen's business only.
        for (auto& other : world.players) {
            if (other->id == focus_id()) continue;
            const auto said = std::exchange(other->events, {});
            for (const auto& event : said)
                if (std::holds_alternative<ev::MusicCue>(event)) handle(event, audio, frame_ms);
            const auto opens = std::ranges::find_if(said, [](const Event& event) { return std::holds_alternative<ev::OpenUI>(event); });
            if (opens == said.end() || !focus_free()) continue;
            take_focus(other->id);
            handle(*opens, audio, frame_ms);
            break;
        }
        if (net_game) net_quests(frame_ms);
        if (frame_ms - world_ms >= kTickMs) world_ms = frame_ms - (frame_ms - world_ms) % kTickMs;
    }

auto Town::handle(const Event& event, Audio& audio, std::uint32_t frame_ms) -> void {
        if (const auto* level_changed = std::get_if<ev::LevelChanged>(&event)) {
            net_interact_pending = -1;                    // its index was the old level's
            if (level_changed->from->layer != level->layer) {
                // A layer change saves the old layer's map and loads the
                // new one's (FUN_00458d40).
                save_automap(automap, automap_dir(world), character.name, scene->map_seed, level_changed->from->layer);
                const bool seen = other_automaps.count(level->layer) != 0;
                other_automaps[level_changed->from->layer] = std::move(automap);
                automap = std::move(other_automaps[level->layer]);
                if (!seen) load_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer, int(scene->automap_cels.frames_per_direction()));
                if (level_changed->keep_map) automap.open = other_automaps[level_changed->from->layer].open;
            }
            hovered_npc = -1;
            // The slide carries on in the new level's cells (a warp's
            // distance still snaps).
            prev_x += float(level_changed->from->world_x - level->world_x);
            prev_y += float(level_changed->from->world_y - level->world_y);
            npc_menu = {}; store = {}; speech = {}; waypoint = {}; menu_after_speech = -1;
            shake = {}; quest_quakes.clear(); audio.stop_quake();
            level_ms = frame_ms;                                  // its song comes in 3 s later
            rain.reassign(rain.rng, level->id, d2d::rules::level_ids::act_of(level->id));   // the host's 0x15 for the warp
            return;
        }
        if (const auto* quake = std::get_if<ev::QuestQuake>(&event)) { quest_quakes.push_back({ quake->kind, frame_ms }); return; }
        if (std::holds_alternative<ev::Lightning>(event)) { rain.set_off(); return; }
        if (std::holds_alternative<ev::Reassigned>(event)) { rain.reassign(rain.rng, level->id, d2d::rules::level_ids::act_of(level->id)); return; }
        if (const auto* news = std::get_if<ev::QuestNews>(&event)) { quest_log_notify(quest_log, news->quest); return; }
        if (const auto* music = std::get_if<ev::MusicCue>(&event)) { audio.play_sfx(*scene, music->sound, float(audio.music_volume) / 100.f, 0); return; }
        if (const auto* object_speech = std::get_if<ev::Speech>(&event)) {   // FUN_004a1600 mode 2: the speech box, heard at once
            speech = start_speech(*scene, object_speech->npc, std::uint16_t(object_speech->string), frame_ms);
            send(cmd::QuestMessage{ object_speech->npc, object_speech->string });
            menu_after_speech = -1;
            return;
        }
        const auto& open_ui = std::get<ev::OpenUI>(event);
        if (open_ui.kind == ev::OpenUI::stash) { stash_open = inv_open = true; char_open = quest_log.open = false; return; }
        if (open_ui.kind == ev::OpenUI::trade) { inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; return; }
        if (open_ui.kind == ev::OpenUI::hire) {
            npc_menu = open_hire_menu(*scene, open_ui.npc, hire_offers, character.stats.get(d2d::d2s::kGold) + character.stats.get(d2d::d2s::kGoldBank));
            return;
        }
        if (open_ui.kind == ev::OpenUI::waypoint) {
            waypoint = { .open = true, .tab = view_act(), .npc = open_ui.npc };   // on the act's own tab
            inv_open = char_open = stash_open = cube_open = quest_log.open = false;
            return;
        }
        // A joined game's lines are the host's (0x27, net_quests): ones that
        // came first, else none till they come.
        auto lines = open_ui.quest;
        if (net_game) {
            const auto* host_npc = net_npc(open_ui.npc);
            const bool early = host_npc && host_npc->id == net_early_quest.id && world_ms - net_early_ms < 3000;
            lines = early ? net_early_quest.lines : std::vector<d2d::rules::QuestMsg>{};
            net_early_quest = {};
        }
        open_menu(open_ui.npc);
        take_npc_quest(open_ui.npc, std::move(lines), frame_ms);
    }

}  // namespace d2d::client
