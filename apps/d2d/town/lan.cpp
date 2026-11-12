// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): hosting d2d players over the LAN (docs/design/lan-host.md). A joiner
// is a World player like a local co-op partner; its commands come over the socket and
// its own View goes back each tick. The rest is in this directory.
#include "town.hpp"

#include "audio.hpp"
#include "load.hpp"
#include "ui.hpp"

#include <cues.hpp>
#include <gamedata.hpp>
#include <lan.hpp>
#include <log.hpp>
#include <protocol.hpp>
#include <replication.hpp>
#include <rules.hpp>
#include <sound_mix.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::client {

namespace {
// A joiner's save goes back this often while they play, so a dead machine loses little.
// D2D_LAN_SAVE_SECONDS shortens it (tests).
auto lan_save_ms() -> std::uint32_t {
    if (const char* seconds = std::getenv("D2D_LAN_SAVE_SECONDS")) return std::uint32_t(std::max(1, std::atoi(seconds))) * 1000u;
    return 5u * 60u * 1000u;
}
auto steady_ms() -> std::uint32_t {
    return std::uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

auto Town::lan_poll() -> void {
        if (!lan_host || !scene) return;
        d2d::net::lan::Bytes info(9);
        for (int shift = 0; shift < 32; shift += 8) info[std::size_t(shift / 8)] = std::uint8_t(scene->map_seed >> shift);
        info[4] = std::uint8_t(scene->difficulty);
        info[5] = std::uint8_t(world.act());
        lan_host->welcome_info = std::move(info);
        for (auto& event : lan_host->poll()) {
            using Type = d2d::net::lan::Host::Event::Type;
            if (event.type == Type::Joined) {
                auto joining = character_of_bytes(*scene, std::as_bytes(std::span(event.hello.save)));
                if (!joining) { lan_host->kick(event.client, "the host couldn't read your character"); continue; }
                if (world.players.size() >= 4) { lan_host->kick(event.client, "the game is full"); continue; }
                // game.exe's join checks (FUN_00539760, net-join.md): a dead hardcore character (0x15),
                // softcore into hardcore (0x13), hardcore into softcore (0x14); their texts (tcpip-menu.md).
                const bool hardcore_game = hero.character.header.hardcore();
                if (joining->header.hardcore() && joining->header.died()) { lan_host->kick(event.client, string_id(*scene, 0x14ef)); continue; }
                if (joining->header.hardcore() != hardcore_game) { lan_host->kick(event.client, string_id(*scene, hardcore_game ? 0x14f2 : 0x14f1)); continue; }
                auto& seated = world.join(*joining);
                seated.own_screen = true;                 // on their own machine: their own level (World::split_levels), the host's party stays one screen
                const int seat_id = seated.id;
                const auto& raw = event.hello.save;
                lan_players.push_back({ .client = event.client, .id = seat_id, .view_enc = {}, .orders = {},
                                        .save = std::vector<std::byte>(reinterpret_cast<const std::byte*>(raw.data()), reinterpret_cast<const std::byte*>(raw.data()) + raw.size()),
                                        .saved_ms = steady_ms() });
                d2d::log::info("lan: {} joined as player {} (client {})", joining->name, seat_id, event.client);
                world.announce({ ev::Message::joined, joining->name, {} });
            } else if (event.type == Type::Command) {
                const auto found = std::ranges::find(lan_players, event.client, &LanPlayer::client);
                if (found == lan_players.end()) continue;
                if (auto command = d2d::game::decode(event.command)) found->orders.push_back(std::move(*command));
                else d2d::log::warn("lan: client {} sent a command that doesn't decode ({} bytes)", event.client, event.command.size());
            } else if (event.type == Type::Resync) {
                const auto found = std::ranges::find(lan_players, event.client, &LanPlayer::client);
                if (found == lan_players.end()) continue;
                found->view_enc.reset();              // the next View is whole
                d2d::log::info("lan: player {} asked for a whole View", found->id);
            } else if (event.type == Type::Left) {
                const auto found = std::ranges::find(lan_players, event.client, &LanPlayer::client);
                if (found == lan_players.end()) continue;
                if (event.goodbye) {                  // still connected: their last save, then close
                    lan_send_save(*found);
                    lan_host->close(event.client);
                }
                lan_remove(*found, event.reason);
                lan_players.erase(found);
            }
        }
        const auto now = steady_ms();
        for (auto& lan_player : lan_players)
            if (now - lan_player.saved_ms >= lan_save_ms()) lan_send_save(lan_player);
    }

auto Town::lan_send_save(LanPlayer& lan_player) -> bool {
        lan_player.saved_ms = steady_ms();
        const auto found = std::ranges::find_if(world.players, [&](const auto& seated) { return seated->id == lan_player.id; });
        if (found == world.players.end()) return false;
        world.act_as(**found);
        auto bytes = world.save_bytes(lan_player.save);
        world.act_as(world.lead());
        if (!bytes) { d2d::log::warn("lan: player {}'s save: {}", lan_player.id, bytes.error()); return false; }
        const auto sent = lan_host->send_save(lan_player.client, std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()));
        if (sent) lan_player.save = std::move(*bytes);
        d2d::log::info("lan: player {}'s save sent ({} bytes){}", lan_player.id, lan_player.save.size(), sent ? "" : ": not delivered");
        return sent;
    }

auto Town::lan_remove(LanPlayer& lan_player, const std::string& why) -> void {
        const auto* saved_store = std::exchange(world.characters, nullptr);   // their save is theirs: none written here
        if (const auto found = std::ranges::find_if(world.players, [&](const auto& seated) { return seated->id == lan_player.id; }); found != world.players.end())
            world.announce({ ev::Message::left, (*found)->character.name, {} });
        world.leave(lan_player.id);
        world.characters = saved_store;
        d2d::log::info("lan: player {} left ({})", lan_player.id, why);
    }

auto Town::lan_leave() -> void {
        if (!lan_client || !lan_client->connected()) return;
        lan_client->leave();
        // The host answers with our last save and closes; wait a little for it.
        for (int wait = 0; wait < 300 && lan_client->connected(); ++wait) {
            for (auto& event : lan_client->poll()) {
                if (event.type == d2d::net::lan::Client::Event::Type::Save && lan_store) {
                    const auto why = lan_store->save_bytes(character.name, std::as_bytes(std::span(event.view)));
                    d2d::log::info("lan: the host's save of {}: {}", character.name, why.empty() ? "written" : why);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        lan_client.reset();
    }

auto Town::lan_publish() -> void {
        if (!lan_host || !scene) return;
        for (auto& lan_player : lan_players) {
            const auto found = std::ranges::find_if(world.players, [&](const auto& seated) { return seated->id == lan_player.id; });
            if (found == world.players.end() || !world.here(**found)) continue;   // split levels: this turn's level only (a leaver: on its new level's)
            world.act_as(**found);
            auto out = world.view();
            world.act_as(world.lead());
            // The tick's sounds (publish takes them after) that this joiner could hear where it stands,
            // as delays from now: its clock isn't ours, it adds its own (lan_client_frame).
            for (const auto& cue : std::span(world.cues.due).subspan(std::min(lan_cue_from, world.cues.due.size()))) {   // this turn's
                if (cue.sound <= 0 || std::size_t(cue.sound) >= world.game_data->sounds.size()) continue;
                const auto& row = world.game_data->sounds[std::size_t(cue.sound)];
                const auto [screen_x, screen_y2] = d2d::rules::screen_offset(cue.x - out.player.x, cue.y - out.player.y);
                if (!d2d::rules::sound_mix_pixels(std::clamp(row.volume, 0, 255), row.falloff, screen_x, screen_y2).audible) continue;
                auto heard = cue;
                heard.when_ms = cue.when_ms > world_ms ? cue.when_ms - world_ms : 0;
                out.sounds.push_back(heard);
            }
            out.events = std::move((*found)->events);
            (*found)->events.clear();
            const auto bytes = encode_view(*scene, out, lan_player.view_enc);
            if (!lan_host->send_view(lan_player.client, bytes)) lan_player.view_enc.reset();   // gone: lan_poll drops it
        }
        lan_cue_from = world.cues.due.size();
    }

// Each level's list to the host's keys: what's there and the same stays (its
// anims are loaded), the rest is made as the host made it: an NPC from its
// MonStats recipe, an object from objects.txt at the host's spot. A shrine's
// kind or a chest's lock aren't the host's rolls (the host resolves them).
auto Town::lan_take_npcs() -> void {
        const GameData& game_data = *world.game_data;
        const auto same = [](const Npc& npc, const View::NpcKey& key) {
            const bool who = npc.root == "objects" ? key.object_id == npc.object_id && key.hc_idx < 0 : key.object_id == 0 && key.hc_idx == npc.hc_idx;
            return who && std::abs(npc.x - key.x) < 0.5f && std::abs(npc.y - key.y) < 0.5f;
        };
        for (std::size_t at = 0; at < view.npc_keys.size();) {
            const int level_id = view.npc_keys[at].level;
            std::size_t end = at;
            while (end < view.npc_keys.size() && view.npc_keys[end].level == level_id) ++end;
            const auto keys = std::span(view.npc_keys).subspan(at, end - at);
            at = end;
            const Level* found = game_data.level(level_id);
            if (!found) continue;
            auto& into = const_cast<Level&>(*found);          // GameData owns its levels mutable (gamedata.cpp)
            std::size_t kept = 0;
            while (kept < keys.size() && kept < into.npcs.size() && same(into.npcs[kept], keys[kept])) ++kept;
            if (kept == keys.size() && into.npcs.size() == keys.size()) continue;
            into.npcs.resize(kept);
            for (const auto& key : keys.subspan(kept)) {
                const std::size_t before = into.npcs.size();
                if (key.object_id > 0 && game_data.builder) {
                    d2d::rules::Rng rgn{ std::uint32_t(key.object_id) ^ std::uint32_t(key.x * 5) << 16 ^ std::uint32_t(key.y * 5) };   // the rolls are the host's; this keeps ours the same each time
                    add_object(game_data, game_data.builder->objects, game_data.builder->obj_row, into, key.object_id, int(key.x * 5), int(key.y * 5), rgn);
                } else if (key.hc_idx >= 0 && std::size_t(key.hc_idx) < game_data.mon_bin.size() && game_data.mon_bin[std::size_t(key.hc_idx)] < game_data.mon_npc.size()) {
                    auto npc = game_data.mon_npc[game_data.mon_bin[std::size_t(key.hc_idx)]];
                    if (!npc.code.empty()) into.npcs.push_back(std::move(npc));
                }
                if (into.npcs.size() == before) {            // nothing to make of it (the host had the same tables, so hardly ever): the index stays
                    Npc blank;
                    blank.root = key.object_id > 0 ? "objects" : "monsters";
                    blank.object_id = key.object_id; blank.hc_idx = key.hc_idx;
                    into.npcs.push_back(std::move(blank));
                }
                auto& made = into.npcs.back();
                made.x = key.x; made.y = key.y; made.palace = key.palace; made.quest = key.quest;
            }
            d2d::log::info("lan: level {}'s NPC list laid out as the host's: {} kept, {} made", level_id, kept, keys.size() - kept);
        }
    }

auto Town::lan_client_frame(const Mouse& mouse, Audio& audio, std::uint32_t frame_ms) -> void {
        if (!lan_client) return;
        for (auto& event : lan_client->poll()) {
            using Type = d2d::net::lan::Client::Event::Type;
            if (event.type == Type::View) {
                // A View that doesn't apply (TCP keeps them in order, so a bug) leaves us behind the
                // host's encoder: ask once for a whole one, drop the rest until one applies.
                // D2D_LAN_BAD_VIEW=n (tests): the nth View arrives cut in half, as a bug would send it.
                static const int bad_view = std::getenv("D2D_LAN_BAD_VIEW") ? std::atoi(std::getenv("D2D_LAN_BAD_VIEW")) : 0;
                if (++lan_views == bad_view) { event.view.resize(event.view.size() / 2); d2d::log::info("lan: View {} cut (D2D_LAN_BAD_VIEW)", lan_views); }
                const auto queued = cues.due.size();
                if (!take_view(event.view, nullptr)) {
                    if (!std::exchange(lan_resyncing, true)) { d2d::log::info("lan: asking the host for a whole View"); lan_client->send_resync(); }
                    continue;
                }
                lan_resyncing = false;
                for (auto cue = cues.due.begin() + std::ptrdiff_t(queued); cue != cues.due.end(); ++cue) cue->when_ms += frame_ms;   // the host's delays, on our clock
                if (std::getenv("D2D_LAN_SOUNDS") && cues.due.size() > queued) d2d::log::info("lan: {} sounds from the host", cues.due.size() - queued);
                for (const auto& said : view.events) {
                    if (const auto* refused = std::get_if<ev::TradeRefused>(&said); refused && store.npc >= 0) npc_menu = open_trade_refused(*scene, store.npc, refused->result, mouse.x, mouse.y);
                    handle(said, audio, frame_ms);
                }
            } else if (event.type == Type::Save) {
                const std::string why = lan_store ? lan_store->save_bytes(character.name, std::as_bytes(std::span(event.view))) : std::string("no save folder");
                d2d::log::info("lan: the host's save of {}: {}", character.name, why.empty() ? "written" : why);
            } else if (event.type == Type::Refused || event.type == Type::Lost) {
                lan_lost = event.reason;
                d2d::log::warn("lan: out of the host's game: {}", event.reason);
            }
        }
    }

}  // namespace d2d::client
