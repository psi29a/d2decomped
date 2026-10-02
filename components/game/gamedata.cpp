// SPDX-License-Identifier: GPL-3.0-or-later
// GameData's runtime side: levels built on demand (the level builder),
// their populations, the COF timings the World reads. A translation unit
// of its own; load.hpp fills GameData at start.
#include "gamedata.hpp"

#include "log.hpp"

#include <compcode.hpp>
#include <dt1.hpp>
#include <maze.hpp>
#include <monsters.hpp>
#include <mpq.hpp>
#include <outdoor.hpp>
#include <outdoor_data.hpp>
#include <quests.hpp>
#include <room_tiles.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <tile_pick.hpp>
#include <txt.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// A composite's COF, the World's part of loading it: the file, its name
// (animdata's key) and timing.
CofAnim open_cof(const d2d::mpq::Stack& mpqs, const std::string& path) {
    CofAnim out;
    const auto bytes = mpqs.try_read(path);
    if (!bytes) return out;
    try {
        out.cof = d2d::cof::Cof(*bytes);
        out.timing.cof_speed = out.cof.speed(); out.timing.cof_frames = out.cof.frames_per_direction();
        out.timing.directions = out.cof.directions();
        const std::string_view path_view(path);
        out.timing.name = std::string(path_view.substr(path_view.rfind('\\') + 1, path_view.rfind('.') - path_view.rfind('\\') - 1));
        for (auto& letter : out.timing.name) letter = char(std::toupper(letter));
        out.path = path;
        out.ok = true;
    } catch (const std::exception& error) {
        d2d::log::warn("{}: {}", path, error.what());
    }
    return out;
}
// A player class's mode: COF <CC><mode><wclass>, the weapon class from the
// hand/shield bytes (compcode::weapon_class), hth when that one's missing.
CofAnim player_cof(const d2d::mpq::Stack& mpqs, const std::vector<d2d::compcode::Entry>& comp, int cls, int mode,
                   const std::array<std::uint8_t, 32>& gfx) {
    const char* class_code = kCharCode[cls];
    const std::string weapon_class(comp.empty() ? std::string_view{} : d2d::compcode::weapon_class(cls, comp, gfx[5], gfx[6], gfx[7]));
    auto path = [&](std::string_view weapon) { return std::format(R"(data\global\CHARS\{}\COF\{}{}{}.cof)", class_code, class_code, kModeCode[mode], weapon); };
    if (auto cof = open_cof(mpqs, path(weapon_class.empty() ? "hth" : weapon_class)); cof.ok) return cof;
    return open_cof(mpqs, path("hth"));
}
// An NPC's or object's: <root>\<code>\COF\<code><mode><BaseW>.
CofAnim npc_cof(const d2d::mpq::Stack& mpqs, const Npc& npc, std::string_view mode) {
    return open_cof(mpqs, std::format(R"(data\global\{}\{}\COF\{}{}{}.cof)", npc.root, npc.code, npc.code, mode, npc.base_w));
}
// The timing lookups: the COF and animdata only (the World's).
template <class Key, class Load>
const GameData::AnimTiming& timing_of(const GameData& game_data, std::map<Key, GameData::AnimTiming>& cache, const Key& key, Load&& load) {
    auto found = cache.find(key);
    if (found == cache.end()) {
        GameData::AnimTiming timing = load();
        if (const auto anim = game_data.anim_data.find(timing.name); anim != game_data.anim_data.end()) std::tie(timing.speed, timing.frames, timing.action) = std::tuple{ anim->second.speed, anim->second.frames, anim->second.action };
        found = cache.emplace(key, std::move(timing)).first;
    }
    return found->second;
}
const GameData::AnimTiming& GameData::npc_timing(const Npc& npc, std::string_view mode) const {
    auto key = npc.root + "/" + npc.code + "/" + std::string(mode) + "/" + npc.base_w;
    for (const auto& component : npc.comp) key += "/" + component;
    return timing_of(*this, npc_timings, key, [&] { return npc_cof(mpqs, npc, mode).timing; });
}
const GameData::AnimTiming& GameData::composite_timing(int d2s_class, int mode, const std::array<std::uint8_t, 32>& gfx) const {
    std::array<std::uint8_t, 18> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.begin() + 16, key.begin() + 2);
    return timing_of(*this, composite_timings, key, [&] { return player_cof(mpqs, comp, d2s_class, mode, gfx).timing; });
}

