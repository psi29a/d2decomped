// SPDX-License-Identifier: GPL-3.0-or-later
// Which IPv4 peers a listener lets in (apps/d2proxy's --allow): addresses
// and a.b.c.d/bits ranges.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::net {

// An IPv4 range: "a.b.c.d" or "a.b.c.d/bits".
struct Range {
    std::uint32_t network = 0, mask = 0;
};

inline auto parse_ipv4(std::string_view text) -> std::optional<std::uint32_t> {
    std::uint32_t value = 0;
    int parts = 0;
    while (!text.empty()) {
        const auto dot = text.find('.');
        const auto part = text.substr(0, dot);
        if (part.empty() || part.size() > 3 || part.find_first_not_of("0123456789") != std::string_view::npos) return std::nullopt;
        const int octet = std::stoi(std::string(part));
        if (octet > 255) return std::nullopt;
        value = value << 8 | std::uint32_t(octet);
        ++parts;
        if (dot == std::string_view::npos) break;
        text.remove_prefix(dot + 1);
    }
    return parts == 4 ? std::optional<std::uint32_t>{ value } : std::nullopt;
}

inline auto parse_range(std::string_view text) -> std::optional<Range> {
    const auto slash = text.find('/');
    const auto address = parse_ipv4(text.substr(0, slash));
    int bits = 32;
    if (slash != std::string_view::npos) {
        const auto tail = text.substr(slash + 1);
        if (tail.empty() || tail.size() > 2 || tail.find_first_not_of("0123456789") != std::string_view::npos) return std::nullopt;
        bits = std::stoi(std::string(tail));
    }
    if (!address || bits < 0 || bits > 32) return std::nullopt;
    const std::uint32_t mask = bits == 0 ? 0 : 0xffffffffu << (32 - bits);
    return Range{ *address & mask, mask };
}

inline auto allowed(const std::vector<Range>& ranges, const std::string& peer) -> bool {
    const auto address = parse_ipv4(peer);
    return address && std::ranges::any_of(ranges, [&](const Range& range) { return (*address & range.mask) == range.network; });
}

} // namespace d2d::net
