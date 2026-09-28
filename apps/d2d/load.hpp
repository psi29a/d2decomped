// Asset loading: composites, NPCs, saves, load_scene, DS1/DT1 world.
#pragma once

#include "ui.hpp"

#include <atomic>
#include <future>
#include <mutex>
#include <thread>

namespace {

// Forward decl — full body lives after Scene{} construction so it can use
// the same members without repeating field types.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path);

// Act 1's layout from the map seed picks the town's DS1 (the side the
// Blood Moor went) and where both sit.
void place_act1(Scene& scene, d2d::mpq::Stack& mpqs, const d2d::drlg::OutdoorAssets& act1, std::uint32_t map_seed) {
    scene.map_seed = map_seed;
    const auto layout = d2d::drlg::act1_from_map_seed(d2d::drlg::level_defs(act1.levels), map_seed);
    static constexpr std::array<const char*, 4> kTown = { R"(data\global\tiles\ACT1\TOWN\townN1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townE1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townS1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townW1.ds1)" };
    load_world(scene, mpqs, kTown[std::size_t(std::max(0, d2d::drlg::town_file(layout)))]);
    for (const auto& p : layout)
        if (p.level == 1) { scene.town.world_x = p.x; scene.town.world_y = p.y; }
    scene.act1_layout = layout;
}

// Composite tokens: d2s class id -> CHARS folder (Assassin is "AI", its
// dev codename), D2 mode ids we use, and layer names by COF type.
constexpr const char* kCharCode[7] = { "AM", "SO", "NE", "PA", "BA", "DZ", "AI" };
constexpr int kModeDT = 0, kModeNU = 1, kModeWL = 2, kModeRN = 3, kModeGH = 4, kModeTN = 5, kModeTW = 6,
              kModeA1 = 7, kModeBL = 9, kModeSC = 10, kModeKK = 12, kModeS1 = 13, kModeDD = 17;

// ponytail: town walk speed picked by eye so the TW cycle doesn't skate
// (~2 cells = 10 subtiles/s). CharStats.txt WalkVelocity (6) is the real
// input; derive from it once movement units are RE'd.
// Movement speed from a unit's velocity (CharStats Walk/RunVelocity,
// MonStats Velocity): the path velocity is velocity << 8 (scaled by
// velocitypercent, FUN_00462a20), and a unit covers path velocity / 4096
// subtiles per 40 ms tick (arrival time (dist << 16) / ((v >> 8) << 12),
// 0x4c86a3) — velocity / 16 subtiles a tick. Walk 6: 1.875 cells/s.
constexpr float cells_per_sec(float velocity) { return velocity / 16.f * 25.f / 5.f; }

// Direction (0..15, D2's DCC order) for a world-space step (dx, dy) in
// cells. Directions are screen-space: project to screen, take the angle
// clockwise from straight down, and map the 16 sectors through D2's
// ordering — the 8 main directions first (0 SW, 1 NW, 2 NE, 3 SE, 4 S,
// 5 W, 6 N, 7 E), then the half-steps (8 between S and SW, ...).
inline int direction16(float dx, float dy) {
    constexpr int kFromSector[16] = { 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15 };
    const float sx = (dx - dy) * (kIsoW / 2), sy = (dx + dy) * (kIsoH / 2);
    const float a = std::atan2(-sx, sy);                    // 0 = down, + = clockwise
    const int sector = int(std::lround(a / (2 * 3.14159265f / 16)));
    return kFromSector[std::size_t((sector % 16 + 16) % 16)];
}
// A composite's direction for a 16-direction facing: 0..7 are the eight
// compass points, 8..15 the ones between; an 8-direction composite (town
// NPCs, mercs) takes the neighbouring point for those — clamping them
// made NPCs walk backwards. ponytail: the neighbour counter-clockwise;
// game.exe maps its 64 unit directions per direction count, not RE'd.
inline std::uint8_t cof_direction(int dir16, int dirs) {
    constexpr int k16to8[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 4, 0, 5, 1, 6, 2, 7, 3 };
    const int d = dirs == 8 && dir16 >= 0 && dir16 < 16 ? k16to8[dir16] : dir16;
    return std::uint8_t(std::clamp(d, 0, std::max(dirs - 1, 0)));
}
constexpr const char* kModeCode[18] = { "DT", "NU", "WL", "RN", "GH", "TN", "TW", "A1", "A2", "BL", "SC",
                                        "TH", "KK", "S1", "S2", "S3", "S4", "DD" };
constexpr const char* kLayerCode[16] = {
    "HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8",
};

// Load one composite: COF <CC><mode><wclass>, then per COF layer the DCC
// <CC><LY><component><mode><layer wclass>. The weapon class comes from
// the hand/shield bytes (compcode::weapon_class, falling back to hth when
// D2 would reject the combination). Empty body layers wear "lit" (a bare
// head under a circlet, say); empty RH/LH/SH draw nothing.
// Each layer's tint (gfx[16 + layer]) maps its pixels through the item
// colormap first (compcode.md "Tints"). A dead hardcore character's ghost
// is monsters\RH (frontend.hpp).
Scene::PlayerAnim load_composite(const d2d::mpq::Stack& mpqs,
                                 const std::vector<d2d::compcode::Entry>& comp,
                                 const std::array<std::vector<std::uint8_t>, 9>& colormaps,
                                 int cls, int mode, const Scene::Appearance& gfx, bool pixels = true) {
    Scene::PlayerAnim out;
    const char* cc = kCharCode[cls];
    std::string wc(comp.empty() ? std::string_view{}
                                : d2d::compcode::weapon_class(cls, comp, gfx[5], gfx[6], gfx[7]));
    char path[256];
    auto cof_path = [&](std::string_view w) {
        std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\COF\%s%s%.*s.cof)",
                      cc, cc, kModeCode[mode], int(w.size()), w.data());
        return mpqs.try_read(path);
    };
    auto cof = cof_path(wc.empty() ? "hth" : wc);
    if (!cof) cof = cof_path("hth");
    if (!cof) return out;
    try {
        out.cof = d2d::cof::Cof(*cof);
        out.cof_speed = out.cof.speed(); out.cof_frames = out.cof.frames_per_direction(); out.directions = out.cof.directions();
        const std::string_view pv(path);
        out.name = std::string(pv.substr(pv.rfind('\\') + 1, pv.rfind('.') - pv.rfind('\\') - 1));
        for (auto& ch : out.name) ch = char(std::toupper(ch));
        if (!pixels) return out;                          // the timing's: no DCCs
        for (const auto& L : out.cof.layer_defs()) {
            if (L.type >= 16) continue;
            const auto b = gfx[L.type];
            std::string code = (b != 0 && b != 0xff && b < comp.size()) ? comp[b].code : "";
            if (code.empty()) {
                if (L.type >= 5 && L.type <= 7) continue;   // empty hand / no shield
                code = "lit";
            }
            std::string lw = L.weapon_class;
            for (auto* t : { &code, &lw }) for (auto& ch : *t) ch = char(std::toupper(ch));
            std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\%s\%s%s%s%s%s.dcc)",
                          cc, kLayerCode[L.type], cc, kLayerCode[L.type], code.c_str(),
                          kModeCode[mode], lw.c_str());
            if (d2d::compcode::Tint tn; d2d::compcode::tint_of(gfx[16 + L.type], tn) && colormaps[std::size_t(tn.transform)].size() >= 21 * 256)
                out.tint[L.type] = colormaps[std::size_t(tn.transform)].data() + tn.colour * 256;
            if (auto d = mpqs.try_read(path)) out.dcc[L.type] = std::move(*d);
            else if (code != "LIT") {                     // no such piece in this mode (death has only LIT's): the lit one
                std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\%s\%s%sLIT%s%s.dcc)",
                              cc, kLayerCode[L.type], cc, kLayerCode[L.type], kModeCode[mode], lw.c_str());
                if (auto d2 = mpqs.try_read(path)) out.dcc[L.type] = std::move(*d2);
            }
        }
    } catch (const std::exception& e) {
        d2d::log::warn("{}: {}", path, e.what());
    }
    return out;
}

const d2d::dcc::Sprite* Scene::overlay_sprite(const OverlayInfo& o) const {
    auto [it, fresh] = overlay_sprites.try_emplace(&o);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\overlays\)" + o.file + ".dcc"))
            try { it->second = d2d::dcc::Sprite(*b); } catch (const std::exception& e) { d2d::log::warn("overlay {}: {}", o.file, e.what()); }
    return it->second.directions() && it->second.frames_per_direction() ? &it->second : nullptr;
}

const Scene::PlayerAnim& Scene::composite(int d2s_class, int mode, const Appearance& gfx) const {
    std::array<std::uint8_t, 34> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.end(), key.begin() + 2);
    auto it = composites.find(key);
    if (it == composites.end()) {
        it = composites.emplace(key, load_composite(mpqs, comp, colormaps, d2s_class, mode, gfx)).first;
        if (const auto a = anim_data.find(it->second.name); a != anim_data.end())
            std::tie(it->second.speed, it->second.frames, it->second.action) = std::tuple{ a->second.speed, a->second.frames, a->second.action };
    }
    return it->second;
}

// Load an NPC/object composite: COF <root>\<code>\COF\<code><mode><BaseW>,
// then per COF layer <root>\<code>\<LY>\<code><LY><comp><mode><wclass>
// with the recipe's component for that layer ("lit" when blank).
Scene::PlayerAnim load_npc_composite(const d2d::mpq::Stack& mpqs, const Npc& n,
                                     const std::string& mode, bool pixels = true) {
    Scene::PlayerAnim out;
    char path[256];
    std::snprintf(path, sizeof(path), R"(data\global\%s\%s\COF\%s%s%s.cof)",
                  n.root.c_str(), n.code.c_str(), n.code.c_str(), mode.c_str(), n.base_w.c_str());
    auto cof = mpqs.try_read(path);
    if (!cof) return out;
    try {
        out.cof = d2d::cof::Cof(*cof);
        out.cof_speed = out.cof.speed(); out.cof_frames = out.cof.frames_per_direction(); out.directions = out.cof.directions();
        const std::string_view pv(path);
        out.name = std::string(pv.substr(pv.rfind('\\') + 1, pv.rfind('.') - pv.rfind('\\') - 1));
        for (auto& ch : out.name) ch = char(std::toupper(ch));
        if (!pixels) return out;                          // the timing's: no DCCs
        for (const auto& L : out.cof.layer_defs()) {
            if (L.type >= 16) continue;
            std::string comp = n.comp[L.type].empty() ? "lit" : n.comp[L.type];
            std::string lw = L.weapon_class;
            for (auto* t : { &comp, &lw }) for (auto& ch : *t) ch = char(std::toupper(ch));
            std::snprintf(path, sizeof(path), R"(data\global\%s\%s\%s\%s%s%s%s%s.dcc)",
                          n.root.c_str(), n.code.c_str(), kLayerCode[L.type], n.code.c_str(),
                          kLayerCode[L.type], comp.c_str(), mode.c_str(), lw.c_str());
            if (auto d = mpqs.try_read(path)) out.dcc[L.type] = std::move(*d);
        }
    } catch (const std::exception& e) {
        d2d::log::warn("{}: {}", path, e.what());
    }
    return out;
}

// The timing lookups: the COF and animdata only (the World's).
template <class Key, class Load>
const GameData::AnimTiming& timing_of(const GameData& g, std::map<Key, GameData::AnimTiming>& cache, const Key& key, Load&& load) {
    auto it = cache.find(key);
    if (it == cache.end()) {
        GameData::AnimTiming t = load();
        if (const auto a = g.anim_data.find(t.name); a != g.anim_data.end()) std::tie(t.speed, t.frames, t.action) = std::tuple{ a->second.speed, a->second.frames, a->second.action };
        it = cache.emplace(key, std::move(t)).first;
    }
    return it->second;
}
const GameData::AnimTiming& GameData::npc_timing(const Npc& n, std::string_view mode) const {
    auto key = n.root + "/" + n.code + "/" + std::string(mode) + "/" + n.base_w;
    for (const auto& c : n.comp) key += "/" + c;
    return timing_of(*this, npc_timings, key, [&] { return GameData::AnimTiming(load_npc_composite(mpqs, n, std::string(mode), false)); });
}
const GameData::AnimTiming& GameData::composite_timing(int d2s_class, int mode, const std::array<std::uint8_t, 32>& gfx) const {
    std::array<std::uint8_t, 18> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.begin() + 16, key.begin() + 2);
    return timing_of(*this, composite_timings, key, [&] { return GameData::AnimTiming(load_composite(mpqs, comp, {}, d2s_class, mode, gfx, false)); });
}

const Scene::PlayerAnim& Scene::npc_anim(const Npc& n, std::string_view mode) const {
    const std::string m(mode);
    auto key = n.root + "/" + n.code + "/" + m + "/" + n.base_w;
    for (const auto& c : n.comp) key += "/" + c;
    auto it = npc_anims.find(key);
    if (it == npc_anims.end()) {
        it = npc_anims.emplace(key, load_npc_composite(mpqs, n, m)).first;
        if (const auto a = anim_data.find(it->second.name); a != anim_data.end())
            std::tie(it->second.speed, it->second.frames, it->second.action) = std::tuple{ a->second.speed, a->second.frames, a->second.action };
    }
    return it->second;
}

const d2d::dc6::Sprite* Scene::flippy(const std::string& code) const {
    const auto info = rules.item_info.find(code);
    if (info == rules.item_info.end() || info->second.flippy.empty()) return nullptr;
    auto [it, fresh] = flippy_sprites.try_emplace(info->second.flippy);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\items\)" + info->second.flippy + ".dc6"))
            it->second = d2d::dc6::Sprite(*b);
    return it->second ? &*it->second : nullptr;
}

const d2d::dc6::Sprite* Scene::item_sprite(const d2d::d2s::Item& item) const {
    const auto info = rules.item_info.find(item.code);
    if (info == rules.item_info.end()) return nullptr;
    auto pick = [](const std::vector<std::string>& v, int i) {
        return i >= 0 && std::size_t(i) < v.size() ? v[std::size_t(i)] : std::string{};
    };
    std::string file = item.quality == 7 ? pick(unique_inv, item.unique_id)
                     : item.quality == 5 ? pick(set_inv, item.set_id) : std::string{};
    if (file.empty() && item.picture >= 0 && item.picture < 6)
        if (const auto g = type_invgfx.find(info->second.type); g != type_invgfx.end())
            file = g->second[std::size_t(item.picture)];
    if (file.empty()) file = info->second.invfile;
    if (file.empty()) return nullptr;
    auto [it, fresh] = item_sprites.try_emplace(file);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\items\)" + file + ".dc6"))
            it->second = d2d::dc6::Sprite(*b);
    return it->second ? &*it->second : nullptr;
}

// MonStats2 layer variants ("lit,med": quoted lists), HDv..S8v.
constexpr const char* kVariant[16] = {
    "HDv", "TRv", "LGv", "RAv", "LAv", "RHv", "LHv", "SHv",
    "S1v", "S2v", "S3v", "S4v", "S5v", "S6v", "S7v", "S8v",
};
std::vector<std::string> split_variants(std::string_view v) {
    std::vector<std::string> out;
    while (!v.empty() && (v.front() == '"' || v.back() == '"')) v = v.front() == '"' ? v.substr(1) : v.substr(0, v.size() - 1);
    while (!v.empty()) {
        const auto c = v.find(',');
        out.emplace_back(v.substr(0, c));
        if (c == std::string_view::npos) break;
        v.remove_prefix(c + 1);
    }
    return out;
}

// MonStats2 row by Id.
std::unordered_map<std::string, std::size_t> id_rows(const d2d::txt::Table& t) {
    std::unordered_map<std::string, std::size_t> out;
    for (std::size_t r = 0; r < t.size(); ++r) out.emplace(std::string(t.get(r, "Id")), r);
    return out;
}

// A monster unit from its MonStats row and the MonStats2 row its
// MonStatsEx names (the tables aren't row-aligned: 734 vs 609 rows).
// A level's own light (FUN_00474550 via FUN_00619d70): when any of Levels.txt
// Intensity / Red / Green / Blue is set its Intensity replaces the day's.
int level_light(std::string_view i, std::string_view r, std::string_view g, std::string_view b) {
    auto n = [](std::string_view v) { return std::atoi(std::string(v).c_str()); };
    return n(i) || n(r) || n(g) || n(b) ? n(i) : -1;
}

Npc monster_npc(const Scene& scene, const d2d::txt::Table& ms, const d2d::txt::Table& ms2,
                const std::unordered_map<std::string, std::size_t>& ms2_rows, std::size_t row) {
    const auto ex = ms2_rows.find(std::string(ms.get(row, "MonStatsEx")));
    const std::size_t row2 = ex == ms2_rows.end() ? row : ex->second;
    Npc n;
    n.root   = "monsters";
    n.mode   = "NU";
    n.code   = std::string(ms.get(row, "Code"));
    n.hc_idx = std::atoi(std::string(ms.get(row, "hcIdx")).c_str());
    n.id     = std::string(ms.get(row, "Id"));
    n.base_w = std::string(ms2.get(row2, "BaseW"));
    n.size_x = std::atoi(std::string(ms2.get(row2, "SizeX")).c_str());
    n.size_y = std::atoi(std::string(ms2.get(row2, "SizeY")).c_str());
    n.light  = std::atoi(std::string(ms2.get(row2, "Light")).c_str());
    n.overlay_class = std::atoi(std::string(ms2.get(row2, "OverlayHeight")).c_str()) - 1;
    if (const auto v = ms.get(row, "Velocity"); !v.empty()) n.velocity = float(std::atoi(std::string(v).c_str()));
    // Hover name: MonStats' string key, only for units MonStats2 marks
    // selectable (isSel) — not the chicken or the camp's guard rogues,
    // whose name key "Dummy" reads "an evil force".
    if (ms2.get(row2, "isSel") == "1") {
        std::string key(ms.get(row, "NameStr"));        // 1.14d; "namco" on the CD
        if (key.empty()) key = std::string(ms.get(row, "namco"));
        auto v = lookup_string(scene, key);
        n.name = v ? u16_to_latin1(*v) : key;
    }
    if (n.code.empty()) return n;
    if (n.base_w.empty()) n.base_w = "hth";
    for (std::size_t l = 0; l < 16; ++l) {
        if (ms2.get(row2, kLayerCode[l]) != "1") continue;
        const auto v = split_variants(ms2.get(row2, kVariant[l]));
        n.comp[l] = v.empty() || v[0].empty() ? "lit" : v[0];
    }
    return n;
}

