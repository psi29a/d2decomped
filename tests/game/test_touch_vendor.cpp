// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-includes: apps/d2d
// test-sources: apps/d2d/input/touch/trade.cpp apps/d2d/panels/store_selection.cpp
// Native input/identity regression; no claim of original touch UI parity.
#include "input/touch/input.hpp"
#include "panels/store_selection.hpp"

#include <d2s_items.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <vendor.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::client;
    using namespace d2d::rules;
    Store store;
    store.npc = 2; store.npc_id = "Akara";
    store.tabs[0] = { { .code = "hp1", .id = 101 }, { .code = "tsc", .id = 102 } };
    const auto potion = StoreSelection::capture(store, 0);
    const auto scroll = StoreSelection::capture(store, 1);
    assert(potion.id == 101 && scroll.id == 102);
    Tables tables;
    tables.item_info["hp1"] = {}; tables.item_info["tsc"] = {};
    tables.item_base["hp1"].cost = 30; tables.item_base["tsc"].cost = 150;
    d2d::d2s::Stats stats;
    stats.values[d2d::d2s::kGold] = 500;
    std::vector<d2d::d2s::Item> items;
    assert(store_buy(tables, store, potion.index(store), items, stats) == Trade::done);
    assert(items.size() == 1 && items[0].code == "hp1" && stats.get(d2d::d2s::kGold) == 470);
    assert(potion.index(store) == -1 && scroll.index(store) == 0);
    const auto encoded = d2d::game::encode(d2d::game::cmd::Buy{ scroll.id });
    const auto decoded = d2d::game::decode(encoded);
    assert(decoded && std::get<d2d::game::cmd::Buy>(*decoded).item == scroll.id);
    store.npc = 3; assert(scroll.index(store) == -1); store.npc = 2;
    store.tab = 1; assert(scroll.index(store) == -1); store.tab = 0;
    store.gamble = true; assert(scroll.index(store) == -1); store.gamble = false;
    assert(StoreSelection::capture(store, -1).index(store) == -1);
    assert(StoreSelection::capture(store, 100).index(store) == -1);
    for (const bool buying : { false, true }) {
        auto sheet = touch::make_item_sheet(false, !buying, buying);
        assert(!sheet.show_confirm());
        const auto box = buying ? sheet.buy : sheet.sell;
        const float point_x = box.x + 10.f, point_y = box.y + 10.f;
        touch::ItemAction action;
        using touch::Phase;
        assert(sheet.trade_event(1, Phase::Down, point_x, point_y, action));
        assert(action == touch::ItemAction::None);
        assert(sheet.trade_event(2, Phase::Up, point_x, point_y, action));
        assert(action == touch::ItemAction::None && sheet.trade_finger == 1);
        assert(sheet.trade_event(1, Phase::Up, point_x, point_y, action));
        assert(action == (buying ? touch::ItemAction::Buy : touch::ItemAction::Sell));
        assert(!sheet.trade_event(1, Phase::Up, point_x, point_y, action));
        assert(sheet.trade_event(-2, Phase::Down, point_x, point_y, action));
        assert(sheet.trade_event(-2, Phase::Move, 0, 0, action));
        assert(sheet.trade_event(-2, Phase::Up, point_x, point_y, action));
        assert(action == touch::ItemAction::None);
        assert(sheet.trade_event(1, Phase::Down, point_x, point_y, action));
        assert(sheet.trade_event(1, Phase::Cancel, point_x, point_y, action));
        assert(action == touch::ItemAction::None);
    }
    assert(touch::make_item_sheet(true, false, false).show_confirm());
    assert(touch::make_confirm_sheet().show_confirm());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
