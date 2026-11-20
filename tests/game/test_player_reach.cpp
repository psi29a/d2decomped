// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The player's melee on a wall map, through the REAL Fight: where the swing starts (engage) and
// where the blow lands (hit). game.exe's client ends its walk to a monster, and its server starts
// the swing, when FUN_00622c40(player, monster, FUN_00622dc0(monster)) holds; the blow's to-hit
// (FUN_0057ec10) asks FUN_00622c40(player, monster, 1) again (tools/emu/player_reach.py). Needs
// the game data (D2_MPQ_DIR, D2_PATCH_INSTALLER); it skips without.
//
// `--player-reach CASES` prints, per case `id px py mx my msize mwalk range nwalls [x y word]...`
// (subtiles; `word` the collision word of a wall subtile), `id swing`: 1 when engage started the
// swing, 0 when it asked to walk on. `--player-hit CASES` prints `id landed` for the blow. The
// cases are tools/emu/player_reach.py's; diff against its `game` and `hit` output.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

// One case as the oracle's files have it, and what game.exe answers (tools/emu/player_reach.py,
// V run: `game` = the swing starts, `hit` = the blow reaches).
struct Pin { const char* line; int start, hit; };
constexpr Pin kPins[] = {
    { "1 30 30 34 30 2 0 0 0", 0, 1 },                                // 4 subtiles off, standing: the blow reaches, the walk goes on
    { "2 30 30 34 30 2 0 0 1 32 30 4", 0, 0 },                        // a missile wall (4) on the line: no blow either
    { "3 30 30 34 30 2 1 0 0", 1, 1 },                                // walking: one more (FUN_00622dc0), the swing starts
    { "4 30 30 34 30 2 1 0 1 32 30 4", 0, 0 },                        // a wall on the line stops the start and the blow
    { "5 30 30 34 30 2 1 0 1 32 30 2048", 0, 0 },                     // a door's word 0x800 does too
    { "6 30 30 34 30 2 1 0 1 32 30 1", 1, 1 },                        // a walk-only wall (1) does not
    { "7 30 30 34 30 2 1 0 1 32 30 1024", 1, 1 },                     // nor an object's word (0x400)
    { "8 30 30 34 30 2 1 0 1 32 31 4", 1, 1 },                        // a wall off the line
    { "9 30 30 34 30 2 1 0 1 33 30 4", 1, 1 },                        // the line's ends step in by the units' sizes: (33, 30) is not on it
    { "10 30 30 34 31 2 1 0 2 32 30 4 32 31 4", 0, 0 },               // a two-subtile wall across it
    { "11 30 30 35 30 2 0 2 0", 0, 1 },                               // a polearm (range 2): the blow reaches 5 off, the swing starts only inside
    { "12 30 30 33 30 2 0 0 1 31 30 4", 1, 1 },                       // 3 off the units' sizes overlap: no line to cut
    { "13 30 30 36 30 3 0 0 0", 0, 0 },                               // a big one 6 off: out of reach
    { "14 30 30 33 30 1 0 0 0", 1, 1 },                               // a small one (size 1) 3 off: the distance table is by size
    { "15 30 30 34 30 1 0 0 0", 0, 0 },                               // 4 off: neither (a size 2 one is reached by the blow here)
    { "16 30 30 34 30 3 0 0 0", 1, 1 },                               // a big one (size 3) 4 off: the swing starts as well
    { "17 30 30 27 29 1 0 0 0", 0, 1 },                               // a small one 3 across and 1 down: the walk goes on, the blow reaches
    { "18 26 27 27 24 2 0 2 1 26 25 4", 0, 0 },                       // a polearm, 3 down and 1 across, a wall on the line: neither
};

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    constexpr int kPaladin = 3;
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, kPaladin, "Headless", false, true, made_rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = data->level(2);                                 // the Blood Moor: out of town
    assert(world.level);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    const Level& level = *world.level;
    d2d::rules::Rng rng{ 3 };

    // A MonStats row of each SizeX (common enemies first, then any plain one: a boss has rules of its
    // own; not a tentacle or a vulture, whose reach has its own rule).
    std::map<int, int> monster_of_size;
    {
        std::vector<std::pair<std::string, int>> ids(data->monsters.by_id.begin(), data->monsters.by_id.end());
        std::ranges::sort(ids);
        for (const char* wanted : { "fallen1", "zombie1", "skeleton1", "goatman1", "quillrat1", "fetish1", "brute1", "bighead1", "sandmaggot1", "mummy1" })
            if (const auto found = data->monsters.by_id.find(wanted); found != data->monsters.by_id.end() && !monster_of_size.contains(data->monsters.types[std::size_t(found->second)].size))
                monster_of_size[data->monsters.types[std::size_t(found->second)].size] = found->second;
        for (const auto& [id, row] : ids) {
            const auto& type_info = data->monsters.types[std::size_t(row)];
            const int size = type_info.size;
            if (!type_info.can_walk || !type_info.can_swing || type_info.interact || type_info.critter || type_info.diff[0].treasure_class.empty()) continue;
            if (monster_of_size.contains(size) || id.find("tentacle") != std::string::npos || id.find("vulture") != std::string::npos) continue;
            const Monster probe = make_monster(*data, row, 10, 10, rng, 0);
            if (probe.targetable() && probe.alive() && !probe.ally_not_interact(*data, level.id)) monster_of_size[size] = row;
        }
    }
    // A melee weapon code of each rangeadder.
    std::map<int, std::string> weapon_of_range;
    for (const auto& [code, info] : data->rules.item_info)
        if (d2d::rules::type_is(data->rules, info.type, "mele") && (!weapon_of_range.contains(info.range_adder) || code < weapon_of_range[info.range_adder])) weapon_of_range[info.range_adder] = code;
    // The right hand's weapon, back as it was before each case (a landed hit wears it, and a worn out one goes).
    const auto find_weapon = [&]() -> d2d::d2s::Item* {
        for (auto& item : actor.character.items)
            if (item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kRightArm) return &item;
        return nullptr;
    };
    assert(find_weapon());
    const d2d::d2s::Item weapon_copy = *find_weapon();
    const int width = level.ds1.width() * 5;

    // One case through the real Fight: {the swing started, the blow landed}, {-1, -1} for a size or range
    // the tables have no row for.
    const auto run_case = [&](const std::string& line, int& id) -> std::pair<int, int> {
        std::istringstream fields(line);
        int player_x, player_y, mon_x, mon_y, size, walk, range, walls;
        if (!(fields >> id >> player_x >> player_y >> mon_x >> mon_y >> size >> walk >> range >> walls)) { id = -1; return { -1, -1 }; }
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) level.walk[std::size_t(y) * std::size_t(width) + std::size_t(x)] = 0;
        // Bounded so a malformed line can't run the loop away or write off the 64 x 64 corner the cases use.
        walls = std::clamp(walls, 0, 64 * 64);
        for (int k = 0; k < walls; ++k) {
            int x = 0, y = 0, word = 0;
            if (!(fields >> x >> y >> word) || x < 0 || y < 0 || x >= 64 || y >= 64) { id = -1; return { -1, -1 }; }
            level.walk[std::size_t(y) * std::size_t(width) + std::size_t(x)] = std::uint16_t(word);
        }
        if (!monster_of_size.contains(size) || !weapon_of_range.contains(range)) return { -1, -1 };
        auto* weapon = find_weapon();
        if (!weapon) { actor.character.items.push_back(weapon_copy); weapon = &actor.character.items.back(); }
        *weapon = weapon_copy;
        weapon->code = weapon_of_range[range];
        assert(fight.weapon_range() == range);
        const auto place = [&] {
            Monster made = make_monster(*data, monster_of_size[size], 10, 10, rng, 0);
            made.stats.hit_points = 50000; made.hit_points = 50000; made.stats.armor_class = 0; made.stats.level = 1;
            made.unit.x = (float(mon_x) + 0.5f) / 5; made.unit.y = (float(mon_y) + 0.5f) / 5;
            made.unit.walking = walk != 0; made.mode = walk ? "WL" : "NU";
            fight.monsters.clear();
            fight.add_monster(std::move(made));
            actor.player.x = (float(player_x) + 0.5f) / 5; actor.player.y = (float(player_y) + 0.5f) / 5;
            actor.attack_skill = 0; actor.attack_mon = 0; actor.pmode = -1;
        };
        place();
        const auto walk_on = fight.engage(1000);
        const int started = !walk_on && actor.pmode >= 0 ? 1 : 0;
        // The blow: Fight::hit on a started swing, as often as its to-hit roll may miss (95 percent at most, no armor).
        place();
        fight.start_swing(1000);
        bool landed = false;
        for (int attempt = 0; attempt < 100 && !landed; ++attempt) {
            fight.hit(1000);
            landed = fight.monsters[0].hit_points < 50000;
        }
        return { started, landed ? 1 : 0 };
    };

    if (argc > 2 && (std::string_view(argv[1]) == "--player-reach" || std::string_view(argv[1]) == "--player-hit")) {
        const bool hit_mode = std::string_view(argv[1]) == "--player-hit";
        std::ifstream cases(argv[2]);
        if (!cases) { std::fprintf(stderr, "player-reach: cannot open %s\n", argv[2]); return 1; }
        std::string line;
        while (std::getline(cases, line)) {
            int id = 0;
            const auto [started, landed] = run_case(line, id);
            if (id < 0) continue;
            std::printf("%d %d\n", id, hit_mode ? landed : started);
        }
        return 0;
    }

    if (argc > 1) { std::fprintf(stderr, "usage: test_player_reach [--player-reach CASES | --player-hit CASES]\n"); return 1; }
    // game.exe's answers for the pins (V run, tools/emu/player_reach.py).
    for (const auto& pin : kPins) {
        int id = 0;
        const auto [started, landed] = run_case(pin.line, id);
        if (started != pin.start || landed != pin.hit) {
            std::fprintf(stderr, "pin %d: swing %d (game.exe %d), blow %d (game.exe %d)\n", id, started, pin.start, landed, pin.hit);
            return 1;
        }
    }
    std::puts("OK: the player's melee reach and line");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_player_reach: %s\n", error.what());
    return 1;
}
