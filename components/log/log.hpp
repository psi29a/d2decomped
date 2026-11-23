// SPDX-License-Identifier: GPL-3.0-or-later
// Logging, after ../opendf's DF::Log: each line, stamped with the UTC time
// (ISO 8601, ms), goes to stdout (warnings and errors to stderr) and to
// the log file once one is open (log_file.hpp). Init sections read like
// ../thirdeye's ("Initializing X...", details indented under it). Five
// levels, error .. trace; lines below the level (info by default) are
// dropped. On a terminal the [ERROR] / [WARNING] tag is red; the file gets
// plain text. The clock, the file and the terminal are log.cpp's: this
// header is in most of the tree.
#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace d2d::log {

enum class Level { error, warning, info, debug, trace };
using Sink = void (*)(Level, std::string_view);

// ms since launch — also for "done (N ms)" timings.
long long ms();

void set_level(Level level);
// Called with each warning and error line as written.
void set_sink(Sink sink);
bool enabled(Level level);
// Windows colours only once the app has turned on VT processing for its
// console (SetConsoleMode) and said so here.
void set_windows_vt(bool processing);

// "error", "warning" (or "warn"), "info", "debug", "trace".
inline std::optional<Level> parse_level(std::string_view name) {
    if (name == "error") return Level::error;
    if (name == "warning" || name == "warn") return Level::warning;
    if (name == "info") return Level::info;
    if (name == "debug") return Level::debug;
    if (name == "trace") return Level::trace;
    return std::nullopt;
}

// One line at `level`, `tag` before it ("" for none), if the level is on.
void write(Level level, std::string_view tag, const std::string& msg);

template <class... A> void error(std::format_string<A...> format, A&&... args) {
    write(Level::error, "[ERROR]", std::format(format, std::forward<A>(args)...));
}
template <class... A> void warn(std::format_string<A...> format, A&&... args) {
    write(Level::warning, "[WARNING]", std::format(format, std::forward<A>(args)...));
}
template <class... A> void info(std::format_string<A...> format, A&&... args) {
    write(Level::info, "", std::format(format, std::forward<A>(args)...));
}
template <class... A> void debug(std::format_string<A...> format, A&&... args) {
    if (enabled(Level::debug)) write(Level::debug, "[DEBUG]", std::format(format, std::forward<A>(args)...));
}
template <class... A> void trace(std::format_string<A...> format, A&&... args) {
    if (enabled(Level::trace)) write(Level::trace, "[TRACE]", std::format(format, std::forward<A>(args)...));
}

}  // namespace d2d::log
