// The game runs on its own: components/game needs nothing from the client
// (apps/d2d: SDL, sound, sprites, fonts), so a standalone server links
// just this library. With the game's MPQs (D2_MPQ_DIR, as test_outdoor)
// it loads GameData and plays a new character for a second.
#include <character.hpp>
#include <character_store.hpp>
#include <drops.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace d2d::game;

int main() {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
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

    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Headless", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    for (std::uint32_t tick = 1; tick <= 25; ++tick) world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
    const auto view = world.view();
    assert(view.level && view.level->id == 1 && !view.dead);
    assert(!view.level->unit_blocked(view.player.x, view.player.y));
    std::printf("OK: %s in level %d at (%.1f, %.1f)\n", character.name.c_str(), view.level->id, view.player.x, view.player.y);
    return 0;
}
