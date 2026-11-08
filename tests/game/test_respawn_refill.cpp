// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The respawn after death (client packet 0x41, FUN_0054c0e0) sets life, mana and stamina to their
// maxima (0x54c17c, 0x54c18a, 0x54c198), through the REAL World::respawn. `--revive-stats CASES`:
// one line per case `id life maxlife mana maxmana stamina maxstamina` (256ths) of
// tools/emu/fr_revive.py, the answer `id life mana stamina maxlife maxmana maxstamina` as
// `python3 fr_revive.py game CASES` prints game.exe's. With no argument it checks one case for two classes.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

// The stats a dead player of class `cls` holds after World::respawn when it died holding `given`
// (life, maxlife, mana, maxmana, stamina, maxstamina), in fr_revive.py's order: life, mana,
// stamina, maxlife, maxmana, maxstamina.
std::array<std::int64_t, 6> respawned(const GameData& data, int cls, const std::array<std::int64_t, 6>& given) {
    using namespace d2d::d2s;
    d2d::rules::Rng made_rng(7);
    auto made = new_character(data, cls, "Headless", false, true, made_rng);
    Character saved;
    saved.character_class = cls;
    saved.name = "Headless";
    saved.header = made.header;
    saved.stats = made.stats;
    saved.items = made.items;
    saved.panel = panel_stats(data, saved.header, saved.items, saved.stats);
    World world(&data, -1, -1);
    world.enter(saved);
    auto& values = world.actor->character.stats.values;
    const std::array<StatId, 6> ids{ kLife, kMaxLife, kMana, kMaxMana, kStamina, kMaxStamina };
    for (std::size_t i = 0; i < ids.size(); ++i) values[ids[i]] = given[i];
    world.respawn(1000);
    return { values[kLife], values[kMana], values[kStamina], values[kMaxLife], values[kMaxMana], values[kMaxStamina] };
}

}  // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (!mode.empty() && !(mode == "--revive-stats" && argc > 2)) {
        std::printf("SKIP: modes are none and `--revive-stats CASES` (see tools/emu/fr_revive.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    if (mode.empty()) {
        for (const int cls : { 1, 4 }) {   // Sorceress, Barbarian
            const auto got = respawned(*data, cls, { 0, 90 * 256 + 77, 12 * 256, 40 * 256 + 99, 3 * 256, 80 * 256 + 13 });
            assert(got[0] == 90 * 256 + 77 && got[1] == 40 * 256 + 99 && got[2] == 80 * 256 + 13);
            assert(got[3] == 90 * 256 + 77 && got[4] == 40 * 256 + 99 && got[5] == 80 * 256 + 13);
        }
        std::printf("test_respawn_refill: ok\n");
        return 0;
    }
    std::ifstream cases(argv[2]);
    if (!cases) {
        std::printf("test_respawn_refill: cannot read %s\n", argv[2]);
        return 1;
    }
    std::string line;
    while (std::getline(cases, line)) {
        std::istringstream record(line);
        std::string id;
        std::array<std::int64_t, 6> given{};
        if (!(record >> id)) continue;   // a blank line
        std::string extra;
        if (!(record >> given[0] >> given[1] >> given[2] >> given[3] >> given[4] >> given[5]) || (record >> extra)) {
            std::printf("test_respawn_refill: bad case line in %s: %s\n", argv[2], line.c_str());
            return 1;
        }
        const auto got = respawned(*data, 4, given);
        std::printf("%s %lld %lld %lld %lld %lld %lld\n", id.c_str(), static_cast<long long>(got[0]), static_cast<long long>(got[1]),
                    static_cast<long long>(got[2]), static_cast<long long>(got[3]), static_cast<long long>(got[4]), static_cast<long long>(got[5]));
    }
    return 0;
} catch (const std::exception& error) {
    std::printf("test_respawn_refill: %s\n", error.what());
    return 1;
}
