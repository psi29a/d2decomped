// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-includes: apps/d2d
#include "skills/portal.hpp"

#include <d2s_items.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    std::vector<d2d::d2s::Item> items(2);
    items[0].code = "tbk"; items[0].id = 10; items[0].location = d2d::d2s::item_location::kStored; items[0].panel = 1;
    items[1].code = "tsc"; items[1].id = 11; items[1].location = d2d::d2s::item_location::kStored; items[1].panel = 1;
    assert(d2d::client::skillpad::portal_item_id(items) == 11);
    assert(d2d::client::skillpad::portal_item_id(items, false) == 10);
    items[0].quantity = 1;
    assert(d2d::client::skillpad::portal_item_id(items) == 10);
    items[0].panel = 5;
    assert(d2d::client::skillpad::portal_item_id(items) == 11);
    items[1].location = d2d::d2s::item_location::kBelt;
    assert(d2d::client::skillpad::portal_item_id(items) == -1);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
