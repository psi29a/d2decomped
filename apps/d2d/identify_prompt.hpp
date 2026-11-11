// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gamepad_map.hpp"

#include <string>

namespace d2d::client {

/**
 * Describe Identify confirmation using only configured controller inputs. Missing bindings and
 * duplicate button labels are omitted; an invalid target retains the selection instruction.
 */
[[nodiscard]] inline std::string identify_prompt(const pad::Bindings& bindings, pad::Family family, bool valid) {
    const auto label_of = [&](pad::Action action) -> std::string {
        const auto& pair = bindings[action];
        const auto button = pair[0] != pad::Bindings::kNone ? pair[0] : pair[1];
        return button == pad::Bindings::kNone ? std::string{} : std::string(pad::label(family, button));
    };
    std::string prompt = "Choose an unidentified item";
    if (valid) {
        auto inputs = label_of(pad::Action::Primary);
        const auto interact = label_of(pad::Action::Interact);
        if (!interact.empty() && interact != inputs) inputs += (inputs.empty() ? "" : " / ") + interact;
        prompt = (inputs.empty() ? "" : inputs + " . ") + std::string("Identify item");
    }
    const auto cancel = label_of(pad::Action::Back);
    if (!cancel.empty()) prompt += " . " + cancel + " Cancel";
    return prompt;
}

} // namespace d2d::client
