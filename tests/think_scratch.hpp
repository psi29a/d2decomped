// SPDX-License-Identifier: GPL-3.0-or-later
// A ThinkIn's scratch pointers, every one pointed at a local. mon_think's AIs
// write them unguarded (each AI its own), and a checker can't tell which AI a
// name picks (Coverity: "explicit null dereferenced"), so a think test fills
// them all: ThinkScratch scratch; scratch.point(input);
#pragma once

#include <monsters.hpp>

#include <vector>

struct ThinkScratch {
    int state = 0, state2 = 0, state3 = 0, pace = 0, command = 0, steps = 0, cast_word = 0;
    bool rally = false;
    std::vector<d2d::rules::ThinkEffect> effects;

    // Only the null ones: a test's own scratch stays.
    void point(d2d::rules::ThinkIn& input) {
        if (!input.state) input.state = &state;
        if (!input.state2) input.state2 = &state2;
        if (!input.state3) input.state3 = &state3;
        if (!input.pace) input.pace = &pace;
        if (!input.command) input.command = &command;
        if (!input.steps) input.steps = &steps;
        if (!input.cast_word) input.cast_word = &cast_word;
        if (!input.rally) input.rally = &rally;
        if (!input.effects) input.effects = &effects;
    }
};
