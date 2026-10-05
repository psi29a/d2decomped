// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_log.hpp"

#include <d2gs/c2s_names.hpp>
#include <d2gs/s2c_names.hpp>

#include <chrono>
#include <format>
#include <ios>

namespace d2d::net {

auto utc_stamp() -> std::string {
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
}

NetLog::NetLog(const std::filesystem::path& path, std::bitset<256> used, bool append)
    : file_(path, append ? std::ios::app : std::ios::trunc), used_(used) {}

auto NetLog::line(std::string_view direction, std::string_view tag, std::span<const std::uint8_t> packet) -> void {
    if (!file_ || packet.empty()) return;
    const auto name = direction == "S>C" ? d2gs::s2c_name(packet[0]) : d2gs::c2s_name(packet[0]);
    std::string hex;
    hex.reserve(packet.size() * 3);
    for (const auto byte : packet) hex += std::format(" {:02x}", byte);
    file_ << std::format("{} {} {:02x} {:<7} {:<22} {:>4}{}\n", utc_stamp(), direction, packet[0], tag, name, packet.size(), hex) << std::flush;
}

auto NetLog::from_host(std::span<const std::uint8_t> packet) -> void {
    if (packet.empty()) return;
    const auto id = packet[0];
    ++from_host_[id];
    line("S>C", used_[id] ? "used" : !d2gs::s2c_name(id).empty() ? "docs" : "UNKNOWN", packet);
}

auto NetLog::to_host(std::span<const std::uint8_t> packet) -> void {
    if (packet.empty()) return;
    ++to_host_[packet[0]];
    line("C>S", d2gs::c2s_name(packet[0]).empty() ? "UNKNOWN" : "", packet);
}

auto NetLog::note(std::string_view text) -> void {
    if (file_) file_ << utc_stamp() << " -- " << text << '\n' << std::flush;
}

auto NetLog::unknown_count() const -> std::uint64_t {
    std::uint64_t count = 0;
    for (std::size_t id = 0; id < 256; ++id)
        if (d2gs::s2c_name(static_cast<std::uint8_t>(id)).empty()) count += from_host_[id];
    return count;
}

auto NetLog::summary() -> std::string {
    std::string out = "S>C packets by id (unknown, then docs, then used):\n";
    for (const auto* wanted : { "UNKNOWN", "docs", "used" })
        for (std::size_t id = 0; id < 256; ++id) {
            if (!from_host_[id]) continue;
            const auto name = d2gs::s2c_name(static_cast<std::uint8_t>(id));
            const std::string_view tag = used_[id] ? "used" : !name.empty() ? "docs" : "UNKNOWN";
            if (tag == wanted) out += std::format("  {:02x} {:<7} {:<22} x{}\n", id, tag, name, from_host_[id]);
        }
    out += "C>S packets by id:\n";
    for (std::size_t id = 0; id < 256; ++id)
        if (to_host_[id]) out += std::format("  {:02x} x{}\n", id, to_host_[id]);
    if (file_) file_ << out << std::flush;
    return out;
}

} // namespace d2d::net
