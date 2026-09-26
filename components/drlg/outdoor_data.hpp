// Loading what the outdoor generator needs from the game's tables and
// DS1s, through a `read(path) -> optional<vector<byte>>` callable (the app
// and the tests hand it their MPQ stack).
#pragma once

#include <drlg.hpp>
#include <outdoor.hpp>
#include <room_tiles.hpp>
#include <txt.hpp>

#include <charconv>
#include <deque>
#include <optional>
#include <string>

namespace d2d::drlg {

inline int to_int(std::string_view s, int fallback = 0) {
    int v = fallback;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

// The tables plus every DS1 the act 1 wilderness can place or stamp.
// Presets point into `maps`, so fill one in place and don't copy it.
struct OutdoorAssets {
    d2d::txt::Table levels, lvl_types, lvl_warp, lvl_maze;
    std::deque<Dt1File> dt1s;                           // loaded by load_room_dt1s
    std::deque<d2d::ds1::Map> maps;
    OutdoorData data;
    OutdoorAssets() = default;
    OutdoorAssets(const OutdoorAssets&) = delete;
    OutdoorAssets& operator=(const OutdoorAssets&) = delete;
};

template <class Read> void load_outdoor_assets(OutdoorAssets& a, Read&& read) {
    auto table = [&](const char* name) {
        const auto b = read(std::string(R"(data\global\excel\)") + name);
        return b ? d2d::txt::Table(*b) : d2d::txt::Table{};
    };
    auto ds1 = [&](std::string_view rel) -> const d2d::ds1::Map* {
        if (rel.empty() || rel == "0") return nullptr;
        std::string p = R"(data\global\tiles\)";
        for (const char c : rel) p.push_back(c == '/' ? '\\' : c);
        const auto b = read(p);
        if (!b) return nullptr;
        try { return &a.maps.emplace_back(*b); } catch (...) { return nullptr; }
    };
    a.levels = table("Levels.txt");
    a.lvl_types = table("LvlTypes.txt");
    a.lvl_warp = table("LvlWarp.txt");
    a.lvl_maze = table("LvlMaze.txt");
    // Unit ids (FUN_00665950): monpreset.bin is {int count; {byte act, byte kind, u16 id}[count]}.
    if (const auto b = read(R"(data\global\excel\monpreset.bin)"); b && b->size() >= 4) {
        auto u8 = [&](std::size_t o) { return int(std::to_integer<unsigned>((*b)[o])); };
        const int n = u8(0) | u8(1) << 8 | u8(2) << 16 | u8(3) << 24;
        for (int i = 0; i < n && std::size_t(4 + 4 * i + 3) < b->size(); ++i) {
            const auto o = std::size_t(4 + 4 * i);
            const int act = u8(o) - 1;
            if (act >= 0 && act < 5) a.data.ids.monpreset[std::size_t(act)].emplace_back(u8(o + 1), u8(o + 2) | u8(o + 3) << 8);
        }
    }
    for (const auto* name : { "MonStats.txt", "SuperUniques.txt" }) {
        const auto t = table(name);
        int rows = 0;
        for (std::size_t r = 0; r < t.size(); ++r) rows += t.get(r, std::optional<std::size_t>{ 0 }) != "Expansion";
        (std::string_view(name) == "MonStats.txt" ? a.data.ids.monstats : a.data.ids.superuniques) = rows;
    }
    const auto prest = table("LvlPrest.txt");
    for (std::size_t r = 0; r < prest.size(); ++r) {
        const int def = to_int(prest.get(r, "Def"));
        if (def < 2 || def > 102) continue;                     // ponytail: act 1's outdoor and cave presets
        Preset p{ to_int(prest.get(r, "SizeX")), to_int(prest.get(r, "SizeY")), to_int(prest.get(r, "Files")),
                  to_int(prest.get(r, "Scan")), to_int(prest.get(r, "Pops")),
                  std::uint32_t(std::stoul("0" + std::string(prest.get(r, "Dt1Mask")))), {} };
        for (int i = 0; i < 6; ++i) p.maps[std::size_t(i)] = ds1(prest.get(r, "File" + std::to_string(i + 1)));
        a.data.presets[def] = p;
    }
    const auto sub = table("LvlSub.txt");
    for (std::size_t r = 0; r < sub.size(); ++r) {
        Sub s;
        s.type = to_int(sub.get(r, "Type"), -1);
        s.check_all = to_int(sub.get(r, "CheckAll"));
        s.bord_type = to_int(sub.get(r, "BordType"));
        s.dt1_mask = std::uint32_t(std::stoul("0" + std::string(sub.get(r, "Dt1Mask"))));
        for (int t = 0; t < 5; ++t) {
            s.prob[std::size_t(t)] = to_int(sub.get(r, "Prob" + std::to_string(t)));
            s.trials[std::size_t(t)] = to_int(sub.get(r, "Trials" + std::to_string(t)));
            s.max[std::size_t(t)] = to_int(sub.get(r, "Max" + std::to_string(t)));
        }
        if (s.type >= 0 && s.type <= 6) s.map = ds1(sub.get(r, "File"));  // act 1's rows
        a.data.subs.push_back(s);
    }
}

// The DT1s rooms of LvlTypes row `type` can list (by mask bit), plus the
// three every room gets (FUN_0066f240).
template <class Read> RoomDt1s load_room_dt1s(OutdoorAssets& a, Read&& read, int type) {
    RoomDt1s d;
    auto load = [&](std::string rel) -> const Dt1File* {
        if (rel.empty() || rel == "0") return nullptr;
        std::string p = R"(data\global\tiles\)", name;
        for (const char c : rel) p.push_back(c == '/' ? '\\' : c);
        for (const char c : rel.substr(rel.find_last_of("/\\") + 1)) name.push_back(char(std::tolower(static_cast<unsigned char>(c))));
        const auto b = read(p);
        if (!b) return nullptr;
        return &a.dt1s.emplace_back(dt1_heads(name, *b));
    };
    for (std::size_t r = 0; r < a.lvl_types.size(); ++r)
        if (to_int(a.lvl_types.get(r, "Id"), -1) == type)
            for (int i = 0; i < 32; ++i) d.by_bit[std::size_t(i)] = load(std::string(a.lvl_types.get(r, "File " + std::to_string(i + 1))));
    d.always = { load("Act1/Outdoors/Blank.dt1"), load("Act1/Barracks/InvisWal.dt1"), load("Act1/Barracks/Warp.dt1") };
    return d;
}

// Levels.txt row by Id.
inline std::optional<std::size_t> level_row(const d2d::txt::Table& levels, int id);

// Level `id`'s warp slots (Levels.txt Warp0..7) with their LvlWarp rows.
inline std::array<WarpSlot, 8> warp_slots(const OutdoorAssets& a, int id) {
    std::array<WarpSlot, 8> slots{};
    const auto row = level_row(a.levels, id);
    if (!row) return slots;
    for (int i = 0; i < 8; ++i) {
        const int w = to_int(a.levels.get(*row, "Warp" + std::to_string(i)), -1);
        for (std::size_t r = 0; w >= 0 && r < a.lvl_warp.size(); ++r)
            if (to_int(a.lvl_warp.get(r, "Id"), -1) == w)
                slots[std::size_t(i)] = { w, to_int(a.lvl_warp.get(r, "LitVersion")) != 0, to_int(a.lvl_warp.get(r, "OffsetX")),
                                          to_int(a.lvl_warp.get(r, "OffsetY")) };
    }
    return slots;
}

// Levels.txt row by Id.
inline std::optional<std::size_t> level_row(const d2d::txt::Table& levels, int id) {
    for (std::size_t r = 0; r < levels.size(); ++r)
        if (to_int(levels.get(r, "Id"), -1) == id) return r;
    return std::nullopt;
}

// Sizes and anchors for the act layout (normal difficulty).
inline LevelDefs level_defs(const d2d::txt::Table& levels) {
    LevelDefs defs;
    for (std::size_t r = 0; r < levels.size(); ++r)
        defs[to_int(levels.get(r, "Id"), -1)] = { to_int(levels.get(r, "SizeX")), to_int(levels.get(r, "SizeY")),
                                                    to_int(levels.get(r, "OffsetX")), to_int(levels.get(r, "OffsetY")),
                                                    to_int(levels.get(r, "DrlgType")) == 3 };
    return defs;
}

// Act 1's layout from the map seed: the act seed is `{map seed, 666}`
// stepped once (FUN_00642da0).
inline std::vector<Placed> act1_from_map_seed(const LevelDefs& defs, std::uint32_t map_seed) {
    d2d::rules::Rng act{ map_seed };
    act.next();
    return act1_layout(defs, act);
}

// A placed outdoor level's generator input. Vis starts as Levels.txt's
// and each chain link adds the pair both ways in the first free slot
// (vis 0, warp -1; FUN_006772c0 -> FUN_00642920); the walkable ones
// (warp -1) that were placed are its neighbours.
inline OutdoorLevel outdoor_level(const d2d::txt::Table& levels, const std::vector<Placed>& layout, int id) {
    OutdoorLevel L;
    auto placed = [&](int lv) -> const Placed* {
        for (const auto& p : layout) if (p.level == lv) return &p;
        return nullptr;
    };
    if (const auto* p = placed(id)) L.rect = *p;
    if (const auto* t = placed(1)) L.town = *t;
    const auto row = level_row(levels, id);
    if (!row) return L;
    L.sub_type = to_int(levels.get(*row, "SubType"), -1);
    L.sub_theme = to_int(levels.get(*row, "SubTheme"), -1);
    L.sub_waypoint = to_int(levels.get(*row, "SubWaypoint"), -1);
    L.sub_shrine = to_int(levels.get(*row, "SubShrine"), -1);
    std::array<int, 8> vis{}, warp{};
    for (int i = 0; i < 8; ++i) {
        vis[std::size_t(i)] = to_int(levels.get(*row, "Vis" + std::to_string(i)));
        warp[std::size_t(i)] = to_int(levels.get(*row, "Warp" + std::to_string(i)), -1);
    }
    auto add = [&](int other) {
        for (int i = 0; i < 8; ++i) if (vis[std::size_t(i)] == other) { warp[std::size_t(i)] = -1; return; }
        for (int i = 0; i < 8; ++i)
            if (vis[std::size_t(i)] == 0 && warp[std::size_t(i)] == -1) { vis[std::size_t(i)] = other; return; }
    };
    for (const auto* chain : { &kAct1Outdoors, &kAct1Highlands })
        for (const auto& r : *chain) {
            if (r.link < 0) continue;
            const int other = (*chain)[std::size_t(r.link)].level;
            if (r.level == id) add(other);
            else if (other == id) add(r.level);
        }
    for (int i = 0; i < 8; ++i)
        if (vis[std::size_t(i)] && warp[std::size_t(i)] == -1)
            if (const auto* p = placed(vis[std::size_t(i)])) L.neighbours.push_back({ *p, i });
    return L;
}

}  // namespace d2d::drlg
