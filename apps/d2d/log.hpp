// Logging, after ../opendf's DF::Log: each line goes to stdout (warnings
// and errors to stderr) and, stamped with the time since launch, to
// <user dir>/d2d.log. Init sections read like ../thirdeye's
// ("Initializing X...", details indented under it).
#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
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

inline void open(const std::filesystem::path& p) {
    std::lock_guard lk(g_mutex);
    g_file.open(p, std::ios::trunc);
    if (!g_file) std::fprintf(stderr, "Failed to open log file %s\n", p.string().c_str());
}

inline void write(std::FILE* out, const char* level, const std::string& msg) {
    std::lock_guard lk(g_mutex);
    std::fprintf(out, "%s%s\n", level, msg.c_str());
    std::fflush(out);
    const auto t = ms();
    if (g_file) g_file << std::format("{:>4}.{:03} {}{}\n", t / 1000, t % 1000, level, msg) << std::flush;
}

template <class... A> void info(std::format_string<A...> f, A&&... a) {
    write(stdout, "", std::format(f, std::forward<A>(a)...));
}
template <class... A> void warn(std::format_string<A...> f, A&&... a) {
    write(stderr, "warning: ", std::format(f, std::forward<A>(a)...));
}
template <class... A> void error(std::format_string<A...> f, A&&... a) {
    write(stderr, "error: ", std::format(f, std::forward<A>(a)...));
}

}  // namespace d2d::log
