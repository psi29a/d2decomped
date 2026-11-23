// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace d2d::client {

// FUN_00438f70 accepts a `*.d2s` result only when the part before its first
// dot is 2..15 bytes, does not start with `_`, and does not end with `-`.
inline bool retail_save_filename(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    const auto dot = filename.find('.');
    if (dot == std::string::npos) return false;
    const std::string_view stem(filename.data(), dot);
    if (stem.size() < 2 || stem.size() > 15) return false;
    if (stem.front() == '_' || stem.back() == '-') return false;
    std::string extension = filename.substr(dot);
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return char(std::tolower(character));
    });
    return extension == ".d2s";
}

// FUN_00438ad0 inserts expansion entries by the unsigned 64-bit FILETIME,
// newest first. Equal times retain the save-folder enumeration order.
template <typename Entries>
void retail_sort_saves(Entries& entries) {
    std::ranges::stable_sort(entries, [](const auto& first, const auto& second) {
        return first.write_time > second.write_time;
    });
}

}  // namespace d2d::client
