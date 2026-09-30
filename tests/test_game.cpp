// The game runs on its own: components/game needs nothing from the client
// (apps/d2d: SDL, sound, sprites, fonts), so a standalone server links
// just this library. With the game's MPQs (D2_MPQ_DIR, as test_outdoor)
// it loads GameData and plays a new character for a second.
#include <character.hpp>
#include <character_store.hpp>
#include <drops.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <tuple>
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

    // d2d's autoloot (deviations.md #5): gold under the player goes into the purse.
    const auto purse = world.character.stats.get(d2d::d2s::kGold);
    world.loot.put({ .code = "gld", .gold = 7 }, world.player.x, world.player.y, 1, world.fight.spawning.game, 26 * kTickMs);
    std::tie(world.loot.ground.back().x, world.loot.ground.back().y) = std::pair{ world.player.x, world.player.y };
    world.tick({}, 26 * kTickMs, 25 * kTickMs);
    assert(world.character.stats.get(d2d::d2s::kGold) == purse + 7 && world.loot.ground.empty());

    // Tristram Cain (FUN_00593290 -> FUN_005e7880): the Gibbet opened, he
    // comes out, walks off, opens his portal, walks back in: camp Cain due.
    // (The 1.14d tables: the CD's leave Tristram's presets unplaced.)
    if (!patch) return 0;
    const auto* tristram = data->level(d2d::rules::CainQuest::kTristram);
    const auto gibbet = std::ranges::find(tristram->npcs, 10, &Npc::operate_fn);
    const auto cain_npc = std::ranges::find(tristram->npcs, d2d::rules::CainQuest::kCain, &Npc::hc_idx);
    assert(gibbet != tristram->npcs.end() && cain_npc != tristram->npcs.end());
    const auto cain_index = std::size_t(cain_npc - tristram->npcs.begin());
    const auto* camp = world.level;
    world.level = tristram;
    std::tie(world.player.x, world.player.y) = tristram->nearest_free(gibbet->x + 2, gibbet->y + 2);
    world.swap_npcs(camp);
    assert(world.npc_states[cain_index].hidden);
    world.operate(int(gibbet - tristram->npcs.begin()), 26 * kTickMs);
    assert(world.cain_walk.npc == int(cain_index) && !world.cain.camp_due);
    float cain_moved = 0;
    for (std::uint32_t tick = 27; tick < 27 + 750 && !world.cain.camp_due; ++tick) {
        world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
        const auto& cain_state = world.npc_states[cain_index];
        if (!cain_state.hidden) cain_moved = std::max(cain_moved, std::hypot(cain_state.x - cain_npc->x, cain_state.y - cain_npc->y));
    }
    assert(world.cain.camp_due && world.cain_walk.npc < 0 && world.npc_states[cain_index].hidden && cain_moved > 0.3f);
    std::printf("OK: Tristram Cain walked %.2f cells and took his portal\n", cain_moved);
    return 0;
}
