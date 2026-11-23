// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// FUN_0042f6e0's three records in each palette branch.

#include <frontend/connecting_rules.hpp>

#include <cassert>
#include <cstdio>

int main() {
    const auto sky = d2d::client::connecting_screen(false);
    assert(sky.box_record == 0xde && sky.cancel_record == 0xdf && sky.text_record == 0xe0);
    assert(sky.box_x == 268 && sky.box_bottom == 350 && sky.box_width == 264 && sky.box_height == 176);
    assert(sky.text_x == 268 && sky.text_bottom == 300 && sky.text_width == 264 && sky.text_height == 100);
    assert(sky.font == 24 && sky.alignment == 2 && sky.string_id == 0x147b);

    const auto character = d2d::client::connecting_screen(true);
    assert(character.box_record == 0xf7 && character.cancel_record == 0xfa && character.text_record == 0xf9);
    assert(character.box_x == 268 && character.box_bottom == 350 && character.box_width == 264 && character.box_height == 176);
    assert(character.text_x == 268 && character.text_bottom == 320 && character.text_width == 264 && character.text_height == 120);
    assert(character.font == 24 && character.alignment == 2 && character.string_id == 0x147b);
    std::puts("connecting screen: ok");
}
