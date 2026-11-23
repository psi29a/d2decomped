// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace d2d::client::touch {

// Touch's persistent badge follows native notifications, independently of
// the legacy button's open/close state. Only displaying an entry reviews it.
class QuestNotice {
public:
    void notify(int quest) {
        if (valid(quest)) unread_[std::size_t(quest)] = true;
    }
    void review(int quest) {
        if (valid(quest)) unread_[std::size_t(quest)] = false;
    }
    [[nodiscard]] bool active() const {
        return std::ranges::any_of(unread_, [](bool unread) { return unread; });
    }

private:
    static constexpr bool valid(int quest) { return quest > 0 && quest < 41; }
    std::array<bool, 41> unread_{};
};

} // namespace d2d::client::touch
