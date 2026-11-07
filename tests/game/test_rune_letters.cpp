// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00486670 (0x486670): an item that takes sockets shows its socketed
// runes' gems.txt letters between RuneQuotes (20506), just under the name
// (FUN_0048dd90 adds that buffer last before the name's, 0x48ebb1); a gem
// adds nothing. Needs the game data.

#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <item_text.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const std::string quote = string_id(*data, 20506);
    assert(quote == "'");                            // no colour code of its own
    d2d::d2s::Item sword;
    sword.code = "crs";
    sword.identified = true; sword.socketed = true; sword.sockets = 3; sword.quality = 2; sword.defense = -1;
    d2d::d2s::Item tal, eth, ruby;
    tal.code = "r07"; eth.code = "r05"; ruby.code = "gsr";
    sword.socketed_items.push_back(std::move(tal));
    sword.socketed_items.push_back(std::move(eth));
    const auto lines = item_lines(*data, sword, 1);
    assert(lines.size() >= 2 && lines[1].text == quote + "TalEth" + quote);
    data->gem_letters.erase("r05");                 // a rune with no letters row adds nothing
    const auto tal_only = item_lines(*data, sword, 1);
    assert(tal_only.size() >= 2 && tal_only[1].text == quote + "Tal" + quote);
    sword.socketed_items.clear();
    sword.socketed_items.push_back(std::move(ruby));
    const auto gem_lines = item_lines(*data, sword, 1);
    assert(std::ranges::none_of(gem_lines, [&](const TextLine& line) { return line.text.starts_with(quote); }));
    std::printf("rune letters: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_rune_letters: %s\n", error.what());
    return 1;
}
