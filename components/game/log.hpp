// SPDX-License-Identifier: GPL-3.0-or-later
// Logging, after ../opendf's DF::Log: each line, stamped with the UTC time
// (ISO 8601, ms), goes to stdout (warnings and errors to stderr) and to
// <user dir>/d2d.log. Init sections read like ../thirdeye's
// ("Initializing X...", details indented under it).
#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <mutex>
#include <ostream>
#include <string>

namespace d2d::log {

inline const auto g_start = std::chrono::steady_clock::now();
inline std::ofstream g_file;
inline std::mutex g_mutex;   // the music worker logs too

// ms since launch — also for "done (N ms)" timings.
inline long long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_start).count();
}

inline void open(const std::filesystem::path& path) {
    std::lock_guard lock(g_mutex);
    g_file.open(path, std::ios::trunc);
    if (!g_file) std::fprintf(stderr, "Failed to open log file %s\n", path.string().c_str());
}

inline void write(std::FILE* out, const char* level, const std::string& msg) {
    std::lock_guard lock(g_mutex);
    const auto line = std::format("{:%FT%TZ} {}{}\n", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()), level, msg);
    std::fputs(line.c_str(), out);
    std::fflush(out);
    if (g_file) g_file << line << std::flush;
}

template <class... A> void info(std::format_string<A...> format, A&&... args) {
    write(stdout, "", std::format(format, std::forward<A>(args)...));
}
template <class... A> void warn(std::format_string<A...> format, A&&... args) {
    write(stderr, "warning: ", std::format(format, std::forward<A>(args)...));
}
template <class... A> void error(std::format_string<A...> format, A&&... args) {
    write(stderr, "error: ", std::format(format, std::forward<A>(args)...));
}

}  // namespace d2d::log
