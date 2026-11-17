// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Larzuk's marker (FUN_00587840, InitFn 71): rules::larzuk_marker with scripted
// free-spot (FUN_00545340) and maker (FUN_005b2f20) calls, against the calls stock
// game.exe made on the same script (tools/emu/larzuk_init.py, frozen in
// larzuk_marker_cases.inc): the maker once, at radius 5, in the room the spot
// search hands out, and the record's made byte set when he's made.

#include <quests_act5.hpp>

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* const kCases[] = {
#include "larzuk_marker_cases.inc"
};

// One case line's script; the calls and the end d2d makes, in the oracle's words.
std::vector<std::string> run(const std::string& line) {
    std::istringstream fields(line);
    std::string word;
    int number = 0, made = 0, free_mode = 0, make_ok = 0;
    fields >> word >> number >> word >> made >> word >> free_mode >> word >> make_ok;
    std::vector<std::string> trace;
    bool record_made = made != 0;
    const auto free_spot = [&](int& room, int& at_x, int& at_y) {
        trace.push_back("free " + std::to_string(room) + " " + std::to_string(at_x) + " " + std::to_string(at_y));
        if (free_mode == 0) return false;
        at_x += 3; at_y += 1;
        if (free_mode == 2) room = 1;
        return true;
    };
    const auto make_at = [&](int room, int at_x, int at_y, int radius) {
        trace.push_back("make " + std::to_string(room) + " " + std::to_string(at_x) + " " + std::to_string(at_y) + " " + std::to_string(radius));
        return make_ok != 0;
    };
    const bool ok = d2d::rules::larzuk_marker(record_made, 0, 184, 104, free_spot, make_at);
    // The made byte comes from the record the port wrote, not from the callback's return: it is
    // the oracle's 0x3000000 only when the record ends made and the call said it made him, and
    // a record that changed without the call saying so (or the reverse) matches no oracle line.
    const bool was_made = made != 0;
    if (ok && record_made) trace.push_back("end 1 0x3000000");
    else if (!ok && record_made == was_made) trace.push_back("end none");
    else trace.push_back(std::string("end mismatch ok ") + (ok ? "1" : "0") + " record_made " + (record_made ? "1" : "0"));
    return trace;
}

}  // namespace

int main() try {
    int differ = 0, cases = 0;
    std::vector<std::string> want, got;
    const auto finish = [&] {
        if (got.empty()) return;
        ++cases;
        if (got != want) { ++differ; std::printf("case differs, game.exe made: %s ...\n", want.empty() ? "" : want.front().c_str()); }
        want.clear();
    };
    for (const char* text : kCases) {
        const std::string line = text;
        if (line.rfind("case ", 0) == 0) { finish(); got = run(line); }
        else want.push_back(line);
    }
    finish();
    std::printf("%d cases, %d differ\n", cases, differ);
    return cases == 24 && differ == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::printf("%s\n", error.what());
    return 1;
}
