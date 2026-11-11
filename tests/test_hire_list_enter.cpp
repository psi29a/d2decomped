// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Enter on the hire list (FUN_004bebc0, key 0xd) hires the row kept at
// +0x180738, the one under the mouse. No row, or arg -1 (string 0xd25),
// hires nothing.
#include <hire_list.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>

namespace {

struct HireRow { int arg = -1; };

}  // namespace

int main() try {
    const std::array<HireRow, 3> rows{ HireRow{ 4 }, HireRow{ -1 }, HireRow{ 2 } };
    static_assert(d2d::rules::list_enter_arg(0, std::array{ HireRow{ 4 }, HireRow{ -1 }, HireRow{ 2 } }) == 4);
    assert(d2d::rules::list_enter_arg(0, rows) == 4);
    assert(d2d::rules::list_enter_arg(1, rows) == -1);          // empty offer (0xd25)
    assert(d2d::rules::list_enter_arg(2, rows) == 2);
    assert(d2d::rules::list_enter_arg(-1, rows) == -1);         // no row under the mouse
    assert(d2d::rules::list_enter_arg(3, rows) == -1);
    const std::array<HireRow, 0> none{};
    assert(d2d::rules::list_enter_arg(0, none) == -1);
    std::printf("OK: Enter hires the row under the mouse\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hire_list_enter: %s\n", error.what());
    return 1;
}
