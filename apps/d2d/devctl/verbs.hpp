// SPDX-License-Identifier: GPL-3.0-or-later
// The devctl verbs that drive and read the game for scripted tests
// (docs/control_channel.md): input (click, key, move, wheel), debug
// set-ups, and reads of the game's state (state, items, npcs, monsters,
// ground, menu). The frame-level ones (info, screenshot, quit) stay in
// main.cpp; `debug`'s sub-verbs are in debug_*.cpp beside this.
#pragma once

#include "audio.hpp"
#include "common.hpp"
#include "debug.hpp"
#include "frontend.hpp"
#include "panels.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "town.hpp"
#include "ui.hpp"
#include "window.hpp"

#include <d2s_items.hpp>
#include <devctl.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <string>
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
        else if (verb == "skill" && verb_args.size() >= 5) town.send(cmd::UseSkill{ int_arg(2, 0), float_arg(3), float_arg(4), int_arg(5), int_arg(6, 0) != 0, int_arg(7) });
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
    // `debug <sub-verb>`: debug_*.cpp, one handler each.
    channel.on("debug", [debug = std::make_shared<DebugVerbs>(character, town, scene)](const std::vector<std::string>& args) {
        return debug->run(args);
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
             + " quake=" + std::format("{},{},{},{},{}", town.shake.envelope(town.now_ms), town.shake.offset.first, town.shake.offset.second,
                                         audio.quake_loop.sound, audio.quake_level)
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