// Monster tables (MonStats, MonStats2, MonLvl), the Blood Moor's Levels.txt
// monster columns, and its rooms populated (components/rules/monsters.hpp).
// ponytail: every room at load, in cell order, normal difficulty — game.exe
// populates a room when it first activates (so the game seed's order
// follows the player) and knows the game's difficulty; the game seed is
// the map seed here.
void load_monsters(Scene& scene, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* n) {
        auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt");
        return b ? d2d::txt::Table(*b) : d2d::txt::Table{};
    };
    const auto ms = txt("MonStats"), ms2 = txt("MonStats2"), ml = txt("MonLvl"), lv = txt("Levels");
    if (ms.size() == 0 || ms2.size() == 0) return;
    const auto ms2_rows = id_rows(ms2);
    auto num = [](std::string_view v) { return std::atoi(std::string(v).c_str()); };
    auto& M = scene.monsters;
    for (std::size_t r = 0; r < ms.size(); ++r) M.by_id.emplace(std::string(ms.get(r, "Id")), int(r));
    auto row = [&](std::string_view id) { return id.empty() ? -1 : M.row(std::string(id)); };
    static constexpr const char* kSfx[3] = { "", "(N)", "(H)" };
    M.types.resize(ms.size());
    scene.mon_npc.resize(ms.size());
    for (std::size_t r = 0; r < ms.size(); ++r) {
        auto& t = M.types[r];
        auto g = [&](std::string c) { return ms.get(r, c); };
        t.id = g("Id"); t.code = g("Code"); t.name_key = g("NameStr"); t.ai = g("AI");
        t.base = row(g("BaseId"));
        t.min_grp = num(g("MinGrp")); t.max_grp = num(g("MaxGrp"));
        t.party_min = num(g("PartyMin")); t.party_max = num(g("PartyMax"));
        t.sparse = num(g("sparsePopulate")); t.rarity = num(g("Rarity"));
        t.minion = { row(g("minion1")), row(g("minion2")) };
        t.velocity = num(g("Velocity")); t.run = num(g("Run"));
        t.enabled = g("enabled") == "1"; t.killable = g("killable") == "1"; t.melee = g("isMelee") == "1";
        t.miss_a2 = g("MissA2");
        t.undead = g("hUndead") == "1" || g("lUndead") == "1"; t.demon = g("demon") == "1";
        for (int e = 0; e < 3; ++e) {
            static constexpr std::array<std::string_view, 5> kEl = { "fire", "ltng", "cold", "pois", "mag" };
            const std::string E = "El" + std::to_string(e + 1);
            t.el_mode[std::size_t(e)] = g(E + "Mode");
            const auto type = g(E + "Type");                  // frze counts as cold; life/mana/stam/stun/rand aren't damage here
            const auto ty = std::ranges::find(kEl, type == "frze" ? std::string_view("cold") : type);
            t.el_type[std::size_t(e)] = ty == kEl.end() ? -1 : int(ty - kEl.begin());
        }
        t.sound = g("MonSound");
        t.montype = g("MonType");
        for (int d = 0; d < 3; ++d) {
            const std::string x = kSfx[d];
            auto& p = t.diff[std::size_t(d)];
            t.level[std::size_t(d)] = num(g("Level" + x));
            p = { num(g("MinHP" + x)), num(g("MaxHP" + x)), num(g("AC" + x)), num(g("Exp" + x)),
                  num(g("A1MinD" + x)), num(g("A1MaxD" + x)), num(g("A1TH" + x)),
                  num(g("A2MinD" + x)), num(g("A2MaxD" + x)), num(g("A2TH" + x)),
                  num(g("aidel" + x)), num(g("aidist" + x)), {}, std::string(g("TreasureClass1" + x)) };
            for (int i = 0; i < 8; ++i) p.aip[std::size_t(i)] = num(g("aip" + std::to_string(i + 1) + x));
            static constexpr const char* kRes[6] = { "ResDm", "ResMa", "ResFi", "ResLi", "ResCo", "ResPo" };
            for (int i = 0; i < 6; ++i) p.res[std::size_t(i)] = num(g(kRes[i] + x));
            p.to_block = num(g("ToBlock" + x));
            t.tc_champion[std::size_t(d)] = g("TreasureClass2" + x);
            t.tc_unique[std::size_t(d)] = g("TreasureClass3" + x);
            p.drain = g("Drain" + x).empty() ? 100 : num(g("Drain" + x));
            p.cold_effect = num(g("coldeffect" + x));
            for (int e = 0; e < 3; ++e) {
                const std::string E = "El" + std::to_string(e + 1);
                p.el[std::size_t(e)] = { num(g(E + "Pct" + x)), num(g(E + "MinD" + x)), num(g(E + "MaxD" + x)), num(g(E + "Dur" + x)) };
            }
        }
        const auto ex = ms2_rows.find(std::string(g("MonStatsEx")));
        if (ex != ms2_rows.end()) {
            const auto r2 = ex->second;
            t.size = std::max(num(ms2.get(r2, "SizeX")), 1);
            t.base_w = ms2.get(r2, "BaseW");
            t.can_block = ms2.get(r2, "mBL") == "1";
            for (std::size_t l = 0; l < 16; ++l)
                if (ms2.get(r2, kLayerCode[l]) == "1") t.parts[l] = split_variants(ms2.get(r2, kVariant[l]));
        }
        scene.mon_npc[r] = monster_npc(scene, ms, ms2, ms2_rows, r);
    }
    // MonLvl: the LoD columns (L-*); normal matches the classic ones.
    for (std::size_t r = 0; r < ml.size(); ++r) {
        const int level = num(ml.get(r, "Level"));
        if (level < 0 || level > 200) continue;
        if (std::size_t(level) >= M.lvl.size()) M.lvl.resize(std::size_t(level) + 1);
        auto& L = M.lvl[std::size_t(level)];
        for (int d = 0; d < 3; ++d) {
            const std::string x = kSfx[d];
            L.ac[std::size_t(d)] = num(ml.get(r, "L-AC" + x)); L.th[std::size_t(d)] = num(ml.get(r, "L-TH" + x));
            L.hp[std::size_t(d)] = num(ml.get(r, "L-HP" + x)); L.dm[std::size_t(d)] = num(ml.get(r, "L-DM" + x));
            L.xp[std::size_t(d)] = num(ml.get(r, "L-XP" + x));
        }
    }
    // MonSounds.txt.
    const auto snd = txt("MonSounds");
    for (std::size_t r = 0; r < snd.size(); ++r) {
        auto id = [&](const char* c) { const auto it = scene.sound_index.find(std::string(snd.get(r, c))); return it == scene.sound_index.end() ? 0 : it->second; };
        auto n = [&](const char* c) { return num(snd.get(r, c)); };
        Scene::MonSound m{ { id("Attack1"), id("Attack2") }, { id("Weapon1"), id("Weapon2") }, { n("Att1Del"), n("Att2Del") },
                           { n("Wea1Del"), n("Wea2Del") }, { n("Att1Prb"), n("Att2Prb") }, id("HitSound"), id("DeathSound"),
                           n("HitDelay"), n("DeaDelay") };
        scene.mon_sounds.emplace(std::string(snd.get(r, "Id")), m);
    }
    // Missiles.txt, the rows monsters and skills fire.
    const auto mt = txt("Missiles"), sk = txt("Skills");
    std::unordered_set<std::string> skill_missiles;
    for (std::size_t r = 0; r < sk.size(); ++r) {
        skill_missiles.emplace(sk.get(r, "srvmissile"));
        for (const char* c : { "srvmissilea", "srvmissileb", "srvmissilec" }) skill_missiles.emplace(sk.get(r, c));
        if (const std::string a(sk.get(r, "srvmissilea")); num(sk.get(r, "srvdofunc")) == 149 && !a.empty())   // necromage1..4
            for (char d = '2'; d <= '4'; ++d) skill_missiles.emplace(a.substr(0, a.size() - 1) + d);
    }
    // Thrown weapons' rows (weapons.txt missiletype: Missiles.txt Id), for
    // Double Throw.
    std::unordered_map<int, std::string> thrown_ids;
    {
        const auto wt = txt("weapons");
        for (std::size_t r = 0; r < wt.size(); ++r)
            if (const int id = num(wt.get(r, "missiletype")); id > 0) thrown_ids.emplace(id, std::string(wt.get(r, "code")));
        for (std::size_t r = 0; r < mt.size(); ++r)
            if (const auto it = thrown_ids.find(num(mt.get(r, "Id"))); it != thrown_ids.end()) skill_missiles.emplace(mt.get(r, "Missile"));
        for (const auto& [id, code] : thrown_ids)
            for (std::size_t r = 0; r < mt.size(); ++r)
                if (num(mt.get(r, "Id")) == id) scene.thrown[code] = std::string(mt.get(r, "Missile"));
    }
    // and the rows those spawn (SubMissile1, HitSubMissile1: Fire Wall's
    // flames, Meteor's fire, Blizzard's shards), to a fixed point.
    for (bool more = true; more;) {
        more = false;
        for (std::size_t r = 0; r < mt.size(); ++r)
            if (skill_missiles.contains(std::string(mt.get(r, "Missile"))))
                for (const char* c : { "SubMissile1", "HitSubMissile1" })
                    if (const std::string n(mt.get(r, c)); !n.empty()) more |= skill_missiles.emplace(n).second;
    }
    std::vector<std::pair<d2d::dcc::Sprite*, std::vector<std::byte>>> cels;
    for (std::size_t r = 0; r < mt.size(); ++r) {
        auto g = [&](std::string c) { return num(mt.get(r, c)); };
        const std::string name(mt.get(r, "Missile"));
        bool used = name == "arrow" || name == "denofevillight" || skill_missiles.contains(name)    // the rogue merc's, the Den's light beams, skills',
                 || std::ranges::contains(d2d::rules::kTrapMissile, std::string_view(name))    // chest traps
                 || std::ranges::contains(d2d::rules::kBossMissile, std::string_view(name));   // a unique's mods
        for (const auto& t : M.types) used = used || t.miss_a2 == name;
        if (!used) continue;
        Scene::MissileInfo mi;
        mi.name = name;
        mi.vel = g("Vel"); mi.range = g("Range"); mi.src_damage = g("SrcDamage"); mi.min = g("MinDamage"); mi.max = g("MaxDamage");
        mi.anim_speed = std::max(g("AnimSpeed"), 1); mi.anim_len = std::max(g("AnimLen"), 1); mi.trans = g("Trans"); mi.light = g("Light");
        mi.skill = mt.get(r, "Skill"); mi.lev_range = g("LevRange"); mi.hit_func = g("pSrvHitFunc"); mi.hit_par1 = g("sHitPar1");
        mi.to_hit = g("ToHit") == 1; mi.collide_kill = g("CollideKill") == 1; mi.pierce = g("Pierce") == 1;
        mi.srv_do = g("pSrvDoFunc"); mi.param1 = g("Param1"); mi.param2 = g("Param2"); mi.hit_par2 = g("sHitPar2");
        mi.next_hit = g("NextHit") == 1; mi.next_delay = g("NextDelay");
        mi.sub = mt.get(r, "SubMissile1"); mi.hit_sub = mt.get(r, "HitSubMissile1");
        static constexpr std::array<std::string_view, 6> kEl = { "fire", "ltng", "cold", "pois", "mag", "frze" };
        if (const auto e = std::ranges::find(kEl, mt.get(r, "EType")); e != kEl.end()) mi.etype = *e == "frze" ? 2 : int(e - kEl.begin());
        mi.emin = g("EMin"); mi.emax = g("Emax"); mi.hitshift = g("HitShift"); mi.elen = g("ELen");
        for (int i = 0; i < 5; ++i) {
            mi.emin_lev[std::size_t(i)] = g("MinELev" + std::to_string(i + 1));
            mi.emax_lev[std::size_t(i)] = g("MaxELev" + std::to_string(i + 1));
        }
        for (int i = 0; i < 3; ++i) mi.elen_lev[std::size_t(i)] = g("ELevLen" + std::to_string(i + 1));
        scene.missiles.emplace(name, std::move(mi));
        if (auto b = mpqs.try_read(R"(data\global\missiles\)" + std::string(mt.get(r, "CelFile")) + ".dcc")) cels.emplace_back(&scene.missile_cels[name], std::move(*b));
    }
    if (auto b = mpqs.try_read(R"(data\global\overlays\NPCSpeechBalloon.dcc)")) cels.emplace_back(&scene.npc_alert, std::move(*b));
    for (std::size_t i = 0; i < 4; ++i)                // the rain's splashes (FUN_00472890)
        if (auto b = mpqs.try_read(R"(data\global\UncompOverlays\Rain)" + std::to_string(i + 1) + ".dc6")) scene.rain_splash[i] = d2d::dc6::Sprite(*b);
    // Their DCCs decode on every core (the reads above stay on this thread:
    // StormLib handles aren't shared).
    {
        std::atomic<std::size_t> next = 0;
        std::vector<std::jthread> pool(std::max(1u, std::thread::hardware_concurrency()));
        for (auto& t : pool) t = std::jthread([&] {
            for (std::size_t i; (i = next++) < cels.size();)
                try { *cels[i].first = d2d::dcc::Sprite(cels[i].second); }
                catch (const std::exception& e) { d2d::log::warn("missile cel: {}", e.what()); }
        });
    }
    // SuperUniques.txt: name (string key), Class, minions.
    std::vector<std::size_t> ms_bin;                    // game.exe's MonStats rows: without the Expansion row
    for (std::size_t r = 0; r < ms.size(); ++r) if (ms.get(r, "Id") != "Expansion") ms_bin.push_back(r);
    if (const auto su = txt("SuperUniques"); su.size() > 0)
        for (std::size_t r = 0; r < su.size(); ++r) {
            if (su.get(r, "Superunique") == "Expansion") continue;
            const std::string key(su.get(r, "Name"));
            const auto v = lookup_string(scene, key);
            std::vector<int> mods;
            for (const char* c : { "Mod1", "Mod2", "Mod3" }) if (const int md = num(su.get(r, c)); md > 0) mods.push_back(md);
            scene.superuniques.push_back({ v ? u16_to_latin1(*v) : key, row(su.get(r, "Class")), num(su.get(r, "MinGrp")),
                                           num(su.get(r, "MaxGrp")), mods,
                                           { std::string(su.get(r, "TC")), std::string(su.get(r, "TC(N)")), std::string(su.get(r, "TC(H)")) } });
        }

    // Random unique names: UniquePrefix / Suffix / Appellation (Name: a
    // string key) and the two formats the client builds them with.
    for (auto [file, i] : { std::pair{ "UniquePrefix", 0 }, { "UniqueSuffix", 1 }, { "UniqueAppellation", 2 } }) {
        const auto t = txt(file);
        for (std::size_t r = 0; r < t.size(); ++r) {
            const std::string key(t.get(r, "Name"));
            if (key.empty() || key == "Expansion") continue;
            const auto v = lookup_string(scene, key);
            scene.unique_names[std::size_t(i)].push_back(v ? u16_to_latin1(*v) : key);
        }
    }
    for (int i = 0; i < 2; ++i)
        if (const auto v = lookup_string(scene, std::uint16_t(0x6b9 + i))) scene.unique_formats[std::size_t(i)] = u16_to_latin1(*v);

    // MonUMod.txt: champion / unique mods and the constants column.
    if (const auto um = txt("MonUMod"); um.size() > 0)
        for (std::size_t r = 0; r < um.size(); ++r) {
            if (um.get(r, "uniquemod") == "Expansion") continue;
            auto g = [&](const char* c) { return num(um.get(r, c)); };
            const int id = g("id");
            if (id >= 0 && id < 34) scene.umods.k[std::size_t(id)] = g("constants");
            scene.umods.rows.push_back({ id, g("enabled") == 1, g("champion") == 1, g("fPick"), std::string(um.get(r, "exclude1")),
                                         std::string(um.get(r, "exclude2")), { g("cpick"), g("cpick (N)"), g("cpick (H)") },
                                         { g("upick"), g("upick (N)"), g("upick (H)") } });
        }

    scene.mon_bin = ms_bin;
    scene.mon_is_npc.resize(ms.size());
    for (std::size_t r = 0; r < ms.size(); ++r) scene.mon_is_npc[r] = ms.get(r, "npc") == "1";
    if (!scene.superuniques.empty()) d2d::log::info("  not implemented: unique mods: thief, poison hit (not in Act 1); Charged Bolt's wander");
}

