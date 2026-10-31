// SPDX-License-Identifier: GPL-3.0-or-later
// The `debug` verb's table: debug.hpp.
#include "debug.hpp"

#include "scene.hpp"
#include "town.hpp"
#include "ui.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <utility>

namespace d2d::client {

DebugVerbs::DebugVerbs(CharCreateUI& character, Town& town, std::optional<Scene>& scene)
    : character(character), town(town), scene(scene) {
    add_fight();
    add_hero();
    add_level();
    add_net();
}

void DebugVerbs::add(const std::string& name, Verb verb) {
    [[maybe_unused]] const bool added = verbs_.emplace(name, std::move(verb)).second;
    assert(added && "a debug sub-verb added twice");
}

std::string DebugVerbs::run(const Args& args) const {
    if (args.size() >= 2) {
        if (const auto verb = verbs_.find(args[1]); verb != verbs_.end()) {
            if (auto reply = verb->second(args)) return *std::move(reply);
        }
    }
    return "err debug collision|automap|portals|objects|net|statpts <n>|skillpts <n>|wear|unid|level|blocked <x> <y>|warp <x> <y>|stat <id> <v>|quest <q>|skill left|right <id>|points <id> <n>\n";
}

}  // namespace d2d::client
