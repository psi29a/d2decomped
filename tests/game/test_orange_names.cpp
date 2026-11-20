// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_0048dd90 (0x48e9b0..0x48ea15) and FUN_004527d0: after the quality's
// colour, an item named ceh bet tes fed toa dhn bey mbr pk1 pk2 pk3, or of
// itemtype 0x4a (rune), gets colour code 8 (orange) on its name, hover and
// floor label alike. A potion or a plain key stays white. Needs the game
// data.

#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <item_text.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

using namespace d2d::game;

namespace {

std::array<std::uint8_t, 3> name_rgb(const GameData& data, const std::string& code) {
    d2d::d2s::Item item;
    item.code = code;
    item.identified = true; item.quality = 2; item.defense = -1;
    const auto lines = item_lines(data, item, 1);
    assert(!lines.empty());
    return lines.front().rgb;
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    for (const char* code : { "ceh", "bet", "tes", "fed", "toa", "dhn", "bey", "mbr", "pk1", "pk2", "pk3" })
        assert(name_rgb(*data, code) == kTxtOrange);
    for (const char* rune : { "r01", "r15", "r33" }) assert(name_rgb(*data, rune) == kTxtOrange);
    for (const char* plain : { "hp1", "key", "isc", "gsr" }) assert(name_rgb(*data, plain) == kTxtWhite);
    std::printf("orange names: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_orange_names: %s\n", error.what());
    return 1;
}