// Skills (components/rules/skills.hpp): skillcalc.txt's operand names,
// ItemStatCost's stat names and Skills.txt's rows with their calcs
// compiled, SkillDesc's tab, icon and name. A calc that doesn't compile is
// logged and reads 0.
// A level's monsters at difficulty `d` (0 normal, 1 nightmare, 2 hell):
// its preset monsters and superuniques, then its rooms populated. Made the
// first time the level is played at `d`, then kept (Level::spawns).
// Every difficulty is its own game: its own region, density and (in
// nightmare and hell) champions and uniques (MonUMin / MonUMax).
// ponytail: each level's rooms in cell order, on a game seed from the map
// seed; game.exe populates a room when it first comes up, in whatever order
// the player brings them, each level's region seeded when it's made.
const std::vector<d2d::rules::Spawn>& level_spawns(const GameData& scene, const Level& L, int d) {
    d = std::clamp(d, 0, 2);
    if (L.spawns[std::size_t(d)]) return *L.spawns[std::size_t(d)];
    static constexpr const char* kSfx[3] = { "", "(N)", "(H)" };
    const auto& M = scene.monsters;
    d2d::rules::Rng game{ scene.map_seed };
    if (L.rooms.empty() || L.walk.empty()) return L.spawns[std::size_t(d)].emplace();
    auto& spawns = L.spawns[std::size_t(d)].emplace();
    d2d::rules::Rng region_seed{ game.next() };
    const auto region = d2d::rules::monster_region(M, L.mon, d, region_seed);
    for (const auto& [row, rarity] : region.types) L.region[std::size_t(d)].push_back(row);
    d2d::rules::Population pop{ 0, int(L.rooms.size()), 0, L.mon.umin[std::size_t(d)], L.mon.umax[std::size_t(d)], d, &scene.umods };
    // Not within WarpDist (2025 = 45^2 subtiles) of where players come
    // in: the camp for the Blood Moor, the warps for a level entered by one.
    std::vector<std::array<int, 4>> ways;
    if (L.id == 2) {
        const int tx0 = (scene.town.world_x - L.world_x) * 5, ty0 = (scene.town.world_y - L.world_y) * 5;
        ways.push_back({ tx0, ty0, tx0 + scene.town.ds1.width() * 5, ty0 + scene.town.ds1.height() * 5 });
    } else {
        for (const auto& w : L.warps) ways.push_back({ int(w.x) * 5, int(w.y) * 5, int(w.x) * 5 + 5, int(w.y) * 5 + 5 });
    }
    auto near_way = [&](int x, int y) {
        for (const auto& [x0, y0, x1, y1] : ways) {
            const int dx = std::max({ x0 - x, 0, x - x1 }), dy = std::max({ y0 - y, 0, y - y1 });
            if (dx * dx + dy * dy < 2025) return true;
        }
        return false;
    };
    // Clear ground, and no monster already within its 2-subtile footprint
    // (game.exe stamps each placed monster into collision, 0x800).
    auto fits = [&](int x, int y) {
        for (const auto& o : spawns) if (std::abs(o.x - x) < 2 && std::abs(o.y - y) < 2) return false;
        return !L.unit_blocked((float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5);
    };
    // Its preset units (Level::units, what the level's DS1s place), as
    // FUN_0054e600 spawns them (docs/research/re/monsters.md "Preset units
    // on the server"): a MonStats row at its spot; a superunique with its
    // minions; a MonPlace code by the switch (Fallen and shamans, champion
    // packs, unique packs, Blood Raven; the rest, group25..100 among them,
    // spawn nothing).
    // ponytail: placements roll a copy of the room's seed (game.exe shares
    // the room's, and a monster's own for its company); the level-list walk
    // up a family's chain (FUN_0063ec70's second half) and the champion /
    // unique pick from Levels.txt umon1.. in normal aren't there (the
    // region's list stands in).
    using d2d::rules::monster_detail::place;
    const int nmon = int(scene.mon_bin.size()), nsu = int(scene.superuniques.size());
    auto bin = [&](int b) { return b >= 0 && b < nmon ? int(scene.mon_bin[std::size_t(b)]) : -1; };
    auto room_at = [&](int x, int y) -> d2d::rules::SpawnRoom {
        for (const auto& rm : L.rooms)
            if (x >= rm.x * 5 && y >= rm.y * 5 && x < (rm.x + rm.w) * 5 && y < (rm.y + rm.h) * 5)
                return { rm.x * 5, rm.y * 5, rm.w * 5, rm.h * 5, d2d::rules::Rng{ rm.seed } };
        return { 0, 0, L.ds1.width() * 5, L.ds1.height() * 5, d2d::rules::Rng{ scene.map_seed } };
    };
    // FUN_005b2f20 at the spot (radius -1), then within `retry` if taken.
    auto at_spot = [&](int type, int x, int y, int retry) {
        if (type < 0) return;
        auto room = room_at(x, y);
        int px, py;
        if (place(room, x, y, -1, fits, px, py) || (retry > 0 && place(room, x, y, retry, fits, px, py))) spawns.push_back({ type, px, py });
    };
    // FUN_0063ec70 + FUN_0054e2a0: a base monster as the level has it — the
    // first of its family in the level's list, then Carvers / Devilkin (and
    // their shamans) by level.
    auto own = [&](int base_bin) {
        int row = bin(base_bin);
        if (row < 0) return -1;
        for (const int r : L.mon.mon)
            if (r >= 0 && std::size_t(r) < M.types.size() && M.types[std::size_t(r)].base == M.types[std::size_t(row)].base) { row = r; break; }
        const int base = M.types[std::size_t(row)].base, id = L.id;
        if (base == bin(0x13)) return id == 6 ? bin(0x14) : id == 7 || id == 12 || id == 16 ? bin(0x15) : row;
        if (base == bin(0x3a)) return id == 6 || id == 7 ? bin(0x3b) : id == 12 || id == 16 ? bin(0x3c) : row;
        return row;
    };
    for (const auto& u : L.units) {
        if (u.type != 1 || u.id < 0) continue;
        if (u.id < nmon) {                                                // a MonStats row (FUN_0054e490)
            const auto r = scene.mon_bin[std::size_t(u.id)];
            const bool stay = u.id == 0xe5 || (u.id >= 0x11c && u.id <= 0x120) || u.id == 0x188 || u.id == 0x189;   // FUN_0054e3a0
            if (!scene.mon_is_npc[r]) at_spot(int(r), u.x, u.y, stay ? 0 : 4);
            continue;
        }
        if (u.id >= nmon + nsu) {                                         // MonPlace
            const int code = u.id - nmon - nsu;
            if (code == 0x11 || code == 0x12) at_spot(own(code == 0x11 ? 0x13 : 0x3a), u.x, u.y, 4);   // place_fallen / _fallenshaman
            else if (code == 0x05) at_spot(bin(0x10b), u.x, u.y, 0);      // place_bloodraven
            else if ((code == 0x02 || code == 0x03) && !region.types.empty()) {
                auto room = room_at(u.x, u.y);
                const int type = d2d::rules::pick_type(region, room.seed);   // FUN_005bde80, the unique pick
                int px, py, sx, sy;
                if (code == 0x03) {                                       // place_champion: at the spot, mod 16, 1..3 more (FUN_0054e1e0)
                    if (!place(room, u.x, u.y, -1, fits, px, py)) continue;
                    const int lead = int(spawns.size());
                    spawns.push_back({ type, px, py, lead, -1, d2d::rules::Boss::champion, { d2d::rules::umod::champion } });
                    for (int c = room.seed(3) + 1; c > 0; --c)
                        if (place(room, px, py, 4, fits, sx, sy)) spawns.push_back({ type, sx, sy, lead, -1, d2d::rules::Boss::champion, { d2d::rules::umod::champion } });
                } else if (d2d::rules::room_spot(room, fits, near_way, sx, sy) && place(room, sx, sy, -1, fits, px, py)) {
                    d2d::rules::boss_pack(M, type, px, py, room, fits, spawns, pop);   // place_unique_pack: a random spot of the room (FUN_005a43e0)
                }
            }
            continue;
        }
        // A superunique (FUN_005a49b0): at its spot, its mods, then
        // MinGrp..MaxGrp (each + difficulty when both are set) of minion1
        // (else its own type) at radius 3 (FUN_005a0c00 / FUN_005b23c0).
        // ponytail: its unique stat bonuses and TC come in the fight / loot;
        // the per-superunique specials (the Countess, the Smith ...) aren't built.
        const int su = u.id - nmon;
        const auto& sup = scene.superuniques[std::size_t(su)];
        if (sup.type < 0) continue;
        auto room = room_at(u.x, u.y);
        int lx, ly;
        if (!place(room, u.x, u.y, -1, fits, lx, ly) && !place(room, u.x, u.y, 5, fits, lx, ly)) continue;
        const int lead = int(spawns.size());
        spawns.push_back({ sup.type, lx, ly, -1, su, d2d::rules::Boss::superunique,
                           d2d::rules::superunique_mods(scene.umods, M.types[std::size_t(sup.type)], sup.mods, d, game) });
        const int minion = M.types[std::size_t(sup.type)].minion[0] >= 0 ? M.types[std::size_t(sup.type)].minion[0] : sup.type;
        const int lo = sup.min_grp + (sup.min_grp && sup.max_grp ? d : 0), hi = sup.max_grp + (sup.min_grp && sup.max_grp ? d : 0);
        const int n = room.seed.range(lo, std::max(lo, hi));
        int placed = 0;
        for (int k = 0; k < n; ++k) {
            int px, py;
            if (place(room, lx, ly, 3, fits, px, py)) { spawns.push_back({ minion, px, py, lead, -1, d2d::rules::Boss::minion, {} }); ++placed; }
        }
        if (d == 0) d2d::log::info("  {} ({}) with {} minions at ({:.1f}, {:.1f})", sup.name, M.types[std::size_t(sup.type)].id, placed,
                                   (float(lx) + 0.5f) / 5, (float(ly) + 0.5f) / 5);
    }
    for (const auto& rm : L.rooms) {
        d2d::rules::populate_room(M, region, L.mon.density[std::size_t(d)],
            { rm.x * 5, rm.y * 5, rm.w * 5, rm.h * 5, d2d::rules::Rng{ rm.seed } }, game, fits, near_way, spawns, &pop);
    }
    std::array<int, 3> by{};
    for (const auto& sp : spawns)
        for (std::size_t i = 0; i < region.types.size() && i < 3; ++i) by[i] += sp.type == region.types[i].first;
    int bosses = 0;
    for (const auto& sp : spawns) bosses += sp.boss == d2d::rules::Boss::champion || sp.boss == d2d::rules::Boss::unique;
    d2d::log::info("  Monsters ({}): {} in {} ({}), {} champions / uniques", kSfx[d][0] ? kSfx[d] : "normal", spawns.size(),
                   L.name, [&] {
        std::string s;
        for (std::size_t i = 0; i < region.types.size() && i < 3; ++i)
            s += (i ? ", " : "") + std::to_string(by[i]) + " " + M.types[std::size_t(region.types[i].first)].id;
        return s;
    }(), bosses);

    return spawns;
}

void load_skills(Scene& scene, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* n) {
        auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt");
        return b ? d2d::txt::Table(*b) : d2d::txt::Table{};
    };
    const auto sc = txt("skillcalc"), isc = txt("ItemStatCost"), sk = txt("Skills"), sd = txt("SkillDesc");
    if (sk.size() == 0) return;
    auto& T = scene.skills;
    auto num = [](std::string_view v) { return std::atoi(std::string(v).c_str()); };
    for (std::size_t r = 0; r < sc.size(); ++r) T.names.operands.emplace_back(sc.get(r, "code"));
    for (std::size_t r = 0; r < isc.size(); ++r) T.names.stats.emplace(std::string(isc.get(r, "Stat")), num(isc.get(r, "ID")));
    for (std::size_t r = 0; r < sk.size(); ++r) {
        const int id = num(sk.get(r, "Id"));
        if (id < 0 || sk.get(r, "Id").empty()) continue;
        T.names.skills.emplace(std::string(sk.get(r, "skill")), id);
        T.by_name.emplace(std::string(sk.get(r, "skill")), id);
    }
    std::unordered_map<std::string, std::size_t> desc_row;
    for (std::size_t r = 0; r < sd.size(); ++r) desc_row.emplace(std::string(sd.get(r, "skilldesc")), r);
    int calcs = 0;
    std::vector<std::string> bad;
    for (std::size_t r = 0; r < sk.size(); ++r) {
        const int id = num(sk.get(r, "Id"));
        if (id < 0 || sk.get(r, "Id").empty()) continue;
        if (std::size_t(id) >= T.rows.size()) T.rows.resize(std::size_t(id) + 1);
        auto& S = T.rows[std::size_t(id)];
        auto g = [&](const std::string& c) { return sk.get(r, c); };
        auto n = [&](const std::string& c) { return num(g(c)); };
        auto calc = [&](const std::string& c) {
            d2d::rules::Calc out;
            if (const auto e = g(c); !e.empty()) {
                std::string err;
                out = d2d::rules::compile_calc(e, T.names, &err);
                ++calcs;
                if (!err.empty()) bad.push_back(std::string(g("skill")) + " " + c + ": " + err);
            }
            return out;
        };
        S.id = id;
        S.name = g("skill"); S.cls = g("charclass"); S.desc = g("skilldesc");
        S.srvstfunc = n("srvstfunc"); S.srvdofunc = n("srvdofunc");
        S.anim = g("anim"); S.range = g("range");
        S.leftskill = g("leftskill") == "1"; S.passive = g("passive") == "1"; S.aura = g("aura") == "1";
        S.use_attack_rate = g("UseAttackRate") == "1"; S.in_town = g("InTown") == "1"; S.attack_no_mana = g("AttackNoMana") == "1";
        S.reqlevel = std::max(n("reqlevel"), 1); if (n("maxlvl") > 0) S.maxlvl = n("maxlvl");
        S.mana = n("mana"); S.lvlmana = n("lvlmana"); S.manashift = n("manashift"); S.minmana = n("minmana");
        S.tohit = n("ToHit"); S.levtohit = n("LevToHit"); S.tohit_calc = calc("ToHitCalc");
        for (int i = 0; i < 4; ++i) S.calc[std::size_t(i)] = calc("calc" + std::to_string(i + 1));
        for (int i = 0; i < 8; ++i) S.par[std::size_t(i)] = n("Param" + std::to_string(i + 1));
        S.hitshift = n("HitShift"); S.srcdam = g("SrcDam").empty() ? 128 : n("SrcDam"); S.srcdam_raw = n("SrcDam");
        S.srvmissile = g("srvmissile"); S.srvmissilea = g("srvmissilea"); S.perdelay = n("perdelay");
        S.summon = g("summon"); S.pettype = g("pettype"); S.petmax = calc("petmax"); S.target_corpse = g("TargetCorpse") == "1";
        for (int i = 0; i < 5; ++i) {
            S.sumskill[std::size_t(i)] = g("sumskill" + std::to_string(i + 1));
            S.sumsk_calc[std::size_t(i)] = calc("sumsk" + std::to_string(i + 1) + "calc");
        }
        S.result_flags = n("ResultFlags");
        static constexpr std::array<std::string_view, 6> kEl = { "fire", "ltng", "cold", "pois", "mag", "stun" };
        const auto et = std::ranges::find(kEl, g("EType"));
        S.etype = et == kEl.end() ? -1 : int(et - kEl.begin());
        S.emin = n("EMin"); S.emax = n("EMax"); S.elen = n("ELen");
        S.mindam = n("MinDam"); S.maxdam = n("MaxDam");
        for (int i = 0; i < 5; ++i) {
            const auto k = std::to_string(i + 1);
            S.emin_lev[std::size_t(i)] = n("EMinLev" + k); S.emax_lev[std::size_t(i)] = n("EMaxLev" + k);
            S.mindam_lev[std::size_t(i)] = n("MinLevDam" + k); S.maxdam_lev[std::size_t(i)] = n("MaxLevDam" + k);
            if (const auto ps = T.names.stats.find(std::string(g("passivestat" + k))); ps != T.names.stats.end())
                S.passive_stat[std::size_t(i)] = ps->second;
            S.passive_calc[std::size_t(i)] = calc("passivecalc" + k);
        }
        S.passive_itype = std::string(g("passiveitype"));
        for (int i = 0; i < 3; ++i) S.elen_lev[std::size_t(i)] = n("ELevLen" + std::to_string(i + 1));
        for (int i = 0; i < 6; ++i) {
            S.aura_calc[std::size_t(i)] = calc("aurastatcalc" + std::to_string(i + 1));
            if (const auto a = T.names.stats.find(std::string(g("aurastat" + std::to_string(i + 1)))); a != T.names.stats.end())
                S.aurastat[std::size_t(i)] = a->second;
        }
        S.auralen = calc("auralencalc"); S.aurarange = calc("aurarangecalc");
        S.aurastate = g("aurastate"); S.auratarget = g("auratargetstate"); S.prgdam = n("prgdam"); S.seqnum = n("seqnum");
        for (int i = 0; i < 3; ++i) {
            S.prgfunc[std::size_t(i)] = n("srvprgfunc" + std::to_string(i + 1));
            S.prgcalc[std::size_t(i)] = calc("prgcalc" + std::to_string(i + 1));
        }
        S.prgstack = g("prgstack") == "1";
        S.srvmissileb = g("srvmissileb"); S.srvmissilec = g("srvmissilec");
        S.edmg_sym = calc("EDmgSymPerCalc"); S.elen_sym = calc("ELenSymPerCalc"); S.dmg_sym = calc("DmgSymPerCalc");
        for (std::size_t c = 0; c < 7; ++c)
            if (S.cls == d2d::rules::kClassCode[c]) T.class_ids[c].push_back(id);
        if (const auto d = desc_row.find(S.desc); d != desc_row.end()) {
            S.page = num(sd.get(d->second, "SkillPage"));
            S.icon = num(sd.get(d->second, "IconCel"));
            S.str_name = sd.get(d->second, "str name");
        }
    }
    d2d::log::info("  Skills: {} rows, {} calcs, {} unreadable", T.rows.size(), calcs, bad.size());
    for (const auto& b : bad) d2d::log::info("  not implemented: calc {}", b);
}

// Act 1 town NPCs from the DS1's type-1 objects: id -> MonPreset.txt
// (Act 1 rows) Place -> MonStats row (by Id) -> its MonStatsEx's MonStats2
// row (monster_npc).
// Positions are in subtiles; a unit stands at its subtile's centre.
// ponytail: act 1 only, NU idle only; "place_*" spawn markers skipped.
// The level builder (Scene::builder): one build at a time, on its own MPQ
// handles and its own DRLG tables (the generator caches DT1 heads in them).
struct GameData::LevelBuilder {
    std::mutex m;                                       // held for a whole build; `mpqs` and `act1` are its
    std::optional<d2d::mpq::Stack> mpqs;
    std::unique_ptr<d2d::drlg::OutdoorAssets> act1;     // the act's DRLG tables (load_scene's, handed over)
    // What load_npcs read that a build needs: objects.txt (and its rows by
    // Id), Levels.txt, SoundEnviron.txt.
    d2d::txt::Table objects, levels, sound_env;
    std::unordered_map<std::string, std::size_t> obj_row;
    std::map<int, std::future<std::unique_ptr<Level>>> jobs;   // the main thread's: builds under way
};

// Footprints into the walk grid, centred on each unit's subtile.
// (Quest-gated units like Cain stay out of it: they're not always there.)
// ponytail: static — fine while NPCs only idle; moving units need a
// separate occupancy layer.
void stamp_footprints(Level& lv) {
    const int ww = lv.ds1.width() * 5, wh = lv.ds1.height() * 5;
    if (lv.walk.size() != std::size_t(ww) * std::size_t(wh)) return;
    for (const auto& n : lv.npcs) {
        if (!n.path.empty() || n.quest) continue;   // walkers don't hold a spot
        const int cx = int(n.x * 5), cy = int(n.y * 5);
        for (int y = cy - n.size_y / 2; y < cy - n.size_y / 2 + n.size_y; ++y)
            for (int x = cx - n.size_x / 2; x < cx - n.size_x / 2 + n.size_x; ++x)
                if (x >= 0 && y >= 0 && x < ww && y < wh)
                    lv.walk[std::size_t(y) * std::size_t(ww) + std::size_t(x)] |= 0x01;
    }
}

