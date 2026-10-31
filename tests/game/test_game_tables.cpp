// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// GameData's tables from the MPQs (D2_MPQ_DIR; the 1.14d ones with
// D2_PATCH_INSTALLER) against game.exe: states, MonProp rows, sounds,
// collision shapes, every level's monster region, Act 1's drops.

#include <ai.hpp>
#include <drops.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s (the checks above ran)\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    {
        // The camp's room seeds (a merc's teleport rolls on them, fight/merc.cpp
        // room_teleport): one per level_room, each a DRLG room's.
        Spawning::LevelState camp;
        bool kept = false;
        for (int tries = 0; tries < 200 && !(kept = keep_camp_seeds(*data, data->town, camp)); ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(kept && !camp.room_seeds.empty() && level_room(data->town, camp.room_seeds.size() - 1) && !level_room(data->town, camp.room_seeds.size()));
        assert(std::ranges::all_of(camp.room_seeds, [](const d2d::rules::Rng& seed) { return seed.low != 0; }));
        std::printf("OK: the camp keeps %zu room seeds\n", camp.room_seeds.size());
    }
    if (data->patched) {
        // The antidote's and thawing's states (misc.txt pSpell 6, FUN_005beda0):
        // +50 resist, +10 max, for len 750.
        const auto& antidote = data->rules.potions.at("yps");
        const auto& thawing = data->rules.potions.at("wms");
        using StatValue = std::pair<int, int>;
        assert(antidote.antidote && antidote.ticks == 750 && (antidote.state_stats == std::vector<StatValue>{ { 45, 50 }, { 46, 10 } }));
        assert(thawing.thawing && thawing.ticks == 750 && (thawing.state_stats == std::vector<StatValue>{ { 43, 50 }, { 44, 10 } }));
        std::printf("OK: the antidote's and thawing's state stats\n");
    }
    {
        // MonProp knock and crush on a monster foe (FUN_005bf960, FUN_005bffc0):
        // MonStats2 small / large, a hireling's 10, a boss's or unique's 8, +50 % a player past one.
        const auto row = [&](std::string_view id) { return int(data->monsters.by_id.at(std::string(id))); };
        const auto props_of = [&](int type, d2d::rules::Boss boss, int players) {
            Monster monster; monster.type = type; monster.boss = boss; monster.players = players;
            Foe foe; monster_foe_props(*data, monster, foe);
            return std::pair{ foe.knock_under, foe.crush_divisor };
        };
        const int zombie = row("zombie1"), quill = row("quillrat1");
        assert(!data->monsters.types[std::size_t(zombie)].small && !data->monsters.types[std::size_t(zombie)].large);
        assert(props_of(zombie, d2d::rules::Boss::none, 1) == std::pair(0x40, 4) && props_of(zombie, d2d::rules::Boss::champion, 1).second == 4);
        assert(props_of(zombie, d2d::rules::Boss::unique, 1).second == 8 && props_of(zombie, d2d::rules::Boss::superunique, 3).second == 16);
        assert(props_of(zombie, d2d::rules::Boss::none, 2).second == 6 && props_of(row("andariel"), d2d::rules::Boss::none, 1).second == 8);
        assert(props_of(row("roguehire"), d2d::rules::Boss::none, 4).second == 10 && row("roguehire") == 0x10f);
        assert(data->monsters.types[std::size_t(quill)].small == (props_of(quill, d2d::rules::Boss::none, 1).first == 0x80));
        assert(std::ranges::any_of(data->monsters.types, &d2d::rules::MonType::large) && std::ranges::any_of(data->monsters.types, &d2d::rules::MonType::small));
        std::printf("OK: MonProp knock and crush on monster foes\n");
    }
    if (data->patched) {                                          // the 1.14d tables (the CD's may differ)
        // MonProp rows as game.exe compiled them (tools/emu/monprop_rows.py: the
        // emulator's DataTables +0xc30), per MonStats row and difficulty;
        // every other row has none.
        struct Case { int row, difficulty; std::string_view code; int min, max; };
        static constexpr Case kCases[] = {
            { 295, 0, "fade", 1, 1 }, { 295, 1, "fade", 2, 2 }, { 295, 2, "fade", 2, 2 },
            { 242, 1, "cast2", 15, 15 }, { 242, 1, "swing2", 15, 15 }, { 242, 2, "cast2", 30, 30 }, { 242, 2, "swing2", 30, 30 },
            { 704, 1, "cast2", 15, 15 }, { 704, 1, "swing2", 15, 15 }, { 704, 2, "cast2", 30, 30 }, { 704, 2, "swing2", 30, 30 },
            { 186, 1, "crush", 15, 15 }, { 186, 2, "crush", 25, 25 }, { 187, 1, "crush", 15, 15 }, { 187, 2, "crush", 25, 25 },
            { 188, 1, "crush", 15, 15 }, { 188, 2, "crush", 25, 25 }, { 189, 1, "crush", 15, 15 }, { 189, 2, "crush", 25, 25 },
            { 618, 1, "crush", 15, 15 }, { 618, 2, "crush", 25, 25 }, { 619, 1, "crush", 15, 15 }, { 619, 2, "crush", 25, 25 },
            { 345, 1, "extra-fire", 40, 40 }, { 345, 2, "extra-fire", 100, 100 }, { 346, 1, "extra-fire", 40, 40 }, { 346, 2, "extra-fire", 100, 100 },
            { 347, 1, "extra-fire", 40, 40 }, { 347, 2, "extra-fire", 100, 100 }, { 557, 1, "extra-fire", 60, 60 }, { 557, 2, "extra-fire", 120, 120 },
            { 267, 2, "knock", 1, 1 },
            { 605, 1, "thorns", 15, 20 }, { 605, 2, "thorns", 30, 40 }, { 606, 1, "thorns", 15, 20 }, { 606, 2, "thorns", 30, 40 },
            { 607, 1, "thorns", 15, 20 }, { 607, 2, "thorns", 30, 40 },
            { 419, 0, "stupidity", -1, -1 }, { 419, 1, "stupidity", -1, -1 }, { 419, 2, "stupidity", -1, -1 },
        };
        std::size_t slots = 0;
        for (const auto& type : data->monsters.types)
            for (const auto& per_difficulty : type.props) slots += per_difficulty.size();
        assert(slots == std::size(kCases));
        std::array<std::array<std::size_t, 3>, 800> seen{};
        for (const auto& entry : kCases) {
            const auto& slot = data->monsters.types.at(std::size_t(entry.row)).props[std::size_t(entry.difficulty)].at(seen[std::size_t(entry.row)][std::size_t(entry.difficulty)]++);
            assert(slot.code == entry.code && slot.min == entry.min && slot.max == entry.max && slot.chance == 0);
        }
        // What they come to: Mephisto's Hell IAS and FCR, a Hell quill rat's
        // thorns drawn on its seed, Blunderbore's crush.
        d2d::rules::Rng seed(1234);
        const auto mephisto = d2d::rules::monster_props(data->rules, data->monsters.types[242], 2, seed);
        assert(mephisto.ias == 30 && mephisto.fcr == 30 && seed.low == 1234);       // min == max: no draw
        auto drawn = seed;
        const auto quill = d2d::rules::monster_props(data->rules, data->monsters.types[607], 2, seed);
        assert(quill.thorns == drawn.range(30, 40) && quill.thorns >= 30 && quill.thorns <= 40);
        assert(d2d::rules::monster_props(data->rules, data->monsters.types[189], 2, seed).crush == 25);
        assert(d2d::rules::monster_props(data->rules, data->monsters.types[267], 1, seed).knock == 0);
        assert(d2d::rules::monster_props(data->rules, data->monsters.types[267], 2, seed).knock == 1);
        std::printf("OK: MonProp rows match game.exe's (%zu slots)\n", slots);
    }
    // Skill and missile sounds (unit-sounds.md): Fire Bolt's cast, Bash's grunt (the
    // Barbarian's own), its swing held back; the fire bolt's flight and impact.
    if (const auto* fire_bolt = data->skills.get(36); fire_bolt && data->missiles.contains("firebolt")) {
        assert(fire_bolt->st_sound == data->sound_index.at("sorceress_cast_fire"));
        const auto* bash = data->skills.get(126);
        assert(bash && bash->st_sound_class == data->sound_index.at("barbarian_grunt_small_1") && bash->st_sound_delay && bash->hit_class == 112);
        const auto& bolt = data->missiles.at("firebolt");
        // Missiles.txt HitClass is a number (FUN_00661b20: type 4, +0x94): an arrow's bow class
        // is the struck player's impact_arrow, and a later block's block_arrow.
        const int arrow = data->missiles.at("arrow").hit_class;
        assert(arrow == 10 && data->missiles.at("javelin").hit_class == 6);
        if (const auto spit = data->missiles.find("goospit1"); spit != data->missiles.end()) assert(spit->second.hit_class == 0xb0);
        assert(d2d::rules::sound_ids::impact(arrow, false).impact == 347 && d2d::rules::sound_ids::block(arrow) == 401);
        assert(std::ranges::any_of(data->monsters.types, [](const auto& type) { return type.id == "zombie1" && type.hit_class == 12; }));
        const auto& zombie = data->mon_sounds.at("zombie");   // its Neutral every 300 ticks at most, two light steps a walk
        assert(zombie.neutral == data->sound_index.at("zombie_neutral_1") && zombie.neutral_time == 300 && zombie.footstep == data->sound_index.at("light_walk_dirt_1") && zombie.fs_count == 2);
        assert(data->states.at("amplifydamage").on_sound == data->sound_index.at("necromancer_amplifydamage") && data->states.at("wolf").off_sound > 0);
        assert(bolt.travel_sound == data->sound_index.at("sorceress_firebolt_1") && bolt.hit_sound == data->sound_index.at("sorceress_firebolt_impact_1"));
    }
    // Collision shapes (FUN_006484e0): the Rogue merc stamps 0x2000 and
    // opens doors, Akara 0x1000; a player walks through the merc, not back.
    if (data->mon_npc.size() > 0x10f) {                  // MonStats: the patch's
        const auto rogue = data->mon_npc[0x10f].shape, akara = data->mon_npc[std::size_t(d2d::rules::monster_ids::kAkara)].shape;
        assert(rogue.bit() == 0x2000 && rogue.mask == 0x3401 && akara.bit() == 0x1000);
        UnitState player{ .x = 10.1f, .y = 10.1f }, merc{ .x = 10.3f, .y = 10.1f, .shape = rogue };
        const Crowd crowd{ { &player, &merc } };
        assert(!crowd.at(player.x, player.y, &player) && crowd.at(merc.x, merc.y, &merc));
        assert(!crowd.at(10.5f, 10.1f, &merc));          // two subtiles off: clear of the player's subtile
        // SizeX 0 (the chicken): no footprint, its own subtile tested; a pet's 1 → 3.
        const auto chicken_row = std::ranges::find(data->monsters.types, std::string("chicken"), &d2d::rules::MonType::id) - data->monsters.types.begin();
        const auto chicken = data->mon_npc[std::size_t(chicken_row)].shape;
        assert(chicken.pattern == 0 && !chicken.stamps() && akara.owned().bit() == 0x2000);
        UnitState bird{ .x = 10.1f, .y = 10.3f, .shape = chicken };
        const Crowd yard{ { &player, &bird } };
        assert(!yard.at(player.x, player.y, &player) && yard.at(bird.x, 10.1f, &bird) && !yard.at(bird.x, bird.y, &bird));
    }
    // A server's GameData keeps the tiles' walk flags, not their pixels.
    for (const auto& archive : data->town.dt1s)
        for (const auto& tile : archive.tiles()) assert(tile.pixels.empty());

    // Every level's monster region, as game.exe makes them at game start
    // (tools/emu regions.py 0x1234 prints the same lines): seed 0x1234, normal.
    std::string regions;
    d2d::rules::Rng region_seed{ d2d::rules::Rng{ 0x1234 }.next() };
    for (std::size_t id = 1; id < data->level_mon.size(); ++id) {
        const auto region = d2d::rules::monster_region(data->monsters, data->level_mon[id], 0, region_seed);
        if (region.types.empty()) continue;
        regions += std::to_string(id);
        for (std::size_t i = 0; i < region.types.size(); ++i) {
            regions += " " + std::to_string(region.types[i].first) + ":";
            for (std::size_t set = 0; set < region.components[i].size(); ++set) {
                if (set) regions += "/";
                for (const auto layer : region.components[i][set]) { char hex[3]; std::snprintf(hex, sizeof hex, "%02x", layer); regions += hex; }
            }
        }
        regions += "\n";
    }
    std::uint64_t hash = 0xcbf29ce484222325;                      // FNV-1a of game.exe's lines
    for (const char letter : regions) hash = (hash ^ std::uint8_t(letter)) * 0x100000001b3;
    if (patch) {                                                  // the 1.14d tables (the CD's differ)
        if (hash != 0x69b556c850c6632eull) std::printf("%s", regions.c_str());
        assert(hash == 0x69b556c850c6632eull);
    }

    // Act 1's drops as game.exe rolls them (tools/emu drops.py act1 prints
    // the same lines): its classes sorted, the jobs of drops.py jobs().
    std::vector<std::string> act1;
    for (const auto& [name, treasure] : data->rules.treasure)
        if (name.starts_with("Act 1 ") || name.starts_with("Andariel") || name.starts_with("Countess") || name.starts_with("Cow")) act1.push_back(name);
    std::sort(act1.begin(), act1.end());
    std::string drops;
    for (std::uint32_t job = 1; job <= 3000; ++job) {
        const std::string& name = act1[job % act1.size()];
        const int level = int(job * 37 % 100), ilvl = int(1 + job * 13 % 99), players = int(1 + job % 8);
        const int magic_find = std::array{ 0, 0, 0, 50, 110, 250, 600, -100 }[job / 8 % 8];
        d2d::rules::Rng seed{ job * 0x9E3779B1u };
        const auto moved = d2d::rules::tc_upgrade(data->rules, name, level);
        std::vector<d2d::rules::Drop> dropped;
        d2d::rules::roll_drops(data->rules, moved, ilvl, seed, dropped, players, magic_find);
        char text[128];
        std::snprintf(text, sizeof text, "%08x %s@%d>%s i%d p%d m%d:", job * 0x9E3779B1u, name.c_str(), level, moved.c_str(), ilvl, players, magic_find);
        drops += text;
        for (const auto& drop : dropped) drops += " " + drop.code + ":" + std::to_string(drop.quality) + (drop.mul ? "*" + std::to_string(drop.mul) : "");
        std::snprintf(text, sizeof text, " -> %08x\n", seed.low);
        drops += text;
    }
    std::uint64_t drops_hash = 0xcbf29ce484222325;
    for (const char letter : drops) drops_hash = (drops_hash ^ std::uint8_t(letter)) * 0x100000001b3;
    if (patch) {
        if (drops_hash != 0x86f78a5cf5373648ull) std::printf("%s%016llx\n", drops.c_str(), (unsigned long long)drops_hash);
        assert(drops_hash == 0x86f78a5cf5373648ull);
    }

    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
