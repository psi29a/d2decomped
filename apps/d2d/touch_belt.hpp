// SPDX-License-Identifier: GPL-3.0-or-later
// Stock remaining in the four native belt columns, including deeper rows.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace d2d::client::touch {

class BeltStock {
public:
    void add(int box) {
        if (box < 0 || box >= 16) return;
        const auto mask = std::uint16_t(1u << unsigned(box));
        if ((occupied_ & mask) != 0) return;
        occupied_ |= mask;
        ++columns_[std::size_t(box % 4)];
    }

    [[nodiscard]] int count(std::size_t column) const {
        return column < columns_.size() ? columns_[column] : 0;
    }

private:
    std::array<int, 4> columns_{};
    std::uint16_t occupied_ = 0;
};

} // namespace d2d::client::touch
