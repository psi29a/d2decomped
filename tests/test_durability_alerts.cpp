// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The worn items' durability alerts (FUN_00497fe0, FUN_00497cf0) against
// stock 1.14d's own, run by tools/emu/durability_alerts.py: 2000 cases of
// seven slots, automap and hostility, each icon's frame and spot. Then
// durability_slot's facts from hand-made tables. No game data needed.
// With a file argument (durability_alerts.py's `gen` cases or `game`
// output) it prints each case line with d2d's draws instead, to diff.
#include <d2s_items.hpp>
#include <durability_alert.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using d2d::rules::DurabilitySlot;

struct SlotRow {
    int item, has_durability, stackable, shield, quiver, durability, most;
};

DurabilitySlot slot_of(const SlotRow& row) {
    return { row.item != 0, row.has_durability != 0, row.stackable != 0, row.shield != 0, row.quiver, row.durability, row.most };
}

int diff_file(const char* path) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (const auto bar = line.find('|'); bar != std::string::npos) line.resize(bar);
        while (!line.empty() && line.back() == ' ') line.pop_back();
        std::istringstream fields(line);
        int width = 0, automap = 0, hostile = 0;
        fields >> width >> automap >> hostile;
        std::array<DurabilitySlot, 7> slots{};
        for (auto& slot : slots) {
            SlotRow row{};
            fields >> row.item >> row.has_durability >> row.stackable >> row.shield >> row.quiver >> row.durability >> row.most;
            slot = slot_of(row);
        }
        const auto icons = d2d::rules::durability_icons(slots, width, automap != 0, hostile != 0);
        std::printf("%s | %zu", line.c_str(), icons.size());
        for (const auto& icon : icons) std::printf(" %d %d %d", icon.frame, icon.x, icon.bottom);
        std::printf("\n");
    }
    return 0;
}

struct Case {
    int width, automap, hostile;
    std::array<SlotRow, 7> slots;
    int count;
    std::array<std::array<int, 3>, 7> icons;
};

const Case kCases[] = {
#include "durability_alerts_cases.inc"
};

void check_oracle() {
    for (std::size_t index = 0; index < std::size(kCases); ++index) {
        const auto& row = kCases[index];
        std::array<DurabilitySlot, 7> slots{};
        for (std::size_t slot = 0; slot < slots.size(); ++slot) slots[slot] = slot_of(row.slots[slot]);
        const auto icons = d2d::rules::durability_icons(slots, row.width, row.automap != 0, row.hostile != 0);
        bool same = int(icons.size()) == row.count;
        for (std::size_t icon = 0; same && icon < icons.size(); ++icon)
            same = icons[icon].frame == row.icons[icon][0] && icons[icon].x == row.icons[icon][1] && icons[icon].bottom == row.icons[icon][2];
        if (!same) std::fprintf(stderr, "FAIL: case %zu: d2d draws %zu icons, game.exe %d\n", index, icons.size(), row.count);
        assert(same);
    }
}

// durability_slot: the leaves' answers off the tables (an empty slot, a
// shield by its type's Equiv, a stack, a quiver, an indestructible item).
void check_slot() {
    d2d::rules::Tables tables;
    tables.types["shld"] = {};
    tables.types["shie"].equiv = { "shld", "" };
    tables.types["bowq"].quiver = "bow";
    tables.types["tkni"] = {};
    tables.item_info["buc"].type = "shie";
    tables.item_info["aqv"].type = "bowq";
    tables.item_info["tkf"].type = "tkni";
    tables.item_base["buc"].durability = 12;
    tables.item_base["aqv"].stackable = true;
    tables.item_base["tkf"].durability = 6;
    tables.item_base["tkf"].stackable = true;

    assert(!d2d::rules::durability_slot(tables, nullptr).item);

    d2d::d2s::Item buckler;
    buckler.code = "buc";
    buckler.durability = 2;
    buckler.max_durability = 12;
    auto slot = d2d::rules::durability_slot(tables, &buckler);
    assert(slot.item && slot.has_durability && !slot.stackable && slot.shield && slot.quiver == 0);
    assert(slot.durability == 2 && slot.max_durability == 12);

    buckler.props.push_back({ d2d::d2s::kIndestructible, 0, 1 });
    assert(!d2d::rules::durability_slot(tables, &buckler).has_durability);

    d2d::d2s::Item quiver;
    quiver.code = "aqv";
    slot = d2d::rules::durability_slot(tables, &quiver);
    assert(slot.item && !slot.has_durability && slot.stackable && slot.quiver != 0 && !slot.shield);

    d2d::d2s::Item knives;
    knives.code = "tkf";
    knives.durability = 1;
    knives.max_durability = 6;
    slot = d2d::rules::durability_slot(tables, &knives);
    assert(slot.has_durability && slot.stackable);
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) return diff_file(argv[1]);
    check_oracle();
    check_slot();
    std::printf("OK: %zu alert cases as game.exe's, durability_slot\n", std::size(kCases));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_durability_alerts: %s\n", error.what());
    return 1;
}