std::vector<std::string> split_variants(std::string_view text) {
    std::vector<std::string> out;
    while (!text.empty() && (text.front() == '"' || text.back() == '"')) text = text.front() == '"' ? text.substr(1) : text.substr(0, text.size() - 1);
    while (!text.empty()) {
        const auto comma = text.find(',');
        out.emplace_back(text.substr(0, comma));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return out;
}

// MonStats2 row by Id.
std::unordered_map<std::string, std::size_t> id_rows(const d2d::txt::Table& table) {
    std::unordered_map<std::string, std::size_t> out;
    for (std::size_t row = 0; row < table.size(); ++row) out.emplace(std::string(table.get(row, "Id")), row);
    return out;
}

// A monster unit from its MonStats row and the MonStats2 row its
// MonStatsEx names (the tables aren't row-aligned: 734 vs 609 rows).
// A level's own light (FUN_00474550 via FUN_00619d70): when any of Levels.txt
// Intensity / Red / Green / Blue is set its Intensity replaces the day's.
int level_light(std::string_view intensity, std::string_view red, std::string_view green, std::string_view blue) {
    auto number = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    return number(intensity) || number(red) || number(green) || number(blue) ? number(intensity) : -1;
}

Npc monster_npc(const GameData& game_data, const d2d::txt::Table& monstats, const d2d::txt::Table& ms2,
                const std::unordered_map<std::string, std::size_t>& ms2_rows, std::size_t row) {
    const auto found = ms2_rows.find(std::string(monstats.get(row, "MonStatsEx")));
    const std::size_t row2 = found == ms2_rows.end() ? row : found->second;
    Npc npc;
    npc.root   = "monsters";
    npc.mode   = "NU";
    npc.code   = std::string(monstats.get(row, "Code"));
    npc.hc_idx = std::atoi(std::string(monstats.get(row, "hcIdx")).c_str());
    npc.id     = std::string(monstats.get(row, "Id"));
    npc.base_w = std::string(ms2.get(row2, "BaseW"));
    npc.size_x = std::atoi(std::string(ms2.get(row2, "SizeX")).c_str());
    npc.size_y = std::atoi(std::string(ms2.get(row2, "SizeY")).c_str());
    npc.light  = std::atoi(std::string(ms2.get(row2, "Light")).c_str());
    npc.overlay_class = std::atoi(std::string(ms2.get(row2, "OverlayHeight")).c_str()) - 1;
    npc.trans_lvl = std::atoi(std::string(monstats.get(row, "TransLvl")).c_str());
    npc.no_unique_shift = ms2.get(row2, "noUniqueShift") == "1";
    npc.utrans = { std::atoi(std::string(ms2.get(row2, "Utrans")).c_str()), std::atoi(std::string(ms2.get(row2, "Utrans(N)")).c_str()),
                 std::atoi(std::string(ms2.get(row2, "Utrans(H)")).c_str()) };
    if (const auto velocity = monstats.get(row, "Velocity"); !velocity.empty()) npc.velocity = float(std::atoi(std::string(velocity).c_str()));
    // Hover name: MonStats' string key, only for units MonStats2 marks
    // selectable (isSel) — not the chicken or the camp's guard rogues,
    // whose name key "Dummy" reads "an evil force".
    if (ms2.get(row2, "isSel") == "1") {
        std::string key(monstats.get(row, "NameStr"));        // 1.14d; "namco" on the CD
        if (key.empty()) key = std::string(monstats.get(row, "namco"));
        auto name = lookup_string(game_data, key);
        npc.name = name ? u16_to_latin1(*name) : key;
    }
    if (npc.code.empty()) return npc;
    if (npc.base_w.empty()) npc.base_w = "hth";
    for (std::size_t layer = 0; layer < 16; ++layer) {
        if (ms2.get(row2, kLayerCode[layer]) != "1") continue;
        const auto variants = split_variants(ms2.get(row2, kVariant[layer]));
        npc.comp[layer] = variants.empty() || variants[0].empty() ? "lit" : variants[0];
    }
    return npc;
}

// Skills (components/rules/skills.hpp): skillcalc.txt's operand names,
// ItemStatCost's stat names and Skills.txt's rows with their calcs
// compiled, SkillDesc's tab, icon and name. A calc that doesn't compile is
// logged and reads 0.

Spawning start_spawning(const GameData& game_data, int difficulty) {
    Spawning spawning;
    spawning.difficulty = std::clamp(difficulty, 0, 2);
    // FUN_00530930: the regions (FUN_00547d20), the object seed
    // (FUN_00546c60), sunitproxy (FUN_00536070) and quests (FUN_00545d80)
    // each take a step of the game seed, {map seed, 666} with -seed
    // (FUN_0052c280); the regions are made on theirs, levels 1 up.
    spawning.game = d2d::rules::Rng{ game_data.map_seed };
    d2d::rules::Rng region_seed{ spawning.game.next() };
    spawning.objects = d2d::rules::Rng{ spawning.game.next() };   // object_seed()
    for (int step = 0; step < 2; ++step) spawning.game.next();
    spawning.regions.resize(game_data.level_mon.size());
    for (std::size_t id = 1; id < game_data.level_mon.size(); ++id)
        spawning.regions[id] = d2d::rules::monster_region(game_data.monsters, game_data.level_mon[id], spawning.difficulty, region_seed);
    return spawning;
}

namespace {

struct RoomRect { int x, y, width, height; };           // act tiles

// A level's rooms, act tiles, in the order they were made (Level::rooms).
// The camp (a preset level, no generated rooms) as 8x8 rooms, row by row:
// its preset split (FUN_00666680), each put at the head of the level's
// list, as game.exe's (tools/emu drlg.py, level +0x10).
std::vector<RoomRect> room_rects(const Level& level) {
    std::vector<RoomRect> rects;
    if (!level.rooms.empty()) {
        for (const auto& room : level.rooms) rects.push_back({ level.world_x + room.x, level.world_y + room.y, room.width, room.height });
        return rects;
    }
    const int width = level.ds1.width() - 1, height = level.ds1.height() - 1;   // the DS1's last column and row are its edge
    for (int y = 0; y < height; y += 8)
        for (int x = 0; x < width; x += 8)
            rects.push_back({ level.world_x + x, level.world_y + y, std::min(8, width - x), std::min(8, height - y) });
    return rects;
}

std::size_t room_count(const Level& level) {
    return level.rooms.empty() ? std::size_t((level.ds1.width() + 6) / 8) * std::size_t((level.ds1.height() + 6) / 8) : level.rooms.size();
}

int room_holding(const std::vector<RoomRect>& rects, const Level& level, float x, float y) {
    const int tile_x = level.world_x + int(std::floor(x)), tile_y = level.world_y + int(std::floor(y));
    for (std::size_t i = 0; i < rects.size(); ++i)
        if (tile_x >= rects[i].x && tile_y >= rects[i].y && tile_x < rects[i].x + rects[i].width && tile_y < rects[i].y + rects[i].height) return int(i);
    return -1;
}

// FUN_0066bc20's test: under 6 tiles apart on both axes.
bool close(const RoomRect& room, const RoomRect& other) {
    const int gap_x = room.x < other.x ? other.x - room.width - room.x : room.x - other.width - other.x;
    const int gap_y = room.y < other.y ? other.y - room.height - room.y : room.y - other.height - other.y;
    return gap_x < 6 && gap_y < 6;
}

struct NearRoom { const Level* level; int room; RoomRect rect; };

// FUN_0066bbc0: a bubble sort moving a room wholly left of or above the
// one before it ahead.
void sort_near(std::vector<NearRoom>& list) {
    for (std::size_t pass = list.size() ? list.size() - 1 : 0; pass > 0; --pass)
        for (std::size_t i = 0; i + 1 < list.size(); ++i) {
            const auto &first = list[i].rect, &second = list[i + 1].rect;
            if (second.x + second.width <= first.x || second.y + second.height <= first.y) std::swap(list[i], list[i + 1]);
        }
}

// A room's near list (room2 +8, FUN_0066c370): its level's rooms close to
// it in the level's list order (newest first), itself included, sorted;
// then each close room of a level next to it (FUN_0066be80), and for a
// warp to another level that level's room with the warp back
// (FUN_0066be10), each appended and the list sorted again (FUN_0066bda0).
// ponytail: game.exe links only rooms flagged (+0x28 bits 0x10..0x800) for
// the level next door; closeness stands in for the flag.
std::vector<NearRoom> near_list(const GameData& game_data, const Level& level, int room) {
    const auto rects = room_rects(level);
    const auto& self = rects[std::size_t(room)];
    std::vector<NearRoom> list;
    for (std::size_t i = rects.size(); i-- > 0;)
        if (close(self, rects[i])) list.push_back({ &level, int(i), rects[i] });
    sort_near(list);
    for (const auto& neighbour : level.nearby) {
        const auto other = room_rects(*neighbour.level);
        for (std::size_t i = other.size(); i-- > 0;)
            if (close(self, other[i])) { list.push_back({ neighbour.level, int(i), other[i] }); sort_near(list); }
    }
    for (const auto& warp : level.warps) {
        if (room_holding(rects, level, warp.x, warp.y) != room) continue;
        const Level* destination = game_data.level(warp.destination);
        if (!destination || destination->rooms.empty()) continue;
        const auto back = std::ranges::find(destination->warps, level.id, &Level::Warp::destination);
        if (back == destination->warps.end()) continue;
        const auto other = room_rects(*destination);
        if (const int there = room_holding(other, *destination, back->x, back->y); there >= 0) {
            list.push_back({ destination, there, other[std::size_t(there)] });
            sort_near(list);
        }
    }
    return list;
}

// One room coming into play at the spawning's difficulty (FUN_0052d0f0):
// its preset units (FUN_005559a0 → FUN_0054e600), its object groups
// (FUN_00552610), then the room populated (FUN_0054ec90), all on the room1
// seed; the objects it made stamped into the walk grid.
void populate(const GameData& game_data, Spawning& spawning, const Level& level, std::size_t made_index) {
    static constexpr const char* kSfx[3] = { "", "(N)", "(H)" };
    const int difficulty = spawning.difficulty;
    auto& state = spawning.levels[&level];
    // A camp room: no object groups and no population (the object seed
    // stays put); its preset units (FUN_005559a0: the objects, then the
    // monsters, each the room's preset list, the DS1's order reversed)
    // each made (FUN_00555230 → FUN_00552df0) on a step of the game seed,
    // their unit seed. Proven: tools/emu diff_drlg.py game, $LEVELS from 1.
    // ponytail: the camp's room1 seeds aren't kept; nothing in the camp rolls on them.
    if (level.rooms.empty()) {
        if (state.up.empty()) state.up.assign(room_count(level), false);
        if (made_index >= state.up.size() || state.up[made_index]) return;
        state.up[made_index] = true;
        for (const char* root : { "objects", "monsters" })
            for (auto it = level.npcs.rbegin(); it != level.npcs.rend(); ++it)
                if (it->room == int(made_index) && !it->quest && it->root == root) it->seed = d2d::rules::Rng{ spawning.game.next() };
        return;
    }
    const auto& monsters = game_data.monsters;
    const auto& region = std::size_t(level.id) < spawning.regions.size() ? spawning.regions[std::size_t(level.id)] : d2d::rules::Region{};
    // FUN_0054ebc0: none in a room flagged 0x800000 — a warp tile's
    // (FUN_0066e360) or one whose near list reaches a town (FUN_0066bd50,
    // FUN_006426a0); the level's totals (FUN_00642be0) leave them out.
    auto nopop = [&](std::size_t index) {
        return (index < level.nopop_rooms.size() && level.nopop_rooms[index])
            || std::ranges::any_of(near_list(game_data, level, int(index)), [&](const NearRoom& other) { return other.level == &game_data.town; });
    };
    if (state.up.empty()) {
        state.up.assign(level.rooms.size(), false);
        int total = 0;
        for (std::size_t index = 0; index < level.rooms.size(); ++index) total += !nopop(index);
        state.pop = { 0, total, 0, level.mon.umin[std::size_t(difficulty)], level.mon.umax[std::size_t(difficulty)], difficulty, &game_data.umods };
        level.region[std::size_t(difficulty)].clear();
        for (const auto& [row, rarity] : region.types) level.region[std::size_t(difficulty)].push_back(row);
    }
    if (made_index >= level.rooms.size() || state.up[made_index] || level.walk.empty()) return;
    state.up[made_index] = true;
    auto& spawns = state.spawns;
    const std::size_t first = spawns.size();
    auto& pop = state.pop;
    const auto& made = level.rooms[made_index];
    auto& level_rw = const_cast<Level&>(level);                 // GameData owns its levels mutable
    const auto npcs_before = level.npcs.size();
    auto stamp_made = [&] {
        for (auto i = npcs_before; i < level_rw.npcs.size(); ++i) stamp_footprint(level_rw, level_rw.npcs[i]);
    };
    // Normal: objgroups.cpp's port, game.exe's to the subtile.
    if (difficulty == 0) {
        room_objects(game_data, spawning, level, made_index, true);
        stamp_made();
        if (spawns.size() > first)
            d2d::log::info("  room ({}, {}) of {} {}: {} monsters", made.x, made.y, level.name, kSfx[difficulty], spawns.size() - first);
        return;
    }
    // The room1 seed: one step (FUN_0054f060) and the preset objects
    // (room_objects), the preset monsters, the object groups (room_groups),
    // then the population (FUN_0052d0f0).
    d2d::rules::SpawnRoom room{ made.x * 5, made.y * 5, made.width * 5, made.height * 5, room_objects(game_data, spawning, level, made_index, false) };
    if (made_index < level.room_areas.size())                   // FUN_0054ec90: areas with an id, not skipped, not empty
        for (const auto& area : level.room_areas[made_index])
            if (area.id && !area.skip && (area.left || area.top || area.right || area.bottom))
                room.areas.push_back({ area.left * 5, area.top * 5, (area.right - area.left) * 5, (area.bottom - area.top) * 5 });
    if (room.areas.empty()) room.areas.push_back({ 0, 0, 0, 0 });   // all skipped: no tries
    // Not within WarpDist (2025 = 45^2 subtiles) of where players come
    // in: the camp for the Blood Moor, the warps for a level entered by one.
    // FUN_0054db50: within WarpDist (Levels +0xc, 2025 squared subtiles)
    // of a point of the level's list (+0x1e0, FUN_00642444: the centre of
    // each room with a warp or flagged 0x30000, a waypoint's) or of its
    // 0xb point (FUN_0066b2b0: the waypoint's tile, FUN_0066ad80).
    const auto rects = room_rects(level);
    std::vector<std::pair<int, int>> ways;
    auto centre = [&](int index) {
        if (index >= 0) ways.push_back({ (level.rooms[std::size_t(index)].x + level.rooms[std::size_t(index)].width / 2) * 5,
                                         (level.rooms[std::size_t(index)].y + level.rooms[std::size_t(index)].height / 2) * 5 });
    };
    for (const auto& warp : level.warps) centre(room_holding(rects, level, warp.x, warp.y));
    for (const auto& unit : level.units)
        if (unit.type == d2d::rules::unit_type::kObject && unit.id >= 0 && std::size_t(unit.id) < game_data.obj_subclass.size() && (game_data.obj_subclass[std::size_t(unit.id)] & d2d::rules::object_ids::kSubclassWaypoint)) {
            const int index = room_holding(rects, level, float(unit.x) / 5, float(unit.y) / 5);
            centre(index);
            ways.push_back({ unit.x / 5 * 5, unit.y / 5 * 5 });
        }
    auto near_way = [&](int x, int y) {
        return std::ranges::any_of(ways, [&](const auto& way) { return (x - way.first) * (x - way.first) + (y - way.second) * (y - way.second) < 2025; });
    };
    // Clear ground, and no monster already within its 2-subtile footprint
    // (game.exe stamps each placed monster into collision, 0x800).
    // Level::unit_blocked's plus, less the level's NPCs' own footprints
    // (stamp_footprints): Flavie isn't there until her preset unit is made.
    auto walk_blocked = [&](int x, int y) {
        if (!level.blocked((float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5)) return false;
        if (x < 0 || y < 0 || x >= level.ds1.width() * 5 || y >= level.ds1.height() * 5) return true;
        if (level.walk[std::size_t(y) * std::size_t(level.ds1.width() * 5) + std::size_t(x)] & 0x08) return true;
        for (const auto& npc : level.npcs) {
            if (npc.root == "objects") continue;
            const int left = int(npc.x * 5) - npc.size_x / 2, top = int(npc.y * 5) - npc.size_y / 2, bit = (y - top) * npc.size_x + x - left;
            if (x >= left && y >= top && x < left + npc.size_x && y < top + npc.size_y && !(bit < 32 && npc.walls >> bit & 1)) return false;
        }
        return true;
    };
    std::vector<std::pair<int, int>> npcs;                     // NPCs made here (Flavie): they block too, drawn by the level's NPCs
    auto fits = [&](int x, int y) {
        for (const auto& other : spawns) if (std::abs(other.x - x) < 2 && std::abs(other.y - y) < 2) return false;
        for (const auto& [npc_x, npc_y] : npcs) if (std::abs(npc_x - x) < 2 && std::abs(npc_y - y) < 2) return false;
        return !walk_blocked(x, y) && !walk_blocked(x - 1, y) && !walk_blocked(x + 1, y) && !walk_blocked(x, y - 1) && !walk_blocked(x, y + 1);
    };
    // Its preset units (Level::units in this room), as FUN_0054e600 spawns
    // them (docs/research/re/monsters.md "Preset units on the server"): a
    // MonStats row at its spot; a superunique with its minions; a MonPlace
    // code by the switch (Fallen and shamans, champion packs, unique packs,
    // Blood Raven, Flavie, the tight spot boss, the dead ones; the rest,
    // group25..100 among them, spawn nothing).
    // ponytail: the level-list walk up a family's chain (FUN_0063ec70's
    // second half) and the champion / unique pick from Levels.txt umon1..
    // in normal aren't there (the region's list stands in); a boss's company
    // rolls the room seed, not the monster's own.
    using d2d::rules::monster_detail::place;
    const int nmon = int(game_data.mon_bin.size()), nsu = int(game_data.superuniques.size());
    auto bin = [&](int bin_row) { return bin_row >= 0 && bin_row < nmon ? int(game_data.mon_bin[std::size_t(bin_row)]) : -1; };
    // FUN_005b2f20 at the spot (radius -1), then within `retry` if taken.
    auto at_spot = [&](int type, int x, int y, int retry, bool dead = false) {
        if (type < 0) return;
        int spot_x, spot_y;
        if (place(room, x, y, -1, fits, spot_x, spot_y) || (retry > 0 && place(room, x, y, retry, fits, spot_x, spot_y)))
            spawns.push_back({ type, spot_x, spot_y, -1, -1, d2d::rules::Boss::none, {}, 0, spawning.game.next(), {}, dead });
    };
    auto npc_at = [&](int x, int y, int retry) {                  // at_spot for an NPC: made and blocking, not a spawn
        int spot_x, spot_y;
        if (place(room, x, y, -1, fits, spot_x, spot_y) || (retry > 0 && place(room, x, y, retry, fits, spot_x, spot_y))) {
            npcs.push_back({ spot_x, spot_y });
            spawning.game.next();
        }
    };
    // FUN_0063ec70 + FUN_0054e2a0: a base monster as the level has it — the
    // first of its family in the level's list, then Carvers / Devilkin (and
    // their shamans) by level.
    auto own = [&](int base_bin) {
        int row = bin(base_bin);
        if (row < 0) return -1;
        for (const int monstats_row : level.mon.mon)
            if (monstats_row >= 0 && std::size_t(monstats_row) < monsters.types.size() && monsters.types[std::size_t(monstats_row)].base == monsters.types[std::size_t(row)].base) { row = monstats_row; break; }
        const int base = monsters.types[std::size_t(row)].base, id = level.id;
        if (base == bin(0x13)) return id == 6 ? bin(0x14) : id == 7 || id == 12 || id == 16 ? bin(0x15) : row;
        if (base == bin(0x3a)) return id == 6 || id == 7 ? bin(0x3b) : id == 12 || id == 16 ? bin(0x3c) : row;
        return row;
    };
    for (const auto& unit : level.units) {
        if (unit.x < room.x || unit.y < room.y || unit.x >= room.x + room.width || unit.y >= room.y + room.height) continue;
        if (unit.type != d2d::rules::unit_type::kMonster || unit.id < 0) continue;            // objects and warp tiles: room_objects
        if (unit.id < nmon) {                                                // a MonStats row (FUN_0054e490)
            const auto monstats_row = game_data.mon_bin[std::size_t(unit.id)];
            const bool stay = unit.id == 0xe5 || (unit.id >= 0x11c && unit.id <= 0x120) || unit.id == 0x188 || unit.id == 0x189;   // FUN_0054e3a0
            if (!game_data.mon_is_npc[monstats_row]) at_spot(int(monstats_row), unit.x, unit.y, stay ? 0 : 4);
            else {                                                           // an NPC (Flavie), standing at its spot
                npc_at(unit.x, unit.y, stay ? 0 : 4);
                if (!game_data.mon_npc[monstats_row].code.empty()) {
                    auto npc = game_data.mon_npc[monstats_row];
                    npc.x = (float(unit.x) + 0.5f) / 5;
                    npc.y = (float(unit.y) + 0.5f) / 5;
                    level_rw.npcs.push_back(std::move(npc));
                }
            }
            continue;
        }
        if (unit.id >= nmon + nsu) {                                         // MonPlace
            const int code = unit.id - nmon - nsu;
            if (code == 0x11 || code == 0x12) at_spot(own(code == 0x11 ? 0x13 : 0x3a), unit.x, unit.y, 4);   // place_fallen / _fallenshaman
            else if (code == 0x05) at_spot(bin(0x10b), unit.x, unit.y, 0);      // place_bloodraven
            else if (code == 0x08) at_spot(bin(0x11c), unit.x, unit.y, 0);      // place_tightspotboss (flag 8)
            else if (code == 0x04) npc_at(unit.x, unit.y, 0);                  // place_rogue_warner: Flavie
            else if (code >= 0x1d && code <= 0x20) {                         // deadminion / deadimp / deadbarb / reanimateddead: mode 12, unit +0xc4 |= 0x2000000
                // ponytail: reanimateddead's (0x1b6) rand(50) on its own seed and its FUN_005417d0 event aren't there.
                static constexpr int kDeadBase[4] = { 0x1c5, 0x1ec, 0x20a, 0x1b6 };   // 0x54e9a0..0x54e9c2
                at_spot(own(code == 0x1e && level.id == d2d::rules::level_ids::kBloodyFoothills ? 0x211 : kDeadBase[code - 0x1d]), unit.x, unit.y, 4, true);
            }
            else if ((code == 0x02 || code == 0x03) && !region.types.empty()) {
                const int type = d2d::rules::pick_type(region, room.seed);   // FUN_005bde80, the unique pick
                int spot_x, spot_y, leader_x, leader_y;
                if (code == 0x03) {                                       // place_champion: at the spot, mod 16, 1..3 more (FUN_0054e1e0)
                    if (!place(room, unit.x, unit.y, -1, fits, spot_x, spot_y)) continue;
                    const int lead = int(spawns.size());
                    spawns.push_back({ type, spot_x, spot_y, lead, -1, d2d::rules::Boss::champion, { d2d::rules::umod::champion }, 0, spawning.game.next() });
                    for (int remaining = room.seed(3) + 1; remaining > 0; --remaining)
                        if (place(room, spot_x, spot_y, 4, fits, leader_x, leader_y))
                            spawns.push_back({ type, leader_x, leader_y, lead, -1, d2d::rules::Boss::champion, { d2d::rules::umod::champion }, 0, spawning.game.next() });
                } else if (d2d::rules::room_spot(room, fits, near_way, leader_x, leader_y) && place(room, leader_x, leader_y, -1, fits, spot_x, spot_y)) {
                    d2d::rules::boss_pack(monsters, type, spot_x, spot_y, room, fits, spawns, pop, spawning.game);   // place_unique_pack: a random spot of the room (FUN_005a43e0)
                }
            }
            continue;
        }
        // A superunique (FUN_005a49b0): once a game unless Stacks, at its
        // spot or (AutoPos) a random one in the room (FUN_0054dc40, no
        // entrance check), its mods and MonUMod 22 (questcomplete), then
        // MinGrp..MaxGrp (each + difficulty when both are set) of minion1
        // (else its own type) at radius 3 (FUN_005a0c00 / FUN_005b23c0).
        // Its specials (the Countess's stat 0x76, AI 0xd) in make_boss.
        // ponytail: its unique stat bonuses and TC come in the fight / loot;
        // quest binding (FUN_005436b0: the Countess's 5, the Cow King's 4)
        // as the kill's superunique row (world.cpp); its mods roll the room
        // seed, not the monster's own.
        const int superunique_index = unit.id - nmon;
        const auto& sup = game_data.superuniques[std::size_t(superunique_index)];
        if (sup.type < 0 || (!sup.stacks && spawning.superuniques.test(std::size_t(superunique_index)))) continue;
        int spot_x = unit.x, spot_y = unit.y, leader_x, leader_y;
        if (sup.autopos && !d2d::rules::room_spot(room, fits, [](int, int) { return false; }, spot_x, spot_y)) continue;
        if (!place(room, spot_x, spot_y, -1, fits, leader_x, leader_y) && !place(room, spot_x, spot_y, 5, fits, leader_x, leader_y)) continue;
        spawning.superuniques.set(std::size_t(superunique_index));
        const int lead = int(spawns.size());
        auto mods = d2d::rules::superunique_mods(game_data.umods, monsters.types[std::size_t(sup.type)], sup.mods, difficulty, room.seed);
        mods.push_back(22);
        spawns.push_back({ sup.type, leader_x, leader_y, -1, superunique_index, d2d::rules::Boss::superunique, std::move(mods), 0, spawning.game.next() });
        for (const auto& [off_x, off_y] : unit.path) spawns.back().path.emplace_back(unit.x + off_x, unit.y + off_y);   // FUN_00555910: the preset's map AI
        const int minion = monsters.types[std::size_t(sup.type)].minion[0] >= 0 ? monsters.types[std::size_t(sup.type)].minion[0] : sup.type;
        const int low = sup.min_grp + (sup.min_grp && sup.max_grp ? difficulty : 0), high = sup.max_grp + (sup.min_grp && sup.max_grp ? difficulty : 0);
        const int count = room.seed.range(low, std::max(low, high));
        int placed = 0;
        for (int k = 0; k < count; ++k) {
            int minion_x, minion_y;
            if (place(room, leader_x, leader_y, 3, fits, minion_x, minion_y)) {
                spawns.push_back({ minion, minion_x, minion_y, lead, -1, d2d::rules::Boss::minion, {}, 0, spawning.game.next() });
                ++placed;
            }
        }
        d2d::log::info("  {} ({}) with {} minions at ({:.1f}, {:.1f})", sup.name, monsters.types[std::size_t(sup.type)].id, placed,
                       (float(leader_x) + 0.5f) / 5, (float(leader_y) + 0.5f) / 5);
    }
    room_groups(game_data, spawning, level, made_index, room.seed);
    if (nopop(made_index)) ++pop.rooms_done;
    else d2d::rules::populate_room(monsters, region, level.mon.density[std::size_t(difficulty)], room, spawning.game, fits, near_way, spawns, &pop);
    if (made_index < state.room_seeds.size()) state.room_seeds[made_index] = room.seed;
    stamp_made();
    if (spawns.size() > first)
        d2d::log::info("  room ({}, {}) of {} {}: {} monsters", made.x, made.y, level.name, kSfx[difficulty], spawns.size() - first);
}

}  // namespace

std::vector<std::pair<const Level*, std::size_t>> player_moved(const GameData& game_data, Spawning& spawning, const Level& level,
                                                                 float x, float y, bool arrived) {
    std::vector<std::pair<const Level*, std::size_t>> grown;
    auto note = [&](const Level* which, std::size_t from) {
        if (spawning.levels[which].spawns.size() == from) return;
        if (std::ranges::find(grown, which, &std::pair<const Level*, std::size_t>::first) == grown.end()) grown.push_back({ which, from });
    };
    const auto rects = room_rects(level);
    const int room = room_holding(rects, level, x, y);
    if (room < 0 || (&level == spawning.room_level && room == spawning.room && !arrived)) return grown;
    spawning.room_level = &level;
    spawning.room = room;
    auto size_of = [&](const Level* which) { return spawning.levels[which].spawns.size(); };
    // Rooms' tiles and grids come up with their room1 (FUN_0061b190), the
    // arrived room first, then the near list's: the picks and collision of a
    // shared edge depend on which rooms are up already (relevel).
    std::vector<const Level*> moved;
    auto bring_up = [&](const Level* which, std::size_t index) {
        auto& order = spawning.levels[which].order;
        if (std::ranges::contains(order, index)) return;
        order.push_back(index);
        if (!std::ranges::contains(moved, which)) moved.push_back(which);
    };
    auto lay = [&] {
        for (const Level* which : moved) relevel(const_cast<Level&>(*which), spawning.levels[which].order, &game_data, &spawning);   // GameData owns its levels mutable
        moved.clear();
    };
    if (arrived) {                                       // FUN_0056cf40: the room the player lands in, at once
        bring_up(&level, std::size_t(room));
        lay();
        const auto from = size_of(&level);
        populate(game_data, spawning, level, std::size_t(room));
        note(&level, from);
    }
    // FUN_0061b6f0 → FUN_0061b390: each room of the near list not yet in
    // play gets its room1 (FUN_0061b2d0), made at the head of the act's
    // room list; the next tick populates the list's new rooms from the
    // head (FUN_0052d160), newest first.
    std::vector<NearRoom> fresh;
    for (const auto& near_room : near_list(game_data, level, room)) {
        auto& state = spawning.levels[near_room.level];
        if (state.up.size() == room_count(*near_room.level) && state.up[std::size_t(near_room.room)]) continue;
        if (std::ranges::any_of(fresh, [&](const NearRoom& other) { return other.level == near_room.level && other.room == near_room.room; })) continue;
        fresh.push_back(near_room);
    }
    for (const auto& near_room : fresh) bring_up(near_room.level, std::size_t(near_room.room));
    lay();
    for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
        const auto from = size_of(it->level);
        populate(game_data, spawning, *it->level, std::size_t(it->room));
        note(it->level, from);
    }
    return grown;
}


std::vector<std::pair<std::size_t, std::size_t>> populate_level(const GameData& game_data, Spawning& spawning, const Level& level,
                                                                 const std::vector<std::size_t>& up,
                                                                 const std::function<void(std::size_t)>& done) {
    auto& state = spawning.levels[&level];
    for (const auto room : up) if (!std::ranges::contains(state.order, room)) state.order.push_back(room);
    for (std::size_t i = room_count(level); i-- > 0;)    // the rest in list order, oldest first
        if (!std::ranges::contains(state.order, i)) state.order.push_back(i);
    relevel(const_cast<Level&>(level), state.order, &game_data, &spawning);     // GameData owns its levels mutable
    std::vector<std::pair<std::size_t, std::size_t>> order;
    for (auto it = state.order.rbegin(); it != state.order.rend(); ++it) order.push_back({ *it, 0 });   // FUN_0052d160: the list, newest first
    for (auto& [room, first] : order) {
        first = spawning.levels[&level].spawns.size();
        populate(game_data, spawning, level, room);
        if (done) done(room);
    }
    return order;
}

// Footprints into the walk grid, centred on each unit's subtile: an
// object's when it collides in its start mode (HasCollision0..7), noting
// the subtiles a tile blocks as well (Npc::walls) for set_footprint.
// (Quest-gated units like Cain stay out of it: they're not always there.)
// ponytail: static — fine while NPCs only idle; moving units need a
// separate occupancy layer.
// ponytail: one walk bit (0x01) for every footprint; game.exe's object
// flags (FUN_006209d0: 0x400, a door 0x800 | 0x06) aren't split out.
void stamp_footprints(Level& level) {
    for (auto& npc : level.npcs) stamp_footprint(level, npc);
}

void stamp_footprint(Level& level, Npc& npc) {
    const int walk_width = level.ds1.width() * 5, walk_height = level.ds1.height() * 5;
    if (level.walk.size() != std::size_t(walk_width) * std::size_t(walk_height)) return;
    if (!npc.path.empty() || npc.quest) return;         // walkers don't hold a spot
    npc.walls = 0;
    const int center_x = int(npc.x * 5), center_y = int(npc.y * 5);
    int bit = 0;
    for (int y = center_y - npc.size_y / 2; y < center_y - npc.size_y / 2 + npc.size_y; ++y)
        for (int x = center_x - npc.size_x / 2; x < center_x - npc.size_x / 2 + npc.size_x; ++x, ++bit)
            if (x >= 0 && y >= 0 && x < walk_width && y < walk_height && bit < 32
                && level.walk[std::size_t(y) * std::size_t(walk_width) + std::size_t(x)] & 0x01) npc.walls |= 1u << bit;
    set_footprint(level, npc, npc.root != "objects" || npc.collision >> mode_index(npc.mode) & 1);
}

void set_footprint(const Level& level, const Npc& npc, bool solid) {
    const int walk_width = level.ds1.width() * 5, walk_height = level.ds1.height() * 5;
    if (level.walk.size() != std::size_t(walk_width) * std::size_t(walk_height)) return;
    const int center_x = int(npc.x * 5), center_y = int(npc.y * 5);
    int bit = 0;
    for (int y = center_y - npc.size_y / 2; y < center_y - npc.size_y / 2 + npc.size_y; ++y)
        for (int x = center_x - npc.size_x / 2; x < center_x - npc.size_x / 2 + npc.size_x; ++x, ++bit) {
            if (x < 0 || y < 0 || x >= walk_width || y >= walk_height) continue;
            auto& cell = level.walk[std::size_t(y) * std::size_t(walk_width) + std::size_t(x)];
            if (solid) cell |= 0x01;
            else if (bit >= 32 || !(npc.walls >> bit & 1)) cell &= std::uint8_t(~0x01);
        }
}

int mode_index(std::string_view mode) {
    const auto found = std::ranges::find(kObjectModes, mode);
    return found == kObjectModes.end() ? 0 : int(found - kObjectModes.begin());
}

// A type-2 object at subtile (sx, sy): objects.txt Id `oid` (through
// game.exe's preset table) as an Npc in `into`, rolling a shrine's kind and
// a chest's trap and lock, then (FUN_0054f5d0) a PreOperate object starts
// opened (ON) one time in 14. `rgn`: the game's object seed (FUN_00546fa0).
// ponytail: the unit flag 0x80 FUN_0054f5d0 checks before the PreOperate
// roll is taken as clear; what sets it isn't traced.
void add_object(const GameData& game_data, const d2d::txt::Table& objects, const std::unordered_map<std::string, std::size_t>& obj_row,
                Level& into, int oid, int spot_x, int spot_y, d2d::rules::Rng& rgn) {
    const auto found = obj_row.find(std::to_string(oid));
    if (oid == 0 || found == obj_row.end()) return;
    const auto row = found->second;
    Npc npc;
    npc.root   = "objects";
    npc.code   = std::string(objects.get(row, "Token"));
    npc.operate_fn = std::atoi(std::string(objects.get(row, "OperateFn")).c_str());
    if (objects.get(row, "Mode1") == "1") npc.op_frames = std::atoi(std::string(objects.get(row, "FrameCnt1")).c_str());
    if (objects.get(row, "InitFn") == "1") {      // a shrine: which one (FUN_0054f9d0)
        npc.shrine = d2d::rules::roll_shrine(game_data.shrines, std::atoi(std::string(objects.get(row, "Parm0")).c_str()), into.id, rgn);
    }
    if (objects.get(row, "InitFn") == "3" || objects.get(row, "InitFn") == "57") {      // a chest: its trap and lock (FUN_0054fcb0; 57: FUN_0054fd90)
        const auto& area_levels = game_data.area_level;
        const int mlvl1 = std::size_t(into.id) < area_levels.size() ? area_levels[std::size_t(into.id)][3] : 1;
        const auto chest = d2d::rules::roll_chest(mlvl1, objects.get(row, "Lockable") == "1", rgn);
        npc.trap = chest.trap; npc.locked = chest.locked; npc.seed = d2d::rules::Rng{ chest.seed };
        npc.sparkle = objects.get(row, "InitFn") == "57";
        if (npc.locked) if (auto locked_name = lookup_string(game_data, "lockedchest")) npc.name = u16_to_latin1(*locked_name);
    }
    if (objects.get(row, "InitFn") == "2") {      // a trap alone (FUN_0054fbb0)
        const auto& area_levels = game_data.area_level;
        npc.trap = d2d::rules::roll_trap(std::size_t(into.id) < area_levels.size() ? area_levels[std::size_t(into.id)][3] : 1, rgn);
    }
    // The gold placeholder (InitFn 28, FUN_0054f8c0): ON, then 1..9 piles
    // at rand(4), rand(4) subtiles off it, where free in its room.
    // ponytail: only its seed steps; the piles (FUN_00559300) aren't dropped and it stays NU (a dummy: nothing draws it).
    if (objects.get(row, "InitFn") == "28")
        for (int piles = rgn(9) + 1; piles > 0; --piles) { rgn.next(); rgn.next(); }
    npc.preoperated = objects.get(row, "PreOperate") == "1" && rgn(14) == 0;
    npc.base_w = "hth";
    for (std::size_t mode = 0; mode < 8; ++mode) npc.lit[mode] = std::uint8_t(std::atoi(std::string(objects.get(row, "Lit" + std::to_string(mode))).c_str()));
    const bool lit_mode = npc.preoperated || (objects.get(row, "Mode2") == "1" && !objects.get(row, "Lit2").empty()
                 && objects.get(row, "Lit2") != "0" && npc.operate_fn != d2d::rules::operate_fn::kShrine && npc.operate_fn != d2d::rules::operate_fn::kChest   // shrines / chests: NU until used
                 && npc.operate_fn != d2d::rules::operate_fn::kCairnStone && npc.operate_fn != d2d::rules::operate_fn::kGibbet   // Cairn stones / the Gibbet: mode 0 until touched (InitFn 6 / 7)
                 && (npc.operate_fn != d2d::rules::operate_fn::kWaypoint || std::ranges::contains(std::array{ d2d::rules::level_ids::kRogueEncampment, d2d::rules::level_ids::kLutGholein, d2d::rules::level_ids::kKurastDocks, d2d::rules::level_ids::kPandemoniumFortress, d2d::rules::level_ids::kHarrogath }, into.id)));   // waypoints: on in towns (InitFn 17)
    npc.mode   = lit_mode ? "ON" : "NU";
    // Hover name when selectable in its start mode (Selectable0 = NU,
    // 2 = ON): objects.txt Name through the string tables.
    if (objects.get(row, lit_mode ? "Selectable2" : "Selectable0") == "1") {
        const std::string key(objects.get(row, "Name"));
        auto name_found = lookup_string(game_data, key);
        npc.name = name_found ? u16_to_latin1(*name_found) : key;
    }
    // Where it blocks walking, by mode (stamp_footprints takes its start mode's).
    npc.object_id = oid;
    npc.door = objects.get(row, "IsDoor") == "1"; npc.monster_ok = objects.get(row, "MonsterOK") == "1";
    for (std::size_t mode = 0; mode < 8; ++mode) {
        if (objects.get(row, "HasCollision" + std::to_string(mode)) == "1") npc.collision |= std::uint8_t(1u << mode);
        if (objects.get(row, "Selectable" + std::to_string(mode)) == "1") npc.selectable |= std::uint8_t(1u << mode);
    }
    npc.size_x = std::atoi(std::string(objects.get(row, "SizeX")).c_str());
    npc.size_y = std::atoi(std::string(objects.get(row, "SizeY")).c_str());
    for (std::size_t layer = 0; layer < 16; ++layer)
        if (objects.get(row, kLayerCode[layer]) == "1") npc.comp[layer] = "lit";
    if (npc.code.empty()) return;
    npc.x = (float(spot_x) + 0.5f) / 5;
    npc.y = (float(spot_y) + 0.5f) / 5;
    into.npcs.push_back(std::move(npc));
}

// A level's tile lookup and collision grid, once its ds1 and dt1s are in.
void finish_level(Level& level) {
    // Populate the (style, seq, type) lookup across all DT1s. First DT1
    // to define a tuple wins — matches how D2's renderer resolves tile
    // priority against its Stack-ordered tileset list. Covers floors,
    // walls, trees, roofs, shadows in one map.
    // ponytail: first match, not game.exe's rarity pick — generated levels
    // carry the real picks (Level::picks) and use those instead.
    for (const auto& dt1 : level.dt1s)
        for (const auto& tile : dt1.tiles()) level.tile_lookup.try_emplace(tile_key(tile.style, tile.sequence, tile.type), &tile);
    // Collision grid from the same tiles the renderer draws: floors
    // (type 0) and walls/objects, but not shadows (13) or roofs (15).
    // A tile's 25 DT1 subtile flags OR straight into the grid, rows stored
    // bottom-up: subtile (x, y) takes flag (4 - y) * 5 + x (FUN_0064c4c0,
    // building the room grid in FUN_0064c900). Units stamp their own
    // footprints (load_npcs).
    const auto& map = level.ds1;
    const int walk_width = map.width() * 5;
    level.walk.assign(std::size_t(walk_width) * std::size_t(map.height()) * 5, 0);
    auto stamp_tile = [&](int cell_x, int cell_y, const d2d::dt1::Tile& tile) {
        for (int k = 0; k < 25; ++k)
            level.walk[std::size_t(cell_y * 5 + 4 - k / 5) * std::size_t(walk_width) + std::size_t(cell_x * 5 + k % 5)] |= tile.subtile_flags[std::size_t(k)];
    };
    auto stamp = [&](int cell_x, int cell_y, int style, int seq, int type) {
        const auto found = level.tile_lookup.find(tile_key(style, seq, type));
        if (found != level.tile_lookup.end()) stamp_tile(cell_x, cell_y, *found->second);
    };
    if (!level.picks.empty()) {
        for (int cell_y = 0; cell_y < map.height(); ++cell_y)
            for (int cell_x = 0; cell_x < map.width(); ++cell_x)
                for (const auto& pick : level.picks[std::size_t(cell_y) * std::size_t(map.width()) + std::size_t(cell_x)]) {
                    if (!pick.stamp) continue;
                    if (pick.layer == 1 || (pick.layer == 0 && pick.orient != 13 && pick.orient != 15)) stamp_tile(cell_x, cell_y, *pick.stamp);
                    for (int k = 0; k < 25 && pick.cell; ++k) level.walk[std::size_t(cell_y * 5 + k / 5) * std::size_t(walk_width) + std::size_t(cell_x * 5 + k % 5)] |= pick.cell;
                }
        for (const auto& patch : level.patches)
            for (int k = 0; k < 25; ++k) {
                auto& at = level.walk[std::size_t(patch.y * 5 + 4 - k / 5) * std::size_t(walk_width) + std::size_t(patch.x * 5 + k % 5)];
                if (patch.old_tile) at &= std::uint8_t(~patch.old_tile->subtile_flags[std::size_t(k)]);
                if (patch.tile) at |= patch.tile->subtile_flags[std::size_t(k)];
            }
        level.tile_walk = level.walk;
        return;
    }
    for (int cell_y = 0; cell_y < map.height(); ++cell_y)
        for (int cell_x = 0; cell_x < map.width(); ++cell_x) {
            const std::size_t off = std::size_t(cell_y) * std::size_t(map.width()) + std::size_t(cell_x);
            // Hidden ones too: game.exe keeps them as tiles (flag 8) that
            // block but aren't drawn (FUN_0064c790) — the camp's river edge.
            for (const auto& layer : map.floors())
                if (layer.cells[off].prop1 & 2) stamp(cell_x, cell_y, layer.cells[off].style, layer.cells[off].sequence, 0);
            for (const auto& layer : map.walls()) {
                const auto& cell = layer.cells[off];
                if (cell.wall_type == 0 || cell.wall_type == 13 || cell.wall_type == 15) continue;
                stamp(cell_x, cell_y, cell.style, cell.sequence, cell.wall_type);
                if (cell.wall_type == 3) stamp(cell_x, cell_y, cell.style, cell.sequence, 4);   // a corner's second tile (FUN_0066e9b0)
            }
        }
    level.tile_walk = level.walk;
}

LevelDt1s load_level_dt1s(Level& level, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, int type, d2d::dt1::Pixels pixels) {
    LevelDt1s dt1s;
    dt1s.heads = d2d::drlg::load_room_dt1s(assets, [&](const std::string& path) { return mpqs.try_read(path); }, type);
    std::vector<std::pair<const d2d::drlg::Dt1File*, std::string>> files;
    for (std::size_t row = 0; row < assets.lvl_types.size(); ++row)
        if (d2d::drlg::to_int(assets.lvl_types.get(row, "Id"), -1) == type)
            for (int i = 0; i < 32; ++i)
                if (dt1s.heads.by_bit[std::size_t(i)]) files.emplace_back(dt1s.heads.by_bit[std::size_t(i)], std::string(assets.lvl_types.get(row, "File " + std::to_string(i + 1))));
    for (std::size_t k = 0; k < 3; ++k)
        if (dt1s.heads.always[k]) files.emplace_back(dt1s.heads.always[k], std::array{ "Act1/Outdoors/Blank.dt1", "Act1/Barracks/InvisWal.dt1", "Act1/Barracks/Warp.dt1" }[k]);
    level.dt1s.reserve(files.size());                      // archive points into it
    for (const auto& [file_key, file] : files) {
        auto bytes = mpqs.try_read(R"(data\global\tiles\)" + ds1_path_to_mpq(file));
        if (!bytes) continue;
        try { dt1s.archive[file_key] = &level.dt1s.emplace_back(*bytes, pixels); } catch (const std::exception& error) { d2d::log::warn("level {}: {}: {}", level.id, file, error.what()); }
    }
    return dt1s;
}

namespace {

// The camp's rooms up (Spawning order) as the level next door finds them:
// game.exe's near list takes the rooms up in the levels next door too
// (FUN_0066c370 → FUN_0066be80), so a Blood Moor room on the camp's edge
// shares the camp's edge tiles (FUN_0066e940) and rolls fewer picks on its
// seed. Laid on the camp's preset (LvlPrest def 1, its file by the Blood
// Moor's side) in the order they came up, in `level`'s tiles, list order.
// ponytail: no DT1s or tile seeds, so no picks: which tiles the level
// finds is all its seeds and picks take; a cell it finds draws a stand-in
// (lay_tiles). The camp's tiles don't take the level's shares back.
std::vector<d2d::drlg::BuiltRoom> camp_rooms(const GameData& game_data, const Spawning& spawning, const Level& level) {
    const auto& town = game_data.town;
    const auto next_door = std::ranges::find(level.nearby, &town, &Level::Near::level);
    const auto state = spawning.levels.find(&town);
    if (!level.assets || next_door == level.nearby.end() || state == spawning.levels.end() || state->second.order.empty()) return {};
    const auto rects = room_rects(town);
    std::vector<std::size_t> up;                         // room indices, as they came up
    for (const auto room : state->second.order) if (room < rects.size() && !std::ranges::contains(up, room)) up.push_back(room);
    std::vector<d2d::drlg::Outdoor::RoomSeed> made;      // those, in the order made
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (!std::ranges::contains(up, i)) continue;
        auto& room = made.emplace_back();
        room.x = rects[i].x - town.world_x, room.y = rects[i].y - town.world_y, room.width = rects[i].width, room.height = rects[i].height;
        room.kind = 2, room.def = 1, room.file = std::max(0, d2d::drlg::town_file(game_data.act1_layout)), room.rolled = true;
    }
    std::vector<std::size_t> order;                      // list indices: the list is newest first
    for (const auto room : up) order.push_back(made.size() - 1 - std::size_t(std::ranges::count_if(up, [&](std::size_t other) { return other < room; })));
    std::vector<std::string> notes;
    auto rooms = d2d::drlg::level_room_tiles(made, {}, level.assets->data, {}, town.id, d2d::drlg::warp_slots(*level.assets, town.id), notes, order);
    for (auto& room : rooms) {
        room.x += next_door->dx, room.y += next_door->dy;
        for (auto& tile : room.tiles) tile.x += next_door->dx, tile.y += next_door->dy;
        room.seed = nullptr;                             // `made` goes
    }
    return rooms;
}

// The level's rooms brought up in `order` (level_room_tiles' list indices,
// empty: list order) into its picks and patches.
// FUN_0064c900: each room's grid takes, as it comes up (BuiltRoom::step),
// the tiles of the near rooms already up, clipped to its rect (FUN_0064c790,
// FUN_00619df0): a tile past its owner's rect (a maze room's shared edge)
// stamps only if the room it lies in came up after its owner. A later
// room re-picking a shared tile patches the grid it lies in, if that's up
// (FUN_0064c860: old tile's flags off, the new one's on).
// `outside`: the up rooms next door (camp_rooms), after the level's in the result.
std::vector<d2d::drlg::BuiltRoom> lay_tiles(Level& level, const std::vector<d2d::drlg::Outdoor::RoomSeed>& made,
                                            const std::vector<std::size_t>& order, std::vector<std::string>& notes,
                                            std::vector<d2d::drlg::BuiltRoom> outside = {}) {
    const auto& assets = *level.assets;
    const auto& dt1s = level.tile_dt1s;
    const auto own = made.size();
    auto built = d2d::drlg::level_room_tiles(made, level.plain, assets.data, dt1s.heads, level.id, d2d::drlg::warp_slots(assets, level.id), notes, order, std::move(outside));
    const int width = level.ds1.width(), height = level.ds1.height();
    level.picks.assign(std::size_t(width) * std::size_t(height), {});
    auto up = [&](int room) { return int(built[std::size_t(room)].step); };
    auto tile_of = [&](const d2d::drlg::Dt1File* file, int index) -> const d2d::dt1::Tile* {
        const auto found = file ? dt1s.archive.find(file) : dt1s.archive.end();
        return found == dt1s.archive.end() || index < 0 || std::size_t(index) >= found->second->size() ? nullptr : &found->second->tiles()[std::size_t(index)];
    };
    auto holder = [&](std::size_t owner, int x, int y) {
        auto holds = [&](const d2d::drlg::BuiltRoom& r) { return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height; };
        if (holds(built[owner])) return int(owner);
        for (std::size_t k = 0; k < built.size(); ++k)
            if (holds(built[k])) return int(k);
        return -1;
    };
    // FUN_0066db20's tile flags as FUN_0064c790 stamps them (2 / 0x40 / 0x80: 0x10, 0x01, 0x04).
    auto cell_of = [](std::uint32_t word) { return std::uint8_t((word & 0x10000000u ? 0x10 : 0) | (word & 0x20000u ? 0x01 : 0) | (word & 0x10000u ? 0x04 : 0)); };
    struct Share { int step; const d2d::drlg::BuiltRoom::Share* at; };
    std::vector<Share> shares;                          // as the rooms came up
    for (std::size_t k = 0; k < built.size(); ++k)
        for (const auto& share : built[k].shares) shares.push_back({ up(int(k)), &share });
    std::ranges::stable_sort(shares, {}, &Share::step);
    level.patches.clear();
    for (const auto& [step, at] : shares) {
        const auto& tile = built[std::size_t(at->owner)].tiles[std::size_t(at->tile)];
        const int in = holder(std::size_t(at->owner), tile.x, tile.y);
        if (in >= 0 && up(in) < step && (at->old_file != at->file || at->old_index != at->index))
            level.patches.push_back({ tile.x, tile.y, tile_of(at->old_file, at->old_index), tile_of(at->file, at->index) });
    }
    for (std::size_t owner = 0; owner < built.size(); ++owner)
        for (std::size_t index = 0; index < built[owner].tiles.size(); ++index) {
            const auto& tile = built[owner].tiles[index];
            if (tile.x < 0 || tile.y < 0 || tile.x >= width || tile.y >= height) continue;
            auto drawn = tile_of(tile.file, tile.index);
            if (!drawn && owner >= own)                  // the camp's, unpicked (camp_rooms): the level's first of its kind
                if (const auto found = level.tile_lookup.find(tile_key(int((tile.word >> 20) & 0x3f), int((tile.word >> 8) & 0xff), tile.orient)); found != level.tile_lookup.end()) drawn = found->second;
            if (!drawn) continue;
            // What it was when the room it lies in came up: none if that came up first.
            const int in = holder(owner, tile.x, tile.y);
            const d2d::dt1::Tile* stamp = in >= 0 && up(int(owner)) > up(in) ? nullptr : drawn;
            std::uint8_t cell = std::uint8_t(cell_of(tile.word) | (tile.layer == 0 && tile.orient >= 8 && tile.orient <= 11 ? 0x10 : 0));   // a door or warp wall is flag 2 too
            for (const auto& [step, at] : shares) {
                if (std::size_t(at->owner) != owner || std::size_t(at->tile) != index) continue;
                if (in < 0 || step <= up(in)) cell |= cell_of(at->word);
                else if (stamp && (at->old_file != at->file || at->old_index != at->index)) { stamp = tile_of(at->old_file, at->old_index); break; }
            }
            level.picks[std::size_t(tile.y) * std::size_t(width) + std::size_t(tile.x)].push_back(
                { std::uint8_t(tile.layer), std::uint8_t(tile.orient), drawn,
                  tile.layer != 2 && (tile.word & 0x80000000u) != 0,
                  cell, stamp });
        }
    return built;
}

}  // namespace

