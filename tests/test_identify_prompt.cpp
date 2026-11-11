// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
#include "gamepad_map.hpp"
#include "identify_prompt.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <string>

/** Verify the actual Identify prompt for classic, remapped, unbound and invalid-target cases. */
int main() try {
    using namespace d2d::client;
    pad::Bindings bindings;
    assert(identify_prompt(bindings, pad::Family::Xbox, true) == "A . Identify item . B Cancel");
    bindings = pad::skill_buttons_config({}).bindings;
    assert(identify_prompt(bindings, pad::Family::Xbox, true) == "A / RS . Identify item . B Cancel");
    bindings[pad::Action::Interact] = { pad::Bindings::kNone, pad::Button::West };
    assert(identify_prompt(bindings, pad::Family::Xbox, true) == "A / X . Identify item . B Cancel");
    assert(identify_prompt(bindings, pad::Family::PlayStation, true) == "Cross / Square . Identify item . Circle Cancel");
    assert(identify_prompt(bindings, pad::Family::Xbox, false) == "Choose an unidentified item . B Cancel");
    bindings[pad::Action::Primary] = { pad::Bindings::kNone, pad::Bindings::kNone };
    assert(identify_prompt(bindings, pad::Family::Xbox, true) == "X . Identify item . B Cancel");
    bindings[pad::Action::Primary] = bindings[pad::Action::Interact];
    assert(identify_prompt(bindings, pad::Family::Xbox, true) == "X . Identify item . B Cancel");
    bindings[pad::Action::Primary] = bindings[pad::Action::Interact] = bindings[pad::Action::Back] =
        { pad::Bindings::kNone, pad::Bindings::kNone };
    assert(identify_prompt(bindings, pad::Family::Xbox, true) == "Identify item");
    assert(identify_prompt(bindings, pad::Family::Xbox, false).find('-') == std::string::npos);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_identify_prompt: %s\n", error.what());
    return 1;
}
