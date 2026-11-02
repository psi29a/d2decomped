// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Runewords (FUN_0062bed0, FUN_006600a0) with the game's MPQs (D2_MPQ_DIR;
// the 1.14d ones with D2_PATCH_INSTALLER): the runes in order in a base its
// row takes, every socket filled; not in a magic item, nor a ladder row.

#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const auto& tables = data->rules;

    auto socketed = [](const std::string& code, std::initializer_list<const char*> runes) {
        d2d::d2s::Item item{ .code = code };
        item.quality = 2; item.identified = true; item.socketed = true; item.uid = 0x1234567;
        item.sockets = int(runes.size());
        for (const char* rune : runes) {
            d2d::d2s::Item filler{ .code = rune };
            filler.location = d2d::d2s::item_location::kSocketed;
            item.socketed_items.push_back(std::move(filler));
        }
        return item;
    };

    // Steel (Runeword134): Tir El in a sword, axe or mace.
    auto steel = socketed("lsd", { "r03", "r01" });
    const auto* row = d2d::rules::runeword_row(tables, steel);
    assert(row && row->name == "Runeword134");
    assert(d2d::rules::make_runeword(tables, steel) && steel.runeword && steel.main_props == 0 && !steel.props.empty());
    const auto has = [&](int stat, int value) { return std::ranges::any_of(steel.props, [&](const d2d::d2s::ItemProp& prop) { return prop.stat == stat && prop.value == value; }); };
    assert(has(d2d::d2s::kFasterAttackRate, 25) && has(d2d::d2s::kOpenWounds, 50) && has(d2d::d2s::kMinDamagePercent, 20));   // swing2, openwounds, dmg%
    assert(data->item_names.runeword[std::size_t(steel.runeword_id - 27)] == "Runeword134");   // item_text's rank + 27
    assert(!d2d::rules::make_runeword(tables, steel));                                       // one already
    std::printf("OK: Tir El in a long sword is Steel, its mods its runeword list\n");

    assert(!d2d::rules::runeword_row(tables, socketed("lsd", { "r01", "r03" })));             // out of order
    auto magic = socketed("lsd", { "r03", "r01" });
    magic.quality = 4;
    assert(!d2d::rules::runeword_row(tables, magic));                                         // magic .. tempered
    auto open = socketed("lsd", { "r03", "r01" });
    open.sockets = 3;
    assert(!d2d::rules::runeword_row(tables, open));                                          // a socket free
    assert(!d2d::rules::runeword_row(tables, socketed("buc", { "r03", "r01" })));             // a shield isn't Steel's
    assert(!d2d::rules::runeword_row(tables, socketed("lsd", { "r07", "r10", "r09", "r11" })));   // Spirit: a ladder row
    std::printf("OK: order, quality, a free socket, the base's type and ladder rows refuse\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
