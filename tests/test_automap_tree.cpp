// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The automap's cell tree (rules/automap_cells.hpp) vs game.exe's own
// FUN_00457b00 (tools/emu/automap_tree.py): 600 random insert runs over four
// lists, each list's in-order walk (FUN_00458470) compared, so the cells
// dropped at one (x, y) match where only the search path is met.
#include <automap_cells.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iterator>
#include <sstream>
#include <string>

namespace {

struct Run { const char* inserts; const char* walks; };
constexpr Run kOracle[] = {
#include "automap_tree_cases.inc"
};

// The walks a tree set gives, in the oracle's `cel,x,y;...|` text.
std::string walks_of(const std::array<d2d::rules::AutomapTree, 4>& trees) {
    std::string out;
    for (std::size_t list = 0; list < trees.size(); ++list) {
        if (list) out += '|';
        bool first = true;
        for (const auto& node : trees[list].in_order()) {
            if (!first) out += ';';
            first = false;
            out += std::to_string(node.cel) + ',' + std::to_string(node.cell_x) + ',' + std::to_string(node.cell_y);
        }
    }
    return out;
}

}  // namespace

int main() try {
    using namespace d2d::rules;
    for (const auto& run : kOracle) {
        std::array<AutomapTree, 4> trees;
        std::istringstream inserts(run.inserts);
        std::string item;
        while (std::getline(inserts, item, ';')) {
            int list = 0, cel = 0, cell_x = 0, cell_y = 0;
            const int fields = std::sscanf(item.c_str(), "%d:%d,%d,%d", &list, &cel, &cell_x, &cell_y);
            assert(fields == 4);
            (void)trees[std::size_t(list)].insert(cel, cell_x, cell_y, automap_cel_group(cel));
        }
        const auto got = walks_of(trees);
        if (got != run.walks) std::printf("inserts %s\ngot  %s\nwant %s\n", run.inserts, got.c_str(), run.walks);
        assert(got == run.walks);
    }
    std::printf("automap tree: %zu runs match game.exe\n", std::size(kOracle));
    // Cleared, a cell the tree dropped before goes in again.
    AutomapTree tree;
    assert(tree.insert(120, 0, 0, automap_cel_group(120)) && !tree.insert(169, 0, 0, automap_cel_group(169)));
    tree.clear();
    assert(tree.insert(169, 0, 0, automap_cel_group(169)) && tree.size() == 1);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
