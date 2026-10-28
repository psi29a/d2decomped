// SPDX-License-Identifier: GPL-3.0-or-later
// The devctl verbs that drive and read the game for scripted tests
// (docs/control_channel.md): input (click, key, move, wheel), debug
// set-ups, and reads of the game's state (state, items, npcs, monsters,
// ground, menu). The frame-level ones (info, screenshot, quit) stay in
// main.cpp.
#pragma once

#include "audio.hpp"
#include "common.hpp"
#include "frontend.hpp"
#include "panels.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "town.hpp"
#include "ui.hpp"
#include "window.hpp"

#include <d2s_items.hpp>
#include <gamedata_load.hpp>
#include <devctl.hpp>
#include <light.hpp>
#include <object_ids.hpp>
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
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace d2d::client {

// A screen's name in `state` replies.
const char* screen_name(Screen screen) {
    switch (screen) {
        case Screen::Title:      return "title";
        case Screen::Credits:    return "credits";
        case Screen::CharSelect: return "charselect";
        case Screen::CharCreate: return "charcreate";
        case Screen::InGame:     return "ingame";
        case Screen::Video:      return "video";
        case Screen::Cinematics: return "cinematics";
        case Screen::Endgame:    return "endgame";
        case Screen::OtherMultiplayer: return "othermultiplayer";
        case Screen::TcpIp:      return "tcpip";
    }
    return "?";
}

// Registers them on `ch`. They capture main()'s loop locals by reference;
// the channel is only pumped inside that loop, so the captures never
// outlive it.
void register_game_verbs(d2d::devctl::Channel& channel, Window& win, Screen& screen, CharSelectUI& csu, CharCreateUI& character,
                         Town& town, std::optional<Scene>& scene, Audio& audio) {
    auto click_verb = [&window = win](const std::vector<std::string>& args, std::uint8_t button) {
        if (args.size() < 3) return std::string("err click <x> <y>\n");
        // Args are game pixels; queued events carry window coords and get
        // converted back by SDL_ConvertEventToRenderCoordinates on poll.
        float x = 0, y = 0;
        SDL_RenderCoordinatesToWindow(window.renderer, std::stof(args[1]), std::stof(args[2]), &x, &y);
        // Motion + down + up land in the next frame's poll. One frame is
        // enough: update_button and the slot picker both accept a press
        // and release in the same frame.
        SDL_Event event{};
        event.motion = { .type = SDL_EVENT_MOUSE_MOTION, .windowID = SDL_GetWindowID(window.window), .x = x, .y = y };
        SDL_PushEvent(&event);
        for (auto type : { SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP }) {
            event = {};
            event.button = { .type = type, .windowID = SDL_GetWindowID(window.window),
                          .button = button,
                          .down = type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                          .clicks = 1, .x = x, .y = y };
            SDL_PushEvent(&event);
        }
        return std::string("ok\n");
    };
    // By value: click_verb is this function's local (a [&] would dangle).
    channel.on("click", [click_verb](const std::vector<std::string>& verb_args) { return click_verb(verb_args, SDL_BUTTON_LEFT); });
    channel.on("rclick", [click_verb](const std::vector<std::string>& verb_args) { return click_verb(verb_args, SDL_BUTTON_RIGHT); });
    // Save the character now (character_store.hpp), as leaving the game does.
    channel.on("save", [&](const std::vector<std::string>&) {
        if (screen != Screen::InGame) return std::string("err not in a game\n");
        const auto err = town.save();
        return err.empty() ? std::string("ok\n") : "err " + err + "\n";
    });
    // A command straight to the World, as a client sends one (protocol.hpp);
    // applied at its next tick.
    channel.on("cmd", [&](const std::vector<std::string>& verb_args) {
        auto float_arg = [&](std::size_t index) { return index < verb_args.size() ? std::stof(verb_args[index]) : 0.f; };
        auto int_arg = [&](std::size_t index, int fallback = -1) { return index < verb_args.size() ? std::atoi(verb_args[index].c_str()) : fallback; };
        const std::string verb = verb_args.size() > 1 ? verb_args[1] : "";
        if (verb == "move" && verb_args.size() >= 4) town.send(cmd::Move{ float_arg(2), float_arg(3), true });
        else if (verb == "skill" && verb_args.size() >= 5) town.send(cmd::UseSkill{ int_arg(2, 0), float_arg(3), float_arg(4), int_arg(5), int_arg(6, 0) != 0 });
        else if (verb == "interact" && verb_args.size() >= 3) town.send(cmd::Interact{ int_arg(2) });
        else if (verb == "pickup" && verb_args.size() >= 3) town.send(cmd::Pickup{ int_arg(2) });
        else if (verb == "resurrect") town.send(cmd::Resurrect{});
        else if (verb == "stat" && verb_args.size() >= 3) town.send(cmd::StatPoint{ int_arg(2, 0), int_arg(3, 1) });
        else if (verb == "skillpt" && verb_args.size() >= 3) town.send(cmd::SkillPoint{ int_arg(2, 0) });
        else if (verb == "select" && verb_args.size() >= 4) town.send(cmd::SelectSkill{ int_arg(2, 0), int_arg(3, 0) != 0 });
        else if (verb == "belt" && verb_args.size() >= 3) town.send(cmd::UseBelt{ int_arg(2, 0), verb_args.size() >= 4 && verb_args[3] == "merc" });
        else if (verb == "waypoint" && verb_args.size() >= 4) town.send(cmd::Waypoint{ int_arg(2), int_arg(3, 0) });
        else if (verb == "caravan" && verb_args.size() >= 3) town.send(cmd::Caravan{ int_arg(2) });
        else if (verb == "imbue" && verb_args.size() >= 3) town.send(cmd::Imbue{ int_arg(2) });
        else if (verb == "hand" && verb_args.size() >= 3) town.send(cmd::ToCursor{ int_arg(2) });
        else if (verb == "use" && verb_args.size() >= 3) town.send(cmd::UseItem{ int_arg(2) });
        else if (verb == "grid" && verb_args.size() >= 4)    // the held item into the inventory (or "cube") at col, row
            town.send(cmd::ToGrid{ verb_args.size() >= 5 && verb_args[4] == "cube" ? d2d::d2s::item_panel::kCube : d2d::d2s::item_panel::kInventory, int_arg(2), int_arg(3) });
        else if (verb == "transmute") town.send(cmd::Transmute{});
        else if (verb == "said" && verb_args.size() >= 4) town.send(cmd::QuestMessage{ int_arg(2), int_arg(3, 0) });
        else if (verb == "chat" && verb_args.size() >= 3) town.send(cmd::Chat{ int_arg(2) });
        else if (verb == "trade" && verb_args.size() >= 3) town.send(cmd::OpenTrade{ int_arg(2), int_arg(3, 0) != 0 });
        else if (verb == "buy" && verb_args.size() >= 3) town.send(cmd::Buy{ int_arg(2), town.store.tab });
        else if (verb == "sell" && verb_args.size() >= 3) town.send(cmd::Sell{ int_arg(2) });
        else if (verb == "close") town.send(cmd::CloseTrade{});
        else return std::string("err cmd move <x> <y> | skill <id> <x> <y> [unit] [left] | interact <npc> | pickup <unit> | resurrect"
                                " | stat <stat> [n] | skillpt <index> | select <skill> <left> | belt <slot> | waypoint <npc> <level>"
                                " | caravan <npc> | imbue <npc> | hand <item> | grid <col> <row> | use <item> | said <npc> <string> | chat <npc|-1>"
                                " | trade <npc> [gamble] | buy <stock> | sell <item> | close\n");
        return std::string("ok\n");
    });
    channel.on("key", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err key <name>\n");
        const SDL_Keycode key = SDL_GetKeyFromName(args[1].c_str());
        if (key == SDLK_UNKNOWN) return std::string("err unknown key\n");
        for (auto type : { SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP }) {
            SDL_Event event{};
            event.key.type = type;
            event.key.windowID = SDL_GetWindowID(win.window);
            event.key.key = key;
            event.key.down = type == SDL_EVENT_KEY_DOWN;
            SDL_PushEvent(&event);
        }
        return std::string("ok\n");
    });
    // A virtual pad (SDL's, plugged in on first use) holds a button (or a
    // trigger, all the way) down or lets it go; the next frame's sample
    // reads it, as a real pad's.
    channel.on("pad", [](const std::vector<std::string>& args) {
        // Virtual SDL pads 0..3, each plugged in at its first use: `pad [n] <button> down|up`,
        // `pad [n] axis <leftx|lefty|rightx|righty> <-1..1>`, `pad <n> plug [deck]` (deck: the
        // Steam Deck's own controls' ids), `pad <n> unplug`.
        static std::array<SDL_Joystick*, 4> virtual_pads{};
        std::size_t first = 1, which = 0;
        if (args.size() > 1 && args[1].size() == 1 && args[1][0] >= '0' && args[1][0] <= '3') { which = std::size_t(args[1][0] - '0'); first = 2; }
        const auto word = [&](std::size_t nth) { return first + nth < args.size() ? args[first + nth] : std::string(); };
        auto& handle = virtual_pads[which];
        const auto plug = [&](bool deck) -> std::string {
            if (handle) return {};
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.vendor_id = deck ? 0x28de : 0x0d2d;
            desc.product_id = deck ? 0x1205 : Uint16(0x0100 + which);   // one key per index: a replug is the same pad
            desc.name = deck ? "devctl Steam Deck pad" : "devctl pad";
            const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
            handle = id ? SDL_OpenJoystick(id) : nullptr;
            return handle ? std::string() : "err " + std::string(SDL_GetError()) + "\n";
        };
        if (word(0) == "plug") { const auto err = plug(word(1) == "deck"); return err.empty() ? std::string("ok\n") : err; }
        if (word(0) == "unplug") {
            if (handle) { const SDL_JoystickID id = SDL_GetJoystickID(handle); SDL_CloseJoystick(handle); SDL_DetachVirtualJoystick(id); handle = nullptr; }
            return std::string("ok\n");
        }
        if (word(0) == "axis") {
            const SDL_GamepadAxis stick = SDL_GetGamepadAxisFromString(word(1).c_str());
            if (stick == SDL_GAMEPAD_AXIS_INVALID || word(2).empty()) return std::string("err pad [n] axis <leftx|lefty|rightx|righty> <-1..1>\n");
            if (const auto err = plug(false); !err.empty()) return err;
            SDL_SetJoystickVirtualAxis(handle, stick, Sint16(std::clamp(std::stof(word(2)), -1.f, 1.f) * SDL_JOYSTICK_AXIS_MAX));
            return std::string("ok\n");
        }
        if (word(1) != "down" && word(1) != "up") return std::string("err pad [n] <button> down|up\n");
        const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(word(0).c_str());
        const SDL_GamepadAxis trigger = SDL_GetGamepadAxisFromString(word(0).c_str());
        const bool is_trigger = trigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || trigger == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        if (button == SDL_GAMEPAD_BUTTON_INVALID && !is_trigger) return std::string("err unknown button (a, b, x, y, dpleft, leftshoulder, lefttrigger, ...)\n");
        if (const auto err = plug(false); !err.empty()) return err;
        if (is_trigger) SDL_SetJoystickVirtualAxis(handle, trigger, word(1) == "down" ? SDL_JOYSTICK_AXIS_MAX : 0);
        else SDL_SetJoystickVirtualButton(handle, button, word(1) == "down");
        return std::string("ok\n");
    });
    channel.on("text", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err text <words>\n");
        static std::string typed;   // SDL keeps the pointer until the event is read
        typed.clear();
        for (std::size_t index = 1; index < args.size(); ++index) typed += (index > 1 ? " " : "") + args[index];
        SDL_Event event{};
        event.text = { .type = SDL_EVENT_TEXT_INPUT, .windowID = SDL_GetWindowID(win.window), .text = typed.c_str() };
        SDL_PushEvent(&event);
        return std::string("ok\n");
    });
    channel.on("move", [&](const std::vector<std::string>& args) {
        if (args.size() < 3) return std::string("err move <x> <y>\n");
        float x = 0, y = 0;
        SDL_RenderCoordinatesToWindow(win.renderer, std::stof(args[1]), std::stof(args[2]), &x, &y);
        SDL_Event event{};
        event.motion = { .type = SDL_EVENT_MOUSE_MOTION, .windowID = SDL_GetWindowID(win.window), .x = x, .y = y };
        SDL_PushEvent(&event);
        return std::string("ok\n");
    });
    channel.on("wheel", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err wheel <dy>\n");
        SDL_Event event{};
        event.wheel = { .type = SDL_EVENT_MOUSE_WHEEL, .windowID = SDL_GetWindowID(win.window),
                     .y = float(std::stoi(args[1])) };
        SDL_PushEvent(&event);
        return std::string("ok\n");
    });
    channel.on("debug", [&](const std::vector<std::string>& args) {
        if (args.size() >= 2 && args[1] == "automap" && scene) {    // reveal the whole level
            const auto& map = town.level->ds1;
            std::vector<DrawnCell> every;
            for (int y = 0; y < int(map.height()); ++y)
                for (int x = 0; x < int(map.width()); ++x) every.push_back({ x, y, nullptr });
            automap_reveal(*scene, *town.level, town.automap, every);
            return "ok " + std::to_string(town.automap.cells.size()) + "\n";
        }
        if (args.size() >= 2 && args[1] == "critters") {   // the client's critters: MonStats row, cell, walking, marked
            std::string out;
            for (const auto& critter : town.critters) out += std::format("{}\t{:.2f}\t{:.2f}\t{}\t{}\n", critter.row, critter.x, critter.y, critter.walking ? 1 : 0, critter.marked ? 1 : 0);
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "level" && town.level) {   // where the player is
            return std::format("ok {} {:.2f} {:.2f} {} {} {} {}\n", town.level->id, town.player.x, town.player.y,
                               town.level->ds1.width(), town.level->ds1.height(), town.level->world_x, town.level->world_y);
        }
        if (args.size() >= 2 && args[1] == "players") {   // the game's players (local co-op): id, name, cell, seat (-1: the first player's), mode (-1 none)
            std::string out;
            for (const auto& player : town.world.players) {
                const auto coop_player = std::ranges::find(town.coop, player->id, &Town::CoopPlayer::id);
                out += std::format("{}\t{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}\n", player->id, player->character.name, player->player.x, player->player.y,
                                   coop_player == town.coop.end() ? -1 : coop_player->seat, player->pmode, int(player->ghost));
            }
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "roster") {    // the joining pads' rosters: seat, stage, pick, entries (* greyed)
            std::string out;
            for (const auto& coop_player : town.coop) {
                if (!coop_player.roster) continue;
                const auto& roster = *coop_player.roster;
                out += std::format("{}\t{}\t{}\t", coop_player.seat, int(roster.stage), roster.pick);
                for (const auto& entry : roster.entries) out += (entry.usable ? "" : "*") + entry.name + ",";
                out += "\t" + roster.name + "\n";
            }
            return out + "ok\n";
        }
        if (args.size() >= 4 && args[1] == "blocked" && town.level) {   // can a unit stand at (x, y)?
            return std::format("ok {}\n", int(town.level->unit_blocked(std::strtof(args[2].c_str(), nullptr),
                                                                   std::strtof(args[3].c_str(), nullptr))));
        }
        if (args.size() >= 2 && args[1] == "grid" && town.level) {   // the level, n spots a cell (subtiles: 5): '#' where a unit can't stand, a row a line
            const int per_cell = args.size() >= 3 ? std::clamp(std::atoi(args[2].c_str()), 1, 5) : 1;
            const float step = 1.f / float(per_cell);
            std::string out;
            for (int y = 0; y < town.level->ds1.height() * per_cell; ++y) {
                for (int x = 0; x < town.level->ds1.width() * per_cell; ++x)
                    out += town.level->unit_blocked((float(x) + 0.5f) * step, (float(y) + 0.5f) * step) ? '#' : '.';
                out += '\n';
            }
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "wp") {         // activate waypoint index n
            town.world.set_waypoint(std::atoi(args[2].c_str()));
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "presets" && town.level) {   // every object / NPC the level placed: index, objects.txt id, cell, hidden, name
            std::string out;
            for (std::size_t i = 0; i < town.level->npcs.size(); ++i) {
                const auto& npc = town.level->npcs[i];
                if (npc.object_id == 0 && npc.name.empty()) continue;
                out += std::format("{}\t{}\t{:.1f}\t{:.1f}\t{}\t{}\n", i, npc.object_id, npc.x, npc.y, i < town.npc_states.size() && town.npc_states[i].hidden ? 1 : 0, npc.name);
            }
            for (const auto& room : town.level->rooms)                // preset rooms (not laid yet too): LvlPrest def, its middle
                if (room.kind == 2) out += std::format("room\t{}\t{}\t{}\n", room.def, room.x + room.width / 2, room.y + room.height / 2);
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "goto" && town.world.game_data) {   // arrive on level id, at a free spot by its middle (quest tests)
            const auto* destination = town.world.game_data->level(std::atoi(args[2].c_str()));
            if (!destination || destination->ds1.width() == 0) return std::string("err no such level\n");
            const auto [spot_x, spot_y] = destination->nearest_free(float(destination->ds1.width()) / 2, float(destination->ds1.height()) / 2);
            town.world.arrive(destination, spot_x, spot_y, "debug goto");
            return std::format("ok {:.1f} {:.1f}\n", spot_x, spot_y);
        }
        if (args.size() >= 4 && args[1] == "warp") {       // put the player at cell (x, y)
            town.player.x = town.target_x = std::strtof(args[2].c_str(), nullptr);
            town.player.y = town.target_y = std::strtof(args[3].c_str(), nullptr);
            town.player.walking = false;
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "warps" && town.level) {     // the level's warps: index, cell, where to
            std::string out;
            for (std::size_t i = 0; i < town.level->warps.size(); ++i)
                out += std::format("{}\t{:.1f}\t{:.1f}\t{}\n", i, town.level->warps[i].x, town.level->warps[i].y, town.level->warps[i].destination);
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "enter" && town.level) {     // stand by warp i as if clicked: the next frame takes it
            const auto npc_index = std::size_t(std::atoi(args[2].c_str()));
            if (npc_index >= town.level->warps.size()) return std::string("err no such warp\n");
            std::tie(town.player.x, town.player.y) = town.level->nearest_free(town.level->warps[npc_index].x + 0.5f, town.level->warps[npc_index].y + 1.5f);
            town.target_x = town.player.x; town.target_y = town.player.y;
            town.player.walking = false;
            town.hero.take_warp = int(npc_index);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "tomb") {          // the true Tal Rasha's Tomb (the act seed's roll, FUN_0061aeb0)
            if (!town.world.game_data) return std::string("err no game data\n");
            auto act = d2d::drlg::act_seed(town.world.game_data->map_seed);
            return std::format("ok {}\n", d2d::drlg::roll_act2_tombs(act).boss);
        }
        if (args.size() >= 3 && args[1] == "netsend" && town.net_game) {   // a raw C->S packet to the host, hex bytes (live checks)
            d2d::net::Bytes packet;
            for (std::size_t i = 2; i < args.size(); ++i) {
                char* end = nullptr;
                const unsigned long byte = std::strtoul(args[i].c_str(), &end, 16);
                if (args[i].empty() || *end != '\0' || byte > 0xff) return std::string("err not a hex byte: ") + args[i] + "\n";
                packet.push_back(std::uint8_t(byte));
            }
            town.net_game->send_items({ packet });
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "net" && town.net_game && town.level) {   // a joined game: where the host has us, where d2d has us (act subtiles), and how far apart
            const float local_x = (town.hero.player.x + float(town.level->world_x)) * 5.f, local_y = (town.hero.player.y + float(town.level->world_y)) * 5.f;
            std::string out = std::format("host {:.0f} {:.0f} local {:.0f} {:.0f} apart {:.1f} units {} trade {} self {}\n", town.net_game->host_x, town.net_game->host_y, local_x, local_y,
                                          std::hypot(town.net_game->host_x - local_x, town.net_game->host_y - local_y), town.net_game->units.size(), town.net_game->trade.state, town.net_game->self_id);
            for (const auto& other : town.view.others)   // the other players: id, name, cell
                out += std::format("player\t{}\t{}\t{:.1f}\t{:.1f}\n", other.id, other.name, other.unit.x, other.unit.y);
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "tradestate" && town.net_game) {   // a joined game: the trade's state, set here (its window's look; the host isn't told)
            town.net_game->trade.state = std::atoi(args[2].c_str());
            return std::string("ok\n");
        }
        if (args.size() >= 3 && args[1] == "chat") {   // a chat line shown as received (its look; no one is told)
            std::string text = args[2];
            for (std::size_t i = 3; i < args.size(); ++i) text += " " + args[i];
            town.add_chat(text);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "typing") {   // the chat edit box open with this text (its look; nothing is sent)
            std::string text;
            for (std::size_t i = 2; i < args.size(); ++i) text += (i > 2 ? " " : "") + args[i];
            town.chat_typing = text;
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "portals") {   // the player's portals: which, level id, cell (none: not open)
            std::string out;
            for (std::size_t i = 0; i < town.world.portal.size(); ++i)
                if (const auto& open = town.world.portal[i]; open.level) out += std::format("{}\t{}\t{:.1f}\t{:.1f}\n", i, open.level->id, open.x, open.y);
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "objects" && town.level) {   // operable objects: index, cell, kind (shrine, chest, opN), shrine row / trap, mode
            std::string out;
            for (std::size_t i = 0; i < town.level->npcs.size(); ++i)
                if (const auto& npc = town.level->npcs[i]; npc.operate_fn > 0 && npc.root == "objects")
                    out += std::format("{}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\n", i, npc.x, npc.y, npc.operate_fn == d2d::rules::operate_fn::kShrine ? "shrine" : npc.operate_fn == d2d::rules::operate_fn::kChest ? "chest" : std::format("op{}", npc.operate_fn),
                                       npc.operate_fn == d2d::rules::operate_fn::kShrine ? npc.shrine : npc.operate_fn == d2d::rules::operate_fn::kChest ? npc.trap : npc.object_id, npc.locked ? "locked" : "-",
                                       i < town.npc_states.size() && !town.npc_states[i].mode.empty() ? town.npc_states[i].mode : std::string_view(npc.mode));
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "operate" && town.level) {   // operate shrine / chest i where it stands
            const int npc_index = std::atoi(args[2].c_str());
            if (npc_index < 0 || std::size_t(npc_index) >= town.level->npcs.size()) return std::string("err no such object\n");
            town.operate(npc_index, town.now_ms, args.size() >= 4 ? std::atoi(args[3].c_str()) : -1);
            return std::format("ok life={} mana={} boost={}\n", town.hero.character.stats.fixed(d2d::d2s::kLife), town.hero.character.stats.fixed(d2d::d2s::kMana), town.hero.boost.shrine);
        }
        if (args.size() >= 4 && args[1] == "stat") {       // set character stat <id 0..15> to <value>
            const int id = std::atoi(args[2].c_str());
            if (id < 0 || id > 15) return std::string("err stat 0..15\n");
            town.hero.character.stats.values[std::size_t(id)] = std::atoll(args[3].c_str());   // the World's character: the client's follows
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "attack") {      // the player's attack as combat sees it
            const auto& fighter = town.hero.player_combat;                         // as combat sees it this frame (self states included)
            return std::format("ok dmg={}-{} ar={} def={} block={} dr={}%+{} mdr={} res={}/{}/{}/{} cb={} ds={} ow={} "
                               "ll={} ml={} ias={} wsm={} frw={} fhr={} fbr={} thorns={}+{}l cold={}-{} fire={}-{} light={}-{} "
                               "crit={} def_melee={} def_missile={} dodge={} avoid={} evade={} ar%={} mcrit={} wblock={}\n",
                               fighter.min, fighter.max, fighter.attack_rating, fighter.defense, fighter.block, fighter.dr_pct, fighter.dr_flat, fighter.mdr, fighter.res[0], fighter.res[1], fighter.res[2],
                               fighter.res[3], fighter.crushing, fighter.deadly, fighter.open_wounds, fighter.life_steal, fighter.mana_steal, fighter.ias, fighter.wsm, fighter.frw,
                               fighter.fhr, fighter.fbr, fighter.thorns, fighter.thorns_light, fighter.elem[2].first, fighter.elem[2].second, fighter.elem[0].first,
                               fighter.elem[0].second, fighter.elem[1].first, fighter.elem[1].second, fighter.critical, fighter.def_melee, fighter.def_missile,
                               fighter.dodge, fighter.avoid, fighter.evade, fighter.ar_pct, fighter.mastery_crit, fighter.weapon_block);
        }
        if (args.size() >= 4 && args[1] == "charges") {    // hold n charges of charge-up skill id (tests)
            auto& fight = town.fight;
            const int id = std::atoi(args[2].c_str());
            std::erase_if(town.hero.charges, [&](const auto& charge) { return charge.skill == id; });
            town.hero.charges.push_back({ id, fight.skill_level ? fight.skill_level(id) : 1, std::clamp(std::atoi(args[3].c_str()), 1, 3), ~0u });
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "release") {     // release the charges on the nearest live monster
            auto& fight = town.fight;
            int best = -1; float best_distance = 1e9f;
            for (std::size_t i = 0; i < fight.monsters.size(); ++i)
                if (const float distance = std::hypot(fight.monsters[i].unit.x - town.player.x, fight.monsters[i].unit.y - town.player.y); fight.monsters[i].alive() && distance < best_distance) { best_distance = distance; best = int(i); }
            if (best < 0) return std::string("err no monster\n");
            fight.release(std::size_t(best), town.world_ms);   // the World's clock, as its own releases
            return std::string("ok ") + std::to_string(fight.missiles.size()) + "\n";
        }
        if (args.size() >= 2 && args[1] == "light") {      // the light grid round the player, a level (0..31) a subtile
            const std::pair<float, float> hero_at{ town.player.x, town.player.y };   // the player's light where they stand, as the frame's
            const auto light = frame_light(*town.scene, town.view, town.cam_x, town.cam_y, {}, -1, {}, nullptr, town.now_ms, &hero_at);   // the frame's clock: frame_light's eases keep it
            std::string out;
            for (int y = 0; y < light.grid.grid_size; ++y) {
                for (int x = 0; x < light.grid.grid_size; ++x) out += "0123456789abcdefghijklmnopqrstuv"[light.grid.values[std::size_t(y * light.grid.grid_size + x)] >> 3];
                out += '\n';
            }
            if (args.size() >= 3 && args[2] == "more") {   // `debug light more`: the lights stamped and the subtiles walls block
                for (const auto& stamp : light.stamps) out += std::format("light {} {} r8 {} {}\n", stamp.x, stamp.y, stamp.radius8, stamp.shadowed ? "shadowed" : "plain");
                for (int y = 0; y < light.grid.grid_size; ++y) {
                    for (int x = 0; x < light.grid.grid_size; ++x) out += light.grid.blocked[std::size_t(y * light.grid.grid_size + x)] ? '#' : '.';
                    out += '\n';
                }
            }
            return out + "ok " + std::to_string(light.grid.origin_x) + " " + std::to_string(light.grid.origin_y) + " bonus=" + std::to_string(town.view.light_bonus) + "\n";
        }
        if (args.size() >= 2 && args[1] == "die") {        // the player (or player <id>, local co-op) dies where they stand
            const int id = args.size() >= 3 ? std::atoi(args[2].c_str()) : town.hero.id;
            const auto found = std::ranges::find(town.world.players, id, [](const auto& player) { return player->id; });
            if (found == town.world.players.end()) return std::string("err no such player\n");
            town.world.act_as(**found);
            (*found)->character.stats.values[d2d::d2s::kLife] = 0;
            town.world.fight.set_pmode(kModeDT, town.world.now);
            town.world.act_as(town.world.lead());
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "corpses") {    // the player's corpses: level, cell, items
            std::string out;
            for (const auto& corpse : town.hero.corpses) out += std::format("{}\t{:.1f}\t{:.1f}\t{}\n", corpse.level ? corpse.level->id : -1, corpse.x, corpse.y, corpse.items.size());
            return out + std::format("ok view={} gfx_tr={}\n", town.view.corpses.size(), int(town.view.gfx[1]));
        }
        if (args.size() >= 4 && args[1] == "walls") {      // a cell's wall tiles: `debug walls <x> <y>` -> layer:orientation ...
            const int x = std::atoi(args[2].c_str()), y = std::atoi(args[3].c_str());
            const auto& level = *town.level;
            std::string out = "ok";
            if (x >= 0 && y >= 0 && x < int(level.ds1.width()) && y < int(level.ds1.height())) {
                const auto off = std::size_t(y) * std::size_t(level.ds1.width()) + std::size_t(x);
                if (!level.picks.empty()) for (const auto& pick : level.picks[off]) out += std::format(" {}:{}", int(pick.layer), int(pick.orient));
                else for (const auto& layer : level.ds1.walls()) out += std::format(" w{}{}", layer.cells[off].wall_type, layer.cells[off].hidden ? "h" : "");
            }
            return out + "\n";
        }
        if (args.size() >= 2 && args[1] == "tiles" && town.level) {      // every picked tile: `x y wall|floor|shadow o<orientation> <dt1>:<index>` (whisper town's form, level cells)
            const auto& level = *town.level;
            std::string out = std::format("level {}\n", level.id);
            auto name_of = [&](const d2d::dt1::Tile* tile) {
                for (const auto& [file, archive] : level.tile_dt1s.archive) {
                    const auto tiles = archive->tiles();
                    if (!tiles.empty() && tile >= tiles.data() && tile < tiles.data() + tiles.size()) return std::format("{}:{}", file->name, tile - tiles.data());
                }
                return std::string("?");
            };
            static constexpr std::array<const char*, 3> kKind = { "wall", "floor", "shadow" };
            const auto& picks = args.size() >= 3 && args[2] == "server" ? level.picks : level.shown();   // `debug tiles server`: collision's
            for (std::size_t cell = 0; cell < picks.size(); ++cell)
                for (const auto& pick : picks[cell])
                    out += std::format("{} {} {} o{} {}\n", cell % level.ds1.width(), cell / level.ds1.width(), kKind[std::min<std::size_t>(pick.layer, 2)],
                                       int(pick.orient), name_of(pick.tile));
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "laid" && town.level) {       // the order relevel laid the level's rooms in: `<list index> <x> <y>` per room, the first `up` were up
            const auto& level = *town.level;
            const std::size_t count = level.rooms.size();
            std::string out = std::format("up {}\n", level.laid_up);
            for (std::size_t step = 0; step < count; ++step) {
                const std::size_t list_index = level.laid.empty() ? step : level.laid[step];
                const auto& room = level.rooms[count - 1 - list_index];   // the list is newest first
                out += std::format("{} {} {}\n", list_index, room.x, room.y);
            }
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "relevel" && town.level) {    // lay the level's tiles again, rooms up in this order: `debug relevel <x>,<y> ...` (room cells)
            auto& level = const_cast<d2d::game::Level&>(*town.level);
            std::vector<std::size_t> came_up;
            for (std::size_t arg = 2; arg < args.size(); ++arg) {
                const auto comma = args[arg].find(',');
                const int x = std::atoi(args[arg].c_str()), y = comma == std::string::npos ? -1 : std::atoi(args[arg].c_str() + comma + 1);
                for (std::size_t index = 0; index < level.rooms.size(); ++index)
                    if (level.rooms[index].x == x && level.rooms[index].y == y) came_up.push_back(index);
            }
            level.laid.clear();
            d2d::game::relevel(level, came_up, town.world.game_data, &town.world.fight.spawning);   // the camp's rooms next door kept; `debug tiles server` shows it
            return std::format("ok {}\n", came_up.size());
        }
        if (args.size() >= 2 && args[1] == "party") {      // the Party screen with three sample players (no joined game needed): `debug party on|off`
            town.party_preview.clear();
            if (args.size() >= 3 && args[2] == "on")
                town.party_preview = { { 2, "Ally", 3, 25, 1, 3, 0, "Rogue Encampment", false }, { 3, "Stranger", 4, 12, 0xffff, 0, 1, {}, false },
                                       { 4, "Rival", 6, 40, 0xffff, 2, 8, {}, true } };
            town.party.open = !town.party_preview.empty();
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "alt") {        // hold "Show Items" (Alt) as if pressed: `debug alt on|off`
            town.alt_held = args.size() >= 3 && args[2] == "on";
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "rain") {       // the weather: `debug rain [state]` (0 clear .. 3 falling) starts that state next tick
            auto& rain = town.rain;
            if (args.size() >= 3) { rain.state = (std::atoi(args[2].c_str()) + 3) % 4; rain.left = 0; }
            return "ok state=" + std::to_string(rain.state) + " density=" + std::to_string(rain.density) + " drops=" + std::to_string(rain.drops.size())
                 + " splashes=" + std::to_string(rain.splashes.size()) + " wind=" + std::to_string(rain.wind) + "\n";
        }
        if (args.size() >= 2 && args[1] == "day") {        // the time of day: `debug day [degrees]`
            auto& day = town.world.day;
            if (args.size() >= 3) {
                const int deg = ((std::atoi(args[2].c_str()) % 360) + 360) % 360;
                day.time = deg * d2d::rules::kDayScale;
                day.phase = 2;                           // the last phase to start by then
                for (int period = 0; period < 6; ++period) {
                    const int start = d2d::rules::kDay[std::size_t(period)].start;
                    if (start <= deg && start > d2d::rules::kDay[std::size_t(day.phase)].start) day.phase = period;
                }
            }
            return "ok phase=" + std::to_string(day.phase) + " time=" + std::to_string(day.time) + " intensity=" + std::to_string(day.intensity()) + "\n";
        }
        if (args.size() >= 3 && args[1] == "difficulty") { // play on difficulty d: a new game's monsters
            const int difficulty = std::clamp(std::atoi(args[2].c_str()), 0, 2);
            for (int i = 0; i < 3; ++i) town.hero.character.header.difficulty[std::size_t(i)] &= 0x7f;   // the World's character
            town.hero.character.header.difficulty[std::size_t(difficulty)] |= 0x80;
            // A new game on it, as a real one starts: the levels built again for
            // the difficulty (set_map_seed: LvlMaze Rooms(N) / (H)), the World
            // entered with its own character (items, stats) in the act's town.
            static_cast<Character&>(town.character) = town.hero.character;
            if (scene) set_map_seed(*scene, scene->map_seed, difficulty);
            town.enter();
            return std::string("ok\n");
        }
        if (args.size() >= 3 && args[1] == "give") {       // an item <code> [quality] at the player's feet, the area level's
            town.world.loot.put({ .code = args[2], .quality = args.size() >= 4 ? std::atoi(args[3].c_str()) : 2 }, town.hero.player.x, town.hero.player.y, 30,
                                town.world.fight.spawning.game, town.world.now);
            return std::format("ok #{}\n", town.world.loot.ground.back().id);
        }
        if (args.size() >= 3 && args[1] == "quest") {      // mark Act quest <q> done (bit 0), active difficulty
            const int quest = std::atoi(args[2].c_str()), bit = quest * 16;
            if (quest < 0 || quest >= 48) return std::string("err quest 0..47\n");
            auto& header = town.hero.character.header;
            auto& quest_bits = town.world.quests();
            if (args.size() >= 4 && args[3] == "reset") {   // not started; the game's Den quest too
                quest_bits[std::size_t(bit >> 3)] = quest_bits[std::size_t(bit >> 3) + 1] = 0;
                town.world.den = {};
                town.world.den_left = -1;
            }
            if (args.size() >= 4)                          // show / reset: its 16 bits, the Den's state
                return std::format("ok bits={:#06x} den={} skillpts={} states={},{},{},{},{},{} radament={} log={} tainted={} log={} dark={} eclipse={} light={} arcane={} log={} summoner={} log={} portal={} tombs={} log={} true={} lair={} a5={},{},{},{},{} logs={},{},{},{},{}\n", quest_bits[std::size_t(bit >> 3)] | quest_bits[std::size_t(bit >> 3) + 1] << 8,
                                   town.world.den.state, town.hero.character.stats.get(d2d::d2s::kSkillPts), town.world.den.state, town.world.burial.state,
                                   town.world.tools.state, town.world.cain.state, town.world.tower.state, town.world.andy.state, town.world.radament.state, town.world.radament.log,
                                   town.world.tainted.state, town.world.tainted.log, town.world.tainted.dark, town.world.day.eclipse, town.world.day.intensity(),
                                   town.world.arcane.state, town.world.arcane.log, town.world.summoner.state, town.world.summoner.log, town.world.arcane.portal_made,
                                   town.world.tombs.state, town.world.tombs.log, town.world.tombs.true_tomb, town.world.tombs.lair_open,
                                   town.world.siege.state, town.world.rescue.state, town.world.prison.state, town.world.betrayal.state, town.world.rite.state,
                                   town.world.siege.log, town.world.rescue.log, town.world.prison.log, town.world.betrayal.log, town.world.rite.log);
            header.quests[std::size_t(header.active_difficulty())][std::size_t(bit >> 3)] |= std::uint8_t(1 << (bit & 7));
            switch (quest) {                                // as a new game would see it (FUN_00546270): its join, then the chain from quest 1
            case 1: town.world.den.join(quest_bits); break;
            case 2: town.world.burial.join(quest_bits); break;
            case 3: town.world.tools.join(quest_bits); break;
            case 4: town.world.cain.join(quest_bits, false, false); break;
            case 5: town.world.tower.join(quest_bits); break;
            case 6: town.world.andy.join(quest_bits); break;
            default: break;
            }
            if (quest >= 1 && quest <= 6) town.world.chain(1);
            for (std::size_t i = 0; i < town.level->npcs.size() && i < town.npc_states.size(); ++i)
                if (const int gated_quest = town.level->npcs[i].quest)
                    town.npc_states[i].hidden = !header.quest_flag(header.active_difficulty(), gated_quest, 0);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "unid") {       // unidentify every carried item
            for (auto& item : town.hero.character.items) if (item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory) item.identified = false;
            return "ok " + std::to_string(d2d::rules::unidentified(town.hero.character.items)) + "\n";
        }
        if (args.size() >= 3 && args[1] == "boss" && scene) {   // the nearest plain monster: a unique with <mod>...
            std::vector<int> mods;
            for (std::size_t k = 2; k < args.size(); ++k) mods.push_back(std::atoi(args[k].c_str()));
            Monster* best = nullptr;
            for (auto& monster : town.world.fight.monsters)
                if (monster.alive() && monster.boss == d2d::rules::Boss::none
                    && (!best || std::hypot(monster.unit.x - town.player.x, monster.unit.y - town.player.y) < std::hypot(best->unit.x - town.player.x, best->unit.y - town.player.y)))
                    best = &monster;
            if (!best) return std::string("err no monster\n");
            make_boss(*scene, *best, d2d::rules::Boss::unique, mods, -1, town.world.rng(0x10000), town.hero.character.header.active_difficulty(), town.world.rng);
            return std::format("ok #{} {} aura={} lvl={}\n", best->id, best->npc.name, best->aura, best->aura_lvl);
        }
        if (args.size() >= 2 && args[1] == "kill" && scene) {   // kill the level's monsters but <n>, those of MonStats id <spare>, those over <cells> off (quests)
            int keep = args.size() >= 3 && args[2] != "spare" && args[2] != "within" ? std::atoi(args[2].c_str()) : 0;
            const auto spare_at = std::ranges::find(args, std::string("spare")), within_at = std::ranges::find(args, std::string("within"));
            if ((spare_at != args.end() && spare_at + 1 == args.end()) || (within_at != args.end() && within_at + 1 == args.end())) return std::string("err spare <MonStats id> / within <cells>\n");
            const std::string spare = spare_at != args.end() ? *(spare_at + 1) : "";
            const float within = within_at != args.end() ? float(std::atof((within_at + 1)->c_str())) : 1e9f;
            auto& monsters = town.world.fight.monsters;
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].alive() && (spare.empty() || scene->monsters.types[std::size_t(monsters[i].type)].id != spare)
                    && std::hypot(monsters[i].unit.x - town.hero.player.x, monsters[i].unit.y - town.hero.player.y) <= within
                    && keep-- <= 0 && hurt(*scene, monsters[i], monsters[i].hit_points, town.world.now)) town.world.fight.killed(i, town.world.now);
            return std::string("ok\n");
        }
        if (args.size() >= 3 && args[1] == "itemat" && scene) {   // where carried item <id> sits on screen with the inventory open: its box's middle
            const auto found = std::ranges::find(town.hero.character.items, std::atoi(args[2].c_str()), &d2d::d2s::Item::id);
            if (found == town.hero.character.items.end()) return std::string("err no such item\n");
            const auto& layout = scene->inv_layout[std::size_t(std::clamp(town.hero.character.character_class, 0, 6))];
            std::array<int, 4> rect{};
            if (found->location == d2d::d2s::item_location::kStored && found->panel == d2d::d2s::item_panel::kInventory) {
                const auto info = scene->rules.item_info.find(found->code);
                const int width = info != scene->rules.item_info.end() ? info->second.width : 1, height = info != scene->rules.item_info.end() ? info->second.height : 1;
                rect = { layout.grid_x + found->column * layout.box_w, layout.grid_y + found->row * layout.box_h, width * layout.box_w, height * layout.box_h };
            } else if (found->location == d2d::d2s::item_location::kEquipped && found->slot >= d2d::d2s::body_location::kFirst && found->slot <= d2d::d2s::body_location::kLast) {
                rect = layout.slots[std::size_t(found->slot)];
            } else return std::string("err not in the inventory panel\n");
            return std::format("ok {} {}\n", rect[0] + rect[2] / 2, rect[1] + rect[3] / 2);
        }
        if (args.size() >= 2 && args[1] == "clearinv") {   // empty the inventory grid (tests that need room)
            std::erase_if(town.hero.character.items, [](const auto& item) { return item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory; });
            return std::string("ok\n");
        }
        if (args.size() >= 4 && args[1] == "equip" && scene) {   // a normal <code> in body slot <n>, replacing what's there (tests)
            auto& items = town.hero.character.items;
            const int slot = std::atoi(args[3].c_str());
            std::erase_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
            auto seed = town.world.rng;
            auto item = d2d::rules::generate_item(scene->rules, args[2], 1, 2, town.world.rng, &seed);
            item.location = d2d::d2s::item_location::kEquipped;
            item.slot = std::uint8_t(slot);
            items.push_back(item);
            return std::format("ok quantity={}\n", item.quantity);
        }
        if (args.size() >= 2 && args[1] == "wear") {       // halve worn items' durability
            for (auto& item : town.hero.character.items) if (item.location == d2d::d2s::item_location::kEquipped) item.durability = d2d::rules::max_durability(item) / 2;
            return std::string("ok\n");
        }
        if (args.size() >= 4 && args[1] == "points" && scene) {   // give class skill <id> n points (tests)
            const auto& ids = scene->skills.class_ids[std::size_t(std::max(town.hero.character.character_class, 0))];
            const auto found = std::ranges::find(ids, std::atoi(args[2].c_str()));
            if (found == ids.end()) return std::string("err not a class skill\n");
            town.hero.character.stats.skills[std::size_t(found - ids.begin())] = std::uint8_t(std::clamp(std::atoi(args[3].c_str()), 0, 99));
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "passives" && scene) {   // the passives' stats: stat=value[/itype]
            std::string out = "ok";
            for (const auto& passive : d2d::rules::passive_stats(scene->skills, town.fight.calc_env()))
                out += std::format(" {}={}{}{}", passive.stat, passive.value, passive.itype.empty() ? "" : "/", passive.itype);
            return out + "\n";
        }
        if (args.size() >= 2 && args[1] == "unbuilt" && scene) {   // class skills no path in Fight takes
            std::string out;
            for (const auto& skill : scene->skills.rows) {
                if (skill.cls.empty() || skill.id < 0) continue;
                auto& fight = town.fight;
                const bool built = skill_built(skill) || self_cast(skill) || fight.missile_skill(skill) || fight.spot_skill(skill) || fight.summon_skill(skill) || skill.aura
                                || (skill.srvstfunc == d2d::rules::ServerStartFunction::kNone && skill.srvdofunc == d2d::rules::ServerDoFunction::kNone && (skill.passive_stat[0] >= 0 || skill.passive));
                if (!built) out += std::format("{} {} st{} do{}\n", skill.id, skill.name, int(skill.srvstfunc), int(skill.srvdofunc));
            }
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "states") {    // the player's buffs; each cursed / cried monster
            std::string out = std::format("absorb={} maxlife={} maxmana={} def={} buffs=", town.hero.absorb_pool,
                                          character.stats.fixed(d2d::d2s::kMaxLife), character.stats.fixed(d2d::d2s::kMaxMana), town.hero.player_combat.defense);
            for (const auto& state : town.hero.self_states) out += std::format("{}:{},", state.skill, state.level);
            out += "\n";
            for (const auto& monster : town.fight.monsters)
                if (monster.curse.skill >= 0 || monster.cry.skill >= 0 || monster.stun_until > 0)
                    out += std::format("{} hp={} curse={} cry={} dmg%={} speed%={} reflect%={} align={} stun={} ai={}{} afraid={}\n", monster.npc.name, monster.hit_points,
                                       monster.curse.skill, monster.cry.skill, monster.dmg_pct, monster.speed_pct, monster.reflect_pct, monster.align,
                                       monster.stun_until, monster.special, monster.other ? "/other" : "", int(monster.afraid));
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "pets") {      // each pet: row level life dmg th ac res ranged/aura
            std::string out;
            for (const auto& pet : town.hero.pets)
                out += std::format("{} L{} hp={}/{} dmg={}-{} th={} ac={} res={},{},{},{} fire={}-{} ranged={}:{} aura={}:{} here={} at={:.1f},{:.1f} {} walk={} path={} goal={:.1f},{:.1f} target={} cast={} ctrl={},{},{} charges={} buffs={}\n",
                                   pet.monster.npc.name, pet.monster.stats.level, pet.monster.hit_points, pet.monster.stats.hit_points, pet.monster.stats.a1_min, pet.monster.stats.a1_max, pet.monster.stats.to_hit, pet.monster.stats.armor_class,
                                   pet.res[0], pet.res[1], pet.res[2], pet.res[3], pet.fire_lo, pet.fire_hi, pet.ranged, pet.ranged_level, pet.aura,
                                   pet.aura_level, pet.where == town.level, pet.monster.unit.x, pet.monster.unit.y, pet.monster.mode,
                                   pet.monster.unit.walking, pet.monster.unit.path.size(), pet.monster.unit.goal_x, pet.monster.unit.goal_y, pet.target, pet.cast,
                                   pet.ctrl[0], pet.ctrl[1], pet.ctrl[2], [&] {
                                       std::string held;
                                       for (const auto& charge : pet.charges) held += std::format("{}{}:{}", held.empty() ? "" : ",", charge.skill, charge.count);
                                       return held.empty() ? std::string("-") : held;
                                   }(), [&] {
                                       std::string active;
                                       for (const auto& buff : pet.buffs) active += std::format("{}{}", active.empty() ? "" : ",", buff.skill);
                                       return active.empty() ? std::string("-") : active;
                                   }());
            return out + "ok\n";
        }
        if (args.size() >= 4 && args[1] == "skill") {     // put skill <id> on the left / right button, if usable
            const int id = std::atoi(args[3].c_str());
            const bool left = args[2] == "left";
            if (!town.skillbar.usable(id, left)) return std::string("err not usable\n");
            (left ? town.skillbar.left : town.skillbar.right) = id;
            return std::string("ok\n");
        }
        if (args.size() >= 3 && (args[1] == "statpts" || args[1] == "skillpts")) {   // grant unspent points
            town.hero.character.stats.values[args[1] == "statpts" ? d2d::d2s::kStatPts : d2d::d2s::kSkillPts] = std::atoi(args[2].c_str());
            return std::string("ok\n");
        }
        if (args.size() < 2 || args[1] != "collision")
            return std::string("err debug collision|automap|portals|objects|net|statpts <n>|skillpts <n>|wear|unid|level|blocked <x> <y>|warp <x> <y>|stat <id> <v>|quest <q>|skill left|right <id>|points <id> <n>\n");
        g_debug_collision = !g_debug_collision;
        return std::string(g_debug_collision ? "ok on\n" : "ok off\n");
    });
    // Every item of the in-game character with its hover text, one item
    // per paragraph (headless check for the tooltips).
    channel.on("items", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        const auto hover_wearer = d2d::game::wearer(*scene, character.character_class, character.items, character.stats);
        for (const auto& item : character.items) {
            out += "[" + item.code + " loc=" + std::to_string(item.location) + " slot=" + std::to_string(item.slot)
                 + " q=" + std::to_string(item.quality) + " panel=" + std::to_string(item.panel) + " id=" + std::to_string(item.id)
                 + std::format(" size={}x{}", d2d::rules::item_size(scene->rules, item.code).first, d2d::rules::item_size(scene->rules, item.code).second)
                 + " at=" + std::to_string(item.column) + "," + std::to_string(item.row) + "]\n";
            for (const auto& line : item_lines(*scene, item, hover_wearer.lvl, &hover_wearer))
                out += "  " + line.text + "\n";
        }
        return out + "ok\n";
    });
    // Named NPCs/objects with their feet on screen (game pixels) and
    // whether they have an NPC menu: "<name>\t<x>\t<y>\t<menu 0/1>".
    channel.on("npcs", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        for (std::size_t i = 0; i < town.level->npcs.size(); ++i) {
            const auto& npc = town.level->npcs[i];
            if (npc.name.empty() || (i < town.npc_states.size() && town.npc_states[i].hidden)) continue;
            const bool live = i < town.npc_states.size() && !npc.path.empty();
            const float dx = (live ? town.npc_states[i].x : npc.x) - town.player.x, dy = (live ? town.npc_states[i].y : npc.y) - town.player.y;
            const int screen_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int screen_y = kViewY + int(std::lround((dx + dy) * (kIsoH / 2)));
            const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& menu_entry) { return menu_entry.hc_idx == npc.hc_idx; });
            out += std::format("{}\t{}\t{}\t{}\t{}\t{:.1f}\t{:.1f}\n", npc.name, screen_x, screen_y, menu ? 1 : 0, i, dx + town.player.x, dy + town.player.y);
        }
        return out + "ok\n";
    });
    // The Blood Moor's monsters (kept while in town too):
    // "<id>\t<x>\t<y>\t<sx>\t<sy>\t<hp>/<max>\t<mode>" — cells in the moor,
    // feet on screen (game pixels, meaningful while in the moor).
    channel.on("monsters", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        for (const auto& monster : town.net_game ? town.view.monsters : town.fight.monsters) {   // a joined game: the host's
            if (monster.gone) continue;                                                        // left the game (the Dark Wanderer)
            const float dx = monster.unit.x - town.player.x, dy = monster.unit.y - town.player.y;
            const int screen_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int screen_y = kViewY + int(std::lround((dx + dy) * (kIsoH / 2)));
            static constexpr std::array<const char*, 5> kBoss = { "-", "champion", "unique", "superunique", "minion" };
            std::string mods;
            for (const int mod : monster.mods) mods += (mods.empty() ? "" : ",") + std::to_string(mod);
            const std::string row = monster.seq_frame >= 0 ? std::format("{}:{}", monster.seq_mode, monster.seq_frame) : "-";   // a sequence's row as drawn
            out += std::format("{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}/{}\t{}\t{}\t{}\tlvl{}\t{}\t#{}\t{}\n", scene->monsters.types[std::size_t(monster.type)].id,
                               monster.unit.x, monster.unit.y, screen_x, screen_y, monster.hit_points, monster.stats.hit_points, monster.mode, kBoss[std::size_t(monster.boss)], mods.empty() ? "-" : mods,
                               monster.stats.level, monster.npc.name, monster.id, row);
        }
        return out + "ok\n";
    });
    // Loot on the ground: "<code>\t<label>\t<sx>\t<sy>" (feet on screen, game pixels).
    channel.on("ground", [&](const std::vector<std::string>&) {
        std::string out;
        for (const auto& ground_item : town.net_game ? town.view.ground : town.loot.ground) {   // a joined game: the host's
            const float dx = ground_item.x - town.player.x, dy = ground_item.y - town.player.y;
            out += std::format("{}\t{}\t{}\t{}\t#{}\t{:.1f}\t{:.1f}\n", ground_item.item.code, ground_item.label,
                               int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
                               kViewY + int(std::lround((dx + dy) * (kIsoH / 2))), ground_item.id, ground_item.x, ground_item.y);
        }
        return out + "ok\n";
    });
    // The open NPC menu's lines: "<text>\t<x>\t<y>" with a point inside
    // each (game pixels), header first.
    channel.on("menu", [&](const std::vector<std::string>&) {
        std::string out;
        int base = town.npc_menu.y;
        for (const auto& line : town.npc_menu.lines) {
            base += line.height;
            out += line.text + "\t" + std::to_string(town.npc_menu.x + town.npc_menu.box_width / 2) + "\t"
                 + std::to_string(base - line.height / 2) + "\n";
        }
        return out + "ok\n";
    });
    channel.on("state", [&](const std::vector<std::string>&) {
        return std::string("screen=") + screen_name(screen)
             + " save=" + std::to_string(csu.selected)
             + " scroll=" + std::to_string(csu.scroll)
             + " class=" + std::to_string(character.selected)
             + " name=" + (screen == Screen::CharSelect && scene && csu.selected >= 0 && std::size_t(csu.selected) < scene->saves.size()
                           ? std::string(scene->saves[std::size_t(csu.selected)].name) : character.name)
             + " hardcore=" + (character.hardcore ? "1" : "0")
             + " difficulty=" + std::to_string((screen == Screen::CharSelect && scene && csu.selected >= 0 && std::size_t(csu.selected) < scene->saves.size()
                                                ? scene->saves[std::size_t(csu.selected)] : character.header).active_difficulty())
             + (csu.pick_difficulty ? "?" : "")
             + " cam=" + std::format("{:.2f},{:.2f}", town.player.x, town.player.y)
             + " walking=" + (town.player.walking ? "1" : "0") + " dir=" + std::to_string(town.player.dir)
             + " saves=" + std::to_string(scene ? scene->saves.size() : 0)
             + " stash=" + (town.stash_open ? "1" : "0")
             + " cube=" + (town.cube_open ? "1" : "0")
             + " store=" + std::to_string(town.store.npc >= 0 ? town.store.vendor : -1) + " store_tab=" + std::to_string(town.store.tab)
             + " gold=" + std::to_string(character.stats.get(d2d::d2s::kGold))
             + " statpts=" + std::to_string(character.stats.get(d2d::d2s::kStatPts))
             + " str=" + std::to_string(character.stats.get(d2d::d2s::kStr))
             + " life=" + std::to_string(character.stats.fixed(d2d::d2s::kLife)) + "/" + std::to_string(character.stats.fixed(d2d::d2s::kMaxLife))
             + " mana=" + std::to_string(character.stats.fixed(d2d::d2s::kMana)) + "/" + std::to_string(character.stats.fixed(d2d::d2s::kMaxMana))
             + " stamina=" + std::to_string(character.stats.values[d2d::d2s::kStamina]) + "/" + std::to_string(character.stats.values[d2d::d2s::kMaxStamina])
             + " level=" + std::to_string(character.stats.get(d2d::d2s::kLevel)) + " exp=" + std::to_string(character.stats.get(d2d::d2s::kExp))
             + " pmode=" + (town.hero.pmode >= 0 ? kModeCode[town.hero.pmode] : "-")
             + " missiles=" + std::to_string(town.fight.missiles.size())
             + " pets=" + [&] { std::string list; for (const auto& pet : town.hero.pets) list += (list.empty() ? "" : ",") + std::to_string(pet.monster.hit_points) + "/" + std::to_string(pet.monster.stats.hit_points) + ":" + std::string(pet.monster.mode); return list.empty() ? std::string("-") : list; }()
             + " lskill=" + std::to_string(town.skillbar.left) + " rskill=" + std::to_string(town.skillbar.right)
             + " picker=" + std::to_string(town.skillbar.picking) + " hover=" + std::to_string(town.hovered_npc) + " hireling=" + std::to_string(int(town.hireling.open)) + " help=" + std::to_string(int(town.help.open)) + " wset=" + std::to_string(character.header.weapon_set)
             + " gamemenu=" + std::to_string(town.game_menu.open ? town.game_menu.menu + 1 : 0) + ":" + std::to_string(town.game_menu.sel)
             + " mini=" + (town.mini.open ? "1" : "0") + " volume=" + std::to_string(audio.master_volume) + "," + std::to_string(audio.music_volume)
             + " charges=" + [&] {
                   std::string charges;
                   for (const auto& charge : town.hero.charges) charges += std::format("{}{}:{}", charges.empty() ? "" : ",", charge.skill, charge.count);
                   return charges.empty() ? std::string("-") : charges;
               }()
             + " skillpts=" + std::to_string(character.stats.get(d2d::d2s::kSkillPts))
             + " tree=" + (town.tree_open ? std::to_string(town.tree_tab) : "0")
             + " waypoint=" + std::to_string(town.waypoint.open ? int(scene->waypoint_levels[std::size_t(town.waypoint.tab)].size()) : 0)
             + " items=" + std::to_string(character.items.size())
             + " held=" + (town.held ? town.held->code : "-")
             + " unid=" + std::to_string(d2d::rules::unidentified(character.items))
             + " merc=" + (town.merc ? std::format("{:.1f},{:.1f}", town.merc->unit.x, town.merc->unit.y) + ":" + town.hero.merc_npc->code : std::string("-"))
             + " menu=" + std::to_string(town.npc_menu.npc >= 0 ? int(town.npc_menu.lines.size()) : 0)
             + " automap=" + std::to_string(town.automap.open ? int(town.automap.cells.size()) : 0)
             + " speech=" + std::to_string(town.speech.npc >= 0 ? int(town.speech.lines.size()) : 0)
             + " voice=" + std::to_string(audio.voice_sound())
             + " music=" + std::to_string(audio.music.sound)
             + " music_old=" + std::to_string(audio.music_old.sound)
             + " sfx=" + [&] {   // world sounds on: Sounds.txt index:owner:gain (left/right when split), p = paused
                   std::string playing;
                   for (const auto& sfx_channel : audio.sfx) {
                       ALint source_state = AL_STOPPED;
                       ALfloat gain = 0.f, right_gain = 0.f;
                       if (sfx_channel.src) { alGetSourcei(sfx_channel.src, AL_SOURCE_STATE, &source_state); alGetSourcef(sfx_channel.src, AL_GAIN, &gain); }
                       if (sfx_channel.right_src) alGetSourcef(sfx_channel.right_src, AL_GAIN, &right_gain);
                       if (source_state == AL_PLAYING || source_state == AL_PAUSED)
                           playing += std::format("{}{}:{}:{:.2f}{}{}", playing.empty() ? "" : ",", sfx_channel.sound, sfx_channel.owner, gain,
                                                  sfx_channel.right_src ? std::format("/{:.2f}", right_gain) : std::string(), source_state == AL_PAUSED ? "p" : "");
                   }
                   return playing.empty() ? std::string("-") : playing;
               }()
             + "\nok\n";
    });
}

}  // namespace d2d::client