// A type-2 object at subtile (sx, sy): objects.txt Id `oid` (through
// game.exe's preset table) as an Npc in `into`, rolling a shrine's kind and
// a chest's trap and lock. `rgn`: the game's object seed (FUN_00546fa0).
void add_object(const GameData& scene, const d2d::txt::Table& objects, const std::unordered_map<std::string, std::size_t>& obj_row,
                Level& into, int oid, int sx, int sy, d2d::rules::Rng& rgn) {
    const auto it = obj_row.find(std::to_string(oid));
    if (oid == 0 || it == obj_row.end()) return;
    const auto r = it->second;
    Npc n;
    n.root   = "objects";
    n.code   = std::string(objects.get(r, "Token"));
    n.operate_fn = std::atoi(std::string(objects.get(r, "OperateFn")).c_str());
    if (objects.get(r, "Mode1") == "1") n.op_frames = std::atoi(std::string(objects.get(r, "FrameCnt1")).c_str());
    if (objects.get(r, "InitFn") == "1") {      // a shrine: which one (FUN_0054f9d0)
        // ponytail: seeded from the level and spot — game.exe rolls the
        // object's own seed and the game's object seed (not emulated).
        d2d::rules::Rng obj(std::uint32_t(sx * 7919 + sy) ^ scene.map_seed);
        n.shrine = d2d::rules::roll_shrine(scene.shrines, std::atoi(std::string(objects.get(r, "Parm0")).c_str()), into.id, obj, rgn);
    }
    if (objects.get(r, "InitFn") == "3") {      // a chest: its trap and lock (FUN_0054fcb0)
        d2d::rules::Rng obj(std::uint32_t(sx * 7919 + sy) ^ scene.map_seed);   // ponytail: as the shrines'
        const auto& al = scene.area_level;
        const int mlvl1 = std::size_t(into.id) < al.size() ? al[std::size_t(into.id)][3] : 1;
        const auto c = d2d::rules::roll_chest(mlvl1, objects.get(r, "Lockable") == "1", obj);
        n.trap = c.trap; n.locked = c.locked;
        if (n.locked) if (auto v = lookup_string(scene, "lockedchest")) n.name = u16_to_latin1(*v);
    }
    n.base_w = "hth";
    for (std::size_t m = 0; m < 8; ++m) n.lit[m] = std::uint8_t(std::atoi(std::string(objects.get(r, "Lit" + std::to_string(m))).c_str()));
    const bool on = objects.get(r, "Mode2") == "1" && !objects.get(r, "Lit2").empty()
                 && objects.get(r, "Lit2") != "0" && n.operate_fn != 2 && n.operate_fn != 4;   // shrines / chests: NU until used
    n.mode   = on ? "ON" : "NU";
    // Hover name when selectable in its start mode (Selectable0 = NU,
    // 2 = ON): objects.txt Name through the string tables.
    if (objects.get(r, on ? "Selectable2" : "Selectable0") == "1") {
        const std::string key(objects.get(r, "Name"));
        auto v = lookup_string(scene, key);
        n.name = v ? u16_to_latin1(*v) : key;
    }
    // Blocks walking in its start mode (HasCollision0 = NU, 2 = ON).
    if (objects.get(r, on ? "HasCollision2" : "HasCollision0") == "1") {
        n.size_x = std::atoi(std::string(objects.get(r, "SizeX")).c_str());
        n.size_y = std::atoi(std::string(objects.get(r, "SizeY")).c_str());
    }
    for (std::size_t l = 0; l < 16; ++l)
        if (objects.get(r, kLayerCode[l]) == "1") n.comp[l] = "lit";
    if (n.code.empty()) return;
    n.x = (float(sx) + 0.5f) / 5;
    n.y = (float(sy) + 0.5f) / 5;
    into.npcs.push_back(std::move(n));
}

void load_npcs(Scene& scene, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* n) {
        auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt");
        return b ? d2d::txt::Table(*b) : d2d::txt::Table{};
    };
    const auto preset = txt("MonPreset"), ms = txt("MonStats"), ms2 = txt("MonStats2");
    if (preset.size() == 0 || ms2.size() == 0) return;
    std::vector<std::size_t> act1;
    for (std::size_t r = 0; r < preset.size(); ++r)
        if (preset.get(r, "Act") == "1") act1.push_back(r);
    const auto ms2_row = id_rows(ms2), ms_row = id_rows(ms);
    auto monster = [&](std::size_t row) { return monster_npc(scene, ms, ms2, ms2_row, row); };
    for (const auto& o : scene.town.ds1.objects()) {
        if (o.type != 1 || o.id < 0 || std::size_t(o.id) >= act1.size()) continue;
        const std::string place(preset.get(act1[std::size_t(o.id)], "Place"));
        const auto it = ms_row.find(place);
        if (it == ms_row.end()) continue;            // place_* markers etc.
        auto n = monster(it->second);
        if (n.code.empty()) continue;
        n.x = (float(o.x) + 0.5f) / 5;
        n.y = (float(o.y) + 0.5f) / 5;
        for (const auto& pt : o.path)
            n.path.emplace_back((float(pt.x) + 0.5f) / 5, (float(pt.y) + 0.5f) / 5);
        scene.town.npcs.push_back(std::move(n));
    }

    // Mercenaries: hireling.txt (LoD rows, Version 100) by Id -> the MonStats
    // row whose hcIdx is its Class; name keys run from NameFirst.
    if (const auto hire = txt("hireling"); hire.size() > 0) {
        std::unordered_map<int, std::size_t> by_hc;
        for (std::size_t r = 0; r < ms.size(); ++r) by_hc.emplace(std::atoi(std::string(ms.get(r, "hcIdx")).c_str()), r);
        for (std::size_t r = 0; r < hire.size(); ++r) {
            if (hire.get(r, "Version") != "100") continue;
            const int id = std::atoi(std::string(hire.get(r, "Id")).c_str());
            const auto m = by_hc.find(std::atoi(std::string(hire.get(r, "Class")).c_str()));
            if (m == by_hc.end() || scene.mercs.contains(id)) continue;
            auto n = monster(m->second);
            if (n.code.empty()) continue;
            scene.mercs.emplace(id, Scene::Merc{ std::move(n), std::string(hire.get(r, "NameFirst")) });
        }
    }

    // Type-2 objects: id -> objects.txt Id through game.exe's own preset
    // table (obj_preset.hpp), then that row's Token and layer flags. Start
    // mode: ON for things with a light in ON (torches, fires, the camp
    // waypoint), else NU. ponytail: D2 sets it per object in its InitFn.
    const auto objects = txt("objects");
    // Shrines.txt, and each level's area level (chests' treasure class).
    const auto shr = txt("Shrines"), lvs = txt("Levels");
    auto num = [](std::string_view v) { return std::atoi(std::string(v).c_str()); };
    for (std::size_t r = 0; r < shr.size(); ++r) {
        auto g = [&](const char* c) { return num(shr.get(r, c)); };
        scene.shrines.push_back({ g("Code"), g("Arg0"), g("Arg1"), g("Duration in frames"), g("reset time in minutes"), g("effectclass"), g("LevelMin") });
    }
    for (std::size_t r = 0; r < lvs.size(); ++r) {
        const int id = num(lvs.get(r, "Id"));
        if (id < 0 || id > 1000) continue;
        if (std::size_t(id) >= scene.area_level.size()) scene.area_level.resize(std::size_t(id) + 1);
        scene.area_level[std::size_t(id)] = { num(lvs.get(r, "MonLvl1Ex")), num(lvs.get(r, "MonLvl2Ex")), num(lvs.get(r, "MonLvl3Ex")), num(lvs.get(r, "MonLvl1")) };
    }
    std::unordered_map<std::string, std::size_t> obj_row;
    for (std::size_t r = 0; r < objects.size(); ++r) obj_row.emplace(std::string(objects.get(r, "Id")), r);
    d2d::rules::Rng rgn(scene.map_seed);            // the game's object seed (FUN_00546fa0)
    auto add = [&](Level& into, int oid, int sx, int sy) { add_object(scene, objects, obj_row, into, oid, sx, sy, rgn); };
    {                                               // a chest trap's fires (5 / 7: objects 162 and 160)
        Level tmp;
        add(tmp, 162, 0, 0);
        add(tmp, 160, 0, 0);
        for (std::size_t k = 0; k < tmp.npcs.size() && k < 2; ++k) scene.trap_fires[k] = std::move(tmp.npcs[k]);
        Level tp;
        add(tp, 59, 0, 0);                          // the town portal
        if (!tp.npcs.empty()) scene.town_portal = std::move(tp.npcs[0]);
    }
    for (const auto& o : scene.town.ds1.objects())
        if (o.type == 2 && o.id >= 0 && o.id < 150) add(scene.town, kObjPreset[0][std::size_t(o.id)], o.x, o.y);   // act 1

    // What a level build reads later (build_level).
    if (scene.builder) {
        scene.builder->objects = objects;
        scene.builder->obj_row = obj_row;
        scene.builder->levels = lvs;
        scene.builder->sound_env = txt("SoundEnviron");
    }

    stamp_footprints(scene.town);

    // Deckard Cain (cain5, hcIdx 265 = 0x109, whose menu has "identify
    // items"), in camp once Act 1 quest 4 is done. On the rescue itself
    // a1q4.cpp (FUN_00596de0 -> FUN_00592960) spawns him where the player
    // came back through the portal.
    // ponytail: where a game with the quest already done places him isn't
    // located; he stands 3 subtiles off the town start, like the
    // Tristram spawn's offset (FUN_00593290).
    for (std::size_t r = 0; r < ms.size() && scene.town.start.first >= 0; ++r) {
        if (ms.get(r, "hcIdx") != "265") continue;
        auto n = monster(r);
        if (n.code.empty()) break;
        n.quest = 4;
        std::tie(n.x, n.y) = scene.town.nearest_free(scene.town.start.first + 0.6f, scene.town.start.second + 0.6f);
        scene.town.npcs.push_back(std::move(n));
        break;
    }
}

