// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A raised skeleton's or skeletal mage's look, its element and the owner's
// seed after Raise's FUN_005c4430, against stock 1.14d run by
// tools/emu/raised_look.py (raised_look_cases.inc): rules::raised_look.
// No game data needed.
#include <rules.hpp>
#include <skills.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

namespace {

struct Case {
    int mage, mastery, level, par1;
    std::uint32_t low, high;
    const char* look;
    const char* after;
    std::uint32_t low_after, high_after;
};

constexpr Case kCases[] = {
#include "raised_look_cases.inc"
};

std::array<std::uint8_t, 16> bytes(const std::string& hex) {
    std::array<std::uint8_t, 16> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::uint8_t(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    return out;
}

}   // namespace

int main() try {
    int bad = 0;
    for (const auto& one : kCases) {
        d2d::rules::Rng owner;
        owner.low = one.low;
        owner.high = one.high;
        const auto raised = d2d::rules::raised_look(bytes(one.look), one.mage != 0, one.mastery, one.level, one.par1, owner);
        const auto want = bytes(one.after);
        const bool element_ok = !one.mage || (raised.element == want[11] && raised.element == want[12]);
        if (raised.look != want || owner.low != one.low_after || owner.high != one.high_after || !element_ok) {
            if (++bad <= 10)
                std::printf("FAIL mage %d mastery %d level %d par1 %d seed %u/%u: seed %u/%u, game.exe %u/%u\n", one.mage, one.mastery, one.level,
                            one.par1, one.low, one.high, owner.low, owner.high, one.low_after, one.high_after);
        }
    }
    std::printf("%zu cases, %d wrong\n", std::size(kCases), bad);
    return bad == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
