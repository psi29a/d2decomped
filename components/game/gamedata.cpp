// GameData's runtime side: levels built on demand (the level builder),
// their populations, the COF timings the World reads. A translation unit
// of its own; load.hpp fills GameData at start.
#include "gamedata.hpp"

#include <mutex>

namespace d2d::game {

// A composite's COF, the World's part of loading it: the file, its name
// (animdata's key) and timing.
CofAnim open_cof(const d2d::mpq::Stack& mpqs, const std::string& path) {
    CofAnim out;
    const auto b = mpqs.try_read(path);
    if (!b) return out;
    try {
        out.cof = d2d::cof::Cof(*b);
        out.timing.cof_speed = out.cof.speed(); out.timing.cof_frames = out.cof.frames_per_direction();
        out.timing.directions = out.cof.directions();
        const std::string_view pv(path);
        out.timing.name = std::string(pv.substr(pv.rfind('\\') + 1, pv.rfind('.') - pv.rfind('\\') - 1));
        for (auto& ch : out.timing.name) ch = char(std::toupper(ch));
        out.path = path;
        out.ok = true;
    } catch (const std::exception& e) {
        d2d::log::warn("{}: {}", path, e.what());
    }
    return out;
}
// A player class's mode: COF <CC><mode><wclass>, the weapon class from the
// hand/shield bytes (compcode::weapon_class), hth when that one's missing.
CofAnim player_cof(const d2d::mpq::Stack& mpqs, const std::vector<d2d::compcode::Entry>& comp, int cls, int mode,
                   const std::array<std::uint8_t, 32>& gfx) {
    const char* cc = kCharCode[cls];
    const std::string wc(comp.empty() ? std::string_view{} : d2d::compcode::weapon_class(cls, comp, gfx[5], gfx[6], gfx[7]));
    auto path = [&](std::string_view w) { return std::format(R"(data\global\CHARS\{}\COF\{}{}{}.cof)", cc, cc, kModeCode[mode], w); };
    if (auto c = open_cof(mpqs, path(wc.empty() ? "hth" : wc)); c.ok) return c;
    return open_cof(mpqs, path("hth"));
}
// An NPC's or object's: <root>\<code>\COF\<code><mode><BaseW>.
CofAnim npc_cof(const d2d::mpq::Stack& mpqs, const Npc& n, std::string_view mode) {
    return open_cof(mpqs, std::format(R"(data\global\{}\{}\COF\{}{}{}.cof)", n.root, n.code, n.code, mode, n.base_w));
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
    return timing_of(*this, npc_timings, key, [&] { return npc_cof(mpqs, n, mode).timing; });
}
const GameData::AnimTiming& GameData::composite_timing(int d2s_class, int mode, const std::array<std::uint8_t, 32>& gfx) const {
    std::array<std::uint8_t, 18> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.begin() + 16, key.begin() + 2);
    return timing_of(*this, composite_timings, key, [&] { return player_cof(mpqs, comp, d2s_class, mode, gfx).timing; });
}

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

Npc monster_npc(const GameData& scene, const d2d::txt::Table& ms, const d2d::txt::Table& ms2,
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
    n.trans_lvl = std::atoi(std::string(ms.get(row, "TransLvl")).c_str());
    n.no_unique_shift = ms2.get(row2, "noUniqueShift") == "1";
    n.utrans = { std::atoi(std::string(ms2.get(row2, "Utrans")).c_str()), std::atoi(std::string(ms2.get(row2, "Utrans(N)")).c_str()),
                 std::atoi(std::string(ms2.get(row2, "Utrans(H)")).c_str()) };
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

}  // namespace d2d::game
