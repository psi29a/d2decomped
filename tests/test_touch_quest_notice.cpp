// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
#include "touch_quest_notice.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    d2d::client::touch::QuestNotice notices;
    assert(!notices.active());
    // Reviewing one entry cannot clear another quest's update.
    notices.notify(1);
    notices.notify(26);
    notices.review(1);
    assert(notices.active());
    notices.review(9);
    assert(notices.active());
    notices.review(26);
    assert(!notices.active());
    // A later update to an already reviewed quest brings the badge back.
    notices.notify(1);
    assert(notices.active());
    notices.notify(1);
    notices.review(1);
    assert(!notices.active());
    // Invalid notification and review IDs cannot alter or index the state.
    notices.notify(-1);
    notices.notify(0);
    notices.notify(41);
    assert(!notices.active());
    notices.notify(40);
    notices.review(41);
    notices.review(-1);
    assert(notices.active());
    notices.review(40);
    assert(!notices.active());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_quest_notice: %s\n", error.what());
    return 1;
}
