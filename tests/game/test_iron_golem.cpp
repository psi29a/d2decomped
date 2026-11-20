// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Iron Golem's item, as tools/emu/iron_golem.py runs game.exe's FUN_005c32a0
// and FUN_0062ea80: `--iron-golem` reads its cases (`type mode pick flags
// held`, the item `pick` modulo the Items rows) and prints each with
// `| takes body1 body2`: rules::iron_golem_takes on the item's Items
// bitfield1, its ItemTypes BodyLoc1 / BodyLoc2 (0 / 0 not an item).
// Without arguments, with the game data: an identified chain mail passes,
// an unidentified one and a leather armour don't.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <pets.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int iron_golem_cases(const GameData& data) {
    const auto& rules = data.rules;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long type = 0, mode = 0, pick = 0, flags = 0, held = 0;
        if (!(fields >> type >> mode >> pick >> flags >> held)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto& code = rules.item_order[std::size_t(pick) % rules.item_order.size()];
        const auto base = rules.item_base.find(code);
        const auto info = rules.item_info.find(code);
        const auto item_type = info == rules.item_info.end() ? rules.types.end() : rules.types.find(info->second.type);
        const bool takes = base != rules.item_base.end()
                           && d2d::rules::iron_golem_takes(int(type), int(mode), base->second.bitfield1, std::uint32_t(flags), held != 0);
        const auto body = type == 4 && item_type != rules.types.end() ? item_type->second.body : std::array<int, 2>{};
        std::printf("%s | %d %d %d\n", line.c_str(), takes ? 1 : 0, body[0], body[1]);
    }
    return 0;
}

int item_check(const GameData& data) {
    const auto takes = [&](const char* code, bool identified) {
        return d2d::rules::iron_golem_takes(4, 3, data.rules.item_base.at(code).bitfield1, identified ? 0x10 : 0, false);
    };
    assert(takes("chn", true) && !takes("chn", false) && !takes("lea", true));
    std::puts("OK: Iron Golem takes an identified chain mail, not an unidentified one or a leather armour");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--iron-golem";
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--iron-golem needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--iron-golem needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    return cases ? iron_golem_cases(*data) : item_check(*data);
} catch (const std::exception& error) {
    std::printf("test_iron_golem: %s\n", error.what());
    return 1;
}
