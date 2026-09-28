// D2Decomp per-user directories and .cfg reader.
//
// Same layout as thirdeye's components/files (OpenMW lineage), so both
// engines put their files in the same places:
//   user dir    macOS    ~/Library/Preferences/<app>/
//               Linux    ~/.config/<app>/
//               Windows  Documents\My Games\<app>
//   global dir  macOS    /Library/Preferences/<app>/
//               Linux    /etc/<app>/
//               Windows  Program Files\<app>
// ponytail: a few free functions instead of thirdeye's FixedPath template
// + token mapping; port the ?user?/?global? tokens when a cfg needs them.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

#if defined(_WIN32)
#  include <shlobj.h>
#else
#  include <pwd.h>
#  include <unistd.h>
#endif

namespace d2d::userdir {

namespace fs = std::filesystem;

inline fs::path user_dir(std::string_view app) {
#if defined(_WIN32)
    fs::path base(".");
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &docs)))
        base = fs::path(docs) / "My Games";
    CoTaskMemFree(docs);
    return base / app;
#else
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        if (const passwd* home_entry = getpwuid(getuid())) home = home_entry->pw_dir;
    const fs::path base = home ? fs::path(home) : fs::path(".");
#  if defined(__APPLE__)
    return base / "Library" / "Preferences" / app;
#  else
    return base / ".config" / app;
#  endif
#endif
}

inline fs::path global_dir(std::string_view app) {
#if defined(_WIN32)
    const char* program_files = std::getenv("ProgramFiles");
    return fs::path(program_files ? program_files : ".") / app;
#elif defined(__APPLE__)
    return fs::path("/Library/Preferences") / app;
#else
    return fs::path("/etc") / app;
#endif
}

using Config = std::unordered_map<std::string, std::string>;

// Merge `key = value` lines from `file` into `cfg` (later loads win).
// Blank lines, '#' comments and lines without '=' are skipped; keys and
// values are whitespace-trimmed. A missing file is not an error.
inline void load_cfg(const fs::path& file, Config& cfg) {
    std::ifstream input(file);
    constexpr std::string_view whitespace = " \t\r\n";
    auto trim = [&](std::string_view text) {
        const auto first = text.find_first_not_of(whitespace);
        if (first == text.npos) return std::string_view{};
        return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
    };
    for (std::string line; std::getline(input, line);) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;
        const auto equals_at = trimmed.find('=');
        if (equals_at == trimmed.npos) continue;
        const auto key = trim(trimmed.substr(0, equals_at));
        if (!key.empty()) cfg[std::string(key)] = std::string(trim(trimmed.substr(equals_at + 1)));
    }
}

}  // namespace d2d::userdir
