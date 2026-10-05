// SPDX-License-Identifier: GPL-3.0-or-later
// The net log: every packet either way, one line each, stamped with the
// UTC time (ISO 8601, ms) like d2d.log, so a session can be read back
// against the host's clock. Each S->C line says how far d2d understands
// the id:
//   used     d2d acts on it
//   docs     its layout is written up (net-packets.md), d2d doesn't use it yet
//   UNKNOWN  nobody has looked at it
// and the summary counts them, the unknown ones first.
#pragma once

#include <d2gs/wire.hpp>

#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

namespace d2d::net {

class NetLog {
public:
    // `used`: the S->C ids the caller acts on. `append`: add to the file
    // (one file for several sessions) instead of starting it over.
    NetLog(const std::filesystem::path& path, std::bitset<256> used, bool append = false);

    auto is_open() const -> bool { return file_.is_open(); }
    auto from_host(std::span<const std::uint8_t> packet) -> void;
    auto to_host(std::span<const std::uint8_t> packet) -> void;
    auto note(std::string_view text) -> void;
    // Packets by id, the unknown ones first; also written to the log.
    auto summary() -> std::string;
    // How many S->C packets had an id nobody has looked at.
    auto unknown_count() const -> std::uint64_t;

private:
    auto line(std::string_view direction, std::string_view tag, std::span<const std::uint8_t> packet) -> void;

    std::ofstream file_;
    std::bitset<256> used_;
    std::array<std::uint64_t, 256> from_host_{};
    std::array<std::uint64_t, 256> to_host_{};
};

// "2026-10-04T19:53:52.280Z": now in UTC, to the millisecond.
auto utc_stamp() -> std::string;

} // namespace d2d::net
