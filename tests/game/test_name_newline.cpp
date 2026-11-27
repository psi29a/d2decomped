// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An item name string with "\n" goes into FUN_0048dd90's bottom-up hover
// string as it is (FUN_0048c060), so its last segment is the top line: the
// Scroll of Resistance shows "Scroll of Resistance" over "Right Click to
// Cast", the Potion of Life its name over its use. No line keeps a "\n".
// Needs the game data.

#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <item_text.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

std::vector<TextLine> lines_of(const GameData& data, const std::string& code) {
    d2d::d2s::Item item;
    item.code = code;
    item.identified = true; item.quality = 2; item.defense = -1;
    return item_lines(data, item, 1);
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
    const auto scroll = lines_of(*data, "tr2");
    assert(scroll.size() >= 2 && scroll[0].text == "Scroll of Resistance" && scroll[1].text == "Right Click to Cast");
    const auto potion = lines_of(*data, "xyz");
    assert(potion.size() >= 2 && potion[0].text == "Potion of Life" && potion[1].text == "Right Click to permanently add 20 to Life");
    for (const auto* lines : { &scroll, &potion })
        for (const auto& line : *lines) assert(line.text.find('\n') == std::string::npos);
    std::printf("name newlines: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_name_newline: %s\n", error.what());
    return 1;
}
