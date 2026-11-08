// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): hosting d2d players over the LAN (docs/design/lan-host.md). A joiner
// is a World player like a local co-op partner; its commands come over the socket and
// its own View goes back each tick. The rest is in this directory.
#include "town.hpp"

#include "load.hpp"

#include <log.hpp>
#include <protocol.hpp>
#include <replication.hpp>

#include <algorithm>
#include <span>

namespace d2d::client {

auto Town::lan_poll() -> void {
        if (!lan_host || !scene) return;
        for (auto& event : lan_host->poll()) {
            using Type = d2d::net::lan::Host::Event::Type;
            if (event.type == Type::Joined) {
                auto joining = character_of_bytes(*scene, std::as_bytes(std::span(event.hello.save)));
                if (!joining) { lan_host->kick(event.client, "the host couldn't read your character"); continue; }
                if (world.players.size() >= 4) { lan_host->kick(event.client, "the game is full"); continue; }
                const int seat_id = world.join(*joining).id;
                lan_players.push_back({ .client = event.client, .id = seat_id, .view_enc = {}, .orders = {} });
                d2d::log::info("lan: {} joined as player {} (client {})", joining->name, seat_id, event.client);
            } else if (event.type == Type::Command) {
                const auto found = std::ranges::find(lan_players, event.client, &LanPlayer::client);
                if (found == lan_players.end()) continue;
                if (auto command = d2d::game::decode(event.command)) found->orders.push_back(std::move(*command));
                else d2d::log::warn("lan: client {} sent a command that doesn't decode ({} bytes)", event.client, event.command.size());
            } else if (event.type == Type::Left) {
                const auto found = std::ranges::find(lan_players, event.client, &LanPlayer::client);
                if (found == lan_players.end()) continue;
                // ponytail: the save isn't sent back yet; the World's leave writes it here, the host's side.
                const auto saved = world.leave(found->id);
                d2d::log::info("lan: player {} left ({}){}", found->id, event.reason, saved.empty() ? "" : "; save: " + saved);
                lan_players.erase(found);
            }
        }
    }

auto Town::lan_publish() -> void {
        if (!lan_host || !scene) return;
        for (auto& lan_player : lan_players) {
            const auto found = std::ranges::find_if(world.players, [&](const auto& seated) { return seated->id == lan_player.id; });
            if (found == world.players.end()) continue;
            world.act_as(**found);
            auto out = world.view();
            world.act_as(world.lead());
            // ponytail: the World's sound cues are the screen's (publish takes them); a joiner hears none yet.
            out.events = std::move((*found)->events);
            (*found)->events.clear();
            const auto bytes = encode_view(*scene, out, lan_player.view_enc);
            if (!lan_host->send_view(lan_player.client, bytes)) lan_player.view_enc.reset();   // gone: lan_poll drops it
        }
    }

}  // namespace d2d::client