// Its rooms brought up again, `up` (Level::rooms indices) first in that
// order, the rest after in list order: picks, patches, the walk grid and
// the room1 seeds as game.exe's rooms would have them, footprints stamped
// again. game.exe's units and warps come out the same in any order
// (tools/emu diff_drlg.py <level> units, rooms in $ORDER), its spawn areas
// too (diff_drlg.py game: the game seed through the monsters); the object
// groups follow as the rooms populate (room_objects, on these seeds).
// ponytail: the whole level is laid again (~15 ms for the Stony Field)
// each time rooms come up; bring them up one at a time, as game.exe does,
// if that hitches.
void relevel(Level& level, const std::vector<std::size_t>& up, const GameData* game_data, const Spawning* spawning) {
    if (!level.assets) return;
    auto outside = game_data && spawning ? camp_rooms(*game_data, *spawning, level) : std::vector<d2d::drlg::BuiltRoom>{};
    const std::size_t count = level.rooms.size();
    std::vector<std::size_t> order;                      // list indices: the list is newest first
    std::vector<bool> taken(count);
    for (const auto room : up) if (room < count && !taken[count - 1 - room]) { order.push_back(count - 1 - room); taken[count - 1 - room] = true; }
    for (std::size_t i = 0; i < count; ++i) if (!taken[i]) order.push_back(i);
    if (outside.empty() && (order == level.laid || (level.laid.empty() && std::ranges::is_sorted(order) && order.size() == count))) return;
    const auto start_ms = d2d::log::ms();
    std::vector<std::string> notes;
    for (const auto& room : lay_tiles(level, level.rooms, order, notes, std::move(outside)))
        if (room.seed) level.room1_seeds[std::size_t(room.seed - level.rooms.data())] = room.room1_seed;
    finish_level(level);
    level.laid = std::move(order);
    stamp_footprints(level);
    d2d::log::info("  {}: rooms laid again ({} ms)", level.name, d2d::log::ms() - start_ms);
}

