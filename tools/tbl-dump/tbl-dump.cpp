// SPDX-License-Identifier: GPL-3.0-or-later
// tbl-dump < string.tbl — print d2d's key/value entries as hexadecimal text.
#include <tbl.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

// Print the table's sorted keys and UTF-16 values as hexadecimal text.
int main() try {
    const std::vector<char> input(std::istreambuf_iterator<char>(std::cin), {});
    std::vector<std::byte> bytes;
    bytes.reserve(input.size());
    for (const char value : input) bytes.push_back(std::byte(value));
    const d2d::tbl::Table table(bytes);
    std::vector<std::string> keys;
    keys.reserve(table.size());
    for (const auto& [key, value] : table.entries()) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        for (const unsigned char value : key) std::printf("%02x", value);
        std::printf("=");
        const auto text = table.get(key);
        for (const char16_t value : *text) std::printf("%04x", unsigned(value));
        std::printf("\n");
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
