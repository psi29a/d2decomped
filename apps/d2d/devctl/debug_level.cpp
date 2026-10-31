// SPDX-License-Identifier: GPL-3.0-or-later
// devctl `debug` sub-verbs for the level: where things are, warps, tiles, light and weather
// (docs/control_channel.md). In name order: a new one goes where it sorts.
#include "debug.hpp"

#include "panels.hpp"
#include "town.hpp"

#include <d2s_items.hpp>
#include <drlg.hpp>
#include <dt1.hpp>
#include <light.hpp>
#include <object_ids.hpp>
#include <outdoor_data.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::client {

void DebugVerbs::add_level() {
    add("automap", [this](const Args& /*args*/) -> Reply {   // reveal the whole level
        if (!scene) return std::nullopt;
        const auto& map = town.level->ds1;
        std::vector<DrawnCell> every;
        for (int y = 0; y < int(map.height()); ++y)
            for (int x = 0; x < int(map.width()); ++x) every.push_back({ x, y, nullptr });
        automap_reveal(*scene, *town.level, town.automap, every);
        return "ok " + std::to_string(town.automap.cells.size()) + "\n";
    });
    add("blocked", [this](const Args& args) -> Reply {   // can a unit stand at (x, y)?
        if (args.size() < 4 || !town.level) return std::nullopt;
        return std::format("ok {}\n", int(town.level->unit_blocked(std::strtof(args[2].c_str(), nullptr),
                                                               std::strtof(args[3].c_str(), nullptr))));
    });
    add("collision", [](const Args& /*args*/) -> Reply {   // draw the collision grid over the level, or stop
        g_debug_collision = !g_debug_collision;
        return std::string(g_debug_collision ? "ok on\n" : "ok off\n");
    });
    add("critters", [this](const Args& /*args*/) -> Reply {   // the client's critters: MonStats row, cell, walking, marked
        std::string out;
        for (const auto& critter : town.critters) out += std::format("{}\t{:.2f}\t{:.2f}\t{}\t{}\n", critter.row, critter.x, critter.y, critter.walking ? 1 : 0, critter.marked ? 1 : 0);
        return out + "ok\n";
    });
    add("day", [this](const Args& args) -> Reply {   // the time of day: `debug day [degrees]`
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
    });
    add("enter", [this](const Args& args) -> Reply {   // stand by warp i as if clicked: the next frame takes it
        if (args.size() < 3 || !town.level) return std::nullopt;
        const auto npc_index = std::size_t(std::atoi(args[2].c_str()));
        if (npc_index >= town.level->warps.size()) return std::string("err no such warp\n");
        std::tie(town.player.x, town.player.y) = town.level->nearest_free(town.level->warps[npc_index].x + 0.5f, town.level->warps[npc_index].y + 1.5f);
        town.target_x = town.player.x; town.target_y = town.player.y;
        town.player.walking = false;
        town.hero.take_warp = int(npc_index);
        return std::string("ok\n");
    });
    add("goto", [this](const Args& args) -> Reply {   // arrive on level id, at a free spot by its middle (quest tests)
        if (args.size() < 3 || !town.world.game_data) return std::nullopt;
        const auto* destination = town.world.game_data->level(std::atoi(args[2].c_str()));
        if (!destination || destination->ds1.width() == 0) return std::string("err no such level\n");
        const auto [spot_x, spot_y] = destination->nearest_free(float(destination->ds1.width()) / 2, float(destination->ds1.height()) / 2);
        town.world.arrive(destination, spot_x, spot_y, "debug goto");
        return std::format("ok {:.1f} {:.1f}\n", spot_x, spot_y);
    });
    add("grid", [this](const Args& args) -> Reply {   // the level, n spots a cell (subtiles: 5): '#' where a unit can't stand, a row a line
        if (!town.level) return std::nullopt;
        const int per_cell = args.size() >= 3 ? std::clamp(std::atoi(args[2].c_str()), 1, 5) : 1;
        const float step = 1.f / float(per_cell);
        std::string out;
        for (int y = 0; y < town.level->ds1.height() * per_cell; ++y) {
            for (int x = 0; x < town.level->ds1.width() * per_cell; ++x)
                out += town.level->unit_blocked((float(x) + 0.5f) * step, (float(y) + 0.5f) * step) ? '#' : '.';
            out += '\n';
        }
        return out + "ok\n";
    });
    add("laid", [this](const Args& /*args*/) -> Reply {   // the order relevel laid the level's rooms in: `<list index> <x> <y>` per room, the first `up` were up
        if (!town.level) return std::nullopt;
        const auto& level = *town.level;
        const std::size_t count = level.rooms.size();
        std::string out = std::format("up {}\n", level.laid_up);
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t list_index = level.laid.empty() ? step : level.laid[step];
            const auto& room = level.rooms[count - 1 - list_index];   // the list is newest first
            out += std::format("{} {} {}\n", list_index, room.x, room.y);
        }
        return out + "ok\n";
    });
    add("level", [this](const Args& /*args*/) -> Reply {   // where the player is
        if (!town.level) return std::nullopt;
        return std::format("ok {} {:.2f} {:.2f} {} {} {} {}\n", town.level->id, town.player.x, town.player.y,
                           town.level->ds1.width(), town.level->ds1.height(), town.level->world_x, town.level->world_y);
    });
    add("light", [this](const Args& args) -> Reply {   // the light grid round the player, a level (0..31) a subtile
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
    });
    add("objects", [this](const Args& /*args*/) -> Reply {   // operable objects: index, cell, kind (shrine, chest, opN), shrine row / trap, mode
        if (!town.level) return std::nullopt;
        std::string out;
        for (std::size_t i = 0; i < town.level->npcs.size(); ++i)
            if (const auto& npc = town.level->npcs[i]; npc.operate_fn > 0 && npc.root == "objects")
                out += std::format("{}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\n", i, npc.x, npc.y, npc.operate_fn == d2d::rules::operate_fn::kShrine ? "shrine" : npc.operate_fn == d2d::rules::operate_fn::kChest ? "chest" : std::format("op{}", npc.operate_fn),
                                   npc.operate_fn == d2d::rules::operate_fn::kShrine ? npc.shrine : npc.operate_fn == d2d::rules::operate_fn::kChest ? npc.trap : npc.object_id, npc.locked ? "locked" : "-",
                                   i < town.npc_states.size() && !town.npc_states[i].mode.empty() ? town.npc_states[i].mode : std::string_view(npc.mode));
        return out + "ok\n";
    });
    add("operate", [this](const Args& args) -> Reply {   // operate shrine / chest i where it stands
        if (args.size() < 3 || !town.level) return std::nullopt;
        const int npc_index = std::atoi(args[2].c_str());
        if (npc_index < 0 || std::size_t(npc_index) >= town.level->npcs.size()) return std::string("err no such object\n");
        town.operate(npc_index, town.now_ms, args.size() >= 4 ? std::atoi(args[3].c_str()) : -1);
        return std::format("ok life={} mana={} boost={}\n", town.hero.character.stats.fixed(d2d::d2s::kLife), town.hero.character.stats.fixed(d2d::d2s::kMana), town.hero.boost.shrine);
    });
    add("portals", [this](const Args& /*args*/) -> Reply {   // the player's portals: which, level id, cell (none: not open)
        std::string out;
        for (std::size_t i = 0; i < town.world.portal.size(); ++i)
            if (const auto& open = town.world.portal[i]; open.level) out += std::format("{}\t{}\t{:.1f}\t{:.1f}\n", i, open.level->id, open.x, open.y);
        return out + "ok\n";
    });
    add("presets", [this](const Args& /*args*/) -> Reply {   // every object / NPC the level placed: index, objects.txt id, cell, hidden, name
        if (!town.level) return std::nullopt;
        std::string out;
        for (std::size_t i = 0; i < town.level->npcs.size(); ++i) {
            const auto& npc = town.level->npcs[i];
            if (npc.object_id == 0 && npc.name.empty()) continue;
            out += std::format("{}\t{}\t{:.1f}\t{:.1f}\t{}\t{}\n", i, npc.object_id, npc.x, npc.y, i < town.npc_states.size() && town.npc_states[i].hidden ? 1 : 0, npc.name);
        }
        for (const auto& room : town.level->rooms)                // preset rooms (not laid yet too): LvlPrest def, its middle
            if (room.kind == 2) out += std::format("room\t{}\t{}\t{}\n", room.def, room.x + room.width / 2, room.y + room.height / 2);
        return out + "ok\n";
    });
    add("rain", [this](const Args& args) -> Reply {   // the weather: `debug rain [state]` (0 clear .. 3 falling) starts that state next tick
        auto& rain = town.rain;
        if (args.size() >= 3) { rain.state = (std::atoi(args[2].c_str()) + 3) % 4; rain.left = 0; }
        std::string out = "ok state=" + std::to_string(rain.state) + " density=" + std::to_string(rain.density) + " drops=" + std::to_string(rain.drops.size())
                        + " splashes=" + std::to_string(rain.splashes.size()) + " bubbles=" + std::to_string(rain.bubbles.size()) + " wind=" + std::to_string(rain.wind)
                        + " lightning=" + std::to_string(rain.lightning) + " second=" + std::to_string(rain.second) + " flash=" + std::to_string(rain.flash)
                        + " strike_left=" + std::to_string(rain.strike_left) + " flashing=" + std::to_string(rain.flashing);
        if (!rain.bubbles.empty()) {
            const auto& bubble = rain.bubbles.front();
            out += " bubble=" + std::to_string(bubble.x) + "," + std::to_string(bubble.y) + "," + std::to_string(bubble.kind) + "," + std::to_string(bubble.frame);
        }
        return out + "\n";
    });
    add("relevel", [this](const Args& args) -> Reply {   // lay the level's tiles again, rooms up in this order: `debug relevel <x>,<y> ...` (room cells)
        if (args.size() < 3 || !town.level) return std::nullopt;
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
    });
    add("tiles", [this](const Args& args) -> Reply {   // every picked tile: `x y wall|floor|shadow o<orientation> <dt1>:<index>` (whisper town's form, level cells)
        if (!town.level) return std::nullopt;
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
    });
    add("tomb", [this](const Args& /*args*/) -> Reply {   // the true Tal Rasha's Tomb (the act seed's roll, FUN_0061aeb0)
        if (!town.world.game_data) return std::string("err no game data\n");
        auto act = d2d::drlg::act_seed(town.world.game_data->map_seed);
        return std::format("ok {}\n", d2d::drlg::roll_act2_tombs(act).boss);
    });
    add("walls", [this](const Args& args) -> Reply {   // a cell's wall tiles: `debug walls <x> <y>` -> layer:orientation ...
        if (args.size() < 4) return std::nullopt;
        const int x = std::atoi(args[2].c_str()), y = std::atoi(args[3].c_str());
        const auto& level = *town.level;
        std::string out = "ok";
        if (x >= 0 && y >= 0 && x < int(level.ds1.width()) && y < int(level.ds1.height())) {
            const auto off = std::size_t(y) * std::size_t(level.ds1.width()) + std::size_t(x);
            if (!level.picks.empty()) for (const auto& pick : level.picks[off]) out += std::format(" {}:{}", int(pick.layer), int(pick.orient));
            else for (const auto& layer : level.ds1.walls()) out += std::format(" w{}{}", layer.cells[off].wall_type, layer.cells[off].hidden ? "h" : "");
        }
        return out + "\n";
    });
    add("warp", [this](const Args& args) -> Reply {   // put the player at cell (x, y)
        if (args.size() < 4) return std::nullopt;
        town.player.x = town.target_x = std::strtof(args[2].c_str(), nullptr);
        town.player.y = town.target_y = std::strtof(args[3].c_str(), nullptr);
        town.player.walking = false;
        return std::string("ok\n");
    });
    add("warps", [this](const Args& /*args*/) -> Reply {   // the level's warps: index, cell, where to
        if (!town.level) return std::nullopt;
        std::string out;
        for (std::size_t i = 0; i < town.level->warps.size(); ++i)
            out += std::format("{}\t{:.1f}\t{:.1f}\t{}\n", i, town.level->warps[i].x, town.level->warps[i].y, town.level->warps[i].destination);
        return out + "ok\n";
    });
    add("wp", [this](const Args& args) -> Reply {   // activate waypoint index n
        if (args.size() < 3) return std::nullopt;
        town.world.set_waypoint(std::atoi(args[2].c_str()));
        return std::string("ok\n");
    });
}

}  // namespace d2d::client
