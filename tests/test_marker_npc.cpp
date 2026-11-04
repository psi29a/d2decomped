// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A marker's town NPC (FUN_005459a0, Nihlathak's InitFn 68): rules::marker_npc
// with scripted free-spot (FUN_00545340), room-near (FUN_00463740) and maker
// (FUN_005b2f20) calls, against the calls stock game.exe made on the same
// script (tools/emu/nihlathak_tries.py, frozen in marker_npc_cases.inc).
// `--marker-npc FILE` diffs a whole run of the oracle instead.

#include <quests_act5.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

const char* const kCases[] = {
#include "marker_npc_cases.inc"
};

// One case line's script; the calls d2d makes, in the oracle's words.
std::vector<std::string> run(const std::string& line) {
    std::istringstream in(line);
    std::string word;
    int number = 0, room_x = 0, room_y = 0, width = 0, height = 0, marker_x = 0, marker_y = 0, delta_x = 0, delta_y = 0, make_ok = 0;
    std::string free_text, near_text;
    in >> word >> number >> word >> room_x >> room_y >> width >> height >> word >> marker_x >> marker_y >> word >> free_text >> delta_x >> delta_y >> word >> near_text >> word >> make_ok;
    const auto free_mask = std::stoull(free_text, nullptr, 16), near_mask = std::stoull(near_text, nullptr, 16);
    std::vector<std::string> trace;
    int free_calls = 0, near_calls = 0, make_calls = 0, made = -1;
    const auto free_spot = [&](int& room, int& at_x, int& at_y) {
        trace.push_back("free " + std::to_string(room) + " " + std::to_string(at_x) + " " + std::to_string(at_y));
        if (free_mask >> free_calls++ & 1) return false;
        at_x += delta_x; at_y += delta_y;
        return true;
    };
    const auto near_room = [&](int room, int at_x, int at_y) -> std::optional<int> {
        trace.push_back("near " + std::to_string(room) + " " + std::to_string(at_x) + " " + std::to_string(at_y));
        const int call = near_calls++;
        if (near_mask >> call & 1) return std::nullopt;
        return call + 1;
    };
    const auto make_at = [&](int room, int at_x, int at_y, int retry) {
        trace.push_back("make " + std::to_string(room) + " " + std::to_string(at_x) + " " + std::to_string(at_y) + " " + std::to_string(retry));
        const int call = make_calls++;
        if (call == make_ok) made = call;
        return call == make_ok;
    };
    const bool ok = d2d::rules::marker_npc(0, { room_x, room_y, width, height }, marker_x, marker_y, free_spot, near_room, make_at);
    trace.push_back(ok ? "made " + std::to_string(made) + " 0x3000000" : "none");
    return trace;
}

// Lines of `case ...` followed by game.exe's calls; the number of mismatches.
int diff(const std::vector<std::string>& lines) {
    int bad = 0, cases = 0;
    for (std::size_t at = 0; at < lines.size();) {
        if (lines[at].rfind("case ", 0) != 0) { ++at; continue; }
        const auto ours = run(lines[at]);
        ++cases;
        for (std::size_t k = 0; k < ours.size(); ++k)
            if (at + 1 + k >= lines.size() || lines[at + 1 + k] != ours[k]) {
                if (bad++ < 5) std::fprintf(stderr, "%s\n  line %zu: game.exe '%s', d2d '%s'\n", lines[at].c_str(), k, at + 1 + k < lines.size() ? lines[at + 1 + k].c_str() : "", ours[k].c_str());
                break;
            }
        at += 1 + ours.size();
    }
    std::printf("%d cases, %d differ\n", cases, bad);
    return bad;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> lines;
    if (argc == 3 && std::string_view(argv[1]) == "--marker-npc") {
        std::ifstream file(argv[2]);
        for (std::string line; std::getline(file, line);) lines.push_back(line);
    } else {
        for (const char* line : kCases) lines.emplace_back(line);
    }
    return diff(lines) == 0 && !lines.empty() ? 0 : 1;
}
