// SPDX-License-Identifier: GPL-3.0-or-later
// The log file (log.hpp's lines go there too once it's open): apart, so
// only the apps that open one parse <filesystem> for it.
#pragma once

#include <filesystem>

namespace d2d::log {

// `append`: add to the file (a server's log across restarts) instead of
// starting it over.
void open(const std::filesystem::path& path, bool append = false);

}  // namespace d2d::log
