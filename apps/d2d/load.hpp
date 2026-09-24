// Asset loading: composites, NPCs, saves, load_scene, DS1/DT1 world.
#pragma once

#include "ui.hpp"

namespace {

// Forward decl — full body lives after Scene{} construction so it can use
// the same members without repeating field types.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path);

// Composite tokens: d2s class id -> CHARS folder (Assassin is "AI", its
// dev codename), D2 mode ids we use, and layer names by COF type.
constexpr const char* kCharCode[7] = { "AM", "SO", "NE", "PA", "BA", "DZ", "AI" };
constexpr int kModeNU = 1, kModeRN = 3, kModeTN = 5, kModeTW = 6;

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
constexpr const char* kModeCode[7] = { "DT", "NU", "WL", "RN", "GH", "TN", "TW" };
constexpr const char* kLayerCode[16] = {
    "HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8",
};

// Load one composite: COF <CC><mode><wclass>, then per COF layer the DCC
// <CC><LY><component><mode><layer wclass>. The weapon class comes from
// the hand/shield bytes (compcode::weapon_class, falling back to hth when
// D2 would reject the combination). Empty body layers wear "lit" (a bare
// head under a circlet, say); empty RH/LH/SH draw nothing.
// ponytail: tints (appearance+16) and the dead-hardcore ghost aren't
// applied yet; add the item colormaps when a portrait's colours matter.
Scene::PlayerAnim load_composite(const d2d::mpq::Stack& mpqs,
                                 const std::vector<d2d::compcode::Entry>& comp,
                                 int cls, int mode, const Scene::Appearance& gfx) {
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
        const std::string_view pv(path);
        out.name = std::string(pv.substr(pv.rfind('\\') + 1, pv.rfind('.') - pv.rfind('\\') - 1));
        for (auto& ch : out.name) ch = char(std::toupper(ch));
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
            if (auto d = mpqs.try_read(path)) out.layers[L.type] = d2d::dcc::Sprite(*d);
        }
    } catch (const std::exception& e) {
        d2d::log::warn("{}: {}", path, e.what());
    }
    return out;
}

const Scene::PlayerAnim& Scene::composite(int d2s_class, int mode, const Appearance& gfx) const {
    std::array<std::uint8_t, 18> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.end(), key.begin() + 2);
    auto it = composites.find(key);
    if (it == composites.end()) {
        it = composites.emplace(key, load_composite(mpqs, comp, d2s_class, mode, gfx)).first;
        if (const auto a = anim_speed.find(it->second.name); a != anim_speed.end()) it->second.speed = a->second;
    }
    return it->second;
}

// Load an NPC/object composite: COF <root>\<code>\COF\<code><mode><BaseW>,
// then per COF layer <root>\<code>\<LY>\<code><LY><comp><mode><wclass>
// with the recipe's component for that layer ("lit" when blank).
Scene::PlayerAnim load_npc_composite(const d2d::mpq::Stack& mpqs, const Scene::Npc& n,
                                     const std::string& mode) {
    Scene::PlayerAnim out;
    char path[256];
    std::snprintf(path, sizeof(path), R"(data\global\%s\%s\COF\%s%s%s.cof)",
                  n.root.c_str(), n.code.c_str(), n.code.c_str(), mode.c_str(), n.base_w.c_str());
    auto cof = mpqs.try_read(path);
    if (!cof) return out;
    try {
        out.cof = d2d::cof::Cof(*cof);
        const std::string_view pv(path);
        out.name = std::string(pv.substr(pv.rfind('\\') + 1, pv.rfind('.') - pv.rfind('\\') - 1));
        for (auto& ch : out.name) ch = char(std::toupper(ch));
        for (const auto& L : out.cof.layer_defs()) {
            if (L.type >= 16) continue;
            std::string comp = n.comp[L.type].empty() ? "lit" : n.comp[L.type];
            std::string lw = L.weapon_class;
            for (auto* t : { &comp, &lw }) for (auto& ch : *t) ch = char(std::toupper(ch));
            std::snprintf(path, sizeof(path), R"(data\global\%s\%s\%s\%s%s%s%s%s.dcc)",
                          n.root.c_str(), n.code.c_str(), kLayerCode[L.type], n.code.c_str(),
                          kLayerCode[L.type], comp.c_str(), mode.c_str(), lw.c_str());
            if (auto d = mpqs.try_read(path)) out.layers[L.type] = d2d::dcc::Sprite(*d);
        }
    } catch (const std::exception& e) {
        d2d::log::warn("{}: {}", path, e.what());
    }
    return out;
}

