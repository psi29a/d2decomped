// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// FUN_00438f70's filename filter and FUN_00438ad0's FILETIME ordering.
#include <frontend/save_list.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

int main() try {
    using d2d::client::retail_save_filename;
    using d2d::client::retail_sort_saves;
    assert(retail_save_filename("Ab.d2s"));
    assert(retail_save_filename("FIFTEEN12345678.D2S"));
    assert(!retail_save_filename("A.d2s"));
    assert(!retail_save_filename("Sixteen123456789.d2s"));
    assert(!retail_save_filename("_Hero.d2s"));
    assert(!retail_save_filename("Hero-.d2s"));
    assert(!retail_save_filename("Hero.sav"));
    assert(!retail_save_filename("Hero.part.d2s"));

    struct Entry { std::string name; std::filesystem::file_time_type write_time; };
    const auto base = std::filesystem::file_time_type{};
    std::vector<Entry> entries = {
        { "old", base + std::chrono::seconds(1) },
        { "equal-first", base + std::chrono::seconds(3) },
        { "new", base + std::chrono::seconds(5) },
        { "equal-second", base + std::chrono::seconds(3) },
    };
    retail_sort_saves(entries);
    assert(entries[0].name == "new" && entries[1].name == "equal-first");
    assert(entries[2].name == "equal-second" && entries[3].name == "old");
    std::puts("save list: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_save_list: %s\n", error.what());
    return 1;
}
