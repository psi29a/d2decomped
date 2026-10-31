// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs for joined games, local co-op and chat
// (docs/control_channel.md). In name order: a new one goes where it sorts.

#include "debug.hpp"

#include <join.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>

namespace d2d::client {

void DebugVerbs::add_net() {
    add("alt", [this](const Args& args) -> Reply {   // hold "Show Items" (Alt) as if pressed: `debug alt on|off`
        town.alt_held = args.size() >= 3 && args[2] == "on";
        return std::string("ok\n");
    });
    add("chat", [this](const Args& args) -> Reply {   // a chat line shown as received (its look; no one is told)
        if (args.size() < 3) return std::nullopt;
        std::string text = args[2];
        for (std::size_t i = 3; i < args.size(); ++i) text += " " + args[i];
        town.add_chat(text);
        return std::string("ok\n");
    });
    add("net", [this](const Args& /*args*/) -> Reply {   // a joined game: where the host has us, where d2d has us (act subtiles), and how far apart
        if (!town.net_game || !town.level) return std::nullopt;
        const float local_x = (town.hero.player.x + float(town.level->world_x)) * 5.f, local_y = (town.hero.player.y + float(town.level->world_y)) * 5.f;
        std::string out = std::format("host {:.0f} {:.0f} local {:.0f} {:.0f} apart {:.1f} units {} trade {} self {}\n", town.net_game->host_x, town.net_game->host_y, local_x, local_y,
                                      std::hypot(town.net_game->host_x - local_x, town.net_game->host_y - local_y), town.net_game->units.size(), town.net_game->trade.state, town.net_game->self_id);
        for (const auto& other : town.view.others)   // the other players: id, name, cell
            out += std::format("player\t{}\t{}\t{:.1f}\t{:.1f}\n", other.id, other.name, other.unit.x, other.unit.y);
        return out + "ok\n";
    });
    add("netsend", [this](const Args& args) -> Reply {   // a raw C->S packet to the host, hex bytes (live checks)
        if (args.size() < 3 || !town.net_game) return std::nullopt;
        d2d::net::Bytes packet;
        for (std::size_t i = 2; i < args.size(); ++i) {
            char* end = nullptr;
            const unsigned long byte = std::strtoul(args[i].c_str(), &end, 16);
            if (args[i].empty() || *end != '\0' || byte > 0xff) return std::string("err not a hex byte: ") + args[i] + "\n";
            packet.push_back(std::uint8_t(byte));
        }
        town.net_game->send_items({ packet });
        return std::string("ok\n");
    });
    add("party", [this](const Args& args) -> Reply {   // the Party screen with three sample players (no joined game needed): `debug party on|off`
        town.party_preview.clear();
        if (args.size() >= 3 && args[2] == "on")
            town.party_preview = { { 2, "Ally", 3, 25, 1, 3, 0, "Rogue Encampment", false }, { 3, "Stranger", 4, 12, 0xffff, 0, 1, {}, false },
                                   { 4, "Rival", 6, 40, 0xffff, 2, 8, {}, true } };
        town.party.open = !town.party_preview.empty();
        return std::string("ok\n");
    });
    add("players", [this](const Args& /*args*/) -> Reply {   // the game's players (local co-op): id, name, cell, seat (-1: the first player's), mode (-1 none)
        std::string out;
        for (const auto& player : town.world.players) {
            const auto coop_player = std::ranges::find(town.coop, player->id, &Town::CoopPlayer::id);
            out += std::format("{}\t{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}\n", player->id, player->character.name, player->player.x, player->player.y,
                               coop_player == town.coop.end() ? -1 : coop_player->seat, player->pmode, int(player->ghost));
        }
        return out + "ok\n";
    });
    add("roster", [this](const Args& /*args*/) -> Reply {   // the joining pads' rosters: seat, stage, pick, entries (* greyed)
        std::string out;
        for (const auto& coop_player : town.coop) {
            if (!coop_player.roster) continue;
            const auto& roster = *coop_player.roster;
            out += std::format("{}\t{}\t{}\t", coop_player.seat, int(roster.stage), roster.pick);
            for (const auto& entry : roster.entries) out += (entry.usable ? "" : "*") + entry.name + ",";
            out += "\t" + roster.name + "\n";
        }
        return out + "ok\n";
    });
    add("tradestate", [this](const Args& args) -> Reply {   // a joined game: the trade's state, set here (its window's look; the host isn't told)
        if (args.size() < 3 || !town.net_game) return std::nullopt;
        town.net_game->trade.state = std::atoi(args[2].c_str());
        return std::string("ok\n");
    });
    add("typing", [this](const Args& args) -> Reply {   // the chat edit box open with this text (its look; nothing is sent)
        std::string text;
        for (std::size_t i = 2; i < args.size(); ++i) text += (i > 2 ? " " : "") + args[i];
        town.chat_typing = text;
        return std::string("ok\n");
    });
}

}  // namespace d2d::client
