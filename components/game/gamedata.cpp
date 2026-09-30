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
    for (int step = 0; step < 3; ++step) spawning.game.next();
    spawning.regions.resize(game_data.level_mon.size());
    for (std::size_t id = 1; id < game_data.level_mon.size(); ++id)
        spawning.regions[id] = d2d::rules::monster_region(game_data.monsters, game_data.level_mon[id], spawning.difficulty, region_seed);
    return spawning;
}

namespace {

struct RoomRect { int x, y, width, height; };           // act tiles

// A level's rooms, act tiles, in the order they were made (Level::rooms).
// The camp (a preset level, no generated rooms) as 8x8 rooms, row by row.
// ponytail: the camp's room order isn't traced; it only orders which of
// the Blood Moor's rooms its edge rooms bring up.
std::vector<RoomRect> room_rects(const Level& level) {
    std::vector<RoomRect> rects;
    if (!level.rooms.empty()) {
        for (const auto& room : level.rooms) rects.push_back({ level.world_x + room.x, level.world_y + room.y, room.width, room.height });
        return rects;
    }
    for (int y = 0; y < level.ds1.height(); y += 8)
        for (int x = 0; x < level.ds1.width(); x += 8)
            rects.push_back({ level.world_x + x, level.world_y + y, std::min(8, level.ds1.width() - x), std::min(8, level.ds1.height() - y) });
    return rects;
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
// its preset units (FUN_005559a0 → FUN_0054e600), then the room populated
// (FUN_0054ec90), both on the room1 seed.
void populate(const GameData& game_data, Spawning& spawning, const Level& level, std::size_t made_index) {
    static constexpr const char* kSfx[3] = { "", "(N)", "(H)" };
    const int difficulty = spawning.difficulty;
    auto& state = spawning.levels[&level];
    const auto& monsters = game_data.monsters;
    const auto& region = std::size_t(level.id) < spawning.regions.size() ? spawning.regions[std::size_t(level.id)] : d2d::rules::Region{};
    if (state.up.empty()) {
        state.up.assign(level.rooms.size(), false);
        state.pop = { 0, int(level.rooms.size()), 0, level.mon.umin[std::size_t(difficulty)], level.mon.umax[std::size_t(difficulty)], difficulty, &game_data.umods };
        level.region[std::size_t(difficulty)].clear();
        for (const auto& [row, rarity] : region.types) level.region[std::size_t(difficulty)].push_back(row);
    }
    if (made_index >= level.rooms.size() || state.up[made_index] || level.walk.empty()) return;
    state.up[made_index] = true;
    auto& spawns = state.spawns;
    const std::size_t first = spawns.size();
    auto& pop = state.pop;
    const auto& made = level.rooms[made_index];
    // The seed monsters roll on is the room1 seed after FUN_00552610 has
    // stepped it (Level::post_object_group_seeds; build_level records the
    // post-552610 seed per room). Falls back to the raw room1 seed for
    // levels that have no object-group placement pass.
    const std::uint32_t seed = made_index < level.post_object_group_seeds.size() && level.post_object_group_seeds[made_index] != 0
                             ? level.post_object_group_seeds[made_index]
                             : made_index < level.room1_seeds.size() ? level.room1_seeds[made_index] : made.seed;
    d2d::rules::SpawnRoom room{ made.x * 5, made.y * 5, made.width * 5, made.height * 5, d2d::rules::Rng{ seed } };
    // Not within WarpDist (2025 = 45^2 subtiles) of where players come
    // in: the camp for the Blood Moor, the warps for a level entered by one.
    std::vector<std::array<int, 4>> ways;
    if (level.id == 2) {
        const int tx0 = (game_data.town.world_x - level.world_x) * 5, ty0 = (game_data.town.world_y - level.world_y) * 5;
        ways.push_back({ tx0, ty0, tx0 + game_data.town.ds1.width() * 5, ty0 + game_data.town.ds1.height() * 5 });
    } else {
        for (const auto& warp : level.warps) ways.push_back({ int(warp.x) * 5, int(warp.y) * 5, int(warp.x) * 5 + 5, int(warp.y) * 5 + 5 });
    }
    auto near_way = [&](int x, int y) {
        for (const auto& [left, top, right, bottom] : ways) {
            const int dx = std::max({ left - x, 0, x - right }), dy = std::max({ top - y, 0, y - bottom });
            if (dx * dx + dy * dy < 2025) return true;
        }
        return false;
    };
    // Clear ground, and no monster already within its 2-subtile footprint
    // (game.exe stamps each placed monster into collision, 0x800).
    auto fits = [&](int x, int y) {
        for (const auto& other : spawns) if (std::abs(other.x - x) < 2 && std::abs(other.y - y) < 2) return false;
        return !level.unit_blocked((float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5);
    };
    // Its preset units (Level::units in this room), as FUN_0054e600 spawns
    // them (docs/research/re/monsters.md "Preset units on the server"): a
    // MonStats row at its spot; a superunique with its minions; a MonPlace
    // code by the switch (Fallen and shamans, champion packs, unique packs,
    // Blood Raven; the rest, group25..100 among them, spawn nothing).
    // ponytail: the level-list walk up a family's chain (FUN_0063ec70's
    // second half) and the champion / unique pick from Levels.txt umon1..
    // in normal aren't there (the region's list stands in); a boss's company
    // rolls the room seed, not the monster's own.
    using d2d::rules::monster_detail::place;
    const int nmon = int(game_data.mon_bin.size()), nsu = int(game_data.superuniques.size());
    auto bin = [&](int bin_row) { return bin_row >= 0 && bin_row < nmon ? int(game_data.mon_bin[std::size_t(bin_row)]) : -1; };
    // FUN_005b2f20 at the spot (radius -1), then within `retry` if taken.
    auto at_spot = [&](int type, int x, int y, int retry) {
        if (type < 0) return;
        int spot_x, spot_y;
        if (place(room, x, y, -1, fits, spot_x, spot_y) || (retry > 0 && place(room, x, y, retry, fits, spot_x, spot_y)))
            spawns.push_back({ type, spot_x, spot_y, -1, -1, d2d::rules::Boss::none, {}, 0, spawning.game.next() });
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
        if (unit.type == 2 || unit.type == 5) { spawning.game.next(); continue; }   // an object or a warp tile: a unit made (FUN_00555230)
        if (unit.type != 1 || unit.id < 0) continue;
        if (unit.id < nmon) {                                                // a MonStats row (FUN_0054e490)
            const auto monstats_row = game_data.mon_bin[std::size_t(unit.id)];
            const bool stay = unit.id == 0xe5 || (unit.id >= 0x11c && unit.id <= 0x120) || unit.id == 0x188 || unit.id == 0x189;   // FUN_0054e3a0
            if (!game_data.mon_is_npc[monstats_row]) at_spot(int(monstats_row), unit.x, unit.y, stay ? 0 : 4);
            else spawning.game.next();                                       // an NPC (Flavie): made, drawn by the level's NPCs
            continue;
        }
        if (unit.id >= nmon + nsu) {                                         // MonPlace
            const int code = unit.id - nmon - nsu;
            if (code == 0x11 || code == 0x12) at_spot(own(code == 0x11 ? 0x13 : 0x3a), unit.x, unit.y, 4);   // place_fallen / _fallenshaman
            else if (code == 0x05) at_spot(bin(0x10b), unit.x, unit.y, 0);      // place_bloodraven
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
        // ponytail: its unique stat bonuses and TC come in the fight / loot;
        // the per-superunique specials (the Countess' stat 0x76, AI 0xd and
        // quest 5, the Cow King's quest 4) wait for quest-unit binding
        // (FUN_005436b0); its mods roll the room seed, not the monster's own.
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
        const int minion = monsters.types[std::size_t(sup.type)].minion[0] >= 0 ? monsters.types[std::size_t(sup.type)].minion[0] : sup.type;
        const int low = sup.min_grp + (sup.min_grp && sup.max_grp ? difficulty : 0), high = sup.max_grp + (sup.min_grp && sup.max_grp ? difficulty : 0);
        const int count = room.seed.range(low, std::max(low, high));
        int placed = 0;
        for (int k = 0; k < count; ++k) {
            int spot_x, spot_y;
            if (place(room, leader_x, leader_y, 3, fits, spot_x, spot_y)) {
                spawns.push_back({ minion, spot_x, spot_y, lead, -1, d2d::rules::Boss::minion, {}, 0, spawning.game.next() });
                ++placed;
            }
        }
        d2d::log::info("  {} ({}) with {} minions at ({:.1f}, {:.1f})", sup.name, monsters.types[std::size_t(sup.type)].id, placed,
                       (float(leader_x) + 0.5f) / 5, (float(leader_y) + 0.5f) / 5);
    }
    d2d::rules::populate_room(monsters, region, level.mon.density[std::size_t(difficulty)], room, spawning.game, fits, near_way, spawns, &pop);
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
    if (arrived) {                                       // FUN_0056cf40: the room the player lands in, at once
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
        if (near_room.level->rooms.empty()) continue;   // the camp: nothing populates
        if (state.up.size() == near_room.level->rooms.size() && state.up[std::size_t(near_room.room)]) continue;
        if (std::ranges::any_of(fresh, [&](const NearRoom& other) { return other.level == near_room.level && other.room == near_room.room; })) continue;
        fresh.push_back(near_room);
    }
    for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
        const auto from = size_of(it->level);
        populate(game_data, spawning, *it->level, std::size_t(it->room));
        note(it->level, from);
    }
    return grown;
}


// Footprints into the walk grid, centred on each unit's subtile.
// (Quest-gated units like Cain stay out of it: they're not always there.)
// ponytail: static — fine while NPCs only idle; moving units need a
// separate occupancy layer.
void stamp_footprints(Level& level) {
    const int walk_width = level.ds1.width() * 5, walk_height = level.ds1.height() * 5;
    if (level.walk.size() != std::size_t(walk_width) * std::size_t(walk_height)) return;
    for (const auto& npc : level.npcs) {
        if (!npc.path.empty() || npc.quest) continue;   // walkers don't hold a spot
        const int center_x = int(npc.x * 5), center_y = int(npc.y * 5);
        for (int y = center_y - npc.size_y / 2; y < center_y - npc.size_y / 2 + npc.size_y; ++y)
            for (int x = center_x - npc.size_x / 2; x < center_x - npc.size_x / 2 + npc.size_x; ++x)
                if (x >= 0 && y >= 0 && x < walk_width && y < walk_height)
                    level.walk[std::size_t(y) * std::size_t(walk_width) + std::size_t(x)] |= 0x01;
    }
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
    if (objects.get(row, "InitFn") == "3") {      // a chest: its trap and lock (FUN_0054fcb0)
        const auto& area_levels = game_data.area_level;
        const int mlvl1 = std::size_t(into.id) < area_levels.size() ? area_levels[std::size_t(into.id)][3] : 1;
        const auto chest = d2d::rules::roll_chest(mlvl1, objects.get(row, "Lockable") == "1", rgn);
        npc.trap = chest.trap; npc.locked = chest.locked;
        if (npc.locked) if (auto locked_name = lookup_string(game_data, "lockedchest")) npc.name = u16_to_latin1(*locked_name);
    }
    npc.preoperated = objects.get(row, "PreOperate") == "1" && rgn(14) == 0;
    npc.base_w = "hth";
    for (std::size_t mode = 0; mode < 8; ++mode) npc.lit[mode] = std::uint8_t(std::atoi(std::string(objects.get(row, "Lit" + std::to_string(mode))).c_str()));
    const bool lit_mode = npc.preoperated || (objects.get(row, "Mode2") == "1" && !objects.get(row, "Lit2").empty()
                 && objects.get(row, "Lit2") != "0" && npc.operate_fn != 2 && npc.operate_fn != 4   // shrines / chests: NU until used
                 && (npc.operate_fn != 23 || std::ranges::contains(std::array{ 1, 40, 75, 103, 109 }, into.id)));   // waypoints: on in towns (InitFn 17)
    npc.mode   = lit_mode ? "ON" : "NU";
    // Hover name when selectable in its start mode (Selectable0 = NU,
    // 2 = ON): objects.txt Name through the string tables.
    if (objects.get(row, lit_mode ? "Selectable2" : "Selectable0") == "1") {
        const std::string key(objects.get(row, "Name"));
        auto name_found = lookup_string(game_data, key);
        npc.name = name_found ? u16_to_latin1(*name_found) : key;
    }
    // Blocks walking in its start mode (HasCollision0 = NU, 2 = ON).
    if (objects.get(row, lit_mode ? "HasCollision2" : "HasCollision0") == "1") {
        npc.size_x = std::atoi(std::string(objects.get(row, "SizeX")).c_str());
        npc.size_y = std::atoi(std::string(objects.get(row, "SizeY")).c_str());
    }
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
                for (const auto& pick : level.picks[std::size_t(cell_y) * std::size_t(map.width()) + std::size_t(cell_x)])
                    if (pick.layer == 1 || (pick.layer == 0 && pick.orient != 13 && pick.orient != 15)) stamp_tile(cell_x, cell_y, *pick.tile);
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

// Its rooms brought up (drlg level_room_tiles, proven against game.exe):
// every cell's picked tiles, its warps, then lookup and collision.
std::size_t set_level_tiles(Level& level, const d2d::drlg::OutdoorAssets& assets, const LevelDt1s& dt1s,
                            const std::vector<d2d::drlg::Outdoor::RoomSeed>& made, const std::vector<d2d::drlg::PlainRoom>& plain,
                            std::vector<std::string>& notes) {
    const auto built = d2d::drlg::level_room_tiles(made, plain, assets.data, dt1s.heads, level.id, d2d::drlg::warp_slots(assets, level.id), notes);
    const int width = level.ds1.width(), height = level.ds1.height();
    level.picks.assign(std::size_t(width) * std::size_t(height), {});
    std::size_t placed = 0;
    for (const auto& room : built)
        for (const auto& tile : room.tiles) {
            if (tile.x < 0 || tile.y < 0 || tile.x >= width || tile.y >= height || !tile.file || tile.index < 0) continue;
            const auto found = dt1s.archive.find(tile.file);
            if (found == dt1s.archive.end() || std::size_t(tile.index) >= found->second->size()) continue;
            level.picks[std::size_t(tile.y) * std::size_t(width) + std::size_t(tile.x)].push_back(
                { std::uint8_t(tile.layer), std::uint8_t(tile.orient), &found->second->tiles()[std::size_t(tile.index)],
                  tile.layer != 2 && (tile.word & 0x80000000u) != 0 });
            ++placed;
        }
    level.room1_seeds.assign(made.size(), 0);
    for (const auto& room : built)
        if (room.seed) level.room1_seeds[std::size_t(room.seed - made.data())] = room.room1_seed;
    for (const auto& room : built)
        for (const auto& unit : room.units) level.units.push_back({ unit.type, unit.id, unit.mode, unit.x + room.x * 5, unit.y + room.y * 5, unit.flags });
    // Warps: the slot's Levels.txt Vis / Warp, LvlWarp's ExitWalk, and the
    // tile unit's spot (FUN_0066e1c0: the slot's LvlWarp Offset).
    const auto slots = d2d::drlg::warp_slots(assets, level.id);
    if (const auto row = d2d::drlg::level_row(assets.levels, level.id))
        for (const auto& room : built)
            for (const auto& warp : room.warps) {
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
                level.warps.push_back({ float(warp.x), float(warp.y), destination, exit_x, exit_y,
                                        (float(warp.x * 5 + slot.off_x) + 0.5f) / 5, (float(warp.y * 5 + slot.off_y) + 0.5f) / 5 });
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
    int court_file = level.id == 27 || level.id == 28 ? d2d::drlg::courtyard_file(game_data.act1_layout, d2d::drlg::act_seed(game_data.map_seed)) : -1;
    const auto [origin_x, origin_y] = d2d::drlg::level_origin(assets.levels, row);
    std::array<int, 4> rect{ origin_x, origin_y, size_x, size_y };
    if (const auto court = d2d::drlg::level_row(assets.levels, 27); level.id == 28 && court) {   // the Barracks sits beside level 27
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
    const bool outdoor = std::ranges::any_of(game_data.act1_layout, [&](const auto& placement) { return placement.level == id; });
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
    // Its preset units (Level::units): objects, and monsters MonStats marks
    // as NPCs (Flavie by the Blood Moor's way in). Unit ids are game.exe's:
    // MonStats rows without its Expansion row.
    // ponytail: each level starts from the game's object seed as made
    // (object_seed); game.exe has one for the game, drawn as rooms come up.
    auto rgn = object_seed(game_data.map_seed);
    for (const auto& unit : level->units) {
        if (unit.type == 2) add_object(game_data, builder.objects, builder.obj_row, *level, unit.id, unit.x, unit.y, rgn);
        if (unit.type != 1 || unit.id < 0 || std::size_t(unit.id) >= game_data.mon_bin.size()) continue;
        const auto bin_row = game_data.mon_bin[std::size_t(unit.id)];
        if (!game_data.mon_is_npc[bin_row] || game_data.mon_npc[bin_row].code.empty()) continue;
        auto npc = game_data.mon_npc[bin_row];
        npc.x = (float(unit.x) + 0.5f) / 5;
        npc.y = (float(unit.y) + 0.5f) / 5;
        level->npcs.push_back(std::move(npc));
    }
    // Random object groups (FUN_00552610): the same picks a room's
    // populate() rolls, made here at build time so they land in Level::npcs
    // and render alongside preset objects. Positions come from the object
    // seed on a naive fits check; game.exe uses the room's outdoor rect
    // and a subtile pick per PopulateFn.
    // ponytail: not bit-exact placement — the picks match game.exe's row
    // choice, but the positions do not.
    const std::size_t npcs_before_groups = level->npcs.size();
    level->post_object_group_seeds.assign(level->rooms.size(), 0);
    for (std::size_t room_index = 0; room_index < level->rooms.size(); ++room_index) {
        const auto& made = level->rooms[room_index];
        const std::uint32_t seed = room_index < level->room1_seeds.size() ? level->room1_seeds[room_index] : made.seed;
        d2d::rules::Rng room_seed{ seed };
        const auto picks = d2d::rules::place_object_groups(level->mon, game_data.obj_groups, room_seed,
                                                            int(room_index), int(level->rooms.size()));
        level->post_object_group_seeds[room_index] = room_seed.low;
        for (const auto& pick : picks) {
            for (int retry = 0; retry < 12; ++retry) {
                const int subtile_x = made.x * 5 + int(rgn(std::max(1, made.width * 5)));
                const int subtile_y = made.y * 5 + int(rgn(std::max(1, made.height * 5)));
                const std::size_t before = level->npcs.size();
                add_object(game_data, builder.objects, builder.obj_row, *level, pick.object_id, subtile_x, subtile_y, rgn);
                if (level->npcs.size() > before) {
                    auto& placed = level->npcs.back();
                    if (level->unit_blocked(placed.x, placed.y)) { level->npcs.pop_back(); continue; }
                    break;
                }
                break;   // add_object refused (unknown id); no retry needed
            }
        }
    }
    stamp_footprints(*level);
    if (const std::size_t groups_placed = level->npcs.size() - npcs_before_groups; groups_placed > 0)
        d2d::log::info("  {}: {} random object-group placements", level->name, groups_placed);
    d2d::log::info("  {} built ({} ms)", level->name, d2d::log::ms() - start_ms);
    return level;
}

// A finished build into GameData::levels (nullptr: tried, not built); an act
// level is linked with the act levels already there, both ways.
void install_level(const GameData& game_data, int id, std::unique_ptr<Level> level) {
    auto in_act = [&](int level_id) { return std::ranges::any_of(game_data.act1_layout, [&](const auto& placement) { return placement.level == level_id; }); };
    if (level && in_act(id)) {
        std::vector<const Level*> others;
        if (game_data.town.ds1.width() > 0) others.push_back(&game_data.town);
        for (const auto& [level_id, built] : game_data.levels) if (built && in_act(level_id)) others.push_back(built.get());
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
}

}  // namespace d2d::game