const Scene::PlayerAnim& Scene::npc_anim(const Npc& n, std::string_view mode) const {
    const std::string m(mode);
    const auto key = n.root + "/" + n.code + "/" + m;
    auto it = npc_anims.find(key);
    if (it == npc_anims.end()) {
        it = npc_anims.emplace(key, load_npc_composite(mpqs, n, m)).first;
        if (const auto a = anim_speed.find(it->second.name); a != anim_speed.end()) it->second.speed = a->second;
    }
    return it->second;
}

const d2d::dc6::Sprite* Scene::item_sprite(const d2d::d2s::Item& item) const {
    const auto info = item_info.find(item.code);
    if (info == item_info.end()) return nullptr;
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

// Act 1 town NPCs from the DS1's type-1 objects: id -> MonPreset.txt
// (Act 1 rows) Place -> MonStats2 row (by Id) -> MonStats row at the same
// index for the monster token (both are indexed by hcIdx; our MonStats is
// the CD's, whose names differ, 1.14d's being a compressed patch entry).
// Positions are in subtiles; a unit stands at its subtile's centre.
// ponytail: act 1 only, NU idle only; "place_*" spawn markers skipped.
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
    std::unordered_map<std::string, std::size_t> ms2_row;
    for (std::size_t r = 0; r < ms2.size(); ++r) ms2_row.emplace(std::string(ms2.get(r, "Id")), r);
    static constexpr const char* kVariant[16] = {
        "HDv", "TRv", "LGv", "RAv", "LAv", "RHv", "LHv", "SHv",
        "S1v", "S2v", "S3v", "S4v", "S5v", "S6v", "S7v", "S8v",
    };
    for (const auto& o : scene.world_ds1.objects()) {
        if (o.type != 1 || o.id < 0 || std::size_t(o.id) >= act1.size()) continue;
        const std::string place(preset.get(act1[std::size_t(o.id)], "Place"));
        const auto it = ms2_row.find(place);
        if (it == ms2_row.end()) continue;           // place_* markers etc.
        Scene::Npc n;
        n.root   = "monsters";
        n.mode   = "NU";
        n.code   = std::string(ms.get(it->second, "Code"));
        n.hc_idx = std::atoi(std::string(ms.get(it->second, "hcIdx")).c_str());
        n.id     = std::string(ms.get(it->second, "Id"));
        n.base_w = std::string(ms2.get(it->second, "BaseW"));
        n.size_x = std::atoi(std::string(ms2.get(it->second, "SizeX")).c_str());
        n.size_y = std::atoi(std::string(ms2.get(it->second, "SizeY")).c_str());
        if (const auto v = ms.get(it->second, "Velocity"); !v.empty()) n.velocity = float(std::atoi(std::string(v).c_str()));
        // Hover name: MonStats' string key, only for units MonStats2 marks
        // selectable (isSel) — not the chicken or the camp's guard rogues,
        // whose name key "Dummy" reads "an evil force".
        if (ms2.get(it->second, "isSel") == "1") {
            std::string key(ms.get(it->second, "NameStr"));        // 1.14d; "namco" on the CD
            if (key.empty()) key = std::string(ms.get(it->second, "namco"));
            auto v = lookup_string(scene, key);
            n.name = v ? u16_to_latin1(*v) : key;
        }
        if (n.code.empty()) continue;
        if (n.base_w.empty()) n.base_w = "hth";
        for (std::size_t l = 0; l < 16; ++l) {
            if (ms2.get(it->second, kLayerCode[l]) != "1") continue;
            const auto v = ms2.get(it->second, kVariant[l]);
            const auto first = v.substr(0, v.find(','));
            n.comp[l] = first.empty() ? "lit" : std::string(first);
        }
        n.x = (float(o.x) + 0.5f) / 5;
        n.y = (float(o.y) + 0.5f) / 5;
        for (const auto& pt : o.path)
            n.path.emplace_back((float(pt.x) + 0.5f) / 5, (float(pt.y) + 0.5f) / 5);
        scene.world_npcs.push_back(std::move(n));
    }

    // Type-2 objects: id -> objects.txt Id through game.exe's own preset
    // table (obj_preset.hpp), then that row's Token and layer flags. Start
    // mode: ON for things with a light in ON (torches, fires, the camp
    // waypoint), else NU. ponytail: D2 sets it per object in its InitFn.
    const auto objects = txt("objects");
    std::unordered_map<std::string, std::size_t> obj_row;
    for (std::size_t r = 0; r < objects.size(); ++r) obj_row.emplace(std::string(objects.get(r, "Id")), r);
    for (const auto& o : scene.world_ds1.objects()) {
        if (o.type != 2 || o.id < 0 || o.id >= 150) continue;
        const int oid = kObjPreset[0][std::size_t(o.id)];    // act 1
        const auto it = obj_row.find(std::to_string(oid));
        if (oid == 0 || it == obj_row.end()) continue;
        const auto r = it->second;
        Scene::Npc n;
        n.root   = "objects";
        n.code   = std::string(objects.get(r, "Token"));
        n.operate_fn = std::atoi(std::string(objects.get(r, "OperateFn")).c_str());
        n.base_w = "hth";
        const bool on = objects.get(r, "Mode2") == "1" && !objects.get(r, "Lit2").empty()
                     && objects.get(r, "Lit2") != "0";
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
        if (n.code.empty()) continue;
        n.x = (float(o.x) + 0.5f) / 5;
        n.y = (float(o.y) + 0.5f) / 5;
        scene.world_npcs.push_back(std::move(n));
    }

    // Footprints into the walk grid, centred on each unit's subtile.
    // ponytail: static — fine while NPCs only idle; moving units need a
    // separate occupancy layer.
    const int ww = scene.world_ds1.width() * 5, wh = scene.world_ds1.height() * 5;
    for (const auto& n : scene.world_npcs) {
        if (!n.path.empty()) continue;           // walkers don't hold a spot
        const int cx = int(n.x * 5), cy = int(n.y * 5);
        for (int y = cy - n.size_y / 2; y < cy - n.size_y / 2 + n.size_y; ++y)
            for (int x = cx - n.size_x / 2; x < cx - n.size_x / 2 + n.size_x; ++x)
                if (x >= 0 && y >= 0 && x < ww && y < wh)
                    scene.world_walk[std::size_t(y) * std::size_t(ww) + std::size_t(x)] |= 0x01;
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
            scene.item_info[std::string(t->get(r, "code"))] = {
                std::string(t->get(r, "invfile")),
                std::max(1, std::atoi(std::string(t->get(r, "invwidth")).c_str())),
                std::max(1, std::atoi(std::string(t->get(r, "invheight")).c_str())),
                std::string(t->get(r, "namestr")), std::string(t->get(r, "type")),
                t == &armor ? 1 : t == &weapons ? 2 : 0,
                t == &armor && !t->get(r, "belt").empty() ? std::atoi(std::string(t->get(r, "belt")).c_str()) : -1 };
    for (std::size_t r = 0; r < types.size(); ++r) {
        const std::string code(types.get(r, "Code"));
        scene.type_equiv[code] = { std::string(types.get(r, "Equiv1")), std::string(types.get(r, "Equiv2")) };
        auto& g = scene.type_invgfx[code];
        for (int i = 0; i < 6; ++i) g[std::size_t(i)] = std::string(types.get(r, "InvGfx" + std::to_string(i + 1)));
    }
    {
        static constexpr const char* kVendorCol[17] = { "Akara", "Gheed", "Charsi", "Fara", "Lysander", "Drognan",
            "Hralti", "Alkor", "Ormus", "Elzix", "Asheara", "Cain", "Halbu", "Jamella", "Malah", "Larzuk", "Drehya" };
        for (const auto* t : { &armor, &weapons, &misc })
            for (std::size_t r = 0; r < t->size(); ++r) {
                const std::string code(t->get(r, "code"));
                auto n = [&](std::string c) { return std::atoi(std::string(t->get(r, c)).c_str()); };
                scene.item_base[code] = { t == &armor ? n("minac") : 0, t == &armor ? n("maxac") : 0, n("cost"),
                                          t->get(r, "stackable") == "1" };
                if (t->get(r, "spawnable") != "1") continue;
                for (std::size_t v = 0; v < 17; ++v) {
                    const std::string V = kVendorCol[v];
                    Scene::VendorItem vi{ code, n(V + "Min"), n(V + "Max"), n(V + "MagicMin"), n(V + "MagicMax"),
                                          n(V + "MagicLvl"), t->get(r, "PermStoreItem") == "1" };
                    if (vi.max > 0 || vi.magic_max > 0) scene.vendor_items[v].push_back(std::move(vi));
                }
            }
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
                std::uint32_t spd; std::memcpy(&spd, b->data() + p + 12, 4);
                scene.anim_speed.emplace(std::move(name), spd);
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
            if (lv.get(r, "Id") == "1") scene.town_level_type = std::atoi(std::string(lv.get(r, "LevelType")).c_str());
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
    // Rogue Encampment (Levels.txt Id 1) -> SoundEnv -> SoundEnviron Song /
    // Day Ambience (Sounds.txt indices).
    {
        const auto lv = txt("Levels"), se = txt("SoundEnviron");
        for (std::size_t r = 0; r < lv.size(); ++r) {
            if (lv.get(r, "Id") != "1") continue;
            const auto env = lv.get(r, "SoundEnv");
            for (std::size_t e = 0; e < se.size(); ++e)
                if (se.get(e, "Index") == env) {
                    scene.town_song = std::atoi(std::string(se.get(e, "Song")).c_str());
                    scene.town_ambience = std::atoi(std::string(se.get(e, "Day Ambience")).c_str());
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
                                             st.get(r, "Loop") == "1", st.get(r, "Music Vol") == "1" };
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
        scene.prefix_cost = pairs("MagicPrefix", "multiply", "add", true);
        scene.suffix_cost = pairs("MagicSuffix", "multiply", "add", true);
        scene.unique_cost = pairs("UniqueItems", "cost mult", "cost add", false);
        scene.set_cost    = pairs("SetItems", "cost mult", "cost add", false);
        if (auto b = mpqs.try_read(R"(data\global\excel\npc.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r) {
                auto n = [&](const char* c) { return std::atoi(std::string(t.get(r, c)).c_str()); };
                Scene::NpcPrice p{ n("buy mult"), n("sell mult"), n("rep mult"),
                                   { n("questflag A"), n("questflag B"), n("questflag C") },
                                   { n("questbuymult A"), n("questbuymult B"), n("questbuymult C") },
                                   { n("questsellmult A"), n("questsellmult B"), n("questsellmult C") },
                                   { n("questrepmult A"), n("questrepmult B"), n("questrepmult C") },
                                   { n("max buy"), n("max buy (N)"), n("max buy (H)") } };
                scene.npc_prices[std::string(t.get(r, "npc"))] = p;
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
    for (std::size_t c = 0; c < 7 && c < charstats.size(); ++c) {
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
    struct Entry { d2d::d2s::Header header; std::vector<d2d::d2s::Item> items; d2d::d2s::Stats stats; };
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
    scene.saves.clear(); scene.save_items.clear(); scene.save_stats.clear();
    for (auto& en : out) {
        scene.saves.push_back(std::move(en.header));
        scene.save_items.push_back(std::move(en.items));
        scene.save_stats.push_back(en.stats);
    }
    d2d::log::info("Characters: {} in {}", scene.saves.size(), dir.string());
}

std::optional<Scene> load_scene(const fs::path& data_dir, const fs::path& patch_installer) {
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

        Scene scene = Scene{
            // Sky = title/credits (game.exe hardcodes palette\sky\pal.pl2 in
            // 5 sites of the menu loader — docs/research/re/frontend-menu-table.md).
            .pal            = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\Sky\pal.dat)")),
            // fechar = "Front End CHARacter", the char-select/creation palette.
            // game.exe's FUN_00435580 (char-select init) loads it right after
            // the char-select asset loader (FUN_004326f0). Firelit warm tones
            // — night camp scene lit by the campfire the classes stand around.
            .charselect_pal = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\fechar\pal.dat)")),
            .sky_pl2        = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\Sky\Pal.PL2)")),
            .fechar_pl2     = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\fechar\Pal.PL2)")),
            .bg          = d2d::dc6::Sprite(*title),
            .logo_static = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\Diablo2.dc6)")),
            .logo_bl     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackLeft.DC6)")),
            .logo_br     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackRight.DC6)")),
            .logo_fl     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireLeft.DC6)")),
            .logo_fr     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireRight.DC6)")),
            .btn_wide    = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank.dc6)")),
            .btn_wide2   = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank02.dc6)")),
            .btn_narrow  = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\NarrowButtonBlank.dc6)")),
            .btn_short   = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\ShortButtonBlank.dc6)")),
            .credits_bg  = [&] {
                // creditsbckgexpand.dc6 (LoD) → creditsbckg.dc6 (classic).
                auto b = mpqs.try_read(R"(data\global\ui\CharSelect\creditsbckgexpand.dc6)");
                if (!b) b = mpqs.read(R"(data\global\ui\CharSelect\creditsbckg.dc6)");
                return d2d::dc6::Sprite(*b);
            }(),
            .charcreate_bg = [&] {
                auto b = mpqs.try_read(R"(data\global\ui\FrontEnd\charactercreationscreenEXP.dc6)");
                if (!b) b = mpqs.read(R"(data\global\ui\FrontEnd\CharacterCreate.dc6)");
                return d2d::dc6::Sprite(*b);
            }(),
            .fire       = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\fire.DC6)")),
            .medium_button     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumButtonBlank.dc6)")),
            .medium_sel_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumSelButtonBlank.dc6)")),
            .textbox           = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\textbox.dc6)")),
            .clickbox          = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\clickbox.dc6)")),
            .charselect_bg     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\characterselectscreenEXP.dc6)")),
            .charselect_box    = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\charselectbox.dc6)")),
            .charselect_scroll = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\joingamescrollbars.dc6)")),
            .tall_button       = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\TallButtonBlank.dc6)")),
            .cursor            = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CURSOR\ohand.dc6)")),
            .class_anims = [&] {
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
            }(),
            .font        = d2d::font::Font(
                             mpqs.read(R"(data\local\FONT\LATIN\font16.tbl)"),
                             d2d::dc6::Sprite(mpqs.read(R"(data\local\FONT\LATIN\font16.dc6)"))),
            .credits     = [&] {
                auto b = mpqs.try_read(R"(data\local\UI\ENG\ExpansionCredits.txt)");
                if (!b) b = mpqs.try_read(R"(data\local\ui\eng\Credits.txt)");
                return b ? parse_credits_utf16(*b) : std::vector<std::string>{};
            }(),
            // Frontend button labels (IDs 0x13f2..0x13f7) live in the base
            // string.tbl per probe. patchstring.tbl (826 entries) overrides
            // specific IDs when Blizzard shipped patches; expansionstring.tbl
            // (2788 entries) carries LoD-specific additions. For MVP we use
            // string.tbl directly; when a subsystem needs a patch-shifted
            // entry, load all three and query in order (patch → expansion →
            // base).
            .strings     = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\string.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }(),
            .patch_strings = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\patchstring.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }(),
            .exp_strings = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\expansionstring.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }(),
        };
        // Rogue-camp world data — separate call so a DS1/DT1 miss doesn't
        // nuke the whole scene; the InGame screen falls back to the credits
        // placeholder when world is empty.
        d2d::log::info("Loading frontend assets... done ({} ms)", d2d::log::ms() - t0);
        d2d::log::info("  Strings: {} base, {} patch, {} expansion; {} credit lines",
                       scene.strings.size(), scene.patch_strings.size(), scene.exp_strings.size(),
                       scene.credits.size());
        load_world(scene, mpqs,
                   R"(data\global\tiles\ACT1\TOWN\townE1.ds1)");
        load_composite_data(scene, mpqs);
        load_npcs(scene, mpqs);
        d2d::log::info("Loading game data... done ({} ms)", d2d::log::ms() - t0);
        d2d::log::info("  Items: {}; sounds: {}; town NPCs/objects: {}",
                       scene.item_tables ? "tables loaded" : "no item tables",
                       scene.sounds.size(), scene.world_npcs.size());
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

