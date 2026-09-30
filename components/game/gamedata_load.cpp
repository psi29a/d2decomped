// Definitions for gamedata_load.hpp: GameData from the MPQs.
#include "gamedata_load.hpp"

#include "gamedata.hpp"
#include "log.hpp"

#include <compcode.hpp>
#include <d2s_items.hpp>
#include <drlg.hpp>
#include <drops.hpp>
#include <dt1.hpp>
#include <mpq.hpp>
#include <obj_preset.hpp>
#include <outdoor_data.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <txt.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {

// Load one DS1 + every DT1 it references (silently skips missing ones —
// some rogue-camp DS1s reference .tg1 tile-group files, which aren't
// present in 1.14d). Fills the town's ds1, dt1s, tile_lookup and walk
// grid; place_act1 calls it.
void load_town(GameData& game_data, d2d::mpq::Stack& mpqs, const char* ds1_path) {
    auto bytes = mpqs.try_read(ds1_path);
    if (!bytes) {
        d2d::log::warn("world: {} not found — placeholder mode", ds1_path);
        return;
    }
    game_data.town.id = 1;                                  // ponytail: the only level loaded so far
    game_data.town.ds1 = d2d::ds1::Map(*bytes);
    // Its DT1s as game.exe lists a preset room's (FUN_0066f240): LvlTypes 1's
    // files by LvlPrest 1's Dt1Mask, then Blank, InvisWal, Warp — never the
    // DS1's own list (townN1's names .tg1 files that don't exist).
    std::vector<std::string> files;
    auto table = [&](const char* name) {
        auto table_bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt");
        return table_bytes ? d2d::txt::Table(*table_bytes) : d2d::txt::Table{};
    };
    const auto types = table("LvlTypes"), prest = table("LvlPrest");
    std::uint32_t mask = 0;
    for (std::size_t row = 0; row < prest.size(); ++row)
        if (prest.get(row, "Def") == "1") mask = std::uint32_t(std::atoll(std::string(prest.get(row, "Dt1Mask")).c_str()));
    for (std::size_t row = 0; row < types.size(); ++row)
        if (types.get(row, "Id") == "1")
            for (int i = 0; i < 32; ++i)
                if (const auto file = types.get(row, "File " + std::to_string(i + 1)); mask >> i & 1 && file != "0" && !file.empty())
                    files.push_back("data/global/tiles/" + std::string(file));
    for (const char* file : { "Act1/Outdoors/Blank.dt1", "Act1/Barracks/InvisWal.dt1", "Act1/Barracks/Warp.dt1" })
        files.push_back(std::string("data/global/tiles/") + file);
    game_data.town.dt1s.reserve(files.size());
    for (const auto& file : files) {
        const auto mpq_path = ds1_path_to_mpq(file);
        auto dt1_bytes = mpqs.try_read(mpq_path);
        if (!dt1_bytes) continue;
        try {
            game_data.town.dt1s.emplace_back(*dt1_bytes, game_data.tile_pixels ? d2d::dt1::Pixels::decode : d2d::dt1::Pixels::skip);
        } catch (const std::exception& error) {
            d2d::log::warn("world: {}: {}", mpq_path, error.what());
        }
    }
    finish_level(game_data.town);
    const auto& map = game_data.town.ds1;
    // The town start, as game.exe picks it on joining: DS1 special walls
    // (orientation 10/11) with main index 30..33 become the level's spawn
    // list (code at 0x667d09: main 30 sub n -> index n, 31 -> n+5,
    // 32 -> 10, 33 -> 11 (town-portal arrival)); a join asks for index 0,
    // which matches any of group 0 (indices 0..4) at random
    // (FUN_0066ac40), at subtile tile*5+3 (FUN_0061b060), then the nearest
    // free spot. ponytail: first match instead of a random one — each
    // Act 1 town DS1 has exactly one.
    for (const auto& layer : map.walls())
        for (std::size_t i = 0; i < layer.cells.size() && game_data.town.start.first < 0; ++i) {
            const auto& tile = layer.cells[i];
            if ((tile.wall_type == 10 || tile.wall_type == 11) && tile.style == 30 && tile.sequence <= 4)
                game_data.town.start = { (float(i % std::size_t(map.width())) * 5 + 3 + 0.5f) / 5,
                                     (float(i / std::size_t(map.width())) * 5 + 3 + 0.5f) / 5 };
        }
    for (const auto& layer : map.walls())                      // 33: where town portals open (index 11)
        for (std::size_t i = 0; i < layer.cells.size(); ++i)
            if (const auto& tile = layer.cells[i]; (tile.wall_type == 10 || tile.wall_type == 11) && tile.style == 33 && game_data.town.portal_spot.first < 0)
                game_data.town.portal_spot = { (float(i % std::size_t(map.width())) * 5 + 3 + 0.5f) / 5,
                                           (float(i / std::size_t(map.width())) * 5 + 3 + 0.5f) / 5 };
    d2d::log::info("  World: {} {}x{}, {} of {} tilesets, {} tiles", ds1_path, map.width(), map.height(),
                   game_data.town.dt1s.size(), files.size(), game_data.town.tile_lookup.size());
}

// Act 1's layout from the map seed picks the town's DS1 (the side the
// Blood Moor went) and where both sit.
void place_act1(GameData& game_data, d2d::mpq::Stack& mpqs, const d2d::drlg::OutdoorAssets& act1, std::uint32_t map_seed) {
    game_data.map_seed = map_seed;
    const auto layout = d2d::drlg::act1_from_map_seed(d2d::drlg::level_defs(act1.levels), map_seed);
    static constexpr std::array<const char*, 4> kTown = { R"(data\global\tiles\ACT1\TOWN\townN1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townE1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townS1.ds1)",
                                                           R"(data\global\tiles\ACT1\TOWN\townW1.ds1)" };
    load_town(game_data, mpqs, kTown[std::size_t(std::max(0, d2d::drlg::town_file(layout)))]);
    for (const auto& placement : layout)
        if (placement.level == 1) { game_data.town.world_x = placement.x; game_data.town.world_y = placement.y; }
    game_data.act1_layout = layout;
}

