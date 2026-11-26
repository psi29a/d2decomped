// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace d2d::rules { struct Store; }
namespace d2d::client {
struct StoreSelection {
    int npc = -1, tab = -1, id = -1;
    bool gamble = false;
    [[nodiscard]] static StoreSelection capture(const d2d::rules::Store& store, int index);
    [[nodiscard]] int index(const d2d::rules::Store& store) const;
};
} // namespace d2d::client