// Its rooms brought up (drlg level_room_tiles, proven against game.exe):
// every cell's picked tiles, its warps, then lookup and collision. List
// order, as population brings the whole level up (a player walking in: relevel).
std::size_t set_level_tiles(Level& level, const d2d::drlg::OutdoorAssets& assets, const LevelDt1s& dt1s,
                            const std::vector<d2d::drlg::Outdoor::RoomSeed>& made, const std::vector<d2d::drlg::PlainRoom>& plain,
                            std::vector<std::string>& notes) {
    level.assets = &assets;
    level.tile_dt1s = dt1s;
    level.plain = plain;
    const auto built = lay_tiles(level, made, {}, notes);
    std::size_t placed = 0;
    for (const auto& cell : level.picks) placed += cell.size();
    level.room1_seeds.assign(made.size(), 0);
    level.nopop_rooms.assign(made.size(), false);
    level.room_areas.assign(made.size(), {});
    for (const auto& room : built)
        if (room.seed) {
            level.room1_seeds[std::size_t(room.seed - made.data())] = room.room1_seed;
            level.room_areas[std::size_t(room.seed - made.data())] = room.areas.empty() ? std::vector<d2d::drlg::Area>{ { room.x, room.y, room.x + room.width, room.y + room.height, 1, false } } : room.areas;
            const auto preset = room.seed->kind == 2 ? assets.data.presets.find(room.seed->def) : assets.data.presets.end();
            level.nopop_rooms[std::size_t(room.seed - made.data())] = !room.warps.empty() || (preset != assets.data.presets.end() && !preset->second.populate);
        }
    for (const auto& room : built)
        for (const auto& unit : room.units) {
            level.units.push_back({ unit.type, unit.id, unit.mode, unit.x + room.x * 5, unit.y + room.y * 5, unit.flags, unit.path });
            level.unit_rooms.push_back(room.seed ? int(room.seed - made.data()) : -1);
        }
    // Warps: the slot's Levels.txt Vis / Warp, LvlWarp's ExitWalk, and the
    // tile unit's spot (FUN_0066e1c0: the slot's LvlWarp Offset).
    const auto slots = d2d::drlg::warp_slots(assets, level.id);
    // 0x800000: a preset room its LvlPrest row doesn't populate (FUN_006666ec),
    // a room with a warp tile, lit or not (FUN_0066e360).
    level.room_flags.assign(made.size(), 0);
    for (std::size_t i = 0; i < made.size(); ++i)
        if (const auto preset = assets.data.presets.find(made[i].def); made[i].kind == 2 && preset != assets.data.presets.end() && !preset->second.populate)
            level.room_flags[i] |= 0x800000;
    for (const auto& room : built) {
        if (room.seed) level.room_flags[std::size_t(room.seed - made.data())] |= (room.warps.empty() ? 0 : 0x800000) | room.vis;
        level.starts.insert(level.starts.end(), room.starts.begin(), room.starts.end());
    }
    // A warp wall alone (the Forgotten Tower's stairs: unflagged words,
    // FUN_0066e260) leaves only its unit (type 5, the LvlWarp id): a warp
    // at the unit's cell less the slot's offset.
    std::vector<d2d::drlg::BuiltRoom::Warp> found;
    for (const auto& room : built)
        found.insert(found.end(), room.warps.begin(), room.warps.end());
    for (const auto& room : built)
        for (const auto& unit : room.units) {
            if (unit.type != d2d::rules::unit_type::kWarp) continue;
            const auto slot = std::ranges::find(slots, unit.id, &d2d::drlg::WarpSlot::id);
            if (slot == slots.end()) continue;
            const int slot_index = int(slot - slots.begin());
            if (std::ranges::any_of(found, [&](const auto& warp) { return warp.slot == slot_index; })) continue;
            found.push_back({ room.x + (unit.x - slot->off_x) / 5, room.y + (unit.y - slot->off_y) / 5, slot_index });
        }
    if (const auto row = d2d::drlg::level_row(assets.levels, level.id))
        for (const auto& warp : found) {
            if (warp.slot < 0 || warp.slot > 7) continue;
            const int destination = d2d::drlg::to_int(assets.levels.get(*row, "Vis" + std::to_string(warp.slot)));
            const int wid = d2d::drlg::to_int(assets.levels.get(*row, "Warp" + std::to_string(warp.slot)), -1);
            if (destination <= 0 || wid < 0) continue;
            float exit_x = 0, exit_y = 0;
            for (std::size_t k = 0; k < assets.lvl_warp.size(); ++k)
                if (d2d::drlg::to_int(assets.lvl_warp.get(k, "Id"), -1) == wid) {
                    exit_x = float(d2d::drlg::to_int(assets.lvl_warp.get(k, "ExitWalkX"))) / 5;
                    exit_y = float(d2d::drlg::to_int(assets.lvl_warp.get(k, "ExitWalkY"))) / 5;
                    break;
                }
            const auto& slot = slots[std::size_t(warp.slot)];
            int pair = 0;
            for (int k = 0; k < warp.slot; ++k) pair += d2d::drlg::to_int(assets.levels.get(*row, "Vis" + std::to_string(k))) == destination;
            level.warps.push_back({ float(warp.x), float(warp.y), destination, exit_x, exit_y,
                                    (float(warp.x * 5 + slot.off_x) + 0.5f) / 5, (float(warp.y * 5 + slot.off_y) + 0.5f) / 5, warp.slot, pair });
        }
    finish_level(level);
    return placed;
}