// Monster tables (MonStats, MonStats2, MonLvl), unique names and mods, and
// every level's Levels.txt monster columns (components/rules/monsters.hpp).
void load_monsters(GameData& game_data, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* name) {
        auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt");
        return bytes ? d2d::txt::Table(*bytes) : d2d::txt::Table{};
    };
    const auto monstats = txt("MonStats"), ms2 = txt("MonStats2"), monlvl = txt("MonLvl"), levels_table = txt("Levels"), objgroup = txt("objgroup");
    if (monstats.size() == 0 || ms2.size() == 0) return;
    // objgroup.txt by Offset (0..132 in 1.14d): a rare column outside 0..N
    // still gets a slot, so we size by max Offset + 1.
    if (objgroup.size() > 0) {
        auto offset_num = [&](std::string_view text) { return std::atoi(std::string(text).c_str()); };
        int max_offset = 0;
        for (std::size_t row_index = 0; row_index < objgroup.size(); ++row_index) max_offset = std::max(max_offset, offset_num(objgroup.get(row_index, "Offset")));
        game_data.obj_groups.assign(std::size_t(max_offset) + 1, {});
        for (std::size_t row_index = 0; row_index < objgroup.size(); ++row_index) {
            const int off = offset_num(objgroup.get(row_index, "Offset"));
            if (off < 0 || off > max_offset) continue;
            auto& group = game_data.obj_groups[std::size_t(off)];
            for (int i = 0; i < 8; ++i) {
                group.id[std::size_t(i)]      = offset_num(objgroup.get(row_index, "ID" + std::to_string(i)));
                group.density[std::size_t(i)] = std::uint8_t(offset_num(objgroup.get(row_index, "DENSITY" + std::to_string(i))));
                group.weight[std::size_t(i)]  = std::uint8_t(offset_num(objgroup.get(row_index, "PROB" + std::to_string(i))));
            }
        }
    }
    const auto ms2_rows = id_rows(ms2);
    auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    auto& monsters = game_data.monsters;
    // An Id names the row at its place among the distinct Ids, as game.exe's
    // compiled tables resolve them: after MonStats' second cr_lancer8 every
    // later Id lands one row early (docs/research/re/bugs.md).
    for (std::size_t row = 0; row < monstats.size(); ++row) monsters.by_id.emplace(std::string(monstats.get(row, "Id")), int(monsters.by_id.size()));
    auto row = [&](std::string_view id) { return id.empty() ? -1 : monsters.row(std::string(id)); };
    static constexpr const char* kSfx[3] = { "", "(N)", "(H)" };
    monsters.types.resize(monstats.size());
    game_data.mon_npc.resize(monstats.size());
    for (std::size_t row_index = 0; row_index < monstats.size(); ++row_index) {
        auto& type_info = monsters.types[row_index];
        auto text = [&](std::string column) { return monstats.get(row_index, column); };
        type_info.id = text("Id"); type_info.code = text("Code"); type_info.name_key = text("NameStr"); type_info.ai_name = text("AI");
        type_info.base = row(text("BaseId"));
        type_info.min_grp = num(text("MinGrp")); type_info.max_grp = num(text("MaxGrp"));
        type_info.party_min = num(text("PartyMin")); type_info.party_max = num(text("PartyMax"));
        type_info.sparse = num(text("sparsePopulate")); type_info.rarity = num(text("Rarity"));
        type_info.tc_quest_id = num(text("TCQuestId")); type_info.tc_quest_cp = num(text("TCQuestCP"));
        type_info.tc_fixed = text("noRatio") == "1" || text("boss") == "1";
        type_info.minion = { row(text("minion1")), row(text("minion2")) };
        type_info.velocity = num(text("Velocity")); type_info.run = num(text("Run"));
        type_info.spawnable = text("isSpawn") == "1"; type_info.ranged = text("rangedtype") == "1"; type_info.killable = text("killable") == "1"; type_info.melee = text("isMelee") == "1";
        type_info.miss_a1 = text("MissA1"); type_info.miss_a2 = text("MissA2");
        for (std::size_t skill = 0; skill < 3; ++skill) { type_info.skill[skill] = text("Skill" + std::to_string(skill + 1)); type_info.sk_mode[skill] = text("Sk" + std::to_string(skill + 1) + "mode");
                                                   type_info.sk_lvl[skill] = num(text("Sk" + std::to_string(skill + 1) + "lvl")); }
        type_info.trans_lvl = num(text("TransLvl"));
        type_info.spawn = text("spawn"); type_info.spawn_mode = text("spawnmode"); type_info.spawn_x = num(text("spawnx")); type_info.spawn_y = num(text("spawny"));
        type_info.undead = text("hUndead") == "1" || text("lUndead") == "1"; type_info.demon = text("demon") == "1";
        for (int element = 0; element < 3; ++element) {
            static constexpr std::array<std::string_view, 5> kEl = { "fire", "ltng", "cold", "pois", "mag" };
            const std::string element_prefix = "El" + std::to_string(element + 1);
            type_info.el_mode[std::size_t(element)] = text(element_prefix + "Mode");
            const auto type = text(element_prefix + "Type");                  // frze counts as cold; life/mana/stam/stun/rand aren't damage here
            const auto element_type = std::ranges::find(kEl, type == "frze" ? std::string_view("cold") : type);
            type_info.el_type[std::size_t(element)] = element_type == kEl.end() ? -1 : int(element_type - kEl.begin());
        }
        type_info.sound = text("MonSound");
        type_info.montype = text("MonType");
        for (int difficulty = 0; difficulty < 3; ++difficulty) {
            const std::string x = kSfx[difficulty];
            auto& per_difficulty = type_info.diff[std::size_t(difficulty)];
            type_info.level[std::size_t(difficulty)] = num(text("Level" + x));
            per_difficulty = { num(text("MinHP" + x)), num(text("MaxHP" + x)), num(text("AC" + x)), num(text("Exp" + x)),
                  num(text("A1MinD" + x)), num(text("A1MaxD" + x)), num(text("A1TH" + x)),
                  num(text("A2MinD" + x)), num(text("A2MaxD" + x)), num(text("A2TH" + x)),
                  num(text("aidel" + x)), num(text("aidist" + x)), {}, std::string(text("TreasureClass1" + x)) };
            for (int i = 0; i < 8; ++i) per_difficulty.aip[std::size_t(i)] = num(text("aip" + std::to_string(i + 1) + x));
            static constexpr const char* kRes[6] = { "ResDm", "ResMa", "ResFi", "ResLi", "ResCo", "ResPo" };
            for (int i = 0; i < 6; ++i) per_difficulty.res[std::size_t(i)] = num(text(kRes[i] + x));
            per_difficulty.to_block = num(text("ToBlock" + x));
            type_info.tc_champion[std::size_t(difficulty)] = text("TreasureClass2" + x);
            type_info.tc_unique[std::size_t(difficulty)] = text("TreasureClass3" + x);
            type_info.tc_quest[std::size_t(difficulty)] = text("TreasureClass4" + x);
            per_difficulty.drain = text("Drain" + x).empty() ? 100 : num(text("Drain" + x));
            per_difficulty.cold_effect = num(text("coldeffect" + x));
            for (int element = 0; element < 3; ++element) {
                const std::string element_prefix = "El" + std::to_string(element + 1);
                per_difficulty.elements[std::size_t(element)] = { num(text(element_prefix + "Pct" + x)), num(text(element_prefix + "MinD" + x)), num(text(element_prefix + "MaxD" + x)), num(text(element_prefix + "Dur" + x)) };
            }
        }
        const auto found = ms2_rows.find(std::string(text("MonStatsEx")));
        if (found != ms2_rows.end()) {
            const auto monstats2_row = found->second;
            type_info.size = std::max(num(ms2.get(monstats2_row, "SizeX")), 1); type_info.melee_rng = num(ms2.get(monstats2_row, "MeleeRng"));
            type_info.base_w = ms2.get(monstats2_row, "BaseW");
            type_info.can_block = ms2.get(monstats2_row, "mBL") == "1";
            for (std::size_t layer = 0; layer < 16; ++layer) {
                auto variants = split_variants(ms2.get(monstats2_row, kVariant[layer]));
                type_info.choices[layer] = std::uint8_t(variants.size());
                if (ms2.get(monstats2_row, kLayerCode[layer]) == "1") type_info.parts[layer] = std::move(variants);
            }
        }
        game_data.mon_npc[row_index] = monster_npc(game_data, monstats, ms2, ms2_rows, row_index);
    }
    // MonLvl: the LoD columns (L-*); normal matches the classic ones.
    for (std::size_t row_index = 0; row_index < monlvl.size(); ++row_index) {
        const int level = num(monlvl.get(row_index, "Level"));
        if (level < 0 || level > 200) continue;
        if (std::size_t(level) >= monsters.lvl.size()) monsters.lvl.resize(std::size_t(level) + 1);
        auto& level_row = monsters.lvl[std::size_t(level)];
        for (int difficulty = 0; difficulty < 3; ++difficulty) {
            const std::string x = kSfx[difficulty];
            level_row.armor_class[std::size_t(difficulty)] = num(monlvl.get(row_index, "L-AC" + x)); level_row.to_hit[std::size_t(difficulty)] = num(monlvl.get(row_index, "L-TH" + x));
            level_row.hit_points[std::size_t(difficulty)] = num(monlvl.get(row_index, "L-HP" + x)); level_row.damage[std::size_t(difficulty)] = num(monlvl.get(row_index, "L-DM" + x));
            level_row.experience[std::size_t(difficulty)] = num(monlvl.get(row_index, "L-XP" + x));
        }
    }
    // MonSounds.txt.
    const auto snd = txt("MonSounds");
    for (std::size_t row_index = 0; row_index < snd.size(); ++row_index) {
        auto id = [&](const char* column) { const auto found = game_data.sound_index.find(std::string(snd.get(row_index, column))); return found == game_data.sound_index.end() ? 0 : found->second; };
        auto number = [&](const char* column) { return num(snd.get(row_index, column)); };
        GameData::MonSound sounds{ { id("Attack1"), id("Attack2") }, { id("Weapon1"), id("Weapon2") }, { number("Att1Del"), number("Att2Del") },
                           { number("Wea1Del"), number("Wea2Del") }, { number("Att1Prb"), number("Att2Prb") }, id("HitSound"), id("DeathSound"),
                           number("HitDelay"), number("DeaDelay") };
        game_data.mon_sounds.emplace(std::string(snd.get(row_index, "Id")), sounds);
    }
    // Missiles.txt, the rows monsters and skills fire.
    const auto missiles_table = txt("Missiles"), skills_table = txt("Skills");
    std::unordered_set<std::string> skill_missiles;
    for (std::size_t row_index = 0; row_index < skills_table.size(); ++row_index) {
        skill_missiles.emplace(skills_table.get(row_index, "srvmissile"));
        for (const char* column : { "srvmissilea", "srvmissileb", "srvmissilec" }) skill_missiles.emplace(skills_table.get(row_index, column));
        if (const std::string missile_name(skills_table.get(row_index, "srvmissilea")); num(skills_table.get(row_index, "srvdofunc")) == 149 && !missile_name.empty())   // necromage1..4
            for (char digit = '2'; digit <= '4'; ++digit) skill_missiles.emplace(missile_name.substr(0, missile_name.size() - 1) + digit);
    }
    // Thrown weapons' rows (weapons.txt missiletype: Missiles.txt Id), for
    // Double Throw.
    std::unordered_map<int, std::string> thrown_ids;
    {
        const auto weapons_table = txt("weapons");
        for (std::size_t row_index = 0; row_index < weapons_table.size(); ++row_index)
            if (const int id = num(weapons_table.get(row_index, "missiletype")); id > 0) thrown_ids.emplace(id, std::string(weapons_table.get(row_index, "code")));
        for (std::size_t row_index = 0; row_index < missiles_table.size(); ++row_index)
            if (const auto found = thrown_ids.find(num(missiles_table.get(row_index, "Id"))); found != thrown_ids.end()) skill_missiles.emplace(missiles_table.get(row_index, "Missile"));
        for (const auto& [id, code] : thrown_ids)
            for (std::size_t row_index = 0; row_index < missiles_table.size(); ++row_index)
                if (num(missiles_table.get(row_index, "Id")) == id) game_data.thrown[code] = std::string(missiles_table.get(row_index, "Missile"));
    }
    // and the rows those spawn (SubMissile1, HitSubMissile1: Fire Wall's
    // flames, Meteor's fire, Blizzard's shards), to a fixed point.
    for (bool more = true; more;) {
        more = false;
        for (std::size_t row_index = 0; row_index < missiles_table.size(); ++row_index)
            if (skill_missiles.contains(std::string(missiles_table.get(row_index, "Missile"))))
                for (const char* column : { "SubMissile1", "HitSubMissile1" })
                    if (const std::string name(missiles_table.get(row_index, column)); !name.empty()) more |= skill_missiles.emplace(name).second;
    }
    for (std::size_t row_index = 0; row_index < missiles_table.size(); ++row_index) {
        auto number = [&](std::string column) { return num(missiles_table.get(row_index, column)); };
        const std::string name(missiles_table.get(row_index, "Missile"));
        bool used = name == "arrow" || name == "denofevillight" || skill_missiles.contains(name)    // the rogue merc's, the Den's light beams, skills',
                 || std::ranges::contains(d2d::rules::kTrapMissile, std::string_view(name))    // chest traps
                 || std::ranges::contains(d2d::rules::kBossMissile, std::string_view(name));   // a unique's mods
        for (const auto& type_info : monsters.types) used = used || type_info.miss_a1 == name || type_info.miss_a2 == name;
        if (!used) continue;
        GameData::MissileInfo missile_info;
        missile_info.name = name;
        missile_info.vel = number("Vel"); missile_info.range = number("Range"); missile_info.src_damage = number("SrcDamage"); missile_info.min = number("MinDamage"); missile_info.max = number("MaxDamage");
        missile_info.anim_speed = std::max(number("AnimSpeed"), 1); missile_info.anim_len = std::max(number("AnimLen"), 1); missile_info.trans = number("Trans"); missile_info.light = number("Light");
        missile_info.skill = missiles_table.get(row_index, "Skill"); missile_info.lev_range = number("LevRange"); missile_info.hit_func = number("pSrvHitFunc"); missile_info.hit_par1 = number("sHitPar1");
        missile_info.to_hit = number("ToHit") == 1; missile_info.collide_kill = number("CollideKill") == 1; missile_info.pierce = number("Pierce") == 1;
        missile_info.srv_do = number("pSrvDoFunc"); missile_info.param1 = number("Param1"); missile_info.param2 = number("Param2"); missile_info.hit_par2 = number("sHitPar2");
        missile_info.next_hit = number("NextHit") == 1; missile_info.next_delay = number("NextDelay");
        missile_info.sub = missiles_table.get(row_index, "SubMissile1"); missile_info.hit_sub = missiles_table.get(row_index, "HitSubMissile1");
        static constexpr std::array<std::string_view, 6> kEl = { "fire", "ltng", "cold", "pois", "mag", "frze" };
        if (const auto element = std::ranges::find(kEl, missiles_table.get(row_index, "EType")); element != kEl.end()) missile_info.etype = *element == "frze" ? 2 : int(element - kEl.begin());
        missile_info.emin = number("EMin"); missile_info.emax = number("Emax"); missile_info.hitshift = number("HitShift"); missile_info.elen = number("ELen");
        for (int i = 0; i < 5; ++i) {
            missile_info.emin_lev[std::size_t(i)] = number("MinELev" + std::to_string(i + 1));
            missile_info.emax_lev[std::size_t(i)] = number("MaxELev" + std::to_string(i + 1));
        }
        for (int i = 0; i < 3; ++i) missile_info.elen_lev[std::size_t(i)] = number("ELevLen" + std::to_string(i + 1));
        missile_info.cel_file = missiles_table.get(row_index, "CelFile");
        game_data.missiles.emplace(name, std::move(missile_info));
    }
    // SuperUniques.txt: name (string key), Class, minions.
    std::vector<std::size_t> ms_bin;                    // game.exe's MonStats rows: without the Expansion row
    for (std::size_t row_index = 0; row_index < monstats.size(); ++row_index) if (monstats.get(row_index, "Id") != "Expansion") ms_bin.push_back(row_index);
    if (const auto superuniques_table = txt("SuperUniques"); superuniques_table.size() > 0)
        for (std::size_t row_index = 0; row_index < superuniques_table.size(); ++row_index) {
            if (superuniques_table.get(row_index, "Superunique") == "Expansion") continue;
            const std::string key(superuniques_table.get(row_index, "Name"));
            const auto found = lookup_string(game_data, key);
            std::vector<int> mods;
            for (const char* column : { "Mod1", "Mod2", "Mod3" }) if (const int mod = num(superuniques_table.get(row_index, column)); mod > 0) mods.push_back(mod);
            game_data.superuniques.push_back({ found ? u16_to_latin1(*found) : key, row(superuniques_table.get(row_index, "Class")), num(superuniques_table.get(row_index, "MinGrp")),
                                           num(superuniques_table.get(row_index, "MaxGrp")), mods,
                                           { std::string(superuniques_table.get(row_index, "TC")), std::string(superuniques_table.get(row_index, "TC(N)")), std::string(superuniques_table.get(row_index, "TC(H)")) },
                                           { num(superuniques_table.get(row_index, "Utrans")), num(superuniques_table.get(row_index, "Utrans(N)")), num(superuniques_table.get(row_index, "Utrans(H)")) },
                                           num(superuniques_table.get(row_index, "AutoPos")) != 0, num(superuniques_table.get(row_index, "Stacks")) != 0 });
        }

    // Random unique names: UniquePrefix / Suffix / Appellation (Name: a
    // string key) and the two formats the client builds them with.
    for (auto [file, index] : { std::pair{ "UniquePrefix", 0 }, { "UniqueSuffix", 1 }, { "UniqueAppellation", 2 } }) {
        const auto table = txt(file);
        for (std::size_t row_index = 0; row_index < table.size(); ++row_index) {
            const std::string key(table.get(row_index, "Name"));
            if (key.empty() || key == "Expansion") continue;
            const auto found = lookup_string(game_data, key);
            game_data.unique_names[std::size_t(index)].push_back(found ? u16_to_latin1(*found) : key);
        }
    }
    for (int i = 0; i < 2; ++i)
        if (const auto found = lookup_string(game_data, std::uint16_t(0x6b9 + i))) game_data.unique_formats[std::size_t(i)] = u16_to_latin1(*found);

    // MonUMod.txt: champion / unique mods and the constants column.
    if (const auto umod_table = txt("MonUMod"); umod_table.size() > 0)
        for (std::size_t row_index = 0; row_index < umod_table.size(); ++row_index) {
            if (umod_table.get(row_index, "uniquemod") == "Expansion") continue;
            auto number = [&](const char* column) { return num(umod_table.get(row_index, column)); };
            const int id = number("id");
            if (id >= 0 && id < 34) game_data.umods.constants[std::size_t(id)] = number("constants");
            game_data.umods.rows.push_back({ id, number("enabled") == 1, number("champion") == 1, number("fPick"), std::string(umod_table.get(row_index, "exclude1")),
                                         std::string(umod_table.get(row_index, "exclude2")), { number("cpick"), number("cpick (N)"), number("cpick (H)") },
                                         { number("upick"), number("upick (N)"), number("upick (H)") } });
        }

    // Every level's Levels.txt monster columns, by Id: their regions roll
    // one after another (rules::monster_region).
    for (std::size_t row_index = 0; row_index < levels_table.size(); ++row_index) {
        auto text = [&](const std::string& column) { return levels_table.get(row_index, column); };
        const int id = num(text("Id"));
        if (id <= 0 || id > 1000) continue;
        if (std::size_t(id) >= game_data.level_mon.size()) game_data.level_mon.resize(std::size_t(id) + 1);
        auto& level_mon = game_data.level_mon[std::size_t(id)];
        level_mon.density = { num(text("MonDen")), num(text("MonDen(N)")), num(text("MonDen(H)")) };
        level_mon.umin = { num(text("MonUMin")), num(text("MonUMin(N)")), num(text("MonUMin(H)")) };
        level_mon.umax = { num(text("MonUMax")), num(text("MonUMax(N)")), num(text("MonUMax(H)")) };
        level_mon.wander = text("MonWndr") == "1";
        level_mon.ranged_first = text("rangedspawn") == "1";
        level_mon.num_mon = num(text("NumMon"));
        for (int i = 1; i <= 25; ++i) {
            if (const int monstats_row = row(text("mon" + std::to_string(i))); monstats_row >= 0) level_mon.mon.push_back(monstats_row);
            if (const int monstats_row = row(text("nmon" + std::to_string(i))); monstats_row >= 0) level_mon.nmon.push_back(monstats_row);
        }
        for (int i = 0; i < 8; ++i) {
            level_mon.obj_group[std::size_t(i)] = std::uint8_t(num(text("ObjGrp" + std::to_string(i))));
            level_mon.obj_prob[std::size_t(i)]  = std::uint8_t(num(text("ObjPrb" + std::to_string(i))));
        }
    }

    game_data.mon_bin = ms_bin;
    game_data.mon_is_npc.resize(monstats.size());
    for (std::size_t row_index = 0; row_index < monstats.size(); ++row_index) game_data.mon_is_npc[row_index] = monstats.get(row_index, "npc") == "1";
    if (!game_data.superuniques.empty()) d2d::log::info("  not implemented: unique mods 10, 20, 23, 24, 31-35, 40-42 (none in Act 1: no upick, no Act 1 SuperUniques Mod); Charged Bolt's wander");
}

