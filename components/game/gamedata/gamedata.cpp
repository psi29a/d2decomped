// SPDX-License-Identifier: GPL-3.0-or-later
// GameData's runtime side: levels built on demand (the level builder),
// their populations, the COF timings the World reads. A translation unit
// of its own; load.hpp fills GameData at start.
#include "gamedata.hpp"

#include "ai.hpp"
#include "ai/units.hpp"
#include "cof_anim.hpp"
#include "game.hpp"
#include "level.hpp"
#include "log.hpp"
#include "npc.hpp"
#include "spawning.hpp"

#include <compcode.hpp>
#include <drlg.hpp>
#include <dt1.hpp>
#include <level_ids.hpp>
#include <maze.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <mpq.hpp>
#include <object_ids.hpp>
#include <outdoor.hpp>
#include <outdoor_data.hpp>
#include <quests.hpp>
#include <room_tiles.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <tile_pick.hpp>
#include <txt.hpp>
#include <unit_size.hpp>
#include <wav_length.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// The heavy members (the MPQs, the town, the act layouts) on the heap: the
// header holds them by pointer, so most files never parse their types.
GameData::GameData()
    : mpq_stack(std::make_unique<d2d::mpq::Stack>()), mpqs(*mpq_stack), town_level(std::make_unique<Level>()), town(*town_level),
      act_layouts(std::make_unique<ActLayouts>()), layouts(*act_layouts) {}
// Moved, the references bind to the same heap objects the pointers hand over.
GameData::GameData(GameData&&) noexcept = default;
GameData::~GameData() = default;

int level_act(const Level* level) { return level ? d2d::rules::level_ids::act_of(level->id) : 0; }

