// Definitions for gamedata_load.hpp: GameData from the MPQs.
#include "gamedata_load.hpp"
#include "log.hpp"

namespace d2d::game {

namespace {

// Load one DS1 + every DT1 it references (silently skips missing ones —
// some rogue-camp DS1s reference .tg1 tile-group files, which aren't
// present in 1.14d). Fills the town's ds1, dt1s, tile_lookup and walk
// grid; place_act1 calls it.
void load_town(GameData& scene, d2d::mpq::Stack& mpqs, const char* ds1_path) {
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
    d2d::log::info("  World: {} {}x{}, {} of {} tilesets, {} tiles", ds1_path, m.width(), m.height(),
                   scene.town.dt1s.size(), files.size(), scene.town.tile_lookup.size());
}

// Act 1's layout from the map seed picks the town's DS1 (the side the
// Blood Moor went) and where both sit.
void place_act1(GameData& scene, d2d::mpq::Stack& mpqs, const d2d::drlg::OutdoorAssets& act1, std::uint32_t map_seed) {
    scene.map_seed = map_seed;
    const auto layout = d2d::drlg::act1_from_map_seed(d2d::drlg::level_defs(act1.levels), map_seed);
    static constexpr std::array<const char*, 4> kTown = { R"(data\global\tiles\ACT1\TOWN\townN1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townE1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townS1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townW1.ds1)" };
    load_town(scene, mpqs, kTown[std::size_t(std::max(0, d2d::drlg::town_file(layout)))]);
    for (const auto& p : layout)
        if (p.level == 1) { scene.town.world_x = p.x; scene.town.world_y = p.y; }
    scene.act1_layout = layout;
}

// Monster tables (MonStats, MonStats2, MonLvl), the Blood Moor's Levels.txt
// monster columns, and its rooms populated (components/rules/monsters.hpp).
// ponytail: every room at load, in cell order, normal difficulty — game.exe
// populates a room when it first activates (so the game seed's order
// follows the player) and knows the game's difficulty; the game seed is
// the map seed here.
void load_monsters(GameData& scene, const d2d::mpq::Stack& mpqs) {
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
        GameData::MonSound m{ { id("Attack1"), id("Attack2") }, { id("Weapon1"), id("Weapon2") }, { n("Att1Del"), n("Att2Del") },
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
    for (std::size_t r = 0; r < mt.size(); ++r) {
        auto g = [&](std::string c) { return num(mt.get(r, c)); };
        const std::string name(mt.get(r, "Missile"));
        bool used = name == "arrow" || name == "denofevillight" || skill_missiles.contains(name)    // the rogue merc's, the Den's light beams, skills',
                 || std::ranges::contains(d2d::rules::kTrapMissile, std::string_view(name))    // chest traps
                 || std::ranges::contains(d2d::rules::kBossMissile, std::string_view(name));   // a unique's mods
        for (const auto& t : M.types) used = used || t.miss_a2 == name;
        if (!used) continue;
        GameData::MissileInfo mi;
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
        mi.cel_file = mt.get(r, "CelFile");
        scene.missiles.emplace(name, std::move(mi));
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
                                           { std::string(su.get(r, "TC")), std::string(su.get(r, "TC(N)")), std::string(su.get(r, "TC(H)")) },
                                           { num(su.get(r, "Utrans")), num(su.get(r, "Utrans(N)")), num(su.get(r, "Utrans(H)")) } });
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

void load_skills(GameData& scene, const d2d::mpq::Stack& mpqs) {
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
void load_npcs(GameData& scene, const d2d::mpq::Stack& mpqs) {
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
            scene.mercs.emplace(id, GameData::Merc{ std::move(n), std::string(hire.get(r, "NameFirst")) });
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
void load_tables(GameData& scene, const d2d::mpq::Stack& mpqs) {
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
    int transform = 1;
    for (const char* n : { "grey", "grey2", "gold", "brown", "greybrown", "invgrey", "invgrey2", "invgreybrown" })
        if (auto b = mpqs.try_read(std::string(R"(data\global\items\palette\)") + n + ".dat"); b && b->size() >= 21 * 256) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(b->data());
            scene.colormaps[std::size_t(transform++)].assign(p, p + 21 * 256);
        } else ++transform;
    scene.item_types.emplace(types);
    if (const auto ov = txt("Overlay"); ov.size() > 0)
        for (std::size_t r = 0; r < ov.size(); ++r) {
            auto n = [&](const char* c) { return std::atoi(std::string(ov.get(r, c)).c_str()); };
            GameData::OverlayInfo o;
            o.file = std::string(ov.get(r, "Filename"));
            o.frames = std::max(n("Frames"), 1); o.x = n("Xoffset"); o.y = n("Yoffset"); o.rate = n("AnimRate");
            o.trans = n("Trans"); o.radius = n("Radius"); o.init_radius = n("InitRadius"); o.predraw = n("PreDraw") != 0;
            o.height = { n("Height1"), n("Height2"), n("Height3"), n("Height4") };
            scene.overlays.emplace(std::string(ov.get(r, "overlay")), std::move(o));
        }
    auto overlay = [&](std::string_view name) -> const GameData::OverlayInfo* {
        const auto it = scene.overlays.find(std::string(name));
        return it == scene.overlays.end() || it->second.file.empty() || it->second.file == "null" ? nullptr : &it->second;
    };
    if (const auto st = txt("States"); st.size() > 0)
        for (std::size_t r = 0; r < st.size(); ++r) {
            GameData::StateInfo i;
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
                GameData::AnimInfo a;
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
    {
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
        for (auto& a : scene.waypoint_levels) std::ranges::sort(a, {}, &GameData::WaypointLevel::wp);
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
    auto& nm = scene.item_names;
    nm.unique   = keys("UniqueItems", "index", false);
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

}  // namespace

std::optional<GameData> load_game_data(const fs::path& data_dir, const fs::path& patch_installer, std::uint32_t map_seed) {
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

        GameData scene;
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
        auto act1 = std::make_unique<d2d::drlg::OutdoorAssets>();
        d2d::drlg::load_outdoor_assets(*act1, [&](const std::string& p) { return mpqs.try_read(p); });
        place_act1(scene, mpqs, *act1, map_seed);
        // The other levels build when they're first wanted (GameData::level).
        scene.builder = std::make_shared<GameData::LevelBuilder>();
        scene.builder->act1 = std::move(act1);
        load_tables(scene, mpqs);
        load_npcs(scene, mpqs);
        load_monsters(scene, mpqs);
        load_skills(scene, mpqs);
        d2d::log::info("Loading game data... done ({} ms)", d2d::log::ms() - t0);
        d2d::log::info("  Items: {}; sounds: {}; town NPCs/objects: {}",
                       scene.item_tables ? "tables loaded" : "no item tables",
                       scene.sounds.size(), scene.town.npcs.size());
        scene.patched = patched;
        scene.data_dir = data_dir;
        scene.mpqs = std::move(mpqs);
        return scene;
    } catch (const std::exception& e) {
        d2d::log::error("load_game_data: {}", e.what());
        return std::nullopt;
    }
}

void set_map_seed(GameData& scene, std::uint32_t seed) {
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

}  // namespace d2d::game