// An outdoor level of the act (the Blood Moor): drlg generate_outdoor
// where the act's layout put it, on its level seed.
bool build_outdoor(const GameData& game_data, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, Level& level) {
    const auto outdoor_level = d2d::drlg::outdoor_level(assets.levels, game_data.act1_layout, level.id);
    if (outdoor_level.rect.width == 0) { d2d::log::warn("{}: the layout didn't place it", level.name); return false; }
    const auto dt1s = load_level_dt1s(level, mpqs, assets, level.type, game_data.tile_pixels ? d2d::dt1::Pixels::decode : d2d::dt1::Pixels::skip);
    assets.data.dt1s = &dt1s.heads;                          // stamps pick their shadows as they go (game.exe's rolls)
    auto outdoor = d2d::drlg::generate_outdoor(assets.data, outdoor_level, d2d::drlg::level_seed(game_data.map_seed, level.id));
    assets.data.dt1s = nullptr;
    auto notes = outdoor.notes;
    level.ds1 = std::move(outdoor.tiles);
    level.world_x = outdoor_level.rect.x;
    level.world_y = outdoor_level.rect.y;
    const auto placed = set_level_tiles(level, assets, dt1s, outdoor.rooms, outdoor.plain, notes);
    for (std::size_t i = 0; i < outdoor.rooms.size(); ++i) {        // a plain room's cell flag 0x30000 (waypoint); its 0x80 (a path) is road_rooms, not a room2 flag
        const auto& room = outdoor.rooms[i];
        const auto cell = std::size_t(room.y / 8) * std::size_t(outdoor.cells_wide) + std::size_t(room.x / 8);
        if (room.kind == 1 && cell < outdoor.g18.size()) level.room_flags[i] |= outdoor.g18[cell] & 0x30000;
    }
    for (const auto& room : outdoor.rooms)
        level.road_rooms.push_back(room.kind == 1 && (outdoor.g2c[std::size_t(room.y / 8 * outdoor.cells_wide + room.x / 8)] & 0x80));
    level.rooms = std::move(outdoor.rooms);
    for (const auto& note : notes) d2d::log::info("  not implemented: {}", note);
    d2d::log::info("  {}: {}x{} tiles at ({}, {}), {} roads, {} tilesets, {} picked tiles, {} warp tiles, map seed {}", level.name,
                   level.ds1.width(), level.ds1.height(), level.world_x, level.world_y, outdoor.roads.size(), level.dt1s.size(), placed,
                   level.warps.size(), game_data.map_seed);
    return true;
}

