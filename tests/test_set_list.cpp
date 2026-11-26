// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A set's bonus list against stock 1.14d's own, run by tools/emu/set_list.py
// (FUN_00663b40 -> FUN_00660120): on each set piece going on, the piece its
// rows get (rules::stats_on, rules::set_list_piece) and the rows
// (rules::set_list_rows, with rules::set_pieces_worn). No game data needed
// at runtime. With a file argument (set_list.py's `game` output, or its
// `gen` cases) it prints each line with d2d's answer instead, to diff.
#include <rules.hpp>
#include <set_list.hpp>

#include <d2s_items.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace d2d::rules;

// One run's sets and the owner's items, fed the case lines in order.
class Replay {
public:
    // An S or E line (set_list.py's case fields, any answer after them
    // ignored): an E line's answer as `piece <id> rows <rows>`, an S line's empty.
    std::string feed(const std::string& line) {
        std::istringstream fields(line);
        std::string kind;
        int run = 0;
        fields >> kind >> run;
        if (run != run_) {                     // a new run: the owner's lists start empty
            run_ = run;
            tables_ = Tables{};
            items_.clear();
        }
        if (kind == "S") {
            int set = 0, pieces = 0, partial = 0, full = 0;
            fields >> set >> pieces >> partial >> full;
            if (std::size_t(set) >= tables_.set_bonuses.size()) tables_.set_bonuses.resize(std::size_t(set) + 1);
            auto& bonus = tables_.set_bonuses[std::size_t(set)];
            bonus.pieces = pieces;
            bonus.partial.assign(8, Mod{});
            bonus.full.assign(8, Mod{});
            for (int index = 0; index < 8; ++index) {
                if (partial >> index & 1) bonus.partial[std::size_t(index)].code = "x";
                if (full >> index & 1) bonus.full[std::size_t(index)].code = "x";
            }
            return {};
        }
        int item_id = 0, set = 0, member = 0, going_on = 0;
        fields >> item_id >> set >> member >> going_on;
        auto found = std::ranges::find(items_, item_id, &d2d::d2s::Item::id);
        if (found == items_.end()) {
            Special row;
            row.set = set;
            row.member = member;
            tables_.sets.push_back(row);
            d2d::d2s::Item item;
            item.id = item_id;
            item.quality = 5;
            item.set_id = int(tables_.sets.size()) - 1;
            items_.push_back(item);
            found = items_.end() - 1;
        }
        std::string answer = "piece -1 rows -";
        if (going_on == 0) {
            found->location = d2d::d2s::item_location::kStored;
            return answer;
        }
        found->location = d2d::d2s::item_location::kEquipped;
        found->slot = d2d::d2s::body_location::kFirst;
        stats_on(items_, *found);
        const auto rows = set_list_rows(tables_.set_bonuses[std::size_t(set)], set_pieces_worn(tables_, items_, set));
        const auto* piece = set_list_piece(tables_, items_, set);
        if (rows.empty() || !piece) return answer;
        answer = "piece " + std::to_string(piece->id) + " rows";
        for (const int row : rows) answer += " " + std::to_string(row);
        return answer;
    }

private:
    int run_ = -1;
    Tables tables_;
    std::vector<d2d::d2s::Item> items_;
};

// The case fields of a line: S has 6, E has 7.
std::string case_fields(const std::string& line) {
    std::istringstream fields(line);
    std::string kind, word, out;
    fields >> kind;
    out = kind;
    for (int count = kind == "S" ? 5 : 6; count > 0 && fields >> word; --count) out += " " + word;
    return out;
}

}  // namespace

int main(int argc, char** argv) try {
    Replay replay;
    if (argc > 1) {
        std::ifstream input(argv[1]);
        if (!input) {
            std::fprintf(stderr, "test_set_list: cannot open %s\n", argv[1]);
            return 1;
        }
        std::string line;
        while (std::getline(input, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            const auto fields = case_fields(line);
            const auto answer = replay.feed(fields);
            std::printf("%s%s%s\n", fields.c_str(), answer.empty() ? "" : " ", answer.c_str());
        }
        return 0;
    }

    const char* const lines[] = {
#include "set_list_cases.inc"
    };
    std::size_t equips = 0;
    for (const char* text : lines) {
        const std::string line = text;
        const auto fields = case_fields(line);
        const auto answer = replay.feed(fields);
        if (answer.empty()) continue;
        const auto want = line.substr(fields.size() + 1);
        if (answer != want) std::fprintf(stderr, "FAIL: %s: got %s\n", line.c_str(), answer.c_str());
        assert(answer == want);
        ++equips;
    }
    std::printf("OK: %zu set pieces on and off, their set's list as game.exe's\n", equips);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_set_list: %s\n", error.what());
    return 1;
}