// Excel tables + the derived composite data: the component table and each
// class's starting-gear appearance (CharStats.txt item1..: "rarm" item in
// the right hand, a "larm" shield on the shield layer; body parts "lit").
void load_composite_data(Scene& scene, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* n) {
        auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt");
        return b ? d2d::txt::Table(*b) : d2d::txt::Table{};
    };
    const auto types = txt("ItemTypes"), weapons = txt("weapons"), armor = txt("armor"),
               misc = txt("misc"), charstats = txt("CharStats");
    if (types.size() == 0 || weapons.size() == 0) return;
    scene.comp = d2d::compcode::build(types, weapons, armor, misc);
    scene.item_pieces = d2d::compcode::pieces(weapons, armor, misc);
    auto col = [&](const char* n, const char* c, bool all) {
        std::vector<std::string> v;
        if (auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt")) {
            const d2d::txt::Table t(*b, all);
            for (std::size_t r = 0; r < t.size(); ++r) v.emplace_back(t.get(r, c));
        }
        return v;
    };
    scene.item_colours = { col("Colors", "Code", false), col("UniqueItems", "chrtransform", false), col("SetItems", "chrtransform", false),
                           col("MagicPrefix", "transformcolor", true), col("MagicSuffix", "transformcolor", true), col("AutoMagic", "transformcolor", true),
                           d2d::compcode::gem_colours(types, misc, txt("gems")), col("UniqueItems", "invtransform", false),
                           col("SetItems", "invtransform", false) };
    int t = 1;
    for (const char* n : { "grey", "grey2", "gold", "brown", "greybrown", "invgrey", "invgrey2", "invgreybrown" })
        if (auto b = mpqs.try_read(std::string(R"(data\global\items\palette\)") + n + ".dat"); b && b->size() >= 21 * 256) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(b->data());
            scene.colormaps[std::size_t(t++)].assign(p, p + 21 * 256);
        } else ++t;
    scene.item_types.emplace(types);
    if (const auto ov = txt("Overlay"); ov.size() > 0)
        for (std::size_t r = 0; r < ov.size(); ++r) {
            auto n = [&](const char* c) { return std::atoi(std::string(ov.get(r, c)).c_str()); };
            Scene::OverlayInfo o;
            o.file = std::string(ov.get(r, "Filename"));
            o.frames = std::max(n("Frames"), 1); o.x = n("Xoffset"); o.y = n("Yoffset"); o.rate = n("AnimRate");
            o.trans = n("Trans"); o.radius = n("Radius"); o.predraw = n("PreDraw") != 0;
            o.height = { n("Height1"), n("Height2"), n("Height3"), n("Height4") };
            scene.overlays.emplace(std::string(ov.get(r, "overlay")), std::move(o));
        }
    auto overlay = [&](std::string_view name) -> const Scene::OverlayInfo* {
        const auto it = scene.overlays.find(std::string(name));
        return it == scene.overlays.end() || it->second.file.empty() || it->second.file == "null" ? nullptr : &it->second;
    };
    if (const auto st = txt("States"); st.size() > 0)
        for (std::size_t r = 0; r < st.size(); ++r) {
            Scene::StateInfo i;
            const auto shift = st.get(r, "colorshift");
            i.shift = shift.empty() ? -1 : std::atoi(std::string(shift).c_str());
            i.pri = std::atoi(std::string(st.get(r, "colorpri")).c_str());
            for (int k = 0; k < 4; ++k) i.over[std::size_t(k)] = overlay(st.get(r, ("overlay" + std::to_string(k + 1)).c_str()));
            i.cast = overlay(st.get(r, "castoverlay"));
            i.item_type = std::string(st.get(r, "itemtype"));
            const std::string trans(st.get(r, "itemtrans"));
            for (std::size_t c = 0; c < scene.item_colours.codes.size(); ++c) if (!trans.empty() && scene.item_colours.codes[c] == trans) i.item_colour = int(c);
            scene.states.emplace(std::string(st.get(r, "state")), std::move(i));
        }
    if (auto pb = mpqs.try_read(R"(data\global\palette\ACT1\Pal.pl2)"); pb && pb->size() >= 0x53500 + 111 * 256) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(pb->data()) + 0x53500;
        scene.colour_shifts.assign(p, p + 111 * 256);
    }

    // Char panel: next-level experience (row "<level>", same for every
    // class) and the expansion's resistance penalty per difficulty.
    if (const auto xp = txt("experience"); xp.size() > 0)
        for (std::size_t r = 0; r < xp.size(); ++r)
            if (const auto l = xp.get(r, "Level"); !l.empty() && l[0] >= '0' && l[0] <= '9') {
                const auto lv = std::size_t(std::stoi(std::string(l)));
                if (lv >= scene.exp_next.size()) scene.exp_next.resize(lv + 1, -1);
                scene.exp_next[lv] = std::stoll(std::string(xp.get(r, "Amazon")));
            }
    if (const auto dl = txt("DifficultyLevels"); dl.size() >= 3)
        for (std::size_t r = 0; r < 3; ++r)
            scene.resist_penalty[r] = std::stoll(std::string(dl.get(r, "ResistPenalty")));

    // Items. ItemStatCost.txt only exists in the 1.14d patch data.
    if (const auto isc = txt("ItemStatCost"); isc.size() > 0)
        scene.item_tables = d2d::d2s::ItemTables::from(isc, armor, weapons, misc);
    for (const auto* t : { &weapons, &armor, &misc })
        for (std::size_t r = 0; r < t->size(); ++r)
            scene.rules.item_info[std::string(t->get(r, "code"))] = {
                std::string(t->get(r, "invfile")),
                std::max(1, std::atoi(std::string(t->get(r, "invwidth")).c_str())),
                std::max(1, std::atoi(std::string(t->get(r, "invheight")).c_str())),
                std::string(t->get(r, "namestr")), std::string(t->get(r, "type")),
                t == &armor ? 1 : t == &weapons ? 2 : 0,
                t == &armor && !t->get(r, "belt").empty() ? std::atoi(std::string(t->get(r, "belt")).c_str()) : -1,
                t == &weapons && t->get(r, "2handed") == "1", t == &weapons && t->get(r, "1or2handed") == "1",
                std::atoi(std::string(t->get(r, "reqstr")).c_str()), std::atoi(std::string(t->get(r, "reqdex")).c_str()),
                std::atoi(std::string(t->get(r, "levelreq")).c_str()), std::string(t->get(r, "flippyfile")),
                std::string(t->get(r, "dropsound")), std::atoi(std::string(t->get(r, "dropsfxframe")).c_str()) };
    for (std::size_t r = 0; r < types.size(); ++r) {
        const std::string code(types.get(r, "Code"));
        scene.rules.types[code] = {
            { std::string(types.get(r, "Equiv1")), std::string(types.get(r, "Equiv2")) },
            { d2d::rules::body_slot(types.get(r, "BodyLoc1")), d2d::rules::body_slot(types.get(r, "BodyLoc2")) },
            std::string(types.get(r, "Class")), types.get(r, "Beltable") == "1",
            types.get(r, "Magic") == "1", types.get(r, "Rare") == "1", types.get(r, "Normal") == "1" };
        auto& g = scene.type_invgfx[code];
        for (int i = 0; i < 6; ++i) g[std::size_t(i)] = std::string(types.get(r, "InvGfx" + std::to_string(i + 1)));
    }
    {
        static constexpr const char* kVendorCol[17] = { "Akara", "Gheed", "Charsi", "Fara", "Lysander", "Drognan",
            "Hralti", "Alkor", "Ormus", "Elzix", "Asheara", "Cain", "Halbu", "Jamella", "Malah", "Larzuk", "Drehya" };
        std::vector<std::pair<std::string, int>> weapons_by_level, armor_by_level;
        for (const auto* t : { &armor, &weapons, &misc })
            for (std::size_t r = 0; r < t->size(); ++r) {
                const std::string code(t->get(r, "code"));
                auto n = [&](std::string c) { return std::atoi(std::string(t->get(r, c)).c_str()); };
                const bool two = t == &weapons && t->get(r, "2handed") == "1" && t->get(r, "1or2handed") != "1";
                scene.rules.item_base[code] = { t == &armor ? n("minac") : 0, t == &armor ? n("maxac") : 0, n("cost"),
                                          // armor.txt's first mindam/maxdam: a shield's smite, boots' kick damage
                                          t == &misc ? 0 : n(two ? "2handmindam" : "mindam"),
                                          t == &misc ? 0 : n(two ? "2handmaxdam" : "maxdam"),
                                          t == &misc ? 0 : n("StrBonus"), t == &misc ? 0 : n("DexBonus"),
                                          t == &weapons ? n("speed") : 0, t == &armor ? n("block") : 0,
                                          t->get(r, "stackable") == "1", n("level"),
                                          t == &misc ? 0 : n("durability"), n("gamble cost"), n("minstack"), n("maxstack"),
                                          std::string(t->get(r, "normcode")), std::string(t->get(r, "ubercode")),
                                          std::string(t->get(r, "ultracode")), std::string(t->get(r, "BetterGem")) };
                if (t->get(r, "spawnable") != "1") continue;
                scene.rules.item_rarity[code] = n("rarity");
                if (t != &misc && n("level") > 0) (t == &weapons ? weapons_by_level : armor_by_level).emplace_back(code, n("level"));
                for (std::size_t v = 0; v < 17; ++v) {
                    const std::string V = kVendorCol[v];
                    d2d::rules::VendorItem vi{ code, n(V + "Min"), n(V + "Max"), n(V + "MagicMin"), n(V + "MagicMax"),
                                          n(V + "MagicLvl"), t->get(r, "PermStoreItem") == "1" };
                    if (vi.max > 0 || vi.magic_max > 0) scene.rules.vendor_items[v].push_back(std::move(vi));
                }
            }
        // Potions (misc.txt stat1/calc1, stat2/calc2, len).
        for (std::size_t r = 0; r < misc.size(); ++r) {
            auto n = [&](std::string c) { return std::atoi(std::string(misc.get(r, c)).c_str()); };
            const auto s1 = misc.get(r, "stat1"), s2 = misc.get(r, "stat2");
            d2d::rules::Tables::Potion p{ 0, 0, n("len") };
            if (s1 == "hpregen") p.life = n("calc1");
            else if (s1 == "manarecovery") p.mana = n("calc1");
            else if (s1 == "hitpoints" && s2 == "mana") { p.life = n("calc1"); p.mana = n("calc2"); p.percent = true; }
            else continue;
            scene.rules.potions.emplace(std::string(misc.get(r, "code")), p);
        }
        // Drops: TreasureClassEx, ItemRatio (the LoD, non-class rows), auto classes.
        const auto tcx = txt("TreasureClassEx");
        for (std::size_t r = 0; r < tcx.size(); ++r) {
            auto n = [&](std::string c) { return std::atoi(std::string(tcx.get(r, c)).c_str()); };
            d2d::rules::TreasureClass c{ std::max(n("Picks"), 1), n("NoDrop"), { n("Unique"), n("Set"), n("Rare"), n("Magic") }, {} };
            for (int i = 1; i <= 10; ++i) {
                auto item = std::string(tcx.get(r, "Item" + std::to_string(i)));
                std::erase(item, '"');
                if (!item.empty()) c.items.emplace_back(std::move(item), n("Prob" + std::to_string(i)));
            }
            scene.rules.treasure.emplace(std::string(tcx.get(r, "Treasure Class")), std::move(c));
        }
        const auto ratio = txt("ItemRatio");
        for (std::size_t r = 0; r < ratio.size(); ++r) {
            if (ratio.get(r, "Version") != "1" || ratio.get(r, "Class Specific") != "0") continue;
            auto n = [&](std::string c) { return std::atoi(std::string(ratio.get(r, c)).c_str()); };
            auto& q = scene.rules.quality_ratio[ratio.get(r, "Uber") == "1" ? 1 : 0];
            q[0] = { n("Unique"), n("UniqueDivisor"), n("UniqueMin") };
            q[1] = { n("Set"), n("SetDivisor"), n("SetMin") };
            q[2] = { n("Rare"), n("RareDivisor"), n("RareMin") };
            q[3] = { n("Magic"), n("MagicDivisor"), n("MagicMin") };
            q[4] = { n("HiQuality"), n("HiQualityDivisor"), 0 };
            q[5] = { n("Normal"), n("NormalDivisor"), 0 };
        }
        d2d::rules::add_auto_treasure(scene.rules, weapons_by_level, armor_by_level);
        for (auto [path, into] : { std::pair{ R"(data\global\ui\PANEL\buysell.dc6)", &scene.store_panel },
                                   { R"(data\global\ui\PANEL\buyselltabs.dc6)", &scene.store_tabs },
                                   { R"(data\global\ui\PANEL\buysellbtn.dc6)", &scene.store_buttons },
                                   { R"(data\global\ui\PANEL\goldcoinbtn.dc6)", &scene.gold_coin } })
            if (auto b = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*b);
    }
    auto keys = [&](const char* n, const char* col, bool all) {
        std::vector<std::string> v;
        if (auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt")) {
            const d2d::txt::Table t(*b, all);
            for (std::size_t r = 0; r < t.size(); ++r) v.emplace_back(t.get(r, col));
        }
        return v;
    };
    if (auto b = mpqs.try_read(R"(data\global\excel\ItemStatCost.txt)")) {
        const d2d::txt::Table t(*b);
        auto num = [&](std::size_t r, const char* c) { return std::atoi(std::string(t.get(r, c)).c_str()); };
        for (std::size_t r = 0; r < t.size(); ++r) {
            const auto id = std::size_t(num(r, "ID"));
            if (id >= scene.stat_desc.size()) scene.stat_desc.resize(id + 1);
            scene.stat_desc[id] = { num(r, "descpriority"), num(r, "descfunc"), num(r, "descval"),
                                    num(r, "op"), num(r, "op param"), num(r, "dgrp"), num(r, "dgrpfunc"),
                                    num(r, "dgrpval"),
                                    std::string(t.get(r, "descstrpos")), std::string(t.get(r, "descstrneg")),
                                    std::string(t.get(r, "descstr2")), std::string(t.get(r, "dgrpstrpos")),
                                    std::string(t.get(r, "dgrpstrneg")), std::string(t.get(r, "dgrpstr2")) };
        }
    }
    {
        // Socket bonuses: gems.txt mods -> Properties.txt funcs -> stats.
        // Property funcs used by gems: 1/3 value, 15/16 min/max, 17 param,
        // 5/6/7 min/max/% damage. ponytail: other funcs dropped.
        std::unordered_map<std::string, int> stat_id;
        if (auto b = mpqs.try_read(R"(data\global\excel\ItemStatCost.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r)
                stat_id[std::string(t.get(r, "Stat"))] = std::atoi(std::string(t.get(r, "ID")).c_str());
        }
        std::unordered_map<std::string, std::vector<std::pair<int, int>>> prop;   // code -> (func, stat)
        const auto pt = txt("Properties");
        for (std::size_t r = 0; r < pt.size(); ++r)
            for (int i = 1; i <= 7; ++i) {
                const int f = std::atoi(std::string(pt.get(r, "func" + std::to_string(i))).c_str());
                if (!f) continue;
                const auto st = stat_id.find(std::string(pt.get(r, "stat" + std::to_string(i))));
                prop[std::string(pt.get(r, "code"))].emplace_back(f, st == stat_id.end() ? -1 : st->second);
                scene.rules.properties[std::string(pt.get(r, "code"))].push_back(
                    { f, st == stat_id.end() ? -1 : st->second,
                      std::atoi(std::string(pt.get(r, "val" + std::to_string(i))).c_str()) });
            }
        const auto gt = txt("gems");
        static constexpr const char* kSlot[3] = { "weaponMod", "helmMod", "shieldMod" };
        for (std::size_t r = 0; r < gt.size(); ++r)
            for (int k = 0; k < 3; ++k)
                for (int m = 1; m <= 3; ++m) {
                    const std::string pre = std::string(kSlot[k]) + std::to_string(m);
                    const auto it = prop.find(std::string(gt.get(r, pre + "Code")));
                    if (it == prop.end()) continue;
                    auto num = [&](const char* c) { return std::atoi(std::string(gt.get(r, pre + c)).c_str()); };
                    const int par = num("Param"), mn = num("Min"), mx = num("Max");
                    auto& out = scene.gem_props[std::string(gt.get(r, "code"))][std::size_t(k)];
                    for (auto [f, st] : it->second) {
                        switch (f) {
                            case 1: case 3: if (st >= 0) out.push_back({ st, par, mn }); break;
                            case 15: if (st >= 0) out.push_back({ st, 0, mn }); break;
                            case 16: if (st >= 0) out.push_back({ st, 0, mx }); break;
                            case 17: if (st >= 0) out.push_back({ st, 0, par }); break;
                            case 5: out.push_back({ 21, 0, mn }); break;
                            case 6: out.push_back({ 22, 0, mx }); break;
                            case 7: out.push_back({ 17, 0, mn }); out.push_back({ 18, 0, mn }); break;
                            default: break;
                        }
                    }
                }
    }
    {
        std::unordered_map<std::string, std::string> desc_name;     // SkillDesc key -> "str name"
        if (auto b = mpqs.try_read(R"(data\global\excel\SkillDesc.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r)
                desc_name[std::string(t.get(r, "skilldesc"))] = std::string(t.get(r, "str name"));
        }
        static constexpr std::string_view kCls[] = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
        if (auto b = mpqs.try_read(R"(data\global\excel\skills.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r) {
                const auto id = std::size_t(std::atoi(std::string(t.get(r, "Id")).c_str()));
                if (id >= scene.skill_name.size()) { scene.skill_name.resize(id + 1); scene.skill_class.resize(id + 1, -1); }
                scene.skill_name[id] = desc_name[std::string(t.get(r, "skilldesc"))];
                scene.rules.skill_id[std::string(t.get(r, "skill"))] = int(id);
                const auto cc = t.get(r, "charclass");
                for (int c = 0; c < 7; ++c) if (cc == kCls[c]) scene.skill_class[id] = c;
            }
        }
        const auto cs = txt("CharStats");
        for (std::size_t r = 0; r < std::min<std::size_t>(cs.size(), 7); ++r) {
            if (const auto w = std::atoi(std::string(cs.get(r, "WalkVelocity")).c_str()); w > 0) scene.walk_velocity[r] = w;
            if (const auto v = std::atoi(std::string(cs.get(r, "RunVelocity")).c_str()); v > 0) scene.run_velocity[r] = v;
        }
        for (std::size_t r = 0; r < std::min<std::size_t>(cs.size(), 7); ++r)
            scene.class_strs[r] = { std::string(cs.get(r, "StrAllSkills")),
                                    { std::string(cs.get(r, "StrSkillTab1")), std::string(cs.get(r, "StrSkillTab2")),
                                      std::string(cs.get(r, "StrSkillTab3")) },
                                    std::string(cs.get(r, "StrClassOnly")) };
    }
    // animdata.d2: blocks of u32 count + count * 160-byte records
    // {char name[8], u32 frames/dir, u32 speed, u8 events[144]}.
    if (auto b = mpqs.try_read(R"(data\global\animdata.d2)"))
        for (std::size_t p = 0; p + 4 <= b->size();) {
            std::uint32_t n; std::memcpy(&n, b->data() + p, 4); p += 4;
            for (std::uint32_t i = 0; i < n && p + 160 <= b->size(); ++i, p += 160) {
                std::string name(reinterpret_cast<const char*>(b->data() + p), 8);
                name.resize(std::strlen(name.c_str()));
                for (auto& ch : name) ch = char(std::toupper(ch));
                Scene::AnimInfo a;
                std::memcpy(&a.frames, b->data() + p + 8, 4);
                std::memcpy(&a.speed, b->data() + p + 12, 4);
                for (std::uint32_t f = 0; f < a.frames && f < 144 && a.action < 0; ++f)
                    if (std::to_integer<int>(b->data()[p + 16 + f]) != 0) a.action = int(f);
                scene.anim_data.emplace(std::move(name), a);
            }
        }
    if (const auto bt = txt("belts"); bt.size() >= 14)
        for (std::size_t b = 0; b < 7; ++b) {
            const std::size_t r = 7 + b;                 // the 800x600 half
            auto& B = scene.belts[b];
            B.boxes = std::clamp(std::atoi(std::string(bt.get(r, "numboxes")).c_str()), 0, 16);
            for (int i = 0; i < B.boxes; ++i)
                for (int k = 0; k < 4; ++k) {
                    static constexpr const char* kSide[4] = { "left", "right", "top", "bottom" };
                    B.box[std::size_t(i)][std::size_t(k)] = std::atoi(std::string(
                        bt.get(r, "box" + std::to_string(i + 1) + kSide[k])).c_str());
                }
        }
    if (auto b = mpqs.try_read(R"(data\global\ui\PANEL\ctrlpnl_popbelt.dc6)")) scene.popbelt = d2d::dc6::Sprite(*b);
    if (auto b = mpqs.try_read(R"(data\global\ui\CURSOR\focus16.dc6)")) scene.focus16 = d2d::dc6::Sprite(*b);
    {
        static constexpr std::string_view kLevelTypeName[] = {
            "None", "1 Town", "1 Wilderness", "1 Cave", "1 Crypt", "1 Monestary", "1 Courtyard", "1 Barracks",
            "1 Jail", "1 Cathedral", "1 Catacombs", "1 Tristram", "2 Town", "2 Sewer", "2 Harem", "2 Basement",
            "2 Desert", "2 Tomb", "2 Lair", "2 Arcane", "3 Town", "3 Jungle", "3 Kurast", "3 Spider", "3 Dungeon",
            "3 Sewer", "4 Town", "4 Mesa", "4 Lava", "5 Town", "5 Siege", "5 Barricade", "5 Temple", "5 Ice",
            "5 Baal", "5 Lava" };
        static constexpr std::string_view kOrientName[] = {
            "fl", "wl", "wr", "wtlr", "wtll", "wtr", "wbl", "wbr", "wld", "wrd", "wle", "wre", "co", "sh", "tr",
            "rf", "ld", "rd", "fd", "fi" };
        const auto am = txt("AutoMap");
        auto idx = [](auto& names, std::string_view v) {
            for (std::size_t i = 0; i < std::size(names); ++i) if (names[i] == v) return int(i);
            return -1;
        };
        auto num = [&](std::size_t r, std::string_view c) {
            const auto v = am.get(r, c);
            return v.empty() ? -1 : std::atoi(std::string(v).c_str());
        };
        for (std::size_t r = 0; r < am.size(); ++r) {
            Scene::AutomapRule rule;
            rule.level_type = idx(kLevelTypeName, am.get(r, "LevelName"));
            rule.orientation = idx(kOrientName, am.get(r, "TileName"));
            if (rule.level_type < 0 || rule.orientation < 0) continue;
            rule.main = num(r, "Style");
            rule.sub0 = num(r, "StartSequence");
            rule.sub1 = num(r, "EndSequence");
            for (const char* c : { "Cel1", "Cel2", "Cel3", "Cel4" })
                if (const int v = num(r, c); v >= 0) rule.cels.push_back(v);
            if (!rule.cels.empty()) scene.automap_rules.push_back(std::move(rule));
        }
        if (auto b = mpqs.try_read(R"(data\global\ui\AutoMap\MaxiMap.dc6)")) scene.automap_cels = d2d::dc6::Sprite(*b);
        const auto lv = txt("Levels");
        for (std::size_t r = 0; r < lv.size(); ++r)
            if (lv.get(r, "Id") == "1") scene.town.type = std::atoi(std::string(lv.get(r, "LevelType")).c_str());
        for (std::size_t r = 0; r < lv.size(); ++r) {
            const auto wp = lv.get(r, "Waypoint");
            const int act = std::atoi(std::string(lv.get(r, "Act")).c_str());
            if (wp.empty() || wp == "255" || act < 0 || act > 4) continue;
            scene.waypoint_levels[std::size_t(act)].push_back({ std::atoi(std::string(wp).c_str()),
                std::atoi(std::string(lv.get(r, "Id")).c_str()), std::string(lv.get(r, "LevelName")) });
        }
        for (auto& a : scene.waypoint_levels) std::ranges::sort(a, {}, &Scene::WaypointLevel::wp);
        for (auto [path, into] : { std::pair{ R"(data\global\ui\menu\waygatebackground.dc6)", &scene.wp_bg },
                                   { R"(data\global\ui\menu\waygateicons.dc6)", &scene.wp_icons },
                                   { R"(data\global\ui\menu\waygatetabs.dc6)", &scene.wp_tabs[0] },
                                   { R"(data\global\ui\menu\expwaygatetabs.dc6)", &scene.wp_tabs[1] } })
            if (auto b = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*b);
    }
    // The Rogue Encampment (Levels.txt Id 1): automap Layer,
    // and SoundEnv -> SoundEnviron Song / Day Ambience (Sounds.txt indices).
    {
        const auto lv = txt("Levels"), se = txt("SoundEnviron");
        for (std::size_t r = 0; r < lv.size(); ++r) {
            const auto id = lv.get(r, "Id");
            Level* into = id == "1" ? &scene.town : nullptr;     // the others: build_level
            if (!into) continue;
            into->name = std::string(lv.get(r, "LevelName"));
            into->layer = std::atoi(std::string(lv.get(r, "Layer")).c_str());
            into->light = level_light(lv.get(r, "Intensity"), lv.get(r, "Red"), lv.get(r, "Green"), lv.get(r, "Blue"));
            into->rain = lv.get(r, "Rain") == "1";
            const auto env = lv.get(r, "SoundEnv");
            for (std::size_t e = 0; e < se.size(); ++e)
                if (se.get(e, "Index") == env) {
                    into->song = std::atoi(std::string(se.get(e, "Song")).c_str());
                    into->ambience = std::atoi(std::string(se.get(e, "Day Ambience")).c_str());
                    into->night_ambience = std::atoi(std::string(se.get(e, "Night Ambience")).c_str());
                    into->day_event = std::atoi(std::string(se.get(e, "Day Event")).c_str());
                    into->night_event = std::atoi(std::string(se.get(e, "Night Event")).c_str());
                    into->event_delay = std::atoi(std::string(se.get(e, "Event Delay")).c_str());
                }
        }
    }
    if (const auto st = txt("Sounds"); st.size() > 0)
        for (std::size_t r = 0; r < st.size(); ++r) {
            const int i = std::atoi(std::string(st.get(r, "Index")).c_str());
            if (i < 0 || i > 20000) continue;
            if (std::size_t(i) >= scene.sounds.size()) scene.sounds.resize(std::size_t(i) + 1);
            scene.sounds[std::size_t(i)] = { std::string(st.get(r, "FileName")),
                                             std::atoi(std::string(st.get(r, "Volume")).c_str()),
                                             st.get(r, "Loop") == "1", st.get(r, "Music Vol") == "1",
                                             std::atoi(std::string(st.get(r, "Fade In")).c_str()),
                                             std::atoi(std::string(st.get(r, "Fade Out")).c_str()),
                                             std::atoi(std::string(st.get(r, "Group Size")).c_str()),
                                             { std::atoi(std::string(st.get(r, "Block 1")).c_str()), std::atoi(std::string(st.get(r, "Block 2")).c_str()),
                                               std::atoi(std::string(st.get(r, "Block 3")).c_str()) } };
            scene.sound_index.emplace(std::string(st.get(r, "Sound")), i);
        }
    if (auto t = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.tbl)"))
        if (auto d = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.dc6)"))
            scene.font_formal11 = d2d::font::Font(*t, d2d::dc6::Sprite(*d));
    auto& nm = scene.item_names;
    nm.unique   = keys("UniqueItems", "index", false);
    scene.unique_inv = keys("UniqueItems", "invfile", false);
    scene.set_inv    = keys("SetItems", "invfile", false);
    nm.set      = keys("SetItems", "index", false);
    nm.prefix   = keys("MagicPrefix", "Name", true);
    {
        auto pairs = [&](const char* n, const char* mul, const char* add, bool all) {
            std::vector<std::pair<int, int>> v;
            if (auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt")) {
                const d2d::txt::Table t(*b, all);
                for (std::size_t r = 0; r < t.size(); ++r)
                    v.emplace_back(std::atoi(std::string(t.get(r, mul)).c_str()), std::atoi(std::string(t.get(r, add)).c_str()));
            }
            return v;
        };
        scene.rules.prefix_cost = pairs("MagicPrefix", "multiply", "add", true);
        scene.rules.suffix_cost = pairs("MagicSuffix", "multiply", "add", true);
        scene.rules.unique_cost = pairs("UniqueItems", "cost mult", "cost add", false);
        scene.rules.set_cost    = pairs("SetItems", "cost mult", "cost add", false);
        // Item generation (components/rules generate_item / gamble_item).
        auto num = [](std::string_view v) { return std::atoi(std::string(v).c_str()); };
        auto affixes = [&](const char* n) {
            std::vector<d2d::rules::Affix> v;
            if (auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt")) {
                const d2d::txt::Table t(*b, true);                 // raw rows = the save's affix IDs
                for (std::size_t r = 0; r < t.size(); ++r) {
                    d2d::rules::Affix a{ std::string(t.get(r, "Name")), num(t.get(r, "level")), num(t.get(r, "maxlevel")),
                                         num(t.get(r, "group")), num(t.get(r, "frequency")),
                                         t.get(r, "spawnable") == "1", t.get(r, "rare") == "1", {}, {}, {} };
                    for (int i = 1; i <= 7; ++i)
                        if (const auto c = t.get(r, "itype" + std::to_string(i)); !c.empty()) a.itypes.emplace_back(c);
                    for (int i = 1; i <= 5; ++i)
                        if (const auto c = t.get(r, "etype" + std::to_string(i)); !c.empty()) a.etypes.emplace_back(c);
                    for (int i = 1; i <= 3; ++i) {
                        const auto k = "mod" + std::to_string(i);
                        if (const auto c = t.get(r, k + "code"); !c.empty())
                            a.mods.push_back({ std::string(c), std::string(t.get(r, k + "param")),
                                               num(t.get(r, k + "min")), num(t.get(r, k + "max")) });
                    }
                    v.push_back(std::move(a));
                }
            }
            return v;
        };
        scene.rules.prefixes = affixes("MagicPrefix");
        scene.rules.suffixes = affixes("MagicSuffix");
        auto specials = [&](const char* n, const char* code_col, int props) {
            std::vector<d2d::rules::Special> v;
            if (auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt")) {
                const d2d::txt::Table t(*b);
                for (std::size_t r = 0; r < t.size(); ++r) {
                    d2d::rules::Special sp{ std::string(t.get(r, code_col)), num(t.get(r, "lvl")), num(t.get(r, "rarity")),
                                            code_col[0] == 'c' ? t.get(r, "enabled") == "1" : true, {} };
                    for (int i = 1; i <= props; ++i) {
                        const auto k = std::to_string(i);
                        if (const auto c = t.get(r, "prop" + k); !c.empty())
                            sp.mods.push_back({ std::string(c), std::string(t.get(r, "par" + k)),
                                                num(t.get(r, "min" + k)), num(t.get(r, "max" + k)) });
                    }
                    v.push_back(std::move(sp));
                }
            }
            return v;
        };
        scene.rules.uniques = specials("UniqueItems", "code", 12);
        scene.rules.sets = specials("SetItems", "item", 9);
        scene.rules.rare_prefixes = int(keys("RarePrefix", "name", false).size());
        scene.rules.rare_suffixes = int(keys("RareSuffix", "name", false).size());
        scene.rules.gamble = keys("gamble", "code", false);
        if (const auto ht = txt("hireling"); ht.size() > 0)
            for (std::size_t r = 0; r < ht.size(); ++r) {
                auto n = [&](const char* c) { return num(ht.get(r, c)); };
                const std::string first(ht.get(r, "NameFirst")), last(ht.get(r, "NameLast"));
                const int names = first.size() >= 2 && last.size() >= 2
                    ? std::atoi(last.substr(last.size() - 2).c_str()) - std::atoi(first.substr(first.size() - 2).c_str()) + 1 : 1;
                scene.rules.hirelings.push_back({ n("Version"), n("Id"), n("Class"), n("Act"), n("Difficulty"), n("Level"),
                    n("Gold"), n("Exp/Lvl"), n("HP"), n("HP/Lvl"), n("Defense"), n("Def/Lvl"), n("Str"), n("Str/Lvl"),
                    n("Dex"), n("Dex/Lvl"), n("Dmg-Min"), n("Dmg-Max"), n("Dmg/Lvl"), std::max(1, names),
                    n("AR"), n("AR/Lvl") });
            }
        if (const auto dl = txt("DifficultyLevels"); dl.size() >= 3)
            for (std::size_t r = 0; r < 3; ++r)
                scene.rules.gamble_rates[r] = { num(dl.get(r, "GambleRare")), num(dl.get(r, "GambleSet")),
                                                num(dl.get(r, "GambleUnique")) };
        if (auto b = mpqs.try_read(R"(data\global\excel\npc.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r) {
                auto n = [&](const char* c) { return std::atoi(std::string(t.get(r, c)).c_str()); };
                d2d::rules::NpcPrice p{ n("buy mult"), n("sell mult"), n("rep mult"),
                                   { n("questflag A"), n("questflag B"), n("questflag C") },
                                   { n("questbuymult A"), n("questbuymult B"), n("questbuymult C") },
                                   { n("questsellmult A"), n("questsellmult B"), n("questsellmult C") },
                                   { n("questrepmult A"), n("questrepmult B"), n("questrepmult C") },
                                   { n("max buy"), n("max buy (N)"), n("max buy (H)") } };
                scene.rules.npc_prices[std::string(t.get(r, "npc"))] = p;
            }
        }
    }
    nm.suffix   = keys("MagicSuffix", "Name", true);
    nm.rare_pre = keys("RarePrefix", "name", true);
    nm.rare_suf = keys("RareSuffix", "name", true);
    {
        // Runes.txt "RunewordN" rows sorted by N (80 and 96 don't exist);
        // the save's ID is that rank + 27.
        std::vector<std::pair<int, std::string>> rw;
        for (const auto& k : keys("Runes", "Name", false))
            if (k.starts_with("Runeword")) rw.emplace_back(std::atoi(k.c_str() + 8), k);
        std::ranges::sort(rw);
        for (auto& [n, k] : rw) nm.runeword.push_back(std::move(k));
    }
    for (auto [name, into] : { std::pair{ "font8", &scene.font_small }, { "font6", &scene.font_tiny } })
        if (auto t = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".tbl"))
            if (auto d = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".dc6"))
                *into = d2d::font::Font(*t, d2d::dc6::Sprite(*d));
    for (auto [path, into] : { std::pair{ R"(data\global\ui\PANEL\800ctrlpnl7.dc6)", &scene.ctrl_panel },
                               { R"(data\global\ui\PANEL\hlthmana.dc6)", &scene.globes },
                               { R"(data\global\ui\PANEL\overlap.dc6)", &scene.globe_glass } })
        if (auto b = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*b);
    if (auto p = mpqs.try_read(R"(data\global\ui\PANEL\invchar6.dc6)"))
        scene.inv_panel = d2d::dc6::Sprite(*p);
    for (auto [path, into] : { std::pair{ R"(data\global\ui\PANEL\level.dc6)", &scene.level_button },
                               { R"(data\global\ui\PANEL\levelsocket.dc6)", &scene.level_socket },
                               { R"(data\global\ui\PANEL\skillpoints.dc6)", &scene.points_box } })
        if (auto b = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*b);
    // inventory.txt "<Class>2" rows are the 800x600 layouts.
    const auto inv = txt("inventory");
    for (std::size_t r = 0; r < inv.size(); ++r) {
        const auto cls = inv.get(r, "class");
        const int e = cls == "Big Bank Page2" ? 1 : cls == "Bank Page2" ? 0
                    : cls == "Transmogrify Box2" ? 2 : -1;
        if (e < 0) continue;
        auto num = [&](const char* col) { return std::atoi(std::string(inv.get(r, col)).c_str()); };
        auto& L = e == 2 ? scene.cube_layout : scene.stash_layout[std::size_t(e)];
        L.grid_x = num("gridLeft"); L.grid_y = num("gridTop");
        L.cols = num("gridX"); L.rows = num("gridY");
        L.box_w = num("gridBoxWidth"); L.box_h = num("gridBoxHeight");
    }
    for (auto [path, e] : { std::pair{ R"(data\global\ui\PANEL\bank.dc6)", 0 },
                            { R"(data\global\ui\PANEL\TradeStash.dc6)", 1 } })
        if (auto b = mpqs.try_read(path)) scene.stash_panel[std::size_t(e)] = d2d::dc6::Sprite(*b);
    if (auto b = mpqs.try_read(R"(data\global\ui\PANEL\supertransmogrifier.dc6)"))
        scene.cube_panel = d2d::dc6::Sprite(*b);
    static constexpr const char* kInvClass[7] = {
        "Amazon2", "Sorceress2", "Necromancer2", "Paladin2", "Barbarian2", "Druid2", "Assassin2" };
    static constexpr const char* kSlotCol[11] = {
        nullptr, "head", "neck", "torso", "rArm", "lArm", "rHand", "lHand", "belt", "feet", "gloves" };
    for (std::size_t c = 0; c < 7; ++c)
        for (std::size_t r = 0; r < inv.size(); ++r) {
            if (inv.get(r, "class") != kInvClass[c]) continue;
            auto num = [&](std::string col) { return std::atoi(std::string(inv.get(r, col)).c_str()); };
            auto& L = scene.inv_layout[c];
            L.panel_x = num("invLeft"); L.panel_y = num("invTop");
            L.grid_x = num("gridLeft"); L.grid_y = num("gridTop");
            L.cols = num("gridX"); L.rows = num("gridY");
            L.box_w = num("gridBoxWidth"); L.box_h = num("gridBoxHeight");
            for (std::size_t sl = 1; sl < 11; ++sl) {
                const std::string k = kSlotCol[sl];
                L.slots[sl] = { num(k + "Left"), num(k + "Top"), num(k + "Width"), num(k + "Height") };
            }
        }
    auto index_of = [&](std::string_view code) {
        for (std::size_t i = 1; i < scene.comp.size(); ++i)
            if (scene.comp[i].code == code) return std::uint8_t(i);
        return std::uint8_t(0xff);
    };
    // Class skills: Skills.txt rows by charclass, in file order (the save's
    // 30 "if" bytes), joined with SkillDesc.txt for the tree.
    {
        const auto skills = txt("skills"), desc = txt("skilldesc");
        std::unordered_map<std::string, std::size_t> desc_row;
        for (std::size_t r = 0; r < desc.size(); ++r) desc_row[std::string(desc.get(r, "skilldesc"))] = r;
        auto num = [](std::string_view v) { return std::atoi(std::string(v).c_str()); };
        for (std::size_t c = 0; c < 7; ++c) {
            auto& list = scene.rules.class_skills[c];
            std::vector<std::string> names;
            for (std::size_t r = 0; r < skills.size(); ++r) {
                if (skills.get(r, "charclass") != d2d::rules::kClassCode[c]) continue;
                d2d::rules::ClassSkill sk;
                sk.req_level = std::max(1, num(skills.get(r, "reqlevel")));
                if (const int m = num(skills.get(r, "maxlvl")); m > 0) sk.max_level = m;
                if (const auto d = desc_row.find(std::string(skills.get(r, "skilldesc"))); d != desc_row.end()) {
                    sk.page = num(desc.get(d->second, "SkillPage"));
                    sk.row = num(desc.get(d->second, "SkillRow"));
                    sk.col = num(desc.get(d->second, "SkillColumn"));
                    sk.icon = num(desc.get(d->second, "IconCel"));
                    sk.name = std::string(desc.get(d->second, "str name"));
                }
                names.emplace_back(skills.get(r, "skill"));
                list.push_back(std::move(sk));
            }
            // reqskill1..3 name skills of the same class.
            std::size_t i = 0;
            for (std::size_t r = 0; r < skills.size(); ++r) {
                if (skills.get(r, "charclass") != d2d::rules::kClassCode[c]) continue;
                for (int q = 0; q < 3; ++q) {
                    const auto want = skills.get(r, "reqskill" + std::to_string(q + 1));
                    if (const auto it = std::ranges::find(names, want); !want.empty() && it != names.end())
                        list[i].req[std::size_t(q)] = int(it - names.begin());
                }
                ++i;
            }
        }
        static constexpr const char* kTree[7] = { "a", "s", "n", "p", "b", "d", "i" };
        static constexpr const char* kIcons[7] = { "Am", "So", "Ne", "Pa", "Ba", "Dr", "As" };
        for (std::size_t c = 0; c < 7; ++c) {
            if (auto b = mpqs.try_read(std::string(R"(data\global\ui\SPELLS\skltree_)") + kTree[c] + "_back.dc6"))
                scene.skill_tree_bg[c] = d2d::dc6::Sprite(*b);
            if (auto b = mpqs.try_read(std::string(R"(data\global\ui\SPELLS\)") + kIcons[c] + "Skillicon.dc6"))
                scene.skill_icons[c] = d2d::dc6::Sprite(*b);
        }
        if (auto b = mpqs.try_read(R"(data\global\ui\SPELLS\Skillicon.dc6)")) scene.generic_skill_icons = d2d::dc6::Sprite(*b);
        for (auto [sprite, file] : { std::pair{ &scene.quest_bg, "questbackground" }, { &scene.quest_tabs, "expquesttabs" },
                                     { &scene.quest_sockets, "questsockets" }, { &scene.quest_last, "questlast" } })
            if (auto b = mpqs.try_read(std::string(R"(data\global\ui\menu\)") + file + ".dc6")) *sprite = d2d::dc6::Sprite(*b);
        for (std::size_t i = 0; i < scene.quest_icons.size(); ++i)    // the names at 0x6da2c8: a1q1..a1q6, a2q1.., a4q1..3, a5q1..
            if (auto b = mpqs.try_read(std::format(R"(data\global\ui\menu\a{}q{}.dc6)", i < 6 ? 1 : i < 12 ? 2 : i < 18 ? 3 : i < 21 ? 4 : 5,
                                                   i < 18 ? i % 6 + 1 : i < 21 ? i - 17 : i - 20)))
                scene.quest_icons[i] = d2d::dc6::Sprite(*b);
    }
    for (std::size_t c = 0; c < 7 && c < charstats.size(); ++c) {
        auto per = [&](const char* col) { return std::atoi(std::string(charstats.get(c, col)).c_str()); };
        scene.class_gains[c] = { per("LifePerVitality"), per("StaminaPerVitality"), per("ManaPerMagic"),
                                 per("LifePerLevel"), per("StaminaPerLevel"), per("ManaPerLevel"),
                                 per("StatPerLevel"), per("ToHitFactor"), per("BlockFactor") };
        auto& cs = scene.class_start[c];
        cs = { per("str"), per("dex"), per("int"), per("vit"), per("stamina"), per("hpadd"), {}, std::string(charstats.get(c, "StartSkill")) };
        for (int i = 1; i <= 10; ++i)
            if (const std::string code(charstats.get(c, "item" + std::to_string(i))); !code.empty() && code != "0")
                cs.items.push_back({ code, std::string(charstats.get(c, "item" + std::to_string(i) + "loc")),
                                     std::atoi(std::string(charstats.get(c, "item" + std::to_string(i) + "count")).c_str()) });
        auto& g = scene.starting_gear[c];
        g.fill(0xff);
        for (int l : { 1, 2, 3, 4, 8, 9 }) g[std::size_t(l)] = 1;   // TR LG RA LA S1 S2 = lit
        for (int i = 1; i <= 10; ++i) {
            const auto item = charstats.get(c, "item" + std::to_string(i));
            const auto loc  = charstats.get(c, "item" + std::to_string(i) + "loc");
            const auto idx  = index_of(item);
            if (idx == 0xff) continue;
            if (loc == "rarm") g[5] = idx;
            else if (loc == "larm") g[scene.comp[idx].armor ? 7 : 6] = idx;
        }
    }
}