// A maze or preset level (the caves): drlg generate_maze / generate_preset from its level seed,
// its preset rooms' tiles picked as game.exe picks them. It sits apart
// from the act's outdoor levels; its warps lead out.
bool build_maze(const GameData& game_data, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, Level& level, std::size_t row) {
    d2d::drlg::MazeDef maze;
    for (std::size_t maze_row = 0; maze_row < assets.lvl_maze.size(); ++maze_row)
        if (d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "Level"), -1) == level.id) {
            maze.rooms.fill(d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "Rooms")));   // ponytail: the .txt's one column for every difficulty
            maze.width = d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "SizeX"));
            maze.height = d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "SizeY"));
            maze.merge = d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "Merge"));
        }
    maze.type = d2d::drlg::to_int(assets.levels.get(row, "LevelType"));
    const bool preset_level = d2d::drlg::to_int(assets.levels.get(row, "DrlgType")) == 2;
    if (maze.width == 0 && !preset_level) { d2d::log::warn("{}: no LvlMaze row", level.name); return false; }
    std::vector<std::string> notes;
    const int size_x = d2d::drlg::to_int(assets.levels.get(row, "SizeX")), size_y = d2d::drlg::to_int(assets.levels.get(row, "SizeY"));
    const auto seed = d2d::drlg::level_seed(game_data.map_seed, level.id);
    int court_file = level.id == d2d::rules::level_ids::kOuterCloister || level.id == d2d::rules::level_ids::kBarracks ? d2d::drlg::courtyard_file(game_data.act1_layout, d2d::drlg::act_seed(game_data.map_seed)) : -1;
    const auto [origin_x, origin_y] = d2d::drlg::level_origin(assets.levels, row);
    std::array<int, 4> rect{ origin_x, origin_y, size_x, size_y };
    if (const auto court = d2d::drlg::level_row(assets.levels, d2d::rules::level_ids::kOuterCloister); level.id == d2d::rules::level_ids::kBarracks && court) {   // the Barracks sits beside the Outer Cloister
        auto court_seed = d2d::drlg::level_seed(game_data.map_seed, 27);
        court_file = d2d::drlg::preset_file(assets.data, 27, court_seed, court_file);
        const auto [court_x, court_y] = d2d::drlg::level_origin(assets.levels, *court);
        rect = { court_x, court_y, d2d::drlg::to_int(assets.levels.get(*court, "SizeX")), d2d::drlg::to_int(assets.levels.get(*court, "SizeY")) };
    }
    auto made = preset_level ? d2d::drlg::generate_preset(assets.data, level.id, size_x, size_y, seed, notes, court_file)
                             : d2d::drlg::generate_maze(assets.data, maze, level.id, size_x, size_y, seed, 0, notes, court_file, &rect);
    int width = 0, height = 0;
    for (const auto& room : made) { width = std::max(width, room.x + room.width); height = std::max(height, room.y + room.height); }
    level.ds1 = d2d::ds1::Map(width, height, 4, 2);
    level.world_x = rect[0];
    level.world_y = rect[1];
    const auto dt1s = load_level_dt1s(level, mpqs, assets, level.type, game_data.tile_pixels ? d2d::dt1::Pixels::decode : d2d::dt1::Pixels::skip);
    const auto placed = set_level_tiles(level, assets, dt1s, made, {}, notes);
    level.rooms = std::move(made);
    for (const auto& note : notes) d2d::log::info("  not implemented: {}", note);
    d2d::log::info("  {}: {}x{} tiles, {} rooms, {} tilesets, {} picked tiles, {} warp tiles", level.name, width, height,
                   level.rooms.size(), level.dt1s.size(), placed, level.warps.size());
    return true;
}

