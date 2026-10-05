// SPDX-License-Identifier: GPL-3.0-or-later
// Logging, after ../opendf's DF::Log: each line, stamped with the UTC time
// (ISO 8601, ms), goes to stdout (warnings and errors to stderr) and to
// the log file once one is open. Init sections read like ../thirdeye's
// ("Initializing X...", details indented under it). Five levels, error
// .. trace; lines below the level (info by default) are dropped.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace d2d::log {

enum class Level { error, warning, info, debug, trace };

inline const auto g_start = std::chrono::steady_clock::now();
inline std::ofstream g_file;
inline std::mutex g_mutex;   // the music worker logs too
inline std::atomic<Level> g_level = Level::info;

// ms since launch — also for "done (N ms)" timings.
inline long long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_start).count();
}

inline void set_level(Level level) { g_level = level; }
inline bool enabled(Level level) { return level <= g_level.load(); }
// "error", "warning" (or "warn"), "info", "debug", "trace".
inline std::optional<Level> parse_level(std::string_view name) {
    if (name == "error") return Level::error;
    if (name == "warning" || name == "warn") return Level::warning;
    if (name == "info") return Level::info;
    if (name == "debug") return Level::debug;
    if (name == "trace") return Level::trace;
    return std::nullopt;
}

inline void open(const std::filesystem::path& path) {
    std::lock_guard lock(g_mutex);
    g_file.open(path, std::ios::trunc);
    if (!g_file) std::fprintf(stderr, "Failed to open log file %s\n", path.string().c_str());
}

inline void write(Level level, const char* tag, const std::string& msg) {
    if (!enabled(level)) return;
    std::lock_guard lock(g_mutex);
    const auto line = std::format("{:%FT%TZ} {}{}\n", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()), tag, msg);
    std::FILE* out = level <= Level::warning ? stderr : stdout;
    std::fputs(line.c_str(), out);
    std::fflush(out);
    if (g_file) g_file << line << std::flush;
}

template <class... A> void error(std::format_string<A...> format, A&&... args) {
    write(Level::error, "error: ", std::format(format, std::forward<A>(args)...));
}
template <class... A> void warn(std::format_string<A...> format, A&&... args) {
    write(Level::warning, "warning: ", std::format(format, std::forward<A>(args)...));
}
template <class... A> void info(std::format_string<A...> format, A&&... args) {
    write(Level::info, "", std::format(format, std::forward<A>(args)...));
}
template <class... A> void debug(std::format_string<A...> format, A&&... args) {
    if (enabled(Level::debug)) write(Level::debug, "debug: ", std::format(format, std::forward<A>(args)...));
}
template <class... A> void trace(std::format_string<A...> format, A&&... args) {
    if (enabled(Level::trace)) write(Level::trace, "trace: ", std::format(format, std::forward<A>(args)...));
}

}  // namespace d2d::log