// Draws a composite frame, feet at the anchor (defined with the other
// DCC blitters below).
void draw_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y);

// Headers (and items) of every valid .d2s in `dir`, most recently played first. Bad files are
// logged and skipped — saves are user-supplied.
void load_saves(Scene& scene, const fs::path& dir) {
    struct Entry { d2d::d2s::Header header; std::vector<d2d::d2s::Item> items; d2d::d2s::Stats stats; std::vector<d2d::d2s::Item> corpse; };
    std::vector<Entry> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".d2s") continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
        const auto bytes = std::as_bytes(std::span(raw));
        try {
            Entry en{ d2d::d2s::parse_header(bytes), {}, {} };
            if (scene.item_tables) {
                try {
                    en.stats = d2d::d2s::parse_stats(bytes, *scene.item_tables);
                    en.items = d2d::d2s::parse_items(bytes, *scene.item_tables);
                    en.corpse = d2d::d2s::parse_corpse(bytes, *scene.item_tables).items;
                }
                catch (const std::exception& ex) {
                    d2d::log::warn("{} items: {}", e.path().string(), ex.what());
                }
            }
            out.push_back(std::move(en));
        } catch (const std::exception& ex) {
            d2d::log::warn("{}: {}", e.path().string(), ex.what());
        }
    }
    // Newest first — LoD inserts each character by last-played time,
    // descending (FUN_00438ad0), and preselects slot 0.
    // Ties (e.g. synthetic saves with no timestamp) fall back to name so
    // the order doesn't depend on directory iteration.
    std::ranges::sort(out, [](const Entry& a, const Entry& b) {
        return std::tie(b.header.last_played, a.header.name)
             < std::tie(a.header.last_played, b.header.name);
    });
    scene.saves.clear(); scene.save_items.clear(); scene.save_stats.clear(); scene.save_corpses.clear();
    for (auto& en : out) {
        scene.saves.push_back(std::move(en.header));
        scene.save_items.push_back(std::move(en.items));
        scene.save_corpses.push_back(std::move(en.corpse));
        scene.save_stats.push_back(en.stats);
    }
    d2d::log::info("Characters: {} in {}", scene.saves.size(), dir.string());
}