void load_skills(GameData& game_data, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* name) {
        auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt");
        return bytes ? d2d::txt::Table(*bytes) : d2d::txt::Table{};
    };
    const auto skill_calc = txt("skillcalc"), isc = txt("ItemStatCost"), skills_table = txt("Skills"), skill_desc = txt("SkillDesc");
    if (skills_table.size() == 0) return;
    auto& skill_tables = game_data.skills;
    auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    for (std::size_t row = 0; row < skill_calc.size(); ++row) skill_tables.names.operands.emplace_back(skill_calc.get(row, "code"));
    for (std::size_t row = 0; row < isc.size(); ++row) skill_tables.names.stats.emplace(std::string(isc.get(row, "Stat")), num(isc.get(row, "ID")));
    for (std::size_t row = 0; row < skills_table.size(); ++row) {
        const int id = num(skills_table.get(row, "Id"));
        if (id < 0 || skills_table.get(row, "Id").empty()) continue;
        skill_tables.names.skills.emplace(std::string(skills_table.get(row, "skill")), id);
        skill_tables.by_name.emplace(std::string(skills_table.get(row, "skill")), id);
    }
    std::unordered_map<std::string, std::size_t> desc_row;
    for (std::size_t row = 0; row < skill_desc.size(); ++row) desc_row.emplace(std::string(skill_desc.get(row, "skilldesc")), row);
    int calcs = 0;
    std::vector<std::string> bad;
    for (std::size_t row = 0; row < skills_table.size(); ++row) {
        const int id = num(skills_table.get(row, "Id"));
        if (id < 0 || skills_table.get(row, "Id").empty()) continue;
        if (std::size_t(id) >= skill_tables.rows.size()) skill_tables.rows.resize(std::size_t(id) + 1);
        auto& skill_row = skill_tables.rows[std::size_t(id)];
        auto text = [&](const std::string& column) { return skills_table.get(row, column); };
        auto number = [&](const std::string& column) { return num(text(column)); };
        auto calc = [&](const std::string& column) {
            d2d::rules::Calc out;
            if (const auto expression = text(column); !expression.empty()) {
                std::string err;
                out = d2d::rules::compile_calc(expression, skill_tables.names, &err);
                ++calcs;
                if (!err.empty()) bad.push_back(std::string(text("skill")) + " " + column + ": " + err);
            }
            return out;
        };
        skill_row.id = id;
        skill_row.name = text("skill"); skill_row.cls = text("charclass"); skill_row.desc = text("skilldesc");
        skill_row.srvstfunc = number("srvstfunc"); skill_row.srvdofunc = number("srvdofunc");
        skill_row.anim = text("anim"); skill_row.range = text("range");
        skill_row.leftskill = text("leftskill") == "1"; skill_row.passive = text("passive") == "1"; skill_row.aura = text("aura") == "1";
        skill_row.use_attack_rate = text("UseAttackRate") == "1"; skill_row.in_town = text("InTown") == "1"; skill_row.attack_no_mana = text("AttackNoMana") == "1";
        skill_row.reqlevel = std::max(number("reqlevel"), 1); if (number("maxlvl") > 0) skill_row.maxlvl = number("maxlvl");
        skill_row.mana = number("mana"); skill_row.lvlmana = number("lvlmana"); skill_row.manashift = number("manashift"); skill_row.minmana = number("minmana");
        skill_row.tohit = number("ToHit"); skill_row.levtohit = number("LevToHit"); skill_row.tohit_calc = calc("ToHitCalc");
        for (int i = 0; i < 4; ++i) skill_row.calc[std::size_t(i)] = calc("calc" + std::to_string(i + 1));
        for (int i = 0; i < 8; ++i) skill_row.par[std::size_t(i)] = number("Param" + std::to_string(i + 1));
        skill_row.hitshift = number("HitShift"); skill_row.srcdam = text("SrcDam").empty() ? 128 : number("SrcDam"); skill_row.srcdam_raw = number("SrcDam");
        skill_row.srvmissile = text("srvmissile"); skill_row.srvmissilea = text("srvmissilea"); skill_row.perdelay = number("perdelay");
        skill_row.summon = text("summon"); skill_row.pettype = text("pettype"); skill_row.petmax = calc("petmax"); skill_row.target_corpse = text("TargetCorpse") == "1";
        for (int i = 0; i < 5; ++i) {
            skill_row.sumskill[std::size_t(i)] = text("sumskill" + std::to_string(i + 1));
            skill_row.sumsk_calc[std::size_t(i)] = calc("sumsk" + std::to_string(i + 1) + "calc");
        }
        skill_row.result_flags = number("ResultFlags");
        static constexpr std::array<std::string_view, 6> kEl = { "fire", "ltng", "cold", "pois", "mag", "stun" };
        const auto element = std::ranges::find(kEl, text("EType"));
        skill_row.etype = element == kEl.end() ? -1 : int(element - kEl.begin());
        skill_row.emin = number("EMin"); skill_row.emax = number("EMax"); skill_row.elen = number("ELen");
        skill_row.mindam = number("MinDam"); skill_row.maxdam = number("MaxDam");
        for (int i = 0; i < 5; ++i) {
            const auto suffix = std::to_string(i + 1);
            skill_row.emin_lev[std::size_t(i)] = number("EMinLev" + suffix); skill_row.emax_lev[std::size_t(i)] = number("EMaxLev" + suffix);
            skill_row.mindam_lev[std::size_t(i)] = number("MinLevDam" + suffix); skill_row.maxdam_lev[std::size_t(i)] = number("MaxLevDam" + suffix);
            if (const auto found = skill_tables.names.stats.find(std::string(text("passivestat" + suffix))); found != skill_tables.names.stats.end())
                skill_row.passive_stat[std::size_t(i)] = found->second;
            skill_row.passive_calc[std::size_t(i)] = calc("passivecalc" + suffix);
        }
        skill_row.passive_itype = std::string(text("passiveitype"));
        for (int i = 0; i < 3; ++i) skill_row.elen_lev[std::size_t(i)] = number("ELevLen" + std::to_string(i + 1));
        for (int i = 0; i < 6; ++i) {
            skill_row.aura_calc[std::size_t(i)] = calc("aurastatcalc" + std::to_string(i + 1));
            if (const auto found = skill_tables.names.stats.find(std::string(text("aurastat" + std::to_string(i + 1)))); found != skill_tables.names.stats.end())
                skill_row.aurastat[std::size_t(i)] = found->second;
        }
        skill_row.auralen = calc("auralencalc"); skill_row.aurarange = calc("aurarangecalc");
        skill_row.aurastate = text("aurastate"); skill_row.auratarget = text("auratargetstate"); skill_row.prgdam = number("prgdam"); skill_row.seqnum = number("seqnum");
        for (int i = 0; i < 3; ++i) {
            skill_row.prgfunc[std::size_t(i)] = number("srvprgfunc" + std::to_string(i + 1));
            skill_row.prgcalc[std::size_t(i)] = calc("prgcalc" + std::to_string(i + 1));
        }
        skill_row.prgstack = text("prgstack") == "1";
        skill_row.srvmissileb = text("srvmissileb"); skill_row.srvmissilec = text("srvmissilec");
        skill_row.edmg_sym = calc("EDmgSymPerCalc"); skill_row.elen_sym = calc("ELenSymPerCalc"); skill_row.dmg_sym = calc("DmgSymPerCalc");
        for (std::size_t class_index = 0; class_index < 7; ++class_index)
            if (skill_row.cls == d2d::rules::kClassCode[class_index]) skill_tables.class_ids[class_index].push_back(id);
        if (const auto found = desc_row.find(skill_row.desc); found != desc_row.end()) {
            skill_row.page = num(skill_desc.get(found->second, "SkillPage"));
            skill_row.list_row = num(skill_desc.get(found->second, "ListRow"));
            skill_row.list_pos = num(skill_desc.get(found->second, "ListPool"));
            skill_row.icon = num(skill_desc.get(found->second, "IconCel"));
            skill_row.str_name = skill_desc.get(found->second, "str name");
        }
    }
    d2d::log::info("  Skills: {} rows, {} calcs, {} unreadable", skill_tables.rows.size(), calcs, bad.size());
    for (const auto& failed : bad) d2d::log::info("  not implemented: calc {}", failed);
}