// Level `id` built from the map seed: its tiles and walk grid, warps, the
// objects and NPCs its DS1s place, its sound, automap layer and monster
// columns. On the builder thread; reads GameData's tables only.
std::unique_ptr<Level> build_level(const GameData& game_data, GameData::LevelBuilder& builder, int id) {
    const auto start_ms = d2d::log::ms();
    std::lock_guard lock(builder.mutex);
    if (!builder.mpqs) builder.mpqs = game_data.mpqs.reopen();
    auto& assets = *builder.act1;
    const auto row = d2d::drlg::level_row(assets.levels, id);
    if (!row) return nullptr;
    auto level = std::make_unique<Level>();
    level->id = id;
    level->type = d2d::drlg::to_int(assets.levels.get(*row, "LevelType"));
    level->name = std::string(assets.levels.get(*row, "LevelName"));
    // A preset the layout places (the Monastery Gate, DrlgType 2) builds as a preset: its rooms (64x18: an 8x2 row) are the DS1's.
    const bool outdoor = d2d::drlg::to_int(assets.levels.get(*row, "DrlgType")) == 3 &&
                         std::ranges::any_of(game_data.act1_layout, [&](const auto& placement) { return placement.level == id; });
    if (!(outdoor ? build_outdoor(game_data, *builder.mpqs, assets, *level) : build_maze(game_data, *builder.mpqs, assets, *level, *row))) return nullptr;
    auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    for (std::size_t row_index = 0; row_index < builder.levels.size(); ++row_index) {
        if (num(builder.levels.get(row_index, "Id")) != id) continue;
        auto text = [&](std::string column) { return builder.levels.get(row_index, column); };
        level->layer = num(text("Layer"));
        level->light = level_light(text("Intensity"), text("Red"), text("Green"), text("Blue"));
        level->rain = text("Rain") == "1";
        for (std::size_t env_row = 0; env_row < builder.sound_env.size(); ++env_row)
            if (builder.sound_env.get(env_row, "Index") == text("SoundEnv")) {
                level->song = num(builder.sound_env.get(env_row, "Song"));
                level->ambience = num(builder.sound_env.get(env_row, "Day Ambience"));
                level->night_ambience = num(builder.sound_env.get(env_row, "Night Ambience"));
                level->day_event = num(builder.sound_env.get(env_row, "Day Event"));
                level->night_event = num(builder.sound_env.get(env_row, "Night Event"));
                level->event_delay = num(builder.sound_env.get(env_row, "Event Delay"));
            }
    }
    if (std::size_t(id) < game_data.level_mon.size()) level->mon = game_data.level_mon[std::size_t(id)];
    // Its preset units (Level::units) come as their rooms populate
    // (populate: objects, and monsters MonStats marks as NPCs, Flavie by the
    // Blood Moor's way in); the rooms' flags, and the units off the rooms, now.
    place_objects(game_data, builder, *level);
    // Tristram Cain (monster 0x92): the Gibbet's opening makes him at its
    // x + 3, y + 3 (FUN_00593290); here from the start, hidden till then
    // (World::cain_walk).
    if (const auto gibbet = std::ranges::find_if(level->units, [](const auto& unit) { return unit.type == d2d::rules::unit_type::kObject && unit.id == d2d::rules::object_ids::kGibbet; });
        id == d2d::rules::CainQuest::kTristram && gibbet != level->units.end() && d2d::rules::monster_ids::kCain < int(game_data.mon_bin.size())) {
        auto npc = game_data.mon_npc[game_data.mon_bin[std::size_t(d2d::rules::monster_ids::kCain)]];
        npc.x = (float(gibbet->x) + 0.5f) / 5 + 0.6f;
        npc.y = (float(gibbet->y) + 0.5f) / 5 + 0.6f;
        npc.quest = d2d::rules::CainQuest::kQuest;   // out of the walk grid
        level->npcs.push_back(std::move(npc));
    }
    level->npc_base = level->npcs.size();
    stamp_footprints(*level);
    d2d::log::info("  {} built ({} ms)", level->name, d2d::log::ms() - start_ms);
    return level;
}