std::optional<Scene> load_scene(const fs::path& data_dir, const fs::path& patch_installer, std::uint32_t map_seed) {
    const auto d2data = data_dir / "d2data.mpq";
    if (!fs::exists(d2data)) {
        d2d::log::error("no d2data.mpq in {} — running without game data (test pattern)", data_dir.string());
        return std::nullopt;
    }
    const auto t0 = d2d::log::ms();
    try {
        d2d::log::info("Initializing MPQs:");
        d2d::mpq::Stack mpqs;
        auto push = [&](const fs::path& p) {
            mpqs.push(p);
            d2d::log::info("  Loading: {} {} bytes", p.filename().string(), fs::file_size(p));
        };
        // 1.14d's patch layer ranks above everything (game.exe opens
        // patch_d2.mpq first). A real install has patch_d2.mpq; a CD-copied
        // data dir can use the LODPatch_114d.exe installer instead (d2d.cfg
        // `patch = ...`, or dropped next to the MPQs).
        bool patched = false;
        if (fs::exists(data_dir / "patch_d2.mpq")) {
            push(data_dir / "patch_d2.mpq");
            patched = true;
        } else {
            for (const auto& p : { patch_installer, data_dir / "LODPatch_114d.exe" }) {
                if (p.empty() || !fs::exists(p)) continue;
                try {
                    mpqs.push_installer(p);
                    d2d::log::info("  Loading: {} (1.14d patch installer)", p.string());
                    patched = true;
                    break;
                }
                catch (const std::exception& e) { d2d::log::warn("{}", e.what()); }
            }
        }
        if (!patched)
            d2d::log::warn("no 1.14d patch data (patch_d2.mpq or LODPatch_114d.exe): "
                           "using CD-era tables and strings");
        const auto d2exp = data_dir / "d2exp.mpq";
        if (fs::exists(d2exp)) push(d2exp);
        push(d2data);
        // Character animations live in d2char.mpq — Stack lookup is
        // priority-ordered so later pushes rank lower; DCC-not-found is
        // silent in load_scene and per-class loaders skip on miss.
        const auto d2char = data_dir / "d2char.mpq";
        if (fs::exists(d2char)) push(d2char);
        // Sounds: expansion speech, speech, effects, music (Sounds.txt paths).
        // (Music is read off the main thread from its own handles: Audio.)
        for (const char* n : { "d2xtalk.mpq", "d2speech.mpq", "d2sfx.mpq" })
            if (fs::exists(data_dir / n)) push(data_dir / n);

        // Prefer the LoD title asset (fenced rogue camp at night). Classic
        // TitleScreen is only 4×3 sub-frames; LoD is the same layout.
        auto title = mpqs.try_read(R"(data\global\ui\FrontEnd\gameselectscreenEXP.dc6)");
        if (!title) title = mpqs.try_read(R"(data\global\ui\FrontEnd\TitleScreen.DC6)");
        if (!title) throw std::runtime_error("no title screen asset");

        Scene scene;
        // Sky = title/credits (game.exe hardcodes palette\sky\pal.pl2 in
        // 5 sites of the menu loader — docs/research/re/frontend-menu-table.md).
        scene.pal = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\Sky\pal.dat)"));
        // fechar = "Front End CHARacter", the char-select/creation palette.
        // game.exe's FUN_00435580 (char-select init) loads it right after
        // the char-select asset loader (FUN_004326f0). Firelit warm tones
        // — night camp scene lit by the campfire the classes stand around.
        scene.charselect_pal = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\fechar\pal.dat)"));
        scene.sky_pl2 = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\Sky\Pal.PL2)"));
        scene.fechar_pl2 = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\fechar\Pal.PL2)"));
        scene.bg = d2d::dc6::Sprite(*title);
        scene.logo_static = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\Diablo2.dc6)"));
        scene.logo_bl = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackLeft.DC6)"));
        scene.logo_br = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackRight.DC6)"));
        scene.logo_fl = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireLeft.DC6)"));
        scene.logo_fr = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireRight.DC6)"));
        scene.btn_wide = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank.dc6)"));
        scene.btn_wide2 = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank02.dc6)"));
        scene.btn_narrow = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\NarrowButtonBlank.dc6)"));
        scene.btn_short = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\ShortButtonBlank.dc6)"));
        scene.credits_bg = [&] {
                // creditsbckgexpand.dc6 (LoD) → creditsbckg.dc6 (classic).
                auto b = mpqs.try_read(R"(data\global\ui\CharSelect\creditsbckgexpand.dc6)");
                if (!b) b = mpqs.read(R"(data\global\ui\CharSelect\creditsbckg.dc6)");
                return d2d::dc6::Sprite(*b);
            }();
        scene.charcreate_bg = [&] {
                auto b = mpqs.try_read(R"(data\global\ui\FrontEnd\charactercreationscreenEXP.dc6)");
                if (!b) b = mpqs.read(R"(data\global\ui\FrontEnd\CharacterCreate.dc6)");
                return d2d::dc6::Sprite(*b);
            }();
        scene.fire = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\fire.DC6)"));
        scene.medium_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumButtonBlank.dc6)"));
        scene.medium_sel_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumSelButtonBlank.dc6)"));
        scene.textbox = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\textbox.dc6)"));
        scene.clickbox = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\clickbox.dc6)"));
        scene.charselect_bg = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\characterselectscreenEXP.dc6)"));
        scene.charselect_box = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\charselectbox.dc6)"));
        scene.charselect_scroll = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\joingamescrollbars.dc6)"));
        scene.tall_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\TallButtonBlank.dc6)"));
        scene.cursor = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CURSOR\ohand.dc6)"));
        scene.class_anims = [&] {
                // Anim files per class, in order {nu1, nu2, fw, nu3, bw}.
                // Class prefix pairs from FUN_004326f0's loader.
                struct C { const char* dir; const char* prefix; };
                constexpr C cs[7] = {
                    {"barbarian",   "ba"},
                    {"necromancer", "ne"},
                    {"paladin",     "pa"},
                    {"amazon",      "am"},
                    {"sorceress",   "so"},
                    {"druid",       "dz"},
                    {"assassin",    "as"},
                };
                constexpr const char* suffix[5] = {"nu1", "nu2", "fw", "nu3", "bw"};
                std::array<std::array<d2d::dc6::Sprite, 5>, 7> out;
                for (std::size_t ci = 0; ci < 7; ++ci) {
                    for (std::size_t si = 0; si < 5; ++si) {
                        char path[256];
                        std::snprintf(path, sizeof(path),
                            R"(data\global\ui\FrontEnd\%s\%s%s.dc6)",
                            cs[ci].dir, cs[ci].prefix, suffix[si]);
                        out[ci][si] = d2d::dc6::Sprite(mpqs.read(path));
                    }
                }
                return out;
            }();
        scene.font = d2d::font::Font(
                             mpqs.read(R"(data\local\FONT\LATIN\font16.tbl)"),
                             d2d::dc6::Sprite(mpqs.read(R"(data\local\FONT\LATIN\font16.dc6)")));
        scene.credits = [&] {
                auto b = mpqs.try_read(R"(data\local\UI\ENG\ExpansionCredits.txt)");
                if (!b) b = mpqs.try_read(R"(data\local\ui\eng\Credits.txt)");
                return b ? parse_credits_utf16(*b) : std::vector<std::string>{};
            }();
        // Frontend button labels (IDs 0x13f2..0x13f7) live in the base
        // string.tbl per probe. patchstring.tbl (826 entries) overrides
        // specific IDs when Blizzard shipped patches; expansionstring.tbl
        // (2788 entries) carries LoD-specific additions. For MVP we use
        // string.tbl directly; when a subsystem needs a patch-shifted
        // entry, load all three and query in order (patch → expansion →
        // base).
        scene.strings = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\string.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }();
        scene.patch_strings = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\patchstring.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }();
        scene.exp_strings = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\expansionstring.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }();
        // Rogue-camp world data — separate call so a DS1/DT1 miss doesn't
        // nuke the whole scene; the InGame screen falls back to the credits
        // placeholder when world is empty.
        d2d::log::info("Loading frontend assets... done ({} ms)", d2d::log::ms() - t0);
        d2d::log::info("  Strings: {} base, {} patch, {} expansion; {} credit lines",
                       scene.strings.size(), scene.patch_strings.size(), scene.exp_strings.size(),
                       scene.credits.size());
        auto act1 = std::make_unique<d2d::drlg::OutdoorAssets>();
        d2d::drlg::load_outdoor_assets(*act1, [&](const std::string& p) { return mpqs.try_read(p); });
        place_act1(scene, mpqs, *act1, map_seed);
        // The other levels build when they're first wanted (Scene::level).
        scene.builder = std::make_shared<GameData::LevelBuilder>();
        scene.builder->act1 = std::move(act1);
        load_composite_data(scene, mpqs);
        load_npcs(scene, mpqs);
        load_monsters(scene, mpqs);
        load_skills(scene, mpqs);
        d2d::log::info("Loading game data... done ({} ms)", d2d::log::ms() - t0);
        d2d::log::info("  Items: {}; sounds: {}; town NPCs/objects: {}",
                       scene.item_tables ? "tables loaded" : "no item tables",
                       scene.sounds.size(), scene.town.npcs.size());
        scene.patched = patched;
        if (auto b = mpqs.try_read(R"(data\global\ui\FrontEnd\CinematicsSelectionEXP.dc6)"))
            scene.cinematics_panel = d2d::dc6::Sprite(*b);
        scene.data_dir = data_dir;
        scene.mpqs = std::move(mpqs);
        d2d::log::info("Scene loaded in {} ms.", d2d::log::ms() - t0);
        return scene;
    } catch (const std::exception& e) {
        d2d::log::error("load_scene: {}", e.what());
        return std::nullopt;
    }
}

// Translate a DS1-embedded tileset path (e.g.
// "\d2\data\global\tiles\act1\town\floor.dt1") into the MPQ path we can hand to Stack::try_read. The
// DS1 files store paths as they were on Blizzard's build box, with a
// leading "\d2\" prefix and forward slashes never — normalize both.
[[nodiscard]] inline std::string ds1_path_to_mpq(std::string_view s) {
    if (s.size() > 4 && (s.starts_with("\\d2\\") || s.starts_with("/d2/")))
        s.remove_prefix(4);
    else if (!s.empty() && (s[0] == '\\' || s[0] == '/'))
        s.remove_prefix(1);
    std::string out(s);
    for (auto& c : out) if (c == '/') c = '\\';
    return out;
}

// Encode (style, sequence, type) into a single lookup key. Style + sequence
// are DS1-record bytes; type is the DT1 orientation code (0..16 per D2's
// tile-type table). 24 bits × 24 bits × 16 bits comfortably fits u64.
[[nodiscard]] inline std::uint64_t tile_key(int style, int seq, int type) {
    return (std::uint64_t(std::uint32_t(style)) << 40)
         | (std::uint64_t(std::uint32_t(seq  )) << 16)
         |  std::uint64_t(std::uint16_t(type ));
}

// A level's tile lookup and collision grid, once its ds1 and dt1s are in.
void finish_level(Level& L) {
    // Populate the (style, seq, type) lookup across all DT1s. First DT1
    // to define a tuple wins — matches how D2's renderer resolves tile
    // priority against its Stack-ordered tileset list. Covers floors,
    // walls, trees, roofs, shadows in one map.
    // ponytail: first match, not game.exe's rarity pick — generated levels
    // carry the real picks (Level::picks) and use those instead.
    for (const auto& dt1 : L.dt1s)
        for (const auto& t : dt1.tiles()) L.tile_lookup.try_emplace(tile_key(t.style, t.sequence, t.type), &t);
    // Collision grid from the same tiles the renderer draws: floors
    // (type 0) and walls/objects, but not shadows (13) or roofs (15).
    // A tile's 25 DT1 subtile flags OR straight into the grid, rows stored
    // bottom-up: subtile (x, y) takes flag (4 - y) * 5 + x (FUN_0064c4c0,
    // building the room grid in FUN_0064c900). Units stamp their own
    // footprints (load_npcs).
    const auto& m = L.ds1;
    const int ww = m.width() * 5;
    L.walk.assign(std::size_t(ww) * std::size_t(m.height()) * 5, 0);
    auto stamp_tile = [&](int gx, int gy, const d2d::dt1::Tile& t) {
        for (int k = 0; k < 25; ++k)
            L.walk[std::size_t(gy * 5 + 4 - k / 5) * std::size_t(ww) + std::size_t(gx * 5 + k % 5)] |= t.subtile_flags[std::size_t(k)];
    };
    auto stamp = [&](int gx, int gy, int style, int seq, int type) {
        const auto it = L.tile_lookup.find(tile_key(style, seq, type));
        if (it != L.tile_lookup.end()) stamp_tile(gx, gy, *it->second);
    };
    if (!L.picks.empty()) {
        for (int gy = 0; gy < m.height(); ++gy)
            for (int gx = 0; gx < m.width(); ++gx)
                for (const auto& p : L.picks[std::size_t(gy) * std::size_t(m.width()) + std::size_t(gx)])
                    if (p.layer == 1 || (p.layer == 0 && p.orient != 13 && p.orient != 15)) stamp_tile(gx, gy, *p.tile);
        return;
    }
    for (int gy = 0; gy < m.height(); ++gy)
        for (int gx = 0; gx < m.width(); ++gx) {
            const std::size_t off = std::size_t(gy) * std::size_t(m.width()) + std::size_t(gx);
            for (const auto& fl : m.floors())
                if (!fl.cells[off].hidden && (fl.cells[off].prop1 & 2)) stamp(gx, gy, fl.cells[off].style, fl.cells[off].sequence, 0);
            for (const auto& wl : m.walls()) {
                const auto& c = wl.cells[off];
                if (c.hidden || c.wall_type == 0 || c.wall_type == 13 || c.wall_type == 15) continue;
                stamp(gx, gy, c.style, c.sequence, c.wall_type);
            }
        }
}

// The Blood Moor from the map seed (components/drlg): act 1's layout
// places it against the town, the generator fills it, its tiles come from
// the Act 1 wilderness DT1s (LvlTypes).
// A generated level's DT1s: the headers its rooms' picks read (LvlTypes
// files of its type by mask bit, then Blank, InvisWal, Warp) and the
// archives drawn from, loaded into the level.
struct LevelDt1s {
    d2d::drlg::RoomDt1s heads;
    std::unordered_map<const d2d::drlg::Dt1File*, const d2d::dt1::Archive*> archive;
};
LevelDt1s load_level_dt1s(Level& lv, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& a, int type) {
    LevelDt1s d;
    d.heads = d2d::drlg::load_room_dt1s(a, [&](const std::string& p) { return mpqs.try_read(p); }, type);
    std::vector<std::pair<const d2d::drlg::Dt1File*, std::string>> files;
    for (std::size_t r = 0; r < a.lvl_types.size(); ++r)
        if (d2d::drlg::to_int(a.lvl_types.get(r, "Id"), -1) == type)
            for (int i = 0; i < 32; ++i)
                if (d.heads.by_bit[std::size_t(i)]) files.emplace_back(d.heads.by_bit[std::size_t(i)], std::string(a.lvl_types.get(r, "File " + std::to_string(i + 1))));
    for (std::size_t k = 0; k < 3; ++k)
        if (d.heads.always[k]) files.emplace_back(d.heads.always[k], std::array{ "Act1/Outdoors/Blank.dt1", "Act1/Barracks/InvisWal.dt1", "Act1/Barracks/Warp.dt1" }[k]);
    lv.dt1s.reserve(files.size());                      // archive points into it
    for (const auto& [h, f] : files) {
        auto db = mpqs.try_read(R"(data\global\tiles\)" + ds1_path_to_mpq(f));
        if (!db) continue;
        try { d.archive[h] = &lv.dt1s.emplace_back(*db); } catch (const std::exception& e) { d2d::log::warn("level {}: {}: {}", lv.id, f, e.what()); }
    }
    return d;
}

// Its rooms brought up (drlg level_room_tiles, proven against game.exe):
// every cell's picked tiles, its warps, then lookup and collision.
std::size_t set_level_tiles(Level& lv, const d2d::drlg::OutdoorAssets& a, const LevelDt1s& d,
                            const std::vector<d2d::drlg::Outdoor::RoomSeed>& made, const std::vector<d2d::drlg::PlainRoom>& plain,
                            std::vector<std::string>& notes) {
    const auto built = d2d::drlg::level_room_tiles(made, plain, a.data, d.heads, lv.id, d2d::drlg::warp_slots(a, lv.id), notes);
    const int W = lv.ds1.width(), H = lv.ds1.height();
    lv.picks.assign(std::size_t(W) * std::size_t(H), {});
    std::size_t placed = 0;
    for (const auto& r : built)
        for (const auto& t : r.tiles) {
            if (t.x < 0 || t.y < 0 || t.x >= W || t.y >= H || !t.file || t.index < 0) continue;
            const auto it = d.archive.find(t.file);
            if (it == d.archive.end() || std::size_t(t.index) >= it->second->size()) continue;
            lv.picks[std::size_t(t.y) * std::size_t(W) + std::size_t(t.x)].push_back(
                { std::uint8_t(t.layer), std::uint8_t(t.orient), &it->second->tiles()[std::size_t(t.index)] });
            ++placed;
        }
    for (const auto& r : built)
        for (const auto& u : r.units) lv.units.push_back({ u.type, u.id, u.mode, u.x + r.x * 5, u.y + r.y * 5, u.flags });
    // Warps: the slot's Levels.txt Vis / Warp, LvlWarp's ExitWalk.
    // ponytail: ExitWalk read as subtiles from the warp's cell; check the
    // arrival spot against game.exe when it matters.
    if (const auto row = d2d::drlg::level_row(a.levels, lv.id))
        for (const auto& r : built)
            for (const auto& w : r.warps) {
                if (w.slot < 0 || w.slot > 7) continue;
                const int to = d2d::drlg::to_int(a.levels.get(*row, "Vis" + std::to_string(w.slot)));
                const int wid = d2d::drlg::to_int(a.levels.get(*row, "Warp" + std::to_string(w.slot)), -1);
                if (to <= 0 || wid < 0) continue;
                float ex = 0, ey = 0;
                for (std::size_t k = 0; k < a.lvl_warp.size(); ++k)
                    if (d2d::drlg::to_int(a.lvl_warp.get(k, "Id"), -1) == wid) {
                        ex = float(d2d::drlg::to_int(a.lvl_warp.get(k, "ExitWalkX"))) / 5;
                        ey = float(d2d::drlg::to_int(a.lvl_warp.get(k, "ExitWalkY"))) / 5;
                        break;
                    }
                lv.warps.push_back({ float(w.x), float(w.y), to, ex, ey });
            }
    finish_level(lv);
    return placed;
}