// Act 1 town NPCs from the DS1's type-1 objects: id -> MonPreset.txt
// (Act 1 rows) Place -> MonStats row (by Id) -> its MonStatsEx's MonStats2
// row (monster_npc).
// Positions are in subtiles; a unit stands at its subtile's centre.
// ponytail: act 1 only, NU idle only; "place_*" spawn markers skipped.
void load_npcs(GameData& game_data, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* name) {
        auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt");
        return bytes ? d2d::txt::Table(*bytes) : d2d::txt::Table{};
    };
    const auto preset = txt("MonPreset"), monstats = txt("MonStats"), ms2 = txt("MonStats2");
    if (preset.size() == 0 || ms2.size() == 0) return;
    std::vector<std::size_t> act1;
    for (std::size_t row = 0; row < preset.size(); ++row)
        if (preset.get(row, "Act") == "1") act1.push_back(row);
    const auto ms2_row = id_rows(ms2), ms_row = id_rows(monstats);
    auto monster = [&](std::size_t row) { return monster_npc(game_data, monstats, ms2, ms2_row, row); };
    for (const auto& object : game_data.town.ds1.objects()) {
        if (object.type != 1 || object.id < 0 || std::size_t(object.id) >= act1.size()) continue;
        const std::string place(preset.get(act1[std::size_t(object.id)], "Place"));
        const auto found = ms_row.find(place);
        if (found == ms_row.end()) continue;            // place_* markers etc.
        auto npc = monster(found->second);
        if (npc.code.empty()) continue;
        npc.x = (float(object.x) + 0.5f) / 5;
        npc.y = (float(object.y) + 0.5f) / 5;
        for (const auto& point : object.path)
            npc.path.emplace_back((float(point.x) + 0.5f) / 5, (float(point.y) + 0.5f) / 5);
        game_data.town.npcs.push_back(std::move(npc));
    }

    // Mercenaries: hireling.txt (LoD rows, Version 100) by Id -> the MonStats
    // row whose hcIdx is its Class; name keys run from NameFirst.
    if (const auto hire = txt("hireling"); hire.size() > 0) {
        std::unordered_map<int, std::size_t> by_hc;
        for (std::size_t row_index = 0; row_index < monstats.size(); ++row_index) by_hc.emplace(std::atoi(std::string(monstats.get(row_index, "hcIdx")).c_str()), row_index);
        for (std::size_t row_index = 0; row_index < hire.size(); ++row_index) {
            if (hire.get(row_index, "Version") != "100") continue;
            const int id = std::atoi(std::string(hire.get(row_index, "Id")).c_str());
            const auto found = by_hc.find(std::atoi(std::string(hire.get(row_index, "Class")).c_str()));
            if (found == by_hc.end() || game_data.mercs.contains(id)) continue;
            auto npc = monster(found->second);
            if (npc.code.empty()) continue;
            game_data.mercs.emplace(id, GameData::Merc{ std::move(npc), std::string(hire.get(row_index, "NameFirst")) });
        }
    }

    // Type-2 objects: id -> objects.txt Id through game.exe's own preset
    // table (obj_preset.hpp), then that row's Token and layer flags. Start
    // mode: ON for things with a light in ON (torches, fires, the camp
    // waypoint), else NU. ponytail: D2 sets it per object in its InitFn.
    const auto objects = txt("objects");
    // Shrines.txt, and each level's area level (chests' treasure class).
    const auto shr = txt("Shrines"), lvs = txt("Levels");
    auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    for (std::size_t row_index = 0; row_index < shr.size(); ++row_index) {
        auto number = [&](const char* column) { return num(shr.get(row_index, column)); };
        game_data.shrines.push_back({ number("Code"), number("Arg0"), number("Arg1"), number("Duration in frames"), number("reset time in minutes"), number("effectclass"), number("LevelMin") });
    }
    for (std::size_t row_index = 0; row_index < lvs.size(); ++row_index) {
        const int id = num(lvs.get(row_index, "Id"));
        if (id < 0 || id > 1000) continue;
        if (std::size_t(id) >= game_data.area_level.size()) game_data.area_level.resize(std::size_t(id) + 1);
        game_data.area_level[std::size_t(id)] = { num(lvs.get(row_index, "MonLvl1Ex")), num(lvs.get(row_index, "MonLvl2Ex")), num(lvs.get(row_index, "MonLvl3Ex")), num(lvs.get(row_index, "MonLvl1")) };
    }
    std::unordered_map<std::string, std::size_t> obj_row;
    for (std::size_t row_index = 0; row_index < objects.size(); ++row_index) obj_row.emplace(std::string(objects.get(row_index, "Id")), row_index);
    auto rgn = object_seed(game_data.map_seed);
    auto add = [&](Level& into, int oid, int spot_x, int spot_y) { add_object(game_data, objects, obj_row, into, oid, spot_x, spot_y, rgn); };
    {                                               // a chest trap's fires (5 / 7: objects 162 and 160)
        Level tmp;
        add(tmp, 162, 0, 0);
        add(tmp, 160, 0, 0);
        for (std::size_t k = 0; k < tmp.npcs.size() && k < 2; ++k) game_data.trap_fires[k] = std::move(tmp.npcs[k]);
        Level portal_level;
        add(portal_level, 59, 0, 0);                          // the town portal
        if (!portal_level.npcs.empty()) game_data.town_portal = std::move(portal_level.npcs[0]);
    }
    for (const auto& object : game_data.town.ds1.objects())
        if (object.type == 2 && object.id >= 0 && object.id < 150) add(game_data.town, kObjPreset[0][std::size_t(object.id)], object.x, object.y);   // act 1

    // What a level build reads later (build_level).
    if (game_data.builder) {
        game_data.builder->objects = objects;
        game_data.builder->obj_row = obj_row;
        game_data.builder->levels = lvs;
        game_data.builder->sound_env = txt("SoundEnviron");
    }

    stamp_footprints(game_data.town);

    // Deckard Cain (cain5, hcIdx 265 = 0x109, whose menu has "identify
    // items"), in camp once Act 1 quest 4 is done. On the rescue itself
    // a1q4.cpp (FUN_00596de0 -> FUN_00592960) spawns him where the player
    // came back through the portal.
    // ponytail: where a game with the quest already done places him isn't
    // located; he stands 3 subtiles off the town start, like the
    // Tristram spawn's offset (FUN_00593290).
    for (std::size_t row_index = 0; row_index < monstats.size() && game_data.town.start.first >= 0; ++row_index) {
        if (monstats.get(row_index, "hcIdx") != "265") continue;
        auto npc = monster(row_index);
        if (npc.code.empty()) break;
        npc.quest = 4;
        std::tie(npc.x, npc.y) = game_data.town.nearest_free(game_data.town.start.first + 0.6f, game_data.town.start.second + 0.6f);
        game_data.town.npcs.push_back(std::move(npc));
        break;
    }
}

