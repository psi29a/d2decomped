// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for log.hpp and log_file.hpp: the clock, the file, the
// terminal and the lock the threads that log share.
#include "log.hpp"
#include "log_file.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace d2d::log {

namespace {

const auto g_start = std::chrono::steady_clock::now();
std::ofstream g_file;
std::mutex g_mutex;   // the music worker logs too
std::atomic<Level> g_level = Level::info;
std::atomic<Sink> g_sink = nullptr;
std::atomic<bool> g_windows_vt = false;

bool is_terminal(std::FILE* out) {
#ifdef _WIN32
    return g_windows_vt && _isatty(_fileno(out)) != 0;
#else
    return isatty(fileno(out)) != 0;
#endif
}

}  // namespace

long long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_start).count();
}

void set_level(Level level) { g_level = level; }
void set_sink(Sink sink) { g_sink = sink; }
bool enabled(Level level) { return level <= g_level.load(); }
void set_windows_vt(bool on) { g_windows_vt = on; }

void open(const std::filesystem::path& path, bool append) {
    std::lock_guard lock(g_mutex);
    g_file.open(path, append ? std::ios::app : std::ios::trunc);
    if (!g_file) std::fprintf(stderr, "Failed to open log file %s\n", path.string().c_str());
}

void write(Level level, std::string_view tag, const std::string& msg) {
    if (!enabled(level)) return;
    const auto stamp = std::format("{:%FT%TZ} ", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
    const auto line = stamp + (tag.empty() ? "" : std::string(tag) + " ") + msg + "\n";
    {
        std::lock_guard lock(g_mutex);
        std::FILE* out = level <= Level::warning ? stderr : stdout;
        if (level <= Level::warning && is_terminal(out))
            std::fputs(std::format("{}\x1b[31m{}\x1b[0m {}\n", stamp, tag, msg).c_str(), out);
        else
            std::fputs(line.c_str(), out);
        std::fflush(out);
        if (g_file) g_file << line << std::flush;
    }
    if (level <= Level::warning)
        if (const auto sink = g_sink.load()) sink(level, line);
}

}  // namespace d2d::log