// An outdoor level of the act (the Blood Moor): drlg generate_outdoor
// where the act's layout put it, on its level seed.
bool build_outdoor(const GameData& scene, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& a, Level& lv) {
    const auto L = d2d::drlg::outdoor_level(a.levels, scene.act1_layout, lv.id);
    if (L.rect.w == 0) { d2d::log::warn("{}: the layout didn't place it", lv.name); return false; }
    const auto dt1s = load_level_dt1s(lv, mpqs, a, lv.type);
    a.data.dt1s = &dt1s.heads;                          // stamps pick their shadows as they go (game.exe's rolls)
    auto o = d2d::drlg::generate_outdoor(a.data, L, d2d::drlg::level_seed(scene.map_seed, lv.id));
    a.data.dt1s = nullptr;
    auto notes = o.notes;
    lv.ds1 = std::move(o.tiles);
    lv.world_x = L.rect.x;
    lv.world_y = L.rect.y;
    const auto placed = set_level_tiles(lv, a, dt1s, o.rooms, o.plain, notes);
    lv.rooms = std::move(o.rooms);
    for (const auto& n : notes) d2d::log::info("  not implemented: {}", n);
    d2d::log::info("  {}: {}x{} tiles at ({}, {}), {} roads, {} tilesets, {} picked tiles, {} warp tiles, map seed {}", lv.name,
                   lv.ds1.width(), lv.ds1.height(), lv.world_x, lv.world_y, o.roads.size(), lv.dt1s.size(), placed,
                   lv.warps.size(), scene.map_seed);
    return true;
}

// A maze level (the Den of Evil): drlg generate_maze from its level seed,
// its preset rooms' tiles picked as game.exe picks them. It sits apart
// from the act's outdoor levels; its warps lead out.
bool build_maze(const GameData& scene, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& a, Level& lv, std::size_t row) {
    d2d::drlg::MazeDef m;
    for (std::size_t r = 0; r < a.lvl_maze.size(); ++r)
        if (d2d::drlg::to_int(a.lvl_maze.get(r, "Level"), -1) == lv.id) {
            m.rooms.fill(d2d::drlg::to_int(a.lvl_maze.get(r, "Rooms")));   // ponytail: the .txt's one column for every difficulty
            m.w = d2d::drlg::to_int(a.lvl_maze.get(r, "SizeX"));
            m.h = d2d::drlg::to_int(a.lvl_maze.get(r, "SizeY"));
            m.merge = d2d::drlg::to_int(a.lvl_maze.get(r, "Merge"));
        }
    if (m.w == 0) { d2d::log::warn("{}: no LvlMaze row", lv.name); return false; }
    std::vector<std::string> notes;
    const int sw = d2d::drlg::to_int(a.levels.get(row, "SizeX")), sh = d2d::drlg::to_int(a.levels.get(row, "SizeY"));
    auto made = d2d::drlg::generate_maze(a.data, m, lv.id, sw, sh, d2d::drlg::level_seed(scene.map_seed, lv.id), 0, notes);
    int W = 0, H = 0;
    for (const auto& r : made) { W = std::max(W, r.x + r.w); H = std::max(H, r.y + r.h); }
    lv.ds1 = d2d::ds1::Map(W, H, 4, 2);
    lv.world_x = d2d::drlg::to_int(a.levels.get(row, "OffsetX"));
    lv.world_y = d2d::drlg::to_int(a.levels.get(row, "OffsetY"));
    const auto dt1s = load_level_dt1s(lv, mpqs, a, lv.type);
    const auto placed = set_level_tiles(lv, a, dt1s, made, {}, notes);
    lv.rooms = std::move(made);
    for (const auto& n : notes) d2d::log::info("  not implemented: {}", n);
    d2d::log::info("  {}: {}x{} tiles, {} rooms, {} tilesets, {} picked tiles, {} warp tiles", lv.name, W, H,
                   lv.rooms.size(), lv.dt1s.size(), placed, lv.warps.size());
    return true;
}

// The levels d2d builds so far (the rest of Act 1 comes with its research).
constexpr std::array kBuiltLevels{ 2, 8 };

// Level `id` built from the map seed: its tiles and walk grid, warps, the
// objects and NPCs its DS1s place, its sound, automap layer and monster
// columns. On the builder thread; reads the Scene's tables only.
std::unique_ptr<Level> build_level(const GameData& scene, GameData::LevelBuilder& b, int id) {
    const auto t0 = d2d::log::ms();
    std::lock_guard lk(b.m);
    if (!b.mpqs) b.mpqs = scene.mpqs.reopen();
    auto& a = *b.act1;
    const auto row = d2d::drlg::level_row(a.levels, id);
    if (!row) return nullptr;
    auto lv = std::make_unique<Level>();
    lv->id = id;
    lv->type = d2d::drlg::to_int(a.levels.get(*row, "LevelType"));
    lv->name = std::string(a.levels.get(*row, "LevelName"));
    const bool outdoor = std::ranges::any_of(scene.act1_layout, [&](const auto& p) { return p.level == id; });
    if (!(outdoor ? build_outdoor(scene, *b.mpqs, a, *lv) : build_maze(scene, *b.mpqs, a, *lv, *row))) return nullptr;
    auto num = [](std::string_view v) { return std::atoi(std::string(v).c_str()); };
    for (std::size_t r = 0; r < b.levels.size(); ++r) {
        if (num(b.levels.get(r, "Id")) != id) continue;
        auto g = [&](std::string c) { return b.levels.get(r, c); };
        lv->layer = num(g("Layer"));
        lv->light = level_light(g("Intensity"), g("Red"), g("Green"), g("Blue"));
        lv->rain = g("Rain") == "1";
        for (std::size_t e = 0; e < b.sound_env.size(); ++e)
            if (b.sound_env.get(e, "Index") == g("SoundEnv")) {
                lv->song = num(b.sound_env.get(e, "Song"));
                lv->ambience = num(b.sound_env.get(e, "Day Ambience"));
                lv->night_ambience = num(b.sound_env.get(e, "Night Ambience"));
                lv->day_event = num(b.sound_env.get(e, "Day Event"));
                lv->night_event = num(b.sound_env.get(e, "Night Event"));
                lv->event_delay = num(b.sound_env.get(e, "Event Delay"));
            }
        auto mon = [&](std::string_view k) { return k.empty() ? -1 : scene.monsters.row(std::string(k)); };
        auto& L = lv->mon;
        L.density = { num(g("MonDen")), num(g("MonDen(N)")), num(g("MonDen(H)")) };
        L.umin = { num(g("MonUMin")), num(g("MonUMin(N)")), num(g("MonUMin(H)")) };
        L.umax = { num(g("MonUMax")), num(g("MonUMax(N)")), num(g("MonUMax(H)")) };
        L.wander = g("MonWndr") == "1";
        L.num_mon = num(g("NumMon"));
        for (int i = 1; i <= 25; ++i) {
            if (const int k = mon(g("mon" + std::to_string(i))); k >= 0) L.mon.push_back(k);
            if (const int k = mon(g("nmon" + std::to_string(i))); k >= 0) L.nmon.push_back(k);
        }
    }
    // Its preset units (Level::units): objects, and monsters MonStats marks
    // as NPCs (Flavie by the Blood Moor's way in). Unit ids are game.exe's:
    // MonStats rows without its Expansion row.
    // ponytail: the object seed starts from the map seed in every level.
    d2d::rules::Rng rgn(scene.map_seed);
    for (const auto& u : lv->units) {
        if (u.type == 2) add_object(scene, b.objects, b.obj_row, *lv, u.id, u.x, u.y, rgn);
        if (u.type != 1 || u.id < 0 || std::size_t(u.id) >= scene.mon_bin.size()) continue;
        const auto r = scene.mon_bin[std::size_t(u.id)];
        if (!scene.mon_is_npc[r] || scene.mon_npc[r].code.empty()) continue;
        auto n = scene.mon_npc[r];
        n.x = (float(u.x) + 0.5f) / 5;
        n.y = (float(u.y) + 0.5f) / 5;
        lv->npcs.push_back(std::move(n));
    }
    stamp_footprints(*lv);
    d2d::log::info("  {} built ({} ms)", lv->name, d2d::log::ms() - t0);
    return lv;
}

// A finished build into Scene::levels (nullptr: tried, not built); an act
// level is linked with the act levels already there, both ways.
void install_level(const GameData& s, int id, std::unique_ptr<Level> lv) {
    auto in_act = [&](int i) { return std::ranges::any_of(s.act1_layout, [&](const auto& p) { return p.level == i; }); };
    if (lv && in_act(id)) {
        std::vector<const Level*> others;
        if (s.town.ds1.width() > 0) others.push_back(&s.town);
        for (const auto& [k, o] : s.levels) if (o && in_act(k)) others.push_back(o.get());
        for (const Level* o : others) {
            lv->nearby.push_back({ o, o->world_x - lv->world_x, o->world_y - lv->world_y });
            o->nearby.push_back({ lv.get(), lv->world_x - o->world_x, lv->world_y - o->world_y });
        }
    }
    s.levels[id] = std::move(lv);
}

std::unique_ptr<Level> finish_job(std::future<std::unique_ptr<Level>>& job, int id) {
    try { return job.get(); }
    catch (const std::exception& e) { d2d::log::warn("level {}: {}", id, e.what()); return nullptr; }
}

void GameData::want_level(int id) const {
    if (!builder || !builder->act1 || id == town.id || levels.contains(id) || builder->jobs.contains(id)
        || !std::ranges::contains(kBuiltLevels, id)) return;
    builder->jobs.emplace(id, std::async(std::launch::async, [this, id] { return build_level(*this, *builder, id); }));
}

void GameData::poll_levels() const {
    if (!builder) return;
    for (auto it = builder->jobs.begin(); it != builder->jobs.end();)
        if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            install_level(*this, it->first, finish_job(it->second, it->first));
            it = builder->jobs.erase(it);
        } else ++it;
}

const Level* GameData::level(int id) const {
    if (id == town.id) return &town;
    if (const auto it = levels.find(id); it != levels.end()) return it->second.get();
    want_level(id);
    const auto job = builder ? builder->jobs.find(id) : decltype(builder->jobs.end()){};
    if (!builder || job == builder->jobs.end()) return nullptr;
    auto lv = finish_job(job->second, id);                    // built now, or waited for
    builder->jobs.erase(job);
    install_level(*this, id, std::move(lv));
    return levels[id].get();
}

// The levels a player on `l` may reach next: the act's levels touching
// it, and where its warps lead.
void want_nearby(const GameData& s, const Level& l) {
    const auto me = std::ranges::find(s.act1_layout, l.id, &d2d::drlg::Placed::level);
    if (me != s.act1_layout.end())
        for (const auto& p : s.act1_layout)
            if (p.level != l.id && p.x <= me->x + me->w && me->x <= p.x + p.w && p.y <= me->y + me->h && me->y <= p.y + p.h)
                s.want_level(p.level);
    for (const auto& w : l.warps) s.want_level(w.to);
}

// Load one DS1 + every DT1 it references (silently skips missing ones —
// some rogue-camp DS1s reference .tg1 tile-group files, which aren't
// present in 1.14d). Populates the level's ds1, dt1s, tile_lookup, walk
// and act1_pal on the scene. Idempotent, called once during load_scene.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path) {
    auto b = mpqs.try_read(ds1_path);
    if (!b) {
        d2d::log::warn("world: {} not found — placeholder mode", ds1_path);
        return;
    }
    scene.town.id = 1;                                  // ponytail: the only level loaded so far
    scene.town.ds1 = d2d::ds1::Map(*b);
    // Its DT1s as game.exe lists a preset room's (FUN_0066f240): LvlTypes 1's
    // files by LvlPrest 1's Dt1Mask, then Blank, InvisWal, Warp — never the
    // DS1's own list (townN1's names .tg1 files that don't exist).
    std::vector<std::string> files;
    auto table = [&](const char* n) {
        auto t = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt");
        return t ? d2d::txt::Table(*t) : d2d::txt::Table{};
    };
    const auto types = table("LvlTypes"), prest = table("LvlPrest");
    std::uint32_t mask = 0;
    for (std::size_t r = 0; r < prest.size(); ++r)
        if (prest.get(r, "Def") == "1") mask = std::uint32_t(std::atoll(std::string(prest.get(r, "Dt1Mask")).c_str()));
    for (std::size_t r = 0; r < types.size(); ++r)
        if (types.get(r, "Id") == "1")
            for (int i = 0; i < 32; ++i)
                if (const auto f = types.get(r, "File " + std::to_string(i + 1)); mask >> i & 1 && f != "0" && !f.empty())
                    files.push_back("data/global/tiles/" + std::string(f));
    for (const char* f : { "Act1/Outdoors/Blank.dt1", "Act1/Barracks/InvisWal.dt1", "Act1/Barracks/Warp.dt1" })
        files.push_back(std::string("data/global/tiles/") + f);
    scene.town.dt1s.reserve(files.size());
    for (const auto& f : files) {
        const auto mpq_path = ds1_path_to_mpq(f);
        auto db = mpqs.try_read(mpq_path);
        if (!db) continue;
        try {
            scene.town.dt1s.emplace_back(*db);
        } catch (const std::exception& e) {
            d2d::log::warn("world: {}: {}", mpq_path, e.what());
        }
    }
    finish_level(scene.town);
    const auto& m = scene.town.ds1;
    // The town start, as game.exe picks it on joining: DS1 special walls
    // (orientation 10/11) with main index 30..33 become the level's spawn
    // list (code at 0x667d09: main 30 sub n -> index n, 31 -> n+5,
    // 32 -> 10, 33 -> 11 (town-portal arrival)); a join asks for index 0,
    // which matches any of group 0 (indices 0..4) at random
    // (FUN_0066ac40), at subtile tile*5+3 (FUN_0061b060), then the nearest
    // free spot. ponytail: first match instead of a random one — each
    // Act 1 town DS1 has exactly one.
    for (const auto& L : m.walls())
        for (std::size_t i = 0; i < L.cells.size() && scene.town.start.first < 0; ++i) {
            const auto& t = L.cells[i];
            if ((t.wall_type == 10 || t.wall_type == 11) && t.style == 30 && t.sequence <= 4)
                scene.town.start = { (float(i % std::size_t(m.width())) * 5 + 3 + 0.5f) / 5,
                                     (float(i / std::size_t(m.width())) * 5 + 3 + 0.5f) / 5 };
        }
    for (const auto& L : m.walls())                      // 33: where town portals open (index 11)
        for (std::size_t i = 0; i < L.cells.size(); ++i)
            if (const auto& t = L.cells[i]; (t.wall_type == 10 || t.wall_type == 11) && t.style == 33 && scene.town.portal_spot.first < 0)
                scene.town.portal_spot = { (float(i % std::size_t(m.width())) * 5 + 3 + 0.5f) / 5,
                                           (float(i / std::size_t(m.width())) * 5 + 3 + 0.5f) / 5 };
    if (auto pb = mpqs.try_read(R"(data\global\palette\ACT1\pal.dat)"))
        scene.act1_pal = d2d::palette::Palette(*pb);
    // Its 32 light levels: PL2 +0x400, 256 indices a level, level 31 as is and
    // 0 black; the software renderer draws a pixel at light v through level
    // v >> 3 (FUN_004f8050).
    if (auto lb = mpqs.try_read(R"(data\global\palette\ACT1\Pal.pl2)"); lb && lb->size() >= 0x400 + 32 * 256)
        for (std::size_t l = 0; l < 32; ++l) {
            std::array<d2d::palette::Rgba, 256> e{};
            for (std::size_t i = 0; i < 256; ++i) e[i] = scene.act1_pal[std::uint8_t((*lb)[0x400 + l * 256 + i])];
            e[0].a = 0;
            scene.act1_lit[l] = d2d::palette::Palette(e);
        }
    d2d::log::info("  World: {} {}x{}, {} of {} tilesets, {} tiles", ds1_path, m.width(), m.height(),
                   scene.town.dt1s.size(), files.size(), scene.town.tile_lookup.size());
}

}  // namespace

// A game on another map seed: act 1 laid out again, the camp rebuilt with
// its units, the other levels dropped (they build again when wanted). The
// World and its Town must enter afterwards; nothing may point into the
// old levels.
void set_map_seed(Scene& scene, std::uint32_t seed) {
    if (seed == scene.map_seed || !scene.builder || !scene.builder->act1) return;
    for (auto& [id, job] : scene.builder->jobs) job.wait();
    scene.builder->jobs.clear();
    scene.levels.clear();
    const Level& o = scene.town;
    Level town{ .id = o.id, .name = o.name, .type = o.type, .layer = o.layer, .song = o.song, .ambience = o.ambience,
                .night_ambience = o.night_ambience, .day_event = o.day_event, .night_event = o.night_event,
                .event_delay = o.event_delay, .light = o.light, .rain = o.rain };
    scene.town = std::move(town);
    place_act1(scene, scene.mpqs, *scene.builder->act1, seed);
    scene.shrines.clear();                          // load_npcs reads Shrines.txt again
    load_npcs(scene, scene.mpqs);
    want_nearby(scene, scene.town);
    d2d::log::info("map seed {:#x}: {}", seed, scene.town.ds1.width() ? "act 1 laid out" : "no town");
}