// Excel tables + the derived composite data: the component table and each
// class's starting-gear appearance (CharStats.txt item1..: "rarm" item in
// the right hand, a "larm" shield on the shield layer; body parts "lit").
void load_tables(GameData& game_data, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* name) {
        auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt");
        return bytes ? d2d::txt::Table(*bytes) : d2d::txt::Table{};
    };
    const auto types = txt("ItemTypes"), weapons = txt("weapons"), armor = txt("armor"),
               misc = txt("misc"), charstats = txt("CharStats");
    if (types.size() == 0 || weapons.size() == 0) return;
    game_data.comp = d2d::compcode::build(types, weapons, armor, misc);
    game_data.item_pieces = d2d::compcode::pieces(weapons, armor, misc);
    auto col = [&](const char* name, const char* column, bool all) {
        std::vector<std::string> values;
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + name + ".txt")) {
            const d2d::txt::Table table(*bytes, all);
            for (std::size_t row = 0; row < table.size(); ++row) values.emplace_back(table.get(row, column));
        }
        return values;
    };
    game_data.item_colours = { col("Colors", "Code", false), col("UniqueItems", "chrtransform", false), col("SetItems", "chrtransform", false),
                           col("MagicPrefix", "transformcolor", true), col("MagicSuffix", "transformcolor", true), col("AutoMagic", "transformcolor", true),
                           d2d::compcode::gem_colours(types, misc, txt("gems")), col("UniqueItems", "invtransform", false),
                           col("SetItems", "invtransform", false) };
    int transform = 1;
    for (const char* name : { "grey", "grey2", "gold", "brown", "greybrown", "invgrey", "invgrey2", "invgreybrown" })
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\items\palette\)") + name + ".dat"); bytes && bytes->size() >= 21 * 256) {
            const auto* colormap = reinterpret_cast<const std::uint8_t*>(bytes->data());
            game_data.colormaps[std::size_t(transform++)].assign(colormap, colormap + 21 * 256);
        } else ++transform;
    game_data.item_types.emplace(types);
    if (const auto overlay_table = txt("Overlay"); overlay_table.size() > 0)
        for (std::size_t row = 0; row < overlay_table.size(); ++row) {
            auto number = [&](const char* column) { return std::atoi(std::string(overlay_table.get(row, column)).c_str()); };
            GameData::OverlayInfo overlay;
            overlay.file = std::string(overlay_table.get(row, "Filename"));
            overlay.frames = std::max(number("Frames"), 1); overlay.x = number("Xoffset"); overlay.y = number("Yoffset"); overlay.rate = number("AnimRate");
            overlay.trans = number("Trans"); overlay.radius = number("Radius"); overlay.init_radius = number("InitRadius"); overlay.predraw = number("PreDraw") != 0;
            overlay.height = { number("Height1"), number("Height2"), number("Height3"), number("Height4") };
            game_data.overlays.emplace(std::string(overlay_table.get(row, "overlay")), std::move(overlay));
        }
    auto overlay = [&](std::string_view name) -> const GameData::OverlayInfo* {
        const auto found = game_data.overlays.find(std::string(name));
        return found == game_data.overlays.end() || found->second.file.empty() || found->second.file == "null" ? nullptr : &found->second;
    };
    if (const auto states_table = txt("States"); states_table.size() > 0)
        for (std::size_t row = 0; row < states_table.size(); ++row) {
            GameData::StateInfo state;
            const auto shift = states_table.get(row, "colorshift");
            state.shift = shift.empty() ? -1 : std::atoi(std::string(shift).c_str());
            state.pri = std::atoi(std::string(states_table.get(row, "colorpri")).c_str());
            for (int k = 0; k < 4; ++k) state.over[std::size_t(k)] = overlay(states_table.get(row, ("overlay" + std::to_string(k + 1)).c_str()));
            state.cast = overlay(states_table.get(row, "castoverlay"));
            state.item_type = std::string(states_table.get(row, "itemtype"));
            const std::string trans(states_table.get(row, "itemtrans"));
            for (std::size_t colour = 0; colour < game_data.item_colours.codes.size(); ++colour) if (!trans.empty() && game_data.item_colours.codes[colour] == trans) state.item_colour = int(colour);
            game_data.states.emplace(std::string(states_table.get(row, "state")), std::move(state));
        }
    if (auto bytes = mpqs.try_read(R"(data\global\palette\ACT1\Pal.pl2)"); bytes && bytes->size() >= 0x53500 + 111 * 256) {
        const auto* shifts = reinterpret_cast<const std::uint8_t*>(bytes->data()) + 0x53500;
        game_data.colour_shifts.assign(shifts, shifts + 111 * 256);
    }

    // Char panel: next-level experience (row "<level>", same for every
    // class) and the expansion's resistance penalty per difficulty.
    if (const auto experience_table = txt("experience"); experience_table.size() > 0)
        for (std::size_t row = 0; row < experience_table.size(); ++row)
            if (const auto level_text = experience_table.get(row, "Level"); !level_text.empty() && level_text[0] >= '0' && level_text[0] <= '9') {
                const auto level = std::size_t(std::stoi(std::string(level_text)));
                if (level >= game_data.exp_next.size()) game_data.exp_next.resize(level + 1, -1);
                game_data.exp_next[level] = std::stoll(std::string(experience_table.get(row, "Amazon")));
            }
    if (const auto difficulty_table = txt("DifficultyLevels"); difficulty_table.size() >= 3)
        for (std::size_t row = 0; row < 3; ++row)
            game_data.resist_penalty[row] = std::stoll(std::string(difficulty_table.get(row, "ResistPenalty")));

    // Items. ItemStatCost.txt only exists in the 1.14d patch data.
    if (const auto isc = txt("ItemStatCost"); isc.size() > 0)
        game_data.item_tables = d2d::d2s::ItemTables::from(isc, armor, weapons, misc);
    for (const auto* table : { &weapons, &armor, &misc })
        for (std::size_t row = 0; row < table->size(); ++row)
            game_data.rules.item_info[std::string(table->get(row, "code"))] = {
                std::string(table->get(row, "invfile")),
                std::max(1, std::atoi(std::string(table->get(row, "invwidth")).c_str())),
                std::max(1, std::atoi(std::string(table->get(row, "invheight")).c_str())),
                std::string(table->get(row, "namestr")), std::string(table->get(row, "type")),
                table == &armor ? 1 : table == &weapons ? 2 : 0,
                table == &armor && !table->get(row, "belt").empty() ? std::atoi(std::string(table->get(row, "belt")).c_str()) : -1,
                table == &weapons && table->get(row, "2handed") == "1", table == &weapons && table->get(row, "1or2handed") == "1",
                std::atoi(std::string(table->get(row, "reqstr")).c_str()), std::atoi(std::string(table->get(row, "reqdex")).c_str()),
                std::atoi(std::string(table->get(row, "levelreq")).c_str()), std::string(table->get(row, "flippyfile")),
                std::string(table->get(row, "dropsound")), std::atoi(std::string(table->get(row, "dropsfxframe")).c_str()) };
    std::vector<std::string> type_order;
    for (std::size_t row = 0; row < types.size(); ++row) {
        const std::string code(types.get(row, "Code"));
        type_order.push_back(code);
        game_data.rules.types[code] = {
            { std::string(types.get(row, "Equiv1")), std::string(types.get(row, "Equiv2")) },
            { d2d::rules::body_slot(types.get(row, "BodyLoc1")), d2d::rules::body_slot(types.get(row, "BodyLoc2")) },
            std::string(types.get(row, "Class")), types.get(row, "Beltable") == "1",
            types.get(row, "Magic") == "1", types.get(row, "Rare") == "1", types.get(row, "Normal") == "1",
            types.get(row, "TreasureClass") == "1", std::atoi(std::string(types.get(row, "Rarity")).c_str()) };
    }
    {
        static constexpr const char* kVendorCol[17] = { "Akara", "Gheed", "Charsi", "Fara", "Lysander", "Drognan",
            "Hralti", "Alkor", "Ormus", "Elzix", "Asheara", "Cain", "Halbu", "Jamella", "Malah", "Larzuk", "Drehya" };
        std::vector<d2d::rules::AutoBase> auto_bases;   // the auto classes' candidates, items-table order
        for (const auto* table : { &weapons, &armor, &misc })
            for (std::size_t row = 0; row < table->size(); ++row)
                if (table->get(row, "spawnable") == "1" && std::atoi(std::string(table->get(row, "quest")).c_str()) == 0)
                    auto_bases.push_back({ std::string(table->get(row, "code")), std::string(table->get(row, "type")), std::string(table->get(row, "type2")),
                                           std::atoi(std::string(table->get(row, "level")).c_str()) });
        for (const auto* table : { &armor, &weapons, &misc })
            for (std::size_t row = 0; row < table->size(); ++row) {
                const std::string code(table->get(row, "code"));
                auto number = [&](std::string column) { return std::atoi(std::string(table->get(row, column)).c_str()); };
                const bool two = table == &weapons && table->get(row, "2handed") == "1" && table->get(row, "1or2handed") != "1";
                game_data.rules.item_base[code] = { table == &armor ? number("minac") : 0, table == &armor ? number("maxac") : 0, number("cost"),
                                          // armor.txt's first mindam/maxdam: a shield's smite, boots' kick damage
                                          table == &misc ? 0 : number(two ? "2handmindam" : "mindam"),
                                          table == &misc ? 0 : number(two ? "2handmaxdam" : "maxdam"),
                                          table == &misc ? 0 : number("StrBonus"), table == &misc ? 0 : number("DexBonus"),
                                          table == &weapons ? number("speed") : 0, table == &armor ? number("block") : 0,
                                          table->get(row, "stackable") == "1", number("level"),
                                          table == &misc ? 0 : number("durability"), number("gamble cost"), number("minstack"), number("maxstack"),
                                          std::string(table->get(row, "normcode")), std::string(table->get(row, "ubercode")),
                                          std::string(table->get(row, "ultracode")), std::string(table->get(row, "BetterGem")),
                                          number("bitfield1"), number("quest") > 0, number("unique") > 0, number("spawnstack") };
                if (table->get(row, "spawnable") != "1") continue;
                for (std::size_t vendor = 0; vendor < 17; ++vendor) {
                    const std::string vendor_name = kVendorCol[vendor];
                    d2d::rules::VendorItem vendor_item{ code, number(vendor_name + "Min"), number(vendor_name + "Max"), number(vendor_name + "MagicMin"), number(vendor_name + "MagicMax"),
                                          number(vendor_name + "MagicLvl"), table->get(row, "PermStoreItem") == "1" };
                    if (vendor_item.max > 0 || vendor_item.magic_max > 0) game_data.rules.vendor_items[vendor].push_back(std::move(vendor_item));
                }
            }
        // Potions (misc.txt stat1/calc1, stat2/calc2, len).
        for (std::size_t row = 0; row < misc.size(); ++row) {
            auto number = [&](std::string column) { return std::atoi(std::string(misc.get(row, column)).c_str()); };
            const auto stat1 = misc.get(row, "stat1"), stat2 = misc.get(row, "stat2");
            d2d::rules::Tables::Potion potion{ 0, 0, number("len") };
            if (stat1 == "hpregen") potion.life = number("calc1");
            else if (stat1 == "manarecovery") potion.mana = number("calc1");
            else if (stat1 == "hitpoints" && stat2 == "mana") { potion.life = number("calc1"); potion.mana = number("calc2"); potion.percent = true; }
            else continue;
            game_data.rules.potions.emplace(std::string(misc.get(row, "code")), potion);
        }
        // Drops: TreasureClassEx (FUN_006547d0; entries under Prob 1 dropped),
        // ItemRatio (FUN_00637910: the highest Version per class / uber), auto classes.
        const auto tcx = txt("TreasureClassEx");
        std::string last_name;
        int last_group = 0;
        for (std::size_t row = 0; row < tcx.size(); ++row) {
            auto number = [&](std::string column) { return std::atoi(std::string(tcx.get(row, column)).c_str()); };
            d2d::rules::TreasureClass treasure_class{ number("Picks") ? number("Picks") : 1, number("NoDrop"), { number("Unique"), number("Set"), number("Rare"), number("Magic") }, {},
                                                      number("group"), number("level"), {} };
            for (int i = 1; i <= 10; ++i) {
                auto item = std::string(tcx.get(row, "Item" + std::to_string(i)));
                std::erase(item, '"');
                if (!item.empty() && number("Prob" + std::to_string(i)) >= 1) treasure_class.items.emplace_back(std::move(item), number("Prob" + std::to_string(i)));
            }
            std::string name(tcx.get(row, "Treasure Class"));
            if (last_group != 0 && treasure_class.group == last_group) game_data.rules.treasure[last_name].next = name;
            last_name = name;
            last_group = treasure_class.group;
            game_data.rules.treasure.emplace(std::move(name), std::move(treasure_class));
        }
        const auto ratio = txt("ItemRatio");
        std::array<int, 4> ratio_version{ -1, -1, -1, -1 };
        for (std::size_t row = 0; row < ratio.size(); ++row) {
            auto number = [&](std::string column) { return std::atoi(std::string(ratio.get(row, column)).c_str()); };
            const auto which = std::size_t((number("Class Specific") ? 2 : 0) + (number("Uber") ? 1 : 0));
            if (number("Version") > 100 || number("Version") < ratio_version[which]) continue;
            ratio_version[which] = number("Version");
            auto& ratios = game_data.rules.quality_ratio[which];
            ratios[0] = { number("Unique"), number("UniqueDivisor"), number("UniqueMin") };
            ratios[1] = { number("Set"), number("SetDivisor"), number("SetMin") };
            ratios[2] = { number("Rare"), number("RareDivisor"), number("RareMin") };
            ratios[3] = { number("Magic"), number("MagicDivisor"), number("MagicMin") };
            ratios[4] = { number("HiQuality"), number("HiQualityDivisor"), 0 };
            ratios[5] = { number("Normal"), number("NormalDivisor"), 0 };
        }
        d2d::rules::add_auto_treasure(game_data.rules, type_order, auto_bases);
    }
    auto keys = [&](const char* file_name, const char* column, bool all) {
        std::vector<std::string> values;
        if (auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + file_name + ".txt")) {
            const d2d::txt::Table table(*bytes, all);
            for (std::size_t row = 0; row < table.size(); ++row) values.emplace_back(table.get(row, column));
        }
        return values;
    };
    if (auto bytes = mpqs.try_read(R"(data\global\excel\ItemStatCost.txt)")) {
        const d2d::txt::Table table(*bytes);
        auto num = [&](std::size_t row_index, const char* column) { return std::atoi(std::string(table.get(row_index, column)).c_str()); };
        for (std::size_t row = 0; row < table.size(); ++row) {
            const auto id = std::size_t(num(row, "ID"));
            if (id >= game_data.stat_desc.size()) game_data.stat_desc.resize(id + 1);
            game_data.stat_desc[id] = { num(row, "descpriority"), num(row, "descfunc"), num(row, "descval"),
                                    num(row, "op"), num(row, "op param"), num(row, "dgrp"), num(row, "dgrpfunc"),
                                    num(row, "dgrpval"),
                                    std::string(table.get(row, "descstrpos")), std::string(table.get(row, "descstrneg")),
                                    std::string(table.get(row, "descstr2")), std::string(table.get(row, "dgrpstrpos")),
                                    std::string(table.get(row, "dgrpstrneg")), std::string(table.get(row, "dgrpstr2")) };
        }
    }
    {
        // Socket bonuses: gems.txt mods -> Properties.txt funcs -> stats.
        // Property funcs used by gems: 1/3 value, 15/16 min/max, 17 param,
        // 5/6/7 min/max/% damage. ponytail: other funcs dropped.
        std::unordered_map<std::string, int> stat_id;
        if (auto bytes = mpqs.try_read(R"(data\global\excel\ItemStatCost.txt)")) {
            const d2d::txt::Table table(*bytes);
            for (std::size_t row = 0; row < table.size(); ++row)
                stat_id[std::string(table.get(row, "Stat"))] = std::atoi(std::string(table.get(row, "ID")).c_str());
        }
        std::unordered_map<std::string, std::vector<std::pair<int, int>>> prop;   // code -> (func, stat)
        const auto properties_table = txt("Properties");
        for (std::size_t row = 0; row < properties_table.size(); ++row)
            for (int i = 1; i <= 7; ++i) {
                const int func = std::atoi(std::string(properties_table.get(row, "func" + std::to_string(i))).c_str());
                if (!func) continue;
                const auto found = stat_id.find(std::string(properties_table.get(row, "stat" + std::to_string(i))));
                prop[std::string(properties_table.get(row, "code"))].emplace_back(func, found == stat_id.end() ? -1 : found->second);
                game_data.rules.properties[std::string(properties_table.get(row, "code"))].push_back(
                    { func, found == stat_id.end() ? -1 : found->second,
                      std::atoi(std::string(properties_table.get(row, "val" + std::to_string(i))).c_str()) });
            }
        const auto gems_table = txt("gems");
        static constexpr const char* kSlot[3] = { "weaponMod", "helmMod", "shieldMod" };
        for (std::size_t row = 0; row < gems_table.size(); ++row)
            for (int k = 0; k < 3; ++k)
                for (int mod = 1; mod <= 3; ++mod) {
                    const std::string pre = std::string(kSlot[k]) + std::to_string(mod);
                    const auto found = prop.find(std::string(gems_table.get(row, pre + "Code")));
                    if (found == prop.end()) continue;
                    auto num = [&](const char* column) { return std::atoi(std::string(gems_table.get(row, pre + column)).c_str()); };
                    const int par = num("Param"), minimum = num("Min"), maximum = num("Max");
                    auto& out = game_data.gem_props[std::string(gems_table.get(row, "code"))][std::size_t(k)];
                    for (auto [func, stat] : found->second) {
                        switch (func) {
                            case 1: case 3: if (stat >= 0) out.push_back({ stat, par, minimum }); break;
                            case 15: if (stat >= 0) out.push_back({ stat, 0, minimum }); break;
                            case 16: if (stat >= 0) out.push_back({ stat, 0, maximum }); break;
                            case 17: if (stat >= 0) out.push_back({ stat, 0, par }); break;
                            case 5: out.push_back({ 21, 0, minimum }); break;
                            case 6: out.push_back({ 22, 0, maximum }); break;
                            case 7: out.push_back({ 17, 0, minimum }); out.push_back({ 18, 0, minimum }); break;
                            default: break;
                        }
                    }
                }
    }
    {
        std::unordered_map<std::string, std::string> desc_name;     // SkillDesc key -> "str name"
        if (auto bytes = mpqs.try_read(R"(data\global\excel\SkillDesc.txt)")) {
            const d2d::txt::Table table(*bytes);
            for (std::size_t row = 0; row < table.size(); ++row)
                desc_name[std::string(table.get(row, "skilldesc"))] = std::string(table.get(row, "str name"));
        }
        static constexpr std::string_view kCls[] = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
        if (auto bytes = mpqs.try_read(R"(data\global\excel\skills.txt)")) {
            const d2d::txt::Table table(*bytes);
            for (std::size_t row = 0; row < table.size(); ++row) {
                const auto id = std::size_t(std::atoi(std::string(table.get(row, "Id")).c_str()));
                if (id >= game_data.skill_name.size()) { game_data.skill_name.resize(id + 1); game_data.skill_class.resize(id + 1, -1); }
                game_data.skill_name[id] = desc_name[std::string(table.get(row, "skilldesc"))];
                game_data.rules.skill_id[std::string(table.get(row, "skill"))] = int(id);
                const auto class_code = table.get(row, "charclass");
                for (int class_index = 0; class_index < 7; ++class_index) if (class_code == kCls[class_index]) game_data.skill_class[id] = class_index;
            }
        }
        const auto charstats_table = txt("CharStats");
        for (std::size_t row = 0; row < std::min<std::size_t>(charstats_table.size(), 7); ++row) {
            if (const auto walk = std::atoi(std::string(charstats_table.get(row, "WalkVelocity")).c_str()); walk > 0) game_data.walk_velocity[row] = walk;
            if (const auto run = std::atoi(std::string(charstats_table.get(row, "RunVelocity")).c_str()); run > 0) game_data.run_velocity[row] = run;
        }
        for (std::size_t row = 0; row < std::min<std::size_t>(charstats_table.size(), 7); ++row)
            game_data.class_strs[row] = { std::string(charstats_table.get(row, "StrAllSkills")),
                                    { std::string(charstats_table.get(row, "StrSkillTab1")), std::string(charstats_table.get(row, "StrSkillTab2")),
                                      std::string(charstats_table.get(row, "StrSkillTab3")) },
                                    std::string(charstats_table.get(row, "StrClassOnly")) };
    }
    // animdata.d2: blocks of u32 count + count * 160-byte records
    // {char name[8], u32 frames/dir, u32 speed, u8 events[144]}.
    if (auto bytes = mpqs.try_read(R"(data\global\animdata.d2)"))
        for (std::size_t at = 0; at + 4 <= bytes->size();) {
            std::uint32_t count; std::memcpy(&count, bytes->data() + at, 4); at += 4;
            for (std::uint32_t i = 0; i < count && at + 160 <= bytes->size(); ++i, at += 160) {
                std::string name(reinterpret_cast<const char*>(bytes->data() + at), 8);
                name.resize(std::strlen(name.c_str()));
                for (auto& letter : name) letter = char(std::toupper(letter));
                GameData::AnimInfo anim;
                std::memcpy(&anim.frames, bytes->data() + at + 8, 4);
                std::memcpy(&anim.speed, bytes->data() + at + 12, 4);
                for (std::uint32_t frame = 0; frame < anim.frames && frame < 144 && anim.action < 0; ++frame)
                    if (std::to_integer<int>(bytes->data()[at + 16 + frame]) != 0) anim.action = int(frame);
                game_data.anim_data.emplace(std::move(name), anim);
            }
        }
    if (const auto belts_table = txt("belts"); belts_table.size() >= 14)
        for (std::size_t belt_index = 0; belt_index < 7; ++belt_index) {
            const std::size_t row = 7 + belt_index;                 // the 800x600 half
            auto& belt = game_data.belts[belt_index];
            belt.boxes = std::clamp(std::atoi(std::string(belts_table.get(row, "numboxes")).c_str()), 0, 16);
            for (int i = 0; i < belt.boxes; ++i)
                for (int k = 0; k < 4; ++k) {
                    static constexpr const char* kSide[4] = { "left", "right", "top", "bottom" };
                    belt.box[std::size_t(i)][std::size_t(k)] = std::atoi(std::string(
                        belts_table.get(row, "box" + std::to_string(i + 1) + kSide[k])).c_str());
                }
        }
    {
        const auto levels_table = txt("Levels");
        for (std::size_t row = 0; row < levels_table.size(); ++row)
            if (levels_table.get(row, "Id") == "1") game_data.town.type = std::atoi(std::string(levels_table.get(row, "LevelType")).c_str());
        for (std::size_t row = 0; row < levels_table.size(); ++row) {
            const auto waypoint = levels_table.get(row, "Waypoint");
            const int act = std::atoi(std::string(levels_table.get(row, "Act")).c_str());
            if (waypoint.empty() || waypoint == "255" || act < 0 || act > 4) continue;
            game_data.waypoint_levels[std::size_t(act)].push_back({ std::atoi(std::string(waypoint).c_str()),
                std::atoi(std::string(levels_table.get(row, "Id")).c_str()), std::string(levels_table.get(row, "LevelName")) });
        }
        for (auto& act_levels : game_data.waypoint_levels) std::ranges::sort(act_levels, {}, &GameData::WaypointLevel::waypoint);
    }
    // The Rogue Encampment (Levels.txt Id 1): automap Layer,
    // and SoundEnv -> SoundEnviron Song / Day Ambience (Sounds.txt indices).
    {
        const auto levels_table = txt("Levels"), sound_env = txt("SoundEnviron");
        for (std::size_t row = 0; row < levels_table.size(); ++row) {
            const auto id = levels_table.get(row, "Id");
            Level* into = id == "1" ? &game_data.town : nullptr;     // the others: build_level
            if (!into) continue;
            into->name = std::string(levels_table.get(row, "LevelName"));
            into->layer = std::atoi(std::string(levels_table.get(row, "Layer")).c_str());
            into->light = level_light(levels_table.get(row, "Intensity"), levels_table.get(row, "Red"), levels_table.get(row, "Green"), levels_table.get(row, "Blue"));
            into->rain = levels_table.get(row, "Rain") == "1";
            const auto env = levels_table.get(row, "SoundEnv");
            for (std::size_t env_row = 0; env_row < sound_env.size(); ++env_row)
                if (sound_env.get(env_row, "Index") == env) {
                    into->song = std::atoi(std::string(sound_env.get(env_row, "Song")).c_str());
                    into->ambience = std::atoi(std::string(sound_env.get(env_row, "Day Ambience")).c_str());
                    into->night_ambience = std::atoi(std::string(sound_env.get(env_row, "Night Ambience")).c_str());
                    into->day_event = std::atoi(std::string(sound_env.get(env_row, "Day Event")).c_str());
                    into->night_event = std::atoi(std::string(sound_env.get(env_row, "Night Event")).c_str());
                    into->event_delay = std::atoi(std::string(sound_env.get(env_row, "Event Delay")).c_str());
                }
        }
    }
    if (const auto sounds_table = txt("Sounds"); sounds_table.size() > 0)
        for (std::size_t row = 0; row < sounds_table.size(); ++row) {
            const int index = std::atoi(std::string(sounds_table.get(row, "Index")).c_str());
            if (index < 0 || index > 20000) continue;
            if (std::size_t(index) >= game_data.sounds.size()) game_data.sounds.resize(std::size_t(index) + 1);
            game_data.sounds[std::size_t(index)] = { std::string(sounds_table.get(row, "FileName")),
                                             std::atoi(std::string(sounds_table.get(row, "Volume")).c_str()),
                                             sounds_table.get(row, "Loop") == "1", sounds_table.get(row, "Music Vol") == "1",
                                             std::atoi(std::string(sounds_table.get(row, "Fade In")).c_str()),
                                             std::atoi(std::string(sounds_table.get(row, "Fade Out")).c_str()),
                                             std::atoi(std::string(sounds_table.get(row, "Group Size")).c_str()),
                                             { std::atoi(std::string(sounds_table.get(row, "Block 1")).c_str()), std::atoi(std::string(sounds_table.get(row, "Block 2")).c_str()),
                                               std::atoi(std::string(sounds_table.get(row, "Block 3")).c_str()) } };
            game_data.sound_index.emplace(std::string(sounds_table.get(row, "Sound")), index);
        }
    auto& item_names = game_data.item_names;
    item_names.unique   = keys("UniqueItems", "index", false);
    item_names.set      = keys("SetItems", "index", false);
    item_names.prefix   = keys("MagicPrefix", "Name", true);
    {
        auto pairs = [&](const char* file_name, const char* mul, const char* add, bool all) {
            std::vector<std::pair<int, int>> values;
            if (auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + file_name + ".txt")) {
                const d2d::txt::Table table(*bytes, all);
                for (std::size_t row = 0; row < table.size(); ++row)
                    values.emplace_back(std::atoi(std::string(table.get(row, mul)).c_str()), std::atoi(std::string(table.get(row, add)).c_str()));
            }
            return values;
        };
        game_data.rules.prefix_cost = pairs("MagicPrefix", "multiply", "add", true);
        game_data.rules.suffix_cost = pairs("MagicSuffix", "multiply", "add", true);
        game_data.rules.unique_cost = pairs("UniqueItems", "cost mult", "cost add", false);
        game_data.rules.set_cost    = pairs("SetItems", "cost mult", "cost add", false);
        // Item generation (components/rules generate_item / gamble_item).
        auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
        auto affixes = [&](const char* file_name) {
            std::vector<d2d::rules::Affix> values;
            if (auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + file_name + ".txt")) {
                const d2d::txt::Table table(*bytes, true);                 // raw rows = the save's affix IDs
                for (std::size_t row = 0; row < table.size(); ++row) {
                    d2d::rules::Affix affix{ std::string(table.get(row, "Name")), num(table.get(row, "level")), num(table.get(row, "maxlevel")),
                                         num(table.get(row, "group")), num(table.get(row, "frequency")),
                                         table.get(row, "spawnable") == "1", table.get(row, "rare") == "1", {}, {}, {} };
                    for (int i = 1; i <= 7; ++i)
                        if (const auto item_type = table.get(row, "itype" + std::to_string(i)); !item_type.empty()) affix.itypes.emplace_back(item_type);
                    for (int i = 1; i <= 5; ++i)
                        if (const auto excluded_type = table.get(row, "etype" + std::to_string(i)); !excluded_type.empty()) affix.etypes.emplace_back(excluded_type);
                    for (int i = 1; i <= 3; ++i) {
                        const auto mod_prefix = "mod" + std::to_string(i);
                        if (const auto mod_code = table.get(row, mod_prefix + "code"); !mod_code.empty())
                            affix.mods.push_back({ std::string(mod_code), std::string(table.get(row, mod_prefix + "param")),
                                               num(table.get(row, mod_prefix + "min")), num(table.get(row, mod_prefix + "max")) });
                    }
                    values.push_back(std::move(affix));
                }
            }
            return values;
        };
        game_data.rules.prefixes = affixes("MagicPrefix");
        game_data.rules.suffixes = affixes("MagicSuffix");
        const auto set_names = keys("Sets", "index", false);
        auto specials = [&](const char* file_name, const char* code_col, int props) {
            std::vector<d2d::rules::Special> values;
            if (auto bytes = mpqs.try_read(std::string(R"(data\global\excel\)") + file_name + ".txt")) {
                const d2d::txt::Table table(*bytes);
                for (std::size_t row = 0; row < table.size(); ++row) {
                    d2d::rules::Special special{ std::string(table.get(row, code_col)), num(table.get(row, "lvl")), num(table.get(row, "rarity")),
                                            code_col[0] == 'c' ? table.get(row, "enabled") == "1" : true, {} };
                    for (int i = 1; i <= props; ++i) {
                        const auto suffix = std::to_string(i);
                        if (const auto prop_code = table.get(row, "prop" + suffix); !prop_code.empty())
                            special.mods.push_back({ std::string(prop_code), std::string(table.get(row, "par" + suffix)),
                                                num(table.get(row, "min" + suffix)), num(table.get(row, "max" + suffix)) });
                    }
                    special.ladder = table.get(row, "ladder") == "1";
                    special.nolimit = table.get(row, "nolimit") == "1";
                    if (const auto set_row = std::ranges::find(set_names, table.get(row, "set")); set_row != set_names.end() && code_col[0] == 'i')
                        special.set = int(set_row - set_names.begin());
                    values.push_back(std::move(special));
                }
            }
            return values;
        };
        game_data.rules.uniques = specials("UniqueItems", "code", 12);
        game_data.rules.sets = specials("SetItems", "item", 9);
        game_data.rules.rare_prefixes = int(keys("RarePrefix", "name", false).size());
        game_data.rules.rare_suffixes = int(keys("RareSuffix", "name", false).size());
        game_data.rules.gamble = keys("gamble", "code", false);
        if (const auto hireling_table = txt("hireling"); hireling_table.size() > 0)
            for (std::size_t row = 0; row < hireling_table.size(); ++row) {
                auto number = [&](const char* column) { return num(hireling_table.get(row, column)); };
                const std::string first(hireling_table.get(row, "NameFirst")), last(hireling_table.get(row, "NameLast"));
                const int names = first.size() >= 2 && last.size() >= 2
                    ? std::atoi(last.substr(last.size() - 2).c_str()) - std::atoi(first.substr(first.size() - 2).c_str()) + 1 : 1;
                game_data.rules.hirelings.push_back({ number("Version"), number("Id"), number("Class"), number("Act"), number("Difficulty"), number("Level"),
                    number("Gold"), number("Exp/Lvl"), number("HP"), number("HP/Lvl"), number("Defense"), number("Def/Lvl"), number("Str"), number("Str/Lvl"),
                    number("Dex"), number("Dex/Lvl"), number("Dmg-Min"), number("Dmg-Max"), number("Dmg/Lvl"), std::max(1, names),
                    number("AR"), number("AR/Lvl") });
            }
        if (const auto difficulty_table = txt("DifficultyLevels"); difficulty_table.size() >= 3)
            for (std::size_t row = 0; row < 3; ++row)
                game_data.rules.gamble_rates[row] = { num(difficulty_table.get(row, "GambleRare")), num(difficulty_table.get(row, "GambleSet")),
                                                num(difficulty_table.get(row, "GambleUnique")) };
        if (auto bytes = mpqs.try_read(R"(data\global\excel\npc.txt)")) {
            const d2d::txt::Table table(*bytes);
            for (std::size_t row = 0; row < table.size(); ++row) {
                auto number = [&](const char* column) { return std::atoi(std::string(table.get(row, column)).c_str()); };
                d2d::rules::NpcPrice prices{ number("buy mult"), number("sell mult"), number("rep mult"),
                                   { number("questflag A"), number("questflag B"), number("questflag C") },
                                   { number("questbuymult A"), number("questbuymult B"), number("questbuymult C") },
                                   { number("questsellmult A"), number("questsellmult B"), number("questsellmult C") },
                                   { number("questrepmult A"), number("questrepmult B"), number("questrepmult C") },
                                   { number("max buy"), number("max buy (N)"), number("max buy (H)") } };
                game_data.rules.npc_prices[std::string(table.get(row, "npc"))] = prices;
            }
        }
    }
    item_names.suffix   = keys("MagicSuffix", "Name", true);
    item_names.rare_pre = keys("RarePrefix", "name", true);
    item_names.rare_suf = keys("RareSuffix", "name", true);
    {
        // Runes.txt "RunewordN" rows sorted by N (80 and 96 don't exist);
        // the save's ID is that rank + 27.
        std::vector<std::pair<int, std::string>> runewords;
        for (const auto& key : keys("Runes", "Name", false))
            if (key.starts_with("Runeword")) runewords.emplace_back(std::atoi(key.c_str() + 8), key);
        std::ranges::sort(runewords);
        for (auto& [number, key] : runewords) item_names.runeword.push_back(std::move(key));
    }
    // inventory.txt "<Class>2" rows are the 800x600 layouts.
    const auto inv = txt("inventory");
    for (std::size_t row = 0; row < inv.size(); ++row) {
        const auto cls = inv.get(row, "class");
        const int layout_index = cls == "Big Bank Page2" ? 1 : cls == "Bank Page2" ? 0
                    : cls == "Transmogrify Box2" ? 2 : -1;
        if (layout_index < 0) continue;
        auto num = [&](const char* column) { return std::atoi(std::string(inv.get(row, column)).c_str()); };
        auto& layout = layout_index == 2 ? game_data.cube_layout : game_data.stash_layout[std::size_t(layout_index)];
        layout.grid_x = num("gridLeft"); layout.grid_y = num("gridTop");
        layout.cols = num("gridX"); layout.rows = num("gridY");
        layout.box_w = num("gridBoxWidth"); layout.box_h = num("gridBoxHeight");
    }
    static constexpr const char* kInvClass[7] = {
        "Amazon2", "Sorceress2", "Necromancer2", "Paladin2", "Barbarian2", "Druid2", "Assassin2" };
    static constexpr const char* kSlotCol[11] = {
        nullptr, "head", "neck", "torso", "rArm", "lArm", "rHand", "lHand", "belt", "feet", "gloves" };
    for (std::size_t class_index = 0; class_index < 7; ++class_index)
        for (std::size_t row = 0; row < inv.size(); ++row) {
            if (inv.get(row, "class") != kInvClass[class_index]) continue;
            auto num = [&](std::string column) { return std::atoi(std::string(inv.get(row, column)).c_str()); };
            auto& layout = game_data.inv_layout[class_index];
            layout.panel_x = num("invLeft"); layout.panel_y = num("invTop");
            layout.grid_x = num("gridLeft"); layout.grid_y = num("gridTop");
            layout.cols = num("gridX"); layout.rows = num("gridY");
            layout.box_w = num("gridBoxWidth"); layout.box_h = num("gridBoxHeight");
            for (std::size_t slot = 1; slot < 11; ++slot) {
                const std::string column = kSlotCol[slot];
                layout.slots[slot] = { num(column + "Left"), num(column + "Top"), num(column + "Width"), num(column + "Height") };
            }
        }
    auto index_of = [&](std::string_view code) {
        for (std::size_t i = 1; i < game_data.comp.size(); ++i)
            if (game_data.comp[i].code == code) return std::uint8_t(i);
        return std::uint8_t(0xff);
    };
    // Class skills: Skills.txt rows by charclass, in file order (the save's
    // 30 "if" bytes), joined with SkillDesc.txt for the tree.
    {
        const auto skills = txt("skills"), desc = txt("skilldesc");
        std::unordered_map<std::string, std::size_t> desc_row;
        for (std::size_t row = 0; row < desc.size(); ++row) desc_row[std::string(desc.get(row, "skilldesc"))] = row;
        auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
        for (std::size_t class_index = 0; class_index < 7; ++class_index) {
            auto& list = game_data.rules.class_skills[class_index];
            std::vector<std::string> names;
            for (std::size_t row = 0; row < skills.size(); ++row) {
                if (skills.get(row, "charclass") != d2d::rules::kClassCode[class_index]) continue;
                d2d::rules::ClassSkill class_skill;
                class_skill.req_level = std::max(1, num(skills.get(row, "reqlevel")));
                if (const int max_level = num(skills.get(row, "maxlvl")); max_level > 0) class_skill.max_level = max_level;
                if (const auto found = desc_row.find(std::string(skills.get(row, "skilldesc"))); found != desc_row.end()) {
                    class_skill.page = num(desc.get(found->second, "SkillPage"));
                    class_skill.row = num(desc.get(found->second, "SkillRow"));
                    class_skill.col = num(desc.get(found->second, "SkillColumn"));
                    class_skill.icon = num(desc.get(found->second, "IconCel"));
                    class_skill.name = std::string(desc.get(found->second, "str name"));
                }
                names.emplace_back(skills.get(row, "skill"));
                list.push_back(std::move(class_skill));
            }
            // reqskill1..3 name skills of the same class.
            std::size_t skill_index = 0;
            for (std::size_t row = 0; row < skills.size(); ++row) {
                if (skills.get(row, "charclass") != d2d::rules::kClassCode[class_index]) continue;
                for (int req_index = 0; req_index < 3; ++req_index) {
                    const auto want = skills.get(row, "reqskill" + std::to_string(req_index + 1));
                    if (const auto found = std::ranges::find(names, want); !want.empty() && found != names.end())
                        list[skill_index].req[std::size_t(req_index)] = int(found - names.begin());
                }
                ++skill_index;
            }
        }
    }
    for (std::size_t class_index = 0; class_index < 7 && class_index < charstats.size(); ++class_index) {
        auto per = [&](const char* column) { return std::atoi(std::string(charstats.get(class_index, column)).c_str()); };
        game_data.class_gains[class_index] = { per("LifePerVitality"), per("StaminaPerVitality"), per("ManaPerMagic"),
                                 per("LifePerLevel"), per("StaminaPerLevel"), per("ManaPerLevel"),
                                 per("StatPerLevel"), per("ToHitFactor"), per("BlockFactor") };
        auto& start = game_data.class_start[class_index];
        start = { per("str"), per("dex"), per("int"), per("vit"), per("stamina"), per("hpadd"), {}, std::string(charstats.get(class_index, "StartSkill")) };
        for (int i = 1; i <= 10; ++i)
            if (const std::string code(charstats.get(class_index, "item" + std::to_string(i))); !code.empty() && code != "0")
                start.items.push_back({ code, std::string(charstats.get(class_index, "item" + std::to_string(i) + "loc")),
                                     std::atoi(std::string(charstats.get(class_index, "item" + std::to_string(i) + "count")).c_str()) });
        auto& gear = game_data.starting_gear[class_index];
        gear.fill(0xff);
        for (int layer : { 1, 2, 3, 4, 8, 9 }) gear[std::size_t(layer)] = 1;   // TR LG RA LA S1 S2 = lit
        for (int i = 1; i <= 10; ++i) {
            const auto item = charstats.get(class_index, "item" + std::to_string(i));
            const auto loc  = charstats.get(class_index, "item" + std::to_string(i) + "loc");
            const auto idx  = index_of(item);
            if (idx == 0xff) continue;
            if (loc == "rarm") gear[5] = idx;
            else if (loc == "larm") gear[game_data.comp[idx].armor ? 7 : 6] = idx;
        }
    }
}

}  // namespace

