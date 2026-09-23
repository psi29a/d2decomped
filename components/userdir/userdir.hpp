// D2Decomp per-user directories and .cfg reader.
//
// Same layout as thirdeye's components/files (OpenMW lineage), so both
// engines put their files in the same places:
//   user dir    macOS    ~/Library/Preferences/<app>/
//               Linux    ~/.config/<app>/
//               Windows  Documents\My Games\<app>\
//   global dir  macOS    /Library/Preferences/<app>/
//               Linux    /etc/<app>/
//               Windows  Program Files\<app>\
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
        if (const passwd* pw = getpwuid(getuid())) home = pw->pw_dir;
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
    const char* pf = std::getenv("ProgramFiles");
    return fs::path(pf ? pf : ".") / app;
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
    std::ifstream in(file);
    constexpr std::string_view ws = " \t\r\n";
    auto trim = [&](std::string_view s) {
        const auto b = s.find_first_not_of(ws);
        if (b == s.npos) return std::string_view{};
        return s.substr(b, s.find_last_not_of(ws) - b + 1);
    };
    for (std::string line; std::getline(in, line);) {
        const auto t = trim(line);
        if (t.empty() || t.front() == '#') continue;
        const auto eq = t.find('=');
        if (eq == t.npos) continue;
        const auto key = trim(t.substr(0, eq));
        if (!key.empty()) cfg[std::string(key)] = std::string(trim(t.substr(eq + 1)));
    }
}

}  // namespace d2d::userdir