const std::vector<d2d::drlg::Placed>& ActLayouts::layout_of(int id) const {
    static const std::vector<d2d::drlg::Placed> kNone;
    switch (d2d::rules::level_ids::act_of(id)) {
        case 0: return act1_layout;
        case 1: return act2_layout;
        case 2: return act3_layout.levels;
        case 3: return act4_layout;
        case 4: return act5_layout;
        default: return kNone;
    }
}

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
// An NPC's or object's: <root>\<code>\COF\<code><mode><weapon class>, the
// class from cof_weapon_class (BaseW, or hth for a monster's death).
// A monster's mode draws with MonMode.txt's token: knockback (code KB) is
// GH's COF, as no monster has a KB one.
CofAnim npc_cof(const d2d::mpq::Stack& mpqs, const Npc& npc, std::string_view mode) {
    const std::string_view token = npc.root == "monsters" && mode == "KB" ? std::string_view("GH") : mode;
    return open_cof(mpqs, std::format(R"(data\global\{}\{}\COF\{}{}{}.cof)", npc.root, npc.code, npc.code, token, cof_weapon_class(npc, token)));
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
// A monster's or object's COF timing for a mode, cached by the COF it names.
std::uint32_t GameData::sound_ms(int index) const {
    if (index < 0 || std::size_t(index) >= sounds.size()) return 0;
    if (const auto found = sound_lengths.find(index); found != sound_lengths.end()) return found->second;
    std::uint32_t length = 0;
    for (const char* folder : { R"(data\global\sfx\)", R"(data\local\sfx\)" })
        if (const auto file = mpqs.try_read(std::string(folder) + sounds[std::size_t(index)].file)) {
            length = d2d::rules::wav_ms(*file).value_or(0);
            break;
        }
    sound_lengths.emplace(index, length);
    return length;
}

const GameData::AnimTiming& GameData::npc_timing(const Npc& npc, std::string_view mode) const {
    auto key = npc.root + "/" + npc.code + "/" + std::string(mode) + "/" + std::string(cof_weapon_class(npc, mode));
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

// A level's own light (FUN_00474550 via FUN_00619d70): when any of Levels.txt
// Intensity / Red / Green / Blue is set its Intensity replaces the day's.
int level_light(std::string_view intensity, std::string_view red, std::string_view green, std::string_view blue) {
    auto number = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    return number(intensity) || number(red) || number(green) || number(blue) ? number(intensity) : -1;
}

// A MonStats row as a composite: its code, MonStats2 BaseW, layers and flags,
// from the MonStats2 row its MonStatsEx names (the tables aren't row-aligned:
// 734 vs 609 rows).
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
    npc.interact = monstats.get(row, "interact") == "1";
    npc.base_w = std::string(ms2.get(row2, "BaseW"));
    npc.composite_death = ms2.get(row2, "compositeDeath") == "1";
    npc.size_x = std::atoi(std::string(ms2.get(row2, "SizeX")).c_str());
    npc.size_y = d2d::rules::unit_size_y(int(d2d::rules::UnitType::kMonster), std::atoi(std::string(ms2.get(row2, "SizeY")).c_str()));   // FUN_006205a0
    npc.shape = UnitShape::monster(npc.size_x, (monstats.get(row, "npc") == "1" || monstats.get(row, "inTown") == "1") && monstats.get(row, "interact") != "1",
                                   monstats.get(row, "flying") == "1", monstats.get(row, "opendoors") == "1");
    npc.light  = std::atoi(std::string(ms2.get(row2, "Light")).c_str());
    npc.overlay_class = std::atoi(std::string(ms2.get(row2, "OverlayHeight")).c_str()) - 1;
    npc.hit_class = std::atoi(std::string(ms2.get(row2, "HitClass")).c_str());
    npc.trans_lvl = std::atoi(std::string(monstats.get(row, "TransLvl")).c_str());
    npc.no_unique_shift = ms2.get(row2, "noUniqueShift") == "1";
    npc.dead_col = ms2.get(row2, "deadCol") == "1";
    npc.height = std::atoi(std::string(ms2.get(row2, "Height")).c_str());
    npc.automap = std::atoi(std::string(ms2.get(row2, "automapCel")).c_str());   // MonStats2 +0x118 (FUN_00458dc0's unit type 1)
    npc.utrans = { std::atoi(std::string(ms2.get(row2, "Utrans")).c_str()), std::atoi(std::string(ms2.get(row2, "Utrans(N)")).c_str()),
                 std::atoi(std::string(ms2.get(row2, "Utrans(H)")).c_str()) };
    if (const auto velocity = monstats.get(row, "Velocity"); !velocity.empty()) npc.velocity = float(std::atoi(std::string(velocity).c_str()));
    const auto ai_name = monstats.get(row, "AI");
    npc.town_ai = ai_name == "Npc" ? TownAi::npc : ai_name == "Towner" ? TownAi::towner : ai_name == "Vendor" ? TownAi::vendor : ai_name == "JarJar" ? TownAi::jarjar
                : ai_name == "Navi" ? TownAi::navi : TownAi::none;
    npc.modes = (ms2.get(row2, "mWL") == "1" ? 1u << 2 : 0u) | (ms2.get(row2, "mS1") == "1" ? 1u << 8 : 0u) | (ms2.get(row2, "mS2") == "1" ? 1u << 9 : 0u);
    // Hover name: MonStats' string key, only for units MonStats2 marks
    // selectable (isSel) — not the chicken or the camp's guard rogues,
    // whose name key "Dummy" reads "an evil force".
    // `title` is that name whatever isSel says (an item's "Reanimate as:", FUN_004e4d80's 0x17).
    {
        std::string key(monstats.get(row, "NameStr"));        // 1.14d; "namco" on the CD
        if (key.empty()) key = std::string(monstats.get(row, "namco"));
        auto name = lookup_string(game_data, key);
        npc.title = name ? u16_to_latin1(*name) : std::move(key);
        if (ms2.get(row2, "isSel") == "1") npc.name = npc.title;
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

// Skills (components/rules/combat/skills.hpp): skillcalc.txt's operand names,
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
    spawning.proxy = d2d::rules::Rng{ spawning.game.next() };     // sunitproxy
    spawning.quests = d2d::rules::Rng{ spawning.game.next() };    // the quests' (FUN_00545d80 -> FUN_00650e40: list +0x18)
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
// then, for each vis slot n its flags hold (+0x28 & 0x10 << n, FUN_00667970:
// a wall tile of type 10 / 11 and style n; none in level 0x85), that slot's
// level (FUN_0066c220). Its rooms are tested for the bit of a slot j of its
// own Vis that names this level (FUN_0066be80, list order): for a walk
// across (Warp n -1), each such room close to this one, the first j that
// finds any; for a warp, the first such room, j the k-th slot back with k
// this slot's rank among this level's slots to that level, else the first
// j that finds one (FUN_0066be10 links it). Each append sorts the list
// again (FUN_0066bda0). tools/emu/near_rooms.py (test_room_near --near).
// ponytail: a walk across is by closeness to a level whose Vis or this
// one's names the other (Warp -1): an outdoor border room's slot flags
// (game.exe's +0x28 0x70 along a Cold Plains edge) aren't kept, only a
// preset's own vis walls (warps). A level with no flags (the camp, a level
// not generated) takes every close room of its next-door levels, and its
// warps by the warp back's room.
std::vector<NearRoom> near_list(const GameData& game_data, const Level& level, int room) {
    const auto rects = room_rects(level);
    const auto& self = rects[std::size_t(room)];
    std::vector<NearRoom> list;
    for (std::size_t i = rects.size(); i-- > 0;)
        if (close(self, rects[i])) list.push_back({ &level, int(i), rects[i] });
    sort_near(list);
    const auto flagged = [](const Level& other) { return !other.rooms.empty() && other.room_flags.size() == other.rooms.size(); };
    const auto flags = flagged(level) ? level.room_flags[std::size_t(room)] : 0;
    if (flagged(level) && level.id != 0x85)
        for (std::size_t slot = 0; slot < 8; ++slot) {
            // A warp's level is built here as game.exe does (FUN_006424a0).
            const bool warp = level.vis_warp[slot] != -1;
            if (!warp || !(flags & (0x10u << slot)) || level.vis[slot] <= 0) continue;
            const Level* other = game_data.level(level.vis[slot]);
            if (!other || !flagged(*other)) continue;
            const auto other_rects = room_rects(*other);
            // FUN_0066be80 on the other level's slot j.
            const auto take = [&](std::size_t back) {
                bool found = false;
                for (std::size_t i = other_rects.size(); i-- > 0;) {
                    if (!(other->room_flags[i] & (0x10u << back))) continue;
                    if (warp) { list.push_back({ other, int(i), other_rects[i] }); sort_near(list); return true; }
                    if (close(self, other_rects[i])) { list.push_back({ other, int(i), other_rects[i] }); sort_near(list); found = true; }
                }
                return found;
            };
            bool done = false;
            if (warp) {
                const auto rank = std::count(level.vis.begin(), level.vis.begin() + std::ptrdiff_t(slot), level.vis[slot]);
                std::ptrdiff_t seen = 0;
                for (std::size_t back = 0; back < 8 && !done; ++back) {
                    if (other->vis[back] != level.id) continue;
                    if (seen++ == rank) { done = take(back); break; }
                }
            }
            for (std::size_t back = 0; back < 8 && !done; ++back)
                if (other->vis[back] == level.id) done = take(back);
        }
    const auto walks_to = [](const Level& from, int id) {
        for (std::size_t slot = 0; slot < 8; ++slot) if (from.vis[slot] == id && from.vis_warp[slot] == -1) return true;
        return false;
    };
    for (const auto& neighbour : level.nearby) {
        // A link is added both ways (FUN_00642920), so either side's Vis naming the other holds.
        if (flagged(level) && flagged(*neighbour.level) && !walks_to(level, neighbour.level->id) && !walks_to(*neighbour.level, level.id)) continue;
        const auto other = room_rects(*neighbour.level);
        for (std::size_t i = other.size(); i-- > 0;)
            if (close(self, other[i])) { list.push_back({ neighbour.level, int(i), other[i] }); sort_near(list); }
    }
    for (const auto& warp : level.warps) {
        if (room_holding(rects, level, warp.x, warp.y) != room) continue;
        const Level* destination = game_data.level(warp.destination);
        if (!destination || destination->rooms.empty()) continue;
        if (flagged(level) && flagged(*destination)) continue;                // the flags' (above)
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
    // Its room seeds come with the first roll on them (keep_camp_seeds).
    if (level.rooms.empty()) {
        if (state.rooms_up.empty()) state.rooms_up.assign(room_count(level), false);
        if (made_index >= state.rooms_up.size() || state.rooms_up[made_index]) return;
        state.rooms_up[made_index] = true;
        for (const char* root : { "objects", "monsters" })
            for (auto it = level.npcs.rbegin(); it != level.npcs.rend(); ++it)
                if (it->room == int(made_index) && !it->quest && it->root == root) {
                    it->seed = d2d::rules::Rng{ spawning.game.next() };
                    // Its init's two draws: the look (FUN_005739d0) and the life
                    // (FUN_00573cb0); what the Npc AI thinks on (town_npcs.hpp).
                    // The guard rogues' init draws (up to 11) aren't stepped: AI Idle, they never read
                    // their seed again (Idle only stands 200 frames: monster-ai.md), so none of it shows.
                    if (it->town_ai != TownAi::none) { it->seed.next(); it->seed.next(); }
                }
        return;
    }
    const auto& region = std::size_t(level.id) < spawning.regions.size() ? spawning.regions[std::size_t(level.id)] : d2d::rules::Region{};
    if (state.rooms_up.empty()) {
        state.rooms_up.assign(level.rooms.size(), false);
        level.region[std::size_t(difficulty)].clear();
        for (const auto& [row, rarity] : region.types) level.region[std::size_t(difficulty)].push_back(row);
    }
    if (made_index >= level.rooms.size() || state.rooms_up[made_index] || level.walk.empty()) return;
    state.rooms_up[made_index] = true;
    auto& spawns = state.spawns;
    const std::size_t first = spawns.size();
    const auto& made = level.rooms[made_index];
    auto& level_rw = const_cast<Level&>(level);                 // GameData owns its levels mutable
    const auto npcs_before = level.npcs.size();
    auto stamp_made = [&] {
        for (auto i = npcs_before; i < level_rw.npcs.size(); ++i) stamp_footprint(level_rw, level_rw.npcs[i]);
    };
    // objgroups.cpp's port, game.exe's to the subtile on every difficulty.
    room_objects(game_data, spawning, level, made_index);
    stamp_made();
    seed_client_units(level_rw);                                // the new ones, as they're made
    if (spawns.size() > first)
        d2d::log::info("  room ({}, {}) of {} {}: {} monsters", made.x, made.y, level.name, kSfx[difficulty], spawns.size() - first);
}

}  // namespace

int room_of(const Level& level, float x, float y) { return room_holding(room_rects(level), level, x, y); }

std::optional<d2d::drlg::Outdoor::RoomSeed> level_room(const Level& level, std::size_t index) {
    const auto rects = room_rects(level);
    if (index >= rects.size()) return std::nullopt;
    const auto& rect = rects[index];
    return d2d::drlg::Outdoor::RoomSeed{ .x = rect.x - level.world_x, .y = rect.y - level.world_y, .width = rect.width, .height = rect.height };
}

bool keep_camp_seeds(const GameData& game_data, const Level& level, Spawning::LevelState& state) {
    if (!level.rooms.empty() || !state.room_seeds.empty()) return true;
    const auto camp = camp_room_seeds(game_data);
    if (!camp) return false;
    // ponytail: the DRLG room's seed; game.exe's room1 seed is drawn after
    // the room's tile picks (room_tiles.hpp room1_seed), which the camp
    // doesn't lay, so a teleport's spot rolls differently.
    const auto rects = room_rects(level);
    state.room_seeds.assign(rects.size(), {});
    for (const auto& room : *camp)
        for (std::size_t index = 0; index < rects.size(); ++index)
            if (rects[index].x - level.world_x == room.x && rects[index].y - level.world_y == room.y) state.room_seeds[index] = d2d::rules::Rng{ room.seed };
    return true;
}

// The level's own monster of MonStats `row`'s family (FUN_0063ec70): the
// first of the level's list (Levels.bin +0x33 / +0x36: Mon1..10) of that
// BaseId; else up the family's chain (NextInClass, at most its count,
// MonStats +0x4a) while the next's Level is at most MonLvl1Ex + 1. A level
// with no list keeps `row`.
int level_monster(const GameData& game_data, const Level& level, int row) {
    const auto& types = game_data.monsters.types;
    if (row < 0 || std::size_t(row) >= types.size() || std::ranges::none_of(level.mon.mon, [](int mon_row) { return mon_row >= 0; })) return row;
    const auto listed = std::ranges::find_if(level.mon.mon, [&](int mon_row) { return mon_row >= 0 && std::size_t(mon_row) < types.size() && types[std::size_t(mon_row)].base == types[std::size_t(row)].base; });
    if (listed != level.mon.mon.end()) return *listed;
    const int top = std::size_t(level.id) < game_data.area_level.size() ? game_data.area_level[std::size_t(level.id)][0] + 1 : 0;
    for (int next = types[std::size_t(row)].next_in_class; next >= 0 && std::size_t(next) < types.size() && types[std::size_t(next)].level[0] <= top; next = types[std::size_t(next)].next_in_class) row = next;
    return row;
}

std::uint32_t area_id_at(const GameData& game_data, const Level& level, float from_x, float from_y, float x, float y) {
    const float act_x = x + float(level.world_x), act_y = y + float(level.world_y);
    auto id_in = [&](const Level& holder, int room, const RoomRect& rect) -> std::uint32_t {   // FUN_0066ce30 on that room
        if (act_x < float(rect.x) || act_y < float(rect.y) || act_x >= float(rect.x + rect.width) || act_y >= float(rect.y + rect.height)) return 0;
        if (std::size_t(room) >= holder.room_areas.size() || holder.room_areas[std::size_t(room)].empty()) return 1;   // a whole room (the camp's too)
        const int at_x = int(act_x) - holder.world_x, at_y = int(act_y) - holder.world_y;
        for (const auto& area : holder.room_areas[std::size_t(room)])
            if (at_x >= area.left && at_y >= area.top && at_x < area.right && at_y < area.bottom) return area.id;
        return 0xffffffffu;
    };
    const Level* from_level = &level;
    int from_room = room_of(level, from_x, from_y);
    for (std::size_t k = 0; from_room < 0 && k < level.nearby.size(); ++k) {             // the unit stands next door
        from_level = level.nearby[k].level;
        from_room = room_of(*from_level, from_x - float(level.nearby[k].dx), from_y - float(level.nearby[k].dy));
    }
    if (from_room < 0) return 0;
    if (const auto id = id_in(*from_level, from_room, room_rects(*from_level)[std::size_t(from_room)])) return id;
    for (const auto& near_room : near_list(game_data, *from_level, from_room))
        if (const auto id = id_in(*near_room.level, near_room.room, near_room.rect)) return id;
    return 0;
}

// The near list (room2 +8, FUN_0066c370) of the room holding (x, y) cells:
// each (level, room index) in game.exe's order; empty outside every room.
std::vector<std::pair<const Level*, int>> near_rooms(const GameData& game_data, const Level& level, float x, float y) {
    std::vector<std::pair<const Level*, int>> rooms;
    const int room = room_holding(room_rects(level), level, x, y);
    if (room < 0) return rooms;
    for (const auto& near_room : near_list(game_data, level, room)) rooms.emplace_back(near_room.level, near_room.room);
    return rooms;
}


// Where a missile in the room holding (x, y) cells can be: that room's rect,
// then its near list's, act subtiles {x, y, w, h} (FUN_00463740 over room1
// +0 / +0x24). Empty when no room holds it. With `spawning`, a near room
// counts once it's come up (Spawning::LevelState::order): room1 +0 holds
// null for one whose room1 isn't made, and FUN_00463740 skips it. A level
// no rooms are kept for (no state): all count. The fight, the thinks and the
// client pass the World's.
std::vector<std::array<int, 4>> missile_rooms(const GameData& game_data, const Level& level, float x, float y, const Spawning* spawning) {
    const int room = room_holding(room_rects(level), level, x, y);
    if (room < 0) return {};
    const auto list = near_list(game_data, level, room);
    const bool kept = spawning && spawning->levels.contains(&level);
    const auto made = [&](const NearRoom& near_room) {
        if (!kept) return true;
        const auto found = spawning->levels.find(near_room.level);
        return found != spawning->levels.end() && std::ranges::contains(found->second.order, std::size_t(near_room.room));
    };
    std::vector<std::array<int, 4>> rects{ { 0, 0, 0, 0 } };
    for (const auto& near_room : list) {
        const std::array<int, 4> rect{ near_room.rect.x * 5, near_room.rect.y * 5, near_room.rect.width * 5, near_room.rect.height * 5 };
        const bool own = near_room.level == &level && near_room.room == room;
        if (own) rects[0] = rect;
        if (own || made(near_room)) rects.push_back(rect);   // FUN_00463740: a null slot of room1 +0 is skipped
    }
    return rects;
}

std::vector<AutomapRoom> automap_rooms(const GameData& game_data, const Level& level, float x, float y) {
    const int room = room_holding(room_rects(level), level, x, y);
    if (room < 0) return {};
    std::vector<AutomapRoom> rooms;
    for (const auto& near_room : near_list(game_data, level, room))
        rooms.push_back({ near_room.level, near_room.room, near_room.rect.x, near_room.rect.y, near_room.rect.width, near_room.rect.height });
    return rooms;
}

// The player at (x, y), cells, in `level`: rooms coming into play round
// the player's room populate, newest first. `arrived`: the player just
// came in, and its own room populates first (FUN_0056cf40). Returns each
// level whose spawns grew, with its first new spawn's index.
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
    std::vector<std::pair<const Level*, std::size_t>> batch_from;   // where this move's batch starts in client_order
    auto bring_up = [&](const Level* which, std::size_t index) {
        auto& state = spawning.levels[which];
        if (state.live.size() != room_count(*which)) state.live.resize(room_count(*which));
        if (state.idle_sweeps.size() != room_count(*which)) state.idle_sweeps.resize(room_count(*which));
        if (index < state.live.size() && state.live[index]) return;
        if (index < state.live.size()) { state.live[index] = true; state.idle_sweeps[index] = 0; }
        auto& order = state.order;
        if (std::ranges::contains(order, index)) return;
        order.push_back(index);
        auto& client = state.client_order;
        auto from = std::ranges::find(batch_from, which, &std::pair<const Level*, std::size_t>::first);
        if (from == batch_from.end()) from = batch_from.insert(batch_from.end(), { which, client.size() });
        client.push_back(index);
        std::sort(client.begin() + std::ptrdiff_t(from->second), client.end());   // the client's batch: made order
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
    // FUN_0052d240 may have released a populated room1. Its population bit
    // stays set, so bring_up re-lays the room without rolling presets again.
    std::vector<NearRoom> fresh;
    for (const auto& near_room : near_list(game_data, level, room)) {
        auto& state = spawning.levels[near_room.level];
        if (state.live.size() == room_count(*near_room.level) && state.live[std::size_t(near_room.room)]) continue;
        if (std::ranges::any_of(fresh, [&](const NearRoom& other) { return other.level == near_room.level && other.room == near_room.room; })) continue;
        fresh.push_back(near_room);
    }
    for (const auto& near_room : fresh) bring_up(near_room.level, std::size_t(near_room.room));
    // The client draws further out: every room of this level within two rooms
    // of the player's (live Lut Gholein: the 5x5 block, 20 rooms with the
    // level's edge), after the near list, made order (Level::drawn only).
    // ponytail: this level's rooms only, two 8-cell rooms out; the client's own
    // reach (FUN_?) isn't traced, nor whether it crosses into the next level.
    if (level.rooms.size() == rects.size()) {
        auto& state = spawning.levels[&level];
        const auto& here = rects[std::size_t(room)];
        const std::size_t ring_from = state.client_order.size();
        for (std::size_t index = 0; index < rects.size(); ++index) {
            const auto& other = rects[index];
            const bool reach = other.x < here.x + here.width + 16 && other.x + other.width > here.x - 16
                            && other.y < here.y + here.height + 16 && other.y + other.height > here.y - 16;
            if (reach && !std::ranges::contains(state.client_order, index)) state.client_order.push_back(index);
        }
        std::sort(state.client_order.begin() + std::ptrdiff_t(ring_from), state.client_order.end());
        if (state.client_order.size() > ring_from && !std::ranges::contains(moved, &level)) moved.push_back(&level);
    }
    lay();
    for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
        const auto from = size_of(it->level);
        populate(game_data, spawning, *it->level, std::size_t(it->room));
        note(it->level, from);
    }
    return grown;
}

std::vector<std::pair<const Level*, std::size_t>> sweep_idle_rooms(const GameData& game_data, Spawning& spawning,
                                                                    std::span<const RoomPosition> players) {
    std::vector<std::pair<const Level*, std::size_t>> released;
    if (++spawning.room_sweep_frame < 12) return released;
    spawning.room_sweep_frame = 0;
    std::vector<std::pair<const Level*, std::size_t>> held;
    for (const auto& player : players) {
        if (!player.level) continue;
        const int room = room_holding(room_rects(*player.level), *player.level, player.cell_x, player.cell_y);
        if (room < 0) continue;
        for (const auto& near_room : near_list(game_data, *player.level, room)) {
            const std::pair key{ near_room.level, std::size_t(near_room.room) };
            if (!std::ranges::contains(held, key)) held.push_back(key);
        }
    }
    std::vector<const Level*> changed_levels;
    for (auto& [level, state] : spawning.levels) {
        if (!level || state.live.empty()) continue;
        bool changed = false;
        for (std::size_t room = 0; room < state.live.size(); ++room) {
            if (age_live_room(state, room, std::ranges::contains(held, std::pair{ level, room }))) {
                released.push_back({ level, room });
                changed = true;
            }
        }
        if (changed) changed_levels.push_back(level);
    }
    // Lay each changed level once, after every retirement: a level's shared
    // edge comes from its neighbours' current lay orders (camp_rooms), so it
    // is laid again too when one of its nearby levels changed.
    for (auto& [level, state] : spawning.levels) {
        if (!level || state.live.empty()) continue;
        const bool stale = std::ranges::contains(changed_levels, level) ||
                           std::ranges::any_of(level->nearby, [&](const Level::Near& near_level) { return std::ranges::contains(changed_levels, near_level.level); });
        if (stale) relevel(const_cast<Level&>(*level), state.order, &game_data, &spawning);
    }
    return released;
}


std::vector<std::pair<std::size_t, std::size_t>> populate_level(const GameData& game_data, Spawning& spawning, const Level& level,
                                                                 const std::vector<std::size_t>& came_up,
                                                                 const std::function<void(std::size_t)>& done) {
    auto& state = spawning.levels[&level];
    for (const auto room : came_up) if (!std::ranges::contains(state.order, room)) state.order.push_back(room);
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

// Footprints into the walk grid, centred on each object's subtile, when it
// collides in its start mode (HasCollision0..7): its own FUN_006209d0 bits
// (Npc::stamp: 0x400, a door 0x800 | 0x06, ...; set_footprint). Monsters and
// NPCs aren't stamped here: they move, and their bits are the Crowd's (ai.hpp).
void stamp_footprints(Level& level) {
    for (auto& npc : level.npcs) stamp_footprint(level, npc);
}

void stamp_footprint(Level& level, Npc& npc) {
    if (npc.root != "objects") return;                  // NPCs are units: the Crowd's (ai.hpp)
    set_footprint(level, npc, npc.collision >> mode_index(npc.mode) & 1);
}

// An object's footprint into (solid) or out of the walk grid, leaving the
// subtiles a tile blocks too (FUN_0064de30 / FUN_0064dc00).

// An object's rect (FUN_0064d0d0: its spot less half its size, SizeX x
// SizeY) gets its stamp bits set (FUN_0064cd70) or cleared (FUN_0064cce0).
void set_footprint(const Level& level, const Npc& npc, bool solid) {
    const int walk_width = level.ds1.width() * 5, walk_height = level.ds1.height() * 5;
    if (level.walk.size() != std::size_t(walk_width) * std::size_t(walk_height) || npc.root != "objects") return;
    const int center_x = int(npc.x * 5), center_y = int(npc.y * 5);
    for (int y = center_y - npc.size_y / 2; y < center_y - npc.size_y / 2 + npc.size_y; ++y)
        for (int x = center_x - npc.size_x / 2; x < center_x - npc.size_x / 2 + npc.size_x; ++x) {
            if (x < 0 || y < 0 || x >= walk_width || y >= walk_height) continue;
            auto& cell = level.walk[std::size_t(y) * std::size_t(walk_width) + std::size_t(x)];
            cell = solid ? std::uint16_t(cell | npc.stamp) : std::uint16_t(cell & ~npc.stamp);
        }
}

int mode_index(std::string_view mode) {
    const auto found = std::ranges::find(kObjectModes, mode);
    return found == kObjectModes.end() ? 0 : int(found - kObjectModes.begin());
}

int monster_mode(std::string_view token, int unknown) {
    const auto found = std::ranges::find(kMonsterModes, token);
    return found == kMonsterModes.end() ? unknown : int(found - kMonsterModes.begin());
}

// A type-2 object at subtile (sx, sy): objects.txt Id `oid` (through
// game.exe's preset table) as an Npc in `into`, rolling a shrine's kind and
// a chest's trap and lock, then (FUN_0054f5d0) a PreOperate object starts
// opened (ON) one time in 14. `rgn`: the game's object seed (FUN_00546fa0).
// The unit flag 0x80 FUN_0054f5d0 checks before the PreOperate roll is
// always clear there: its one caller is unit creation (FUN_00555230 at
// 005553fe) on a unit FUN_00620290 zeroes, and the one write of it
// (0054f406, FUN_0054f370: preset 580) comes after that returns.
void add_object(const GameData& game_data, const d2d::txt::Table& objects, const std::unordered_map<std::string, std::size_t>& obj_row,
                Level& into, int oid, int spot_x, int spot_y, d2d::rules::Rng& rgn) {
    const auto found = obj_row.find(std::to_string(oid));
    if (oid == 0 || found == obj_row.end()) return;
    const auto row = found->second;
    Npc npc;
    npc.root   = "objects";
    npc.code   = std::string(objects.get(row, "Token"));
    npc.operate_fn = std::atoi(std::string(objects.get(row, "OperateFn")).c_str());
    npc.mode2 = objects.get(row, "Mode2") == "1";
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
    if (objects.get(row, "InitFn") == "27") npc.trap = rgn.next() % 1000 <= 0x14c ? 3 : 0;   // a goo pile (FUN_0054fc50): trap 3, 333 in 1000
    // The gold placeholder (InitFn 28, FUN_0054f8c0): ON (its first call,
    // FUN_00624690 mode 2), then 1..9 piles at rand(4), rand(4) subtiles
    // off it, where free in its room.
    // Its piles' spots and coins are objgroups.cpp's (ObjectRooms::piles), on the floor by World::drop_new_gold.
    const char* init_mode = nullptr;
    if (objects.get(row, "InitFn") == "28") {
        init_mode = "ON";
        for (int piles = rgn(9) + 1; piles > 0; --piles) { rgn.next(); rgn.next(); }
    }
    // Act 4's (quests-act4.md "Act 4's other objects"): the hell brazier (InitFn 34, FUN_005500f0)
    // lights (mode 1) on an odd step of the object seed; the skeleton spawns (26) take mode 1;
    // the hellfires (22, FUN_0054fb40) mode 2 when objects.txt's +0x141 is set and +0x13f isn't.
    if (objects.get(row, "InitFn") == "34" && (rgn.next() & 1)) init_mode = "OP";
    if (objects.get(row, "InitFn") == "26") init_mode = "OP";
    // The fissures (InitFn 58, FUN_0054fdb0): a step, their first eruption (event 8) that % 250 + 25 frames on
    // (World::fissures plays them).
    if (objects.get(row, "InitFn") == "58") npc.erupts = int(rgn.next() % 250) + 25;
    if (objects.get(row, "InitFn") == "22" && objects.get(row, "Mode2") == "1" && objects.get(row, "Mode0") != "1") init_mode = "ON";
    npc.preoperated = objects.get(row, "PreOperate") == "1" && rgn(14) == 0;
    npc.env_effect = objects.get(row, "EnvEffect") == "1";
    npc.base_w = "hth";
    for (std::size_t mode = 0; mode < 8; ++mode) npc.lit[mode] = std::uint8_t(std::atoi(std::string(objects.get(row, "Lit" + std::to_string(mode))).c_str()));
    // Its start mode: a preset object is made in mode 0 (the DS1 loader, FUN_00665950: drlg ds1_units),
    // as one an object group lays (FUN_00555230); then its InitFn (table 0x731bc0) may set one
    // (FUN_00624690), read in each: 8 (0x5500c0) 2; 10 (FUN_0054f860) 2 outside the town; 12
    // (FUN_0054fe70) and 59 (FUN_0054fe10) 1 from mode 0; 14 (0x5500d0) 1; and 22, 26, 28, 34
    // above. 13 (FUN_00594020) sets 2 only when FUN_00543640 finds no quest record, which a game has. A PreOperate one is ON. Every other
    // InitFn sets none here (1..3, 5, 24, 27, 57, 58: none at all) or sets its quest's (the stones,
    // the Gibbet, the altars, the seals, the portals, ...: their World code). The town's waypoint is
    // on (InitFn 17 activates it where the player has it: game.exe's list, d2d's town rule).
    const int init_fn = std::atoi(std::string(objects.get(row, "InitFn")).c_str());
    if (!init_mode) {
        if (init_fn == 8 || (init_fn == 10 && into.id != d2d::rules::level_ids::kRogueEncampment)) init_mode = "ON";
        else if (init_fn == 12 || init_fn == 14 || init_fn == 59) init_mode = "OP";
        else if (init_fn == 17 && d2d::rules::level_ids::is_town(into.id)) init_mode = "ON";
    }
    const bool lit_mode = npc.preoperated || (init_mode && std::string_view(init_mode) == "ON");
    npc.mode   = init_mode && !npc.preoperated ? init_mode : lit_mode ? "ON" : "NU";
    // Where it blocks walking, by mode (stamp_footprints takes its start mode's).
    npc.object_id = oid;
    npc.name_offset = std::atoi(std::string(objects.get(row, "NameOffset")).c_str());
    npc.automap = std::atoi(std::string(objects.get(row, "AutoMap")).c_str());
    npc.door = objects.get(row, "IsDoor") == "1"; npc.monster_ok = objects.get(row, "MonsterOK") == "1";
    {                                                     // FUN_006209d0
        const bool blocks_vis = objects.get(row, "BlocksVis") == "1", block_missile = objects.get(row, "BlockMissile") == "1";
        const int subclass = std::atoi(std::string(objects.get(row, "SubClass")).c_str());
        npc.stamp = npc.door ? std::uint16_t(blocks_vis ? 0x806 : block_missile ? 0x804 : 0x400)
                             : std::uint16_t((subclass & 4) ? 0x8000 : block_missile ? 0x404 : 0x400);
    }
    for (std::size_t mode = 0; mode < 8; ++mode) {
        if (objects.get(row, "HasCollision" + std::to_string(mode)) == "1") npc.collision |= std::uint8_t(1u << mode);
        if (objects.get(row, "Selectable" + std::to_string(mode)) == "1") npc.selectable |= std::uint8_t(1u << mode);
        if (objects.get(row, "CycleAnim" + std::to_string(mode)) != "1") npc.cycle &= std::uint8_t(~(1u << mode));
        npc.frames[mode] = std::uint8_t(std::atoi(std::string(objects.get(row, "FrameCnt" + std::to_string(mode))).c_str()));
        npc.loop_from[mode] = std::uint8_t(std::atoi(std::string(objects.get(row, "Start" + std::to_string(mode))).c_str()));
        npc.frame_delta[mode] = std::uint16_t(std::atoi(std::string(objects.get(row, "FrameDelta" + std::to_string(mode))).c_str()));
    }
    // Hover name: objects.txt Name through the string tables, for one Selectable in any mode; the
    // hover takes it in the modes it's Selectable in (Selectable0..7 by the mode it's in, ingame.cpp).
    if (npc.selectable != 0) {
        const std::string key(objects.get(row, "Name"));
        auto name_found = lookup_string(game_data, key);
        npc.name = name_found ? u16_to_latin1(*name_found) : key;
    }
    npc.sync = objects.get(row, "Sync") == "1";
    npc.size_x = std::atoi(std::string(objects.get(row, "SizeX")).c_str());
    npc.size_y = d2d::rules::unit_size_y(int(d2d::rules::UnitType::kObject), std::atoi(std::string(objects.get(row, "SizeY")).c_str()));   // FUN_006205a0
    for (std::size_t layer = 0; layer < 16; ++layer)
        if (objects.get(row, kLayerCode[layer]) == "1") npc.comp[layer] = "lit";
    if (npc.code.empty()) return;
    npc.x = (float(spot_x) + 0.5f) / 5;
    npc.y = (float(spot_y) + 0.5f) / 5;
    into.npcs.push_back(std::move(npc));
}

// An Animate preset's tiles (Act 4's lava): FUN_0066d440 lists frames 1..n-1 after the room's other tiles, hidden
// (+0x14 | 8: the floor draw at 0x4dee52 skips 0x408), with a record {frames, n, position, speed, next}; the speed
// is its LvlPrest row's +0x24, which no column fills (0x61ec90's field table: Expansion +0x20, SizeX +0x28), so 0:
// 0x50. Each tick (FUN_0066d410 from 0x61aefd) FUN_0066d3b0 hides the shown frame, steps the position by the speed
// mod n << 8 and shows frame position >> 8; every room starts at 0, so all run in step.
// ponytail: the shown frame is drawn in frame 0's place. game.exe adds frames 1..n-1 to the room's list unchained
// (0x66d6a1: edx 0), and where its floor draw puts them against the rock after them isn't read. The tick is one a
// game frame (40 ms), also not read.
const d2d::dt1::Tile* animated_tile(std::span<const Level::Pick> picks, std::size_t index, std::uint32_t elapsed_ms) {
    const auto& pick = picks[index];
    if (pick.frames == 0) return pick.tile;
    if (pick.frame != 0) return nullptr;
    const auto showing = std::uint8_t(std::uint64_t(elapsed_ms / 40) * 0x50 / 256 % pick.frames);
    if (showing == 0) return pick.tile;
    // The nth animated tile of its layer in the cell that has this frame owns the nth pick of it after them all.
    std::size_t nth = 0;
    for (std::size_t other = 0; other < index; ++other)
        if (picks[other].frames > showing && picks[other].frame == 0 && picks[other].layer == pick.layer) ++nth;
    for (const auto& other : picks)
        if (other.frames && other.layer == pick.layer && other.frame == showing && nth-- == 0) return other.tile;
    return pick.tile;
}

// A level's tile lookup and collision grid, once its ds1 and dt1s are in.
// The town start, as game.exe picks it on joining: DS1 special walls
// (orientation 10/11) with main index 30..33 become the level's spawn
// list (code at 0x667d09: main 30 sub n -> index n, 31 -> n+5,
// 32 -> 10, 33 -> 11 (town-portal arrival)); a join asks for index 0,
// which matches any of group 0 (indices 0..4) at random
// (FUN_0066ac40), at subtile tile*5+3 (FUN_0061b060), then the nearest
// free spot. The first match is that pick: each Act 1 town DS1 has
// exactly one (test_ds1).
void find_spawn_spots(Level& level) {
    const auto& map = level.ds1;
    auto spot = [&](std::size_t cell) { return std::pair{ (float(cell % std::size_t(map.width())) * 5 + 3 + 0.5f) / 5,
                                                          (float(cell / std::size_t(map.width())) * 5 + 3 + 0.5f) / 5 }; };
    for (const auto& layer : map.walls())
        for (std::size_t i = 0; i < layer.cells.size(); ++i) {
            const auto& tile = layer.cells[i];
            if (tile.wall_type != 10 && tile.wall_type != 11) continue;
            if (tile.style == 30 && tile.sequence <= 4 && level.start.first < 0) level.start = spot(i);
            if (tile.style == 33 && level.portal_spot.first < 0) level.portal_spot = spot(i);   // 33: where town portals open (index 11)
            const int index = tile.style == 30 ? tile.sequence : tile.style == 31 ? tile.sequence + 5 : tile.style == 32 ? 10 : tile.style == 33 ? 11 : -1;
            if (index >= 0 && index < 12 && level.spawn_at[std::size_t(index)].first < 0) level.spawn_at[std::size_t(index)] = spot(i);
        }
}

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
                    if (pick.layer == 1 || pick.layer == 2 || (pick.layer == 0 && pick.orient != 13 && pick.orient != 15)) stamp_tile(cell_x, cell_y, *pick.stamp);
                    for (int k = 0; k < 25 && pick.cell; ++k) level.walk[std::size_t(cell_y * 5 + k / 5) * std::size_t(walk_width) + std::size_t(cell_x * 5 + k % 5)] |= pick.cell;
                }
        for (const auto& patch : level.patches)
            for (int k = 0; k < 25; ++k) {
                auto& walk_cell = level.walk[std::size_t(patch.y * 5 + 4 - k / 5) * std::size_t(walk_width) + std::size_t(patch.x * 5 + k % 5)];
                if (patch.old_tile) walk_cell &= std::uint8_t(~patch.old_tile->subtile_flags[std::size_t(k)]);
                if (patch.tile) walk_cell |= patch.tile->subtile_flags[std::size_t(k)];
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
// Any other level next door (act 3's Travincal on the Causeway): its rooms
// up, laid as it laid them (its seeds, its order), in `level`'s tiles.
// ponytail: a room here re-picking a shared tile of theirs would roll
// nothing (game.exe: that room's DT1 list and seed); never seen (0 in Act 3
// x 10 seeds and Act 1 x 3, whole-game order). Their own neighbours aren't
// laid with them: only their far side's tiles, which aren't ours.
std::vector<d2d::drlg::BuiltRoom> camp_rooms(const GameData& game_data, const Spawning& spawning, const Level& level) {
    std::vector<d2d::drlg::BuiltRoom> rooms;
    for (const auto& beside : level.nearby) {
        const auto state = spawning.levels.find(beside.level);
        if (beside.level == &game_data.town || !beside.level->assets || state == spawning.levels.end() || state->second.order.empty()) continue;
        const Level& other = *beside.level;
        std::vector<std::size_t> came_up;
        for (const auto room : state->second.order) if (room < other.rooms.size() && !std::ranges::contains(came_up, room)) came_up.push_back(room);
        std::vector<d2d::drlg::Outdoor::RoomSeed> made;   // those, in list order
        for (std::size_t i = 0; i < other.rooms.size(); ++i) if (std::ranges::contains(came_up, i)) made.push_back(other.rooms[i]);
        std::vector<std::size_t> order;                   // list indices: the list is newest first
        for (const auto room : came_up) order.push_back(made.size() - 1 - std::size_t(std::ranges::count_if(came_up, [&](std::size_t earlier) { return earlier < room; })));
        std::vector<std::string> notes;
        const int base = int(rooms.size());               // each level's shares index its own build: re-based into `rooms`
        for (auto& room : d2d::drlg::level_room_tiles(made, other.plain, other.assets->data, other.tile_dt1s.heads, other.id, d2d::drlg::warp_slots(*other.assets, other.id), notes, order)) {
            for (auto& share : room.shares) share.owner += base;
            room.x += beside.dx, room.y += beside.dy;
            for (auto& tile : room.tiles) tile.x += beside.dx, tile.y += beside.dy;
            room.seed = nullptr;                          // `made` goes
            rooms.push_back(std::move(room));
        }
    }
    const auto& town = game_data.town;
    const auto next_door = std::ranges::find(level.nearby, &town, &Level::Near::level);
    const auto state = spawning.levels.find(&town);
    if (!level.assets || next_door == level.nearby.end() || state == spawning.levels.end() || state->second.order.empty()) return rooms;
    const auto rects = room_rects(town);
    std::vector<std::size_t> came_up;                         // room indices, as they came up
    for (const auto room : state->second.order) if (room < rects.size() && !std::ranges::contains(came_up, room)) came_up.push_back(room);
    std::vector<d2d::drlg::Outdoor::RoomSeed> made;      // those, in the order made
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (!std::ranges::contains(came_up, i)) continue;
        auto& room = made.emplace_back();
        room.x = rects[i].x - town.world_x, room.y = rects[i].y - town.world_y, room.width = rects[i].width, room.height = rects[i].height;
        room.kind = 2, room.def = 1, room.file = std::max(0, d2d::drlg::town_file(game_data.layouts.act1_layout)), room.rolled = true;
    }
    std::vector<std::size_t> order;                      // list indices: the list is newest first
    for (const auto room : came_up) order.push_back(made.size() - 1 - std::size_t(std::ranges::count_if(came_up, [&](std::size_t other) { return other < room; })));
    std::vector<std::string> notes;
    auto camp = d2d::drlg::level_room_tiles(made, {}, level.assets->data, {}, town.id, d2d::drlg::warp_slots(*level.assets, town.id), notes, order);
    const int camp_base = int(rooms.size());
    for (auto& room : camp) {
        for (auto& share : room.shares) share.owner += camp_base;
        room.x += next_door->dx, room.y += next_door->dy;
        for (auto& tile : room.tiles) tile.x += next_door->dx, tile.y += next_door->dy;
        room.seed = nullptr;                             // `made` goes
    }
    std::ranges::move(camp, std::back_inserter(rooms));
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
                                            std::vector<d2d::drlg::BuiltRoom> outside = {}, std::size_t up_count = SIZE_MAX) {
    const auto& assets = *level.assets;
    const auto& dt1s = level.tile_dt1s;
    const auto own = made.size();
    auto built = d2d::drlg::level_room_tiles(made, level.plain, assets.data, dt1s.heads, level.id, d2d::drlg::warp_slots(assets, level.id), notes, order, std::move(outside), up_count);
    const int width = level.ds1.width(), height = level.ds1.height();
    level.picks.assign(std::size_t(width) * std::size_t(height), {});
    level.room_tiles.assign(made.size(), {});
    auto step_of = [&](int room) { return int(built[std::size_t(room)].step); };
    auto tile_of = [&](const d2d::drlg::Dt1File* file, int index) -> const d2d::dt1::Tile* {
        const auto found = file ? dt1s.archive.find(file) : dt1s.archive.end();
        return found == dt1s.archive.end() || index < 0 || std::size_t(index) >= found->second->size() ? nullptr : &found->second->tiles()[std::size_t(index)];
    };
    auto holder = [&](std::size_t owner, int x, int y) {
        auto holds = [&](const d2d::drlg::BuiltRoom& candidate) { return x >= candidate.x && y >= candidate.y && x < candidate.x + candidate.width && y < candidate.y + candidate.height; };
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
        for (const auto& share : built[k].shares) shares.push_back({ step_of(int(k)), &share });
    std::ranges::stable_sort(shares, {}, &Share::step);
    level.patches.clear();
    for (const auto& [step, at] : shares) {
        const auto& tile = built[std::size_t(at->owner)].tiles[std::size_t(at->tile)];
        if (tile.x < 0 || tile.y < 0 || tile.x >= width || tile.y >= height) continue;   // a room next door's, off this level: its walk isn't ours (it wrote past level.walk)
        const int holding = holder(std::size_t(at->owner), tile.x, tile.y);
        if (holding >= 0 && step_of(holding) < step && (at->old_file != at->file || at->old_index != at->index))
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
            const int holding = holder(owner, tile.x, tile.y);
            const d2d::dt1::Tile* stamp = holding >= 0 && step_of(int(owner)) > step_of(holding) ? nullptr : drawn;
            std::uint8_t cell = std::uint8_t(cell_of(tile.word) | (tile.layer == 0 && tile.orient >= 8 && tile.orient <= 11 ? 0x10 : 0));   // a door or warp wall is flag 2 too
            for (const auto& [step, at] : shares) {
                if (std::size_t(at->owner) != owner || std::size_t(at->tile) != index) continue;
                if (holding < 0 || step <= step_of(holding)) cell |= cell_of(at->word);
                else if (stamp && (at->old_file != at->file || at->old_index != at->index)) { stamp = tile_of(at->old_file, at->old_index); break; }
            }
            auto& cell_picks = level.picks[std::size_t(tile.y) * std::size_t(width) + std::size_t(tile.x)];
            if (const auto& seed = built[owner].seed; owner < own && seed && tile.layer < 2)   // the room's floor / wall list (Level::room_tiles)
                level.room_tiles[std::size_t(seed - made.data())][tile.layer == 1 ? 0 : 1].push_back(
                    { std::uint32_t(tile.y * width + tile.x), std::uint16_t(cell_picks.size()) });
            cell_picks.push_back(
                { std::uint8_t(tile.layer), std::uint8_t(tile.orient), drawn,
                  tile.layer != 2 && (tile.word & 0x80000000u) != 0,
                  cell, stamp, Level::never_fades(tile.word, drawn->material_flags), tile.frame, tile.frames });
        }
    return built;
}

// A room's floor tiles in its list's order (room1 +8), the edge tiles it
// shares only when it came up before the room next to it.
std::vector<Level::Floor> floors_of(const d2d::drlg::BuiltRoom& room) {
    std::vector<Level::Floor> floors;
    for (const auto& tile : room.tiles)
        if (tile.layer == 1 && tile.file && tile.index >= 0 && std::size_t(tile.index) < tile.file->tiles.size())
            floors.push_back({ tile.x, tile.y, std::uint16_t(tile.file->tiles[std::size_t(tile.index)].material) });
    return floors;
}

// Its spawn areas (FUN_0061ad50's list): a Logicals room's, else the whole room.
std::vector<d2d::drlg::Area> areas_of(const d2d::drlg::BuiltRoom& room) {
    return room.areas.empty() ? std::vector<d2d::drlg::Area>{ { room.x, room.y, room.x + room.width, room.y + room.height, 1, false } } : room.areas;
}

}  // namespace

// Its rooms brought up again, `came_up` (Level::rooms indices) first in that
// order, the rest after in list order: picks, patches, the walk grid and
// the room1 seeds as game.exe's rooms would have them, footprints stamped
// again. game.exe's units and warps come out the same in any order
// (tools/emu diff_drlg.py <level> units, rooms in $ORDER), its spawn areas
// too (diff_drlg.py game: the game seed through the monsters); the object
// groups follow as the rooms populate (room_objects, on these seeds).
// The whole level is laid again (Debug ~15 ms for the Stony Field) each
// time rooms come up, where game.exe lays only the new ones: the same
// result (rooms laid later never change earlier ones), only slower; not a
// fidelity gap. Lay one at a time (level_room_tiles' state kept) if it hitches.
void relevel(Level& level, const std::vector<std::size_t>& came_up, const GameData* game_data, const Spawning* spawning) {
    if (!level.assets) return;
    auto outside = game_data && spawning ? camp_rooms(*game_data, *spawning, level) : std::vector<d2d::drlg::BuiltRoom>{};
    const std::size_t count = level.rooms.size();
    std::vector<std::size_t> order;                      // list indices: the list is newest first
    std::vector<bool> taken(count);
    for (const auto room : came_up) if (room < count && !taken[count - 1 - room]) { order.push_back(count - 1 - room); taken[count - 1 - room] = true; }
    const std::size_t up_count = order.size();                 // the rest are laid for their tiles; they merge no areas (merge_areas)
    for (std::size_t i = 0; i < count; ++i) if (!taken[i]) order.push_back(i);
    // The client's (Level::drawn): the same, each batch in made order, list indices; empty when it's the server's.
    std::vector<std::size_t> client;
    if (spawning)
        if (const auto state = spawning->levels.find(&level); state != spawning->levels.end() && !state->second.client_order.empty()) {
            std::vector<bool> client_taken(count);
            for (const auto room : state->second.client_order)
                if (room < count && !client_taken[count - 1 - room]) { client.push_back(count - 1 - room); client_taken[count - 1 - room] = true; }
            for (std::size_t i = 0; i < count; ++i) if (!client_taken[i]) client.push_back(i);
            if (client == order) client.clear();
        }
    if (outside.empty() && up_count == level.laid_up && client == level.drawn_laid
        && (order == level.laid || (level.laid.empty() && std::ranges::is_sorted(order) && order.size() == count))) return;
    const auto start_ms = d2d::log::ms();
    std::vector<std::string> notes;
    auto client_outside = client.empty() ? std::vector<d2d::drlg::BuiltRoom>{} : outside;   // the next door's rooms don't hang on this level's lay
    const auto built = lay_tiles(level, level.rooms, order, notes, std::move(outside), up_count);
    for (const auto& room : built)
        if (room.seed) {
            level.room1_seeds[std::size_t(room.seed - level.rooms.data())] = room.room1_seed;
            level.room_floors[std::size_t(room.seed - level.rooms.data())] = floors_of(room);   // FUN_005b2700's water spots
            level.room_areas[std::size_t(room.seed - level.rooms.data())] = areas_of(room);     // a Logicals room's walls take the near rooms up
        }
    // The units again: a door on a shared edge (FUN_0066d9e0) is the room's
    // that came up to it first, and its rand(3) that room's seed.
    level.units.clear();
    level.unit_rooms.clear();
    for (const auto& room : built)
        for (const auto& unit : room.units) {
            level.units.push_back({ unit.type, unit.id, unit.mode, unit.x + room.x * 5, unit.y + room.y * 5, unit.flags, unit.path, unit.actions });
            level.unit_rooms.push_back(room.seed ? int(room.seed - level.rooms.data()) : -1);
        }
    finish_level(level);
    level.drawn.clear();
    if (!client.empty()) {                               // the client's picks, collision's untouched
        auto server_picks = std::move(level.picks);
        auto server_patches = std::move(level.patches);
        std::vector<std::string> client_notes;
        lay_tiles(level, level.rooms, client, client_notes, std::move(client_outside), up_count);
        level.drawn = std::exchange(level.picks, std::move(server_picks));
        level.patches = std::move(server_patches);
    }
    level.drawn_laid = std::move(client);
    level.laid = std::move(order);
    ++level.lays;
    level.laid_up = up_count;
    stamp_footprints(level);
    d2d::log::info("  {}: rooms laid again ({} ms)", level.name, d2d::log::ms() - start_ms);
}

// A preset level's (no generated rooms: the towns) rooms laid as game.exe
// splits it (FUN_00666680, 8x8, each at the list's head) and walks each
// (FUN_00666ac0: its (w+1)x(h+1) slice of every floor layer, then every
// wall layer, then the shadow): its 8x8 rooms in split order, with their
// tiles, each picked from GameData::prest_automap_dt1s (none: no picks);
// `assets`: any generated level's (the presets' rows and DS1s).

// The camp's split (room_rects, made order) laid as camp_rooms lays it for
// the level next door, here in the preset level's own tiles.
// ponytail: no DT1s or tile seeds (as camp_rooms): every word the walk
// takes is listed, where game.exe lists only those it finds a tile for.
std::vector<d2d::drlg::BuiltRoom> preset_rooms(const GameData& game_data, const Level& level, const d2d::drlg::OutdoorAssets& assets) {
    namespace ids = d2d::rules::level_ids;
    if (!level.rooms.empty()) return {};
    int def = 0;
    for (const auto& [preset_def, preset] : assets.data.presets)
        if (preset.level_id == level.id && (def == 0 || preset_def < def)) def = preset_def;
    if (def == 0) return {};
    const int file = level.id == ids::kRogueEncampment ? std::max(0, d2d::drlg::town_file(game_data.layouts.act1_layout))
                   : level.id == ids::kLutGholein      ? d2d::drlg::lut_gholein_file(game_data.layouts.act2_layout) : 0;
    std::vector<d2d::drlg::Outdoor::RoomSeed> made;
    for (const auto& rect : room_rects(level)) {
        auto& room = made.emplace_back();
        room.x = rect.x - level.world_x, room.y = rect.y - level.world_y, room.width = rect.width, room.height = rect.height;
        room.kind = 2, room.def = def, room.file = file, room.rolled = true;
    }
    std::vector<std::string> notes;
    const auto dt1s = game_data.layouts.prest_automap_dt1s.find(level.id);
    auto rooms = d2d::drlg::level_room_tiles(made, {}, assets.data, dt1s == game_data.layouts.prest_automap_dt1s.end() ? d2d::drlg::RoomDt1s{} : dt1s->second,
                                             level.id, d2d::drlg::warp_slots(assets, level.id), notes);
    std::ranges::reverse(rooms);                          // the list is newest first: split order
    for (auto& room : rooms) room.seed = nullptr;        // `made` goes
    return rooms;
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
    level.room_floors.assign(made.size(), {});
    for (const auto& room : built)
        if (room.seed) {
            level.room_floors[std::size_t(room.seed - made.data())] = floors_of(room);
            level.room1_seeds[std::size_t(room.seed - made.data())] = room.room1_seed;
            level.room_areas[std::size_t(room.seed - made.data())] = areas_of(room);
            const auto preset = room.seed->kind == 2 ? assets.data.presets.find(room.seed->def) : assets.data.presets.end();
            level.nopop_rooms[std::size_t(room.seed - made.data())] = !room.warps.empty() || (preset != assets.data.presets.end() && !preset->second.populate);
        }
    for (const auto& room : built)                      // a town's spawn tiles (find_spawn_spots' rules)
        for (const auto& spawn : room.spawns) {
            const std::pair spot{ (float(spawn.x) * 5 + 3 + 0.5f) / 5, (float(spawn.y) * 5 + 3 + 0.5f) / 5 };
            if (spawn.index <= 4 && level.start.first < 0) level.start = spot;
            if (spawn.index == 11 && level.portal_spot.first < 0) level.portal_spot = spot;
            if (spawn.index < 12 && level.spawn_at[std::size_t(spawn.index)].first < 0) level.spawn_at[std::size_t(spawn.index)] = spot;
        }
    // FUN_0066ac40 with no tile of the start's group (0..4) counts none and
    // stops at the list's first tile: the Pandemonium Fortress starts at its
    // town-portal tile (style 33), its only one; Harrogath's town portals
    // (index 11, FUN_0056cf40) open at its start tile, its only one.
    for (const auto& room : built)
        for (const auto& spawn : room.spawns) {
            const std::pair spot{ (float(spawn.x) * 5 + 3 + 0.5f) / 5, (float(spawn.y) * 5 + 3 + 0.5f) / 5 };
            if (level.start.first < 0) level.start = spot;
            if (level.portal_spot.first < 0) level.portal_spot = spot;
        }
    for (const auto& room : built)
        for (const auto& unit : room.units) {
            level.units.push_back({ unit.type, unit.id, unit.mode, unit.x + room.x * 5, unit.y + room.y * 5, unit.flags, unit.path, unit.actions });
            level.unit_rooms.push_back(room.seed ? int(room.seed - made.data()) : -1);
        }
    // Warps: the slot's Levels.txt Vis / Warp, LvlWarp's ExitWalk, and the
    // tile unit's spot (FUN_0066e1c0: the slot's LvlWarp Offset).
    const auto slots = d2d::drlg::warp_slots(assets, level.id);
    // 0x800000: a preset room its LvlPrest row doesn't populate (LAB_006666ec),
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
    // FUN_0066e260) leaves its unit; its slot is the wall's style
    // (BuiltRoom::wall_warps), one warp a slot.
    std::vector<d2d::drlg::BuiltRoom::Warp> found;
    for (const auto& room : built)
        found.insert(found.end(), room.warps.begin(), room.warps.end());
    for (const auto& room : built)
        for (const auto& wall : room.wall_warps)
            if (!std::ranges::any_of(found, [&](const auto& warp) { return warp.slot == wall.slot; })) found.push_back(wall);
    if (const auto row = d2d::drlg::level_row(assets.levels, level.id))
        for (const auto& warp : found) {
            if (warp.slot < 0 || warp.slot > 7) continue;
            const int destination = d2d::drlg::to_int(assets.levels.get(*row, "Vis" + std::to_string(warp.slot)));
            const int wid = d2d::drlg::to_int(assets.levels.get(*row, "Warp" + std::to_string(warp.slot)), -1);
            if (destination <= 0 || wid < 0) continue;
            float exit_x = 0, exit_y = 0;
            std::array<int, 4> select{};
            for (std::size_t k = 0; k < assets.lvl_warp.size(); ++k)
                if (d2d::drlg::to_int(assets.lvl_warp.get(k, "Id"), -1) == wid) {
                    exit_x = float(d2d::drlg::to_int(assets.lvl_warp.get(k, "ExitWalkX"))) / 5;
                    exit_y = float(d2d::drlg::to_int(assets.lvl_warp.get(k, "ExitWalkY"))) / 5;
                    for (std::size_t edge = 0; const char* column : { "SelectX", "SelectY", "SelectDX", "SelectDY" }) select[edge++] = d2d::drlg::to_int(assets.lvl_warp.get(k, column));
                    break;
                }
            const auto& slot = slots[std::size_t(warp.slot)];
            int pair = 0;
            for (int k = 0; k < warp.slot; ++k) pair += d2d::drlg::to_int(assets.levels.get(*row, "Vis" + std::to_string(k))) == destination;
            level.warps.push_back({ float(warp.x), float(warp.y), destination, exit_x, exit_y,
                                    (float(warp.x * 5 + slot.off_x) + 0.5f) / 5, (float(warp.y * 5 + slot.off_y) + 0.5f) / 5, warp.slot, pair, select });
        }
    // 0x800000 also: a room whose warp leads into a town, the town's room on
    // its near list (FUN_0066bd50, FUN_006426a0) — the Sewers' ways up.
    for (const auto& warp : level.warps)
        if (d2d::rules::level_ids::is_town(warp.destination))
            for (std::size_t i = 0; i < made.size(); ++i)
                if (warp.x >= float(made[i].x) && warp.x < float(made[i].x + made[i].width) && warp.y >= float(made[i].y) && warp.y < float(made[i].y + made[i].height))
                    level.room_flags[i] |= 0x800000;
    finish_level(level);
    return placed;
}

// An outdoor level of the act (the Blood Moor): drlg generate_outdoor
// where the act's layout put it, on its level seed.
bool build_outdoor(const GameData& game_data, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, Level& level) {
    const auto outdoor_level = d2d::rules::level_ids::act_of(level.id) == 2 ? d2d::drlg::act3_outdoor_level(assets.levels, game_data.layouts.act3_layout, level.id)
                                                                            : d2d::drlg::outdoor_level(assets.levels, game_data.layouts.layout_of(level.id), level.id);
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
            for (std::size_t k = 0; k < 3; ++k) maze.rooms[k] = d2d::drlg::to_int(assets.lvl_maze.get(maze_row, std::array{ "Rooms", "Rooms(N)", "Rooms(H)" }[k]));
            maze.width = d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "SizeX"));
            maze.height = d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "SizeY"));
            maze.merge = d2d::drlg::to_int(assets.lvl_maze.get(maze_row, "Merge"));
        }
    maze.type = d2d::drlg::to_int(assets.levels.get(row, "LevelType"));
    { auto act = d2d::drlg::act_seed(game_data.map_seed); const auto tombs = d2d::drlg::roll_act2_tombs(act); maze.boss_tomb = tombs.boss; maze.staff_tomb = tombs.staff; }
    const bool preset_level = d2d::drlg::to_int(assets.levels.get(row, "DrlgType")) == 2;
    if (maze.width == 0 && !preset_level) { d2d::log::warn("{}: no LvlMaze row", level.name); return false; }
    std::vector<std::string> notes;
    const int size_x = d2d::drlg::to_int(assets.levels.get(row, "SizeX")), size_y = d2d::drlg::to_int(assets.levels.get(row, "SizeY"));
    const auto seed = d2d::drlg::level_seed(game_data.map_seed, level.id);
    int court_file = level.id == d2d::rules::level_ids::kOuterCloister || level.id == d2d::rules::level_ids::kBarracks ? d2d::drlg::courtyard_file(game_data.layouts.act1_layout, d2d::drlg::act_seed(game_data.map_seed)) : -1;
    if (level.id == d2d::rules::level_ids::kLutGholein) court_file = d2d::drlg::lut_gholein_file(game_data.layouts.act2_layout);   // LutW / LutN by the Rocky Waste's side
    const auto [origin_x, origin_y] = d2d::drlg::level_origin(assets.levels, row);
    std::array<int, 4> rect{ origin_x, origin_y, size_x, size_y };
    if (const auto court = d2d::drlg::level_row(assets.levels, d2d::rules::level_ids::kOuterCloister); level.id == d2d::rules::level_ids::kBarracks && court) {   // the Barracks sits beside the Outer Cloister
        auto court_seed = d2d::drlg::level_seed(game_data.map_seed, 27);
        court_file = d2d::drlg::preset_file(assets.data, 27, court_seed, court_file);
        const auto [court_x, court_y] = d2d::drlg::level_origin(assets.levels, *court);
        rect = { court_x, court_y, d2d::drlg::to_int(assets.levels.get(*court, "SizeX")), d2d::drlg::to_int(assets.levels.get(*court, "SizeY")) };
    }
    if (level.id == d2d::rules::level_ids::kRiverOfFlame)   // FUN_00673320 hangs it under the Chaos Sanctuary
        for (const auto& placement : game_data.layouts.act4_layout)
            if (placement.level == d2d::rules::level_ids::kChaosSanctuary) rect = { placement.x, placement.y, placement.width, placement.height };
    auto made = preset_level ? d2d::drlg::generate_preset(assets.data, level.id, size_x, size_y, seed, notes, court_file)
                             : d2d::drlg::generate_maze(assets.data, maze, level.id, size_x, size_y, seed, game_data.difficulty, notes, court_file, &rect);
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
std::optional<std::vector<d2d::drlg::Outdoor::RoomSeed>> camp_room_seeds(const GameData& game_data) {
    if (!game_data.builder || !game_data.builder->act1) return std::vector<d2d::drlg::Outdoor::RoomSeed>{};
    std::unique_lock lock(game_data.builder->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return std::nullopt;
    const auto& assets = *game_data.builder->act1;
    const auto row = d2d::drlg::level_row(assets.levels, d2d::rules::level_ids::kRogueEncampment);
    if (!row) return std::vector<d2d::drlg::Outdoor::RoomSeed>{};
    std::vector<std::string> notes;
    return d2d::drlg::generate_preset(assets.data, d2d::rules::level_ids::kRogueEncampment, d2d::drlg::to_int(assets.levels.get(*row, "SizeX")),
                                      d2d::drlg::to_int(assets.levels.get(*row, "SizeY")), d2d::drlg::level_seed(game_data.map_seed, d2d::rules::level_ids::kRogueEncampment), notes);
}

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
    // Its palette is its Pal's (Levels.bin +2: the loader's field table at 0x61c587), not its act's (+3):
    // on a level change the client compares the two levels' Pal (0x465622) and loads palette\act{Pal+1}
    // (FUN_004fb480). Abaddon, the Pit of Acheron and the Infernal Pit (125-127) have Act 4's (Pal 3).
    level->pal = d2d::drlg::to_int(assets.levels.get(*row, "Pal"));
    // A preset the layout places (the Monastery Gate, DrlgType 2) builds as a preset: its rooms (64x18: an 8x2 row) are the DS1's.
    const bool outdoor = d2d::drlg::to_int(assets.levels.get(*row, "DrlgType")) == 3 &&
                         std::ranges::any_of(game_data.layouts.layout_of(id), [&](const auto& placement) { return placement.level == id; });
    if (!(outdoor ? build_outdoor(game_data, *builder.mpqs, assets, *level) : build_maze(game_data, *builder.mpqs, assets, *level, *row))) return nullptr;
    auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    for (std::size_t row_index = 0; row_index < builder.levels.size(); ++row_index) {
        if (num(builder.levels.get(row_index, "Id")) != id) continue;
        auto text = [&](const std::string& column) { return builder.levels.get(row_index, column); };
        level->layer = num(text("Layer"));
        level->light = level_light(text("Intensity"), text("Red"), text("Green"), text("Blue"));
        level->rain = text("Rain") == "1";
        level->mud = text("Mud") == "1";
        level->teleport = d2d::drlg::to_int(text("Teleport"), 1);
        level->critters = level_critters(game_data.monsters, text);
        for (std::size_t env_row = 0; env_row < builder.sound_env.size(); ++env_row)
            if (builder.sound_env.get(env_row, "Index") == text("SoundEnv")) {
                level->song = num(builder.sound_env.get(env_row, "Song"));
                level->material = num(builder.sound_env.get(env_row, "Material 1"));
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
    // Blood Moor's way in); the rooms' flags now.
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
    // Lut Gholein's palace (rules::palace_spots, a2q4.cpp): Jerhyn by object
    // 121, Jerhyn in the palace and the gate guard by object 122, each made
    // here and shown by the World as the quests stand (World::show_palace).
    // Jerhyn's spots are FUN_00545340's search (Level::free_area) round them:
    // size 2 by object 121 (FUN_0059f380), size 3 in the palace (FUN_0059ef70),
    // mask 0x100, 100 rings; FUN_005b2f20 then makes him on that subtile
    // (mode 1, radius -1); no spot found keeps the one asked.
    // The guard (object 122, FUN_0059f440): FUN_005b3090(x + 1, y, 0x14b, 1, 0)
    // -> FUN_005b2a00 (spawn_spot, radius -1: the spot alone). A miss keeps
    // the one asked.
    // ponytail: that search runs as the room comes up and steps the room seed
    // after room_objects' first step. Here it tries the room1 seed while the
    // level is built, with no crowd.
    for (const auto& unit : level->units) {
        if (id != d2d::rules::level_ids::kLutGholein || unit.type != d2d::rules::unit_type::kObject || (unit.id != 121 && unit.id != 122)) continue;
        auto add = [&](int hc_idx, int at_x, int at_y, int palace, int size) {
            if (std::size_t(hc_idx) >= game_data.mon_bin.size()) return;
            auto npc = game_data.mon_npc[game_data.mon_bin[std::size_t(hc_idx)]];
            if (npc.code.empty()) return;
            if (size > 0) {
                if (const auto spot = level->free_area(at_x, at_y, size, 0x100, 100)) std::tie(at_x, at_y) = *spot;
                npc.x = (float(at_x) + 0.5f) / 5;
                npc.y = (float(at_y) + 0.5f) / 5;
            } else {
                const int guard_row = int(game_data.mon_bin[std::size_t(hc_idx)]);
                const int guard_room = room_of(*level, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5);
                if (guard_room >= 0 && std::size_t(guard_room) < level->rooms.size()) {
                    Spawning::LevelState rooms_in_play;
                    rooms_in_play.rooms_up.assign(level->rooms.size(), false);
                    rooms_in_play.rooms_up[std::size_t(guard_room)] = true;
                    rooms_in_play.room_seeds.assign(level->rooms.size(), d2d::rules::Rng{});
                    const std::uint32_t room_seed = std::size_t(guard_room) < level->room1_seeds.size() ? level->room1_seeds[std::size_t(guard_room)]
                                                                                                        : level->rooms[std::size_t(guard_room)].seed;
                    rooms_in_play.room_seeds[std::size_t(guard_room)] = d2d::rules::Rng{ room_seed };
                    const Crowd crowd;
                    int spot_x = at_x, spot_y = at_y;
                    if (spawn_spot(game_data, *level, crowd, &rooms_in_play, guard_row, at_x, at_y, -1, guard_room, spot_x, spot_y)) {
                        at_x = spot_x;
                        at_y = spot_y;
                    }
                }
                npc.x = (float(at_x) + 0.5f) / 5;
                npc.y = (float(at_y) + 0.5f) / 5;
            }
            npc.palace = palace;
            level->npcs.push_back(std::move(npc));
        };
        if (unit.id == 121) add(d2d::rules::monster_ids::kJerhyn, unit.x, unit.y, 1, 2);
        else {
            add(d2d::rules::monster_ids::kJerhyn, unit.x - 10, unit.y - 3, 2, 3);
            add(d2d::rules::monster_ids::kJerhyn, unit.x + 15, unit.y - 3, 3, 3);
            add(d2d::rules::monster_ids::kPalaceGuard, unit.x + 1, unit.y, 4, 0);
        }
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

// A client unit's first seed (FUN_00465fd0 via FUN_00466730, 0x46606a..0x466093): {1, 666}, then, with
// a room at its spot, that room's seed (+0x6c / +0x70) stepped once (FUN_00650e40) and its low word
// as {low, 666}. `room` is a Level::rooms index, -1 none.

// The client builds its rooms with the same DRLG code as the server, so each client room seed
// starts at that room's room1 seed (taken so, not compared on a client). The unit order is the
// level's object order, not the client's packet order.
d2d::rules::Rng client_unit_seed(const Level& level, int room) {
    if (room < 0 || std::size_t(room) >= level.rooms.size()) return d2d::rules::Rng{ 1 };
    const auto start_of = [&](std::size_t index) { return index < level.room1_seeds.size() ? level.room1_seeds[index] : level.rooms[index].seed; };
    if (level.client_rooms.size() != level.rooms.size()) {
        level.client_rooms.clear();
        level.client_room_starts.clear();
        for (std::size_t index = 0; index < level.rooms.size(); ++index) {
            level.client_rooms.emplace_back(start_of(index));
            level.client_room_starts.push_back(start_of(index));
        }
    }
    const auto index = std::size_t(room);
    if (level.client_room_starts[index] != start_of(index)) {   // relevel laid it anew: a new room1, a new stream
        level.client_rooms[index] = d2d::rules::Rng{ start_of(index) };
        level.client_room_starts[index] = start_of(index);
    }
    return d2d::rules::Rng{ level.client_rooms[index].next() };
}

// Each object of the level without one its client seed (its room, else the room at its spot), in the
// level's object order: as the level is built, then as each room's objects are made. A level with no
// room list (a camp) leaves them be: object_frame_delta seeds each from its spot.
void seed_client_units(Level& level) {
    // A camp keeps no room list (its rooms are the DS1's, no room1 seed d2d knows): its objects keep
    // their spot as the seed (object_frame_delta), not one {1, 666} for all.
    if (level.rooms.empty()) return;
    for (auto& npc : level.npcs) {
        if (npc.root != "objects" || npc.client_seeded) continue;
        npc.client_seed = client_unit_seed(level, npc.room >= 0 ? npc.room : room_of(level, npc.x, npc.y));
        npc.client_seeded = true;
    }
}

// A finished build into GameData::levels (nullptr: tried, not built); an act
// level is linked with the act levels already there, both ways, and with the
// levels it walks into.
void install_level(const GameData& game_data, int id, std::unique_ptr<Level> level) {
    auto in_act = [&](int level_id) { return std::ranges::contains(game_data.layouts.layout_of(id), level_id, &d2d::drlg::Placed::level); };
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
    if (level) seed_client_units(*level);
    if (level && level->assets) std::tie(level->vis, level->vis_warp) = d2d::drlg::level_vis(level->assets->levels, game_data.layouts.layout_of(id), id);
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
    const auto& layout = game_data.layouts.layout_of(level.id);
    const auto placement = std::ranges::find(layout, level.id, &d2d::drlg::Placed::level);
    if (placement != layout.end())
        for (const auto& other_placement : layout)
            if (other_placement.level != level.id && other_placement.x <= placement->x + placement->width && placement->x <= other_placement.x + other_placement.width && other_placement.y <= placement->y + placement->height && placement->y <= other_placement.y + other_placement.height)
                game_data.want_level(other_placement.level);
    for (const auto& warp : level.warps) game_data.want_level(warp.destination);
    for (const int walk : walk_links(game_data, level.id)) game_data.want_level(walk);
}


// D2 TBL values are UTF-16; our font is Latin-1. Downcast char by char.
std::string u16_to_latin1(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char16_t code_unit : text) {
        // Keep printable Latin-1 (0x20..0xFF) and line breaks (two-line
        // labels like "Fire\nResistance"), drop the rest — D2 UI strings
        // are ASCII with occasional accented chars, all inside Latin-1.
        if ((code_unit >= 0x20 && code_unit <= 0xFF) || code_unit == '\n') out.push_back(char(code_unit));
    }
    return out;
}

int direction16(float dx, float dy) {
    const auto& kFromSector = kDir16FromSector;
    const float screen_x = (dx - dy) * (kIsoW / 2), screen_y = (dx + dy) * (kIsoH / 2);
    const float angle = std::atan2(-screen_x, screen_y);                    // 0 = down, + = clockwise
    const int sector = int(std::lround(angle / (2 * 3.14159265f / 16)));
    return kFromSector[std::size_t((sector % 16 + 16) % 16)];
}

// A composite's direction for a 16-direction facing: 0..7 are the eight
// compass points, 8..15 the ones between; an 8-direction composite (town
// NPCs, mercs) takes the neighbouring point for those — clamping them
// made NPCs walk backwards. The neighbour clockwise, as game.exe's 64-step
// table rounds a unit's direction too (FUN_005ff760 -> FUN_00600c90, the
// same as missile_direction: test_game_rules).
std::uint8_t cof_direction(int dir16, int dirs) {
    constexpr int k16to8[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 5, 1, 6, 2, 7, 3, 4 };
    const int dir = dirs == 8 && dir16 >= 0 && dir16 < 16 ? k16to8[dir16] : dir16;
    return std::uint8_t(std::clamp(dir, 0, std::max(dirs - 1, 0)));
}

// A missile's frame in a sprite of `dirs` directions for its 64-step
// facing `dir64` (FUN_00600c90: the table at 0x6e45a0, [log2(dirs) + 1]
// [dir64]; fireball.dcc and firebolt.dcc have 16). The table rounds the
// 64 steps to the nearest of the sprite's own, clockwise from straight
// down, in D2's DCC order for that count (8: S SW W NW N NE E SE; 4 by
// angle; 2's row is all 0). Checked against it for every step and for 1,
// 2, 4, 8, 16 and 32 directions (tools/emu/missile_dirs.py table).
std::uint8_t missile_direction(int dir64, int dirs) {
    if (dirs <= 2) return 0;
    const int step = 64 / std::min(dirs, 64);
    const int sector = ((dir64 & 0x3f) + step / 2) / step % dirs;
    if (dirs == 32) return std::uint8_t(kDir32FromSector[std::size_t(sector)]);
    if (dirs == 16) return std::uint8_t(kDir16FromSector[std::size_t(sector)]);
    if (dirs == 8) return std::uint8_t(kDir16FromSector[std::size_t(sector * 2)]);
    return std::uint8_t(sector);
}

// Loading and building (gamedata.cpp).
// Translate a DS1-embedded tileset path (e.g.
// "\d2\data\global\tiles\act1\town\floor.dt1") into the MPQ path we can hand to Stack::try_read. The
// DS1 files store paths as they were on Blizzard's build box, with a
// leading "\d2\" prefix and forward slashes never — normalize both.
[[nodiscard]] std::string ds1_path_to_mpq(std::string_view path) {
    if (path.size() > 4 && (path.starts_with("\\d2\\") || path.starts_with("/d2/")))
        path.remove_prefix(4);
    else if (!path.empty() && (path[0] == '\\' || path[0] == '/'))
        path.remove_prefix(1);
    std::string out(path);
    for (auto& letter : out) if (letter == '/') letter = '\\';
    return out;
}

// Encode (style, sequence, type) into a single lookup key. Style + sequence
// are DS1-record bytes; type is the DT1 orientation code (0..16 per D2's
// tile-type table). 24 bits × 24 bits × 16 bits comfortably fits u64.
[[nodiscard]] std::uint64_t tile_key(int style, int seq, int type) {
    return (std::uint64_t(std::uint32_t(style)) << 40)
         | (std::uint64_t(std::uint32_t(seq  )) << 16)
         |  std::uint64_t(std::uint16_t(type ));
}

// The weapon class in a monster's COF name (FUN_0064f5b0, its monster case):
// death (DT) and dead (DD) are hth unless MonStats2 compositeDeath
// (FUN_004638a0 bit 0x10, the MonStats2 loader names it at 0x658197); other modes BaseW.
std::string_view cof_weapon_class(const Npc& npc, std::string_view mode) {
    if (npc.root == "monsters" && !npc.composite_death && (mode == "DT" || mode == "DD")) return "hth";
    return npc.base_w;
}

// One FUN_0061a790 age step as FUN_0052d240 uses it. `rooms_up` deliberately is
// not cleared: room2 keeps the populated bit while room1 is absent.
bool age_live_room(Spawning::LevelState& state, std::size_t room, bool held) {
    if (room >= state.live.size() || !state.live[room]) return false;
    if (state.idle_sweeps.size() != state.live.size()) state.idle_sweeps.resize(state.live.size());
    if (held) { state.idle_sweeps[room] = 0; return false; }
    if (state.idle_sweeps[room] < UINT8_MAX) ++state.idle_sweeps[room];
    if (state.idle_sweeps[room] <= 10) return false;
    state.live[room] = false;
    std::erase(state.order, room);
    std::erase(state.client_order, room);
    return true;
}

// A missile row's own elemental damage in 256ths (FUN_0064b100 /
// FUN_0064b1d0): (EMin + MinELev brackets) plus its EDmgSymPerCalc % of
// that (FUN_00483360), then << HitShift.
int missile_elem(const GameData& game_data, const GameData::MissileInfo& missile, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool max) {
    int damage = (max ? missile.emax : missile.emin) + d2d::rules::level_bonus(max ? missile.emax_lev : missile.emin_lev, lvl);
    if (const int bonus = missile.edmg_sym.empty() ? 0 : d2d::rules::eval_calc(game_data.skills, missile.edmg_sym, env, skill_id, lvl); bonus != 0)
        damage += int(std::int64_t(damage) * bonus / 100);
    return damage << (missile.hitshift & 31);
}

// Its physical part in 256ths (FUN_0064af20 / FUN_0064aff0): (MinDamage /
// MaxDamage, +0xb0 / +0xb4, + MinLevDam / MaxLevDam brackets) plus its
// DmgSymPerCalc (+0xe0) % of that, then << HitShift (+0x196).
int missile_phys(const GameData& game_data, const GameData::MissileInfo& missile, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool max) {
    int damage = (max ? missile.max : missile.min) + d2d::rules::level_bonus(max ? missile.max_lev : missile.min_lev, lvl);
    if (const int bonus = missile.dmg_sym.empty() ? 0 : d2d::rules::eval_calc(game_data.skills, missile.dmg_sym, env, skill_id, lvl); bonus != 0)
        damage += int(std::int64_t(damage) * bonus / 100);
    return damage << (missile.hitshift & 31);
}

// The rate object_frame_delta(npc, mode) would give, without drawing: the
// cached one while the unit is in that mode, else the roll its seed makes
// next. A timing query (object_op_ticks) uses it so asking about a mode the
// unit is not in neither changes the animation cache nor steps the seed.
int object_frame_delta_peek(const Npc& npc, std::size_t mode) {
    if (npc.sync) return npc.frame_delta[mode];
    if (npc.client_seeded && npc.client_mode == int(mode)) return npc.client_frame_delta;
    auto seed = npc.client_seeded ? npc.client_seed : d2d::rules::Rng{ std::uint32_t(npc.x * 5) << 16 | std::uint32_t(npc.y * 5) };
    const int delta = npc.frame_delta[mode];
    return std::clamp(seed(delta >> 3) + delta - (delta >> 4), 0, 0x7fff);
}

// The game's object seed: {the game seed's second step, 666}
// (FUN_00546c60, objrgn.cpp; the first step made the monster regions).
d2d::rules::Rng object_seed(std::uint32_t map_seed) {
    d2d::rules::Rng game{ map_seed };
    game.next();
    return d2d::rules::Rng{ game.next() };
}

}  // namespace d2d::game