std::optional<GameData> load_game_data(const fs::path& data_dir, const fs::path& patch_installer, std::uint32_t map_seed,
                                       bool tile_pixels) {
    const auto d2data = data_dir / "d2data.mpq";
    if (!fs::exists(d2data)) {
        d2d::log::error("no d2data.mpq in {} — running without game data (test pattern)", data_dir.string());
        return std::nullopt;
    }
    const auto start_ms = d2d::log::ms();
    try {
        d2d::log::info("Initializing MPQs:");
        d2d::mpq::Stack mpqs;
        auto push = [&](const fs::path& path) {
            mpqs.push(path);
            d2d::log::info("  Loading: {} {} bytes", path.filename().string(), fs::file_size(path));
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
            for (const auto& path : { patch_installer, data_dir / "LODPatch_114d.exe" }) {
                if (path.empty() || !fs::exists(path)) continue;
                try {
                    mpqs.push_installer(path);
                    d2d::log::info("  Loading: {} (1.14d patch installer)", path.string());
                    patched = true;
                    break;
                }
                catch (const std::exception& error) { d2d::log::warn("{}", error.what()); }
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
        for (const char* name : { "d2xtalk.mpq", "d2speech.mpq", "d2sfx.mpq" })
            if (fs::exists(data_dir / name)) push(data_dir / name);

        GameData game_data;
        game_data.tile_pixels = tile_pixels;
        // Frontend button labels (IDs 0x13f2..0x13f7) live in the base
        // string.tbl per probe. patchstring.tbl (826 entries) overrides
        // specific IDs when Blizzard shipped patches; expansionstring.tbl
        // (2788 entries) carries LoD-specific additions. For MVP we use
        // string.tbl directly; when a subsystem needs a patch-shifted
        // entry, load all three and query in order (patch → expansion →
        // base).
        game_data.strings = [&] {
                auto bytes = mpqs.try_read(R"(data\local\LNG\ENG\string.tbl)");
                return bytes ? d2d::tbl::Table(*bytes) : d2d::tbl::Table{};
            }();
        game_data.patch_strings = [&] {
                auto bytes = mpqs.try_read(R"(data\local\LNG\ENG\patchstring.tbl)");
                return bytes ? d2d::tbl::Table(*bytes) : d2d::tbl::Table{};
            }();
        game_data.exp_strings = [&] {
                auto bytes = mpqs.try_read(R"(data\local\LNG\ENG\expansionstring.tbl)");
                return bytes ? d2d::tbl::Table(*bytes) : d2d::tbl::Table{};
            }();
        auto act1 = std::make_unique<d2d::drlg::OutdoorAssets>();
        d2d::drlg::load_outdoor_assets(*act1, [&](const std::string& path) { return mpqs.try_read(path); });
        place_act1(game_data, mpqs, *act1, map_seed);
        // The other levels build when they're first wanted (GameData::level).
        game_data.builder = std::make_shared<GameData::LevelBuilder>();
        game_data.builder->act1 = std::move(act1);
        load_tables(game_data, mpqs);
        load_npcs(game_data, mpqs);
        load_monsters(game_data, mpqs);
        load_skills(game_data, mpqs);
        d2d::log::info("Loading game data... done ({} ms)", d2d::log::ms() - start_ms);
        d2d::log::info("  Items: {}; sounds: {}; town NPCs/objects: {}",
                       game_data.item_tables ? "tables loaded" : "no item tables",
                       game_data.sounds.size(), game_data.town.npcs.size());
        game_data.patched = patched;
        game_data.data_dir = data_dir;
        game_data.mpqs = std::move(mpqs);
        return game_data;
    } catch (const std::exception& error) {
        d2d::log::error("load_game_data: {}", error.what());
        return std::nullopt;
    }
}

void set_map_seed(GameData& game_data, std::uint32_t seed) {
    if (seed == game_data.map_seed || !game_data.builder || !game_data.builder->act1) return;
    for (auto& [id, job] : game_data.builder->jobs) job.wait();
    game_data.builder->jobs.clear();
    game_data.levels.clear();
    const Level& old = game_data.town;
    Level town{ .id = old.id, .name = old.name, .type = old.type, .layer = old.layer, .song = old.song, .ambience = old.ambience,
                .night_ambience = old.night_ambience, .day_event = old.day_event, .night_event = old.night_event,
                .event_delay = old.event_delay, .light = old.light, .rain = old.rain };
    game_data.town = std::move(town);
    place_act1(game_data, game_data.mpqs, *game_data.builder->act1, seed);
    game_data.shrines.clear();                          // load_npcs reads Shrines.txt again
    load_npcs(game_data, game_data.mpqs);
    want_nearby(game_data, game_data.town);
    d2d::log::info("map seed {:#x}: {}", seed, game_data.town.ds1.width() ? "act 1 laid out" : "no town");
}

}  // namespace d2d::game
