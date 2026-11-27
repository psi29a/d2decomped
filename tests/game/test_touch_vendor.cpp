// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-includes: apps/d2d
// test-sources: apps/d2d/input/touch/trade.cpp apps/d2d/panels/store_selection.cpp
// Native input/identity regression; no claim of original touch UI parity.
#include "input/touch/input.hpp"
#include "input/touch/trade.hpp"
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
    const touch::Box inventory{ 500, 200, 290, 116 }, vendor{ 96, 123, 290, 290 };
    assert(touch::bag_drop(inventory, vendor, 610, 260) == touch::BagDrop::Inventory);
    assert(touch::bag_drop(inventory, vendor, 110, 137) == touch::BagDrop::Sell);
    assert(touch::bag_drop(inventory, vendor, 450, 450) == touch::BagDrop::None);
    assert(touch::bag_drop(inventory, {}, 110, 137) == touch::BagDrop::None);
    assert(touch::bag_drop(inventory, {}, 610, 260) == touch::BagDrop::Inventory);
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
    const int potion_at = potion.index(store);
    assert(potion_at >= 0);
    assert(store_buy(tables, store, potion_at, items, stats) == Trade::done);
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
    auto mixed = touch::make_item_sheet(true, true, false);
    assert(mixed.show_use && mixed.show_sell && mixed.show_confirm());
    assert(mixed.hit(mixed.use.x + 10.f, mixed.use.y + 10.f) == touch::ItemAction::Use);
    assert(mixed.hit(mixed.confirm.x + 10.f, mixed.confirm.y + 10.f) == touch::ItemAction::Confirm);
    touch::ItemAction action;
    assert(!mixed.trade_event(1, touch::Phase::Down, mixed.use.x + 10.f, mixed.use.y + 10.f, action));
    mixed.chosen = touch::ItemAction::Use; mixed.confirming = true;
    assert(mixed.trade_event(2, touch::Phase::Down, mixed.sell.x + 10.f, mixed.sell.y + 10.f, action));
    assert(action == touch::ItemAction::None);
    assert(mixed.trade_event(2, touch::Phase::Up, mixed.sell.x + 10.f, mixed.sell.y + 10.f, action));
    assert(action == touch::ItemAction::Sell && mixed.chosen == touch::ItemAction::Use);
    assert(touch::make_item_sheet(true, false, false).show_confirm());
    assert(touch::make_confirm_sheet().show_confirm());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
