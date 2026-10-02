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
#include <devctl.hpp>
#include <light.hpp>
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
    }
    return "?";
}

// Registers them on `ch`. They capture main()'s loop locals by reference;
// the channel is only pumped inside that loop, so the captures never
// outlive it.
void register_game_verbs(d2d::devctl::Channel& channel, Window& win, Screen& screen, CharSelectUI& csu, CharCreateUI& character,
                         Town& town, const std::optional<Scene>& scene, Audio& audio) {
    auto click_verb = [&win = win](const std::vector<std::string>& args, std::uint8_t button) {
        if (args.size() < 3) return std::string("err click <x> <y>\n");
        // Args are game pixels; queued events carry window coords and get
        // converted back by SDL_ConvertEventToRenderCoordinates on poll.
        float x = 0, y = 0;
        SDL_RenderCoordinatesToWindow(win.renderer, std::stof(args[1]), std::stof(args[2]), &x, &y);
        // Motion + down + up land in the next frame's poll. One frame is
        // enough: update_button and the slot picker both accept a press
        // and release in the same frame.
        SDL_Event event{};
        event.motion = { .type = SDL_EVENT_MOUSE_MOTION, .windowID = SDL_GetWindowID(win.window), .x = x, .y = y };
        SDL_PushEvent(&event);
        for (auto type : { SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP }) {
            event = {};
            event.button = { .type = type, .windowID = SDL_GetWindowID(win.window),
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
        if (verb == "move" && verb_args.size() >= 4) town.net.send(cmd::Move{ float_arg(2), float_arg(3), true });
        else if (verb == "skill" && verb_args.size() >= 5) town.net.send(cmd::UseSkill{ int_arg(2, 0), float_arg(3), float_arg(4), int_arg(5), int_arg(6, 0) != 0 });
        else if (verb == "interact" && verb_args.size() >= 3) town.net.send(cmd::Interact{ int_arg(2) });
        else if (verb == "pickup" && verb_args.size() >= 3) town.net.send(cmd::Pickup{ int_arg(2) });
        else if (verb == "resurrect") town.net.send(cmd::Resurrect{});
        else if (verb == "stat" && verb_args.size() >= 3) town.net.send(cmd::StatPoint{ int_arg(2, 0), int_arg(3, 1) });
        else if (verb == "skillpt" && verb_args.size() >= 3) town.net.send(cmd::SkillPoint{ int_arg(2, 0) });
        else if (verb == "select" && verb_args.size() >= 4) town.net.send(cmd::SelectSkill{ int_arg(2, 0), int_arg(3, 0) != 0 });
        else if (verb == "belt" && verb_args.size() >= 3) town.net.send(cmd::UseBelt{ int_arg(2, 0) });
        else if (verb == "waypoint" && verb_args.size() >= 4) town.net.send(cmd::Waypoint{ int_arg(2), int_arg(3, 0) });
        else if (verb == "goeast" && verb_args.size() >= 3) town.net.send(cmd::GoEast{ int_arg(2) });
        else if (verb == "imbue" && verb_args.size() >= 3) town.net.send(cmd::Imbue{ int_arg(2) });
        else if (verb == "hand" && verb_args.size() >= 3) town.net.send(cmd::ToCursor{ int_arg(2) });
        else if (verb == "use" && verb_args.size() >= 3) town.net.send(cmd::UseItem{ int_arg(2) });
        else if (verb == "grid" && verb_args.size() >= 4) town.net.send(cmd::ToGrid{ 1, int_arg(2), int_arg(3) });
        else if (verb == "said" && verb_args.size() >= 4) town.net.send(cmd::QuestMessage{ int_arg(2), int_arg(3, 0) });
        else if (verb == "chat" && verb_args.size() >= 3) town.net.send(cmd::Chat{ int_arg(2) });
        else return std::string("err cmd move <x> <y> | skill <id> <x> <y> [unit] [left] | interact <npc> | pickup <unit> | resurrect"
                                " | stat <stat> [n] | skillpt <index> | select <skill> <left> | belt <slot> | waypoint <npc> <level>"
                                " | goeast <npc> | imbue <npc> | hand <item> | grid <col> <row> | use <item> | said <npc> <string> | chat <npc|-1>\n");
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
                     .integer_y = std::stoi(args[1]) };
        SDL_PushEvent(&event);
        return std::string("ok\n");
    });
    channel.on("debug", [&](const std::vector<std::string>& args) {
        if (args.size() >= 2 && args[1] == "automap" && scene) {    // reveal the whole level
            const auto& map = town.level->ds1;
            for (int y = 0; y < int(map.height()); y += 12)
                for (int x = 0; x < int(map.width()); x += 12) automap_reveal(*scene, *town.level, town.automap, float(x), float(y));
            return "ok " + std::to_string(town.automap.cells.size()) + "\n";
        }
        if (args.size() >= 2 && args[1] == "level" && town.level) {   // where the player is
            return std::format("ok {} {:.2f} {:.2f} {} {} {} {}\n", town.level->id, town.player.x, town.player.y,
                               town.level->ds1.width(), town.level->ds1.height(), town.level->world_x, town.level->world_y);
        }
        if (args.size() >= 4 && args[1] == "blocked" && town.level) {   // can a unit stand at (x, y)?
            return std::format("ok {}\n", int(town.level->unit_blocked(std::strtof(args[2].c_str(), nullptr),
                                                                   std::strtof(args[3].c_str(), nullptr))));
        }
        if (args.size() >= 3 && args[1] == "wp") {         // activate waypoint index n
            town.world.set_waypoint(std::atoi(args[2].c_str()));
            return std::string("ok\n");
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
            town.world.take_warp = int(npc_index);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "objects" && town.level) {   // operable objects: index, cell, kind (shrine, chest, opN), shrine row / trap, mode
            std::string out;
            for (std::size_t i = 0; i < town.level->npcs.size(); ++i)
                if (const auto& npc = town.level->npcs[i]; npc.operate_fn > 0 && npc.root == "objects")
                    out += std::format("{}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\n", i, npc.x, npc.y, npc.operate_fn == 2 ? "shrine" : npc.operate_fn == 4 ? "chest" : std::format("op{}", npc.operate_fn),
                                       npc.operate_fn == 2 ? npc.shrine : npc.operate_fn == 4 ? npc.trap : npc.object_id, npc.locked ? "locked" : "-",
                                       i < town.npc_states.size() && !town.npc_states[i].mode.empty() ? town.npc_states[i].mode : std::string_view(npc.mode));
            return out + "ok\n";
        }
        if (args.size() >= 3 && args[1] == "operate" && town.level) {   // operate shrine / chest i where it stands
            const int npc_index = std::atoi(args[2].c_str());
            if (npc_index < 0 || std::size_t(npc_index) >= town.level->npcs.size()) return std::string("err no such object\n");
            town.operate(npc_index, town.now_ms, args.size() >= 4 ? std::atoi(args[3].c_str()) : -1);
            return std::format("ok life={} mana={} boost={}\n", town.world.character.stats.fixed(d2d::d2s::kLife), town.world.character.stats.fixed(d2d::d2s::kMana), town.fight.boost.shrine);
        }
        if (args.size() >= 4 && args[1] == "stat") {       // set character stat <id 0..15> to <value>
            const int id = std::atoi(args[2].c_str());
            if (id < 0 || id > 15) return std::string("err stat 0..15\n");
            town.world.character.stats.values[std::size_t(id)] = std::atoll(args[3].c_str());   // the World's character: the client's follows
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "attack") {      // the player's attack as combat sees it
            const auto& fighter = town.fight.player_combat;                         // as combat sees it this frame (self states included)
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
            std::erase_if(fight.charges, [&](const auto& charge) { return charge.skill == id; });
            fight.charges.push_back({ id, fight.skill_level ? fight.skill_level(id) : 1, std::clamp(std::atoi(args[3].c_str()), 1, 3), ~0u });
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "release") {     // release the charges on the nearest live monster
            auto& fight = town.fight;
            int best = -1; float best_distance = 1e9f;
            for (std::size_t i = 0; i < fight.monsters.size(); ++i)
                if (const float distance = std::hypot(fight.monsters[i].unit.x - town.player.x, fight.monsters[i].unit.y - town.player.y); fight.monsters[i].alive() && distance < best_distance) { best_distance = distance; best = int(i); }
            if (best < 0) return std::string("err no monster\n");
            fight.release(std::size_t(best), std::uint32_t(SDL_GetTicks()));
            return std::string("ok ") + std::to_string(fight.missiles.size()) + "\n";
        }
        if (args.size() >= 2 && args[1] == "light") {      // the light grid round the player, a level (0..31) a subtile
            const auto light = frame_light(*town.scene, town.view, town.cam_x, town.cam_y);
            std::string out;
            for (int y = 0; y < light.grid.grid_size; ++y) {
                for (int x = 0; x < light.grid.grid_size; ++x) out += "0123456789abcdefghijklmnopqrstuv"[light.grid.values[std::size_t(y * light.grid.grid_size + x)] >> 3];
                out += '\n';
            }
            return out + "ok " + std::to_string(light.grid.origin_x) + " " + std::to_string(light.grid.origin_y) + " bonus=" + std::to_string(town.view.light_bonus) + "\n";
        }
        if (args.size() >= 2 && args[1] == "die") {        // the player dies where they stand
            town.world.character.stats.values[d2d::d2s::kLife] = 0;
            town.world.fight.set_pmode(kModeDT, town.world.now);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "corpses") {    // the player's corpses: level, cell, items
            std::string out;
            for (const auto& corpse : town.world.corpses) out += std::format("{}\t{:.1f}\t{:.1f}\t{}\n", corpse.level ? corpse.level->id : -1, corpse.x, corpse.y, corpse.items.size());
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
            for (int i = 0; i < 3; ++i) town.world.character.header.difficulty[std::size_t(i)] &= 0x7f;   // the World's character
            town.world.character.header.difficulty[std::size_t(difficulty)] |= 0x80;
            town.new_game();
            return std::string("ok\n");
        }
        if (args.size() >= 3 && args[1] == "quest") {      // mark Act quest <q> done (bit 0), active difficulty
            const int quest = std::atoi(args[2].c_str()), bit = quest * 16;
            if (quest < 0 || quest >= 48) return std::string("err quest 0..47\n");
            auto& header = town.world.character.header;
            auto& quest_bits = town.world.quests();
            if (args.size() >= 4 && args[3] == "reset") {   // not started; the game's Den quest too
                quest_bits[std::size_t(bit >> 3)] = quest_bits[std::size_t(bit >> 3) + 1] = 0;
                town.world.den = {};
                town.world.den_left = -1;
            }
            if (args.size() >= 4)                          // show / reset: its 16 bits, the Den's state
                return std::format("ok bits={:#06x} den={} skillpts={}\n", quest_bits[std::size_t(bit >> 3)] | quest_bits[std::size_t(bit >> 3) + 1] << 8,
                                   town.world.den.state, town.world.character.stats.get(d2d::d2s::kSkillPts));
            header.quests[std::size_t(header.active_difficulty())][std::size_t(bit >> 3)] |= std::uint8_t(1 << (bit & 7));
            for (std::size_t i = 0; i < town.level->npcs.size() && i < town.npc_states.size(); ++i)
                if (const int gated_quest = town.level->npcs[i].quest)
                    town.npc_states[i].hidden = !header.quest_flag(header.active_difficulty(), gated_quest, 0);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "unid") {       // unidentify every carried item
            for (auto& item : town.world.character.items) if (item.location == 0 && item.panel == 1) item.identified = false;
            return "ok " + std::to_string(d2d::rules::unidentified(town.world.character.items)) + "\n";
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
            make_boss(*scene, *best, d2d::rules::Boss::unique, mods, -1, town.world.rng(0x10000), town.world.character.header.active_difficulty(), town.world.rng);
            return std::format("ok #{} {} aura={} lvl={}\n", best->id, best->npc.name, best->aura, best->aura_lvl);
        }
        if (args.size() >= 2 && args[1] == "kill" && scene) {   // kill the level's monsters but <n> (quests)
            int keep = args.size() >= 3 ? std::atoi(args[2].c_str()) : 0;
            auto& monsters = town.world.fight.monsters;
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].alive() && keep-- <= 0 && hurt(*scene, monsters[i], monsters[i].hit_points, town.world.now)) town.world.fight.killed(i, town.world.now);
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "clearinv") {   // empty the inventory grid (tests that need room)
            std::erase_if(town.world.character.items, [](const auto& item) { return item.location == 0 && item.panel == 1; });
            return std::string("ok\n");
        }
        if (args.size() >= 2 && args[1] == "wear") {       // halve worn items' durability
            for (auto& item : town.world.character.items) if (item.location == 1) item.durability = d2d::rules::max_durability(item) / 2;
            return std::string("ok\n");
        }
        if (args.size() >= 4 && args[1] == "points" && scene) {   // give class skill <id> n points (tests)
            const auto& ids = scene->skills.class_ids[std::size_t(std::max(character.character_class, 0))];
            const auto found = std::ranges::find(ids, std::atoi(args[2].c_str()));
            if (found == ids.end()) return std::string("err not a class skill\n");
            town.world.character.stats.skills[std::size_t(found - ids.begin())] = std::uint8_t(std::clamp(std::atoi(args[3].c_str()), 0, 99));
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
                                || (skill.srvstfunc == 0 && skill.srvdofunc == 0 && (skill.passive_stat[0] >= 0 || skill.passive));
                if (!built) out += std::format("{} {} st{} do{}\n", skill.id, skill.name, skill.srvstfunc, skill.srvdofunc);
            }
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "states") {    // the player's buffs; each cursed / cried monster
            std::string out = std::format("absorb={} maxlife={} maxmana={} def={} buffs=", town.fight.absorb_pool,
                                          character.stats.fixed(d2d::d2s::kMaxLife), character.stats.fixed(d2d::d2s::kMaxMana), town.fight.player_combat.defense);
            for (const auto& state : town.fight.self_states) out += std::format("{}:{},", state.skill, state.level);
            out += "\n";
            for (const auto& monster : town.fight.monsters)
                if (monster.curse.skill >= 0 || monster.cry.skill >= 0)
                    out += std::format("{} hp={} curse={} cry={} dmg%={} speed%={} reflect%={} flee={} blind={}\n", monster.npc.name, monster.hit_points,
                                       monster.curse.skill, monster.cry.skill, monster.dmg_pct, monster.speed_pct, monster.reflect_pct, monster.flee_until, monster.blind_until);
            return out + "ok\n";
        }
        if (args.size() >= 2 && args[1] == "pets") {      // each pet: row level life dmg th ac res ranged/aura
            std::string out;
            for (const auto& pet : town.fight.pets)
                out += std::format("{} L{} hp={}/{} dmg={}-{} th={} ac={} res={},{},{},{} fire={}-{} ranged={}:{} aura={}:{} here={}\n",
                                   pet.monster.npc.name, pet.monster.stats.level, pet.monster.hit_points, pet.monster.stats.hit_points, pet.monster.stats.a1_min, pet.monster.stats.a1_max, pet.monster.stats.to_hit, pet.monster.stats.armor_class,
                                   pet.res[0], pet.res[1], pet.res[2], pet.res[3], pet.fire_lo, pet.fire_hi, pet.ranged, pet.ranged_level, pet.aura,
                                   pet.aura_level, pet.where == town.level);
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
            town.world.character.stats.values[args[1] == "statpts" ? d2d::d2s::kStatPts : d2d::d2s::kSkillPts] = std::atoi(args[2].c_str());
            return std::string("ok\n");
        }
        if (args.size() < 2 || args[1] != "collision")
            return std::string("err debug collision|automap|statpts <n>|skillpts <n>|wear|unid|level|blocked <x> <y>|warp <x> <y>|stat <id> <v>|quest <q>|skill left|right <id>|points <id> <n>\n");
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
            const int screen_y = int(kScreenHeight) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
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
        for (const auto& monster : town.fight.monsters) {
            const float dx = monster.unit.x - town.player.x, dy = monster.unit.y - town.player.y;
            const int screen_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int screen_y = int(kScreenHeight) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
            static constexpr std::array<const char*, 5> kBoss = { "-", "champion", "unique", "superunique", "minion" };
            std::string mods;
            for (const int mod : monster.mods) mods += (mods.empty() ? "" : ",") + std::to_string(mod);
            out += std::format("{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}/{}\t{}\t{}\t{}\tlvl{}\t{}\t#{}\n", scene->monsters.types[std::size_t(monster.type)].id,
                               monster.unit.x, monster.unit.y, screen_x, screen_y, monster.hit_points, monster.stats.hit_points, monster.mode, kBoss[std::size_t(monster.boss)], mods.empty() ? "-" : mods,
                               monster.stats.level, monster.npc.name, monster.id);
        }
        return out + "ok\n";
    });
    // Loot on the ground: "<code>\t<label>\t<sx>\t<sy>" (feet on screen, game pixels).
    channel.on("ground", [&](const std::vector<std::string>&) {
        std::string out;
        for (const auto& ground_item : town.loot.ground) {
            const float dx = ground_item.x - town.player.x, dy = ground_item.y - town.player.y;
            out += std::format("{}\t{}\t{}\t{}\t#{}\n", ground_item.item.code, ground_item.label,
                               int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
                               int(kScreenHeight) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))), ground_item.id);
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
             + " cam=" + std::format("{:.2f},{:.2f}", town.player.x, town.player.y)
             + " walking=" + (town.player.walking ? "1" : "0") + " dir=" + std::to_string(town.player.dir)
             + " saves=" + std::to_string(scene ? scene->saves.size() : 0)
             + " stash=" + (town.stash_open ? "1" : "0")
             + " cube=" + (town.cube_open ? "1" : "0")
             + " store=" + std::to_string(town.store.npc >= 0 ? town.store.vendor : -1)
             + " gold=" + std::to_string(character.stats.get(d2d::d2s::kGold))
             + " statpts=" + std::to_string(character.stats.get(d2d::d2s::kStatPts))
             + " str=" + std::to_string(character.stats.get(d2d::d2s::kStr))
             + " life=" + std::to_string(character.stats.fixed(d2d::d2s::kLife)) + "/" + std::to_string(character.stats.fixed(d2d::d2s::kMaxLife))
             + " mana=" + std::to_string(character.stats.fixed(d2d::d2s::kMana)) + "/" + std::to_string(character.stats.fixed(d2d::d2s::kMaxMana))
             + " level=" + std::to_string(character.stats.get(d2d::d2s::kLevel)) + " exp=" + std::to_string(character.stats.get(d2d::d2s::kExp))
             + " pmode=" + (town.fight.pmode >= 0 ? kModeCode[town.fight.pmode] : "-")
             + " missiles=" + std::to_string(town.fight.missiles.size())
             + " pets=" + [&] { std::string list; for (const auto& pet : town.fight.pets) list += (list.empty() ? "" : ",") + std::to_string(pet.monster.hit_points) + "/" + std::to_string(pet.monster.stats.hit_points) + ":" + std::string(pet.monster.mode); return list.empty() ? std::string("-") : list; }()
             + " lskill=" + std::to_string(town.skillbar.left) + " rskill=" + std::to_string(town.skillbar.right)
             + " picker=" + std::to_string(town.skillbar.picking)
             + " gamemenu=" + std::to_string(town.game_menu.open ? town.game_menu.menu + 1 : 0) + ":" + std::to_string(town.game_menu.sel)
             + " mini=" + (town.mini.open ? "1" : "0") + " volume=" + std::to_string(audio.master_volume) + "," + std::to_string(audio.music_volume)
             + " charges=" + [&] {
                   std::string charges;
                   for (const auto& charge : town.fight.charges) charges += std::format("{}{}:{}", charges.empty() ? "" : ",", charge.skill, charge.count);
                   return charges.empty() ? std::string("-") : charges;
               }()
             + " skillpts=" + std::to_string(character.stats.get(d2d::d2s::kSkillPts))
             + " tree=" + (town.tree_open ? std::to_string(town.tree_tab) : "0")
             + " waypoint=" + std::to_string(town.waypoint.open ? int(scene->waypoint_levels[std::size_t(town.waypoint.tab)].size()) : 0)
             + " items=" + std::to_string(character.items.size())
             + " held=" + (town.held ? town.held->code : "-")
             + " unid=" + std::to_string(d2d::rules::unidentified(character.items))
             + " merc=" + (town.merc ? std::format("{:.1f},{:.1f}", town.merc->x, town.merc->y) + ":" + town.world.merc_npc->code : std::string("-"))
             + " menu=" + std::to_string(town.npc_menu.npc >= 0 ? int(town.npc_menu.lines.size()) : 0)
             + " automap=" + std::to_string(town.automap.open ? int(town.automap.cells.size()) : 0)
             + " speech=" + std::to_string(town.speech.npc >= 0 ? int(town.speech.lines.size()) : 0)
             + " voice=" + std::to_string(audio.voice_sound())
             + " music=" + std::to_string(audio.music.sound)
             + " music_old=" + std::to_string(audio.music_old.sound)
             + "\nok\n";
    });
}

}  // namespace d2d::client