// The levels `id` walks into, no warp between (Levels.txt Vis with Warp -1:
// the Outer Cloister's gate to the Barracks, the Inner Cloister's to the Cathedral).
std::vector<int> walk_links(const GameData& game_data, int id) {
    std::vector<int> out;
    if (!game_data.builder) return out;
    const auto& levels = game_data.builder->levels;
    if (const auto row = d2d::drlg::level_row(levels, id))
        for (int i = 0; i < 8; ++i) {
            const int vis = d2d::drlg::to_int(levels.get(*row, "Vis" + std::to_string(i)));
            if (vis > 0 && d2d::drlg::to_int(levels.get(*row, "Warp" + std::to_string(i))) == -1) out.push_back(vis);
        }
    return out;
}

// A finished build into GameData::levels (nullptr: tried, not built); an act
// level is linked with the act levels already there, both ways, and with the
// levels it walks into.
void install_level(const GameData& game_data, int id, std::unique_ptr<Level> level) {
    auto in_act = [&](int level_id) { return std::ranges::any_of(game_data.act1_layout, [&](const auto& placement) { return placement.level == level_id; }); };
    if (level) {
        const auto walks = walk_links(game_data, id);
        auto linked = [&](int level_id) { return (in_act(id) && in_act(level_id)) || std::ranges::contains(walks, level_id); };
        std::vector<const Level*> others;
        if (game_data.town.ds1.width() > 0 && linked(game_data.town.id)) others.push_back(&game_data.town);
        for (const auto& [level_id, built] : game_data.levels) if (built && linked(level_id)) others.push_back(built.get());
        for (const Level* other_level : others) {
            level->nearby.push_back({ other_level, other_level->world_x - level->world_x, other_level->world_y - level->world_y });
            other_level->nearby.push_back({ level.get(), level->world_x - other_level->world_x, level->world_y - other_level->world_y });
        }
    }
    game_data.levels[id] = std::move(level);
}

std::unique_ptr<Level> finish_job(std::future<std::unique_ptr<Level>>& job, int id) {
    try { return job.get(); }
    catch (const std::exception& error) { d2d::log::warn("level {}: {}", id, error.what()); return nullptr; }
}

void GameData::want_level(int id) const {
    if (!builder || !builder->act1 || id == town.id || levels.contains(id) || builder->jobs.contains(id)
        || !std::ranges::contains(kBuiltLevels, id)) return;
    builder->jobs.emplace(id, std::async(std::launch::async, [this, id] { return build_level(*this, *builder, id); }));
}

void GameData::poll_levels() const {
    if (!builder) return;
    for (auto job = builder->jobs.begin(); job != builder->jobs.end();)
        if (job->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            install_level(*this, job->first, finish_job(job->second, job->first));
            job = builder->jobs.erase(job);
        } else ++job;
}

const Level* GameData::level(int id) const {
    if (id == town.id) return &town;
    if (const auto found = levels.find(id); found != levels.end()) return found->second.get();
    want_level(id);
    const auto job = builder ? builder->jobs.find(id) : decltype(builder->jobs.end()){};
    if (!builder || job == builder->jobs.end()) return nullptr;
    auto level = finish_job(job->second, id);                    // built now, or waited for
    builder->jobs.erase(job);
    install_level(*this, id, std::move(level));
    return levels[id].get();
}

// The levels a player on `l` may reach next: the act's levels touching
// it, and where its warps lead.
void want_nearby(const GameData& game_data, const Level& level) {
    const auto placement = std::ranges::find(game_data.act1_layout, level.id, &d2d::drlg::Placed::level);
    if (placement != game_data.act1_layout.end())
        for (const auto& other_placement : game_data.act1_layout)
            if (other_placement.level != level.id && other_placement.x <= placement->x + placement->width && placement->x <= other_placement.x + other_placement.width && other_placement.y <= placement->y + placement->height && placement->y <= other_placement.y + other_placement.height)
                game_data.want_level(other_placement.level);
    for (const auto& warp : level.warps) game_data.want_level(warp.destination);
    for (const int walk : walk_links(game_data, level.id)) game_data.want_level(walk);
}

}  // namespace d2d::game
