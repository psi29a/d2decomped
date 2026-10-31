// SPDX-License-Identifier: GPL-3.0-or-later
// The devctl `debug` verb (docs/control_channel.md): its sub-verbs, one
// handler each, kept by name. Each area adds its own in its own file, in
// name order (debug_fight.cpp, _hero, _level, _net), so two changes
// that each add a sub-verb don't touch the same lines.
#pragma once

#include "scene.hpp"
#include "town.hpp"
#include "ui.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace d2d::client {

class DebugVerbs {
public:
    using Args = std::vector<std::string>;
    // nullopt: the arguments don't fit the sub-verb (too few, or no level or
    // scene yet); the reply is then the usage line.
    using Reply = std::optional<std::string>;
    using Verb = std::function<Reply(const Args&)>;

    // The references are main()'s loop locals; the channel that holds this
    // is pumped only inside that loop.
    DebugVerbs(CharCreateUI& character, Town& town, std::optional<Scene>& scene);
    DebugVerbs(const DebugVerbs&) = delete;
    DebugVerbs& operator=(const DebugVerbs&) = delete;
    DebugVerbs(DebugVerbs&&) = delete;
    DebugVerbs& operator=(DebugVerbs&&) = delete;
    ~DebugVerbs() = default;

    // `debug <sub-verb> ...`: the sub-verb's reply, else the usage line.
    [[nodiscard]] std::string run(const Args& args) const;

private:
    void add(const std::string& name, Verb verb);
    void add_fight();
    void add_hero();
    void add_level();
    void add_net();

    std::map<std::string, Verb, std::less<>> verbs_;
    CharCreateUI& character;
    Town& town;
    std::optional<Scene>& scene;
};

}  // namespace d2d::client