// Translate a DS1-embedded tileset path (e.g. "\d2\data\global\tiles\act1\
// town\floor.dt1") into the MPQ path we can hand to Stack::try_read. The
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

// Load one DS1 + every DT1 it references (silently skips missing ones —
// some rogue-camp DS1s reference .tg1 tile-group files, which aren't
// present in 1.14d). Populates world_ds1, world_dt1s, world_floor_lookup
// and act1_pal on the scene. Idempotent, called once during load_scene.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path) {
    auto b = mpqs.try_read(ds1_path);
    if (!b) {
        d2d::log::warn("world: {} not found — placeholder mode", ds1_path);
        return;
    }
    scene.world_ds1 = d2d::ds1::Map(*b);
    scene.world_dt1s.reserve(scene.world_ds1.files().size());
    for (const auto& f : scene.world_ds1.files()) {
        const auto mpq_path = ds1_path_to_mpq(f);
        auto db = mpqs.try_read(mpq_path);
        if (!db) continue;   // .tg1 or otherwise-missing — silent skip
        try {
            scene.world_dt1s.emplace_back(*db);
        } catch (const std::exception& e) {
            d2d::log::warn("world: {}: {}", mpq_path, e.what());
        }
    }
    // Populate the (style, seq, type) lookup across all DT1s. First DT1
    // to define a tuple wins — matches how D2's renderer resolves tile
    // priority against its Stack-ordered tileset list. Covers floors,
    // walls, trees, roofs, shadows in one map.
    for (const auto& dt1 : scene.world_dt1s) {
        for (const auto& t : dt1.tiles()) {
            const auto k = tile_key(t.style, t.sequence, t.type);
            scene.world_tile_lookup.try_emplace(k, &t);
        }
    }
    // Collision grid from the same tiles the renderer draws: floors
    // (type 0) and walls/objects, but not shadows (13) or roofs (15).
    // ponytail: subtile flag k taken as (x, y) = (k % 5, k / 5); objects
    // and NPCs (DS1 object list) don't block yet.
    const auto& m = scene.world_ds1;
    const int ww = m.width() * 5;
    scene.world_walk.assign(std::size_t(ww) * std::size_t(m.height()) * 5, 0);
    auto stamp = [&](int gx, int gy, int style, int seq, int type) {
        const auto it = scene.world_tile_lookup.find(tile_key(style, seq, type));
        if (it == scene.world_tile_lookup.end()) return;
        for (int k = 0; k < 25; ++k)
            scene.world_walk[std::size_t(gy * 5 + k / 5) * std::size_t(ww) + std::size_t(gx * 5 + k % 5)]
                |= it->second->subtile_flags[std::size_t(k)];
    };
    for (int gy = 0; gy < m.height(); ++gy)
        for (int gx = 0; gx < m.width(); ++gx) {
            const std::size_t off = std::size_t(gy) * std::size_t(m.width()) + std::size_t(gx);
            for (const auto& fl : m.floors())
                if (!fl.cells[off].hidden) stamp(gx, gy, fl.cells[off].style, fl.cells[off].sequence, 0);
            for (const auto& wl : m.walls()) {
                const auto& c = wl.cells[off];
                if (c.hidden || c.wall_type == 0 || c.wall_type == 13 || c.wall_type == 15) continue;
                stamp(gx, gy, c.style, c.sequence, c.wall_type);
            }
        }
    if (auto pb = mpqs.try_read(R"(data\global\palette\ACT1\pal.dat)"))
        scene.act1_pal = d2d::palette::Palette(*pb);
    d2d::log::info("  World: {} {}x{}, {} of {} tilesets, {} tiles", ds1_path, m.width(), m.height(),
                   scene.world_dt1s.size(), m.files().size(), scene.world_tile_lookup.size());
}

}  // namespace
