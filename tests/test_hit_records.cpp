// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A unit's melee hit records (unit +0xac, SUnitDmg.cpp: FUN_0057ca80 adds,
// FUN_0057d690 finds, FUN_0057c9f0 drops a pair, FUN_0057c980 drops the
// owner's) against stock 1.14d's own, run by tools/emu/hit_records.py:
// d2d::rules::HitRecords on the same operations. No game data needed at
// runtime. With a file argument (hit_records.py's `game` output, or its `gen`
// cases) it prints each case line with d2d's answer instead, to diff.
#include <hit_records.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using d2d::rules::HitRecords;
using d2d::rules::UnitKey;

constexpr const char* kFixture[] = {
#include "hit_records_cases.inc"
};

constexpr int kUnits = 4;

auto split(const std::string& text, char separator) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    for (std::string part; std::getline(stream, part, separator);) parts.push_back(part);
    return parts;
}

// One case line (`id\top\top...`, maybe with game.exe's `|...` after it): the
// line with d2d's answer, as hit_records.py's `game` prints it. game.exe's null
// unit (-1) finds nothing and drops nothing (FUN_0057d690, FUN_0057c9f0 return
// at once); d2d's callers always hold both units, so the harness does that.
auto line_of(const std::string& text) -> std::string {
    const std::string case_text = text.substr(0, text.find('|'));
    const auto fields = split(case_text, '\t');
    if (fields.size() < 2) throw std::runtime_error("malformed case: " + text);
    std::array<UnitKey, kUnits> keys{};
    std::array<HitRecords<int>, kUnits> lists{};
    for (int index = 0; index < kUnits; ++index) keys[std::size_t(index)] = { index % 2, index };
    std::string found;
    const auto key_of = [&](int index) { return index < 0 ? UnitKey{} : keys[std::size_t(index)]; };
    for (std::size_t at = 1; at < fields.size(); ++at) {
        const auto parts = split(fields[at], ',');
        std::vector<int> args;
        for (std::size_t k = 1; k < parts.size(); ++k) args.push_back(int(std::stoll(parts[k])));
        const char kind = parts.at(0).at(0);
        if (kind == 't') {
            keys.at(std::size_t(args.at(0))) = { args.at(1), args.at(2) };
        } else if (kind == 'a') {
            lists.at(std::size_t(args.at(0))).add(key_of(args[0]), key_of(args.at(1)), args.at(2));
        } else if (kind == 'f') {
            const int* record = args.at(0) < 0 || args.at(1) < 0 ? nullptr : lists[std::size_t(args[0])].find(key_of(args[0]), key_of(args[1]));
            found += (found.empty() ? "" : " ") + (record ? std::to_string(*record) : std::string("-"));
        } else if (kind == 'd') {
            if (args.at(0) >= 0 && args.at(1) >= 0) lists[std::size_t(args[0])].drop(key_of(args[0]), key_of(args[1]));
        } else if (kind == 's') {
            lists.at(std::size_t(args.at(0))).drop_self(key_of(args[0]));
        } else {
            throw std::runtime_error("unknown op: " + fields[at]);
        }
    }
    std::string out = case_text + "|" + (found.empty() ? "-" : found) + "|";
    for (int index = 0; index < kUnits; ++index) {
        std::string nodes;
        for (const auto& node : lists[std::size_t(index)].nodes)
            nodes += (nodes.empty() ? "" : ",") + std::to_string(node.owner.type) + "." + std::to_string(node.owner.id) + "." + std::to_string(node.target.type) + "."
                     + std::to_string(node.target.id) + "." + std::to_string(node.record);
        out += (index ? " " : "") + (nodes.empty() ? std::string("-") : nodes);
    }
    return out;
}

// By hand: newest first, a stale owner, an object target refused.
void hand_checks() {
    HitRecords<int> list;
    const UnitKey player{ 0, 1 }, fallen{ 1, 5 }, chest{ 2, 9 };
    list.add(player, fallen, 10);
    list.add(player, fallen, 11);
    list.add(player, chest, 12);                         // an object: not kept
    assert(list.nodes.size() == 2 && *list.find(player, fallen) == 11);
    assert(list.find(fallen, player) == nullptr);
    list.add({ 0, 2 }, fallen, 13);                      // a node naming another owner
    list.drop_self(player);
    assert(list.nodes.size() == 1 && list.nodes[0].record == 13);
    list.drop({ 0, 2 }, fallen);
    assert(list.nodes.empty());
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream file(argv[1]);
        std::string text;
        while (std::getline(file, text)) {
            if (!text.empty() && text.back() == '\r') text.pop_back();
            if (!text.empty()) std::printf("%s\n", line_of(text).c_str());
        }
        return 0;
    }
    hand_checks();
    int matched = 0;
    for (const char* row : kFixture) {
        const std::string want = row;
        const std::string got = line_of(want);
        if (got != want) {
            std::fprintf(stderr, "game.exe: %s\nd2d:      %s\n", want.c_str(), got.c_str());
            return 1;
        }
        ++matched;
    }
    std::printf("hit records: %d of game.exe's cases match\n", matched);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hit_records: %s\n", error.what());
    return 1;
}
